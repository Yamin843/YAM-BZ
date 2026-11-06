package com.yambz.bridge;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * لوحة ImGui كـ GLSurfaceView شفافة full-screen.
 *
 * - تُضاف مرة واحدة عند onActivityReady (لا حذف/إضافة متكرر)
 * - RENDERMODE_WHEN_DIRTY — لا CPU عند الإغلاق
 * - dispatchTouchEvent يقرر: هل اللمسة داخل نافذة ImGui؟
 *     - نعم → consume (touchOwned حتى ACTION_UP)
 *     - لا  → return false (اللمسة تذهب للتطبيق)
 */
public class PanelBridge extends GLSurfaceView implements GLSurfaceView.Renderer {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private boolean touchOwned = false;

    public PanelBridge(Context ctx) {
        super(ctx);
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(true);
        setRenderer(this);
        // ⚡ الأهم: لا 60fps دائم
        setRenderMode(RENDERMODE_WHEN_DIRTY);
        setPreserveEGLContextOnPause(true);
        setFocusable(false);
        setFocusableInTouchMode(false);
        setLayoutParams(new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
    }

    @Override public void onSurfaceCreated(GL10 gl, EGLConfig cfg) {
        try { nativeOnSurfaceCreated(); }
        catch (Throwable t) { Log.e(TAG, "surfCreate", t); }
    }

    @Override public void onSurfaceChanged(GL10 gl, int w, int h) {
        try { nativeOnSurfaceChanged(w, h); } catch (Throwable t) {}
        requestRender();
    }

    @Override public void onDrawFrame(GL10 gl) {
        try { nativeOnDrawFrame(getWidth(), getHeight()); } catch (Throwable t) {}
        if (nativeWantsAnotherFrame()) requestRender();
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        final float x = e.getX();
        final float y = e.getY();

        if (action == MotionEvent.ACTION_DOWN) {
            touchOwned = nativeHitTest(x, y);
        }
        if (!touchOwned) {
            // اللمسة ليست على ImGui → دعها تمر
            return false;
        }

        try {
            nativeOnTouch(action, x, y, e.getPointerId(e.getActionIndex()));
        } catch (Throwable t) {}
        requestRender();

        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            touchOwned = false;
        }
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        if (e.getAction() == MotionEvent.ACTION_SCROLL) {
            float v = e.getAxisValue(MotionEvent.AXIS_VSCROLL);
            float h = e.getAxisValue(MotionEvent.AXIS_HSCROLL);
            try { nativeOnScroll(h, v); } catch (Throwable t) {}
            requestRender();
            return true;
        }
        return super.onGenericMotionEvent(e);
    }

    /** من C++ — أي thread */
    public void requestRenderFromAnyThread() {
        post(this::requestRender);
    }

    private static native void nativeOnSurfaceCreated();
    private static native void nativeOnSurfaceChanged(int w, int h);
    private static native void nativeOnDrawFrame(int w, int h);
    private static native void nativeOnTouch(int action, float x, float y, int pointerId);
    private static native void nativeOnScroll(float dx, float dy);
    private static native boolean nativeHitTest(float x, float y);
    private static native boolean nativeWantsAnotherFrame();
    
    // ── Classes enumeration ──
    public static native void nativeClassStartEnum();
    public static native void nativeClassCancel();
    public static native boolean nativeClassIsRunning();
    public static native int nativeClassCount();

    // ── Error queries ──
    public static native String nativeGetRecentErrors(int maxCount);
    public static native int    nativeErrorCount();
    public static native void   nativeClearErrors();

    // ── Deep backtrace ──
    public static native boolean nativeStartDeepBacktrace(int durationMs, int maxFrames);
}
