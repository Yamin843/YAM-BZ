package com.yambz.util;

import android.util.Log;

import java.io.PrintWriter;
import java.io.StringWriter;

/**
 * توحيد تحويل الاستثناءات إلى C++ بدون رمي.
 *
 * كل دالة في هذه الحزمة:
 *   - try/catch شامل
 *   - على الفشل: nativeReportError(...)
 *   - إرجاع قيمة افتراضية آمنة
 *
 * يتيح لـ C++ تقديم "تدهور لطيف" بدل crash.
 */
public final class ErrorBridge {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private ErrorBridge() {}

    // ─── domains ───
    public static final int DOM_REFLECTION = 0;
    public static final int DOM_HOOK       = 1;
    public static final int DOM_DUMP       = 2;
    public static final int DOM_BUILD      = 3;
    public static final int DOM_INVOKE     = 4;
    public static final int DOM_NATIVE     = 5;
    public static final int DOM_IO         = 6;
    public static final int DOM_INTERNAL   = 7;

    public static void report(int domain, String op, String target, Throwable t) {
        try {
            String stack = stackTraceOf(t);
            nativeReport(domain, op, target != null ? target : "",
                        t != null ? t.toString() : "unknown",
                        stack);
        } catch (Throwable inner) {
            Log.e(TAG, "error report itself failed", inner);
        }
    }

    public static String stackTraceOf(Throwable t) {
        if (t == null) return "";
        try {
            StringWriter sw = new StringWriter();
            t.printStackTrace(new PrintWriter(sw));
            String s = sw.toString();
            // cap
            return s.length() > 4096 ? s.substring(0, 4096) + "...[trunc]" : s;
        } catch (Throwable ignored) { return ""; }
    }

    // ─── Helpers ───

    /** آمن: ينفذ Runnable ويعيد true على النجاح. */
    public static boolean safeRun(int domain, String op, Runnable r) {
        try { r.run(); return true; }
        catch (Throwable t) { report(domain, op, "", t); return false; }
    }

    /** آمن: يرجع fallback عند الفشل. */
    public static <T> T safeGet(int domain, String op, T fallback, ThrowingSupplier<T> s) {
        try { return s.get(); }
        catch (Throwable t) { report(domain, op, "", t); return fallback; }
    }

    public interface ThrowingSupplier<T> {
        T get() throws Throwable;
    }

    private static native void nativeReport(int domain, String op, String target,
                                            String message, String stackTrace);
}
