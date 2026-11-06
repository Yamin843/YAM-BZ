#include "ClassesTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "core/MemberSpec.h"
#include "trace/OceanState.h"
#include "../jni/ClassesTab_jni.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace yambz {

ClassesTab& ClassesTab::instance() {
    static ClassesTab t;
    return t;
}

ClassesTab::State* ClassesTab::state() {
    static State s;
    return &s;
}

// ═══════════════════════════════════════════════════════════
//  JNI helpers
// ═══════════════════════════════════════════════════════════
namespace ClassesTab_jni {
    // from ClassesTab_jni.cpp
    void startEnum();
    bool isEnumerating();
    bool isComplete();
    int  getTotal();
    std::vector<std::string> nextBatch(int n);
    std::vector<std::string> getMembers(const std::string& className);
}

// ═══════════════════════════════════════════════════════════
//  draw
// ═══════════════════════════════════════════════════════════
void ClassesTab::draw() {
    auto* s = state();

    // ─── Controls ───
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));

    if (!s->enumerating && !s->complete) {
        if (ImGui::Button("▶ Start Enumeration", ImVec2(200, 32))) {
            startClassEnum();
        }
    } else if (s->enumerating) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.3f, 0.1f, 1));
        ImGui::Button("⏳ Enumerating...", ImVec2(200, 32));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        int total = ClassesTab_jni::getTotal();
        ImGui::Text("Total: %d", total);
    } else {
        int total = ClassesTab_jni::getTotal();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "✓ %d classes", total);
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(100, 32))) {
            startClassEnum();
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("##search", "search classes...",
        s->searchBuf, sizeof(s->searchBuf));

    // Drain batches
    if (s->enumerating) {
        auto batch = ClassesTab_jni::nextBatch(500);
        if (!batch.empty()) {
            std::lock_guard<std::mutex> lk(s->classesMu);
            for (auto& n : batch) s->classes.push_back(std::move(n));
        }
        s->complete = ClassesTab_jni::isComplete();
    }

    ImGui::PopStyleVar();
    ImGui::Separator();

    // ─── Layout: class list (left) + members (right) ───
    float leftW = ImGui::GetContentRegionAvail().x * 0.4f;

    // Filter classes
    std::string q = s->searchBuf;
    for (auto& c : q) c = (char)std::tolower((unsigned char)c);

    std::vector<size_t> visible;
    {
        std::lock_guard<std::mutex> lk(s->classesMu);
        visible.reserve(s->classes.size());
        for (size_t i = 0; i < s->classes.size(); i++) {
            if (!q.empty()) {
                std::string sl = s->classes[i];
                for (auto& c : sl) c = (char)std::tolower((unsigned char)c);
                if (sl.find(q) == std::string::npos) continue;
            }
            visible.push_back(i);
        }
    }

    ImGui::BeginChild("##classes", ImVec2(leftW, 0), true);
    ImGui::Text("Classes (%zu)", visible.size());
    ImGui::Separator();

    ImGuiListClipper clip;
    clip.Begin((int)visible.size(), 18.0f);
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            size_t idx = visible[k];
            std::string name;
            {
                std::lock_guard<std::mutex> lk(s->classesMu);
                if (idx >= s->classes.size()) continue;
                name = s->classes[idx];
            }
            bool sel = (s->selectedClass == name);
            if (ImGui::Selectable(name.c_str(), sel)) {
                s->selectedClass = name;
                s->selectedMembers = ClassesTab_jni::getMembers(name);
                s->selectedMemberIdx = -1;
                LOGI("selected class: %s (%zu members)",
                     name.c_str(), s->selectedMembers.size());
            }
        }
    }
    clip.End();
    ImGui::EndChild();

    ImGui::SameLine();

    // ─── Members (right) ───
    ImGui::BeginChild("##members", ImVec2(0, 0), true);
    if (s->selectedClass.empty()) {
        ImGui::TextDisabled("(select a class on the left)");
    } else {
        ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1), "%s",
            s->selectedClass.c_str());
        ImGui::Separator();
        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##msearch", "search members...",
            s->memberSearchBuf, sizeof(s->memberSearchBuf));

        std::string mq = s->memberSearchBuf;
        for (auto& c : mq) c = (char)std::tolower((unsigned char)c);

        std::vector<size_t> mVisible;
        for (size_t i = 0; i < s->selectedMembers.size(); i++) {
            if (!mq.empty()) {
                std::string sl = s->selectedMembers[i];
                for (auto& c : sl) c = (char)std::tolower((unsigned char)c);
                if (sl.find(mq) == std::string::npos) continue;
            }
            mVisible.push_back(i);
        }

        ImGui::Separator();
        ImGuiListClipper mclip;
        mclip.Begin((int)mVisible.size(), 20.0f);
        while (mclip.Step()) {
            for (int k = mclip.DisplayStart; k < mclip.DisplayEnd; k++) {
                size_t idx = mVisible[k];
                const auto& m = s->selectedMembers[idx];
                bool sel = (s->selectedMemberIdx == (int)idx);

                if (m[0] == 'M') ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.85f, 0.6f, 1));
                else              ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.85f, 1));
                if (ImGui::Selectable(m.c_str(), sel)) {
                    s->selectedMemberIdx = (int)idx;
                }
                ImGui::PopStyleColor();

                // hover actions
                if (ImGui::IsItemHovered()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Trace")) {
                        // Transition to TraceTab with member selected
                        auto& cfg = oceanUI().config;
                        cfg.mode = 2;
                        std::string spec = buildMemberSpec(s->selectedClass, m);
                        strncpy(cfg.memberSpec, spec.c_str(), sizeof(cfg.memberSpec) - 1);
                        cfg.memberSpec[sizeof(cfg.memberSpec) - 1] = '\0';
                        oceanUI().currentTab = 0;  // Trace tab → Config sub-tab
                        Notifications::instance().push(
                            ("Target set: " + spec).c_str(), 3.0f);
                    }
                }
            }
        }
        mclip.End();
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════
//  Public
// ═══════════════════════════════════════════════════════════
void ClassesTab::startClassEnum() {
    auto* s = state();
    {
        std::lock_guard<std::mutex> lk(s->classesMu);
        s->classes.clear();
    }
    s->complete = false;
    s->enumerating = true;
    ClassesTab_jni::startEnum();
    LOGI("Class enumeration started");
}

void ClassesTab::onStartClicked() {
    startClassEnum();
}

} // namespace yambz
