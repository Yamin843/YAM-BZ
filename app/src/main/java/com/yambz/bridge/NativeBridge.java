package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import qing.albatross.common.Const;
import qing.albatross.nativehook.Address;
import qing.albatross.nativehook.AlbNative;
import qing.albatross.nativehook.CModule;
import qing.albatross.nativehook.CModuleCompileException;
import qing.albatross.nativehook.DlInfo;
import qing.albatross.nativehook.HookRecord;
import qing.albatross.nativehook.InstructionCallback;
import qing.albatross.nativehook.Libc;
import qing.albatross.nativehook.NativeFunction;

/**
 * NativeBridge — كل عمليات Native عبر Albatross.
 *
 * يدعم:
 *  - listLibraries / listSymbols (enumeration)
 *  - CModule (compile, getFunction, call)
 *  - hookInstruction (hook a C function)
 *  - Delayed hooks
 *  - Libc wrappers
 *  - NativeFunction (call by address)
 *  - hookNative (annotation-based on hooker class)
 */
public final class NativeBridge {
    private static final String TAG = "YAMBZ";

    private NativeBridge() {}

    // ══════════════════════════════════════════════════════════
    //  Enumeration
    // ══════════════════════════════════════════════════════════
    public static String[] listLibraries() {
        final List<String> out = new ArrayList<>();
        try {
            AlbNative.enumerateModules((symbol, addr, size, idx) -> {
                if (out.size() < 10000) out.add(symbol + "|0x" + Long.toHexString(addr));
                return true;
            });
        } catch (Throwable t) {
            Log.e(TAG, "listLibraries failed", t);
        }
        return out.toArray(new String[0]);
    }

    public static String[] listSymbols(String lib) {
        final List<String> out = new ArrayList<>();
        DlInfo info = null;
        try {
            info = AlbNative.openLib(lib);
            if (info == null) return new String[0];
            info.enumerateFunctions((symbol, addr, size, idx) -> {
                if (out.size() < 10000) {
                    out.add(symbol + "|0x" + Long.toHexString(addr));
                }
                return true;
            });
        } catch (Throwable t) {
            Log.e(TAG, "listSymbols(" + lib + ") failed", t);
        } finally {
            if (info != null) try { info.close(); } catch (Throwable ignored) {}
        }
        return out.toArray(new String[0]);
    }

    public static long findSymbol(String lib, String symbol) {
        DlInfo info = null;
        try {
            info = AlbNative.openLib(lib);
            if (info == null) return 0;
            return info.getSymbolAddress(symbol);
        } catch (Throwable t) {
            return 0;
        } finally {
            if (info != null) try { info.close(); } catch (Throwable ignored) {}
        }
    }

    // ══════════════════════════════════════════════════════════
    //  CModule
    // ══════════════════════════════════════════════════════════
    private static volatile CModule sActiveModule = null;

