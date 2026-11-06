package com.yambz.bridge;

import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.Collection;
import java.util.IdentityHashMap;
import java.util.Map;

/**
 * ObjectDumper — Safe object dump with cycle detection.
 */
public final class ObjectDumper {
    private static final int MAX_DEPTH = 8;
    private static final int MAX_ELEMENTS = 1000;
    private static final int MAX_STRING = 500;

    private ObjectDumper() {}

    public static String dump(Object obj) {
        StringBuilder sb = new StringBuilder(128);
        try {
            dumpInternal(sb, obj, new IdentityHashMap<>(), 0);
        } catch (Throwable t) {
            sb.append("<dump-error>");
        }
        return sb.toString();
    }

    private static void dumpInternal(StringBuilder sb, Object obj,
                                      IdentityHashMap<Object, Boolean> visited, int depth) {
        if (obj == null) { sb.append("null"); return; }
        if (depth > MAX_DEPTH) { sb.append("..."); return; }

        Class<?> c = obj.getClass();
        String name = c.getName();

        if (obj instanceof String) {
            String s = (String) obj;
            if (s.length() > MAX_STRING) s = s.substring(0, MAX_STRING) + "...";
            sb.append('"').append(s).append('"');
            return;
        }
        if (obj instanceof CharSequence) {
            sb.append('"').append(obj.toString()).append('"');
            return;
        }
        if (obj instanceof Number || obj instanceof Boolean || obj instanceof Character) {
            sb.append(obj.toString());
            return;
        }
        if (c.isEnum()) {
            sb.append(((Enum<?>) obj).name());
            return;
        }
        if (visited.containsKey(obj)) {
            sb.append("<cycle>");
            return;
        }
        visited.put(obj, Boolean.TRUE);

        try {
            if (c.isArray()) {
                int n = Array.getLength(obj);
                sb.append('[');
                int show = Math.min(n, MAX_ELEMENTS);
                for (int i = 0; i < show; i++) {
                    if (i > 0) sb.append(", ");
                    dumpInternal(sb, Array.get(obj, i), visited, depth + 1);
                }
                if (show < n) sb.append(", ...(").append(n - show).append(" more)");
                sb.append(']');
                return;
            }
            if (obj instanceof Collection) {
                Collection<?> col = (Collection<?>) obj;
                sb.append("[").append(col.size()).append("] {");
                int i = 0;
                for (Object e : col) {
                    if (i >= MAX_ELEMENTS) { sb.append(", ..."); break; }
                    if (i > 0) sb.append(", ");
                    dumpInternal(sb, e, visited, depth + 1);
                    i++;
                }
                sb.append('}');
                return;
            }
            if (obj instanceof Map) {
                Map<?, ?> m = (Map<?, ?>) obj;
                sb.append("{").append(m.size()).append("} {");
                int i = 0;
                for (Map.Entry<?, ?> e : m.entrySet()) {
                    if (i >= MAX_ELEMENTS) { sb.append(", ..."); break; }
                    if (i > 0) sb.append(", ");
                    dumpInternal(sb, e.getKey(), visited, depth + 1);
                    sb.append("=");
                    dumpInternal(sb, e.getValue(), visited, depth + 1);
                    i++;
                }
                sb.append('}');
                return;
            }

            // Object
            sb.append(c.getSimpleName()).append('@')
              .append(Integer.toHexString(System.identityHashCode(obj)));
            sb.append(" {");
            boolean first = true;
            for (Class<?> cc = c; cc != null && cc != Object.class; cc = cc.getSuperclass()) {
                for (Field f : cc.getDeclaredFields()) {
                    if (Modifier.isStatic(f.getModifiers())) continue;
                    if (!first) sb.append(", ");
                    first = false;
                    sb.append(f.getName()).append('=');
                    try {
                        f.setAccessible(true);
                        Object v = f.get(obj);
                        dumpInternal(sb, v, visited, depth + 1);
                    } catch (Throwable t) {
                        sb.append("<access>");
                    }
                }
            }
            sb.append('}');
        } finally {
            visited.remove(obj);
        }
    }
}
