#include "TraceTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "trace/OceanState.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <cstdio>

namespace yambz {

TraceTab& TraceTab::instance() {
    static TraceTab t;
    return t;
}

// ═══════════════════════════════════════════════════════════
//  Colors per event kind
// ═══════════════════════════════════════════════════════════
static ImVec4 colorForKind(int kind) {
    switch (kind) {
        case 1: return ImVec4(0.3f, 0.9f, 0.9f, 1); // CALL_ENTER — cyan
        case 2: return ImVec4(0.4f, 0.8f, 0.4f, 1); // CALL — green
        case 3: return ImVec4(0.4f, 0.6f, 0.95f, 1);// RETURN — blue
        case 4: return ImVec4(0.7f, 0.7f, 0.4f, 1); // EXEC — yellow
        case 5: return ImVec4(0.9f, 0.6f, 0.3f, 1); // BLOCK — orange
        case 6: return ImVec4(0.95f, 0.5f, 0.9f, 1);// FIELD_READ — pink
        case 7: return ImVec4(0.95f, 0.35f, 0.4f, 1);// FIELD_WRITE — red
    }
    return ImVec4(0.7f, 0.7f, 0.7f, 1);
}

static const char* kindName(int kind) {
    switch (kind) {
        case 1: return "CALL_ENTER";
        case 2: return "CALL";
        case 3: return "RETURN";
        case 4: return "EXEC";
        case 5: return "BLOCK";
        case 6: return "FILE_READ";
        case 7: return "FIELD_WRITE";
    }
    return "?";
}

// ═══════════════════════════════════════════════════════════
//  draw: Config tab
// ═══════════════════════════════════════════════════════════
static void drawConfigTab(OceanUIState& ui) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));

    // ─── Mode ───
    Theme::sectionHeader("Mode");
    int mode = ui.config.mode;
    if (ImGui::RadioButton("Global (all threads)", &mode, 0)) ui.config.mode = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Current thread", &mode, 1)) ui.config.mode = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Specific member", &mode, 2)) ui.config.mode = 2;

    if (ui.config.mode == 2) {
        ImGui::Text("Member spec:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##spec",
            "com.example.Foo#bar(java.lang.String,int)  (or #<init> for ctor)",
            ui.config.memberSpec, sizeof(ui.config.memberSpec));
    }

    ImGui::Spacing();

    // ─── Events ───
    Theme::sectionHeader("Events (8 types)");
    ImGui::Checkbox("CALL_ENTER", &ui.config.evCallEnter);  ImGui::SameLine(200);
    ImGui::Checkbox("CALL", &ui.config.evCall);  ImGui::SameLine(300);
    ImGui::Checkbox("RETURN", &ui.config.evReturn);
    ImGui::Checkbox("EXEC (heavy)", &ui.config.evExec);  ImGui::SameLine(200);
    ImGui::Checkbox("BLOCK", &ui.config.evBlock);  ImGui::SameLine(300);
    ImGui::Checkbox("FILE_READ", &ui.config.evFileRead);
    ImGui::Checkbox("FIELD_WRITE", &ui.config.evFieldWrite);

    ImGui::Spacing();

    // ─── Scope ───
    Theme::sectionHeader("Scope (5)");
    ImGui::Checkbox("kJdk", &ui.config.scJdk);  ImGui::SameLine(120);
    ImGui::Checkbox("kPlatform", &ui.config.scPlatform);  ImGui::SameLine(260);
    ImGui::Checkbox("kApplication", &ui.config.scApplication);
    ImGui::Checkbox("kPackageFilter", &ui.config.scPackageFilter);  ImGui::SameLine(200);
    ImGui::Checkbox("kEntryOnly", &ui.config.scEntryOnly);

    if (ui.config.scPackageFilter) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pkgfilter", "package prefix...",
            ui.config.packagePrefix, sizeof(ui.config.packagePrefix));
    }

    ImGui::Spacing();

    // ─── Sinker ───
    Theme::sectionHeader("Sinker");
    int sk = ui.config.sinkerKind;
    if (ImGui::RadioButton("Custom (all events)", &sk, 0)) ui.config.sinkerKind = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("KeyWord", &sk, 1)) ui.config.sinkerKind = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("String", &sk, 2)) ui.config.sinkerKind = 2;
    ImGui::SameLine();
    if (ImGui::RadioButton("Empty (count only)", &sk, 3)) ui.config.sinkerKind = 3;

    if (ui.config.sinkerKind == 1 || ui.config.sinkerKind == 2) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##kw", "keyword...",
            ui.config.sinkerKeyword, sizeof(ui.config.sinkerKeyword));
    }

    ImGui::Spacing();

    // ─── Limits ───
    Theme::sectionHeader("Limits");
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Timeout (ms)", &ui.config.timeoutMs, 1000, 10000);
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Max events", &ui.config.maxEvents, 1000, 100000);
    ImGui::Checkbox("Only main thread", &ui.config.onlyMainThread);

    ImGui::Spacing();

    // ─── Controls ───
    if (!ui.tracing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
        if (ImGui::Button("▶ Start Trace", ImVec2(150, 36))) {
            if (oceanStart()) {
                Notifications::instance().push("Ocean trace started", 2.0f);
            } else {
                Notifications::instance().push("Ocean trace failed", 3.0f);
            }
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("■ Stop Trace", ImVec2(150, 36))) {
            oceanStop();
            Notifications::instance().push("Ocean stopped", 2.0f);
        }
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Sync", ImVec2(80, 36))) {
        oceanSync();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear events", ImVec2(120, 36))) {
        oceanClearEvents();
    }

    ImGui::PopStyleVar();
}

