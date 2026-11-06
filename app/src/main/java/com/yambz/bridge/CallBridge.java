package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Member;
import java.util.ArrayList;
import java.util.List;

/**
 * جسر C++ → CallEngine.
 */
public final class CallBridge {
    private static final String TAG = "YAMBZ";

    static {
        try { System.loadLibrary("yambz"); } catch (Throwable ignored) {}
    }

    private CallBridge() {}

    /** يُستدعى من C++ UI — يبدأ الاستدعاء async. */
    public static boolean invoke(Member method, String methodDesc,
                                 String thisJson, String[] argsJson) {
        try {
            return nativeInvoke(method, methodDesc, thisJson, argsJson);
        } catch (Throwable t) {
            Log.e(TAG, "invoke", t);
            return false;
        }
    }

    public static boolean isRunning() {
        try { return nativeIsRunning(); } catch (Throwable t) { return false; }
    }

    public static String lastResultJson() {
        try { return nativeLastResultJson(); } catch (Throwable t) { return "{}"; }
    }

    public static void clear() {
        try { nativeClear(); } catch (Throwable t) {}
    }

    private static native boolean nativeInvoke(Member method, String methodDesc,
                                                String thisJson, String[] argsJson);
    private static native boolean nativeIsRunning();
    private static native String  nativeLastResultJson();
    private static native void    nativeClear();

    // ── Presets ──
    public static boolean savePreset(String methodDesc, String name, String json) {
        try { return nativeSavePreset(methodDesc, name, json); }
        catch (Throwable t) { return false; }
    }
    public static String[] listPresets(String methodDesc) {
        try { return nativeListPresets(methodDesc); }
        catch (Throwable t) { return new String[0]; }
    }
    private static native boolean nativeSavePreset(String methodDesc, String name, String json);
    private static native String[] nativeListPresets(String methodDesc);
}
