#include "MainWindow.h"
#include "ClassesTab.h"
#include "../call/CallTab.h"
#include "../call/NativeTab.h"
#include "LogsTab.h"
#include "SettingsTab.h"
#include "../core/Favorites.h"
#include "Theme.h"
#include "Renderer.h"
#include "../core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <mutex>
#include <atomic>
#include <algorithm>

namespace yambz {

// ═══════════════════════════════════════════════════════════
//  layout constants
// ═══════════════════════════════════════════════════════════
static constexpr float kInitialW   = 0.85f;
static constexpr float kInitialH   = 0.80f;
static constexpr float kMinW       = 520.0f;
static constexpr float kMinH       = 400.0f;
static constexpr float kSidebarW   = 130.0f;
static constexpr float kResizeZone = 42.0f;
static constexpr float kCorner     = 100.0f;
static constexpr float kDragThresh2 = 64.0f;

static std::atomic<int> s_cachedTitleBarH{90};

// ═══════════════════════════════════════════════════════════
//  internal state
// ═══════════════════════════════════════════════════════════
struct MainWindow::Impl {
    int     currentTab    = 0;
    bool    fullscreen    = false;
    bool    firstDraw     = true;
    char    title[64]     = "YAM-BZ";
    char    status[256]   = "ready";

    ImVec2  lastPos{0, 0};
    ImVec2  lastSize{0, 0};
    ImVec2  initialSize{0, 0};
    bool    initializedSize = false;
    bool    resetWindow   = false;

    // drag
    bool   dragPending    = false;
    bool   windowDragging = false;
    ImVec2 dragStartMouse{0, 0};
    ImVec2 dragStartWindow{0, 0};

    // hit-test rect (mutex)
    mutable std::mutex rectMu;
    ImVec2 rectPos{0, 0};
    ImVec2 rectSize{0, 0};
    bool   rectValid = false;

    // fade-in animation
    float  alpha        = 0.0f;
    float  targetAlpha  = 0.0f;