// ═══════════════════════════════════════════════════════════
//  draw: Events tab
// ═══════════════════════════════════════════════════════════
static void drawEventsTab(OceanUIState& ui) {
    // Stats
    ImGui::TextDisabled("%s | total: %ld | %s",
        ui.tracing ? "TRACING" : "IDLE",
        ui.totalEvents, "");
    ImGui::SameLine(400);
    ImGui::TextColored(colorForKind(2), "CALL:%ld", ui.countByKind[2]);
    ImGui::SameLine();
    ImGui::TextColored(colorForKind(3), "RET:%ld", ui.countByKind[3]);
    ImGui::SameLine();
    ImGui::TextColored(colorForKind(4), "EXEC:%ld", ui.countByKind[4]);
    ImGui::SameLine();
    ImGui::TextColored(colorForKind(6), "fR:%ld", ui.countByKind[6]);
    ImGui::SameLine();
    ImGui::TextColored(colorForKind(7), "fW:%ld", ui.countByKind[7]);

    // Filter
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", "search...", ui.searchBuf, sizeof(ui.searchBuf));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    const char* filters[] = {"All", "CALL_ENTER", "CALL", "RETURN", "EXEC", "BLOCK", "FILE_READ", "FIELD_WRITE"};
    ImGui::Combo("##filt", &ui.filterKind, filters, 8);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &ui.autoScroll);

    ImGui::Separator();

    // Snapshot events under lock
    std::vector<OceanEvent> snapshot;
    {
        std::lock_guard<std::mutex> lk(ui.eventsMu);
        snapshot = ui.events;
    }

    // Compute filter
    std::string q = ui.searchBuf;
    for (auto& c : q) c = (char)std::tolower((unsigned char)c);

    std::vector<size_t> visible;
    visible.reserve(snapshot.size());
    for (size_t i = 0; i < snapshot.size(); i++) {
        const auto& e = snapshot[i];
        if (ui.filterKind != 0 && e.kind != ui.filterKind) continue;
        if (!q.empty()) {
            std::string hay = e.methodDesc + " " + e.extra + " " + e.className + " " + e.methodName;
            for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
            if (hay.find(q) == std::string::npos) continue;
        }
        visible.push_back(i);
    }

    // Split: list (left) + detail (right)
    float listW = ImGui::GetContentRegionAvail().x * 0.55f;
    ImGui::BeginChild("##evlist", ImVec2(listW, 0), true);
    ImGuiListClipper clip;
    clip.Begin((int)visible.size(), 20.0f);
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            size_t idx = visible[k];
            const auto& e = snapshot[idx];
            ImGui::PushID((int)idx);

            // Indent by depth
            for (int d = 0; d < e.depth && d < 15; d++) ImGui::Indent(8.0f);

            ImGui::TextColored(colorForKind(e.kind), "[%s]", kindName(e.kind));
            ImGui::SameLine();
            const char* m = e.methodDesc.empty() ? "?" : e.methodDesc.c_str();
            std::string methodPreview = m;
            if (methodPreview.length() > 60) methodPreview = methodPreview.substr(0, 60) + "…";

            bool sel = (ui.selectedEvent == (int)idx);
            if (ImGui::Selectable(methodPreview.c_str(), sel)) {
                ui.selectedEvent = (int)idx;
            }
            if (!e.extra.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", e.extra.c_str());
            }

            for (int d = 0; d < e.depth && d < 15; d++) ImGui::Unindent(8.0f);

            ImGui::PopID();
        }
    }
    clip.End();
    ImGui::EndChild();

    ImGui::SameLine();

    // Detail pane
    ImGui::BeginChild("##evdetail", ImVec2(0, 0), true);
    if (ui.selectedEvent >= 0 && ui.selectedEvent < (int)snapshot.size()) {
        const auto& e = snapshot[ui.selectedEvent];
        Theme::sectionHeader("Event Detail");
        ImGui::Text("Timestamp: %lld ms", (long long)e.ts);
        ImGui::Text("Kind: %s", kindName(e.kind));
        ImGui::Text("Depth: %d", e.depth);
        ImGui::Separator();
        ImGui::TextWrapped("Method: %s", e.methodDesc.c_str());
        if (!e.className.empty()) ImGui::TextWrapped("Class: %s", e.className.c_str());
        if (!e.methodName.empty()) ImGui::TextWrapped("Name: %s", e.methodName.c_str());
        if (!e.extra.empty()) {
            ImGui::Separator();
            Theme::sectionHeader("Details");
            ImGui::TextWrapped("%s", e.extra.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("Copy as text")) {
            std::string copyText = std::string(kindName(e.kind)) + " " + e.methodDesc + " " + e.extra;
            // TODO: clipboard via JNI
            Notifications::instance().push("Copied to clipboard", 2.0f);
        }
    } else {
        ImGui::TextDisabled("(select an event)");
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════
//  draw: Status tab
// ═══════════════════════════════════════════════════════════
static void drawStatusTab(OceanUIState& ui) {
    Theme::sectionHeader("Status");
    ImGui::Text("Tracing: %s", ui.tracing ? "YES" : "NO");
    ImGui::Text("Session ID: %d", ui.sessionId);
    ImGui::Text("Mode: %d", ui.mode);
    ImGui::Text("Total events: %ld", ui.totalEvents);
    ImGui::Text("Stop reason: %d", ui.stopReason);
    ImGui::Text("Trace count (Ocean): %ld", ui.traceCount);

    ImGui::Separator();
    Theme::sectionHeader("Per-Kind Counts");
    for (int i = 1; i <= 7; i++) {
        ImGui::TextColored(colorForKind(i), "%s: %ld", kindName(i), ui.countByKind[i]);
    }

    ImGui::Separator();
    if (ImGui::Button("Sync", ImVec2(120, 0))) oceanSync();
    ImGui::SameLine();
    if (ImGui::Button("Clear events", ImVec2(140, 0))) oceanClearEvents();
}

// ═══════════════════════════════════════════════════════════
//  Main draw
// ═══════════════════════════════════════════════════════════
void TraceTab::draw() {
    auto& ui = oceanUI();
    if (!ui.initialized) {
        ui.initialized = true;
    }

    // Sub-tabs
    const char* subTabs[] = {"Config", "Events", "Status"};
    for (int i = 0; i < 3; i++) {
        if (i > 0) ImGui::SameLine();
        bool sel = (ui.currentTab == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.24f, 0.1f, 1.0f));
        if (ImGui::Button(subTabs[i], ImVec2(120, 32))) ui.currentTab = i;
        if (sel) ImGui::PopStyleColor();
    }

    ImGui::Separator();

    switch (ui.currentTab) {
        case 0: drawConfigTab(ui); break;
        case 1: drawEventsTab(ui); break;
        case 2: drawStatusTab(ui); break;
    }
}

} // namespace yambz
