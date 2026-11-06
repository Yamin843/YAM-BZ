package com.yambz.call;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;

/**
 * يغلّف HeapScanner.Callback → C++ JNI.
 *
 * يستخدم آلية WeakRef: يتحول كل Entry إلى مقطع صغير (idx, addr, display, ref)
 * ثم يُرسل كـ Object[] عبر JNI.
 */
public final class HeapScannerCallback implements HeapScanner.Callback {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private static volatile HeapScanner sCurrent;
    private volatile boolean cancelled;

    public HeapScannerCallback() {}

    @Override
    public boolean onBatch(List<HeapScanner.Entry> entries) {
        if (cancelled) return false;
        try {
            int n = entries.size();
            Object[][] payload = new Object[n][];
            for (int i = 0; i < n; i++) {
                HeapScanner.Entry e = entries.get(i);
                payload[i] = new Object[]{
                    new BridgeEntry(e.idx, e.addr, e.display, e.ref.get())
                };
            }
            // نُرسل مصفوفة BridgeEntry[] مباشرة
            BridgeEntry[] be = new BridgeEntry[n];
            for (int i = 0; i < n; i++) {
                HeapScanner.Entry e = entries.get(i);
                be[i] = new BridgeEntry(e.idx, e.addr, e.display, e.ref.get());
            }
            nativeOnBatch(be);
        } catch (Throwable t) {
            Log.e(TAG, "onBatch", t);
        }
        return true;
    }

    @Override
    public void onComplete(int total) {
        if (cancelled) return;
        nativeOnComplete(total);
    }

    @Override
    public void onError(Throwable t) {
        nativeOnError(t);
    }

    public static void cancelCurrent() {
        if (sCurrent != null) sCurrent.cancel();
        sCurrent = null;
    }

    // ─── BridgeEntry: يُقرأ من C++ عبر JNI ───
    public static final class BridgeEntry {
        public final int idx;
        public final long addr;
        public final String display;
        public final Object ref;

        public BridgeEntry(int idx, long addr, String display, Object ref) {
            this.idx = idx;
            this.addr = addr;
            this.display = display;
            this.ref = ref;
        }

        // accessors C++ سيستدعيها
        public int getInt0() { return idx; }
        public long getLong0() { return addr; }
        public String getStr0() { return display; }
        public Object getRef0() { return ref; }
    }

    private static native void nativeOnBatch(BridgeEntry[] entries);
    private static native void nativeOnComplete(int total);
    private static native void nativeOnError(Throwable t);
}
