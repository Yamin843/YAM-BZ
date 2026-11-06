#pragma once

#include <jni.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

#include "ClassTypes.h"

namespace yambz {

class ClassEnumerator {
public:
    static ClassEnumerator& instance();

    void start(JNIEnv* env);
    void cancel();

    bool isRunning()  const { return m_running.load(); }
    bool isComplete() const { return m_complete.load(); }
    size_t count()    const { return m_classes.size(); }
    float  progress() const;

    std::shared_ptr<const std::vector<ClassInfo>> snapshot() const;
    const std::vector<MemberInfo>& membersOf(size_t classIdx);
    void clear();

    // Accessors for static thread worker (must be public)
    JavaVM* vmPtr() const { return m_vm; }
    void    runWorkerMain(JNIEnv* env) { workerMain(env); }

private:
    ClassEnumerator() = default;
    ~ClassEnumerator() = default;

    void workerMain(JNIEnv* env);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_complete{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<uint32_t> m_loadedBatches{0};

    std::vector<ClassInfo> m_classes;
    std::unordered_map<size_t, std::vector<MemberInfo>> m_membersCache;

    mutable std::mutex m_mu;
    mutable std::mutex m_membersMu;
    JavaVM*  m_vm = nullptr;
    jobject  m_javaHelper = nullptr;
};

} // namespace yambz