    // تعطيل الحركة عند التفعيل الأول
    bool   everDrawn    = false;
};

// ═══════════════════════════════════════════════════════════
//  lifetime
// ═══════════════════════════════════════════════════════════
MainWindow::MainWindow() : m_impl(new Impl()) {
    std::strncpy(m_impl->title, "YAM-BZ", sizeof(m_impl->title)-1);
}
MainWindow& MainWindow::instance() { static MainWindow w; return w; }

void MainWindow::show() {
    m_visible = true;
    m_impl->targetAlpha = 1.0f;
}

void MainWindow::hide() {
    m_visible = false;
    m_impl->targetAlpha = 0.0f;
}

void MainWindow::toggle() { m_visible ? hide() : show(); }

bool MainWindow::animating() const {
    return std::fabs(m_impl->alpha - m_impl->targetAlpha) > 0.01f;
}

void MainWindow::setTitle(const std::string& title) {
    std::strncpy(m_impl->title, title.c_str(), sizeof(m_impl->title)-1);
}

void MainWindow::setStatusText(const std::string& text) {
    std::strncpy(m_impl->status, text.c_str(), sizeof(m_impl->status)-1);
}

bool MainWindow::consumeCloseRequest() {
    if (m_closeRequested) { m_closeRequested = false; return true; }
    return false;
}

// ═══════════════════════════════════════════════════════════
//  hit test (thread-safe)
// ═══════════════════════════════════════════════════════════
bool MainWindow::hitTest(float x, float y) const {
    std::lock_guard<std::mutex> lk(m_impl->rectMu);
    if (!m_impl->rectValid) return false;
    if (m_impl->rectSize.x <= 0 || m_impl->rectSize.y <= 0) return false;
    return x >= m_impl->rectPos.x && x <= m_impl->rectPos.x + m_impl->rectSize.x &&
           y >= m_impl->rectPos.y && y <= m_impl->rectPos.y + m_impl->rectSize.y;
}

bool MainWindow::isOnTitleBar(float y) const {
    float rx, ry, rw, rh;
    {
        std::lock_guard<std::mutex> lk(m_impl->rectMu);
        if (!m_impl->rectValid) return false;
        rx = m_impl->rectPos.x; ry = m_impl->rectPos.y;
        rw = m_impl->rectSize.x; rh = m_impl->rectSize.y;
    }
    int tbh = s_cachedTitleBarH.load(std::memory_order_relaxed);
    return y >= ry && y <= ry + (float)tbh;
}

void MainWindow::invalidateRect() {
    std::lock_guard<std::mutex> lk(m_impl->rectMu);
    m_impl->rectValid = false;
}

// ═══════════════════════════════════════════════════════════
//  draw
// ═══════════════════════════════════════════════════════════
void MainWindow::draw() {
    // fade animation
    float step = ImGui::GetIO().DeltaTime * 10.0f;
    if (m_impl->alpha < m_impl->targetAlpha)
        m_impl->alpha = std::min(m_impl->alpha + step, m_impl->targetAlpha);
    else if (m_impl->alpha > m_impl->targetAlpha)
        m_impl->alpha = std::max(m_impl->alpha - step, m_impl->targetAlpha);

    if (m_impl->alpha < 0.01f && !m_visible) {
        // غير مرئي — أبلغ الـ JNI بعدم تفعيل hit test
        invalidateRect();
        // التقط طلبات الإغلاق حتى لو مخفي
        if (consumeCloseRequest()) { /* already hidden */ }
        return;
    }

    drawMainWindow();
}

void MainWindow::drawMainWindow() {
    ImGuiIO& io = ImGui::GetIO();

    // cache title bar height
    {
        int tbh = (int)(ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f) + 16;
        s_cachedTitleBarH.store(tbh, std::memory_order_relaxed);
    }

    if (!m_impl->initializedSize) {
        m_impl->initialSize = ImVec2(io.DisplaySize.x * kInitialW,
                                     io.DisplaySize.y * kInitialH);
        m_impl->initializedSize = true;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoTitleBar;

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kMinW, kMinH), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    if (m_impl->firstDraw) {
        ImGui::SetNextWindowPos(
            ImVec2((io.DisplaySize.x - m_impl->initialSize.x) * 0.5f,
                   (io.DisplaySize.y - m_impl->initialSize.y) * 0.5f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(m_impl->initialSize, ImGuiCond_Always);
        m_impl->firstDraw = false;
    }

    if (m_impl->resetWindow) {
        m_impl->resetWindow = false;
        ImGui::SetNextWindowPos(m_impl->lastPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(m_impl->lastSize, ImGuiCond_Always);
    }

    // fade
    ImGui::SetNextWindowBgAlpha(0.96f * m_impl->alpha);

    // remove padding (custom)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    bool opened = ImGui::Begin(m_impl->title, nullptr, flags);

    if (!opened) {
        ImGui::End();
        ImGui::PopStyleVar();
        invalidateRect();
        return;
    }

    ImVec2 wpos  = ImGui::GetWindowPos();
    ImVec2 wsize = ImGui::GetWindowSize();
    int tbh = s_cachedTitleBarH.load(std::memory_order_relaxed);

    // ═══ title bar ═══
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // background gradient
        ImVec2 tbMin(wpos.x, wpos.y);
        ImVec2 tbMax(wpos.x + wsize.x, wpos.y + tbh);
        dl->AddRectFilledMultiColor(tbMin, tbMax,
            IM_COL32(0x08, 0x08, 0x0A, 255),
            IM_COL32(0x08, 0x08, 0x0A, 255),
            IM_COL32(0x14, 0x11, 0x08, 255),
            IM_COL32(0x14, 0x11, 0x08, 255));
        // bottom separator
        dl->AddLine(ImVec2(wpos.x, wpos.y + tbh),
                    ImVec2(wpos.x + wsize.x, wpos.y + tbh),
                    Theme::border(), 1.5f);
        // title text
        ImGui::SetCursorScreenPos(ImVec2(wpos.x + 24, wpos.y + (tbh - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
        ImGui::TextUnformatted(m_impl->title);
        ImGui::PopStyleColor();

        // ═══ buttons ═══
        const float btnSize = 32.0f;
        const float margin  = 8.0f;
        float bx = wpos.x + wsize.x - btnSize - margin;

        // close button
        ImGui::SetCursorScreenPos(ImVec2(bx, wpos.y + (tbh - btnSize) * 0.5f));
        ImGui::PushID("##clos");
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0x30, 0x18, 0x18, 200));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0x50, 0x20, 0x20, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0x80, 0x20, 0x20, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btnSize*0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::Button("##close", ImVec2(btnSize, btnSize))) {
            m_closeRequested = true;
        }
        // X icon
        {
            ImVec2 a = ImGui::GetItemRectMin();
            ImVec2 b = ImGui::GetItemRectMax();
            float cx = (a.x+b.x)*0.5f, cy = (a.y+b.y)*0.5f;
            ImU32 col = IM_COL32(240, 180, 180, 255);
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(cx-7, cy-7), ImVec2(cx+7, cy+7), col, 2.5f);
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(cx+7, cy-7), ImVec2(cx-7, cy+7), col, 2.5f);
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::PopID();

        // minimize button
        bx -= btnSize + margin;
        ImGui::SetCursorScreenPos(ImVec2(bx, wpos.y + (tbh - btnSize) * 0.5f));
        ImGui::PushID("##min");
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0x1A, 0x1A, 0x1F, 200));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0x30, 0x28, 0x10, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0x50, 0x40, 0x10, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btnSize*0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::Button("##min", ImVec2(btnSize, btnSize))) {
            m_closeRequested = true;  // same effect — hide to FAB
        }
        {
            ImVec2 a = ImGui::GetItemRectMin();
            ImVec2 b = ImGui::GetItemRectMax();
            float cx = (a.x+b.x)*0.5f, cy = (a.y+b.y)*0.5f;
            ImU32 col = IM_COL32(240, 200, 80, 255);
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(cx-8, cy), ImVec2(cx+8, cy), col, 2.5f);
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }

    // ═══ content (sidebar + area) ═══
    float contentH = wsize.y - tbh - 40;  // 40 = status bar

    // sidebar
    ImGui::SetCursorPos(ImVec2(0, (float)tbh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 12));
    ImGui::BeginChild("##side", ImVec2(kSidebarW, contentH), false,
        ImGuiWindowFlags_NoScrollbar);
    drawSidebar();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // content area
    ImGui::SetCursorPos(ImVec2(kSidebarW, (float)tbh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::BeginChild("##content", ImVec2(wsize.x - kSidebarW, contentH), false);
    drawContent();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // ═══ status bar ═══
    ImGui::SetCursorPos(ImVec2(0, wsize.y - 40));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 10));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
    ImGui::BeginChild("##status", ImVec2(wsize.x, 40), false, ImGuiWindowFlags_NoScrollbar);
    drawStatusBar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // ═══ resize zones ═══
    drawResizeZones();

    // ═══ drag from title bar ═══
    {
        bool hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        bool onTitle = io.MousePos.y >= wpos.y
                    && io.MousePos.y <= wpos.y + (float)tbh
                    && io.MousePos.x >= wpos.x
                    && io.MousePos.x <= wpos.x + wsize.x;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered && onTitle
            && !ImGui::IsAnyItemActive() && !m_impl->windowDragging) {
            m_impl->dragPending     = true;
            m_impl->dragStartMouse  = io.MousePos;
            m_impl->dragStartWindow = wpos;
        }
        if (m_impl->dragPending) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_impl->dragPending = false;
            } else {
                float dx = io.MousePos.x - m_impl->dragStartMouse.x;
                float dy = io.MousePos.y - m_impl->dragStartMouse.y;
                if (dx*dx + dy*dy > kDragThresh2) {
                    m_impl->windowDragging = true;
                    m_impl->dragPending    = false;
                }
            }
        }
        if (m_impl->windowDragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImVec2 d(io.MousePos.x - m_impl->dragStartMouse.x,
                         io.MousePos.y - m_impl->dragStartMouse.y);
                ImVec2 np(m_impl->dragStartWindow.x + d.x,
                          m_impl->dragStartWindow.y + d.y);
                // clamp
                if (np.x < 0) np.x = 0;
                if (np.y < 0) np.y = 0;
                if (np.x + wsize.x > io.DisplaySize.x) np.x = io.DisplaySize.x - wsize.x;
                if (np.y + wsize.y > io.DisplaySize.y) np.y = io.DisplaySize.y - wsize.y;
                ImGui::SetWindowPos(np);
            } else {
                m_impl->windowDragging = false;
            }
        }
    }

    // ═══ save rect ═══
    {
        std::lock_guard<std::mutex> lk(m_impl->rectMu);
        m_impl->rectPos   = ImGui::GetWindowPos();
        m_impl->rectSize  = ImGui::GetWindowSize();
        m_impl->rectValid = true;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// ═══════════════════════════════════════════════════════════
//  sidebar tabs
// ═══════════════════════════════════════════════════════════
void MainWindow::drawSidebar() {
    struct Tab { const char* label; const char* icon; int id; };
    static const Tab tabs[] = {
        {"CLASSES", "C", 0},
        {"TRACE",   "T", 1},
        {"CALL",    "F", 2},
        {"LOGS",    "L", 3},
        {"NATIVE",  "N", 5},
        {"SETTINGS","S", 4},
    };

    for (const auto& t : tabs) {
        bool selected = (m_impl->currentTab == t.id);

        ImVec4 bg    = selected ? ImVec4(0.20f, 0.16f, 0.05f, 1.0f)
                                : ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
        ImVec4 hov   = selected ? bg
                                : ImVec4(0.16f, 0.14f, 0.08f, 1.0f);
        ImVec4 act   = ImVec4(0.30f, 0.24f, 0.10f, 1.0f);
        ImVec4 txt   = selected ? ImVec4(1.00f, 0.83f, 0.30f, 1.0f)
                                : ImVec4(0.70f, 0.70f, 0.74f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
        ImGui::PushStyleColor(ImGuiCol_Text,          txt);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 18));
        ImGui::PushID(t.id);

        if (ImGui::Button(t.label, ImVec2(kSidebarW - 16, 72))) {
            m_impl->currentTab = t.id;
        }

        ImGui::PopID();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::Spacing();
    }
}

