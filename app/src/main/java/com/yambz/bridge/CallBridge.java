package com.yambz.bridge;

import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import qing.albatross.reflection.ConstructorDef;
import qing.albatross.reflection.MethodDef;
import qing.albatross.reflection.StaticMethodDef;

/**
 * CallBridge — استدعاء member مع بناء شجرة معاملات كاملة من JSON.
 *
 * JSON format (from ParamValue.toJson()):
 *   {"k":"int","v":42}
 *   {"k":"str","v":"hello"}
 *   {"k":"null"}
 *   {"k":"obj","cls":"com.Foo","fields":{"x":{"k":"int","v":1},...}}
 *   {"k":"array","elemType":"int","items":[...]}
 *   {"k":"list","elemType":"String","items":[...]}
 *   {"k":"map","keyType":"String","valueType":"int","entries":[[k,v],...]}
 *   {"k":"enum","cls":"java.lang.Thread$State","v":"RUNNABLE"}
 */
public final class CallBridge {
    private static final String TAG = "YAMBZ";

    private CallBridge() {}

    public static String invokeSpec(String spec, String jsonArgs) {
        long t0 = System.currentTimeMillis();
        try {
            Member m = resolveMember(spec);
            if (m == null) return errorJson("cannot resolve: " + spec, 0);

            JSONArray arr = new JSONArray(jsonArgs);
            Class<?>[] paramTypes = getParamTypes(m);
            if (arr.length() != paramTypes.length) {
                return errorJson("arg count mismatch: expected "
                    + paramTypes.length + ", got " + arr.length(), 0);
            }
            Object[] args = new Object[arr.length()];
            for (int i = 0; i < arr.length; i++) {
                args[i] = buildValue(arr.getJSONObject(i), paramTypes[i]);
            }

            Object result = invoke(m, args);
            long elapsed = System.currentTimeMillis() - t0;
            return successJson(result, elapsed);
        } catch (Throwable t) {
            Log.e(TAG, "invoke failed", t);
            return errorJson(t.toString(), System.currentTimeMillis() - t0);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Invoke via Albatross wrappers
    // ══════════════════════════════════════════════════════════
    private static Object invoke(Member m, Object[] args) throws Exception {
        if (m instanceof Method) {
            Method mm = (Method) m;
            mm.setAccessible(true);
            if (Modifier.isStatic(mm.getModifiers())) {
                StaticMethodDef<Object> def = new StaticMethodDef<>(mm);
                return def.call(args);
            }
            Object self = instantiate(mm.getDeclaringClass());
            MethodDef<Object> def = new MethodDef<>(mm);
            return def.invoke(self, args);
        }
        if (m instanceof Constructor) {
            Constructor<?> c = (Constructor<?>) m;
            c.setAccessible(true);
            ConstructorDef<Object> def = new ConstructorDef<>(c);
            return def.newInstance(args);
        }
        throw new IllegalArgumentException("not method or ctor");
    }

    private static Object instantiate(Class<?> cls) throws Exception {
        try {
            Constructor<?> c = cls.getDeclaredConstructor();
            c.setAccessible(true);
            return c.newInstance();
        } catch (NoSuchMethodException e) {
            Class<?> unsafe = Class.forName("sun.misc.Unsafe");
            Field f = unsafe.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            Object u = f.get(null);
            Method alloc = unsafe.getMethod("allocateInstance", Class.class);
            return alloc.invoke(u, cls);
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Build value from JSON node
    // ══════════════════════════════════════════════════════════
    private static Object buildValue(JSONObject node, Class<?> expectedType) throws Exception {
        String kind = node.optString("k", "null");
        switch (kind) {
            case "null": return null;
            case "int": return node.getInt("v");
            case "long": return node.getLong("v");
            case "short": return (short) node.getInt("v");
            case "byte": return (byte) node.getInt("v");
            case "char": return (char) node.getInt("v");
            case "bool": return node.getBoolean("v");
            case "float": return (float) node.getDouble("v");
            case "double": return node.getDouble("v");
            case "str": return node.getString("v");
            case "enum": {
                String cn = node.getString("cls");
                String en = node.getString("v");
                Class<?> cls = ClassBridge.findClass(cn);
                if (cls == null) throw new ClassNotFoundException(cn);
                @SuppressWarnings({"rawtypes","unchecked"})
                Object v = Enum.valueOf((Class<? extends Enum>) cls, en);
                return v;
            }
            case "obj": {
                String cn = node.getString("cls");
                Class<?> cls = ClassBridge.findClass(cn);
                if (cls == null) throw new ClassNotFoundException(cn);
                Object inst = instantiate(cls);
                JSONObject fields = node.optJSONObject("fields");
                if (fields != null) {
                    java.util.Iterator<String> it = fields.keys();
                    while (it.hasNext()) {
                        String fname = it.next();
                        Field f = findField(cls, fname);
                        if (f == null) continue;
                        f.setAccessible(true);
                        Object fv = buildValue(fields.getJSONObject(fname), f.getType());
                        try { f.set(inst, fv); } catch (Throwable ignored) {}
                    }
                }
                return inst;
            }
            case "array": {
                String elemType = node.optString("elemType", "java.lang.Object");
                Class<?> compCls = ClassBridge.findClass(elemType);
                if (compCls == null) compCls = Object.class;
                JSONArray items = node.optJSONArray("items");
                int n = items == null ? 0 : items.length();
                Object arr = Array.newInstance(compCls, n);
                for (int i = 0; i < n; i++) {
                    Array.set(arr, i, buildValue(items.getJSONObject(i), compCls));
                }
                return arr;
            }
            case "list": {
                String elemType = node.optString("elemType", "java.lang.Object");
                Class<?> compCls = ClassBridge.findClass(elemType);
                if (compCls == null) compCls = Object.class;
                JSONArray items = node.optJSONArray("items");
                List<Object> list = new ArrayList<>();
                if (items != null) {
                    for (int i = 0; i < items.length(); i++) {
                        list.add(buildValue(items.getJSONObject(i), compCls));
                    }
                }
                return list;
            }
            case "map": {
                String keyType = node.optString("keyType", "java.lang.Object");
                String valType = node.optString("valueType", "java.lang.Object");
                Class<?> kc = ClassBridge.findClass(keyType); if (kc == null) kc = Object.class;
                Class<?> vc = ClassBridge.findClass(valType); if (vc == null) vc = Object.class;
                Map<Object, Object> m = new LinkedHashMap<>();
                JSONArray entries = node.optJSONArray("entries");
                if (entries != null) {
                    for (int i = 0; i < entries.length(); i++) {
                        JSONArray pair = entries.getJSONArray(i);
                        m.put(buildValue(pair.getJSONObject(0), kc),
                              buildValue(pair.getJSONObject(1), vc));
                    }
                }
                return m;
            }
        }
        throw new IllegalArgumentException("unknown kind: " + kind);
    }

    private static Field findField(Class<?> cls, String name) {
        for (Class<?> c = cls; c != null && c != Object.class; c = c.getSuperclass()) {
            try { return c.getDeclaredField(name); } catch (Throwable ignored) {}
        }
        return null;
    }

    private static Class<?>[] getParamTypes(Member m) {
        if (m instanceof Method) return ((Method) m).getParameterTypes();
        return ((Constructor<?>) m).getParameterTypes();
    }

    // ══════════════════════════════════════════════════════════
    //  Member resolution
    // ══════════════════════════════════════════════════════════
    private static Member resolveMember(String spec) throws Exception {
        int hash = spec.indexOf('#');
        if (hash < 0) throw new IllegalArgumentException("no #");
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
        if (cls == null) throw new ClassNotFoundException(className);
        Class<?>[] argTypes = parseArgTypes(argsSpec);
        if ("<init>".equals(name) || "constructor".equals(name)) {
            Constructor<?> c = cls.getDeclaredConstructor(argTypes);
            c.setAccessible(true);
            return c;
        }
        Method m = cls.getDeclaredMethod(name, argTypes);
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
            }
        }
        return out;
    }

    // ══════════════════════════════════════════════════════════
    //  Output JSON
    // ══════════════════════════════════════════════════════════
    private static String successJson(Object result, long ms) {
        return "{\"success\":true,\"result\":" + jsonStr(dump(result))
             + ",\"error\":null,\"elapsed\":" + ms + "}";
    }
    private static String errorJson(String msg, long ms) {
        return "{\"success\":false,\"result\":null,\"error\":"
             + jsonStr(msg) + ",\"elapsed\":" + ms + "}";
    }
    private static String dump(Object o) {
        if (o == null) return "null";
        try { return ObjectDumper.dump(o); } catch (Throwable t) { return String.valueOf(o); }
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