    public static String compileC(String source) {
        if (source == null || source.trim().isEmpty()) return "empty source";
        try {
            if (sActiveModule != null) {
                try { sActiveModule.close(); } catch (Throwable ignored) {}
                sActiveModule = null;
            }
            sActiveModule = CModule.compile(source);
            Log.i(TAG, "CModule compiled OK");
            return "OK";
        } catch (CModuleCompileException e) {
            Log.e(TAG, "CModule compile failed", e);
            return "COMPILE_ERROR: " + e.getMessage();
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    public static boolean hasCFunction(String symbol) {
        if (sActiveModule == null) return false;
        try { return sActiveModule.hasSymbol(symbol); } catch (Throwable t) { return false; }
    }

    public static String callCFunction(String symbol, String signature, String argsJson) {
        if (sActiveModule == null) return "no active module";
        try {
            byte[] parts = parseSignature(signature);
            if (parts == null) return "bad signature";

            byte retType = parts[0];
            byte[] argTypes = new byte[parts.length - 1];
            System.arraycopy(parts, 1, argTypes, 0, parts.length - 1);

            Object[] args = parseArgs(argsJson, argTypes);
            if (args == null) return "bad args";
            if (args.length != argTypes.length) {
                return "arg count mismatch: sig=" + argTypes.length + " args=" + args.length;
            }

            NativeFunction fn = sActiveModule.getFunction(symbol, retType, argTypes);
            Object result = fn.call(args);
            return dumpResult(result);
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  hookInstruction (C function hook)
    // ══════════════════════════════════════════════════════════
    private static final Map<Long, HookRecord> sActiveHooks = new ConcurrentHashMap<>();
    private static final List<String> sHookInfo = new ArrayList<>();

    public static String addHookInstruction(String lib, String symbol) {
        try {
            InstructionCallback cb = (ctx, ud) -> {
                // Log to logcat — UI reads via C++ TraceStore integration in future
                Log.i(TAG, "hook hit: arg0=" + ctx.getNthArgument(0)
                    + " arg1=" + ctx.getNthArgument(1));
            };
            HookRecord rec = AlbNative.hookInstruction(lib, symbol, cb, null, null);
            if (rec == null) return "null record";
            long id = System.identityHashCode(rec);
            sActiveHooks.put(id, rec);
            sHookInfo.add(lib + "!" + symbol + "|0x" + Long.toHexString(id));
            Log.i(TAG, "Hook installed: " + lib + "!" + symbol);
            return "OK";
        } catch (Throwable t) {
            Log.e(TAG, "hookInstruction failed", t);
            return "ERROR: " + t;
        }
    }

    public static String[] listHooks() {
        return sHookInfo.toArray(new String[0]);
    }

    public static boolean unhookAll() {
        boolean any = false;
        for (Map.Entry<Long, HookRecord> e : sActiveHooks.entrySet()) {
            try { e.getValue().unHook(); any = true; } catch (Throwable ignored) {}
        }
        sActiveHooks.clear();
        sHookInfo.clear();
        return any;
    }

    // ══════════════════════════════════════════════════════════
    //  Libc wrappers
    // ══════════════════════════════════════════════════════════
    public static String libcOpen(String path, int flags) {
        try {
            int fd = Libc.open(path, flags, 0644);
            return "fd=" + fd;
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    public static String libcWrite(int fd, String data) {
        try {
            long n = Libc.write(fd, data, data.length());
            return "wrote " + n;
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    public static String libcRead(int fd, int maxLen) {
        try {
            Address buf = Address.malloc(maxLen, true);
            try {
                long n = Libc.read(fd, buf.getAddress(), maxLen);
                if (n <= 0) return "(empty)";
                return buf.readString((int) n);
            } finally {
                buf.delete();
            }
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    public static String libcClose(int fd) {
        try {
            int r = Libc.close(fd);
            return "close=" + r;
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  NativeFunction (call by address)
    // ══════════════════════════════════════════════════════════
    public static String callNativeAddr(long addr, String signature, String argsJson) {
        if (addr == 0) return "null address";
        try {
            byte[] parts = parseSignature(signature);
            if (parts == null) return "bad signature";

            byte retType = parts[0];
            byte[] argTypes = new byte[parts.length - 1];
            System.arraycopy(parts, 1, argTypes, 0, parts.length - 1);

            Object[] args = parseArgs(argsJson, argTypes);
            if (args == null) return "bad args";

            NativeFunction fn = new NativeFunction(addr, retType, argTypes);
            return dumpResult(fn.call(args));
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  hookNative (annotation-based)
    // ══════════════════════════════════════════════════════════
    public static String hookNativeClass(String className, String lib) {
        try {
            Class<?> cls = ClassBridge.findClass(className);
            if (cls == null) return "class not found: " + className;
            int n = (lib == null || lib.isEmpty())
                ? AlbNative.hookNative(cls)
                : AlbNative.hookNative(cls, lib);
            return "hooked " + n + " items";
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    public static String unhookNativeClass(String className, String lib) {
        try {
            Class<?> cls = ClassBridge.findClass(className);
            if (cls == null) return "class not found";
            int n = (lib == null || lib.isEmpty())
                ? AlbNative.unhookNative(cls)
                : AlbNative.unhookNative(cls, lib);
            return "unhooked " + n + " items";
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Signature parsing
    //  Format: "int(int,String,ptr)" → [ARG_INT, ARG_INT, ARG_STR, ARG_PTR]
    // ══════════════════════════════════════════════════════════
    private static byte[] parseSignature(String sig) {
        if (sig == null) return null;
        sig = sig.trim();
        int p1 = sig.indexOf('(');
        int p2 = sig.lastIndexOf(')');
        if (p1 < 0 || p2 < 0) return null;
        String retStr = sig.substring(0, p1).trim();
        String argsStr = sig.substring(p1 + 1, p2).trim();

        byte ret = typeCode(retStr);
        if (ret < 0) return null;

        if (argsStr.isEmpty()) return new byte[]{ret};

        String[] parts = argsStr.split(",");
        byte[] out = new byte[parts.length + 1];
        out[0] = ret;
        for (int i = 0; i < parts.length; i++) {
            byte c = typeCode(parts[i].trim());
            if (c < 0) return null;
            out[i + 1] = c;
        }
        return out;
    }

    private static byte typeCode(String s) {
        switch (s) {
            case "int":    return Const.ARG_INT;
            case "long":   return Const.ARG_LONG;
            case "bool":
            case "boolean":return Const.ARG_BOOL;
            case "String": return Const.ARG_STR;
            case "float":  return Const.ARG_FLOAT;
            case "double": return Const.ARG_DOUBLE;
            case "ptr":    return Const.ARG_PTR;
            case "bytes":  return Const.ARG_BYTES;
            case "void":   return Const.ARG_VOID;
        }
        return -1;
    }

    private static Object[] parseArgs(String json, byte[] argTypes) {
        try {
            if (argTypes.length == 0) return new Object[0];
            org.json.JSONArray a = new org.json.JSONArray(json);
            if (a.length() != argTypes.length) return null;
            Object[] out = new Object[argTypes.length];
            for (int i = 0; i < argTypes.length; i++) {
                Object v = a.get(i);
                if (v == org.json.JSONObject.NULL) { out[i] = null; continue; }
                switch (argTypes[i]) {
                    case Const.ARG_INT:    out[i] = ((Number) v).intValue(); break;
                    case Const.ARG_LONG:   out[i] = ((Number) v).longValue(); break;
                    case Const.ARG_BOOL:   out[i] = (Boolean) v; break;
                    case Const.ARG_STR:    out[i] = v.toString(); break;
                    case Const.ARG_FLOAT:  out[i] = ((Number) v).floatValue(); break;
                    case Const.ARG_DOUBLE: out[i] = ((Number) v).doubleValue(); break;
                    case Const.ARG_PTR:    out[i] = v instanceof Number ? ((Number) v).longValue() : 0L; break;
                    case Const.ARG_BYTES:  out[i] = v.toString().getBytes(); break;
                    default:               out[i] = v;
                }
            }
            return out;
        } catch (Throwable t) {
            return null;
        }
    }

    private static String dumpResult(Object r) {
        if (r == null) return "null";
        return String.valueOf(r);
    }
}
