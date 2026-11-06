package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Member;
import java.lang.reflect.Method;

import qing.albatross.core.Albatross;

/**
 * CompileBridge — compile/decompile UI through Albatross.
 */
public final class CompileBridge {
    private static final String TAG = "YAMBZ";

    private CompileBridge() {}

    public static String compileClassByName(String className, int option) {
        try {
            Class<?> c = ClassBridge.findClass(className);
            if (c == null) return "class not found";
            int n = Albatross.compileClass(c, option);
            return "compiled " + n + " methods";
        } catch (Throwable t) {
            return "error: " + t;
        }
    }

    public static String compileMethodByName(String spec) {
        try {
            Member m = resolveMember(spec);
            if (m == null) return "cannot resolve";
            boolean ok = Albatross.compileMethod(m);
            return ok ? "OK" : "failed";
        } catch (Throwable t) {
            return "error: " + t;
        }
    }

    public static String decompileMethodByName(String spec) {
        try {
            Member m = resolveMember(spec);
            if (m == null) return "cannot resolve";
            int result = Albatross.decompileMethod(m, true);
            return result > 0 ? "OK (" + result + ")" : "failed";
        } catch (Throwable t) {
            return "error: " + t;
        }
    }

    public static String getMethodInfo(String spec) {
        try {
            Member m = resolveMember(spec);
            if (m == null) return "cannot resolve";
            StringBuilder sb = new StringBuilder();
            sb.append("isCompiled: ").append(Albatross.isCompiled((Method) m)).append("\n");
            sb.append("codeSize: ").append(Albatross.getMethodCodeSize(m)).append("\n");
            sb.append("hookCount: ").append(Albatross.getMethodHookCount(m)).append("\n");
            sb.append("entryPoint: 0x").append(Long.toHexString(
                Albatross.entryPointFromQuickCompiledCode(m))).append("\n");
            return sb.toString();
        } catch (Throwable t) {
            return "error: " + t;
        }
    }

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
            if ("<init>".equals(name)) return cls.getDeclaredConstructor(argTypes);
            return cls.getDeclaredMethod(name, argTypes);
        } catch (Throwable t) {
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
}
