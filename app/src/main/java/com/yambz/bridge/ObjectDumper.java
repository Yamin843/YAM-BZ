package com.yambz.bridge;

import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Collection;
import java.util.IdentityHashMap;
import java.util.Map;

/**
 * مُفرِّغ شامل — يدعم كل أنواع Java:
 *   ✓ primitives + wrappers
 *   ✓ String / CharSequence
 *   ✓ Arrays (متعددة الأبعاد)
 *   ✓ Collections / Maps (بلا حد عملي)
 *   ✓ Custom classes (private/hidden/final fields via Unsafe)
 *   ✓ Enums
 *   ✓ Interfaces (dump كائن فعلي)
 *   ✓ Annotations
 *   ✓ Inner classes (outer ref)
 *   ✓ Hidden API (Android 9+) — عبر Unsafe
 *
 * حماية:
 *   - Cycle detection (IdentityHashMap)
 *   - Time budget (افتراضي 50ms)
 *   - Byte budget (افتراضي 16KB)
 *   - Depth limit (افتراضي 8)
 *   - Element limit (افتراضي 50,000)
 */
public final class ObjectDumper {

    public static final class Options {
        public int maxDepth = 8;
        public int maxElements = 50_000;
        public int maxString = 4_000;
        public int timeBudgetMs = 50;
        public int byteBudget = 16_384;
        public boolean showFieldTypes = false;
        public boolean includeStatic = false;
        public Options copy() {
            Options o = new Options();
            o.maxDepth = maxDepth;
            o.maxElements = maxElements;
            o.maxString = maxString;
            o.timeBudgetMs = timeBudgetMs;
            o.byteBudget = byteBudget;
            o.showFieldTypes = showFieldTypes;
            o.includeStatic = includeStatic;
            return o;
        }
    }

    private ObjectDumper() {}

    // ─── Unsafe (lazy init) ───
    private static volatile boolean sUnsafeInit = false;
    private static Object sUnsafe;
    private static Method sUnsafeFieldOffset;
    private static Method sUnsafeGetObject, sUnsafeGetInt, sUnsafeGetLong;
    private static Method sUnsafeGetBoolean, sUnsafeGetByte, sUnsafeGetShort;
    private static Method sUnsafeGetChar, sUnsafeGetFloat, sUnsafeGetDouble;

    private static synchronized void initUnsafe() {
        if (sUnsafeInit) return;
        sUnsafeInit = true;
        try {
            Class<?> cls = Class.forName("sun.misc.Unsafe");
            Field f = cls.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            sUnsafe = f.get(null);
            sUnsafeFieldOffset = cls.getMethod("objectFieldOffset", Field.class);
            sUnsafeGetObject  = cls.getMethod("getObject",  Object.class, long.class);
            sUnsafeGetInt     = cls.getMethod("getInt",     Object.class, long.class);
            sUnsafeGetLong    = cls.getMethod("getLong",    Object.class, long.class);
            sUnsafeGetBoolean = cls.getMethod("getBoolean", Object.class, long.class);
            sUnsafeGetByte    = cls.getMethod("getByte",    Object.class, long.class);
            sUnsafeGetShort   = cls.getMethod("getShort",   Object.class, long.class);
            sUnsafeGetChar    = cls.getMethod("getChar",    Object.class, long.class);
            sUnsafeGetFloat   = cls.getMethod("getFloat",   Object.class, long.class);
            sUnsafeGetDouble  = cls.getMethod("getDouble",  Object.class, long.class);
        } catch (Throwable t) {
            sUnsafe = null; // unavailable
        }
    }

    // ─── Budget tracker ───
    private static final class Budget {
        final long deadlineNanos;
        final int  byteLimit;
        int        bytesWritten;
        boolean    exhausted;
        Budget(int ms, int bytes) {
            deadlineNanos = System.nanoTime() + ms * 1_000_000L;
            byteLimit = bytes;
        }
        boolean check() {
            if (exhausted) return false;
            if (System.nanoTime() > deadlineNanos) { exhausted = true; return false; }
            return true;
        }
        boolean write(int n) {
            bytesWritten += n;
            if (bytesWritten > byteLimit) { exhausted = true; return false; }
            return true;
        }
    }

