package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

import qing.albatross.core.Albatross;
import qing.albatross.search.FieldCallback;
import qing.albatross.search.SearchCallback;
import qing.albatross.search.SearchClassCallback;

/**
 * ClassBridge — كل عمليات الفحص عبر Albatross.
 * - searchClass (تعداد)
 * - getDeclaredMethods (native، bypass hidden)
 * - searchField (find writers/readers)
 * - searchMethodCaller (find callers)
 * - searchSubClass
 */
public final class ClassBridge {
    private static final String TAG = "YAMBZ";

    private static final Deque<String> sQueue = new ArrayDeque<>();
    private static final List<String> sAllClasses = new ArrayList<>();
    private static volatile boolean sEnumerating = false;
    private static volatile boolean sComplete = false;
    private static volatile int sTotal = 0;

    private ClassBridge() {}

    // ──────────────────────────────────────────────────────────
    //  Enumeration
    // ──────────────────────────────────────────────────────────
    public static synchronized void startEnum() {
        if (sEnumerating) return;
        sEnumerating = true;
        sComplete = false;
        sQueue.clear();
        sAllClasses.clear();
        sTotal = 0;

        new Thread(() -> {
            long t0 = System.currentTimeMillis();
            try {
                Albatross.searchClass((SearchClassCallback<Object>) (clz, pos) -> {
                    synchronized (ClassBridge.class) {
                        sQueue.add(clz.getName());
                        sAllClasses.add(clz.getName());
                        sTotal++;
                    }
                    return SearchClassCallback.CONTINUE;
                }, SearchClassCallback.SCOPE_ALL);
            } catch (Throwable t) {
                Log.e(TAG, "searchClass failed", t);
            } finally {
                sComplete = true;
                sEnumerating = false;
                Log.i(TAG, "Enum done: " + sTotal + " classes in "
                    + (System.currentTimeMillis() - t0) + "ms");
            }
        }, "yambz-enum").start();
    }

    public static synchronized String[] nextBatch(int n) {
        int size = Math.min(n, sQueue.size());
        if (size == 0) return new String[0];
        String[] out = new String[size];
        for (int i = 0; i < size; i++) out[i] = sQueue.pollFirst();
        return out;
    }

    public static boolean isEnumerating() { return sEnumerating; }
    public static boolean isComplete() { return sComplete; }
    public static synchronized int getTotal() { return sTotal; }

