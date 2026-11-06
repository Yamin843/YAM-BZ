package com.yambz.call;

import android.util.Log;

import com.yambz.bridge.AlbatrossReflection;

import java.lang.reflect.Constructor;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

/**
 * يحل مشكلة abstract classes وinterfaces:
 *  - abstract → يبحث عن subclass قابل للإنشاء
 *  - interface → يبحث عن impl قابل للإنشاء، أو proxy كملاذ أخير
 */
public final class AbstractHelper {
    private static final String TAG = "YAMBZ";

    private AbstractHelper() {}

    public static List<String> findInstantiableSubclasses(Class<?> parent, int maxResults) {
        List<String> out = new ArrayList<>();
        try {
            List<Class<?>> subs = AlbatrossReflection.searchSubclasses(parent, maxResults > 0 ? maxResults : 1000);
            for (Class<?> c : subs) {
                if (c.isInterface() || Modifier.isAbstract(c.getModifiers())) continue;
                Constructor<?>[] ctors = c.getDeclaredConstructors();
                if (ctors.length == 0) continue;
                out.add(c.getName());
                if (maxResults > 0 && out.size() >= maxResults) break;
            }
        } catch (Throwable t) {
            Log.e(TAG, "findInstantiableSubclasses failed", t);
        }
        return out;
    }

    /**
     * يُنشئ instance فعلي لـ abstract/interface:
     *  1. interface → proxy ديناميكي
     *  2. abstract → search subclasses + Unsafe.allocateInstance للأول
     *  3. fallback → Unsafe على cls نفسه (قد يفشل)
     */
    public static Object instantiate(Class<?> cls, ClassLoader loader,
                                      String proxyStrategy) throws Exception {
        if (cls.isInterface()) {
            // Interface: نجرّب ابحث عن impl حقيقي أولاً
            List<String> candidates = findInstantiableSubclasses(cls, 5);
            for (String candidate : candidates) {
                try {
                    Class<?> implCls = ParameterBuilder.loadClass(candidate, loader);
                    return UnsafeHelper.allocateInstance(implCls);
                } catch (Throwable t) {
                    Log.w(TAG, "cannot instantiate " + candidate + ": " + t);
                }
            }
            // Fallback: proxy
            return InterfaceProxyHandler.createProxy(cls,
                proxyStrategy != null ? proxyStrategy : "null", null);
        }

        if (Modifier.isAbstract(cls.getModifiers())) {
            List<String> candidates = findInstantiableSubclasses(cls, 5);
            for (String candidate : candidates) {
                try {
                    Class<?> implCls = ParameterBuilder.loadClass(candidate, loader);
                    return UnsafeHelper.allocateInstance(implCls);
                } catch (Throwable t) {
                    Log.w(TAG, "cannot instantiate abstract subclass " + candidate + ": " + t);
                }
            }
            throw new IllegalStateException(
                "no concrete subclass of " + cls.getName() + " found");
        }

        // Concrete: Unsafe
        return UnsafeHelper.allocateInstance(cls);
    }
}
