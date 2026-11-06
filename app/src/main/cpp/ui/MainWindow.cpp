#include "MainWindow.h"
#include "Theme.h"
#include "../tabs/ClassesTab.h"
#include "../tabs/TraceTab.h"
#include "../tabs/CallTab.h"
#include "../tabs/NativeTab.h"
#include "../tabs/StalkerTab.h"
#include "../tabs/SettingsTab.h"
#include "../tabs/BackupTab.h"
#include "../tabs/HeapTab.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <mutex>

namespace yambz {

struct MainWindow::ImplState {
    int  tab = 0;
    char title[64] = "YAM-BZ";
    mutable std::mutex rectMu;
    ImVec2 rectPos{0, 0};
    ImVec2 rectSize{0, 0};
    bool   rectValid = false;
    float  alpha = 0.0f;
    float  targetAlpha = 0.0f;
    bool   firstDraw = true;
};

MainWindow::MainWindow() : m_impl(new ImplState()) {}
MainWindow& MainWindow::instance() { static MainWindow w; return w; }

void MainWindow::show() { m_visible = true; m_impl->targetAlpha = 1.0f; }
void MainWindow::hide() { m_visible = false; m_impl->targetAlpha = 0.0f; }
void MainWindow::toggle() { m_visible ? hide() : show(); }

bool MainWindow::animating() const {
    return std::fabs(m_impl->alpha - m_impl->targetAlpha) > 0.01f;
}

bool MainWindow::hitTest(float x, float y) const {
    std::lock_guard<std::mutex> lk(m_impl->rectMu);
    if (!m_impl->rectValid) return false;
    return x >= m_impl->rectPos.x && x <= m_impl->rectPos.x + m_impl->rectSize.x
        && y >= m_impl->rectPos.y && y <= m_impl->rectPos.y + m_impl->rectSize.y;
}

void MainWindow::invalidateRect() {
    std::lock_guard<std::mutex> lk(m_impl->rectMu);
    m_impl->rectValid = false;
}

void MainWindow::draw() {
    float step = ImGui::GetIO().DeltaTime * 10.0f;
    if (m_impl->alpha < m_impl->targetAlpha)
        m_impl->alpha = std::min(m_impl->alpha + step, m_impl->targetAlpha);
    else if (m_impl->alpha > m_impl->targetAlpha)
        m_impl->alpha = std::max(m_impl->alpha - step, m_impl->targetAlpha);

    if (m_impl->alpha < 0.01f && !m_visible) {
        invalidateRect();
        return;
    }
    drawMain();
}

void MainWindow::drawMain() {
    ImGuiIO& io = ImGui::GetIO();

    ImVec2 size(io.DisplaySize.x * 0.92f, io.DisplaySize.y * 0.88f);
    ImVec2 pos((io.DisplaySize.x - size.x) * 0.5f,
               (io.DisplaySize.y - size.y) * 0.5f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f * m_impl->alpha);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool opened = ImGui::Begin("##yamz_main", nullptr, flags);
    if (!opened) {
        ImGui::End();
        ImGui::PopStyleVar();
        invalidateRect();
        return;
    }

    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsize = ImGui::GetWindowSize();
    const float tbh = 64.0f;
    const float sbw = 130.0f;
    const float sbh = 40.0f;

    // ─── Title bar ───
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(
        ImVec2(wpos.x, wpos.y), ImVec2(wpos.x + wsize.x, wpos.y + tbh),
        IM_COL32(0x08, 0x08, 0x0A, 255), IM_COL32(0x08, 0x08, 0x0A, 255),
        IM_COL32(0x14, 0x11, 0x08, 255), IM_COL32(0x14, 0x11, 0x08, 255));
    dl->AddLine(ImVec2(wpos.x, wpos.y + tbh),
                ImVec2(wpos.x + wsize.x, wpos.y + tbh),
                IM_COL32(0x2E, 0x28, 0x1A, 255), 1.5f);

    ImGui::SetCursorScreenPos(ImVec2(wpos.x + 20, wpos.y + 18));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextUnformatted(m_impl->title);
    ImGui::PopStyleColor();

    // Close button
    float bx = wpos.x + wsize.x - 44;
    ImGui::SetCursorScreenPos(ImVec2(bx, wpos.y + 16));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0x30, 0x18, 0x18, 200));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0x50, 0x20, 0x20, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
    if (ImGui::Button("X", ImVec2(32, 32))) {
        hide();
        // Notify Java to hide panel
        extern void yambz_requestClosePanel();
        yambz_requestClosePanel();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // ─── Sidebar ───
    ImGui::SetCursorPos(ImVec2(0, tbh));
    ImGui::BeginChild("##sb", ImVec2(sbw, wsize.y - tbh - sbh), false);
    const char* tabs[] = {"CLASSES", "TRACE", "CALL", "NATIVE", "STALKER", "BACKUP", "HEAP", "SETTINGS"};
    for (int i = 0; i < 8; i++) {
        bool sel = (m_impl->tab == i);
        ImVec4 bg = sel ? ImVec4(0.20f, 0.16f, 0.05f, 1.0f)
                        : ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.14f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.24f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? ImVec4(1.0f, 0.83f, 0.30f, 1.0f)
                                                 : ImVec4(0.70f, 0.70f, 0.74f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        if (ImGui::Button(tabs[i], ImVec2(sbw - 16, 64))) m_impl->tab = i;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::Spacing();
    }
    ImGui::EndChild();

    // ─── Content ───
    ImGui::SetCursorPos(ImVec2(sbw, tbh));
    ImGui::BeginChild("##ct", ImVec2(wsize.x - sbw, wsize.y - tbh - sbh), false);
    switch (m_impl->tab) {
        case 0: ClassesTab::instance().draw(); break;
        case 1: TraceTab::instance().draw(); break;
        case 2: CallTab::instance().draw(); break;
        case 3: NativeTab::instance().draw(); break;
        case 4: StalkerTab::instance().draw(); break;
        case 5: BackupTab::instance().draw(); break;
        case 6: HeapTab::instance().draw(); break;
        case 7: SettingsTab::instance().draw(); break;
    }
    ImGui::EndChild();

    // ─── Status bar ───
    ImGui::SetCursorPos(ImVec2(0, wsize.y - sbh));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
    ImGui::BeginChild("##stb", ImVec2(wsize.x, sbh), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(20, 10));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextUnformatted("YAM-BZ v1.0.0");
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Save rect for hit test
    {
        std::lock_guard<std::mutex> lk(m_impl->rectMu);
        m_impl->rectPos = ImGui::GetWindowPos();
        m_impl->rectSize = ImGui::GetWindowSize();
        m_impl->rectValid = true;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace yambz
