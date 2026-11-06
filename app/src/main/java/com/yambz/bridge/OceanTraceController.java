package com.yambz.bridge;

import android.util.Log;

import qing.albatross.tracker.DefaultSinker;
import qing.albatross.tracker.EmptySinker;
import qing.albatross.tracker.EventType;
import qing.albatross.tracker.KeyWordSinker;
import qing.albatross.tracker.OceanSinker;
import qing.albatross.tracker.OceanTracker;
import qing.albatross.tracker.StringSinker;

/**
 * OceanTraceController: يدير بدء/إيقاف Ocean trace من C++.
 *
 * يستدعي OceanTracker.traceMe/traceGlobal مع sinker مخصص
 * (OceanTraceBridge) يمرر كل شيء إلى C++ UI.
 */
public final class OceanTraceController {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private static volatile OceanSinker sActiveSinker = null;
    private static volatile int sActiveSessionId = 0;

    private OceanTraceController() {}

    /**
     * بدء trace.
     *
     * @param eventMask بتة لكل نوع حدث (راجع OceanTracePanel.cpp)
     * @param scope   kApplication (4) افتراضي
     * @param timeoutMs  60000
     * @param maxEvents  200000
     * @param sinkerKind  0=DefaultSinker, 1=KeyWordSinker, 2=StringSinker, 3=EmptySinker
     * @param onlyMainThread  0/1
     * @param packageFilter  للـ kPackageFilter scope
     * @param methodPtr  jobject Method إذا traceMember (0 = global)
     * @param global  1 = traceGlobal (all threads), 0 = traceMe (current thread)
     */
    public static boolean startTrace(int eventMask, int scope, int timeoutMs,
                                     int maxEvents, int sinkerKind, int onlyMainThread,
                                     String packageFilter, Object methodPtr, boolean global) {
        if (sActiveSinker != null) {
            Log.w(TAG, "OceanTraceController: already tracing");
            return false;
        }

        if (!OceanTracker.init(0)) {
            Log.e(TAG, "OceanTracker.init failed");
            return false;
        }

        // Apply limits
        OceanTracker.setTraceScope(scope);
        OceanTracker.setTraceTimeout(timeoutMs);
        OceanTracker.setTraceMaxEvents(maxEvents);

        // Apply package filter
        if (packageFilter != null && !packageFilter.isEmpty()) {
            OceanTracker.addWhitePackage(packageFilter);
        }

        // Create bridge
        int sessionId = (int)(System.currentTimeMillis() & 0x7FFFFFFF);
        OceanTraceBridge bridge = new OceanTraceBridge(sessionId);

        sActiveSinker = bridge;
        sActiveSessionId = sessionId;

        boolean ok;
        if (global) {
            ok = OceanTracker.traceGlobal(bridge, eventMask, timeoutMs);
        } else {
            ok = OceanTracker.traceMe(bridge, eventMask, timeoutMs);
        }

        if (ok) {
            Log.i(TAG, "OceanTraceController: started session=" + sessionId
                + " mask=0x" + Integer.toHexString(eventMask)
                + " scope=" + scope
                + " global=" + global);
            return true;
        } else {
            Log.e(TAG, "OceanTraceController: startTrace returned false");
            sActiveSinker = null;
            sActiveSessionId = 0;
            return false;
        }
    }

    public static void stopTrace() {
        if (sActiveSinker == null) return;

        try {
            // Try both — whichever was active
            OceanTracker.stopTraceMe();
            OceanTracker.stopTraceGlobal();
        } catch (Throwable t) {
            Log.e(TAG, "OceanTraceController: stop failed", t);
        }

        sActiveSinker = null;
        sActiveSessionId = 0;
        Log.i(TAG, "OceanTraceController: stopped");
    }

    public static boolean isTracing() {
        return sActiveSinker != null;
    }

    public static int getSessionId() {
        return sActiveSessionId;
    }
}
