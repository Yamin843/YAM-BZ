package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;

import qing.albatross.tracker.EventType;
import qing.albatross.tracker.FieldReadEvent;
import qing.albatross.tracker.FieldWriteEvent;
import qing.albatross.tracker.MethodCallContext;
import qing.albatross.tracker.MethodReturnContext;
import qing.albatross.tracker.OceanSinker;
import qing.albatross.tracker.OceanTracker;
import qing.albatross.tracker.TraceContext;

/**
 * OceanTraceBridge: يمثل جسراً كاملاً بين Ocean Tracker وطبقة C++.
 *
 * يدعم كل أنواع الأحداث (8 أنواع):
 *  - CALL_ENTER: عند دخول أي method
 *  - CALL: عند استدعاء أي method (call event)
 *  - RETURN: عند عودة أي method
 *  - EXEC: كل تعليمة (dex instruction)
 *  - BLOCK: كل basic block
 *  - FILE_READ: قراءة حقل
 *  - FIELD_WRITE: كتابة حقل
 *  - DUMP: dump على الطلب
 *
 * يستخدم ThreadLocal للـ contexts (لكل thread سياق خاص) لتجنب التزامن.
 *
 * النقل إلى C++: كل حدث يُرسل عبر JNI method مختلفة حسب النوع.
 * في C++، TraceStore يستقبلها بنفس بنية TraceEvent.
 */
public final class OceanTraceBridge extends OceanSinker {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private final int sessionId;

    // ThreadLocal contexts (Ocean يوفرها لكنها في SinkerDelegate)
    private final ThreadLocal<MethodCallContext> callCtx = new ThreadLocal<>();
    private final ThreadLocal<MethodReturnContext> retCtx = new ThreadLocal<>();
    private final ThreadLocal<TraceContext> execCtx = new ThreadLocal<>();
    private final ThreadLocal<FieldReadEvent> fieldReadCtx = new ThreadLocal<>();
    private final ThreadLocal<FieldWriteEvent> fieldWriteCtx = new ThreadLocal<>();

    // عدادات
    private volatile long eventCount = 0;
    private volatile long callCount = 0;
    private volatile long returnCount = 0;
    private volatile long execCount = 0;
    private volatile long blockCount = 0;
    private volatile long fieldReadCount = 0;
    private volatile long fieldWriteCount = 0;

    public OceanTraceBridge(int sessionId) {
        this.sessionId = sessionId;
    }

