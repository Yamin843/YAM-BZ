package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;

import qing.albatross.core.Albatross;
import qing.albatross.core.CallFrame;
import qing.albatross.core.MethodCallHook;
import qing.albatross.core.MethodHookCallback;
import qing.albatross.exception.AlbatrossException;

/**
 * BackupHookBridge: يدير backup/replace من UI.
 *
 * - backupMethod: احفظ الأصل قبل hook
 * - replaceMethod: استبدل بدون حفظ
 * - invokeOriginal من CallFrame (dynamic hook) — يعمل بدون mirror
 * - backupAndHook: للـ mirror classes (يتطلب @MethodBackup native method)
 */
public final class BackupHookBridge {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private BackupHookBridge() {}

    // ══════════════════════════════════════════════════════════
    //  backupAndHook — للـ mirror classes
    // ══════════════════════════════════════════════════════════
    public static boolean backupAndHook(Member target, Method hook, Method backup) {
        try {
            return Albatross.backupAndHook(target, hook, backup);
        } catch (AlbatrossException e) {
            Log.e(TAG, "backupAndHook failed", e);
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  replace — بدون backup
    // ══════════════════════════════════════════════════════════
    public static boolean replace(Member target, Method hook) {
        try {
            return Albatross.replace(target, hook);
        } catch (AlbatrossException e) {
            Log.e(TAG, "replace failed", e);
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  backupField — field backup
    // ══════════════════════════════════════════════════════════
    public static boolean backupField(Field target, Field backup) {
        try {
            return Albatross.backupField(target, backup);
        } catch (Throwable t) {
            Log.e(TAG, "backupField failed", t);
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Dynamic hook with invokeOriginal callback
    //  — يعطينا backup functionality بدون mirror classes
    // ══════════════════════════════════════════════════════════
    public interface DynamicBackupCallback {
        /** beforeCall — return false لإلغاء الأصل. true = نُفِّذ الأصل تلقائياً. */
        boolean before(CallFrame frame);
        /** afterCall — قراءة النتيجة */
        void after(CallFrame frame);
    }

    public static MethodCallHook hookWithBackup(Member target, final DynamicBackupCallback cb) {
        return Albatross.hookMethod(target, new MethodHookCallback() {
            @Override
            public boolean beforeCall(CallFrame frame) {
                try {
                    return cb.before(frame);
                } catch (Throwable t) {
                    Log.e(TAG, "beforeCall error", t);
                    return true;
                }
            }

            @Override
            public void afterCall(CallFrame frame) {
                try {
                    cb.after(frame);
                } catch (Throwable t) {
                    Log.e(TAG, "afterCall error", t);
                }
            }
        });
    }
}
