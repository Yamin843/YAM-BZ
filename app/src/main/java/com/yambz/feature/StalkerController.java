package com.yambz.feature;

import android.util.Log;

import qing.albatross.nativehook.Stalker;
import qing.albatross.nativehook.StalkerConfig;

public final class StalkerController {
    private static final String TAG = "YAMBZ";

    private StalkerController() {}

    public static boolean followSymbol(String lib, String symbol) {
        try {
            StalkerConfig cfg = new StalkerConfig().call(true).ret(true).block(false);
            cfg.timeout(5000);
            cfg.eventListener = events -> {
                for (qing.albatross.nativehook.StalkerEvent e : events) {
                    if (e.type == qing.albatross.nativehook.StalkerEvent.Type.CALL) {
                        Log.i(TAG, "stalker CALL " + Long.toHexString(e.location)
                            + " → " + Long.toHexString(e.target));
                    }
                }
            };
            Stalker.followSymbol(lib, symbol, cfg);
            Log.i(TAG, "Stalker following " + lib + "!" + symbol);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "followSymbol failed", t);
            return false;
        }
    }

    public static void followCurrentThread(String a, String b) {
        try {
            StalkerConfig cfg = new StalkerConfig().call(true);
            cfg.timeout(5000);
            Stalker.followCurrentThread(cfg);
            Log.i(TAG, "Stalker following current thread");
        } catch (Throwable t) {
            Log.e(TAG, "followCurrentThread failed", t);
        }
    }

    public static void unfollow() {
        try {
            Stalker.unfollowCurrentThread();
            Log.i(TAG, "Stalker unfollowed");
        } catch (Throwable t) {
            Log.e(TAG, "unfollow failed", t);
        }
    }
}
