package com.yambz.bridge;

import android.util.Log;

import java.util.ArrayList;
import java.util.List;

import qing.albatross.common.Const;
import qing.albatross.nativehook.AlbNative;
import qing.albatross.nativehook.CModule;
import qing.albatross.nativehook.CModuleCompileException;
import qing.albatross.nativehook.DlInfo;
import qing.albatross.nativehook.NativeFunction;

/**
 * NativeBridge — يستخدم AlbNative + CModule + NativeFunction من Albatross.
 */
public final class NativeBridge {
    private static final String TAG = "YAMBZ";

    private NativeBridge() {}

    // ══════════════════════════════════════════════════════════
    //  Libraries + symbols
    // ══════════════════════════════════════════════════════════
    public static String[] listLibraries() {
        final List<String> out = new ArrayList<>();
        try {
            AlbNative.enumerateModules((symbol, addr, size, idx) -> {
                if (out.size() < 10000) out.add(symbol);
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
                if (out.size() < 5000) {
                    out.add(symbol + "|" + Long.toHexString(addr));
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

    // ══════════════════════════════════════════════════════════
    //  CModule
    // ══════════════════════════════════════════════════════════
    private static volatile CModule sActiveModule = null;
    private static volatile String sLastCompileError = null;

    public static String compileC(String source) {
        if (source == null || source.trim().isEmpty()) return "empty source";
        try {
            if (sActiveModule != null) {
                try { sActiveModule.close(); } catch (Throwable ignored) {}
                sActiveModule = null;
            }
            sActiveModule = CModule.compile(source);
            sLastCompileError = null;
            Log.i(TAG, "CModule compiled OK");
            return "OK";
        } catch (CModuleCompileException e) {
            sLastCompileError = e.getMessage();
            Log.e(TAG, "CModule compile failed", e);
            return "COMPILE_ERROR: " + e.getMessage();
        } catch (Throwable t) {
            sLastCompileError = t.toString();
            return "ERROR: " + t;
        }
    }

    public static String callCFunction(String symbol, String signature, String argsJson) {
        if (sActiveModule == null) return "no active module";
        try {
            // Signature format: "int(int,int)" — parse
            // For simplicity, only support 0-arg int return if signature empty
            NativeFunction fn;
            Object[] args = parseSimpleArgs(argsJson);
            byte retType = Const.ARG_INT;
            byte[] argTypes = new byte[args.length];

            // Parse signature
            if (signature != null && signature.contains("(")) {
                int p = signature.indexOf('(');
                String retStr = signature.substring(0, p).trim();
                String argsStr = signature.substring(p + 1, signature.lastIndexOf(')')).trim();
                retType = typeCode(retStr);
                String[] parts = argsStr.isEmpty() ? new String[0] : argsStr.split(",");
                if (parts.length != args.length) {
                    return "arg count mismatch: sig=" + parts.length + " args=" + args.length;
                }
                for (int i = 0; i < parts.length; i++) {
                    argTypes[i] = typeCode(parts[i].trim());
                }
            } else {
                return "bad signature: " + signature;
            }

            fn = sActiveModule.getFunction(symbol, retType, argTypes);
            Object result = fn.call(args);
            return String.valueOf(result);
        } catch (Throwable t) {
            return "ERROR: " + t;
        }
    }

    private static byte typeCode(String s) {
        switch (s) {
            case "int": return Const.ARG_INT;
            case "long": return Const.ARG_LONG;
            case "bool":
            case "boolean": return Const.ARG_BOOL;
            case "String": return Const.ARG_STR;
            case "float": return Const.ARG_FLOAT;
            case "double": return Const.ARG_DOUBLE;
            case "ptr": return Const.ARG_PTR;
            case "void": return Const.ARG_VOID;
        }
        return Const.ARG_INT;
    }

    private static Object[] parseSimpleArgs(String json) {
        if (json == null || json.trim().isEmpty() || json.equals("[]")) return new Object[0];
        try {
            org.json.JSONArray a = new org.json.JSONArray(json);
            Object[] out = new Object[a.length()];
            for (int i = 0; i < a.length(); i++) out[i] = a.get(i);
            return out;
        } catch (Throwable t) {
            return new Object[0];
        }
    }
}
