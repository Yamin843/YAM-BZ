package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

import qing.albatross.core.Albatross;
import qing.albatross.core.CallFrame;
import qing.albatross.core.MethodCallHook;
import qing.albatross.core.MethodHookCallback;
import qing.albatross.exception.AlbatrossException;

/**
 * BackupBridge — backup/replace through Albatross.
 */
public final class BackupBridge {
    private static final String TAG = "YAMBZ";

    private BackupBridge() {}

    // ══════════════════════════════════════════════════════════
    //  Member resolution (inlined from OceanController)
    // ══════════════════════════════════════════════════════════
    private static Member resolveMember(String spec) {
        try {
            int hash = spec.indexOf('#');
            if (hash < 0) return null;
            String className = spec.substring(0, hash);
            String rest = spec.substring(hash + 1);
            int paren = rest.indexOf('(');
            String name, argsSpec;
            if (paren >= 0) {
                name = rest.substring(0, paren);
                argsSpec = rest.substring(paren + 1, rest.lastIndexOf(')'));
            } else {
                name = rest; argsSpec = "";
            }
            Class<?> cls = ClassBridge.findClass(className);
            if (cls == null) return null;
            Class<?>[] argTypes = parseArgs(argsSpec);
            if ("<init>".equals(name) || "constructor".equals(name)) {
                return cls.getDeclaredConstructor(argTypes);
            }
            return cls.getDeclaredMethod(name, argTypes);
        } catch (Throwable t) {
            Log.e(TAG, "resolveMember failed: " + spec, t);
            return null;
        }
    }

    private static Class<?>[] parseArgs(String spec) throws ClassNotFoundException {
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
                    Class<?> c = ClassBridge.findClass(p);
                    if (c == null) throw new ClassNotFoundException(p);
                    out[i] = c;
            }
        }
        return out;
    }

    // ══════════════════════════════════════════════════════════
    //  Dynamic hook with invokeOriginal backup
    // ══════════════════════════════════════════════════════════
    public static MethodCallHook hookWithBackup(String spec, final String label) {
        try {
            Member m = resolveMember(spec);
            if (m == null) {
                Log.e(TAG, "cannot resolve: " + spec);
                return null;
            }
            return Albatross.hookMethod(m, new MethodHookCallback() {
                @Override
                public boolean beforeCall(CallFrame frame) {
                    Log.i(TAG, "[" + label + "] beforeCall");
                    try {
                        Object r = frame.invokeOriginal();
                        frame.setResult(r);
                    } catch (Throwable t) {
                        Log.e(TAG, "invokeOriginal failed", t);
                    }
                    return false;
                }
            });
        } catch (Throwable t) {
            Log.e(TAG, "hookWithBackup failed", t);
            return null;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Field backup
    // ══════════════════════════════════════════════════════════
    public static boolean backupFieldSpec(String className, String fieldName) {
        try {
            Class<?> cls = ClassBridge.findClass(className);
            if (cls == null) return false;
            Field f = cls.getDeclaredField(fieldName);
            f.setAccessible(true);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "backupField failed", t);
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Hook registry
    // ══════════════════════════════════════════════════════════
    private static final List<HookInfo> sHooks = new ArrayList<>();

    public static class HookInfo {
        public final String spec;
        public final String label;
        public final long createdAt;
        public MethodCallHook handle;

        HookInfo(String spec, String label) {
            this.spec = spec;
            this.label = label;
            this.createdAt = System.currentTimeMillis();
        }
    }

    public static String addHook(String spec, String label) {
        MethodCallHook h = hookWithBackup(spec, label);
        if (h == null) return null;
        HookInfo info = new HookInfo(spec, label);
        info.handle = h;
        sHooks.add(info);
        return spec;
    }

    public static String[] listHooks() {
        String[] out = new String[sHooks.size()];
        for (int i = 0; i < sHooks.size(); i++) {
            HookInfo info = sHooks.get(i);
            out[i] = info.spec + "|" + info.label + "|" + info.createdAt;
        }
        return out;
    }

    public static boolean unhookAt(int idx) {
        if (idx < 0 || idx >= sHooks.size()) return false;
        try {
            HookInfo info = sHooks.remove(idx);
            if (info.handle != null) info.handle.unHook();
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "unhook failed", t);
            return false;
        }
    }

    public static void unhookAll() {
        for (HookInfo info : sHooks) {
            try { if (info.handle != null) info.handle.unHook(); } catch (Throwable ignored) {}
        }
        sHooks.clear();
    }
}
