#pragma once
#include "ParamNode.h"
#include <jni.h>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>

namespace yambz {

struct CallSpec {
    jobject                 methodMember = nullptr;  // LocalRef (caller keeps alive)
    std::string             methodDesc;
    std::shared_ptr<ParamNode> thisNode;   // nullptr if static
    std::vector<std::shared_ptr<ParamNode>> args;
};

struct CallResult {
    bool     success = false;
    std::string valueDump;      // via ObjectDumper
    std::string errorDump;
    long     elapsedMs = 0;
    bool     cancelled = false;
};

/**
 * يدير دورة الاستدعاء:
 *   - build method + this + args من شجرة C++ → Java objects (عبر ParameterBuilder)
 *   - invoke عبر MethodInvoker (Java, على thread منفصل)
 *   - يجمع النتيجة ويعرضها
 *
 * Threading:
 *   invoke() يعمل على خيط الخلفية — لا يُعطّل UI.
 */
class CallEngine {
public:
    static CallEngine& instance();

    /// بدء استدعاء async
    bool invokeAsync(JNIEnv* env, const CallSpec& spec);

    /// إلغاء جارٍ
    void cancel();

    /// الحالة
    bool isRunning() const { return m_running.load(); }

    /// آخر نتيجة
    CallResult lastResult() const;

    /// dump للـ result بعد اكتمال (يمسح الـ error state)
    void clearResult();

private:
    CallEngine() = default;
    mutable std::mutex m_mu;
    std::atomic<bool> m_running{false};
    CallResult m_last;
};

} // namespace yambz