// ═══════════════════════════════════════════════════════════
//  content per tab (placeholder — responses 4-9 fill in)
// ═══════════════════════════════════════════════════════════
void MainWindow::drawContent() {
    switch (m_impl->currentTab) {
        case 0:
            Theme::drawSectionHeader("CLASSES");
            ImGui::TextWrapped(
                "متصفح الكلاسات سيكون هنا.\n\n"
                "سيتم عرض شجرة الحزم والكلاسات والأعضاء (methods/fields) "
                "لعملية التطبيق الحالية، مع البحث الفوري والتصفية.");
            break;
        case 1:
            Theme::drawSectionHeader("TRACE");
            ImGui::TextWrapped(
                "أداة Trace ستكون هنا.\n\n"
                "اختر دالة، اضغط Trace، شاهد المعاملات وقيم الكائنات وbacktrace "
                "بعمق غير محدود، يتم تحديثه لحظياً.");
            break;
        case 2:
            CallTab::instance().draw();
            break;
        case 3:
            LogsTab::instance().draw();
            break;
        case 4:
            SettingsTab::instance().draw();
            break;
        case 5:
            NativeTab::instance().draw();
            break;
    }
}

void MainWindow::drawStatusBar() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextUnformatted("YAM-BZ");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
    ImGui::Text(" · %s", m_impl->status);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    auto favs = Favorites::instance().list();
    if (!favs.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.83f, 0.30f, 1.0f), " · ★ %zu", favs.size());
    }
}

