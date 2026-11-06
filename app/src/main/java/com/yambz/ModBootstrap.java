package com.yambz;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.util.Log;

import com.yambz.bridge.ActivityHook;
import com.yambz.bridge.UIBridge;
import com.yambz.feature.OceanController;
import com.yambz.util.DexLoader;

import java.lang.ref.WeakReference;

/**
 * ModBootstrap — نقطة الدخول من JNI_OnLoad.
 *
 * التدفق:
 * 1. JNI_OnLoad يستخرج libalbatross_base.so إلى code_cache
 * 2. JNI يحمّل هذا الـ dex عبر InMemoryDexClassLoader
 * 3. JNI يستدعي ModBootstrap.bootstrap(albSoPath, cacheDir)
 * 4. هنا:
 *    a. System.load(albSoPath) → تحميل مكتبة Albatross
 *    b. Albatross.init(0) → تهيئة الإطار
 *    c. ActivityHook.install() → hooks على Activity.onResume
 *    d. UIBridge.init() → ربط FAB/Panel
 *    e. OceanController.init() → تهيئة Ocean
 */
public final class ModBootstrap {
    private static final String TAG = "YAMBZ";

    private static volatile boolean sInitialized = false;
    private static volatile ClassLoader sLoader = null;
    private static WeakReference<Activity> sCurrentActivity = new WeakReference<>(null);

    private ModBootstrap() {}

    public static ClassLoader getClassLoader() { return sLoader; }

    public static Activity currentActivity() {
        return sCurrentActivity.get();
    }

    static void setCurrentActivity(Activity a) {
        sCurrentActivity = new WeakReference<>(a);
    }

    /** Called from JNI_OnLoad via InMemoryDexClassLoader. */
    public static boolean bootstrap(String albSoPath, String cacheDir) {
        if (sInitialized) return true;
        sInitialized = true;

        long t0 = System.currentTimeMillis();
        sLoader = ModBootstrap.class.getClassLoader();

        try {
            // 1) Load albatross native
            Log.i(TAG, "Loading albatross: " + albSoPath);
            System.load(albSoPath);
            Log.i(TAG, "  albatross loaded ✓");

            // 2) Init Albatross
            Class<?> alb = Class.forName("qing.albatross.core.Albatross");
            java.lang.reflect.Method init = alb.getMethod("init", int.class);
            boolean ok = (boolean) init.invoke(null, 0);
            if (!ok) {
                Log.e(TAG, "Albatross.init returned false");
                return false;
            }
            Log.i(TAG, "  Albatross.init ✓");

            // 3) Hook Activity lifecycle
            ActivityHook.install();
            Log.i(TAG, "  ActivityHook installed ✓");

            // 4) Init UI bridge (FAB + Panel)
            UIBridge.setAppContext(qing.albatross.core.Albatross.currentApplication());
            UIBridge.init(cacheDir);
            Log.i(TAG, "  UIBridge initialized ✓");

            // 5) Init Ocean (Java tracer)
            OceanController.init();
            Log.i(TAG, "  OceanController initialized ✓");

            Log.i(TAG, "bootstrap complete in " + (System.currentTimeMillis() - t0) + "ms");
            return true;

        } catch (Throwable t) {
            Log.e(TAG, "bootstrap failed", t);
            return false;
        }
    }
}
