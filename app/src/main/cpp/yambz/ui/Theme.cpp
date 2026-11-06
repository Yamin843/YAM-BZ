#include "Theme.h"
#include "../core/Logger.h"

#include <unistd.h>

namespace yambz {

float Theme::s_fontSize = 38.0f;

// ── color helpers ──────────────────────────────────────────
static inline ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r/255.0f, g/255.0f, b/255.0f, a);
}

ImU32 Theme::gold()        { return IM_COL32(0xD4, 0xA0, 0x17, 0xFF); }
ImU32 Theme::goldDim()     { return IM_COL32(0xA8, 0x80, 0x1A, 0xFF); }
ImU32 Theme::goldBright()  { return IM_COL32(0xF0, 0xBE, 0x3C, 0xFF); }
ImU32 Theme::textPrimary() { return IM_COL32(0xF2, 0xF2, 0xF4, 0xFF); }
ImU32 Theme::textDim()     { return IM_COL32(0xA0, 0xA0, 0xA8, 0xFF); }
ImU32 Theme::bgCard()      { return IM_COL32(0x1A, 0x1A, 0x1F, 0xFF); }
ImU32 Theme::bgDeeper()    { return IM_COL32(0x0A, 0x0A, 0x0C, 0xFF); }
ImU32 Theme::border()      { return IM_COL32(0x2E, 0x2A, 0x1F, 0xFF); }

// ── font loading ───────────────────────────────────────────
static ImFont* loadSystemFont(float size) {
    const char* candidates[] = {
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/RobotoCondensed-Regular.ttf",
        "/product/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
        "/system/fonts/NotoSans-Regular.ttf",
        "/system/fonts/NotoSansArabic-Regular.ttf",
    };
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = false;
    cfg.RasterizerMultiply = 1.05f;

    for (const char* path : candidates) {
        if (access(path, R_OK) != 0) continue;
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, size, &cfg);
        if (f) {
            LOGI("Theme: font %s @ %.0fpx", path, size);
            return f;
        }
    }
    LOGW("Theme: fallback ProggyClean");
    ImFontConfig dc;
    dc.SizePixels = size * 0.72f;
    dc.OversampleH = 2;
    dc.OversampleV = 2;
    return io.Fonts->AddFontDefault(&dc);
}

void Theme::reloadFont() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    loadSystemFont(s_fontSize);
    io.Fonts->Build();
    // notify backend to re-upload
    io.Fonts->SetTexID(io.Fonts->TexID);
}

void Theme::setFontSize(float px) {
    if (px < 12.0f) px = 12.0f;
    if (px > 72.0f) px = 72.0f;
    s_fontSize = px;
}

float Theme::fontSize() { return s_fontSize; }

