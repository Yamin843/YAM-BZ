package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

import qing.albatross.core.Albatross;
import qing.albatross.search.SearchClassCallback;

/**
 * ClassBridge — تعداد كلاسات العملية + member lookup.
 *
 * التدفق:
 *  1. startEnum() — يبدأ thread يعمل Albatross.searchClass (native)
 *  2. nextBatch(n) — يستدعى متكرراً من C++ لسحب دفعات
 *  3. isComplete() — عند اكتمال التعداد
 *  4. getMembers(className) — أعضاء كلاس معين (methods + fields)
 *
 * كل شيء thread-safe، لا stubs، يستخدم Albatross مباشرة.
 */
public final class ClassBridge {
    private static final String TAG = "YAMBZ";

    private static final Deque<String> sQueue = new ArrayDeque<>();
    private static final List<String> sAllClasses = new ArrayList<>();
    private static volatile boolean sEnumerating = false;
    private static volatile boolean sComplete = false;
    private static volatile int sTotal = 0;

    private ClassBridge() {}

    // ══════════════════════════════════════════════════════════
    //  Enumeration
    // ══════════════════════════════════════════════════════════
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
                    String name = clz.getName();
                    synchronized (ClassBridge.class) {
                        sQueue.add(name);
                        sAllClasses.add(name);
                        sTotal++;
                    }
                    return SearchClassCallback.CONTINUE;
                }, SearchClassCallback.SCOPE_ALL);
            } catch (Throwable t) {
                Log.e(TAG, "searchClass failed", t);
            } finally {
                sComplete = true;
                sEnumerating = false;
                Log.i(TAG, "ClassBridge: enum done, " + sTotal + " classes in "
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

    // ══════════════════════════════════════════════════════════
    //  Members of a class (uses Albatross native — bypass hidden API)
    // ══════════════════════════════════════════════════════════
    public static String[] getMembers(String className) {
        List<String> out = new ArrayList<>();
        try {
            Class<?> cls = findClass(className);
            if (cls == null) return new String[0];

            // Methods (native bypass)
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
                    sb.append('|');
                    sb.append(m.getReturnType().getName());
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
            Log.e(TAG, "getMembers(" + className + ") failed", t);
        }
        return out.toArray(new String[0]);
    }

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
