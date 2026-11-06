package com.yambz.bridge;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.MotionEvent;
import android.view.ViewGroup;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * PanelBridge — GLSurfaceView شفاف full-screen لـ ImGui.
 * RENDERMODE_WHEN_DIRTY + touch forwarding.
 */
public final class PanelBridge extends GLSurfaceView implements GLSurfaceView.Renderer {
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
        setRenderMode(RENDERMODE_WHEN_DIRTY);
        setPreserveEGLContextOnPause(true);
        setFocusable(false);
        setFocusableInTouchMode(false);
        setLayoutParams(new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));
    }

    @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        try { nativeOnSurfaceCreated(); }
        catch (Throwable t) { Log.e(TAG, "surfaceCreated", t); }
    }

    @Override public void onSurfaceChanged(GL10 gl, int w, int h) {
        try { nativeOnSurfaceChanged(w, h); }
        catch (Throwable t) {}
        requestRender();
    }

    @Override public void onDrawFrame(GL10 gl) {
        try { nativeOnDrawFrame(getWidth(), getHeight()); }
        catch (Throwable t) {}
        if (nativeWantsAnotherFrame()) requestRender();
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        float x = e.getX();
        float y = e.getY();

        if (action == MotionEvent.ACTION_DOWN) {
            touchOwned = nativeHitTest(x, y);
        }
        if (!touchOwned) return false;

        try { nativeOnTouch(action, x, y, e.getPointerId(e.getActionIndex())); }
        catch (Throwable t) {}
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

    // ══════════════════════════════════════════════════════════
    //  Native calls
    // ══════════════════════════════════════════════════════════
    static native void nativeOnSurfaceCreated();
    static native void nativeOnSurfaceChanged(int w, int h);
    static native void nativeOnDrawFrame(int w, int h);
    static native void nativeOnTouch(int action, float x, float y, int pointerId);
    static native void nativeOnScroll(float dx, float dy);
    static native boolean nativeHitTest(float x, float y);
    static native boolean nativeWantsAnotherFrame();
}
