package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Member;

import qing.albatross.core.InstructionListener;
import qing.albatross.core.InvocationContext;

/**
 * يستقبل enter/exit لـ constructor عبر Albatross.hookInstruction.
 *
 * لماذا؟
 *   - Albatross.hookMethod يرفض constructors
 *   - hookInstruction يعمل على أي Member (method أو constructor)
 *
 * التدفق:
 *   onEnter: self=null (كائن لم يُنشأ بعد) + args
 *   onReturn: ret=الكائن المُنشأ + args
 */
public class ConstructorTraceCapture extends InstructionListener {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private final int sessionId;

    public ConstructorTraceCapture(int sessionId) {
        this.sessionId = sessionId;
    }

    @Override
    public void onEnter(Member method, Object self, int dexPc, InvocationContext ctx) {
        try {
            nativeOnEnter(sessionId, method, dexPc, ctx);
        } catch (Throwable t) {
            Log.e(TAG, "ctor onEnter", t);
        }
    }

    @Override
    public void onReturn(Member method, Object ret, int dexPc, InvocationContext ctx) {
        try {
            nativeOnReturn(sessionId, method, ret, dexPc, ctx);
        } catch (Throwable t) {
            Log.e(TAG, "ctor onReturn", t);
        }
    }

    private static native void nativeOnEnter(int sessionId, Member method, int dexPc, InvocationContext ctx);
    private static native void nativeOnReturn(int sessionId, Member method, Object ret, int dexPc, InvocationContext ctx);
}
