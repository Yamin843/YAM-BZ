package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Member;

import qing.albatross.core.Albatross;
import qing.albatross.core.InstructionListener;
import qing.albatross.core.InvocationContext;

/**
 * ConstructorTraceCapture: full hookInstruction-based constructor tracing.
 *
 * install() returns boolean (not listenerId — listenerId is package-private
 * in InstructionListener). Use unHook() to cancel — inherited from parent.
 */
public class ConstructorTraceCapture extends InstructionListener {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private final int sessionId;
    private volatile boolean installed = false;

    public ConstructorTraceCapture(int sessionId) {
        super(false);  // traceReturn = false; we handle onReturn manually
        this.sessionId = sessionId;
    }

    /**
     * Install this listener on a constructor.
     * @return true on success. Use unHook() to cancel.
     */
    public boolean install(Constructor<?> ctor, int maxDexPc) {
        try {
            boolean ok = Albatross.hookInstruction(ctor, 0, maxDexPc, this);
            if (ok) {
                installed = true;
                Log.i(TAG, "ConstructorTraceCapture installed for " + ctor);
            } else {
                Log.e(TAG, "hookInstruction returned false for " + ctor);
            }
            return ok;
        } catch (Throwable t) {
            Log.e(TAG, "install failed", t);
            return false;
        }
    }

    /** Override to mark installed=false. */
    @Override
    public synchronized boolean unHook() {
        boolean ok = super.unHook();
        installed = false;
        return ok;
    }

    public boolean isInstalled() { return installed; }

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
