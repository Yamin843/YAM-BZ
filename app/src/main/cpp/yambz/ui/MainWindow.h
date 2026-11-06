#pragma once
#include <imgui.h>
#include <mutex>
#include <atomic>
#include <string>

namespace yambz {

/**
 * النافذة الرئيسية لـ YAM-BZ.
 *
 * التصميم:
 *   ┌──────────────────────────────────┐
 *   │ ≡ YAM-BZ               [—] [×] │  ← title (draggable)
 *   ├────┬─────────────────────────────┤
 *   │CLS │                              │
 *   │TRC │     content area             │
 *   │CALL│                              │
 *   │LOG │                              │
 *   │SET │                              │
 *   ├────┴─────────────────────────────┤
 *   │ status bar: ready | 0 events     │
 *   └──────────────────────────────────┘
 *
 * - Drag: title bar فقط (يترك المحتوى للسكرول)
 * - Resize: 5 zones (4 edges + corner) via InvisibleButton
 * - Minimize/Close: يعود للـ FAB
 */
class MainWindow {
public:
    static MainWindow& instance();

    void draw();
    void show();
    void hide();
    void toggle();
    bool visible() const { return m_visible; }
    bool animating() const;

    // hit-test من UI thread
    bool hitTest(float x, float y) const;
    bool isOnTitleBar(float y) const;
    void invalidateRect();

    // مساحة العنوان
    void setTitle(const std::string& title);

    /// إغلاق النافذة (يُطلب من زر X أو ضغط ESC)
    void requestClose() { m_closeRequested = true; }
    bool consumeCloseRequest();

    /// إبلاغ عن نشاط (لميض التنبيه في زر التنبيه)
    void setStatusText(const std::string& text);

private:
    MainWindow();
    ~MainWindow() = default;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void drawMainWindow();
    void drawSidebar();
    void drawContent();
    void drawStatusBar();
    void drawResizeZones();

    struct Impl;
    Impl* m_impl;
    bool  m_visible = false;
    bool  m_closeRequested = false;
};

} // namespace yambz
