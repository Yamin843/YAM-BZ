#pragma once
#include <jni.h>
#include <mutex>

namespace yambz {

/**
 * يدير زر FAB العائم (Android View) من C++.
 *
 * الـ FAB هو FabBridge.java (ImageView). يُضاف إلى DecorView عند
 * onActivityReady، ويُبقى هناك. إظهار/إخفاء عبر setVisibility.
 *
 * المميزات:
 *   - 56dp × 56dp فقط → لا يسرق لمس
 *   - قابل للسحب (يُدار من Java)
 *   - النقر (tap) → notify C++ → PanelManager::show()
 */
class FabManager {
public:
    static FabManager& instance();

    /// يُستدعى من JNI: nativeOnActivityReady
    void attach(JNIEnv* env, jobject activity);

    /// إزالة الـ FAB (عند تدمير الـ Activity)
    void detach();

    /// إظهار/إخفاء (thread-safe)
    void setVisible(bool visible);

    /// الحالة
    bool isAttached() const { return m_fab != nullptr; }
    bool isVisible()  const { return m_visible; }

    /// من JNI: عند tap
    void onFabTapped();

private:
    FabManager() = default;
    ~FabManager();

    jobject m_fab        = nullptr;  // GlobalRef
    jobject m_decorView  = nullptr;  // GlobalRef
    jobject m_activity   = nullptr;  // GlobalRef
    bool    m_visible    = false;
    mutable std::mutex m_mu;
};

} // namespace yambz
