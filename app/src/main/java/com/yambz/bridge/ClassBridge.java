package com.yambz.bridge;

import android.util.Log;

import java.lang.ref.WeakReference;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import qing.albatross.core.Albatross;
import qing.albatross.search.SearchClassCallback;

/**
 * Java side of class enumeration. Called from C++ in batches.
 *
 * التصميم: queue آمن thread-safe. C++ يستدعي nextBatch() متكرراً حتى
 * يرجع مصفوفة فارغة = اكتمل.
 *
 * - count-first: نحاول أخذ snapshot سريع أولاً ثم نعدد
 * - batch-safe: كل استدعاء nextBatch يرجع ≤1000 عنصر (قابل للضبط من C++)
 * - منصة التنفيذ: Albatross.searchClass الذي يمر على كل ClassLoader
 *   الخاص بالتطبيق + platform
 */
public final class ClassBridge {
    private static final String TAG = "YAMBZ";
    private static volatile ClassBridge sInstance;

    public static ClassBridge getInstance() {
        if (sInstance == null) {
            synchronized (ClassBridge.class) {
                if (sInstance == null) sInstance = new ClassBridge();
            }
        }
        return sInstance;
    }

    private final Deque<String> mQueue = new ArrayDeque<>(16384);
    private final AtomicBoolean mEnumerating = new AtomicBoolean(false);
    private final AtomicBoolean mComplete = new AtomicBoolean(false);
    private volatile Throwable mError = null;

    private ClassBridge() {}

    /** C++ calls this repeatedly until empty */
    public synchronized String[] nextBatch(int maxSize) {
        // إن لم يبدأ التعداد، ابدأه الآن
        if (!mEnumerating.get() && mQueue.isEmpty() && !mComplete.get()) {
            startAsync();
        }

        if (mQueue.isEmpty()) {
            // إن لم يكتمل التعداد، انتظر قليلاً — C++ سيستدعي مجدداً
            return new String[0];
        }

        int n = Math.min(maxSize, mQueue.size());
        String[] out = new String[n];
        for (int i = 0; i < n; i++) out[i] = mQueue.pollFirst();
        return out;
    }

    private void startAsync() {
        if (!mEnumerating.compareAndSet(false, true)) return;
        mComplete.set(false);
        mError = null;
        Log.i(TAG, "ClassBridge: enumerating all classes");

        new Thread(() -> {
            try {
                // استخدم Albatross.searchClass (native traversal)
                Albatross.searchClass((SearchClassCallback<Object>) (clz, pos) -> {
                    try {
                        mQueue.add(clz.getName());
                    } catch (Throwable t) {
                        // تجاهل — قد يكون كلاس ميت
                    }
                    return SearchClassCallback.CONTINUE;
                }, SearchClassCallback.SCOPE_ALL);
                Log.i(TAG, "ClassBridge: done");
            } catch (Throwable t) {
                mError = t;
                Log.e(TAG, "ClassBridge: enumeration failed", t);
            } finally {
                mEnumerating.set(false);
                mComplete.set(true);
            }
        }, "YambzClassEnum").start();
    }

    public boolean isComplete() {
        return mComplete.get() && mQueue.isEmpty();
    }

    public Throwable getError() { return mError; }