    // ──────────────────────────────────────────────────────────
    //  Members
    // ──────────────────────────────────────────────────────────
    public static String[] getMembers(String className) {
        List<String> out = new ArrayList<>();
        try {
            Class<?> cls = findClass(className);
            if (cls == null) return new String[0];

            // Constructors
            try {
                for (java.lang.reflect.Constructor<?> c : cls.getDeclaredConstructors()) {
                    StringBuilder sb = new StringBuilder("C|");
                    sb.append(modStr(c.getModifiers())).append('|');
                    sb.append("<init>").append('|');
                    Class<?>[] params = c.getParameterTypes();
                    for (int i = 0; i < params.length; i++) {
                        if (i > 0) sb.append(',');
                        sb.append(params[i].getName());
                    }
                    sb.append('|').append(cls.getName());
                    out.add(sb.toString());
                }
            } catch (Throwable ignored) {}

            // Methods (native via Albatross)
            Method[] methods = Albatross.getDeclaredMethods(cls, Albatross.SEARCH_ALL);
            if (methods != null) {
                for (Method m : methods) {
                    if (m == null) continue;
                    StringBuilder sb = new StringBuilder("M|");
                    sb.append(modStr(m.getModifiers())).append('|');
                    sb.append(m.getName()).append('|');
                    Class<?>[] params = m.getParameterTypes();
                    for (int i = 0; i < params.length; i++) {
                        if (i > 0) sb.append(',');
                        sb.append(params[i].getName());
                    }
                    sb.append('|').append(m.getReturnType().getName());
                    out.add(sb.toString());
                }
            }

            // Fields
            Field[] fields = cls.getDeclaredFields();
            if (fields != null) {
                for (Field f : fields) {
                    if (f == null) continue;
                    StringBuilder sb = new StringBuilder("F|");
                    sb.append(modStr(f.getModifiers())).append('|');
                    sb.append(f.getName()).append('|');
                    sb.append(f.getType().getName());
                    out.add(sb.toString());
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "getMembers failed", t);
        }
        return out.toArray(new String[0]);
    }

    // ──────────────────────────────────────────────────────────
    //  Search sub-tabs (Albatross native search)
    // ──────────────────────────────────────────────────────────

    /** Search field writers/readers */
    public static String[] searchField(String className, String fieldName, int operation) {
        List<String> out = new ArrayList<>();
        try {
            Class<?> cls = findClass(className);
            if (cls == null) return new String[0];
            Field f = cls.getDeclaredField(fieldName);
            f.setAccessible(true);

            Albatross.searchField(f, operation,
                (member, op, pos) -> {
                    String opName = (op == FieldCallback.FIELD_GET) ? "READ" : "WRITE";
                    out.add(opName + "|" + member.toString() + "|dexPc=" + pos);
                    return out.size() < 500;
                }, false, true);
        } catch (Throwable t) {
            Log.e(TAG, "searchField failed", t);
        }
        return out.toArray(new String[0]);
    }

    /** Search method callers */
    public static String[] searchMethodCaller(String className, String methodName, String argTypesStr) {
        List<String> out = new ArrayList<>();
        try {
            Class<?> cls = findClass(className);
            if (cls == null) return new String[0];
            Class<?>[] argTypes = parseArgTypes(argTypesStr);
            Method m = cls.getDeclaredMethod(methodName, argTypes);
            m.setAccessible(true);

            Albatross.searchMethodCaller(m, (member, pos) -> {
                out.add(member.toString());
                return out.size() < 500;
            }, false, SearchClassCallback.SCOPE_ALL);
        } catch (Throwable t) {
            Log.e(TAG, "searchMethodCaller failed", t);
        }
        return out.toArray(new String[0]);
    }

    /** Search subclasses */
    public static String[] searchSubClass(String className) {
        List<String> out = new ArrayList<>();
        try {
            Class<?> cls = findClass(className);
            if (cls == null) return new String[0];

            Albatross.searchSubClass((subCls, pos) -> {
                out.add(subCls.getName());
                return out.size() < 500
                    ? SearchClassCallback.CONTINUE
                    : SearchClassCallback.STOP;
            }, cls, SearchClassCallback.SCOPE_ALL);
        } catch (Throwable t) {
            Log.e(TAG, "searchSubClass failed", t);
        }
        return out.toArray(new String[0]);
    }

    // ──────────────────────────────────────────────────────────
    //  Helpers
    // ──────────────────────────────────────────────────────────
    public static Class<?> findClass(String name) {
        try { return Class.forName(name, false, null); } catch (Throwable ignored) {}
        try { return Class.forName(name); } catch (Throwable ignored) {}
        try {
            for (ClassLoader cl : Albatross.getClassLoaderList()) {
                try { return Class.forName(name, false, cl); } catch (Throwable ignored) {}
            }
        } catch (Throwable ignored) {}
        return null;
    }

    private static Class<?>[] parseArgTypes(String spec) throws ClassNotFoundException {
        if (spec == null || spec.trim().isEmpty()) return new Class<?>[0];
        String[] parts = spec.split(",");
        Class<?>[] out = new Class<?>[parts.length];
        for (int i = 0; i < parts.length; i++) {
            String p = parts[i].trim();
            switch (p) {
                case "int": out[i] = int.class; break;
                case "long": out[i] = long.class; break;
                case "short": out[i] = short.class; break;
                case "byte": out[i] = byte.class; break;
                case "char": out[i] = char.class; break;
                case "boolean": out[i] = boolean.class; break;
                case "float": out[i] = float.class; break;
                case "double": out[i] = double.class; break;
                default:
                    Class<?> c = findClass(p);
                    if (c == null) throw new ClassNotFoundException(p);
                    out[i] = c;
            }
        }
        return out;
    }

    private static String modStr(int m) {
        StringBuilder sb = new StringBuilder();
        if (Modifier.isPublic(m))       sb.append("public ");
        if (Modifier.isPrivate(m))      sb.append("private ");
        if (Modifier.isProtected(m))    sb.append("protected ");
        if (Modifier.isStatic(m))       sb.append("static ");
        if (Modifier.isFinal(m))        sb.append("final ");
        if (Modifier.isAbstract(m))     sb.append("abstract ");
        if (Modifier.isNative(m))       sb.append("native ");
        if (Modifier.isSynchronized(m)) sb.append("synchronized ");
        return sb.toString().trim();
    }
}