    // ─── public ───
    public static String dump(Object obj) { return dump(obj, new Options()); }

    public static String dump(Object obj, Options opts) {
        if (opts == null) opts = new Options();
        StringBuilder sb = new StringBuilder(256);
        Budget budget = new Budget(opts.timeBudgetMs, opts.byteBudget);
        try {
            dumpInternal(sb, obj, opts, new IdentityHashMap<>(), 0, budget);
        } catch (Throwable t) {
            sb.append("<dump-error:").append(t.getClass().getSimpleName()).append('>');
        }
        if (budget.exhausted) sb.append("…<truncated>");
        return sb.toString();
    }

    // ─── core ───
    private static void dumpInternal(StringBuilder sb, Object obj, Options opts,
                                     IdentityHashMap<Object, Boolean> visited,
                                     int depth, Budget budget) {
        if (!budget.check() || !budget.write(16)) return;
        if (obj == null) { sb.append("null"); return; }

        // primitives / common types
        if (obj instanceof String) {
            String s = (String) obj;
            sb.append('"');
            if (s.length() > opts.maxString) {
                sb.append(s, 0, opts.maxString).append("…");
            } else {
                sb.append(s);
            }
            sb.append('"');
            return;
        }
        if (obj instanceof CharSequence) {
            String s = obj.toString();
            if (s.length() > opts.maxString) s = s.substring(0, opts.maxString) + "…";
            sb.append('"').append(s).append('"');
            return;
        }
        if (obj instanceof Number || obj instanceof Boolean || obj instanceof Character) {
            sb.append(obj.toString());
            return;
        }

        Class<?> cls = obj.getClass();

        // enums
        if (cls.isEnum()) {
            sb.append(cls.getSimpleName()).append('.').append(((Enum<?>) obj).name());
            return;
        }

        // annotations
        if (cls.isAnnotation()) {
            dumpAnnotation(sb, obj, cls, opts, budget);
            return;
        }

        // cycle
        if (visited.containsKey(obj)) {
            sb.append("<cycle@").append(Integer.toHexString(System.identityHashCode(obj))).append('>');
            return;
        }

        if (depth > opts.maxDepth) {
            sb.append(cls.getSimpleName()).append('@')
              .append(Integer.toHexString(System.identityHashCode(obj)))
              .append("<max-depth>");
            return;
        }

        visited.put(obj, Boolean.TRUE);
        try {
            // arrays (multi-dim)
            if (cls.isArray()) {
                int len = Array.getLength(obj);
                Class<?> comp = cls.getComponentType();
                sb.append(comp.getSimpleName()).append('[').append(len).append("] {");
                int shown = 0;
                for (int i = 0; i < len && shown < opts.maxElements; i++, shown++) {
                    if (i > 0) sb.append(", ");
                    dumpInternal(sb, Array.get(obj, i), opts, visited, depth + 1, budget);
                }
                if (shown < len) sb.append(", …(").append(len - shown).append(" more)");
                sb.append('}');
                return;
            }

            // Collection
            if (obj instanceof Collection) {
                Collection<?> c = (Collection<?>) obj;
                sb.append(cls.getSimpleName()).append('(').append(c.size()).append(") [");
                int shown = 0;
                for (Object e : c) {
                    if (shown >= opts.maxElements) {
                        sb.append(", …(").append(c.size() - shown).append(" more)");
                        break;
                    }
                    if (shown > 0) sb.append(", ");
                    dumpInternal(sb, e, opts, visited, depth + 1, budget);
                    shown++;
                    if (!budget.check()) return;
                }
                sb.append(']');
                return;
            }

            // Map
            if (obj instanceof Map) {
                Map<?, ?> m = (Map<?, ?>) obj;
                sb.append(cls.getSimpleName()).append('(').append(m.size()).append(") {");
                int shown = 0;
                for (Map.Entry<?, ?> e : m.entrySet()) {
                    if (shown >= opts.maxElements) {
                        sb.append(", …(").append(m.size() - shown).append(" more)");
                        break;
                    }
                    if (shown > 0) sb.append(", ");
                    dumpInternal(sb, e.getKey(), opts, visited, depth + 1, budget);
                    sb.append("=");
                    dumpInternal(sb, e.getValue(), opts, visited, depth + 1, budget);
                    shown++;
                    if (!budget.check()) return;
                }
                sb.append('}');
                return;
            }

            // custom object
            sb.append(cls.getName()).append('@')
              .append(Integer.toHexString(System.identityHashCode(obj)));

            // collect all fields (superclass chain, incl. hidden)
            java.util.List<Field> fields = new java.util.ArrayList<>();
            for (Class<?> c = cls; c != null && c != Object.class; c = c.getSuperclass()) {
                for (Field f : c.getDeclaredFields()) {
                    if (!opts.includeStatic && Modifier.isStatic(f.getModifiers())) continue;
                    if (f.isSynthetic()) continue;
                    fields.add(f);
                }
            }

            if (!fields.isEmpty()) {
                sb.append(" {");
                boolean first = true;
                for (Field f : fields) {
                    if (!budget.check()) return;
                    if (!first) sb.append(", ");
                    first = false;
                    sb.append(f.getName());
                    if (opts.showFieldTypes) sb.append(':').append(f.getType().getSimpleName());
                    sb.append('=');
                    Object v = readField(obj, f);
                    dumpInternal(sb, v, opts, visited, depth + 1, budget);
                }
                sb.append('}');
            }
        } finally {
            visited.remove(obj);
        }
    }

