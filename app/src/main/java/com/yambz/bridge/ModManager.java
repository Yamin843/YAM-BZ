package com.yambz.bridge;

import android.app.Activity;
import android.util.Log;
import android.view.ViewGroup;

import java.lang.ref.WeakReference;

/**
 * المنسّق بين دورة حياة الـ Activity وطبقة الـ Native.
 *
 * الطبقة الـ native تحتفظ بـ:
 *   - FAB كـ Android View (ModFab، native-owned)
 *   - Panel كـ GLSurfaceView
 *   - حالة ImGui
 *
 * Java هنا فقط يبلّغ native عن التغييرات في الأكتيفيتي.
 */
public final class ModManager {
    private static final String TAG = "YAMBZ";
    private static volatile WeakReference<Activity> sForeground = new WeakReference<>(null);
    private static volatile boolean sInstalled = false;

    private ModManager() {}

    static void install() {
        if (sInstalled) return;
        sInstalled = true;
        Log.i(TAG, "ModManager.install");
    }

    // ── Lifecycle (من ActivityLifecycleHook) ──
    static void onActivityCreated(Activity a) {
        nativeOnActivityCreated(a);
    }

    static void onActivityResumed(final Activity a) {
        sForeground = new WeakReference<>(a);
        a.runOnUiThread(() -> {
            try { nativeOnActivityReady(a); }
            catch (Throwable t) { Log.e(TAG, "nativeOnActivityReady", t); }
        });
    }

    static void onActivityPaused(final Activity a) {
        a.runOnUiThread(() -> {
            try { nativeOnActivityPaused(a); }
            catch (Throwable t) { Log.e(TAG, "nativeOnActivityPaused", t); }
        });
    }

    static void onActivityDestroyed(final Activity a) {
        a.runOnUiThread(() -> {
            try { nativeOnActivityDestroyed(a); }
            catch (Throwable t) { Log.e(TAG, "nativeOnActivityDestroyed", t); }
        });
    }

    // ── Helpers ──
    public static Activity currentActivity() {
        WeakReference<Activity> r = sForeground;
        return r != null ? r.get() : null;
    }

    // ── Native entry points (implemented in fab_manager.cpp) ──
    static native void nativeOnActivityCreated(Activity a);
    static native void nativeOnActivityReady(Activity a);
    static native void nativeOnActivityPaused(Activity a);
    static native void nativeOnActivityDestroyed(Activity a);
}
