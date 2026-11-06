#include "OceanTracePanel.h"
#include "TraceStore.h"
#include "../core/Helpers.h"
#include "../core/Logger.h"
#include "../ui/Theme.h"
#include "../ui/Notifications.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <cstring>
#include <sstream>

namespace yambz {

// ═══════════════════════════════════════════════════════════
//  State
// ═══════════════════════════════════════════════════════════
OceanTraceUIState& oceanTraceUI() {
    static OceanTraceUIState s;
    return s;
}

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ═══════════════════════════════════════════════════════════
//  Push event (من JNI)
// ═══════════════════════════════════════════════════════════
void oceanTracePushEvent(int kind, int depth,
                          const std::string& method,
                          const std::string& extra) {
    auto& ui = oceanTraceUI();
    OceanTraceUIState::Event ev;
    ev.ts = (uint64_t) nowMs();
    ev.kind = kind;
    ev.depth = depth;
    ev.method = method;
    ev.extra = extra;

    ui.events.push_back(std::move(ev));
    while (ui.events.size() > ui.eventsCapacity) {
        ui.events.erase(ui.events.begin());
    }

    // Update counts
    switch (kind) {
        case 1: ui.callCount++; break;
        case 2: ui.returnCount++; break;
        case 3: ui.execCount++; break;
        case 4: ui.blockCount++; break;
        case 5: ui.fieldReadCount++; break;
        case 6: ui.fieldWriteCount++; break;
    }
    ui.eventCount++;
}

// ═══════════════════════════════════════════════════════════
//  Start / Stop
// ═══════════════════════════════════════════════════════════
void oceanTraceStart() {
    auto& ui = oceanTraceUI();

    if (ui.tracing) {
        LOGW("OceanTrace: already tracing");
        return;
    }

    // نادِ Java bridge للبدء
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) {
        LOGE("OceanTrace: no JavaVM");
        return;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    // نحصل على OceanTraceController (ستُضاف في الـ block 4)
    jclass cls = env->FindClass("com/yambz/bridge/OceanTraceController");
    if (!cls) {
        env->ExceptionClear();
        LOGE("OceanTraceController not found");
        return;
    }

    jmethodID mid_start = env->GetStaticMethodID(cls, "startTrace",
        "(IIIIIILjava/lang/String;IZ)Z");
    if (!mid_start) {
        env->ExceptionClear();
        LOGE("startTrace method not found");
        return;
    }

    int eventsMask = 0;
    if (ui.config.eventCallEnter) eventsMask |= 0x100;  // CALL_ENTER
    if (ui.config.eventCall)      eventsMask |= 0x02;   // CALL
    if (ui.config.eventReturn)    eventsMask |= 0x10;   // RETURN
    if (ui.config.eventExec)      eventsMask |= 0x01;   // EXEC
    if (ui.config.eventBlock)     eventsMask |= 0x08;   // BLOCK
    if (ui.config.eventFileRead)  eventsMask |= 0x20;   // FILE_READ
    if (ui.config.eventFieldWrite) eventsMask |= 0x40;  // FIELD_WRITE

    jstring jpkg = env->NewStringUTF(ui.config.packageFilter.c_str());
    jstring jkey = env->NewStringUTF(ui.config.sinkerKeyword.c_str());

    jboolean ok = env->CallStaticBooleanMethod(cls, mid_start,
        eventsMask,
        ui.config.scope,
        ui.config.timeoutMs,
        ui.config.maxEvents,
        ui.config.sinkerKind,
        ui.config.onlyMainThread ? 1 : 0,
        jpkg,
        0,  // methodPtr (0 = global)
        JNI_TRUE);

    if (jpkg) env->DeleteLocalRef(jpkg);
    if (jkey) env->DeleteLocalRef(jkey);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return;
    }

