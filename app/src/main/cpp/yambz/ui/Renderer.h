#pragma once
#include <atomic>
#include <mutex>
#include <vector>

struct ImFont;

namespace yambz {

/**
 * ImGui backend. يتصل بـ PanelBridge (GLSurfaceView) عبر JNI callbacks.
 *
 * دورة الحياة:
 *   onSurfaceCreated  → create ImGui context + init OpenGL3
 *   onSurfaceChanged  → set display size
 *   onDrawFrame       → NewFrame → MainWindow::draw → Render
 *   onTouch/onScroll  → forward to ImGui IO
 *
 * render mode:
 *   WHEN_DIRTY. `wantsAnotherFrame()` يرجع true عند وجود نشاط
 *   (إشعار نشط، نافذة منبثقة، سحب). Java يقرأها بعد onDrawFrame ويقرّر
 *   طلب إطار آخر عبر requestRender().
 */
class Renderer {
public:
    static Renderer& instance();

    void onSurfaceCreated();
    void onSurfaceChanged(int w, int h);
    void onDrawFrame(int w, int h);

    void onTouch(int action, float x, float y, int pointerId);
    void onScroll(float dx, float dy);
    void onChar(unsigned int codepoint);
    void onKey(int keyCode, int action);  // 0=down 1=up

    bool wantsAnotherFrame() const;
    bool wantsCaptureMouse() const;
    bool wantsTextInput() const;
    bool isReady() const { return m_ready.load(); }
    int width() const { return m_w; }
    int height() const { return m_h; }

    void shutdown();

private:
    Renderer() = default;
    ~Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drainInputQueues();

    std::mutex m_mu;
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_init{false};
    std::atomic<bool> m_wantMouse{false};
    std::atomic<bool> m_wantText{false};
    int  m_w = 0;
    int  m_h = 0;
    double m_lastTime = 0.0;
    bool   m_backendInit = false;

    // deferred char queue (thread-safe)
    std::mutex m_charMu;
    std::vector<unsigned int> m_charQueue;

    // scroll inertia
    std::mutex m_scrollMu;
    float m_scrollVelX = 0.0f;
    float m_scrollVelY = 0.0f;
};

} // namespace yambz
