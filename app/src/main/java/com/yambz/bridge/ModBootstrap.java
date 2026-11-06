package com.yambz.bridge;

import android.util.Log;

import java.io.File;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * نقطة الدخول من C++.
 *
 * يُستدعى من jni_bridge.cpp بعد:
 *   1. استخراج libalbatross_base.so إلى code_cache
 *   2. تحميل dex عبر InMemoryDexClassLoader
 *
 * هنا:
 *   1. System.load(albatrossSoPath) ← ART يقبل المسار المطلق
 *   2. Albatross.init(0)
 *   3. تسجيل hooks
 *   4. تسجيل native callbacks للـ FAB/Panel
 */
public final class ModBootstrap {
    private static final String TAG = "YAMBZ";
    private static final AtomicBoolean sInited = new AtomicBoolean(false);

    private ModBootstrap() {}

    /**
     * يُستدعى من C++ (jni_bridge.cpp).
     * @param albatrossSoPath المسار المطلق لـ libalbatross_base.so
     * @param dexPath          مسار classes.dex (معلوماتي)
     * @return true إذا نجح init
     */
    private static ClassLoader sDexLoader;
    public static ClassLoader getClassLoader() { return sDexLoader; }
    public static boolean bootstrap(String albatrossSoPath, String dexPath) {
        try { sDexLoader = ModBootstrap.class.getClassLoader(); } catch (Throwable ignored) {}
        if (!sInited.compareAndSet(false, true)) return true;
        long t0 = System.currentTimeMillis();
        Log.i(TAG, "bootstrap: alb=" + albatrossSoPath + " dex=" + dexPath);

        // ── 1) تحميل libalbatross_base.so ──
        try {
            File f = new File(albatrossSoPath);
            if (!f.exists() || !f.canRead()) {
                Log.e(TAG, "albatross so missing/unreadable: " + albatrossSoPath);
                return false;
            }
            Log.i(TAG, "so size: " + f.length() + " bytes");
            System.load(albatrossSoPath);
            Log.i(TAG, "libalbatross_base loaded ✓");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "System.load(albatross) failed", e);
            return false;
        }

        // ── 2) تهيئة Albatross ──
        try {
            Class<?> albCls = Class.forName("qing.albatross.core.Albatross");
            java.lang.reflect.Method init = albCls.getDeclaredMethod("init", int.class);
            init.setAccessible(true);
            boolean ok = (boolean) init.invoke(null, 0);
            if (!ok) {
                Log.e(TAG, "Albatross.init returned false");
                return false;
            }
            Log.i(TAG, "Albatross.init OK — features: "
                + albCls.getDeclaredMethod("supportFeatures").invoke(null));
        } catch (Throwable t) {
            Log.e(TAG, "Albatross.init threw", t);
            return false;
        }

        // ── 3) hooks + UI ──
        try {
            ActivityLifecycleHook.install();
            Log.i(TAG, "lifecycle hooks installed");
        } catch (Throwable t) {
            Log.e(TAG, "lifecycle hooks failed", t);
        }

        try {
            ModManager.install();
            Log.i(TAG, "ModManager installed");
        } catch (Throwable t) {
            Log.e(TAG, "ModManager failed", t);
        }

        try {
            // init storage للـ presets/history
            android.content.Context ctx = qing.albatross.core.Albatross.currentApplication();
            if (ctx != null) {
                String cacheDir = ctx.getCodeCacheDir().getAbsolutePath();
                nativeSetCacheDir(cacheDir);
                nativeInitStorage(cacheDir);
                nativeInitFavorites(cacheDir);
                Log.i(TAG, "storage initialized: " + cacheDir);
            }
        } catch (Throwable t) {
            Log.w(TAG, "storage init failed", t);
        }

        Log.i(TAG, "bootstrap complete in " + (System.currentTimeMillis() - t0) + "ms");
        return true;
    }

    private static native void nativeInitStorage(String cacheDir);
    private static native void nativeSetCacheDir(String dir);
    private static native void nativeInitFavorites(String dir);
}
