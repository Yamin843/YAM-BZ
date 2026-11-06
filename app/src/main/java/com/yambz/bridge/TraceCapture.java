package com.yambz.bridge;

import android.util.Log;

import qing.albatross.core.CallFrame;
import qing.albatross.core.MethodHookCallback;

/**
 * يستقبل قبل/بعد الدالة من Albatross ويُمرّرها إلى C++.
 *
 * التصميم:
 *   - سريع جداً (لا تخصيص في المسار الساخن)
 *   - تسليم C++ يقوم بالتفريغ الثقيل (ObjectDumper) بشكل متوازٍ
 *   - backtrace يُلتقط هنا (Thread.getStackTrace) — قابل للتعطيل
 *
 * استثناءات:
 *   - أي استثناء هنا **يُسجَّل ولا يُرمى** (يجب ألا نُعطّل هدف التطبيق)
 */
public class TraceCapture extends MethodHookCallback {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private final int sessionId;
    private final ThreadLocal<Boolean> inCallback = ThreadLocal.withInitial(() -> false);

    public TraceCapture(int sessionId) {
        this.sessionId = sessionId;
    }

    @Override
    public boolean beforeCall(CallFrame frame) {
        // حماية من إعادة الدخول (نادراً لكن ضروري)
        if (inCallback.get()) return true;
        inCallback.set(true);
        try {
            nativeOnBefore(sessionId, frame);
        } catch (Throwable t) {
            Log.e(TAG, "beforeCall", t);
        } finally {
            inCallback.set(false);
        }
        return true;
    }

    @Override
    public void afterCall(CallFrame frame) {
        if (inCallback.get()) return;
        inCallback.set(true);
        try {
            nativeOnAfter(sessionId, frame);
        } catch (Throwable t) {
            Log.e(TAG, "afterCall", t);
        } finally {
            inCallback.set(false);
        }
    }

    private static native void nativeOnBefore(int sessionId, CallFrame frame);
    private static native void nativeOnAfter(int sessionId, CallFrame frame);
}
