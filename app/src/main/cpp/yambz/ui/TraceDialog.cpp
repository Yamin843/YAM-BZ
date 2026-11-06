#include "TraceDialog.h"
#include <vector>
#include <string>
#include "Theme.h"
#include "Notifications.h"
#include "../trace/TraceStore.h"
#include "../trace/TraceEngine.h"
#include "../trace/TraceUI.h"
#include "../trace/DeepBacktrace.h"
#include "../call/TracePresets.h"
#include "../core/ErrorContext.h"
#include "../core/Logger.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>
#include <chrono>
#include <cstring>

namespace yambz {

static char s_query[512] = {0};
static bool s_showOnlyErr = false;
static bool s_showArgs = true;
static bool s_showBacktrace = true;
static bool s_deepBacktrace = false;
static bool s_followTail = true;
static int  s_selectedEvent = -1;
static int  s_lastEventCount = 0;
static float s_scroll = 0;

// ─── helpers ───
static std::string fmtParam(const ParamValue& p) {
    switch (p.kind) {
        case ParamValue::Kind::Null:   return "null";
        case ParamValue::Kind::Int:    return std::to_string(p.i64);
        case ParamValue::Kind::Long:   return std::to_string(p.i64);
        case ParamValue::Kind::Float:  { char b[32]; snprintf(b,sizeof b,"%.6g",(float)p.f64); return b; }
        case ParamValue::Kind::Double: { char b[32]; snprintf(b,sizeof b,"%.9g",p.f64); return b; }
        case ParamValue::Kind::Bool:   return p.b ? "true" : "false";
        case ParamValue::Kind::String:
        case ParamValue::Kind::Object: return p.str;
    }
    return "?";
}

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void copyToClipboard(const std::string& text) {
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls_CB = env->FindClass("com/yambz/bridge/ClipboardBridge");
    if (!cls_CB) { env->ExceptionClear(); return; }
    jmethodID mid = env->GetStaticMethodID(cls_CB, "copyToClipboard",
        "(Ljava/lang/String;)V");
    if (!mid) { env->ExceptionClear(); return; }
    jstring js = env->NewStringUTF(text.c_str());
    env->CallStaticVoidMethod(cls_CB, mid, js);
    env->DeleteLocalRef(js);
}

// ─── export event as text ───
static std::string eventToText(const TraceEvent& e) {
    std::stringstream ss;
    ss << (e.completed ? "◀ " : "▶ ");
    ss << e.methodDesc;
    if (e.threw) ss << " [threw]";
    ss << "\n";
    if (e.hasThis) {
        ss << "  this = " << e.thisDump << "\n";
    }
    for (size_t i = 0; i < e.args.size(); i++) {
        ss << "  arg[" << i << "] = " << fmtParam(e.args[i]) << "\n";
    }
    if (!e.backtrace.empty()) {
        ss << "  backtrace:\n";
        for (size_t i = 0; i < e.backtrace.size(); i++) {
            const auto& f = e.backtrace[i];
            ss << "    #" << i << " " << f.className << "." << f.methodName;
            if (f.lineNumber > 0) ss << " (" << f.fileName << ":" << f.lineNumber << ")";
            ss << "\n";
        }
    }
    if (e.completed) {
        if (e.threw) ss << "  THREW: " << e.throwableDump << "\n";
        else ss << "  result = " << fmtParam(e.result) << "\n";
    }
    return ss.str();
}

// ═══════════════════════════════════════════════════════════
//  draw
// ═══════════════════════════════════════════════════════════
void TraceDialog::draw() {
    auto& ui = traceUI();
    if (!ui.showDialog) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.94f, io.DisplaySize.y * 0.88f),
                              ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.03f, io.DisplaySize.y * 0.06f),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.97f);

    bool open = true;
    ImGui::Begin("Trace ##main", &open,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    if (!open) { ui.showDialog = false; ImGui::End(); return; }

    // ─── top bar ───
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextWrapped("%s", ui.activeMethodDesc.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(session #%u, %zu events, dropped %zu, %llu/ms)",
        ui.activeSessionId,
        TraceStore::instance().count(),
        TraceStore::instance().dropped(),
        (unsigned long long) nowMs());

    // ─── action buttons ───
    if (ImGui::Button("Stop")) {
        extern void yambz_stopTrace();
        yambz_stopTrace();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) TraceStore::instance().clear();
    ImGui::SameLine();
    if (ImGui::Button("Pause")) {
        TraceStore::instance().setRecording(!TraceStore::instance().recording());
    }
    ImGui::SameLine();
    ImGui::TextDisabled(TraceStore::instance().recording() ? "(recording)" : "(PAUSED)");

    ImGui::SameLine();
    if (ImGui::Button("Save preset...")) {
        ImGui::OpenPopup("save-preset");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load preset...")) {
        ImGui::OpenPopup("load-preset");
    }

    // save preset popup
    if (ImGui::BeginPopup("save-preset")) {
        static char s_name[128] = {0};
        ImGui::InputText("Name", s_name, sizeof s_name);
        if (ImGui::Button("Save") && s_name[0]) {
            extern void yambz_saveTracePreset(const char* name);
            yambz_saveTracePreset(s_name);
            s_name[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // load preset popup
    if (ImGui::BeginPopup("load-preset")) {
        auto presets = TracePresets::instance().listAll();
        if (presets.empty()) {
            ImGui::TextDisabled("(no presets)");
        } else {
            for (const auto& p : presets) {
                if (ImGui::Selectable(p.name.c_str())) {
                    extern void yambz_loadTracePreset(const char* name);
                    yambz_loadTracePreset(p.name.c_str());
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    // ─── deep backtrace toggle ───
    if (ImGui::Checkbox("Deep backtrace (Stalker)", &s_deepBacktrace)) {
        if (s_deepBacktrace) {
            extern void yambz_startDeepBacktrace(int ms, int max);
            yambz_startDeepBacktrace(2000, 500);
            Notifications::instance().push("DeepBacktrace: collecting for 2s...", 2.0f);
        }
    }
    if (s_deepBacktrace) {
        auto frames = DeepBacktrace::instance().getFrames();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1.0f),
            "(%zu frames)", frames.size());
    }

    ImGui::Separator();

    // ─── filters ───
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##q", "search in events (method, target, args)", s_query, sizeof s_query);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &s_showOnlyErr);
    ImGui::SameLine();
    ImGui::Checkbox("Args", &s_showArgs);
    ImGui::SameLine();
    ImGui::Checkbox("Backtrace", &s_showBacktrace);
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &s_followTail);

    ImGui::Separator();

    // ─── events snapshot ───
    auto events = TraceStore::instance().snapshot();
    std::string q_lower = s_query;
    for (auto& c : q_lower) c = (char) std::tolower((unsigned char)c);

    std::vector<size_t> visible;
    visible.reserve(events.size());
    for (size_t i = 0; i < events.size(); i++) {
        const auto& e = events[i];
        if (s_showOnlyErr && !e.threw && !e.isError) continue;
        if (!q_lower.empty()) {
            auto lower = [](std::string s) {
                for (auto& c : s) c = (char) std::tolower((unsigned char)c);
                return s;
            };
            std::string hay = lower(e.methodDesc + " " + e.thisDump);
            for (const auto& p : e.args) hay += " " + lower(fmtParam(p));
            if (hay.find(q_lower) == std::string::npos) continue;
        }
        visible.push_back(i);
    }

    // ─── split view ───
    float listW = ImGui::GetContentRegionAvail().x * 0.42f;
    ImGui::BeginChild("##list", ImVec2(listW, 0), true);
    {
        ImGuiListClipper clip;
        clip.Begin((int) visible.size(), 20.0f);
        while (clip.Step()) {
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                size_t idx = visible[k];
                const auto& e = events[idx];
                char ts[32]; snprintf(ts, sizeof ts, "%.2f", e.timestampMs / 1000.0);

                bool isSel = (s_selectedEvent == (int) idx);
                ImGui::PushID((int) idx);

                if (e.isError) {
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "✖ %s", ts);
                } else if (e.threw) {
                    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "▶ %s", ts);
                } else if (e.completed) {
                    ImGui::TextColored(ImVec4(0.5f, 0.75f, 0.5f, 1), "◀ %s", ts);
                } else {
                    ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1), "▶ %s", ts);
                }
                ImGui::SameLine(0, 6);

                if (isSel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.83f, 0.30f, 1.0f));
                std::string preview = e.methodDesc;
                if (preview.length() > 60) preview = preview.substr(0, 60) + "…";
                if (ImGui::Selectable(preview.c_str(), isSel)) {
                    s_selectedEvent = (int) idx;
                }
                if (isSel) ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        clip.End();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ─── detail pane ───
    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    if (s_selectedEvent >= 0 && s_selectedEvent < (int) events.size()) {
        const auto& e = events[s_selectedEvent];

        // copy buttons
        if (ImGui::Button("Copy text")) {
            copyToClipboard(eventToText(e));
            Notifications::instance().push("Copied event to clipboard", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy JSON")) {
            std::stringstream js;
            js << "{\"eventId\":" << e.eventId
               << ",\"ts\":" << e.timestampMs
               << ",\"method\":\"" << e.methodDesc << "\""
               << ",\"completed\":" << (e.completed ? "true" : "false")
               << ",\"threw\":" << (e.threw ? "true" : "false")
               << ",\"result\":\"" << fmtParam(e.result) << "\"}";
            copyToClipboard(js.str());
            Notifications::instance().push("Copied JSON", 2.0f);
        }

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
        ImGui::Text("Event #%u", e.eventId);
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", e.methodDesc.c_str());
        ImGui::Separator();

        if (e.hasThis) {
            Theme::drawSectionHeader("this");
            ImGui::TextWrapped("%s", e.thisDump.c_str());
        }

        if (!e.args.empty() && s_showArgs) {
            Theme::drawSectionHeader("Arguments");
            for (size_t i = 0; i < e.args.size(); i++) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
                ImGui::Text("[%zu]", i);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", fmtParam(e.args[i]).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(("copy##a" + std::to_string(i)).c_str())) {
                    copyToClipboard(fmtParam(e.args[i]));
                }
            }
        }

        if (!e.backtrace.empty() && s_showBacktrace) {
            Theme::drawSectionHeader("Backtrace");
            for (size_t i = 0; i < e.backtrace.size(); i++) {
                const auto& f = e.backtrace[i];
                ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1.0f), "#%zu ", i);
                ImGui::SameLine();
                ImGui::Text("%s.%s", f.className.c_str(), f.methodName.c_str());
                if (f.lineNumber > 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "%s:%d", f.fileName.c_str(), f.lineNumber);
                }
            }
        }

        // deep backtrace section
        if (s_deepBacktrace) {
            auto frames = DeepBacktrace::instance().getFrames();
            if (!frames.empty()) {
                Theme::drawSectionHeader("Deep Backtrace (Stalker)");
                for (size_t i = 0; i < frames.size() && i < 50; i++) {
                    const auto& f = frames[i];
                    ImGui::Text("0x%llx → 0x%llx (d=%d)",
                        (unsigned long long) f.location,
                        (unsigned long long) f.target,
                        f.depth);
                }
                if (frames.size() > 50) {
                    ImGui::TextDisabled("... %zu more frames", frames.size() - 50);
                }
            }
        }

        if (e.completed) {
            Theme::drawSectionHeader("Result");
            if (e.threw) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1.0f),
                    "THREW: %s", e.throwableDump.c_str());
            } else {
                ImGui::TextWrapped("%s", fmtParam(e.result).c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("copy##res")) {
                copyToClipboard(e.threw ? e.throwableDump : fmtParam(e.result));
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "select an event to inspect");
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace yambz
