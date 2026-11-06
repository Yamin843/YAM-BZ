package com.yambz.bridge;

import android.app.Activity;
import android.util.Log;
import android.view.ViewGroup;

import java.lang.ref.WeakReference;

/**
 * UIBridge — يربط دورة حياة Activity مع FAB/Panel.
 *
 * - onCreate → attach FAB
 * - onResume → show FAB
 * - onPause → hide Panel (not FAB)
 * - onDestroy → detach all
 */
public final class UIBridge {
    private static final String TAG = "YAMBZ";

    private static volatile WeakReference<Activity> sActivity = new WeakReference<>(null);
    private static volatile FabBridge sFab = null;
    private static volatile PanelBridge sPanel = null;
    private static volatile boolean sInitialized = false;

    private UIBridge() {}

    public static synchronized void init(String cacheDir) {
        if (sInitialized) return;
        sInitialized = true;
        nativeSetCacheDir(cacheDir);
        Log.i(TAG, "UIBridge initialized");
    }

    public static void onActivityCreated(final Activity a) {
        Log.i(TAG, "onActivityCreated: " + a);
    }

    public static void onActivityResumed(final Activity a) {
        sActivity = new WeakReference<>(a);
        a.runOnUiThread(() -> attachFab(a));
    }

    public static void onActivityPaused(final Activity a) {
        a.runOnUiThread(() -> {
            if (sPanel != null) {
                hidePanel();
            }
        });
    }

    public static void onActivityDestroyed(final Activity a) {
        a.runOnUiThread(() -> {
            if (sActivity.get() == a) {
                detachAll();
            }
        });
    }

    private static void attachFab(Activity a) {
        try {
            ViewGroup decor = (ViewGroup) a.getWindow().getDecorView();
            if (decor == null) return;

            // Existing FAB?
            if (sFab != null && sFab.getParent() == decor) {
                sFab.setVisibility(android.view.View.VISIBLE);
                return;
            }

            // Create FAB
            if (sFab == null) {
                sFab = new FabBridge(a);
            }
            if (sFab.getParent() != null) {
                ((ViewGroup) sFab.getParent()).removeView(sFab);
            }
            decor.addView(sFab);
            Log.i(TAG, "FAB attached to " + a.getClass().getSimpleName());
        } catch (Throwable t) {
            Log.e(TAG, "attachFab failed", t);
        }
    }

    /** Called from FabBridge on tap. */
    public static void openPanel() {
        Activity a = sActivity.get();
        if (a == null) return;

        a.runOnUiThread(() -> {
            try {
                if (sFab != null) sFab.setVisibility(android.view.View.GONE);

                if (sPanel == null) {
                    sPanel = new PanelBridge(a);
                }
                ViewGroup decor = (ViewGroup) a.getWindow().getDecorView();
                if (sPanel.getParent() == null && decor != null) {
                    decor.addView(sPanel);
                }
                Log.i(TAG, "Panel opened");
            } catch (Throwable t) {
                Log.e(TAG, "openPanel failed", t);
            }
        });
    }

    public static void hidePanel() {
        Activity a = sActivity.get();
        if (a == null) return;

        a.runOnUiThread(() -> {
            try {
                if (sPanel != null) {
                    ViewGroup parent = (ViewGroup) sPanel.getParent();
                    if (parent != null) parent.removeView(sPanel);
                    sPanel = null;
                }
                if (sFab != null) sFab.setVisibility(android.view.View.VISIBLE);
                Log.i(TAG, "Panel closed");
            } catch (Throwable t) {
                Log.e(TAG, "hidePanel failed", t);
            }
        });
    }

    private static void detachAll() {
        try {
            if (sPanel != null) {
                ViewGroup p = (ViewGroup) sPanel.getParent();
                if (p != null) p.removeView(sPanel);
                sPanel = null;
            }
            if (sFab != null) {
                ViewGroup p = (ViewGroup) sFab.getParent();
                if (p != null) p.removeView(sFab);
                sFab = null;
            }
        } catch (Throwable ignored) {}
    }

    // ══════════════════════════════════════════════════════════
    //  Native calls (from jni_ui.cpp)
    // ══════════════════════════════════════════════════════════
    private static native void nativeSetCacheDir(String dir);
}
