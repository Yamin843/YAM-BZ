#include "Theme.h"
#include <unistd.h>

namespace yambz {

static inline ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r/255.0f, g/255.0f, b/255.0f, a);
}

ImU32 Theme::gold()        { return IM_COL32(0xD4, 0xA0, 0x17, 0xFF); }
ImU32 Theme::textPrimary() { return IM_COL32(0xF2, 0xF2, 0xF4, 0xFF); }
ImU32 Theme::textDim()     { return IM_COL32(0xA0, 0xA0, 0xA8, 0xFF); }

void Theme::sectionHeader(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void Theme::apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    s.WindowPadding = ImVec2(20, 18);
    s.FramePadding  = ImVec2(14, 8);
    s.ItemSpacing   = ImVec2(10, 10);
    s.ScrollbarSize = 32;
    s.GrabMinSize   = 44;

    s.WindowRounding    = 14;
    s.ChildRounding     = 10;
    s.FrameRounding     = 8;
    s.PopupRounding     = 10;
    s.ScrollbarRounding = 10;
    s.GrabRounding      = 8;

    s.WindowBorderSize = 2;
    s.FrameBorderSize  = 0.5f;
    s.PopupBorderSize  = 2;

    c[ImGuiCol_Text]            = rgba(0xF2, 0xF2, 0xF4);
    c[ImGuiCol_TextDisabled]    = rgba(0x70, 0x70, 0x78);
    c[ImGuiCol_WindowBg]        = rgba(0x0E, 0x0E, 0x11);
    c[ImGuiCol_ChildBg]         = rgba(0x0B, 0x0B, 0x0E);
    c[ImGuiCol_PopupBg]         = rgba(0x12, 0x12, 0x16, 0.98f);
    c[ImGuiCol_Border]          = rgba(0x2E, 0x28, 0x1A);
    c[ImGuiCol_FrameBg]         = rgba(0x1A, 0x1A, 0x1F);
    c[ImGuiCol_FrameBgHovered]  = rgba(0x24, 0x24, 0x2A);
    c[ImGuiCol_FrameBgActive]   = rgba(0x2E, 0x2E, 0x36);
    c[ImGuiCol_TitleBg]         = rgba(0x08, 0x08, 0x0A);
    c[ImGuiCol_TitleBgActive]   = rgba(0x14, 0x11, 0x08);
    c[ImGuiCol_Button]          = rgba(0x1A, 0x1A, 0x1F);
    c[ImGuiCol_ButtonHovered]   = rgba(0x25, 0x1E, 0x0A);
    c[ImGuiCol_ButtonActive]    = rgba(0x3A, 0x2C, 0x0E);
    c[ImGuiCol_Header]          = rgba(0x1A, 0x1A, 0x1E);
    c[ImGuiCol_HeaderHovered]   = rgba(0x25, 0x20, 0x10);
    c[ImGuiCol_HeaderActive]    = rgba(0x3A, 0x2E, 0x12);
    c[ImGuiCol_Separator]       = rgba(0x8A, 0x68, 0x10, 0.55f);
    c[ImGuiCol_CheckMark]       = rgba(0xF0, 0xBE, 0x3C);
    c[ImGuiCol_SliderGrab]      = rgba(0xD4, 0xA0, 0x17);
    c[ImGuiCol_SliderGrabActive]= rgba(0xF0, 0xBE, 0x3C);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = rgba(0xFF, 0xFF, 0xFF, 0.20f);

    // Load system font
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const char* fonts[] = {
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
        "/system/fonts/NotoSans-Regular.ttf",
    };
    ImFont* f = nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.RasterizerMultiply = 1.05f;
    for (auto p : fonts) {
        if (access(p, R_OK) != 0) continue;
        f = io.Fonts->AddFontFromFileTTF(p, 36.0f, &cfg);
        if (f) break;
    }
    if (!f) {
        ImFontConfig dc;
        dc.SizePixels = 28.0f;
        io.Fonts->AddFontDefault(&dc);
    }
}

} // namespace yambz
