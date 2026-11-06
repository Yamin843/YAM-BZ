package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Member;

import qing.albatross.core.Albatross;
import qing.albatross.core.InstructionListener;
import qing.albatross.core.InvocationContext;

/**
 * ConstructorTraceCapture: كامل باستخدام Albatross.hookInstruction.
 *
 * يلتقط:
 * - onEnter (dexPc=0): هذا الكائن لم يُنشأ بعد (self=null), args متاحة
 * - onReturn: الكائن المُنشأ (ret) + args
 *
 * يُستخدم من TraceEngine عند trace constructor.
 */
public class ConstructorTraceCapture extends InstructionListener {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private final int sessionId;

    public ConstructorTraceCapture(int sessionId) {
        super(false);  // traceReturn = false (سنتعامل مع onReturn يدوياً)
        this.sessionId = sessionId;
    }

    /**
     * تثبيت المراقب على constructor معيّن.
     * @return id للاستخدام في unHook (0 إذا فشل)
     */
    public long install(Constructor<?> ctor, int maxDexPc) {
        try {
            boolean ok = Albatross.hookInstruction(
                ctor, 0, maxDexPc, this);
            if (!ok) {
                Log.e(TAG, "hookInstruction failed for " + ctor);
                return 0;
            }
            // listenerId يُعيَّن داخلياً بواسطة Albatross
            return this.listenerId;
        } catch (Throwable t) {
            Log.e(TAG, "install failed", t);
            return 0;
        }
    }

    @Override
    public void onEnter(Member method, Object self, int dexPc, InvocationContext ctx) {
        try {
            nativeOnEnter(sessionId, method, dexPc, ctx);
        } catch (Throwable t) {
            Log.e(TAG, "onEnter failed", t);
        }
    }

    @Override
    public void onReturn(Member method, Object ret, int dexPc, InvocationContext ctx) {
        try {
            nativeOnReturn(sessionId, method, ret, dexPc, ctx);
        } catch (Throwable t) {
            Log.e(TAG, "onReturn failed", t);
        }
    }

    private static native void nativeOnEnter(int sessionId, Member method, int dexPc, InvocationContext ctx);
    private static native void nativeOnReturn(int sessionId, Member method, Object ret, int dexPc, InvocationContext ctx);
}