    // ─── readField: reflection → Unsafe fallback ───
    private static Object readField(Object obj, Field f) {
        boolean isStatic = Modifier.isStatic(f.getModifiers());
        try {
            f.setAccessible(true);
            return f.get(isStatic ? null : obj);
        } catch (Throwable t) {
            // hidden API blocked
        }
        if (!sUnsafeInit) initUnsafe();
        if (sUnsafe == null) return "<inaccessible>";
        try {
            Object target = isStatic ? sUnsafe : obj;
            long off = (Long) sUnsafeFieldOffset.invoke(sUnsafe, f);
            Class<?> ty = f.getType();
            if (ty == int.class)     return sUnsafeGetInt.invoke(sUnsafe, target, off);
            if (ty == long.class)    return sUnsafeGetLong.invoke(sUnsafe, target, off);
            if (ty == boolean.class) return sUnsafeGetBoolean.invoke(sUnsafe, target, off);
            if (ty == byte.class)    return sUnsafeGetByte.invoke(sUnsafe, target, off);
            if (ty == short.class)   return sUnsafeGetShort.invoke(sUnsafe, target, off);
            if (ty == char.class)    return sUnsafeGetChar.invoke(sUnsafe, target, off);
            if (ty == float.class)   return sUnsafeGetFloat.invoke(sUnsafe, target, off);
            if (ty == double.class)  return sUnsafeGetDouble.invoke(sUnsafe, target, off);
            return sUnsafeGetObject.invoke(sUnsafe, target, off);
        } catch (Throwable t) {
            return "<unsafe-fail:" + t.getClass().getSimpleName() + ">";
        }
    }

    // ─── Annotation dump ───
    private static void dumpAnnotation(StringBuilder sb, Object a, Class<?> cls, Options opts, Budget budget) {
        sb.append('@').append(cls.getName()).append('(');
        boolean first = true;
        for (Method m : cls.getDeclaredMethods()) {
            if (m.getParameterCount() != 0) continue;
            try {
                Object v = m.invoke(a);
                if (!first) sb.append(", ");
                first = false;
                sb.append(m.getName()).append('=');
                dumpInternal(sb, v, opts, new IdentityHashMap<>(), 1, budget);
            } catch (Throwable ignore) {}
        }
        sb.append(')');
    }
}
