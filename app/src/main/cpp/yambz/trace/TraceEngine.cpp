#include "TraceEngine.h"
#include "TraceStore.h"
#include "../core/Logger.h"

namespace yambz {

TraceEngine& TraceEngine::instance() {
    static TraceEngine e;
    return e;
}

void TraceEngine::cacheJavaMethods(JNIEnv* env) {
    if (m_methodsCached) return;

    // Albatross.hookMethod(Member, MethodHookCallback) → MethodCallHook
    jclass cls_A = env->FindClass("qing/albatross/core/Albatross");
    if (!cls_A) {
        LOGE("TraceEngine: Albatross class not found");
        return;
    }
    m_mid_hookMethod = env->GetStaticMethodID(cls_A, "hookMethod",
        "(Ljava/lang/reflect/Member;Lqing/albatross/core/MethodHookCallback;)Lqing/albatross/core/MethodCallHook;");
    if (!m_mid_hookMethod) {
        env->ExceptionClear();
        LOGE("TraceEngine: hookMethod not found");
        return;
    }
    m_cls_Albatross = (jclass) env->NewGlobalRef(cls_A);

    // MethodCallHook.unHook()V
    jclass cls_H = env->FindClass("qing/albatross/core/MethodCallHook");
    if (cls_H) {
        m_mid_unHook = env->GetMethodID(cls_H, "unHook", "()V");
    }

    // TraceCapture
    jclass cls_TC = env->FindClass("com/yambz/bridge/TraceCapture");
    if (cls_TC) {
        m_mid_captureCtor = env->GetMethodID(cls_TC, "<init>", "(I)V");
        m_cls_TraceCapture = (jclass) env->NewGlobalRef(cls_TC);
    }

    m_methodsCached = (m_mid_hookMethod != nullptr && m_mid_captureCtor != nullptr);
    LOGI("TraceEngine: cached methods=%d", m_methodsCached);
}

uint32_t TraceEngine::startTrace(JNIEnv* env, jobject method,
                                  const TraceSession& cfg) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_methodsCached) cacheJavaMethods(env);
    if (!m_methodsCached) {
        LOGE("TraceEngine: java methods not ready");
        return 0;
    }

    uint32_t id = m_nextId.fetch_add(1);

    // 1) TraceCapture(int sessionId)
    jobject local_cb = env->NewObject(m_cls_TraceCapture, m_mid_captureCtor,
                                       (jint) id);
    if (!local_cb) {
        LOGE("TraceEngine: TraceCapture ctor failed");
        env->ExceptionDescribe(); env->ExceptionClear();
        return 0;
    }
    jobject global_cb = env->NewGlobalRef(local_cb);

    // 2) Albatross.hookMethod(method, callback)
    jobject local_hook = env->CallStaticObjectMethod(m_cls_Albatross,
        m_mid_hookMethod, method, local_cb);
    if (env->ExceptionCheck()) {
        LOGE("TraceEngine: hookMethod threw");
        env->ExceptionDescribe(); env->ExceptionClear();
        env->DeleteGlobalRef(global_cb);
        return 0;
    }
    if (!local_hook) {
        LOGE("TraceEngine: hookMethod returned null");
        env->DeleteGlobalRef(global_cb);
        return 0;
    }
    jobject global_hook = env->NewGlobalRef(local_hook);

    // 3) احفظ
    Session s;
    s.id = id;
    s.cfg = cfg;
    s.callback = global_cb;
    s.hookHandle = global_hook;
    s.method = env->NewGlobalRef(method);
    s.methodDesc = cfg.methodDesc;
    m_sessions[id] = std::move(s);

    env->DeleteLocalRef(local_cb);
    env->DeleteLocalRef(local_hook);

    LOGI("TraceEngine: started session %u for %s", id, cfg.methodDesc.c_str());
    return id;
}

bool TraceEngine::stopTrace(JNIEnv* env, uint32_t sessionId) {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return false;

    Session& s = it->second;
    if (s.hookHandle && m_mid_unHook) {
        env->CallVoidMethod(s.hookHandle, m_mid_unHook);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe(); env->ExceptionClear();
        }
    }

    if (s.hookHandle) env->DeleteGlobalRef(s.hookHandle);
    if (s.callback)   env->DeleteGlobalRef(s.callback);
    if (s.method)     env->DeleteGlobalRef(s.method);

    m_sessions.erase(it);
    LOGI("TraceEngine: stopped session %u", sessionId);
    return true;
}

void TraceEngine::stopAll(JNIEnv* env) {
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        for (auto& p : m_sessions) ids.push_back(p.first);
    }
    for (uint32_t id : ids) stopTrace(env, id);
}

size_t TraceEngine::activeCount() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_sessions.size();
}

bool TraceEngine::isActive(uint32_t sessionId) const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_sessions.count(sessionId) > 0;
}

void TraceEngine::onEventCaptured(uint32_t sessionId, JNIEnv* env,
                                   jobject frame, bool isBefore) {
    // هذه الدالة تُستدعى من TraceCapture (Java) لكل حدث
    // مؤقتاً: نضع علامة أن التسجيل يعمل. المعالجة الكاملة في jni_trace.cpp
    extern void jni_trace_onEvent(uint32_t sessionId, JNIEnv* env,
                                   jobject frame, bool isBefore);
    jni_trace_onEvent(sessionId, env, frame, isBefore);
}

void TraceEngine::shutdown(JNIEnv* env) {
    stopAll(env);
    if (m_cls_Albatross) { env->DeleteGlobalRef(m_cls_Albatross); m_cls_Albatross = nullptr; }
    if (m_cls_TraceCapture) { env->DeleteGlobalRef(m_cls_TraceCapture); m_cls_TraceCapture = nullptr; }
    m_methodsCached = false;
}

} // namespace yambz
