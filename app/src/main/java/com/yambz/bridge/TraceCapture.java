package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

import qing.albatross.core.CallFrame;
import qing.albatross.core.MethodHookCallback;

/**
 * TraceCapture: كامل — كل الأنواع، كل الأعماق.
 *
 * عند beforeCall:
 * - يمرر CallFrame إلى C++
 * - C++ يقرأ كل parameter بمعرفته النوعية (int/long/float/double/Object/String)
 * - C++ يستخدم ObjectDumper للكائنات المعقدة
 * - C++ يلتقط backtrace بـ Thread.getStackTrace (مرجعي)
 *
 * عند afterCall:
 * - يمرر النتيجة (typed) + throwable + duration
 */
public class TraceCapture extends MethodHookCallback {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private final int sessionId;
    private final ThreadLocal<Boolean> inCallback = ThreadLocal.withInitial(() -> false);
    private final ThreadLocal<Long> startTime = ThreadLocal.withInitial(() -> 0L);

    public TraceCapture(int sessionId) {
        this.sessionId = sessionId;
    }

    @Override
    public boolean beforeCall(CallFrame frame) {
        if (inCallback.get()) return true;
        inCallback.set(true);
        try {
            startTime.set(System.nanoTime());
            nativeOnBefore(sessionId, frame);
        } catch (Throwable t) {
            Log.e(TAG, "beforeCall failed", t);
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
            long elapsed = 0;
            try {
                elapsed = System.nanoTime() - startTime.get();
            } catch (Throwable ignored) {}
            nativeOnAfter(sessionId, frame, elapsed / 1_000_000L);  // ms
        } catch (Throwable t) {
            Log.e(TAG, "afterCall failed", t);
        } finally {
            inCallback.set(false);
        }
    }

    private static native void nativeOnBefore(int sessionId, CallFrame frame);
    private static native void nativeOnAfter(int sessionId, CallFrame frame, long elapsedMs);
}
