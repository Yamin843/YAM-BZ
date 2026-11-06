#pragma once
#include <jni.h>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

namespace yambz {

struct HeapEntry {
    uint32_t idx = 0;
    uint64_t addr = 0;
    std::string display;
    jobject ref = nullptr;  // GlobalRef
};

class HeapPicker {
public:
    static HeapPicker& instance();

    /// بدء البحث (async) — typeName = fully qualified
    void startSearch(const std::string& typeName);

    /// إلغاء
    void cancel();

    /// حالة
    bool isSearching() const { return m_searching.load(); }
    int  totalSeen() const { return m_seen.load(); }

    /// نتائج مقروءة من UI (thread-safe snapshot)
    std::vector<HeapEntry> snapshot(size_t offset, size_t limit) const;
    size_t totalResults() const;

    /// استدعاء من Java callback
    void pushBatch(JNIEnv* env, jobjectArray entries);
    void onComplete(int total);
    void onError(JNIEnv* env, jobject throwable);

    /// نظّف النتائج
    void clear(JNIEnv* env);

    /// الـ UI يقود: هل نعرض "استخدام هذه instance"؟
    /// @return GlobalRef أو nullptr
    jobject pick(int idx);

    struct State;
    State& state() { return *m_state; }

private:
    HeapPicker();
    ~HeapPicker();

    std::atomic<bool> m_searching{false};
    std::atomic<int>  m_seen{0};
    std::unique_ptr<State> m_state;
};

} // namespace yambz
