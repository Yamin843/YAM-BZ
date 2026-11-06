package com.yambz.call;

import android.util.Log;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.HashMap;
import java.util.Map;

/**
 * InvocationHandler ديناميكي لإنشاء proxies للواجهات.
 *
 * الاستراتيجيات (يمكن ضبطها من C++ JSON):
 *   - "null"     → كل method يرجع null/0/false
 *   - "throw"    → يرمي UnsupportedOperationException
 *   - "const"    → يرجع قيمة ثابتة محددة (per-method)
 *   - "record"   → يسجّل كل استدعاء ويعرضه في Trace
 *
 * Thread-safe: كل proxy على thread قد يستدعي في أي وقت.
 */
public final class InterfaceProxyHandler implements InvocationHandler {
    private static final String TAG = "YAMBZ";

    private final String strategy;
    private final Map<String, Object> constants;
    private final Map<String, Integer> callCounts;

    public InterfaceProxyHandler(String strategy, Map<String, Object> constants) {
        this.strategy = strategy == null ? "null" : strategy;
        this.constants = constants == null ? new HashMap<>() : constants;
        this.callCounts = new HashMap<>();
    }

    public static Object createProxy(Class<?> iface, String strategy,
                                     Map<String, Object> constants) throws Exception {
        if (!iface.isInterface()) {
            throw new IllegalArgumentException(iface + " is not an interface");
        }
        return Proxy.newProxyInstance(
            iface.getClassLoader(),
            new Class<?>[]{iface},
            new InterfaceProxyHandler(strategy, constants));
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        String name = method.getName();

        // Handle Object methods
        if ("toString".equals(name) && method.getParameterCount() == 0) {
            return "Proxy<" + strategy + ">@" + Integer.toHexString(System.identityHashCode(proxy));
        }
        if ("hashCode".equals(name) && method.getParameterCount() == 0) {
            return System.identityHashCode(proxy);
        }
        if ("equals".equals(name) && method.getParameterCount() == 1) {
            return proxy == args[0];
        }

        // Track
        synchronized (callCounts) {
            callCounts.merge(name, 1, Integer::sum);
        }

        switch (strategy) {
            case "throw":
                throw new UnsupportedOperationException(
                    "proxy throws for " + name);
            case "const": {
                if (constants.containsKey(name)) return constants.get(name);
                break; // fallback to default
            }
            case "record": {
                Log.i(TAG, "proxy." + name + " called, args=" +
                    (args == null ? "[]" : java.util.Arrays.toString(args)));
                break; // fallback to default
            }
            case "null":
            default:
                break;
        }

        // return default by return type
        Class<?> rt = method.getReturnType();
        if (rt == void.class) return null;
        if (rt == boolean.class) return false;
        if (rt == byte.class)    return (byte) 0;
        if (rt == char.class)    return (char) 0;
        if (rt == short.class)   return (short) 0;
        if (rt == int.class)     return 0;
        if (rt == long.class)    return 0L;
        if (rt == float.class)   return 0.0f;
        if (rt == double.class)  return 0.0d;
        return null;
    }

    public Map<String, Integer> getCallCounts() {
        synchronized (callCounts) { return new HashMap<>(callCounts); }
    }
}
