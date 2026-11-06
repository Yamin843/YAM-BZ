package com.yambz.call;

import android.util.Log;

import com.yambz.bridge.ObjectDumper;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import qing.albatross.core.Albatross;

/**
 * يبحث في heap عن instances من type معيّن — batched streaming.
 *
 * الميزات:
 *   - بلا حد أقصى: تُمرَّر كل النتائج عبر callback
 *   - Batched: كل ~500 نتيجة يُنادى callback
 *   - Cancelable
 *   - يأخذ title hint من toString (truncated)
 *   - يحفظ WeakRef لكل instance
 *
 * الاستخدام:
 *   HeapScanner.start(ArrayList.class, scanner -> { ... });
 *   ... يمكن إيقافه بـ scanner.cancel()
 */
public final class HeapScanner {
    private static final String TAG = "YAMBZ";
    private static final int BATCH_SIZE = 500;

    public static final class Entry {
        public final int  idx;
        public final long addr;             // object address (Albatross)
        public final String display;        // truncated toString
        public final WeakReference<Object> ref;

        Entry(int idx, long addr, String display, Object obj) {
            this.idx = idx;
            this.addr = addr;
            this.display = display;
            this.ref = new WeakReference<>(obj);
        }
    }

    public interface Callback {
        /** @param entries batch (≤ BATCH_SIZE). @return false للإلغاء */
        boolean onBatch(List<Entry> entries);
        void onComplete(int total);
        void onError(Throwable t);
    }

    private final Class<?> targetType;
    private final Callback callback;
    private final AtomicBoolean cancelRequested = new AtomicBoolean(false);
    private final AtomicInteger seenCount = new AtomicInteger(0);
    private volatile Thread thread;

    private HeapScanner(Class<?> type, Callback cb) {
        this.targetType = type;
        this.callback = cb;
    }

    public static HeapScanner start(Class<?> type, Callback cb) {
        HeapScanner s = new HeapScanner(type, cb);
        s.thread = new Thread(s::run, "YambzHeapScan");
        s.thread.setDaemon(true);
        s.thread.start();
        return s;
    }

    public void cancel() {
        cancelRequested.set(true);
    }

    private void run() {
        try {
            List<Entry> batch = new ArrayList<>(BATCH_SIZE);
            Albatross.searchObject(targetType, (obj, i) -> {
                if (cancelRequested.get()) return false;

                int idx = seenCount.getAndIncrement();
                String display;
                try { display = ObjectDumper.dump(obj); }
                catch (Throwable t) { display = "<dump-error>"; }

                // truncate
                if (display.length() > 160)
                    display = display.substring(0, 160) + "...";

                long addr = 0;
                try { addr = Albatross.getObjectAddress(obj); }
                catch (Throwable ignored) {}

                batch.add(new Entry(idx, addr, display, obj));

                if (batch.size() >= BATCH_SIZE) {
                    List<Entry> copy = new ArrayList<>(batch);
                    batch.clear();
                    if (!callback.onBatch(copy)) {
                        cancelRequested.set(true);
                        return false;
                    }
                }
                return true;
            });

            // flush remaining
            if (!cancelRequested.get() && !batch.isEmpty()) {
                callback.onBatch(batch);
            }
            callback.onComplete(seenCount.get());
        } catch (Throwable t) {
            Log.e(TAG, "heap scan", t);
            callback.onError(t);
        }
    }
}
