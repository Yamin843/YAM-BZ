package com.yambz.bridge;

import android.util.Log;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import qing.albatross.nativehook.Stalker;
import qing.albatross.nativehook.StalkerConfig;
import qing.albatross.nativehook.StalkerEvent;
import qing.albatross.nativehook.StalkerFollowHandle;

/**
 * Deep backtrace عبر Stalker — يجمع عناوين native تعليمات لعدة مئات من الإطارات.
 *
 * مكلف — استخدمه فقط عند الطلب الصريح.
 *
 * الاستراتيجية:
 *   1. Stalker.followCurrentThread مع call=true
 *   2. جمع CALL events لفترة قصيرة (50ms)
 *   3. تحويل العناوين إلى module+offset عبر DlInfo
 *   4. dedupe + cap
 *
 * القيود:
 *   - arm64 cross-thread follow غير جاهز — نستخدم currentThread فقط
 *   - يوقف الجمع تلقائياً بعد timeout
 *   - يجب فتح جلسة واحدة فقط في وقت واحد
 */
public final class DeepBacktrace {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private DeepBacktrace() {}

    private static volatile boolean sActive = false;
    private static StalkerFollowHandle sHandle;
    private static final List<Frame> sFrames = new ArrayList<>();

    public static final class Frame {
        public final long location;
        public final long target;
        public final int depth;
        Frame(long loc, long tgt, int d) { location = loc; target = tgt; depth = d; }
    }

    /**
     * بدء جمع backtrace عميق لمدة {durationMs} كحد أقصى.
     * @param maxFrames حد أعلى (default 500)
     * @return true إذا نجح البدء
     */
    public static boolean start(int durationMs, int maxFrames) {
        if (sActive) return false;
        sActive = true;
        synchronized (sFrames) { sFrames.clear(); }

        StalkerConfig cfg = new StalkerConfig().call(true).ret(false).block(false);
        cfg.timeout(durationMs);
        cfg.maxEvents(maxFrames * 4);
        cfg.eventListener = events -> {
            for (StalkerEvent e : events) {
                if (e.type == StalkerEvent.Type.CALL) {
                    synchronized (sFrames) {
                        if (sFrames.size() < maxFrames) {
                            sFrames.add(new Frame(e.location, e.target, e.depth));
                        }
                    }
                }
            }
        };
        cfg.transformer = null;

        try {
            // تسجيل بدء
            Log.i(TAG, "DeepBacktrace: start " + durationMs + "ms");
            sHandle = Stalker.followFunction(
                nativeAddressOfNextFrame(), cfg);
            if (sHandle == null) {
                Log.e(TAG, "DeepBacktrace: followFunction returned null");
                sActive = false;
                return false;
            }
            // جدولة إيقاف تلقائي
            new Thread(() -> {
                try { Thread.sleep(durationMs + 200); }
                catch (InterruptedException ignore) {}
                stop();
            }, "DeepBacktraceTimer").start();
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "DeepBacktrace start failed", t);
            sActive = false;
            return false;
        }
    }

    public static void stop() {
        if (sHandle != null) {
            try { sHandle.unhook(); } catch (Throwable ignored) {}
            sHandle = null;
        }
        if (sActive) {
            sActive = false;
            Log.i(TAG, "DeepBacktrace: stop, " + sFrames.size() + " frames");
            nativeOnComplete(exportJson());
        }
    }

    public static boolean isActive() { return sActive; }

    public static List<Frame> frames() {
        synchronized (sFrames) { return new ArrayList<>(sFrames); }
    }

    /** تحويل العناوين إلى JSON ليقرأها C++ */
    private static String exportJson() {
        StringBuilder sb = new StringBuilder("[");
        synchronized (sFrames) {
            for (int i = 0; i < sFrames.size(); i++) {
                if (i > 0) sb.append(',');
                Frame f = sFrames.get(i);
                sb.append("{\"loc\":\"").append(Long.toHexString(f.location))
                  .append("\",\"tgt\":\"").append(Long.toHexString(f.target))
                  .append("\",\"d\":").append(f.depth).append('}');
            }
        }
        sb.append(']');
        return sb.toString();
    }

    // يعطي عنواناً حقيقياً (native) لآخر إطار — نستخدم أول symbol في libc
    private static native long nativeAddressOfNextFrame();
    private static native void nativeOnComplete(String json);
}