// ═══════════════════════════════════════════════════════════
//  resize zones (5 zones via InvisibleButton)
// ═══════════════════════════════════════════════════════════
void MainWindow::drawResizeZones() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 wPos  = ImGui::GetWindowPos();
    ImVec2 wSize = ImGui::GetWindowSize();

    // top (drag، نستعمله كـ drag لكن مع زر مستقل)
    ImGui::SetCursorScreenPos(ImVec2(wPos.x + kResizeZone, wPos.y));
    ImGui::InvisibleButton("##rz_top",
        ImVec2(wSize.x - 2*kResizeZone, 8),
        ImGuiButtonFlags_MouseButtonLeft);

    // bottom
    ImGui::SetCursorScreenPos(ImVec2(wPos.x + kResizeZone, wPos.y + wSize.y - kResizeZone));
    ImGui::InvisibleButton("##rz_bot",
        ImVec2(wSize.x - 2*kResizeZone, kResizeZone),
        ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive()) {
        ImVec2 d = io.MouseDelta;
        float nh = std::min(std::max(wSize.y + d.y, kMinH), io.DisplaySize.y);
        ImGui::SetWindowSize(ImVec2(wSize.x, nh));
    }

    // left
    ImGui::SetCursorScreenPos(ImVec2(wPos.x, wPos.y + kResizeZone));
    ImGui::InvisibleButton("##rz_left",
        ImVec2(kResizeZone, wSize.y - 2*kResizeZone),
        ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive()) {
        ImVec2 d = io.MouseDelta;
        float nw = std::min(std::max(wSize.x - d.x, kMinW), io.DisplaySize.x);
        ImGui::SetWindowSize(ImVec2(nw, wSize.y));
    }

    // right
    ImGui::SetCursorScreenPos(ImVec2(wPos.x + wSize.x - kResizeZone, wPos.y + kResizeZone));
    ImGui::InvisibleButton("##rz_right",
        ImVec2(kResizeZone, wSize.y - 2*kResizeZone),
        ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive()) {
        ImVec2 d = io.MouseDelta;
        float nw = std::min(std::max(wSize.x + d.x, kMinW), io.DisplaySize.x);
        ImGui::SetWindowSize(ImVec2(nw, wSize.y));
    }

    // bottom-right corner
    ImGui::SetCursorScreenPos(
        ImVec2(wPos.x + wSize.x - kCorner, wPos.y + wSize.y - kCorner));
    ImGui::InvisibleButton("##rz_corner",
        ImVec2(kCorner, kCorner),
        ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive()) {
        ImVec2 d = io.MouseDelta;
        float nw = std::min(std::max(wSize.x + d.x, kMinW), io.DisplaySize.x);
        float nh = std::min(std::max(wSize.y + d.y, kMinH), io.DisplaySize.y);
        ImGui::SetWindowSize(ImVec2(nw, nh));
    }
}

} // namespace yambz
