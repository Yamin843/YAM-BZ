package com.yambz.bridge;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import qing.albatross.core.Albatross;

/**
 * HeapBridge — searchObject streaming.
 */
public final class HeapBridge {
    private static final String TAG = "YAMBZ";

    private static final AtomicBoolean sCancelled = new AtomicBoolean(false);
    private static final List<String> sResults = new ArrayList<>();

    private HeapBridge() {}

    public static void startSearch(String className) {
        sCancelled.set(false);
        synchronized (sResults) { sResults.clear(); }

        new Thread(() -> {
            try {
                Class<?> cls = ClassBridge.findClass(className);
                if (cls == null) { Log.e(TAG, "class not found: " + className); return; }

                Albatross.searchObject(cls, (obj, i) -> {
                    if (sCancelled.get()) return false;
                    String dump = ObjectDumper.dump(obj);
                    long addr = 0;
                    try { addr = Albatross.getObjectAddress(obj); } catch (Throwable ignored) {}
                    synchronized (sResults) {
                        sResults.add("0x" + Long.toHexString(addr) + " | " + dump);
                    }
                    return sResults.size() < 200;
                });
            } catch (Throwable t) {
                Log.e(TAG, "searchObject failed", t);
            }
        }, "heap-search").start();
    }

    public static void cancel() { sCancelled.set(true); }

    public static int count() {
        synchronized (sResults) { return sResults.size(); }
    }

    public static String[] getResults() {
        synchronized (sResults) {
            return sResults.toArray(new String[0]);
        }
    }
}