    if (ok) {
        ui.tracing = true;
        ui.events.clear();
        ui.eventCount = 0;
        ui.callCount = 0;
        ui.returnCount = 0;
        ui.execCount = 0;
        ui.blockCount = 0;
        ui.fieldReadCount = 0;
        ui.fieldWriteCount = 0;
        Notifications::instance().push("Ocean Trace started", 2.0f);
        LOGI("OceanTrace: started");
    } else {
        LOGE("OceanTrace: startTrace returned false");
    }
}

void oceanTraceStop() {
    auto& ui = oceanTraceUI();
    if (!ui.tracing) return;

    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls = env->FindClass("com/yambz/bridge/OceanTraceController");
    if (cls) {
        jmethodID mid = env->GetStaticMethodID(cls, "stopTrace", "()V");
        if (mid) env->CallStaticVoidMethod(cls, mid);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    ui.tracing = false;
    Notifications::instance().push("Ocean Trace stopped", 2.0f);
    LOGI("OceanTrace: stopped (events=%ld)", ui.eventCount);
}

// ═══════════════════════════════════════════════════════════
//  Draw Panel
// ═══════════════════════════════════════════════════════════
void drawOceanTracePanel() {
    auto& ui = oceanTraceUI();
    if (!ui.show) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.94f, io.DisplaySize.y * 0.88f),
                              ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.03f, io.DisplaySize.y * 0.06f),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.97f);

    bool open = true;
    ImGui::Begin("Ocean Trace ##panel", &open,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    if (!open) { ui.show = false; ImGui::End(); return; }

    // ─── header ───
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::Text("Ocean Tracker — %s",
        ui.tracing ? "TRACING" : "IDLE");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(events: %ld | call: %ld | ret: %ld | exec: %ld | fieldR: %ld | fieldW: %ld)",
        ui.eventCount, ui.callCount, ui.returnCount, ui.execCount,
        ui.fieldReadCount, ui.fieldWriteCount);

    // ─── start/stop ───
    if (!ui.tracing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
        if (ImGui::Button("▶ Start Trace", ImVec2(150, 0))) {
            oceanTraceStart();
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("■ Stop Trace", ImVec2(150, 0))) {
            oceanTraceStop();
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        ui.events.clear();
        ui.selectedEvent = -1;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &ui.autoScroll);

    ImGui::Separator();

    // ─── Config (left pane) ───
    float configW = 320.0f;
    ImGui::BeginChild("##config", ImVec2(configW, 0), true);

    Theme::drawSectionHeader("Events");
    ImGui::Checkbox("CALL_ENTER", &ui.config.eventCallEnter);
    ImGui::Checkbox("CALL", &ui.config.eventCall);
    ImGui::Checkbox("RETURN", &ui.config.eventReturn);
    ImGui::Checkbox("EXEC (heavy)", &ui.config.eventExec);
    ImGui::Checkbox("BLOCK", &ui.config.eventBlock);
    ImGui::Checkbox("FILE_READ", &ui.config.eventFileRead);
    ImGui::Checkbox("FIELD_WRITE", &ui.config.eventFieldWrite);

    Theme::drawSectionHeader("Scope");
    int scope = ui.config.scope;
    ImGui::RadioButton("JDK", &scope, 1);       ImGui::SameLine();
    ImGui::RadioButton("Platform", &scope, 2);  ImGui::SameLine();
    ImGui::RadioButton("App", &scope, 4);
    ImGui::RadioButton("PackageFilter", &scope, 8); ImGui::SameLine();
    ImGui::RadioButton("EntryOnly", &scope, 16);
    ui.config.scope = scope;

    if ((scope & 8) != 0) {
        char buf[256];
        strncpy(buf, ui.config.packageFilter.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##pkgfilter", buf, sizeof(buf))) {
            ui.config.packageFilter = buf;
        }
    }

    Theme::drawSectionHeader("Limits");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragInt("Timeout (ms)", &ui.config.timeoutMs, 100, 1000, 600000);
    ImGui::SetNextItemWidth(-1);
    ImGui::DragInt("Max events", &ui.config.maxEvents, 1000, 1000, 1000000);

    Theme::drawSectionHeader("Threads");
    ImGui::Checkbox("Only main thread", &ui.config.onlyMainThread);

    Theme::drawSectionHeader("Sinker");
    int kind = ui.config.sinkerKind;
    ImGui::RadioButton("Default", &kind, 0);    ImGui::SameLine();
    ImGui::RadioButton("Empty", &kind, 3);
    ImGui::RadioButton("Keyword", &kind, 1);    ImGui::SameLine();
    ImGui::RadioButton("String", &kind, 2);
    ui.config.sinkerKind = kind;
    if (kind == 1 || kind == 2) {
        char buf[256];
        strncpy(buf, ui.config.sinkerKeyword.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##keyword", buf, sizeof(buf))) {
            ui.config.sinkerKeyword = buf;
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // ─── Events list (right pane) ───
    ImGui::BeginChild("##events", ImVec2(0, 0), true);

    Theme::drawSectionHeader("Events");
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##search", "search events...", ui.searchFilter, sizeof(ui.searchFilter));

    // filter events
    std::string q_lower = ui.searchFilter;
    for (auto& c : q_lower) c = (char) std::tolower((unsigned char) c);

    std::vector<size_t> visible;
    visible.reserve(ui.events.size());
    for (size_t i = 0; i < ui.events.size(); i++) {
        const auto& ev = ui.events[i];
        if (!q_lower.empty()) {
            std::string hay = ev.method + " " + ev.extra;
            for (auto& c : hay) c = (char) std::tolower((unsigned char) c);
            if (hay.find(q_lower) == std::string::npos) continue;
        }
        visible.push_back(i);
    }

    // virtual scrolling
    ImGui::BeginChild("##eventList", ImVec2(0, ImGui::GetContentRegionAvail().y - 150), false);
    ImGuiListClipper clip;
    clip.Begin((int) visible.size(), 20.0f);
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            size_t idx = visible[k];
            const auto& ev = ui.events[idx];

            ImGui::PushID((int) idx);

            // color by kind
            ImVec4 col;
            const char* prefix;
            switch (ev.kind) {
                case 1: col = ImVec4(0.4f, 0.8f, 0.4f, 1); prefix = "▶ CALL "; break;
                case 2: col = ImVec4(0.4f, 0.6f, 0.9f, 1); prefix = "◀ RET  "; break;
                case 3: col = ImVec4(0.7f, 0.7f, 0.4f, 1); prefix = "· EXEC "; break;
                case 4: col = ImVec4(0.9f, 0.6f, 0.3f, 1); prefix = "▸ BLK  "; break;
                case 5: col = ImVec4(0.9f, 0.4f, 0.8f, 1); prefix = "R FLD  "; break;
                case 6: col = ImVec4(0.9f, 0.3f, 0.4f, 1); prefix = "W FLD  "; break;
                default: col = ImVec4(0.6f, 0.6f, 0.6f, 1); prefix = "?      ";
            }

            // indent by depth
            for (int d = 0; d < ev.depth && d < 20; d++) ImGui::Indent(8.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(prefix);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Selectable(ev.method.c_str(), ui.selectedEvent == (int) idx)) {
                ui.selectedEvent = (int) idx;
            }
            if (!ev.extra.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("— %s", ev.extra.c_str());
            }

            for (int d = 0; d < ev.depth && d < 20; d++) ImGui::Unindent(8.0f);

            ImGui::PopID();
        }
    }
    clip.End();
    ImGui::EndChild();

    // ─── detail pane ───
    Theme::drawSectionHeader("Detail");
    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    if (ui.selectedEvent >= 0 && ui.selectedEvent < (int) ui.events.size()) {
        const auto& ev = ui.events[ui.selectedEvent];
        ImGui::Text("Timestamp: %llu ms", (unsigned long long) ev.ts);
        ImGui::Text("Kind: %d | Depth: %d", ev.kind, ev.depth);
        ImGui::TextWrapped("Method: %s", ev.method.c_str());
        if (!ev.extra.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", ev.extra.c_str());
        }
    } else {
        ImGui::TextDisabled("(select an event)");
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::End();
}

} // namespace yambz
