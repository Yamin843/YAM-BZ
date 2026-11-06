#pragma once
#include "TraceEvent.h"
#include <jni.h>
#include <string>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace yambz {

/**
 * TraceEngine: يدير hooks ديناميكية على methods.
 *
 * لكل method:
 *   - يُنشئ MethodCallHook عبر Albatross.hookMethod
 *   - يُسجّل TraceCapture (Java) كـ callback
 *   - TraceCapture ينادي C++ per-event
 *
 * Life-cycle:
 *   startTrace(Member) → تُثبّت hook → callback يدعو C++ → events إلى TraceStore
 *   stopTrace(sessionId) → unHook
 *
 * ملاحظات:
 *   - Constructors غير مدعومة (MethodCallHook ترفضها)
 *   - static методы مدعومة (this=null)
 *   - يمكن trace عدة methods في نفس الوقت
 */
class TraceEngine {
public:
    static TraceEngine& instance();

    /// بدء trace method (يُرجع sessionId أو 0)
    /// Start tracing a Constructor (uses ConstructorTraceCapture via hookInstruction)
    uint32_t startConstructorTrace(JNIEnv* env, jobject constructor,
                                   const TraceSession& cfg);

    /// Start tracing a Method (uses TraceCapture via Albatross.hookMethod)
    uint32_t startTrace(JNIEnv* env, jobject methodOrCtor,
                        const TraceSession& cfg);

    /// إيقاف trace
    bool stopTrace(JNIEnv* env, uint32_t sessionId);

    /// إيقاف الكل
    void stopAll(JNIEnv* env);

    /// حالة
    size_t activeCount() const;
    bool isActive(uint32_t sessionId) const;

    /// من JNI callback — يُستدعى من Java TraceCapture
    void onEventCaptured(uint32_t sessionId, JNIEnv* env,
                         jobject frame, bool isBefore);

    /// تحرير أي شيء عند الخروج
    void shutdown(JNIEnv* env);

private:
    TraceEngine() = default;

    struct Session {
        uint32_t     id;
        TraceSession cfg;
        jobject      hookHandle  = nullptr;  // GlobalRef → MethodCallHook
        jobject      callback    = nullptr;  // GlobalRef → TraceCapture
        jobject      method      = nullptr;  // GlobalRef → Member
        std::string  methodDesc;
    };

    mutable std::mutex m_mu;
    std::unordered_map<uint32_t, Session> m_sessions;
    std::atomic<uint32_t> m_nextId{1};

    // Java method IDs (cached)
    void cacheJavaMethods(JNIEnv* env);
    bool m_methodsCached = false;
    jclass    m_cls_Albatross     = nullptr;
    jmethodID m_mid_hookMethod    = nullptr;
    jmethodID m_mid_unHook        = nullptr;  // MethodCallHook.unHook
    jclass    m_cls_TraceCapture  = nullptr;
    jmethodID m_mid_captureCtor   = nullptr;
};

} // namespace yambz
