#include "SearchBar.h"
#include "Theme.h"
#include "../core/Logger.h"
#include <imgui.h>
#include <cstring>
#include <chrono>

namespace yambz {

char SearchBar::s_queryBuf[512] = {0};
bool SearchBar::s_initialized = false;
bool SearchBar::s_showFilters = false;
char SearchBar::s_pkgBuf[256] = {0};
double SearchBar::s_lastTypedAt = 0.0;

void SearchBar::clear() {
    s_queryBuf[0] = '\0';
}

bool SearchBar::draw(FilterOptions& opts) {
    if (!s_initialized) {
        // نسخ من opts لأول مرة
        std::strncpy(s_queryBuf, opts.query.c_str(), sizeof(s_queryBuf) - 1);
        std::strncpy(s_pkgBuf, opts.packagePrefix.c_str(), sizeof(s_pkgBuf) - 1);
        s_initialized = true;
    }

    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));

    // ─── Search input ───
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
    if (ImGui::InputTextWithHint("##search", "ابحث في الكلاسات / الدوال / الحقول...",
        s_queryBuf, sizeof(s_queryBuf)))
    {
        s_lastTypedAt = ImGui::GetTime();
        // debounce — سنُطبّق لاحقاً
        std::string newQ = s_queryBuf;
        if (opts.caseSensitive) {
            // keep as-is
        } else {
            for (auto& c : newQ) c = (char)std::tolower((unsigned char)c);
        }
        if (newQ != opts.query) {
            opts.query = newQ;
            changed = true;
        }
    }

    // clear button (X)
    ImGui::SameLine(0, 6);
    if (ImGui::Button("×", ImVec2(36, 0))) {
        s_queryBuf[0] = '\0';
        opts.query.clear();
        changed = true;
    }

    // ─── Mode toggles ───
    ImGui::SameLine(0, 12);

    auto modeBtn = [&](const char* label, uint8_t bit) -> bool {
        bool active = (opts.kindMask & bit) != 0;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.24f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.83f, 0.30f, 1.0f));
        }
        bool clicked = ImGui::Button(label, ImVec2(44, 0));
        if (active) ImGui::PopStyleColor(2);
        return clicked;
    };

    if (modeBtn("C", 0x04)) { opts.kindMask ^= 0x04; changed = true; }
    ImGui::SameLine(0, 4);
    if (modeBtn("M", 0x02)) { opts.kindMask ^= 0x02; changed = true; }
    ImGui::SameLine(0, 4);
    if (modeBtn("F", 0x01)) { opts.kindMask ^= 0x01; changed = true; }

    // ─── Filters button ───
    ImGui::SameLine(0, 8);
    if (ImGui::Button("⚙", ImVec2(36, 0))) {
        s_showFilters = !s_showFilters;
        if (s_showFilters) ImGui::OpenPopup("##filters");
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // ─── Filters popup ───
    if (ImGui::BeginPopup("##filters")) {
        ImGui::TextUnformatted("Filters");
        ImGui::Separator();

        ImGui::TextUnformatted("Package prefix:");
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputTextWithHint("##pkg", "com.example.",
            s_pkgBuf, sizeof(s_pkgBuf)))
        {
            opts.packagePrefix = s_pkgBuf;
            changed = true;
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Case sensitive", &opts.caseSensitive)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Regex", &opts.useRegex)) changed = true;

        ImGui::Spacing();
        ImGui::TextUnformatted("Sort by:");
        int cur = (int)opts.sortBy;
        if (ImGui::RadioButton("Name", &cur, 0)) { opts.sortBy = FilterOptions::SortBy::Name; changed = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Package", &cur, 1)) { opts.sortBy = FilterOptions::SortBy::Package; changed = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Members", &cur, 2)) { opts.sortBy = FilterOptions::SortBy::Members; changed = true; }

        ImGui::Checkbox("Descending", &opts.sortDescending);

        ImGui::Separator();
        if (ImGui::Button("Reset", ImVec2(120, 0))) {
            opts = FilterOptions{};
            s_queryBuf[0] = '\0';
            s_pkgBuf[0] = '\0';
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    return changed;
}

} // namespace yambz
