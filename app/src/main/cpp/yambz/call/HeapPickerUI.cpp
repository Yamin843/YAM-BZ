#include "HeapPickerUI.h"
#include "HeapPicker.h"
#include "ui/Theme.h"
#include "core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include "core/Helpers.h"

namespace yambz {

HeapPickerUIState& heapPickerUI() {
    static HeapPickerUIState s;
    return s;
}

void drawHeapPicker() {
    auto& ui = heapPickerUI();
    if (!ui.show) return;

    auto& hp = HeapPicker::instance();

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Appearing);
    bool open = true;
    ImGui::Begin("Find existing instance", &open, ImGuiWindowFlags_NoCollapse);
    if (!open) { ui.show = false; ImGui::End(); return; }

    // type input
    ImGui::Text("Class name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(500);
    ImGui::InputText("##type", ui.searchType, sizeof(ui.searchType));
    ImGui::SameLine();
    if (ImGui::Button("Search")) {
        hp.startSearch(ui.searchType);
        ui.scrollOffset = 0;
    }
    ImGui::SameLine();
    if (hp.isSearching()) {
        if (ImGui::Button("Cancel")) hp.cancel();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "⏳ searching...");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) hp.clear(nullptr);

    ImGui::Separator();
    ImGui::Text("Results: %zu", hp.totalResults());

    // list
    ImGui::BeginChild("##results", ImVec2(0, -50), true);
    auto results = hp.snapshot((size_t)ui.scrollOffset, 500);
    for (size_t i = 0; i < results.size(); i++) {
        const auto& e = results[i];
        ImGui::PushID((int)e.idx);
        bool sel = (ui.selectedIdx == (int)e.idx);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.83f, 0.30f, 1.0f));
        char line[512];
        snprintf(line, sizeof line, "#%u @0x%llx — %s",
            e.idx, (unsigned long long)e.addr, e.display.c_str());
        if (ImGui::Selectable(line, sel)) ui.selectedIdx = (int)e.idx;
        if (sel) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();

    // controls
    if (ui.selectedIdx >= 0) {
        ImGui::Text("Selected: #%d", ui.selectedIdx);
        ImGui::SameLine();
        if (ImGui::Button("Use instance")) {

            yambz_useInstanceFromPicker(ui.selectedIdx);
            ui.show = false;
        }
    } else {
        ImGui::TextDisabled("(select an instance)");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::DragInt("##scroll", &ui.scrollOffset, 100, 0, (int)hp.totalResults());

    ImGui::End();
}

} // namespace yambz
