#pragma once
#include <jni.h>
#include <mutex>

namespace yambz {

/**
 * يدير PanelBridge (GLSurfaceView full-screen).
 *
 * الإضافة تحدث مرة واحدة عند onActivityReady.
 * الفتح/الإغلاق عبر MainWindow::show/hide.
 *
 * عندما MainWindow غير مرئي:
 *   - PanelBridge لا يرسم شيئاً (transparent)
 *   - nativeHitTest يرجع false → touches تمر إلى التطبيق
 */
class PanelManager {
public:
    static PanelManager& instance();

    void attach(JNIEnv* env, jobject activity);
    void detach();

    void show();
    void hide();
    bool isVisible() const { return m_visible; }
    bool isAttached() const { return m_panel != nullptr; }

private:
    PanelManager() = default;
    ~PanelManager();

    jobject m_panel      = nullptr;  // GlobalRef
    jobject m_decorView  = nullptr;  // GlobalRef
    bool    m_visible    = false;
    mutable std::mutex m_mu;
};

} // namespace yambz
