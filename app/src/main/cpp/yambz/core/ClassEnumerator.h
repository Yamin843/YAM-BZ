#pragma once
#include <memory>
#include <unordered_map>
#include <functional>
#include "ClassTypes.h"
#include <jni.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

namespace yambz {

/**
 * تعداد كلاسات التطبيق (ملايين ممكن) عبر Java bridge.
 *
 * آلية العمل (batch-pull بدون حدود):
 *   C++ يدعو JavaHelper.nextBatch(1000) → يرجع String[] كلاسات
 *   C++ يحوّل كل اسم إلى ClassInfo ويضيفه للـ cache
 *   يُكرَّر حتى batch فارغ
 *
 * - إن أُلغيت العملية (cancel) يتوقف فوراً.
 * - background thread منفصل — لا يعطّل UI.
 * - يخزّن نتائج بين الجلسات (لا إعادة تعداد).
 *
 * يتكامل مع Albatross.searchClass (الموجود مسبقاً) بدون تعديل المشروع الأساسي.
 */
class ClassEnumerator {
public:
    static ClassEnumerator& instance();

    /// بدء التعداد (idempotent — لا يعيد إذا كان جارياً)
    void start(JNIEnv* env);

    /// إلغاء التعداد الجاري (سريع)
    void cancel();

    /// الحالة
    bool isRunning()    const { return m_running.load(); }
    bool isComplete()   const { return m_complete.load(); }
    size_t count()      const { return m_classes.size(); }
    float  progress()   const;  // 0..1 (تقريبي)

    /// نسخة thread-safe من اللائحة (shared_ptr لتفادي النسخ)
    std::shared_ptr<const std::vector<ClassInfo>> snapshot() const;

    /// تفاصيل كلاس معيّن (lazy — يُجلب مرة واحدة)
    const std::vector<MemberInfo>& membersOf(size_t classIdx);

    /// مسح الكاش
    void clear();

private:
    ClassEnumerator() = default;
    ~ClassEnumerator() = default;

    void workerMain(JNIEnv* env);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_complete{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<uint32_t> m_loadedBatches{0};

    std::vector<ClassInfo> m_classes;
    // members cache: class index → vector<MemberInfo>
    std::unordered_map<size_t, std::vector<MemberInfo>> m_membersCache;

    mutable std::mutex m_mu;
    mutable std::mutex m_membersMu;
    JavaVM* m_vm = nullptr;
    jobject m_javaHelper = nullptr;  // GlobalRef
};

} // namespace yambz
