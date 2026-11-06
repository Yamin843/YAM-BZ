package com.yambz.call;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;

/**
 * استدعاء method أو constructor على خيط منفصل، قابل للإلغاء.
 *
 * Lifecycle:
 *   invoke(method, thisObj, args) → InvocationResult
 *   invokeAsync(..., callback) → id (للاستخدام من UI)
 *   cancel(id)
 *
 * سلوك:
 *   - إذا كان static → thisObj مُهمَل
 *   - إذا instance → thisObj مطلوب (أو Unsafe.allocateInstance)
 *   - الأخطاء تُغلَّف في InvocationResult (لا تُرمى)
 *   - timeout (افتراضي 30s) — يُلغي الخيط داخلياً
 */
public final class MethodInvoker {
    private static final String TAG = "YAMBZ";
    private static final int DEFAULT_TIMEOUT_MS = 30_000;

    private MethodInvoker() {}

    public static final class Result {
        public boolean     success;
        public Object      value;
        public Throwable   error;
        public long        elapsedMs;
        public String      memberDesc;
        public String      stackTrace;
    }

    public interface Callback {
        void onResult(Result r);
    }

    // ═══════════════════════════════════════════════════════
    //  Sync (للاستخدام من C++ أو من Java سريع)
    // ═══════════════════════════════════════════════════════
    public static Result invoke(Member member, Object self, Object[] args) {
        Result r = new Result();
        long t0 = System.currentTimeMillis();
        try {
            r.memberDesc = member.toString();
            if (member instanceof Method) {
                Method m = (Method) member;
                m.setAccessible(true);
                Object target = Modifier.isStatic(m.getModifiers()) ? null : self;
                r.value = m.invoke(target, args);
                r.success = true;
            } else if (member instanceof Constructor) {
                Constructor<?> c = (Constructor<?>) member;
                c.setAccessible(true);
                r.value = c.newInstance(args);
                r.success = true;
            } else {
                throw new IllegalArgumentException("not a Method/Constructor");
            }
        } catch (InvocationTargetException ite) {
            r.error = ite.getCause() != null ? ite.getCause() : ite;
        } catch (Throwable t) {
            r.error = t;
        }
        r.elapsedMs = System.currentTimeMillis() - t0;
        return r;
    }

    // ═══════════════════════════════════════════════════════
    //  Async (للاستخدام من UI)
    // ═══════════════════════════════════════════════════════
    private static volatile Thread sActiveThread;
    private static volatile boolean sCancelRequested;

    public static void invokeAsync(final Member member, final Object self,
                                   final Object[] args, final Callback cb) {
        invokeAsyncWithTimeout(member, self, args, DEFAULT_TIMEOUT_MS, cb);
    }

    public static void invokeAsyncWithTimeout(final Member member, final Object self,
                                              final Object[] args, final long timeoutMs,
                                              final Callback cb) {
        sCancelRequested = false;
        final Thread worker = new Thread(() -> {
            Result r = invoke(member, self, args);
            if (sCancelRequested) {
                r.success = false;
                r.error = new InterruptedException("cancelled");
            }
            if (cb != null) cb.onResult(r);
        }, "YambzInvoker");
        worker.setDaemon(true);
        sActiveThread = worker;

        // Timeout enforcer thread
        Thread watchdog = new Thread(() -> {
            try {
                worker.join(timeoutMs);
            } catch (InterruptedException e) {
                return;
            }
            if (worker.isAlive()) {
                sCancelRequested = true;
                worker.interrupt();
                // send timeout result (invoke still may finish, but we don't wait)
                Result r = new Result();
                r.success = false;
                r.error = new java.util.concurrent.TimeoutException(
                    "invocation exceeded " + timeoutMs + "ms");
                r.elapsedMs = timeoutMs;
                r.memberDesc = String.valueOf(member);
                if (cb != null) cb.onResult(r);
            }
        }, "YambzTimeout");
        watchdog.setDaemon(true);

        worker.start();
        watchdog.start();
    }

    public static void cancel() {
        sCancelRequested = true;
        Thread t = sActiveThread;
        if (t != null && t.isAlive()) t.interrupt();
        sActiveThread = null;
    }
}
