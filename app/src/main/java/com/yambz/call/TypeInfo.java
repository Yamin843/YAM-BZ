package com.yambz.call;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

/**
 * يوفّر معلومات عن أي نوع Java:
 *   - هل هو واجهة / abstract / enum / hidden؟
 *   - constructors متاحة
 *   - static factories
 *   - fields
 *   - subclasses (عبر AlbatrossReflection.searchSubclasses)
 */
public final class TypeInfo {

    private TypeInfo() {}

    public static final class ConstructorInfo {
        public String[] paramTypes;
        public String   signature;
        public boolean  isPrivate;
        public Constructor<?> real;
    }

    public static final class FieldInfo {
        public String  name;
        public String  type;
        public boolean isStatic;
        public boolean isFinal;
        public Field   real;
    }

    public static final class FactoryInfo {
        public String name;
        public String returnType;
        public Method real;
    }

    public static final class Info {
        public String className;
        public boolean isInterface;
        public boolean isAbstract;
        public boolean isEnum;
        public boolean isHidden;    // لم يتمكن من تحميله عادة
        public boolean canUnsafe;   // Unsafe available
        public List<ConstructorInfo> constructors = new ArrayList<>();
        public List<FieldInfo>       fields = new ArrayList<>();
        public List<FactoryInfo>     factories = new ArrayList<>();
        public List<String>          enumValues = new ArrayList<>();
        public List<String>          subclasses = new ArrayList<>();
    }

    public static Info inspect(Class<?> cls) {
        Info info = new Info();
        info.className = cls.getName();
        info.isInterface = cls.isInterface();
        info.isAbstract  = Modifier.isAbstract(cls.getModifiers()) && !info.isInterface;
        info.isEnum      = cls.isEnum();
        info.canUnsafe   = UnsafeHelper.available();

        if (info.isEnum) {
            try {
                Object[] vs = cls.getEnumConstants();
                if (vs != null) for (Object v : vs) info.enumValues.add(((Enum<?>) v).name());
            } catch (Throwable ignored) {}
        }

        // constructors
        try {
            for (Constructor<?> c : cls.getDeclaredConstructors()) {
                ConstructorInfo ci = new ConstructorInfo();
                Class<?>[] p = c.getParameterTypes();
                ci.paramTypes = new String[p.length];
                StringBuilder sig = new StringBuilder("(");
                for (int i = 0; i < p.length; i++) {
                    ci.paramTypes[i] = p[i].getName();
                    sig.append(typeName(p[i])).append(',');
                }
                if (sig.length() > 1) sig.setLength(sig.length() - 1);
                sig.append(')');
                ci.signature = sig.toString();
                ci.isPrivate = Modifier.isPrivate(c.getModifiers());
                ci.real = c;
                info.constructors.add(ci);
            }
        } catch (Throwable ignored) {}

        // fields
        try {
            for (Class<?> c = cls; c != null && c != Object.class; c = c.getSuperclass()) {
                for (Field f : c.getDeclaredFields()) {
                    if (f.isSynthetic()) continue;
                    FieldInfo fi = new FieldInfo();
                    fi.name = f.getName();
                    fi.type = typeName(f.getType());
                    fi.isStatic = Modifier.isStatic(f.getModifiers());
                    fi.isFinal = Modifier.isFinal(f.getModifiers());
                    fi.real = f;
                    info.fields.add(fi);
                }
            }
        } catch (Throwable ignored) {}

        // static factories (0 args for now)
        try {
            for (Method m : cls.getDeclaredMethods()) {
                if (!Modifier.isStatic(m.getModifiers())) continue;
                if (!Modifier.isPublic(m.getModifiers())) continue;
                if (m.getParameterCount() != 0) continue;
                if (!cls.isAssignableFrom(m.getReturnType())) continue;
                FactoryInfo fi = new FactoryInfo();
                fi.name = m.getName();
                fi.returnType = typeName(m.getReturnType());
                fi.real = m;
                info.factories.add(fi);
            }
        } catch (Throwable ignored) {}

        return info;
    }

    public static List<String> subclassesOf(Class<?> cls, int max) {
        try {
            List<Class<?>> subs = com.yambz.bridge.AlbatrossReflection.searchSubclasses(cls, max);
            List<String> out = new ArrayList<>(subs.size());
            for (Class<?> c : subs) out.add(c.getName());
            return out;
        } catch (Throwable t) {
            return new ArrayList<>();
        }
    }

    private static String typeName(Class<?> c) {
        if (c == null) return "null";
        if (c.isArray()) return typeName(c.getComponentType()) + "[]";
        return c.getSimpleName();
    }
}
