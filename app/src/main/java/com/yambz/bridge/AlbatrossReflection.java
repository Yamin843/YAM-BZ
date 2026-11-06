package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

import qing.albatross.core.Albatross;

/**
 * جسر موحّد لميزات Albatross الـ native:
 *   - getDeclaredMethods (يتجاوز hidden API)
 *   - findMethod (يتجاوز hidden API)
 *   - getObjectAddress (عنوان الـ heap)
 *   - searchObject (instances في الذاكرة)
 *   - searchSubClass
 *   - searchField
 *   - hookInstruction (لـ constructors)
 *
 * الاستخدام: C++ و UI يستدعيان هذه بدل reflection المباشر.
 */
public final class AlbatrossReflection {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private AlbatrossReflection() {}

    // ═══════════════════════════════════════════════════════
    //  Methods (native، bypass hidden API)
    // ═══════════════════════════════════════════════════════
    public static Method[] getDeclaredMethods(Class<?> clz, boolean staticOnly) {
        try {
            int flag = staticOnly ? Albatross.SEARCH_STATIC : Albatross.SEARCH_ALL;
            Method[] m = Albatross.getDeclaredMethods(clz, flag);
            if (m != null && m.length > 0) return m;
        } catch (Throwable t) {
            Log.w(TAG, "getDeclaredMethods native failed: " + t);
        }
        // fallback
        return clz.getDeclaredMethods();
    }

    public static Method findMethod(Class<?> clz, String name, Class<?>[] argTypes, boolean isStatic) {
        try {
            int flag = isStatic ? Albatross.SEARCH_STATIC : Albatross.SEARCH_INSTANCE;
            Method m = Albatross.findMethod(clz, argTypes, flag);
            if (m != null && (name == null || name.equals(m.getName()))) return m;
        } catch (Throwable t) {
            Log.w(TAG, "findMethod native failed: " + t);
        }
        // fallback
        try {
            return clz.getDeclaredMethod(name, argTypes);
        } catch (Throwable t) {
            return null;
        }
    }

    // ═══════════════════════════════════════════════════════
    //  Constructors (reflection — no native available)
    // ═══════════════════════════════════════════════════════
    public static Constructor<?>[] getDeclaredConstructors(Class<?> clz) {
        try {
            return clz.getDeclaredConstructors();
        } catch (Throwable t) {
            return new Constructor<?>[0];
        }
    }

    public static Constructor<?> findConstructor(Class<?> clz, Class<?>[] argTypes) {
        try {
            return clz.getDeclaredConstructor(argTypes);
        } catch (Throwable t) {
            return null;
        }
    }

    // ═══════════════════════════════════════════════════════
    //  Fields (reflection + Unsafe fallback)
    // ═══════════════════════════════════════════════════════
    public static Field[] getDeclaredFields(Class<?> clz) {
        List<Field> all = new ArrayList<>();
        for (Class<?> c = clz; c != null && c != Object.class; c = c.getSuperclass()) {
            try {
                for (Field f : c.getDeclaredFields()) {
                    if (!f.isSynthetic()) all.add(f);
                }
            } catch (Throwable ignore) {}
        }
        return all.toArray(new Field[0]);
    }

    // ═══════════════════════════════════════════════════════
    //  Addresses (native)
    // ═══════════════════════════════════════════════════════
    public static long getObjectAddress(Object obj) {
        try {
            return Albatross.getObjectAddress(obj);
        } catch (Throwable t) {
            return 0;
        }
    }

    // ═══════════════════════════════════════════════════════
    //  Heap search (native)
    // ═══════════════════════════════════════════════════════
    public static <T> List<T> searchInstances(Class<T> clz, int maxResults) {
        List<T> out = new ArrayList<>();
        try {
            Albatross.searchObject(clz, (o, i) -> {
                out.add(o);
                return out.size() < maxResults;
            });
        } catch (Throwable t) {
            Log.w(TAG, "searchObject failed: " + t);
        }
        return out;
    }

    public static List<Class<?>> searchSubclasses(Class<?> parent, int maxResults) {
        List<Class<?>> out = new ArrayList<>();
        try {
            Albatross.searchSubClass((Class<?> c, long p) -> {
                out.add(c);
                return out.size() < maxResults
                    ? qing.albatross.search.SearchClassCallback.CONTINUE
                    : qing.albatross.search.SearchClassCallback.STOP;
            }, parent, qing.albatross.search.SearchClassCallback.SCOPE_ALL);
        } catch (Throwable t) {
            Log.w(TAG, "searchSubClass failed: " + t);
        }
        return out;
    }

    // ═══════════════════════════════════════════════════════
    //  Hook instruction (للـ constructors)
    // ═══════════════════════════════════════════════════════
    public static boolean hookConstructorInstruction(Member constructor,
                                                    int minDexPc, int maxDexPc,
                                                    qing.albatross.core.InstructionListener listener) {
        try {
            return Albatross.hookInstruction(constructor, minDexPc, maxDexPc, listener);
        } catch (Throwable t) {
            Log.e(TAG, "hookInstruction failed: " + t);
            return false;
        }
    }
}
