#pragma once
#include <mutex>
#include <atomic>

namespace yambz {
class MainWindow {
public:
    static MainWindow& instance();
    void draw();
    void show();
    void hide();
    void toggle();
    bool visible() const { return m_visible; }
    bool animating() const;
    bool hitTest(float x, float y) const;
    void invalidateRect();

    struct Impl;
private:
    MainWindow();
    void drawMain();
    struct ImplState;
    ImplState* m_impl;
    bool m_visible = false;
};
}
