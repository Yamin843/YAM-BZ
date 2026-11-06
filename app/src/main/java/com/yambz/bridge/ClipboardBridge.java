package com.yambz.bridge;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.util.Log;

/**
 * نسخ نص إلى الحافظة — من أي thread.
 * يجب استدعاؤه من UI thread للـ ClipboardManager، لكننا نُمرّر إلى main looper.
 */
public final class ClipboardBridge {
    private static final String TAG = "YAMBZ";

    private ClipboardBridge() {}

    public static void copyToClipboard(final String text) {
        if (text == null) return;
        android.os.Handler h = null;
        try {
            h = new android.os.Handler(android.os.Looper.getMainLooper());
        } catch (Throwable ignored) {}
        if (h != null) {
            h.post(() -> doCopy(text));
        } else {
            doCopy(text);
        }
    }

    private static void doCopy(String text) {
        try {
            Context ctx = qing.albatross.core.Albatross.currentApplication();
            if (ctx == null) return;
            ClipboardManager cm = (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null) return;
            cm.setPrimaryClip(ClipData.newPlainText("YAM-BZ", text));
        } catch (Throwable t) {
            Log.e(TAG, "copy failed", t);
        }
    }
}
