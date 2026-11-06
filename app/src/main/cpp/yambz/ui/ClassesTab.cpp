#include "ClassesTab.h"
#include <vector>
#include "SearchBar.h"
#include "Theme.h"
#include "ClassesState.h"
#include "../core/Favorites.h"
#include "../core/ClassEnumerator.h"
#include "../core/SearchEngine.h"
#include "../core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <unordered_set>
#include <chrono>
#include <memory>

#include <jni.h>
#include "core/ClassTypes.h"

namespace yambz {

ClassesTab& ClassesTab::instance() {
    static ClassesTab t;
    return t;
}

// ─── state ───
struct TabState {
    std::unordered_set<uint32_t> expanded;
    uint32_t hoveredIdx = UINT32_MAX;
    uint32_t selectedIdx = UINT32_MAX;
    double lastSearchAt = 0.0;
    size_t totalResults = 0;
};
static TabState g_tab;

// ─── debounce search ───
static void doSearchIfNeeded() {
    auto& st = ClassesState::instance();
    double now = ImGui::GetTime();
    if (now - g_tab.lastSearchAt < 0.12) return;
    if (st.searching) return;

    g_tab.lastSearchAt = now;
    auto classes = ClassEnumerator::instance().snapshot();
    if (!classes) return;

    auto results = std::make_shared<std::vector<SearchEngine::Result>>(
        SearchEngine::search(*classes, st.filter, 5000));
    st.results = results;
    g_tab.totalResults = results->size();
}

// ─── row rendering ───
static void drawClassRow(const ClassInfo& c, uint32_t idx) {
    ImGui::PushID((int)idx);
    bool isExpanded = g_tab.expanded.count(idx) > 0;
    bool isSelected = (g_tab.selectedIdx == idx);

    // arrow
    ImGui::PushStyleColor(ImGuiCol_Text,
        isExpanded ? ImVec4(1.0f, 0.83f, 0.30f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    if (ImGui::ArrowButton("##expand", isExpanded ? ImGuiDir_Down : ImGuiDir_Right)) {
        if (isExpanded) g_tab.expanded.erase(idx);
        else            g_tab.expanded.insert(idx);
    }
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 8);

    // package (dim) + simple name (bright)
    if (!c.package.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
        ImGui::TextUnformatted((c.package + ".").c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
    }

    ImVec4 nameCol = isSelected ? ImVec4(1.0f, 0.83f, 0.30f, 1.0f)
                                : ImVec4(0.95f, 0.95f, 0.97f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, nameCol);
    ImGui::TextUnformatted(c.simpleName.c_str());
    ImGui::PopStyleColor();

    // selection
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        g_tab.selectedIdx = isSelected ? UINT32_MAX : idx;
    }
    if (ImGui::IsItemHovered()) {
        g_tab.hoveredIdx = idx;
        ImGui::SetTooltip("%s", c.fullName.c_str());
    }

    // favorite star (always visible if marked, hover otherwise)
    ImGui::SameLine(ImGui::GetWindowWidth() - 290);
    {
        bool fav = Favorites::instance().isFavorite(c.fullName);
        ImGui::PushStyleColor(ImGuiCol_Text, fav ?
            ImVec4(1.0f, 0.83f, 0.30f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        if (ImGui::SmallButton(fav ? "★" : "☆")) {
            if (fav) {
                Favorites::instance().remove(c.fullName);
            } else {
                FavoriteItem fi;
                fi.kind = "method";
                fi.className = c.fullName;
                fi.methodName = "";
                fi.methodDesc = c.fullName;
                fi.addedAt = (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                Favorites::instance().add(fi);
            }
        }
        ImGui::PopStyleColor();
    }

    // hover actions: Trace | Call | Details
    if (g_tab.hoveredIdx == idx) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 260);
        if (ImGui::SmallButton("Trace")) {
            extern void yambz_openTraceForClass(uint32_t);
            yambz_openTraceForClass(idx);
        }
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton("Call")) {
            extern void yambz_openCallFor(uint32_t);
            yambz_openCallFor(idx);
        }
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton("Info")) {
            extern void yambz_openDetail(uint32_t);
            yambz_openDetail(idx);
        }
    }

    // expanded: render members
    if (isExpanded) {
        ImGui::Indent(28);
        try {
            const auto& members = ClassEnumerator::instance().membersOf(idx);
            if (members.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted("(no members)");
                ImGui::PopStyleColor();
            } else {
                for (size_t mi = 0; mi < members.size(); mi++) {
                    const auto& m = members[mi];
                    const char* kindStr =
                        m.kind == MemberKind::Constructor ? "⌬" :
                        m.kind == MemberKind::Method ? "ƒ" : "▪";
                    const char* access =
                        (m.modifiers & 0x01) ? "public" :
                        (m.modifiers & 0x02) ? "private" :
                        (m.modifiers & 0x04) ? "protected" : "pkg";
                    const char* stat = (m.modifiers & 0x08) ? "static" : "";

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
                    ImGui::TextUnformatted(kindStr);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 6);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::TextUnformatted(access);
                    ImGui::PopStyleColor();
                    if (stat[0]) {
                        ImGui::SameLine(0, 6);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                        ImGui::TextUnformatted(stat);
                        ImGui::PopStyleColor();
                    }
                    ImGui::SameLine(0, 8);
                    ImGui::TextUnformatted(m.name.c_str());
                    ImGui::SameLine(0, 0);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
                    ImGui::TextUnformatted(m.signature.c_str());
                    ImGui::PopStyleColor();

                    if (m.kind != MemberKind::Field) {
                        ImGui::SameLine(0, 8);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.75f, 0.5f, 1.0f));
                        ImGui::TextUnformatted("→");
                        ImGui::PopStyleColor();
                        ImGui::SameLine(0, 4);
                        ImGui::TextUnformatted(m.returnType.c_str());
                    }
                }
            }
        } catch (const std::exception& e) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "(members load failed: %s)", e.what());
        }
        ImGui::Unindent(28);
    }

    ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════