// ── apply (dark + gold) ────────────────────────────────────
void Theme::apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    // spacing
    s.WindowPadding       = ImVec2(22, 20);
    s.FramePadding        = ImVec2(16, 10);
    s.CellPadding         = ImVec2(12, 8);
    s.ItemSpacing         = ImVec2(10, 12);
    s.ItemInnerSpacing    = ImVec2(8, 6);
    s.IndentSpacing       = 24;
    s.ScrollbarSize       = 32;
    s.GrabMinSize         = 48;

    // rounding
    s.WindowRounding      = 16;
    s.ChildRounding       = 12;
    s.FrameRounding       = 10;
    s.PopupRounding       = 12;
    s.ScrollbarRounding   = 12;
    s.GrabRounding        = 10;
    s.TabRounding         = 10;

    // borders
    s.WindowBorderSize    = 2;
    s.ChildBorderSize     = 0;
    s.FrameBorderSize     = 0.5f;
    s.PopupBorderSize     = 2;
    s.TabBorderSize       = 0;

    // alignment
    s.WindowTitleAlign    = ImVec2(0.5f, 0.5f);
    s.ButtonTextAlign     = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.ColorButtonPosition      = ImGuiDir_Right;

    // anti-aliasing
    s.AntiAliasedLines          = true;
    s.AntiAliasedLinesUseTex    = true;
    s.AntiAliasedFill           = true;
    s.CircleTessellationMaxError = 0.15f;

    // === colors ===
    c[ImGuiCol_Text]                  = rgba(0xF2, 0xF2, 0xF4);
    c[ImGuiCol_TextDisabled]          = rgba(0x62, 0x62, 0x68);

    c[ImGuiCol_WindowBg]              = rgba(0x0E, 0x0E, 0x11);
    c[ImGuiCol_ChildBg]               = rgba(0x0B, 0x0B, 0x0E);
    c[ImGuiCol_PopupBg]               = rgba(0x12, 0x12, 0x16, 0.98f);
    c[ImGuiCol_MenuBarBg]             = rgba(0x0E, 0x0E, 0x11);

    c[ImGuiCol_Border]                = rgba(0x2E, 0x28, 0x1A);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]               = rgba(0x1A, 0x1A, 0x1F);
    c[ImGuiCol_FrameBgHovered]        = rgba(0x24, 0x24, 0x2A);
    c[ImGuiCol_FrameBgActive]         = rgba(0x2E, 0x2E, 0x36);

    c[ImGuiCol_TitleBg]               = rgba(0x08, 0x08, 0x0A);
    c[ImGuiCol_TitleBgActive]         = rgba(0x14, 0x11, 0x08);
    c[ImGuiCol_TitleBgCollapsed]      = rgba(0x08, 0x08, 0x0A);

    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = rgba(0xFF, 0xFF, 0xFF, 0.20f);
    c[ImGuiCol_ScrollbarGrabHovered]  = rgba(0xD4, 0xA0, 0x17, 0.35f);
    c[ImGuiCol_ScrollbarGrabActive]   = rgba(0xD4, 0xA0, 0x17);

    c[ImGuiCol_CheckMark]             = rgba(0xF0, 0xBE, 0x3C);
    c[ImGuiCol_SliderGrab]            = rgba(0xD4, 0xA0, 0x17);
    c[ImGuiCol_SliderGrabActive]      = rgba(0xF0, 0xBE, 0x3C);

    c[ImGuiCol_Button]                = rgba(0x1A, 0x1A, 0x1F);
    c[ImGuiCol_ButtonHovered]         = rgba(0x25, 0x1E, 0x0A);
    c[ImGuiCol_ButtonActive]          = rgba(0x3A, 0x2C, 0x0E);

    c[ImGuiCol_Header]                = rgba(0x1A, 0x1A, 0x1E);
    c[ImGuiCol_HeaderHovered]         = rgba(0x25, 0x20, 0x10);
    c[ImGuiCol_HeaderActive]          = rgba(0x3A, 0x2E, 0x12);

    c[ImGuiCol_Separator]             = rgba(0x8A, 0x68, 0x10, 0.55f);
    c[ImGuiCol_SeparatorHovered]      = rgba(0xD4, 0xA0, 0x17);
    c[ImGuiCol_SeparatorActive]       = rgba(0xF0, 0xBE, 0x3C);

    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive]      = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Tab]                   = rgba(0x12, 0x12, 0x14);
    c[ImGuiCol_TabHovered]            = rgba(0x1E, 0x19, 0x0A);
    c[ImGuiCol_TabSelected]           = rgba(0x2A, 0x22, 0x0E);

    c[ImGuiCol_PlotLines]             = rgba(0xD4, 0xA0, 0x17);
    c[ImGuiCol_PlotLinesHovered]      = rgba(0xF0, 0xBE, 0x3C);
    c[ImGuiCol_PlotHistogram]         = rgba(0xD4, 0xA0, 0x17);
    c[ImGuiCol_PlotHistogramHovered]  = rgba(0xF0, 0xBE, 0x3C);

    c[ImGuiCol_TableHeaderBg]         = rgba(0x14, 0x12, 0x0A);
    c[ImGuiCol_TableBorderStrong]     = rgba(0x8A, 0x68, 0x10, 0.45f);
    c[ImGuiCol_TableBorderLight]      = rgba(0x2A, 0x2A, 0x2E);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = rgba(0xFF, 0xFF, 0xFF, 0.02f);

    c[ImGuiCol_TextSelectedBg]        = rgba(0xD4, 0xA0, 0x17, 0.35f);
    c[ImGuiCol_DragDropTarget]        = rgba(0xF0, 0xBE, 0x3C);
    c[ImGuiCol_NavHighlight]          = rgba(0xF0, 0xBE, 0x3C);
    c[ImGuiCol_NavWindowingDimBg]     = rgba(0, 0, 0, 0.6f);
    c[ImGuiCol_ModalWindowDimBg]      = rgba(0, 0, 0, 0.7f);

    // font
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    loadSystemFont(s_fontSize);
}

void Theme::applyLight() {
    // TODO: نسخة فاتحة
    apply();
}

// ── UI helpers ─────────────────────────────────────────────
void Theme::drawSectionHeader(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

bool Theme::pillButton(const char* label, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 22.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.15f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.22f, 0.10f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return clicked;
}

void Theme::separator() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

} // namespace yambz
