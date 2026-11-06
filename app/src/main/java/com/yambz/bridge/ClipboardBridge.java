package com.yambz.bridge;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/**
 * ClipboardBridge — copy text to Android clipboard.
 * Posts to main looper (required for ClipboardManager).
 */
public final class ClipboardBridge {
    private static final String TAG = "YAMBZ";

    private ClipboardBridge() {}

    public static void copy(String text) {
        if (text == null) return;
        try {
            final Context ctx = UIBridge.getAppContext();
            if (ctx == null) {
                Log.w(TAG, "no app context — clipboard unavailable");
                return;
            }
            new Handler(Looper.getMainLooper()).post(() -> {
                try {
                    ClipboardManager cm = (ClipboardManager)
                        ctx.getSystemService(Context.CLIPBOARD_SERVICE);
                    if (cm != null) {
                        cm.setPrimaryClip(ClipData.newPlainText("YAM-BZ", text));
                    }
                } catch (Throwable t) {
                    Log.e(TAG, "clipboard copy failed", t);
                }
            });
        } catch (Throwable t) {
            Log.e(TAG, "copy failed", t);
        }
    }
}