//  main draw
// ═══════════════════════════════════════════════════════════
void ClassesTab::draw() {
    auto& st = ClassesState::instance();

    // ─── SearchBar ───
    if (SearchBar::draw(st.filter)) {
        st.results.reset();
        g_tab.lastSearchAt = 0.0;
    }

    ImGui::Spacing();

    // ─── progress / status ───
    auto& enumr = ClassEnumerator::instance();
    if (enumr.isRunning()) {
        float prog = enumr.progress();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
        ImGui::ProgressBar(prog, ImVec2(-1, 8), "");
        ImGui::PopStyleColor();
        ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1.0f),
            "⏳ enumerating... %zu classes", enumr.count());
    } else if (enumr.isComplete()) {
        ImGui::TextColored(ImVec4(0.5f, 0.75f, 0.5f, 1.0f),
            "✓ %zu classes", enumr.count());
        ImGui::SameLine();
        if (g_tab.totalResults > 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "· %zu results", g_tab.totalResults);
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "idle — press start");
        ImGui::SameLine();
        if (ImGui::Button("Start enumeration")) {
            // نطلب من C++ البدء — من نفس الـ thread عبر JNI من PanelBridge
            // (نتجاوز: نستخدم PanelBridge class methods)
            // الطريقة الأنظف: ندعو من Java. لكن للتبسيط، نستخدم helper:
            extern void yambz_startEnumFromUI();
            yambz_startEnumFromUI();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ─── search (debounced) ───
    doSearchIfNeeded();

    // ─── favorites section ───
    {
        auto favs = Favorites::instance().list();
        if (!favs.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.83f, 0.30f, 1.0f));
            ImGui::Text("★ Favorites (%zu)", favs.size());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("collapse##fav")) {
                // toggle
                static bool collapsed = false;
                collapsed = !collapsed;
            }
            ImGui::Separator();
            for (const auto& f : favs) {
                ImGui::PushID(f.methodDesc.c_str());
                ImGui::TextColored(ImVec4(1.0f, 0.83f, 0.30f, 1.0f), "★");
                ImGui::SameLine();
                if (ImGui::Selectable(f.className.c_str())) {
                    // اعثر على classIdx
                    // (خارج النطاق — يحتاج بحث)
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    // ─── results list ───
    if (!st.results || st.results->empty()) {
        if (!enumr.isRunning()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "اكتب للبحث أو اضغط Start enumeration");
        }
        return;
    }

    auto classes = enumr.snapshot();
    if (!classes) return;

    // Virtual scrolling — ImGuiListClipper
    ImGui::BeginChild("##resultList", ImVec2(0, 0), false);
    ImGuiListClipper clipper;
    clipper.Begin((int)st.results->size(), 28.0f);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const auto& r = (*st.results)[i];
            if (r.classIdx >= classes->size()) continue;
            drawClassRow((*classes)[r.classIdx], r.classIdx);
        }
    }
    clipper.End();
    ImGui::EndChild();
}

// ─── helper extern (يُربط في jni_class.cpp) ───
extern void yambz_startEnumFromUI() {
    JavaVM* vm = nullptr;
    // نحصل على VM من الـ thread الحالي
    JNIEnv* env = nullptr;
    // (يفترض أن JNI_OnLoad قد حفظ g_vm في مكان ما — سنستخدم وظيفة مساعدة)
    // البديل: نطلب من C++ تخزين VM statically
    extern JavaVM* yambz_getGlobalVM();
    vm = yambz_getGlobalVM();
    if (!vm) return;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    ClassEnumerator::instance().start(env);
}

} // namespace yambz
