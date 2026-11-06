package com.yambz.feature;

import android.util.Log;

import com.yambz.bridge.OceanSinkerBridge;

import java.lang.reflect.Constructor;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.util.concurrent.ConcurrentHashMap;

import qing.albatross.tracker.OceanSinker;
import qing.albatross.tracker.OceanTracker;

/**
 * OceanController — تحكم كامل في OceanTracker.
 *
 * 3 أوضاع:
 *  - Global: traceGlobal (كل الـ threads)
 *  - Thread: traceMe (current thread)
 *  - Member: traceMemberNative (member محدد — via reflection)
 *
 * يدعم:
 *  - 8 event types
 *  - 5 scopes
 *  - 4 sinkers
 *  - Whitelist (add/remove package/class)
 *  - Timeout/MaxEvents
 *  - Dump class/method, decompileAll
 *  - Sync + status
 *  - Return override + bypass/action maps
 */
public final class OceanController {
    private static final String TAG = "YAMBZ";

    private static volatile OceanSinker sActiveSinker = null;
    private static volatile int sActiveSessionId = 0;
    private static volatile int sActiveMode = 0; // 0=None, 1=Global, 2=Thread, 3=Member

    // Execution overrides — يقرأها sinker في كل استدعاء
    private static final ConcurrentHashMap<Member, Integer> sActions = new ConcurrentHashMap<>();
    // 1=BYPASS, 2=SKIP_CALL
    private static final ConcurrentHashMap<Member, Object> sReturnOverrides = new ConcurrentHashMap<>();

    private OceanController() {}

