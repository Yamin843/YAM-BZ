package com.yambz.bridge;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

import qing.albatross.reflection.ConstructorDef;
import qing.albatross.reflection.MethodDef;
import qing.albatross.reflection.StaticMethodDef;

/**
 * CallBridge — استدعاء member عبر Albatross reflection wrappers.
 *
 * Inputs:
 *  - spec:  "com.example.Foo#bar(int,java.lang.String)" or "#<init>()"
 *  - jsonArgs: JSON array "[1, \"hello\", null]"
 *
 * Output: JSON: {"success":true,"result":"...","error":null,"elapsed":12}
 *
 * يدعم:
 *  - static & instance methods
 *  - constructors
 *  - varargs (delegates to Albatross wrappers)
 *  - primitives + objects (via simple JSON parser)
 */
public final class CallBridge {
    private static final String TAG = "YAMBZ";

    private CallBridge() {}

    public static String invokeSpec(String spec, String jsonArgs) {
        long t0 = System.currentTimeMillis();
        try {
            Member m = resolveMember(spec);
            if (m == null) {
                return errorJson("cannot resolve: " + spec, 0);
            }

            Object[] args = parseArgs(jsonArgs, m);

            Object result = null;
            if (m instanceof Method) {
                Method mm = (Method) m;
                mm.setAccessible(true);
                boolean isStatic = Modifier.isStatic(mm.getModifiers());
                if (isStatic) {
                    StaticMethodDef<Object> def = new StaticMethodDef<>(mm);
                    result = def.call(args);
                } else {
                    // Instance method — نحتاج instance
                    Object self = null;
                    try {
                        self = instantiateDeclaring(mm.getDeclaringClass());
                    } catch (Throwable t) {
                        return errorJson("cannot create instance: " + t, 0);
                    }
                    MethodDef<Object> def = new MethodDef<>(mm);
                    result = def.invoke(self, args);
                }
            } else if (m instanceof Constructor) {
                Constructor<?> c = (Constructor<?>) m;
                c.setAccessible(true);
                ConstructorDef<Object> def = new ConstructorDef<>(c);
                result = def.newInstance(args);
            } else {
                return errorJson("not a method/constructor", 0);
            }

            long elapsed = System.currentTimeMillis() - t0;
            return successJson(result, elapsed);
        } catch (Throwable t) {
            Log.e(TAG, "invoke failed", t);
            return errorJson(t.toString(), System.currentTimeMillis() - t0);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Member resolution
    // ══════════════════════════════════════════════════════════
    private static Member resolveMember(String spec) throws Exception {
        int hash = spec.indexOf('#');
        if (hash < 0) throw new IllegalArgumentException("no # in spec");

        String className = spec.substring(0, hash);
        String rest = spec.substring(hash + 1);
        int paren = rest.indexOf('(');
        String methodName;
        String argsSpec;
        if (paren >= 0) {
            methodName = rest.substring(0, paren);
            argsSpec = rest.substring(paren + 1, rest.lastIndexOf(')'));
        } else {
            methodName = rest;
            argsSpec = "";
        }

        Class<?> cls = ClassBridge.findClass(className);
        if (cls == null) throw new ClassNotFoundException(className);

        Class<?>[] argTypes = parseArgTypes(argsSpec);

        if ("<init>".equals(methodName) || "constructor".equals(methodName)) {
            Constructor<?> c = cls.getDeclaredConstructor(argTypes);
            c.setAccessible(true);
            return c;
        }

        Method m = cls.getDeclaredMethod(methodName, argTypes);
        m.setAccessible(true);
        return m;
    }

    private static Class<?>[] parseArgTypes(String spec) throws ClassNotFoundException {
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
                    break;
            }
        }
        return out;
    }

    private static Object instantiateDeclaring(Class<?> cls) throws Exception {
        try {
            Constructor<?> c = cls.getDeclaredConstructor();
            c.setAccessible(true);
            return c.newInstance();
        } catch (NoSuchMethodException e) {
            // Try Unsafe
            try {
                Class<?> unsafeCls = Class.forName("sun.misc.Unsafe");
                java.lang.reflect.Field f = unsafeCls.getDeclaredField("theUnsafe");
                f.setAccessible(true);
                Object unsafe = f.get(null);
                Method alloc = unsafeCls.getMethod("allocateInstance", Class.class);
                return alloc.invoke(unsafe, cls);
            } catch (Throwable t) {
                throw new Exception("no default ctor and Unsafe failed", t);
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    //  JSON parser (minimal — array of primitives + strings)
    // ══════════════════════════════════════════════════════════
    private static Object[] parseArgs(String json, Member m) throws Exception {
        if (json == null || json.trim().isEmpty() || json.trim().equals("[]")) {
            return new Object[0];
        }
        org.json.JSONArray arr = new org.json.JSONArray(json);
        Class<?>[] paramTypes;
        if (m instanceof Method) paramTypes = ((Method) m).getParameterTypes();
        else paramTypes = ((Constructor<?>) m).getParameterTypes();

        if (arr.length() != paramTypes.length) {
            throw new IllegalArgumentException(
                "args count mismatch: expected " + paramTypes.length
                + ", got " + arr.length());
        }
        Object[] out = new Object[arr.length()];
        for (int i = 0; i < arr.length(); i++) {
            Object v = arr.get(i);
            out[i] = convert(v, paramTypes[i]);
        }
        return out;
    }

    private static Object convert(Object v, Class<?> type) {
        if (v == org.json.JSONObject.NULL) return null;
        if (type == int.class || type == Integer.class) return ((Number) v).intValue();
        if (type == long.class || type == Long.class) return ((Number) v).longValue();
        if (type == short.class || type == Short.class) return ((Number) v).shortValue();
        if (type == byte.class || type == Byte.class) return ((Number) v).byteValue();
        if (type == float.class || type == Float.class) return ((Number) v).floatValue();
        if (type == double.class || type == Double.class) return ((Number) v).doubleValue();
        if (type == boolean.class || type == Boolean.class) return v;
        if (type == char.class || type == Character.class) {
            String s = v.toString();
            return s.isEmpty() ? '\0' : s.charAt(0);
        }
        if (type == String.class) return v.toString();
        return v;
    }

    // ══════════════════════════════════════════════════════════
    //  JSON output
    // ══════════════════════════════════════════════════════════
    private static String successJson(Object result, long ms) {
        String resStr = dump(result);
        return "{\"success\":true,\"result\":" + jsonStr(resStr)
             + ",\"error\":null,\"elapsed\":" + ms + "}";
    }

    private static String errorJson(String msg, long ms) {
        return "{\"success\":false,\"result\":null,\"error\":"
             + jsonStr(msg) + ",\"elapsed\":" + ms + "}";
    }

    private static String dump(Object o) {
        if (o == null) return "null";
        try {
            // Try ObjectDumper
            Class<?> cls = Class.forName("com.yambz.bridge.ObjectDumper");
            Method mid = cls.getMethod("dump", Object.class);
            return (String) mid.invoke(null, o);
        } catch (Throwable ignored) {}
        return String.valueOf(o);
    }

    private static String jsonStr(String s) {
        if (s == null) return "null";
        StringBuilder sb = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default: sb.append(c);
            }
        }
        return sb.append("\"").toString();
    }
}
