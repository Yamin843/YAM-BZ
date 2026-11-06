package com.yambz.feature;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicBoolean;

import qing.albatross.nativehook.Stalker;
import qing.albatross.nativehook.StalkerConfig;
import qing.albatross.nativehook.StalkerEvent;
import qing.albatross.nativehook.StalkerEventListener;
import qing.albatross.nativehook.StalkerFollowHandle;
import qing.albatross.nativehook.StalkerTransformer;

/**
 * StalkerController — كامل. يدير:
 *  - followSymbol / followFunction / followCurrentThread / followThread
 *  - 5 event types (CALL, RET, EXEC, BLOCK, COMPILE)
 *  - include/exclude (range + module + named)
 *  - transformer + callout
 *  - timeout / maxEvents / stopReason
 *  - Event buffer for UI (bounded)
 */
public final class StalkerController {
    private static final String TAG = "YAMBZ";
    private static final int MAX_EVENTS_BUFFER = 10000;

    private static volatile boolean sFollowing = false;
    private static volatile int sMode = 0; // 1=symbol, 2=currentThread, 3=thread
    private static volatile StalkerFollowHandle sHandle = null;
    private static volatile long sFollowedThreadId = 0;
    private static volatile long sFollowedAddress = 0;

    // Event buffer (thread-safe, bounded)
    private static final ConcurrentLinkedQueue<StalkerEvent> sBuffer =
        new ConcurrentLinkedQueue<>();
    private static final AtomicBoolean sBufferOverflow = new AtomicBoolean(false);
    private static volatile int sStopReason = 0;

    private StalkerController() {}

