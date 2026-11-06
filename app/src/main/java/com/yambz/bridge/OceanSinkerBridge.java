package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Member;

import qing.albatross.tracker.DefaultSinker;
import qing.albatross.tracker.EmptySinker;
import qing.albatross.tracker.FieldReadEvent;
import qing.albatross.tracker.FieldWriteEvent;
import qing.albatross.tracker.KeyWordSinker;
import qing.albatross.tracker.MethodCallContext;
import qing.albatross.tracker.MethodReturnContext;
import qing.albatross.tracker.OceanSinker;
import qing.albatross.tracker.StringSinker;
import qing.albatross.tracker.TraceContext;

/**
 * OceanSinkerBridge — sinker كامل يمرر 8 أحداث إلى C++.
 *
 * 4 sinkers:
 *  0 = custom (this class, all events)
 *  1 = KeyWordSinker (فلترة بالكلمة)
 *  2 = StringSinker (فلترة بالنص)
 *  3 = EmptySinker (عدّاد فقط)
 *
 * في وضع custom، يستقبل كل الأحداث، يقرأ الـ context كاملاً، يمرر إلى C++.
 * في الأوضاع 1-3، يفوّض لـ Ocean's ready-made sinker (أسرع، مفلتر).
 */
public final class OceanSinkerBridge extends OceanSinker {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private final int sessionId;
    private final int sinkerKind;
    private final String sinkerKeyword;

    // Delegate for sinkers 1-3
    private final OceanSinker delegate;

