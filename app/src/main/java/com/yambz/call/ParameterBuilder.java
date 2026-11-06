package com.yambz.call;

import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * يبني كائنات Java من JSON spec.
 *
 * spec kinds:
 *   {"k":"null"}
 *   {"k":"int","v":42}  / long / short / byte / bool / char / float / double
 *   {"k":"str","v":"hello"}
 *   {"k":"cls","v":"java.lang.String"}               ← class token
 *   {"k":"enum","cls":"java.lang.Thread$State","v":"RUNNABLE"}
 *   {"k":"array","cls":"java.lang.String[]","items":[spec,...]}
 *   {"k":"list","cls":"java.util.ArrayList","items":[spec,...]}
 *   {"k":"set","cls":"java.util.HashSet","items":[spec,...]}
 *   {"k":"map","cls":"java.util.LinkedHashMap","entries":[[k_spec,v_spec],...]}
 *   {"k":"obj",
 *     "cls":"com.example.Foo",
 *     "how":"unsafe" | "ctor" | "factory",
 *     "ctor":[spec,...],        ← إذا how=ctor
 *     "factory":"name",         ← إذا how=factory (static, 0 args)
 *     "fields":{"f":spec,...}   ← تُضبط بعد الإنشاء
 *   }
 */
public final class ParameterBuilder {
    private static final String TAG = "YAMBZ";

    private ParameterBuilder() {}

    public static Object build(String json, ClassLoader loader) throws Exception {
        JSONObject spec = new JSONObject(json);
        return buildFrom(spec, loader, 0);
    }

