#pragma once
#include <atomic>
#include <mutex>
#include <vector>

namespace yambz {
class Renderer {
public:
    static Renderer& instance();
    void onSurfaceCreated();
    void onSurfaceChanged(int w, int h);
    void onDrawFrame(int w, int h);
    void onTouch(int action, float x, float y, int pointerId);
    void onScroll(float dx, float dy);
    void onChar(unsigned int cp);
    void onKey(int keyCode, int action);
    bool wantsAnotherFrame() const;
    bool wantCaptureMouse() const;
    bool wantTextInput() const;
    bool isReady() const { return m_ready.load(); }
    int width() const { return m_w; }
    int height() const { return m_h; }
private:
    Renderer() = default;
    std::mutex m_mu;
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_init{false};
    std::atomic<bool> m_wantMouse{false};
    std::atomic<bool> m_wantText{false};
    int m_w = 0, m_h = 0;
    double m_lastTime = 0.0;
    bool m_backendInit = false;
    std::mutex m_charMu;
    std::vector<unsigned int> m_charQueue;
    std::mutex m_scrollMu;
    float m_scrollVX = 0.0f, m_scrollVY = 0.0f;
};
}