    // ═══════════════════════════════════════════════════════════
    //  Members of a single class
    // ═══════════════════════════════════════════════════════════
    public String[] getMembers(String className) {
        try {
            Class<?> cls = findClass(className);
            if (cls == null) return new String[0];

            List<String> out = new ArrayList<>();

            // constructors
            try {
                for (Constructor<?> c : cls.getDeclaredConstructors()) {
                    StringBuilder sb = new StringBuilder("C|");
                    sb.append(modsToStr(c.getModifiers())).append('|');
                    sb.append("<init>").append('|');
                    sb.append('(');
                    for (Class<?> p : c.getParameterTypes()) sb.append(p.getName());
                    sb.append(")V|");
                    // (returnType empty for constructors)
                    out.add(sb.toString());
                }
            } catch (Throwable ignored) {}

            // methods (native — يتجاوز hidden API)
            try {
                for (Method m : AlbatrossReflection.getDeclaredMethods(cls, false)) {
                    StringBuilder sb = new StringBuilder("M|");
                    sb.append(modsToStr(m.getModifiers())).append('|');
                    sb.append(m.getName()).append('|');
                    sb.append('(');
                    for (Class<?> p : m.getParameterTypes()) sb.append(p.getName());
                    sb.append(')');
                    sb.append(retTypeToStr(m.getReturnType())).append('|');
                    sb.append(retTypeToStr(m.getReturnType()));
                    out.add(sb.toString());
                }
            } catch (Throwable ignored) {}

            // fields (with superclass chain + synthetic filter)
            try {
                for (Field f : AlbatrossReflection.getDeclaredFields(cls)) {
                    StringBuilder sb = new StringBuilder("F|");
                    sb.append(modsToStr(f.getModifiers())).append('|');
                    sb.append(f.getName()).append('|');
                    sb.append(retTypeToStr(f.getType())).append('|');
                    // no returnType for fields
                    out.add(sb.toString());
                }
            } catch (Throwable ignored) {}

            // static methods
            try {
                for (Method m : AlbatrossReflection.getDeclaredMethods(cls, true)) {
                    if (!Modifier.isStatic(m.getModifiers())) continue;
                    StringBuilder sb = new StringBuilder("M|static ");
                    sb.append(modsToStr(m.getModifiers())).append('|');
                    sb.append(m.getName()).append('|');
                    sb.append('(');
                    for (Class<?> p : m.getParameterTypes()) sb.append(p.getName());
                    sb.append(')');
                    sb.append(retTypeToStr(m.getReturnType())).append('|');
                    sb.append(retTypeToStr(m.getReturnType()));
                    out.add(sb.toString());
                }
            } catch (Throwable ignored) {}

            return out.toArray(new String[0]);
        } catch (Throwable t) {
            Log.e(TAG, "getMembers(" + className + ") failed", t);
            return new String[0];
        }
    }

    private static Class<?> findClass(String name) {
        // boot classes first (سريع)
        try { return Class.forName(name, false, null); } catch (Throwable ignored) {}
        // التطبيق
        try { return Class.forName(name); } catch (Throwable ignored) {}
        // من ClassLoader List
        for (ClassLoader cl : Albatross.getClassLoaderList()) {
            try { return Class.forName(name, false, cl); } catch (Throwable ignored) {}
        }
        return null;
    }

    private static String modsToStr(int mods) {
        StringBuilder sb = new StringBuilder(48);
        if (Modifier.isPublic(mods))       sb.append("public ");
        if (Modifier.isPrivate(mods))      sb.append("private ");
        if (Modifier.isProtected(mods))    sb.append("protected ");
        if (Modifier.isStatic(mods))       sb.append("static ");
        if (Modifier.isFinal(mods))        sb.append("final ");
        if (Modifier.isAbstract(mods))     sb.append("abstract ");
        if (Modifier.isNative(mods))       sb.append("native ");
        if (Modifier.isSynchronized(mods)) sb.append("synchronized ");
        return sb.toString().trim();
    }

    private static String retTypeToStr(Class<?> t) {
        if (t == null) return "V";
        if (t.isArray()) return retTypeToStr(t.getComponentType()) + "[]";
        if (t == int.class)     return "I";
        if (t == long.class)    return "J";
        if (t == short.class)   return "S";
        if (t == byte.class)    return "B";
        if (t == char.class)    return "C";
        if (t == boolean.class) return "Z";
        if (t == float.class)   return "F";
        if (t == double.class)  return "D";
        if (t == void.class)    return "V";
        return "L" + t.getName().replace('.', '/') + ";";
    }

    // ═══════════════════════════════════════════════════════════
    //  Reset (لاختبار أو reload)
    // ═══════════════════════════════════════════════════════════
    public void reset() {
        synchronized (this) { mQueue.clear(); }
        mComplete.set(false);
    }
}
