package com.yambz.util;

import android.util.Log;
import java.nio.ByteBuffer;

/**
 * Helper for loaders. Not actively used (JNI does the InMemoryDexClassLoader).
 */
public final class DexLoader {
    private static final String TAG = "YAMBZ";

    private DexLoader() {}

    public static ClassLoader loadFromByteBuffer(ByteBuffer dex, ClassLoader parent) {
        try {
            Class<?> cls = Class.forName("dalvik.system.InMemoryDexClassLoader");
            java.lang.reflect.Constructor<?> ctor = cls.getConstructor(
                ByteBuffer[].class, ClassLoader.class);
            return (ClassLoader) ctor.newInstance(new ByteBuffer[]{dex}, parent);
        } catch (Throwable t) {
            Log.e(TAG, "loadFromByteBuffer failed", t);
            return null;
        }
    }
}