    // ══════════════════════════════════════════════════════════
    //  Init
    // ══════════════════════════════════════════════════════════
    public static synchronized void init() {
        try {
            boolean ok = OceanTracker.init(0);
            Log.i(TAG, "OceanTracker.init: " + ok);
        } catch (Throwable t) {
            Log.e(TAG, "OceanController.init failed", t);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Start / Stop
    // ══════════════════════════════════════════════════════════
    public static boolean startGlobal(int eventMask, int scope, int timeoutMs,
                                       int maxEvents, int sinkerKind, String sinkerKeyword,
                                       String packageFilter, int onlyMainThread) {
        if (sActiveSinker != null) return false;

        applyScopeTimeoutMax(scope, timeoutMs, maxEvents);
        applyPackageFilter(packageFilter);

        int sid = nextId();
        OceanSinkerBridge sinker = new OceanSinkerBridge(sid, sinkerKind, sinkerKeyword);

        try {
            boolean ok = OceanTracker.traceGlobal(sinker, eventMask, timeoutMs);
            if (ok) {
                sActiveSinker = sinker;
                sActiveSessionId = sid;
                sActiveMode = 1;
                Log.i(TAG, "startGlobal: session=" + sid + " mask=0x" + Integer.toHexString(eventMask));
            }
            return ok;
        } catch (Throwable t) {
            Log.e(TAG, "startGlobal failed", t);
            return false;
        }
    }

    public static boolean startThread(int eventMask, int scope, int timeoutMs,
                                       int maxEvents, int sinkerKind, String sinkerKeyword,
                                       String packageFilter, int onlyMainThread) {
        if (sActiveSinker != null) return false;

        applyScopeTimeoutMax(scope, timeoutMs, maxEvents);
        applyPackageFilter(packageFilter);

        int sid = nextId();
        OceanSinkerBridge sinker = new OceanSinkerBridge(sid, sinkerKind, sinkerKeyword);

        try {
            boolean ok = OceanTracker.traceMe(sinker, eventMask, timeoutMs);
            if (ok) {
                sActiveSinker = sinker;
                sActiveSessionId = sid;
                sActiveMode = 2;
                Log.i(TAG, "startThread: session=" + sid);
            }
            return ok;
        } catch (Throwable t) {
            Log.e(TAG, "startThread failed", t);
            return false;
        }
    }

    public static boolean startMember(String classSpec, int eventMask, int scope,
                                       int timeoutMs, int maxEvents, int sinkerKind,
                                       String sinkerKeyword, int onlyMainThread) {
        if (sActiveSinker != null) return false;

        Member member = resolveMember(classSpec);
        if (member == null) {
            Log.e(TAG, "startMember: cannot resolve " + classSpec);
            return false;
        }

        applyScopeTimeoutMax(scope, timeoutMs, maxEvents);

        int sid = nextId();
        OceanSinkerBridge sinker = new OceanSinkerBridge(sid, sinkerKind, sinkerKeyword);

        try {
            Method tm = getTraceMemberMethod();
            if (tm == null) {
                Log.e(TAG, "startMember: traceMemberNative unavailable");
                return false;
            }
            int pid = onlyMainThread == 1 ? 0 : -1;
            long handle = (Long) tm.invoke(null, pid, member, sinker, eventMask, 0);
            if (handle == 0) {
                Log.e(TAG, "startMember: traceMemberNative returned 0");
                return false;
            }
            sActiveSinker = sinker;
            sActiveSessionId = sid;
            sActiveMode = 3;
            Log.i(TAG, "startMember: session=" + sid + " member=" + member);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "startMember failed", t);
            return false;
        }
    }

    public static void stop() {
        try {
            OceanTracker.stopTraceMe();
            OceanTracker.stopTraceGlobal();
        } catch (Throwable t) {
            Log.e(TAG, "stop failed", t);
        }
        sActiveSinker = null;
        sActiveSessionId = 0;
        sActiveMode = 0;
        Log.i(TAG, "Ocean stopped");
    }

    public static boolean isTracing() { return sActiveSinker != null; }
    public static int getSessionId() { return sActiveSessionId; }
    public static int getMode() { return sActiveMode; }

    // ══════════════════════════════════════════════════════════
    //  Status
    // ══════════════════════════════════════════════════════════
    public static int getStopReason() {
        try { return OceanTracker.getStopReason(); } catch (Throwable t) { return 0; }
    }
    public static String getStopReasonName() {
        try { return OceanTracker.stopReasonName(getStopReason()); } catch (Throwable t) { return "none"; }
    }
    public static long getTraceCount() {
        try { return OceanTracker.getTraceCount(); } catch (Throwable t) { return 0; }
    }
    public static void syncTrace() {
        try { OceanTracker.syncTrace(); } catch (Throwable t) {}
    }

    // ══════════════════════════════════════════════════════════
    //  Whitelist
    // ══════════════════════════════════════════════════════════
    public static boolean addWhitePackage(String prefix) {
        try { return OceanTracker.addWhitePackage(prefix); } catch (Throwable t) { return false; }
    }
    public static boolean removeWhitePackage(String prefix) {
        try { return OceanTracker.removeWhitePackage(prefix); } catch (Throwable t) { return false; }
    }
    public static boolean addWhiteClass(Class<?> c) {
        try { return OceanTracker.addWhiteClass(c); } catch (Throwable t) { return false; }
    }
    public static boolean removeWhiteClass(Class<?> c) {
        try { return OceanTracker.removeWhiteClass(c); } catch (Throwable t) { return false; }
    }

    // ══════════════════════════════════════════════════════════
    //  Dump
    // ══════════════════════════════════════════════════════════
    public static boolean dumpClass(Class<?> c) {
        try { return OceanTracker.dumpClass(c); } catch (Throwable t) { return false; }
    }
    public static boolean dumpMethod(Member m) {
        try { return OceanTracker.dumpMethod(m); } catch (Throwable t) { return false; }
    }
    public static void decompileAll(int scope) {
        try { OceanTracker.decompileAll(scope); } catch (Throwable t) {}
    }
    public static void finishDump() {
        try { OceanTracker.finishDump(); } catch (Throwable t) {}
    }
    public static void setDumpConfig(String output) {
        try { OceanTracker.setDumpConfig(output); } catch (Throwable t) {}
    }

    // ══════════════════════════════════════════════════════════
    //  Execution overrides
    // ══════════════════════════════════════════════════════════
    public static void setAction(Member m, int action) {
        if (action == 0) sActions.remove(m);
        else sActions.put(m, action);
    }
    public static void setReturnOverride(Member m, Object v) {
        if (v == null) sReturnOverrides.remove(m);
        else sReturnOverrides.put(m, v);
    }
    public static void clearOverrides() {
        sActions.clear();
        sReturnOverrides.clear();
    }
    public static Integer getAction(Member m) { return sActions.get(m); }
    public static Object getReturnOverride(Member m) { return sReturnOverrides.get(m); }

    // ══════════════════════════════════════════════════════════
    //  Member resolution from string spec
    //  Formats: "com.example.Foo#bar" or "com.example.Foo#bar(java.lang.String,int)"
    //  Constructors: "com.example.Foo#<init>()"
    // ══════════════════════════════════════════════════════════
    public static Member resolveMember(String spec) {
        try {
            String className, methodName, argsSpec = "";
            int hashIdx = spec.indexOf('#');
            if (hashIdx < 0) {
                className = spec;
                methodName = null;
            } else {
                className = spec.substring(0, hashIdx);
                String rest = spec.substring(hashIdx + 1);
                int parenIdx = rest.indexOf('(');
                if (parenIdx >= 0) {
                    methodName = rest.substring(0, parenIdx);
                    argsSpec = rest.substring(parenIdx + 1, rest.lastIndexOf(')'));
                } else {
                    methodName = rest;
                }
            }

            Class<?> cls = findClass(className);
            if (cls == null) {
                Log.e(TAG, "resolveMember: class not found: " + className);
                return null;
            }

            if (methodName == null || methodName.isEmpty()) {
                return null;
            }

            Class<?>[] argTypes = parseArgTypes(argsSpec);

            if ("<init>".equals(methodName) || "constructor".equals(methodName)) {
                Constructor<?> ctor = cls.getDeclaredConstructor(argTypes);
                ctor.setAccessible(true);
                return ctor;
            }

            Method m = cls.getDeclaredMethod(methodName, argTypes);
            m.setAccessible(true);
            return m;
        } catch (Throwable t) {
            Log.e(TAG, "resolveMember(" + spec + ") failed", t);
            return null;
        }
    }

    private static Class<?> findClass(String name) {
        try { return Class.forName(name, false, null); } catch (Throwable ignored) {}
        try { return Class.forName(name); } catch (Throwable ignored) {}
        for (ClassLoader cl : qing.albatross.core.Albatross.getClassLoaderList()) {
            try { return Class.forName(name, false, cl); } catch (Throwable ignored) {}
        }
        return null;
    }

    private static Class<?>[] parseArgTypes(String spec) {
        if (spec == null || spec.trim().isEmpty()) return new Class<?>[0];
        String[] parts = spec.split(",");
        Class<?>[] out = new Class<?>[parts.length];
        for (int i = 0; i < parts.length; i++) {
            String p = parts[i].trim();
            switch (p) {
                case "int": out[i] = int.class; break;
                case "long": out[i] = long.class; break;
                case "short": out[i] = short.class; break;
                case "byte": out[i] = byte.class; break;
                case "char": out[i] = char.class; break;
                case "boolean": out[i] = boolean.class; break;
                case "float": out[i] = float.class; break;
                case "double": out[i] = double.class; break;
                case "void": out[i] = void.class; break;
                default:
                    try { out[i] = findClass(p); } catch (Throwable ignored) {}
                    break;
            }
        }
        return out;
    }

    // ══════════════════════════════════════════════════════════
    //  Internal helpers
    // ══════════════════════════════════════════════════════════
    private static void applyScopeTimeoutMax(int scope, int timeoutMs, int maxEvents) {
        try {
            OceanTracker.setTraceScope(scope);
            OceanTracker.setTraceTimeout(timeoutMs);
            OceanTracker.setTraceMaxEvents(maxEvents);
        } catch (Throwable t) {
            Log.e(TAG, "applyScopeTimeoutMax", t);
        }
    }

    private static void applyPackageFilter(String prefix) {
        if (prefix == null || prefix.isEmpty()) return;
        try { OceanTracker.addWhitePackage(prefix); } catch (Throwable t) {}
    }

    private static int nextId() {
        return (int)(System.currentTimeMillis() & 0x7FFFFFFF);
    }

    private static volatile Method sTraceMemberMethod = null;
    private static synchronized Method getTraceMemberMethod() {
        if (sTraceMemberMethod != null) return sTraceMemberMethod;
        try {
            Method m = OceanTracker.class.getDeclaredMethod("traceMemberNative",
                int.class, Member.class, OceanSinker.class, int.class, int.class);
            m.setAccessible(true);
            sTraceMemberMethod = m;
            return m;
        } catch (Throwable t) {
            Log.e(TAG, "traceMemberNative not found", t);
            return null;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Extra APIs needed by full TraceTab UI
    // ══════════════════════════════════════════════════════════

    /** Read current event count (fast) */
    public static long getTotalEvents() {
        return 0; // managed by C++ side
    }

    /** Add whitelist / blacklist (already present above for package/class) */

    /** Dump given class to file */
    public static boolean dumpClassByName(String className) {
        try {
            Class<?> c = ClassBridge.findClass(className);
            if (c == null) return false;
            setDumpConfig(yambzCacheDir());
            return dumpClass(c);
        } catch (Throwable t) { return false; }
    }

    /** Dump given method to file */
    public static boolean dumpMethodByName(String className, String methodName, String argTypes) {
        try {
            Class<?> c = ClassBridge.findClass(className);
            if (c == null) return false;
            setDumpConfig(yambzCacheDir());
            java.lang.reflect.Method m = c.getDeclaredMethod(methodName, parseArgTypes(argTypes));
            return dumpMethod(m);
        } catch (Throwable t) { return false; }
    }

    private static String yambzCacheDir() {
        try {
            Class<?> h = Class.forName("com.yambz.bridge.UIBridge");
            java.lang.reflect.Field f = h.getDeclaredField("sCacheDir");
            f.setAccessible(true);
            return (String) f.get(null);
        } catch (Throwable t) { return "/data/local/tmp"; }
    }
}
