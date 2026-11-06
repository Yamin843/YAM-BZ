package com.yambz.call;

import android.util.Log;

import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * MethodInvoker — كامل:
 *  - sync invoke
 *  - async invoke with real timeout
 *  - batch parallel invoke
 *  - varargs handling
 */
public final class MethodInvoker {
    private static final String TAG = "YAMBZ";
    private static final int DEFAULT_TIMEOUT_MS = 30_000;

    private MethodInvoker() {}

    // ═══════════════════════════════════════════════════════════
    //  Result
    // ═══════════════════════════════════════════════════════════
    public static final class Result {
        public boolean   success;
        public Object    value;
        public Throwable error;
        public long      elapsedMs;
        public String    memberDesc;
        public String    stackTrace;
    }

    // ═══════════════════════════════════════════════════════════
    //  Callback
    // ═══════════════════════════════════════════════════════════
    public interface Callback {
        void onResult(Result r);
    }

    public interface BatchCallback {
        void onBatchComplete(List<Result> results);
    }

    // ═══════════════════════════════════════════════════════════
    //  Core: varargs resolution + invoke
    // ═══════════════════════════════════════════════════════════
    private static Object[] resolveVarargs(Method m, Object[] args) {
        if (!m.isVarArgs()) return args;
        Class<?>[] params = m.getParameterTypes();
        int fixed = params.length - 1;
        if (args.length == params.length) {
            // Caller passed array directly — respect it
            return args;
        }
        // Pack extras into varargs array
        Class<?> varType = params[fixed].getComponentType();
        int varCount = Math.max(0, args.length - fixed);
        Object varArray = Array.newInstance(varType, varCount);
        for (int i = 0; i < varCount; i++) {
            Array.set(varArray, i, args[fixed + i]);
        }
        Object[] newArgs = new Object[params.length];
        System.arraycopy(args, 0, newArgs, 0, fixed);
        newArgs[fixed] = varArray;
        return newArgs;
    }

    public static Result invoke(Member member, Object self, Object[] args) {
        Result r = new Result();
        long t0 = System.currentTimeMillis();
        try {
            r.memberDesc = member.toString();
            if (member instanceof Method) {
                Method m = (Method) member;
                m.setAccessible(true);
                Object target = Modifier.isStatic(m.getModifiers()) ? null : self;
                Object[] actualArgs = resolveVarargs(m, args);
                r.value = m.invoke(target, actualArgs);
                r.success = true;
            } else if (member instanceof Constructor) {
                Constructor<?> c = (Constructor<?>) member;
                c.setAccessible(true);
                Object[] actualArgs = resolveVarargsForCtor(c, args);
                r.value = c.newInstance(actualArgs);
                r.success = true;
            } else {
                throw new IllegalArgumentException("not Method/Constructor");
            }
        } catch (InvocationTargetException ite) {
            r.error = ite.getCause() != null ? ite.getCause() : ite;
        } catch (Throwable t) {
            r.error = t;
        }
        r.elapsedMs = System.currentTimeMillis() - t0;
        return r;
    }

    private static Object[] resolveVarargsForCtor(Constructor<?> c, Object[] args) {
        if (!c.isVarArgs()) return args;
        Class<?>[] params = c.getParameterTypes();
        int fixed = params.length - 1;
        if (args.length == params.length) return args;
        Class<?> varType = params[fixed].getComponentType();
        int varCount = Math.max(0, args.length - fixed);
        Object varArray = Array.newInstance(varType, varCount);
        for (int i = 0; i < varCount; i++) {
            Array.set(varArray, i, args[fixed + i]);
        }
        Object[] newArgs = new Object[params.length];
        System.arraycopy(args, 0, newArgs, 0, fixed);
        newArgs[fixed] = varArray;
        return newArgs;
    }

    // ═══════════════════════════════════════════════════════════
    //  Async with timeout
    // ═══════════════════════════════════════════════════════════
    private static volatile Thread sActiveThread;
    private static volatile boolean sCancelRequested;

    public static void invokeAsync(Member member, Object self, Object[] args, Callback cb) {
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

        Thread watchdog = new Thread(() -> {
            try { worker.join(timeoutMs); } catch (InterruptedException e) { return; }
            if (worker.isAlive()) {
                sCancelRequested = true;
                worker.interrupt();
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

    // ═══════════════════════════════════════════════════════════
    //  Batch invocation (parallel)
    // ═══════════════════════════════════════════════════════════
    public static final class BatchRequest {
        public final Member member;
        public final Object self;
        public final Object[] args;
        public BatchRequest(Member m, Object s, Object[] a) {
            this.member = m; this.self = s; this.args = a;
        }
    }

    public static void invokeBatch(List<BatchRequest> requests, int maxConcurrent,
                                    long perCallTimeoutMs, BatchCallback cb) {
        if (requests == null || requests.isEmpty()) {
            if (cb != null) cb.onBatchComplete(new ArrayList<>());
            return;
        }
        ExecutorService pool = Executors.newFixedThreadPool(
            Math.max(1, Math.min(maxConcurrent, requests.size())));
        List<Future<Result>> futures = new ArrayList<>(requests.size());
        for (BatchRequest req : requests) {
            futures.add(pool.submit(() -> invoke(req.member, req.self, req.args)));
        }

        new Thread(() -> {
            List<Result> results = new ArrayList<>(requests.size());
            for (int i = 0; i < futures.size(); i++) {
                try {
                    Result r = futures.get(i).get(perCallTimeoutMs, TimeUnit.MILLISECONDS);
                    results.add(r);
                } catch (Throwable e) {
                    Result r = new Result();
                    r.success = false;
                    r.error = e;
                    r.memberDesc = String.valueOf(requests.get(i).member);
                    results.add(r);
                }
            }
            pool.shutdownNow();
            if (cb != null) cb.onBatchComplete(results);
        }, "YambzBatch").start();
    }
}
