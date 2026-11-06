package com.yambz.call;

import android.util.Log;

import com.yambz.bridge.AlbatrossReflection;

import java.lang.reflect.Constructor;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

/**
 * يجد subclasses instantiable لـ abstract class أو interface.
 *
 * لماذا؟
 *   - abstract/interface لا يمكن إنشاؤها مباشرة
 *   - المستخدم يحتاج اختيار concrete impl
 *
 * يُستخدم:
 *   - عند اختيار abstract class → نعرض قائمة subclasses
 *   - يمكن تخزين اختياره في ParamNode (as subclass name)
 */
public final class AbstractHelper {
    private static final String TAG = "YAMBZ";

    private AbstractHelper() {}

    /**
     * @param maxResults 0 = unlimited (but Memory bounded)
     * @return list of instantiable subclass names
     */
    public static List<String> findInstantiableSubclasses(Class<?> parent, int maxResults) {
        List<String> out = new ArrayList<>();
        try {
            List<Class<?>> subs = AlbatrossReflection.searchSubclasses(parent, maxResults > 0 ? maxResults : 1000);
            for (Class<?> c : subs) {
                if (c.isInterface() || Modifier.isAbstract(c.getModifiers())) continue;
                // check for at least one accessible ctor
                Constructor<?>[] ctors = c.getDeclaredConstructors();
                if (ctors.length == 0) continue;
                out.add(c.getName());
                if (maxResults > 0 && out.size() >= maxResults) break;
            }
        } catch (Throwable t) {
            Log.e(TAG, "findInstantiableSubclasses", t);
        }
        return out;
    }

    /**
     * جاهز للاستخدام من ParameterBuilder: يحول اسم subclass إلى Class.
     */
    public static Class<?> resolve(String name, ClassLoader loader) throws ClassNotFoundException {
        return ParameterBuilder.loadClass(name, loader);
    }
}
