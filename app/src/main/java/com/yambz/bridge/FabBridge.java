package com.yambz.bridge;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;

/**
 * زر عائم صغير (56dp × 56dp).
 *
 * - لا يسرق لمساً خارج حدوده
 * - قابل للسحب (drag يحرك الزر)
 * - نقرة سريعة (tap) → nativeFabTapped()
 *
 * كل المنطق في Java — C++ فقط يستلم tap event.
 */
public class FabBridge extends ImageView {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private float lastX, lastY;
    private long  downTime;
    private boolean dragging;
    private static final int DP = 56;

    public FabBridge(Context ctx) {
        super(ctx);

        int sizePx = dp(DP);

        // شكل: دائرة داكنة + إطار ذهبي
        GradientDrawable bg = new GradientDrawable();
        bg.setShape(GradientDrawable.OVAL);
        bg.setColor(0xF01A1A1F);
        bg.setStroke(dp(2), 0xFFD4A017);
        setBackground(bg);
        setElevation(dp(8));
        setContentDescription("YAM-BZ");

        // أيقونة: نقطة ذهبية
        GradientDrawable icon = new GradientDrawable();
        icon.setShape(GradientDrawable.OVAL);
        icon.setColor(0xFFF0BE3C);
        setImageDrawable(icon);
        setPadding(sizePx/3, sizePx/3, sizePx/3, sizePx/3);
        setScaleType(ScaleType.CENTER_CROP);

        FrameLayout.LayoutParams lp =
            new FrameLayout.LayoutParams(sizePx, sizePx);
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.leftMargin = dp(16);
        lp.topMargin  = dp(120);
        setLayoutParams(lp);

        setOnTouchListener(new OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        downTime = System.currentTimeMillis();
                        lastX = e.getRawX();
                        lastY = e.getRawY();
                        dragging = false;
                        v.animate().scaleX(1.15f).scaleY(1.15f).setDuration(80).start();
                        return true;

                    case MotionEvent.ACTION_MOVE: {
                        float dx = e.getRawX() - lastX;
                        float dy = e.getRawY() - lastY;
                        if (!dragging && (Math.abs(dx) > dp(6) || Math.abs(dy) > dp(6))) {
                            dragging = true;
                        }
                        if (dragging) {
                            float nx = Math.max(0, Math.min(
                                getX() + dx, ((View) getParent()).getWidth() - getWidth()));
                            float ny = Math.max(0, Math.min(
                                getY() + dy, ((View) getParent()).getHeight() - getHeight()));
                            setX(nx);
                            setY(ny);
                            lastX = e.getRawX();
                            lastY = e.getRawY();
                        }
                        return true;
                    }

                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        v.animate().scaleX(1f).scaleY(1f).setDuration(80).start();
                        if (!dragging && System.currentTimeMillis() - downTime < 350) {
                            try { nativeFabTapped(); }
                            catch (Throwable t) { Log.e(TAG, "tap", t); }
                        }
                        return true;
                }
                return false;
            }
        });
    }

    /** يُستدعى من C++ — أي thread → post إلى UI */
    public void setVisibleFromAnyThread(boolean visible) {
        final int vis = visible ? VISIBLE : GONE;
        post(() -> setVisibility(vis));
    }

    private int dp(int v) {
        return Math.round(getResources().getDisplayMetrics().density * v);
    }

    private static native void nativeFabTapped();
}