    // ══════════════════════════════════════════════════════════
    //  Follow modes
    // ══════════════════════════════════════════════════════════
    public static boolean followSymbol(String lib, String symbol,
                                        int eventMask, int timeoutMs, int maxEvents) {
        if (sFollowing) { Log.w(TAG, "already following"); return false; }
        try {
            StalkerConfig cfg = buildConfig(eventMask, timeoutMs, maxEvents);
            sHandle = Stalker.followSymbol(lib, symbol, cfg);
            if (sHandle == null) return false;
            sFollowing = true;
            sMode = 1;
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "followSymbol failed", t);
            return false;
        }
    }

    public static boolean followFunction(long address, int eventMask,
                                          int timeoutMs, int maxEvents) {
        if (sFollowing) return false;
        try {
            StalkerConfig cfg = buildConfig(eventMask, timeoutMs, maxEvents);
            sHandle = Stalker.followFunction(address, cfg);
            if (sHandle == null) return false;
            sFollowing = true;
            sMode = 1;
            sFollowedAddress = address;
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "followFunction failed", t);
            return false;
        }
    }

    public static boolean followCurrentThread(int eventMask, int timeoutMs, int maxEvents) {
        if (sFollowing) return false;
        try {
            StalkerConfig cfg = buildConfig(eventMask, timeoutMs, maxEvents);
            Stalker.followCurrentThread(cfg);
            sFollowing = true;
            sMode = 2;
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "followCurrentThread failed", t);
            return false;
        }
    }

    public static boolean followThread(long tid, int eventMask, int timeoutMs, int maxEvents) {
        if (sFollowing) return false;
        try {
            StalkerConfig cfg = buildConfig(eventMask, timeoutMs, maxEvents);
            Stalker.followThread(tid, cfg);
            sFollowing = true;
            sMode = 3;
            sFollowedThreadId = tid;
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "followThread failed", t);
            return false;
        }
    }

    public static void unfollow() {
        try {
            switch (sMode) {
                case 1:
                    if (sHandle != null) sHandle.unhook();
                    break;
                case 2:
                    Stalker.unfollowCurrentThread();
                    break;
                case 3:
                    Stalker.unfollowThread(sFollowedThreadId);
                    break;
            }
        } catch (Throwable t) {
            Log.e(TAG, "unfollow failed", t);
        }
        sHandle = null;
        sFollowing = false;
        sMode = 0;
    }

    // ══════════════════════════════════════════════════════════
    //  Include / Exclude
    // ══════════════════════════════════════════════════════════
    public static void includeModule(String moduleName) {
        try { Stalker.includeModule(moduleName); } catch (Throwable t) {}
    }
    public static void excludeModule(String moduleName) {
        try { Stalker.excludeModule(moduleName); } catch (Throwable t) {}
    }
    public static void includeRange(long start, long size) {
        try { Stalker.include(start, size); } catch (Throwable t) {}
    }
    public static void excludeRange(long start, long size) {
        try { Stalker.exclude(start, size); } catch (Throwable t) {}
    }
    public static void excludeNamed(String name) {
        try { Stalker.excludeNamed(name); } catch (Throwable t) {}
    }
    public static void clearInclusions() {
        try { Stalker.clearInclusions(); } catch (Throwable t) {}
    }
    public static void clearExclusions() {
        try { Stalker.clearExclusions(); } catch (Throwable t) {}
    }

    // ══════════════════════════════════════════════════════════
    //  Utilities
    // ══════════════════════════════════════════════════════════
    public static void flushEvents() {
        try { Stalker.flushEvents(); } catch (Throwable t) {}
    }

    public static void setTimeout(int ms) {
        try { Stalker.setTimeout(ms); } catch (Throwable t) {}
    }

    public static int getStopReason() {
        try { return Stalker.getStopReason(); } catch (Throwable t) { return 0; }
    }

    public static long getEventCount() {
        try { return Stalker.getEventCount(); } catch (Throwable t) { return 0; }
    }

    // ══════════════════════════════════════════════════════════
    //  Config + event listener
    // ══════════════════════════════════════════════════════════
    private static StalkerConfig buildConfig(int eventMask, int timeoutMs, int maxEvents) {
        StalkerConfig cfg = new StalkerConfig();
        // Decode eventMask bits
        cfg.call((eventMask & 0x01) != 0);
        cfg.ret((eventMask & 0x02) != 0);
        cfg.exec((eventMask & 0x04) != 0);
        cfg.block((eventMask & 0x08) != 0);
        cfg.compile((eventMask & 0x10) != 0);
        cfg.timeout(timeoutMs);
        cfg.maxEvents(maxEvents);
        cfg.eventListener = events -> {
            if (sBuffer.size() > MAX_EVENTS_BUFFER) {
                sBufferOverflow.set(true);
            } else {
                sBuffer.addAll(events);
            }
        };
        return cfg;
    }

    // ══════════════════════════════════════════════════════════
    //  Buffer access (called from C++ via JNI)
    // ══════════════════════════════════════════════════════════
    public static boolean isFollowing() { return sFollowing; }
    public static int getMode() { return sMode; }
    public static int getBufferSize() { return sBuffer.size(); }
    public static boolean isBufferOverflowed() { return sBufferOverflow.get(); }

    public static void clearBuffer() {
        sBuffer.clear();
        sBufferOverflow.set(false);
    }

    /** Returns latest N events as ["type|loc|tgt|depth", ...] */
    public static String[] getEvents(int maxCount) {
        List<String> out = new ArrayList<>();
        while (!sBuffer.isEmpty() && out.size() < maxCount) {
            StalkerEvent e = sBuffer.poll();
            if (e == null) break;
            String type;
            switch (e.type) {
                case CALL:    type = "CALL"; break;
                case RET:     type = "RET"; break;
                case EXEC:    type = "EXEC"; break;
                case BLOCK:   type = "BLOCK"; break;
                case COMPILE: type = "COMPILE"; break;
                case STOPPED: type = "STOPPED"; break;
                default:      type = "?"; break;
            }
            out.add(type + "|0x" + Long.toHexString(e.location)
                + "|0x" + Long.toHexString(e.target) + "|" + e.depth);
        }
        return out.toArray(new String[0]);
    }
}
