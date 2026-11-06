#include "DetailView.h"
#include "Theme.h"
#include "../core/ClassEnumerator.h"
#include <imgui.h>

namespace yambz {

DetailViewState& detailViewState() {
    static DetailViewState s;
    return s;
}

void drawDetailView() {
    auto& st = detailViewState();
    if (!st.show) return;

    auto classes = ClassEnumerator::instance().snapshot();
    if (!classes || st.classIdx >= classes->size()) {
        st.show = false;
        return;
    }
    const auto& c = (*classes)[st.classIdx];

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Appearing);
    bool open = true;
    ImGui::Begin(("Class: " + c.simpleName).c_str(), &open,
                 ImGuiWindowFlags_NoCollapse);
    if (!open) { st.show = false; ImGui::End(); return; }

    Theme::drawSectionHeader("Full name");
    ImGui::TextWrapped("%s", c.fullName.c_str());

    Theme::drawSectionHeader("Package");
    ImGui::TextUnformatted(c.package.c_str());

    Theme::drawSectionHeader("Members");
    const auto& members = ClassEnumerator::instance().membersOf(st.classIdx);
    if (members.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(none)");
    } else {
        for (const auto& m : members) {
            const char* kindStr =
                m.kind == MemberKind::Constructor ? "⌬" :
                m.kind == MemberKind::Method ? "ƒ" : "▪";
            ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1.0f), "%s", kindStr);
            ImGui::SameLine();
            ImGui::Text("%s%s%s",
                m.name.c_str(),
                m.signature.c_str(),
                m.returnType.empty() ? "" : (" → " + m.returnType).c_str());
        }
    }

    ImGui::End();
}

} // namespace yambz
