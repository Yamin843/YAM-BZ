#include "CallState.h"
#include "../core/Logger.h"

namespace yambz {

CallUIState& callUI() {
    static CallUIState s;
    return s;
}

void openCallForMethod(JNIEnv* env, jobject method) {
    auto& st = callUI();
    if (st.target.method) env->DeleteGlobalRef(st.target.method);
    st.target.method = env->NewGlobalRef(method);

    jclass cls = env->GetObjectClass(method);
    jmethodID m_name = env->GetMethodID(cls, "getName", "()Ljava/lang/String;");
    jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
    jmethodID m_getDecl = env->GetMethodID(cls, "getDeclaringClass",
        "()Ljava/lang/Class;");
    jmethodID m_getParamTypes = env->GetMethodID(cls, "getParameterTypes",
        "()[Ljava/lang/Class;");
    jmethodID m_getRet = env->GetMethodID(cls, "getReturnType", "()Ljava/lang/Class;");
    jmethodID m_getMod = env->GetMethodID(cls, "getModifiers", "()I");

    auto toString = [&](jobject o) -> std::string {
        if (!o) return "";
        jclass c = env->GetObjectClass(o);
        jmethodID m = env->GetMethodID(c, "getName", "()Ljava/lang/String;");
        if (!m) m = env->GetMethodID(c, "toString", "()Ljava/lang/String;");
        if (!m) return "";
        jstring js = (jstring) env->CallObjectMethod(o, m);
        if (!js) return "";
        const char* cc = env->GetStringUTFChars(js, nullptr);
        std::string r = cc ? cc : "";
        env->ReleaseStringUTFChars(js, cc);
        env->DeleteLocalRef(js);
        return r;
    };

    if (m_toStr) {
        jstring js = (jstring) env->CallObjectMethod(method, m_toStr);
        if (js) {
            const char* cc = env->GetStringUTFChars(js, nullptr);
            st.target.methodDesc = cc ? cc : "";
            env->ReleaseStringUTFChars(js, cc);
            env->DeleteLocalRef(js);
        }
    }
    if (m_getDecl) {
        jobject d = env->CallObjectMethod(method, m_getDecl);
        st.target.declaringClass = toString(d);
        if (d) env->DeleteLocalRef(d);
    }
    if (m_getRet) {
        jobject r = env->CallObjectMethod(method, m_getRet);
        st.target.returnType = toString(r);
        if (r) env->DeleteLocalRef(r);
    }
    if (m_getParamTypes) {
        jobjectArray pts = (jobjectArray) env->CallObjectMethod(method, m_getParamTypes);
        if (pts) {
            jsize n = env->GetArrayLength(pts);
            st.target.argTypes.clear();
            st.args.clear();
            for (jsize i = 0; i < n; i++) {
                jobject pt = env->GetObjectArrayElement(pts, i);
                std::string tn = toString(pt);
                st.target.argTypes.push_back(tn);
                st.args.push_back(ParamNode::forType(tn));
                if (pt) env->DeleteLocalRef(pt);
            }
            env->DeleteLocalRef(pts);
        }
    }
    if (m_getMod) {
        jint mod = env->CallIntMethod(method, m_getMod);
        st.target.isStatic = (mod & 0x8) != 0;
    }

    if (!st.target.isStatic) {
        st.thisNode = ParamNode::makeObjectUnsafe(st.target.declaringClass);
    } else {
        st.thisNode = nullptr;
    }

    st.show = true;
    st.showingResult = false;
    st.lastResultJson.clear();

    env->DeleteLocalRef(cls);
    LOGI("Call: opened for %s", st.target.methodDesc.c_str());
}

} // namespace yambz