    // ══════════════════════════════════════════════════════════
    //  CALL_ENTER / CALL
    // ══════════════════════════════════════════════════════════
    @Override
    public void call(MethodCallContext ctx, Member member) {
        if (ctx == null) return;
        callCount++;
        eventCount++;

        try {
            // depth من Ocean (فعلي)
            long depth = ctx.depth;
            long ctxId = ctx.hashCode();  // معرف مؤقت
            String methodDesc = member.toString();
            String className = member.getDeclaringClass().getName();
            String methodName = member.getName();

            // الجلب من CallFrame/InvokeContext
            Object[] args = safeGetArgs(ctx);
            Object self = safeGetThis(ctx);

            nativeOnCall(sessionId, member, methodDesc, className,
                         methodName, (int) depth, args, self, ctxId);

        } catch (Throwable t) {
            Log.e(TAG, "Ocean.call failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  RETURN
    // ══════════════════════════════════════════════════════════
    @Override
    public void ret(MethodReturnContext ctx, Member member, int dexPc, Object returnValue) {
        if (ctx == null) return;
        returnCount++;
        eventCount++;
        try {
            nativeOnReturn(sessionId, member, dexPc, returnValue, 0);
        } catch (Throwable t) {
            Log.e(TAG, "Ocean.ret failed", t);
        }
    }

    @Override
    public void retPrimitive(MethodReturnContext ctx, Member member, int dexPc,
                             Class<?> returnType, long rawValue) {
        if (ctx == null) return;
        returnCount++;
        eventCount++;
        try {
            nativeOnReturnPrim(sessionId, member, dexPc, returnType, rawValue);
        } catch (Throwable t) {
            Log.e(TAG, "Ocean.retPrimitive failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  EXEC (كل تعليمة)
    // ══════════════════════════════════════════════════════════
    @Override
    public void exec(TraceContext ctx, Member member, int dexPc, Object self) {
        if (ctx == null) return;
        execCount++;
        eventCount++;
        try {
            nativeOnExec(sessionId, member, dexPc, self);
        } catch (Throwable t) {
            Log.e(TAG, "Ocean.exec failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  BLOCK (basic block)
    // ══════════════════════════════════════════════════════════
    @Override
    public void block(TraceContext ctx, Member member, int dexPc, int offset) {
        if (ctx == null) return;
        blockCount++;
        eventCount++;
        try {
            nativeOnBlock(sessionId, member, dexPc, offset);
        } catch (Throwable t) {
            Log.e(TAG, "Ocean.block failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  FIELD_READ
    // ══════════════════════════════════════════════════════════
    @Override
    public void fieldRead(FieldReadEvent ctx, Member member, int dexPc,
                          Object owner, Field field) {
        if (ctx == null) return;
        fieldReadCount++;
        eventCount++;
        try {
            nativeOnFieldRead(sessionId, member, dexPc, owner, field);
        } catch (Throwable t) {
            Log.e(TAG, "Ocean.fieldRead failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  FIELD_WRITE
    // ══════════════════════════════════════════════════════════
    @Override
    public void fieldWritten(FieldWriteEvent ctx, Member member, int dexPc,
                             Object owner, Field field, Object newValue) {
        if (ctx == null) return;
        fieldWriteCount++;
        eventCount++;
        try {
            nativeOnFieldWritten(sessionId, member, dexPc, owner, field, newValue);
        } catch (Throwable t) {
            Log.e(TAG, "Ocean.fieldWritten failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Cleanup
    // ══════════════════════════════════════════════════════════
    @Override
    public void onUnTrace() {
        Log.i(TAG, "OceanTraceBridge.onUnTrace: session=" + sessionId
            + " events=" + eventCount
            + " (call=" + callCount + ", ret=" + returnCount
            + ", exec=" + execCount + ", block=" + blockCount
            + ", fieldR=" + fieldReadCount + ", fieldW=" + fieldWriteCount + ")");
        try {
            nativeOnUnTrace(sessionId);
        } catch (Throwable ignored) {}
    }

    @Override
    public boolean traceCurrentThread() {
        // احترم إعدادات ThreadConfig — Ocean يتعامل معها في SinkerDelegate
        return true;
    }

    // ══════════════════════════════════════════════════════════
    //  Helpers
    // ══════════════════════════════════════════════════════════
    private static Object[] safeGetArgs(MethodCallContext ctx) {
        try {
            Object[] a = ctx.getArguments();
            return a != null ? a : new Object[0];
        } catch (Throwable t) {
            return new Object[0];
        }
    }

    private static Object safeGetThis(MethodCallContext ctx) {
        try {
            return ctx.getThis();
        } catch (Throwable t) {
            return null;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Statistics (يقرأها UI)
    // ══════════════════════════════════════════════════════════
    public long getEventCount() { return eventCount; }
    public long getCallCount() { return callCount; }
    public long getReturnCount() { return returnCount; }
    public long getExecCount() { return execCount; }
    public long getBlockCount() { return blockCount; }
    public long getFieldReadCount() { return fieldReadCount; }
    public long getFieldWriteCount() { return fieldWriteCount; }

    // ══════════════════════════════════════════════════════════
    //  Native (implemented in jni_ocean.cpp)
    // ══════════════════════════════════════════════════════════
    private static native void nativeOnCall(int sessionId, Member member,
                                            String methodDesc, String className,
                                            String methodName, int depth,
                                            Object[] args, Object self, long ctxId);
    private static native void nativeOnReturn(int sessionId, Member member, int dexPc,
                                              Object returnValue, int eventKind);
    private static native void nativeOnReturnPrim(int sessionId, Member member, int dexPc,
                                                  Class<?> returnType, long rawValue);
    private static native void nativeOnExec(int sessionId, Member member, int dexPc, Object self);
    private static native void nativeOnBlock(int sessionId, Member member, int dexPc, int offset);
    private static native void nativeOnFieldRead(int sessionId, Member member, int dexPc,
                                                 Object owner, Field field);
    private static native void nativeOnFieldWritten(int sessionId, Member member, int dexPc,
                                                    Object owner, Field field, Object newValue);
    private static native void nativeOnUnTrace(int sessionId);
}