    public OceanSinkerBridge(int sessionId, int sinkerKind, String keyword) {
        this.sessionId = sessionId;
        this.sinkerKind = sinkerKind;
        this.sinkerKeyword = keyword != null ? keyword : "";
        switch (sinkerKind) {
            case 1: this.delegate = new KeyWordSinker(this.sinkerKeyword); break;
            case 2: this.delegate = new StringSinker(this.sinkerKeyword); break;
            case 3: this.delegate = new EmptySinker(); break;
            default: this.delegate = null; break;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  CALL
    // ══════════════════════════════════════════════════════════
    @Override
    public void call(MethodCallContext ctx, Member member) {
        if (ctx == null || member == null) return;

        // Delegate mode
        if (delegate != null) {
            try { delegate.call(ctx, member); } catch (Throwable ignored) {}
            return;
        }

        try {
            // Execute override BEFORE reporting
            Integer action = OceanControllerHelper_getAction(member);
            if (action != null) {
                if (action == 1) { ctx.bypass(); }
                else if (action == 2) { ctx.skipCall(); }
            }
            Object retOverride = OceanControllerHelper_getReturnOverride(member);
            if (retOverride != null) {
                try { ctx.returnWithResult(retOverride); } catch (Throwable ignored) {}
            }

            // Report event
            long depth = ctx.depth;
            String className = member.getDeclaringClass().getName();
            String methodName = member.getName();
            String methodDesc = member.toString();
            Object[] args = safeArgs(ctx);
            Object self = safeThis(ctx);

            nativeOnCall(sessionId, member, methodDesc, className,
                         methodName, (int) depth, args, self);
        } catch (Throwable t) {
            Log.e(TAG, "call failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  RETURN (object)
    // ══════════════════════════════════════════════════════════
    @Override
    public void ret(MethodReturnContext ctx, Member member, int dexPc, Object returnValue) {
        if (delegate != null) { try { delegate.ret(ctx, member, dexPc, returnValue); } catch (Throwable ignored) {} return; }
        try { nativeOnReturn(sessionId, member, dexPc, returnValue); }
        catch (Throwable t) { Log.e(TAG, "ret", t); }
    }

    // ══════════════════════════════════════════════════════════
    //  RETURN (primitive — decoded here)
    // ══════════════════════════════════════════════════════════
    @Override
    public void retPrimitive(MethodReturnContext ctx, Member member, int dexPc,
                             Class<?> returnType, long rawValue) {
        if (delegate != null) { try { delegate.retPrimitive(ctx, member, dexPc, returnType, rawValue); } catch (Throwable ignored) {} return; }

        Object decoded = decodePrimitive(returnType, rawValue);
        try { nativeOnReturn(sessionId, member, dexPc, decoded); }
        catch (Throwable t) { Log.e(TAG, "retPrimitive", t); }
    }

    // ══════════════════════════════════════════════════════════
    //  EXEC
    // ══════════════════════════════════════════════════════════
    @Override
    public void exec(TraceContext ctx, Member member, int dexPc, Object self) {
        if (delegate != null) { try { delegate.exec(ctx, member, dexPc, self); } catch (Throwable ignored) {} return; }
        try { nativeOnExec(sessionId, member, dexPc, self); }
        catch (Throwable t) {}
    }

    // ══════════════════════════════════════════════════════════
    //  BLOCK
    // ══════════════════════════════════════════════════════════
    @Override
    public void block(TraceContext ctx, Member member, int dexPc, int offset) {
        if (delegate != null) { try { delegate.block(ctx, member, dexPc, offset); } catch (Throwable ignored) {} return; }
        try { nativeOnBlock(sessionId, member, dexPc, offset); }
        catch (Throwable t) {}
    }

    // ══════════════════════════════════════════════════════════
    //  FIELD READ
    // ══════════════════════════════════════════════════════════
    @Override
    public void fieldRead(FieldReadEvent ctx, Member member, int dexPc, Object owner, Field field) {
        if (delegate != null) { try { delegate.fieldRead(ctx, member, dexPc, owner, field); } catch (Throwable ignored) {} return; }
        try { nativeOnFieldRead(sessionId, member, dexPc, owner, field); }
        catch (Throwable t) {}
    }

    // ══════════════════════════════════════════════════════════
    //  FIELD WRITE
    // ══════════════════════════════════════════════════════════
    @Override
    public void fieldWritten(FieldWriteEvent ctx, Member member, int dexPc,
                             Object owner, Field field, Object newValue) {
        if (delegate != null) { try { delegate.fieldWritten(ctx, member, dexPc, owner, field, newValue); } catch (Throwable ignored) {} return; }
        try { nativeOnFieldWritten(sessionId, member, dexPc, owner, field, newValue); }
        catch (Throwable t) {}
    }

    // ══════════════════════════════════════════════════════════
    //  UNTRACE
    // ══════════════════════════════════════════════════════════
    @Override
    public void onUnTrace() {
        try { nativeOnUnTrace(sessionId); } catch (Throwable ignored) {}
    }

    @Override
    public boolean traceCurrentThread() {
        return true;
    }

    // ══════════════════════════════════════════════════════════
    //  Helpers
    // ══════════════════════════════════════════════════════════
    private static Object[] safeArgs(MethodCallContext ctx) {
        try { Object[] a = ctx.getArguments(); return a != null ? a : new Object[0]; }
        catch (Throwable t) { return new Object[0]; }
    }

    private static Object safeThis(MethodCallContext ctx) {
        try { return ctx.getThis(); } catch (Throwable t) { return null; }
    }

    private static Object decodePrimitive(Class<?> t, long raw) {
        if (t == null) return null;
        if (t == boolean.class || t == Boolean.class) return raw != 0;
        if (t == byte.class || t == Byte.class) return (byte) raw;
        if (t == char.class || t == Character.class) return (char) raw;
        if (t == short.class || t == Short.class) return (short) raw;
        if (t == int.class || t == Integer.class) return (int) raw;
        if (t == long.class || t == Long.class) return raw;
        if (t == float.class || t == Float.class) return Float.intBitsToFloat((int) raw);
        if (t == double.class || t == Double.class) return Double.longBitsToDouble(raw);
        if (t == void.class || t == Void.class) return null;
        return null;
    }

    // ══════════════════════════════════════════════════════════
    //  Controller accessors (avoid circular import)
    // ══════════════════════════════════════════════════════════
    private static Integer OceanControllerHelper_getAction(Member m) {
        try {
            Class<?> cls = Class.forName("com.yambz.feature.OceanController");
            java.lang.reflect.Method mid = cls.getMethod("getAction", Member.class);
            return (Integer) mid.invoke(null, m);
        } catch (Throwable t) { return null; }
    }

    private static Object OceanControllerHelper_getReturnOverride(Member m) {
        try {
            Class<?> cls = Class.forName("com.yambz.feature.OceanController");
            java.lang.reflect.Method mid = cls.getMethod("getReturnOverride", Member.class);
            return mid.invoke(null, m);
        } catch (Throwable t) { return null; }
    }

    // ══════════════════════════════════════════════════════════
    //  Native (jni/jni_ocean.cpp)
    // ══════════════════════════════════════════════════════════
    private static native void nativeOnCall(int sessionId, Member member,
                                             String methodDesc, String className,
                                             String methodName, int depth,
                                             Object[] args, Object self);
    private static native void nativeOnReturn(int sessionId, Member member, int dexPc, Object returnValue);
    private static native void nativeOnExec(int sessionId, Member member, int dexPc, Object self);
    private static native void nativeOnBlock(int sessionId, Member member, int dexPc, int offset);
    private static native void nativeOnFieldRead(int sessionId, Member member, int dexPc, Object owner, Field field);
    private static native void nativeOnFieldWritten(int sessionId, Member member, int dexPc, Object owner, Field field, Object newValue);
    private static native void nativeOnUnTrace(int sessionId);
}
