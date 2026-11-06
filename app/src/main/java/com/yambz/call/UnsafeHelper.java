package com.yambz.call;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * Unsafe wrapper — كل العمليات التي لا يسمح بها reflection العادي.
 */
public final class UnsafeHelper {
    private static final sun.misc.Unsafe U = _get();
    private static final boolean AVAILABLE = U != null;

    private static sun.misc.Unsafe _get() {
        try {
            Field f = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            return (sun.misc.Unsafe) f.get(null);
        } catch (Throwable t) { return null; }
    }

    private UnsafeHelper() {}

    public static boolean available() { return AVAILABLE; }

    /** إنشاء instance بدون تشغيل constructor. */
    public static Object allocateInstance(Class<?> cls) throws Exception {
        if (!AVAILABLE) throw new UnsupportedOperationException("Unsafe unavailable");
        return U.allocateInstance(cls);
    }

    /** كتابة حقل بـ Unsafe (يتجاوز final و hidden API). */
    public static void putField(Object obj, Field f, Object value) throws Exception {
        if (!AVAILABLE) throw new UnsupportedOperationException("Unsafe unavailable");
        long off = U.objectFieldOffset(f);
        Class<?> ty = f.getType();
        if (ty == int.class)      U.putInt(obj, off, ((Number) value).intValue());
        else if (ty == long.class) U.putLong(obj, off, ((Number) value).longValue());
        else if (ty == short.class)U.putShort(obj, off, ((Number) value).shortValue());
        else if (ty == byte.class) U.putByte(obj, off, ((Number) value).byteValue());
        else if (ty == char.class) U.putChar(obj, off, (Character) value);
        else if (ty == boolean.class) U.putBoolean(obj, off, (Boolean) value);
        else if (ty == float.class) U.putFloat(obj, off, ((Number) value).floatValue());
        else if (ty == double.class) U.putDouble(obj, off, ((Number) value).doubleValue());
        else                        U.putObject(obj, off, value);
    }

    /** قراءة حقل بـ Unsafe (يتجاوز hidden API). */
    public static Object getField(Object obj, Field f) throws Exception {
        if (!AVAILABLE) throw new UnsupportedOperationException("Unsafe unavailable");
        long off = U.objectFieldOffset(f);
        Class<?> ty = f.getType();
        if (ty == int.class)      return U.getInt(obj, off);
        if (ty == long.class)     return U.getLong(obj, off);
        if (ty == short.class)    return U.getShort(obj, off);
        if (ty == byte.class)     return U.getByte(obj, off);
        if (ty == char.class)     return U.getChar(obj, off);
        if (ty == boolean.class)  return U.getBoolean(obj, off);
        if (ty == float.class)    return U.getFloat(obj, off);
        if (ty == double.class)   return U.getDouble(obj, off);
        return U.getObject(obj, off);
    }
}