    private static Object buildFrom(JSONObject spec, ClassLoader loader, int depth) throws Exception {
        if (depth > 64) throw new IllegalArgumentException("nesting too deep (64)");
        String k = spec.optString("k", "");

        switch (k) {
            case "null":   return null;
            case "int":    return spec.getInt("v");
            case "long":   return spec.getLong("v");
            case "short":  return (short) spec.getInt("v");
            case "byte":   return (byte) spec.getInt("v");
            case "char":   return (char) spec.getInt("v");
            case "bool":   return spec.getBoolean("v");
            case "float":  return (float) spec.getDouble("v");
            case "double": return spec.getDouble("v");
            case "str":    return spec.getString("v");
            case "cls": {
                String cn = spec.getString("v");
                return loadClass(cn, loader);
            }
            case "enum": {
                String cn = spec.getString("cls");
                String en = spec.getString("v");
                Class<?> c = loadClass(cn, loader);
                @SuppressWarnings({"rawtypes","unchecked"})
                Object v = Enum.valueOf((Class<? extends Enum>) c, en);
                return v;
            }
            case "array": {
                String cn = spec.getString("cls");
                Class<?> cls = loadClass(cn, loader);
                JSONArray items = spec.optJSONArray("items");
                int n = items == null ? 0 : items.length();
                Object arr = Array.newInstance(cls.getComponentType(), n);
                for (int i = 0; i < n; i++) {
                    Object v = buildFrom(items.getJSONObject(i), loader, depth + 1);
                    Array.set(arr, i, v);
                }
                return arr;
            }
            case "list": {
                String cn = spec.optString("cls", "java.util.ArrayList");
                Class<?> cls = loadClass(cn, loader);
                @SuppressWarnings("unchecked")
                java.util.Collection<Object> c = (java.util.Collection<Object>) newInstance(cls, loader);
                JSONArray items = spec.optJSONArray("items");
                int n = items == null ? 0 : items.length();
                for (int i = 0; i < n; i++) {
                    c.add(buildFrom(items.getJSONObject(i), loader, depth + 1));
                }
                return c;
            }
            case "set": {
                String cn = spec.optString("cls", "java.util.LinkedHashSet");
                Class<?> cls = loadClass(cn, loader);
                @SuppressWarnings("unchecked")
                java.util.Collection<Object> c = (java.util.Collection<Object>) newInstance(cls, loader);
                JSONArray items = spec.optJSONArray("items");
                int n = items == null ? 0 : items.length();
                for (int i = 0; i < n; i++) {
                    c.add(buildFrom(items.getJSONObject(i), loader, depth + 1));
                }
                return c;
            }
            case "map": {
                String cn = spec.optString("cls", "java.util.LinkedHashMap");
                Class<?> cls = loadClass(cn, loader);
                @SuppressWarnings("unchecked")
                Map<Object,Object> m = (Map<Object,Object>) newInstance(cls, loader);
                JSONArray entries = spec.optJSONArray("entries");
                if (entries != null) {
                    for (int i = 0; i < entries.length(); i++) {
                        JSONArray pair = entries.getJSONArray(i);
                        Object key = buildFrom(pair.getJSONObject(0), loader, depth + 1);
                        Object val = buildFrom(pair.getJSONObject(1), loader, depth + 1);
                        m.put(key, val);
                    }
                }
                return m;
            }
            case "proxy": {
                String ifaceName = spec.getString("cls");
                Class<?> iface = loadClass(ifaceName, loader);
                String strategy = spec.optString("strategy", "null");
                // parse const values (very simple: string->string/number)
                java.util.Map<String, Object> constants = new java.util.HashMap<>();
                org.json.JSONObject cjs = spec.optJSONObject("constants");
                if (cjs != null) {
                    java.util.Iterator<String> it = cjs.keys();
                    while (it.hasNext()) {
                        String fldName = it.next();
                        constants.put(k, cjs.get(k));
                    }
                }
                return InterfaceProxyHandler.createProxy(iface, strategy, constants);
            }
            case "subclass": {
                // {k:"subclass", parent:"X", cls:"Y", args:[...]}
                String childName = spec.getString("cls");
                Class<?> child = loadClass(childName, loader);
                org.json.JSONArray argsJson = spec.optJSONArray("ctor");
                int argn = argsJson == null ? 0 : argsJson.length();
                Object[] args = new Object[argn];
                Class<?>[] argTypes = new Class<?>[argn];
                for (int i = 0; i < argn; i++) {
                    args[i] = buildFrom(argsJson.getJSONObject(i), loader, depth + 1);
                    argTypes[i] = args[i] == null ? Object.class : args[i].getClass();
                }
                Constructor<?> ctor = findBestConstructor(child, argTypes);
                if (ctor == null) throw new NoSuchMethodException("no ctor " + childName);
                ctor.setAccessible(true);
                return ctor.newInstance(args);
            }
            case "obj": {
                String cn = spec.getString("cls");
                Class<?> cls = loadClass(cn, loader);
                String how = spec.optString("how", "unsafe");
                Object instance = null;

                // Auto-handle interfaces
                if (cls.isInterface()) {
                    String strategy = spec.optString("strategy", "null");
                    return InterfaceProxyHandler.createProxy(cls, strategy, null);
                }

                if ("ctor".equals(how)) {
                    JSONArray argsJson = spec.optJSONArray("ctor");
                    int argn = argsJson == null ? 0 : argsJson.length();
                    Object[] args = new Object[argn];
                    Class<?>[] argTypes = new Class<?>[argn];
                    for (int i = 0; i < argn; i++) {
                        args[i] = buildFrom(argsJson.getJSONObject(i), loader, depth + 1);
                        argTypes[i] = args[i] == null ? Object.class : args[i].getClass();
                    }
                    Constructor<?> ctor = findBestConstructor(cls, argTypes);
                    if (ctor == null) throw new NoSuchMethodException("no ctor " + cn);
                    ctor.setAccessible(true);
                    instance = ctor.newInstance(args);
                } else if ("factory".equals(how)) {
                    String factory = spec.getString("factory");
                    Method fm = findZeroArgStatic(cls, factory);
                    if (fm == null) throw new NoSuchMethodException("factory " + cn + "." + factory);
                    fm.setAccessible(true);
                    instance = fm.invoke(null);
                } else { // unsafe
                    instance = UnsafeHelper.allocateInstance(cls);
                }

                // apply fields (correctly scoped loop variable)
                JSONObject fields = spec.optJSONObject("fields");
                if (fields != null) {
                    java.util.Iterator<String> fldIt = fields.keys();
                    while (fldIt.hasNext()) {
                        String fieldName = fldIt.next();
                        JSONObject fspec = fields.getJSONObject(fieldName);
                        Object fval = buildFrom(fspec, loader, depth + 1);
                        setField(instance, cls, fieldName, fval);
                    }
                }
                }
                return instance;
            }
            default:
                throw new IllegalArgumentException("unknown kind: " + k);
        }
    }

    // ─── helpers ───
    static Class<?> loadClass(String name, ClassLoader loader) throws ClassNotFoundException {
        // primitive names
        switch (name) {
            case "int":     return int.class;
            case "long":    return long.class;
            case "short":   return short.class;
            case "byte":    return byte.class;
            case "char":    return char.class;
            case "boolean": return boolean.class;
            case "float":   return float.class;
            case "double":  return double.class;
            case "void":    return void.class;
        }
        // array syntax: [I, [Ljava.lang.String;, etc.
        if (name.startsWith("[")) {
            return Class.forName(name, false, loader);
        }
        // "Type[]" syntax
        if (name.endsWith("[]")) {
            Class<?> c = loadClass(name.substring(0, name.length()-2), loader);
            return Array.newInstance(c, 0).getClass();
        }
        try { return Class.forName(name, false, loader); } catch (Throwable ignored) {}
        try { return Class.forName(name, false, null); } catch (Throwable ignored) {}
        // try from app loader
        for (ClassLoader l : qing.albatross.core.Albatross.getClassLoaderList()) {
            try { return Class.forName(name, false, l); } catch (Throwable ignored) {}
        }
        throw new ClassNotFoundException(name);
    }

    static Object newInstance(Class<?> cls, ClassLoader loader) throws Exception {
        try {
            Constructor<?> c = cls.getDeclaredConstructor();
            c.setAccessible(true);
            return c.newInstance();
        } catch (NoSuchMethodException e) {
            return UnsafeHelper.allocateInstance(cls);
        }
    }

    static Constructor<?> findBestConstructor(Class<?> cls, Class<?>[] argTypes) {
        Constructor<?>[] ctors = cls.getDeclaredConstructors();
        for (Constructor<?> c : ctors) {
            if (c.getParameterCount() != argTypes.length) continue;
            Class<?>[] p = c.getParameterTypes();
            boolean ok = true;
            for (int i = 0; i < p.length; i++) {
                if (argTypes[i] == Object.class) continue; // null
                if (!isWideningOk(p[i], argTypes[i])) { ok = false; break; }
            }
            if (ok) return c;
        }
        return null;
    }

    static boolean isWideningOk(Class<?> param, Class<?> arg) {
        if (param == arg) return true;
        if (param.isPrimitive() || arg.isPrimitive()) {
            // boxed
            Class<?> pBox = box(param), aBox = box(arg);
            if (pBox == aBox) return true;
        }
        return param.isAssignableFrom(arg);
    }

    static Class<?> box(Class<?> c) {
        if (!c.isPrimitive()) return c;
        if (c == int.class)     return Integer.class;
        if (c == long.class)    return Long.class;
        if (c == short.class)   return Short.class;
        if (c == byte.class)    return Byte.class;
        if (c == char.class)    return Character.class;
        if (c == boolean.class) return Boolean.class;
        if (c == float.class)   return Float.class;
        if (c == double.class)  return Double.class;
        return c;
    }

    static Method findZeroArgStatic(Class<?> cls, String name) {
        for (Method m : cls.getDeclaredMethods()) {
            if (m.getName().equals(name) && Modifier.isStatic(m.getModifiers())
                && m.getParameterCount() == 0) return m;
        }
        return null;
    }

    static void setField(Object instance, Class<?> cls, String fname, Object value) throws Exception {
        Field f = null;
        for (Class<?> c = cls; c != null && c != Object.class; c = c.getSuperclass()) {
            try { f = c.getDeclaredField(fname); break; } catch (Throwable ignored) {}
        }
        if (f == null) throw new NoSuchFieldException(fname);
        boolean isStatic = Modifier.isStatic(f.getModifiers());
        try {
            f.setAccessible(true);
            f.set(isStatic ? null : instance, value);
        } catch (Throwable t) {
            // hidden API fallback
            UnsafeHelper.putField(instance, f, value);
        }
    }
}
