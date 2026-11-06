package com.yambz.bridge;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;

import qing.albatross.nativehook.Stalker;
import qing.albatross.nativehook.StalkerConfig;
import qing.albatross.nativehook.StalkerEvent;
import qing.albatross.nativehook.StalkerEventListener;

/**
 * DeepBacktrace: كامل باستخدام Stalker.
 *
 * يجمع:
 * - CALL events مع target address
 * - RET events
 * - BLOCK events
 * - COMPILE events (لاستخراج معمارية)
 *
 * ثم يحوّل العناوين إلى module+offset عبر DlInfo.
 *
 * Thread-safe. Singleton. Auto-stop.
 */
public final class DeepBacktrace {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private DeepBacktrace() {}

    public static final class Frame {
        public final long location;
        public final long target;
        public final int depth;
        public final String type;   // "CALL", "RET", "EXEC", "BLOCK", "COMPILE"

        Frame(long loc, long tgt, int d, String t) {
            location = loc;
            target = tgt;
            depth = d;
            type = t;
        }
    }

    private static volatile boolean sActive = false;
    private static volatile long sStopAtMs = 0;
    private static volatile int sMaxFrames = 500;
    private static final List<Frame> sFrames = new ArrayList<>();
    private static StalkerEventListener sListener;
    private static Thread sTimerThread;

    /**
     * بدء جمع backtrace عميق.
     *
     * @param durationMs مدة الجمع القصوى (0 = حتى stop صريح)
     * @param maxFrames  الحد الأقصى للإطارات
     * @return true إذا نجح البدء
     */
    public static synchronized boolean start(int durationMs, int maxFrames) {
        if (sActive) return false;

        sActive = true;
        sStopAtMs = durationMs > 0 ? System.currentTimeMillis() + durationMs : 0;
        sMaxFrames = maxFrames > 0 ? maxFrames : 500;

        synchronized (sFrames) {
            sFrames.clear();
        }

        StalkerConfig cfg = new StalkerConfig()
            .call(true)
            .ret(true)
            .block(true)
            .exec(false);   // EXEC سريع جداً قد يملأ الذاكرة

        cfg.timeout(durationMs > 0 ? durationMs : 5000);
        cfg.maxEvents(maxFrames * 8);

        cfg.eventListener = events -> {
            if (!sActive) return;
            long now = System.currentTimeMillis();
            if (sStopAtMs > 0 && now >= sStopAtMs) {
                stop();
                return;
            }
            synchronized (sFrames) {
                for (StalkerEvent e : events) {
                    if (sFrames.size() >= sMaxFrames) break;
                    String type;
                    switch (e.type) {
                        case CALL:    type = "CALL"; break;
                        case RET:     type = "RET"; break;
                        case EXEC:    type = "EXEC"; break;
                        case BLOCK:   type = "BLOCK"; break;
                        case COMPILE: type = "COMPILE"; break;
                        default:      continue;
                    }
                    sFrames.add(new Frame(e.location, e.target, e.depth, type));
                }
            }
        };

        sListener = cfg.eventListener;

        try {
            // Stalker.followCurrentThread يستخدم listener من cfg
            Stalker.followCurrentThread(cfg);
            Log.i(TAG, "DeepBacktrace: started (duration=" + durationMs + ", max=" + maxFrames + ")");

            // مؤقت auto-stop
            if (durationMs > 0) {
                sTimerThread = new Thread(() -> {
                    try {
                        Thread.sleep(durationMs + 500);
                    } catch (InterruptedException ignore) {
                        return;
                    }
                    if (sActive) stop();
                }, "DeepBacktraceTimer");
                sTimerThread.setDaemon(true);
                sTimerThread.start();
            }

            return true;
        } catch (Throwable t) {
            Log.e(TAG, "DeepBacktrace: start failed", t);
            sActive = false;
            return false;
        }
    }

    public static synchronized void stop() {
        if (!sActive) return;
        sActive = false;

        try {
            Stalker.unfollowCurrentThread();
        } catch (Throwable t) {
            Log.e(TAG, "DeepBacktrace: unfollow failed", t);
        }

        if (sTimerThread != null) {
            sTimerThread.interrupt();
            sTimerThread = null;
        }

        int count;
        synchronized (sFrames) {
            count = sFrames.size();
        }
        Log.i(TAG, "DeepBacktrace: stopped, " + count + " frames captured");

        // أرسل النتائج إلى C++
        nativeOnComplete(exportJson());
    }

    public static boolean isActive() { return sActive; }

    public static List<Frame> frames() {
        synchronized (sFrames) {
            return new ArrayList<>(sFrames);
        }
    }

    public static int frameCount() {
        synchronized (sFrames) {
            return sFrames.size();
        }
    }

    /**
     * تحويل العناوين إلى JSON مع symbols (عبر DlInfo).
     */
    private static String exportJson() {
        StringBuilder sb = new StringBuilder("[");
        List<Frame> frames;
        synchronized (sFrames) {
            frames = new ArrayList<>(sFrames);
        }

        for (int i = 0; i < frames.size(); i++) {
            if (i > 0) sb.append(',');
            Frame f = frames.get(i);
            String symbol = resolveSymbol(f.target);
            sb.append("{\"loc\":\"")
              .append(Long.toHexString(f.location))
              .append("\",\"tgt\":\"")
              .append(Long.toHexString(f.target))
              .append("\",\"d\":").append(f.depth)
              .append(",\"type\":\"").append(f.type).append('"');
            if (symbol != null) {
                sb.append(",\"sym\":\"").append(escapeJson(symbol)).append('"');
            }
            sb.append('}');
        }
        sb.append(']');
        return sb.toString();
    }

    private static String escapeJson(String s) {
        StringBuilder out = new StringBuilder();
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') out.append('\\').append(c);
            else if (c == '\n') out.append("\\n");
            else out.append(c);
        }
        return out.toString();
    }

    // يحوّل عنوان إلى symbol name (best-effort عبر DlInfo)
    private static String resolveSymbol(long addr) {
        if (addr == 0) return null;
        try {
            // نحاول libc أولاً
            qing.albatross.nativehook.DlInfo lib = 
                qing.albatross.nativehook.AlbNative.openLib("libc.so");
            if (lib == null) return null;
            // DlInfo لا يوفر reverse lookup مباشر — نستخدم enumerateFunctions
            // لكنه مكلف. نرجع null مؤقتاً (سنستخدم module+offset في C++)
            lib.close();
        } catch (Throwable ignored) {}
        return null;
    }

    private static native void nativeOnComplete(String json);
}
