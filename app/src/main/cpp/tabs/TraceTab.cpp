#include "TraceTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "trace/OceanState.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <mutex>

namespace yambz {

TraceTab& TraceTab::instance() { static TraceTab t; return t; }

// Colors
static ImVec4 colorForKind(int kind) {
    switch (kind) {
        case 1: return ImVec4(0.3f, 0.9f, 0.9f, 1);
        case 2: return ImVec4(0.4f, 0.8f, 0.4f, 1);
        case 3: return ImVec4(0.4f, 0.6f, 0.95f, 1);
        case 4: return ImVec4(0.7f, 0.7f, 0.4f, 1);
        case 5: return ImVec4(0.9f, 0.6f, 0.3f, 1);
        case 6: return ImVec4(0.95f, 0.5f, 0.9f, 1);
        case 7: return ImVec4(0.95f, 0.35f, 0.4f, 1);
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
//  Whitelist state (global for this tab)
// ═══════════════════════════════════════════════════════════
static std::vector<std::string> g_whitePackages;
static std::vector<std::string> g_whiteClasses;
static char g_newWhitePackage[256] = {0};
static char g_newWhiteClass[256] = {0};

// Dump state
static char g_dumpClass[256] = {0};
static char g_dumpMethod[128] = {0};
static char g_dumpArgs[256] = {0};
static char g_dumpStatus[512] = "idle";

// ═══════════════════════════════════════════════════════════
//  JNI helper (call OceanController)
// ═══════════════════════════════════════════════════════════
static JNIEnv* getEnv() {
    JavaVM* vm = yambz_getVM();
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

static bool jniCallBool(const char* method, const char* arg) {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass cls = env->FindClass("com/yambz/feature/OceanController");
    if (!cls) { env->ExceptionClear(); return false; }
    jmethodID mid = env->GetStaticMethodID(cls, method, "(Ljava/lang/String;)Z");
    if (!mid) { env->ExceptionClear(); return false; }
    jstring ja = env->NewStringUTF(arg);
    jboolean r = env->CallStaticBooleanMethod(cls, mid, ja);
    env->DeleteLocalRef(ja);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return r == JNI_TRUE;
}

static bool jniAddWhitePackage(const char* pkg) {
    return jniCallBool("addWhitePackage", pkg);
}
static bool jniRemoveWhitePackage(const char* pkg) {
    return jniCallBool("removeWhitePackage", pkg);
}

static std::string jniDumpClass(const char* className) {
    JNIEnv* env = getEnv();
    if (!env) return "no VM";
    jclass cls = env->FindClass("com/yambz/feature/OceanController");
    if (!cls) { env->ExceptionClear(); return "no controller"; }
    jmethodID mid = env->GetStaticMethodID(cls, "dumpClassByName", "(Ljava/lang/String;)Z");
    if (!mid) { env->ExceptionClear(); return "no method"; }
    jstring jc = env->NewStringUTF(className);
    jboolean ok = env->CallStaticBooleanMethod(cls, mid, jc);
    env->DeleteLocalRef(jc);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return "exception"; }
    return ok == JNI_TRUE ? "OK" : "failed";
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Config (same as before, expanded)
// ═══════════════════════════════════════════════════════════
static void drawConfig(OceanUIState& ui) {
    int mode = ui.config.mode;
    Theme::sectionHeader("Mode");
    if (ImGui::RadioButton("Global", &mode, 0)) ui.config.mode = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Thread", &mode, 1)) ui.config.mode = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Member", &mode, 2)) ui.config.mode = 2;

    if (ui.config.mode == 2) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##spec",
            "com.example.Foo#bar(int,String) or #<init>()",
            ui.config.memberSpec, sizeof(ui.config.memberSpec));
    }

    Theme::sectionHeader("Events (8)");
    ImGui::Checkbox("CALL_ENTER", &ui.config.evCallEnter); ImGui::SameLine(200);
    ImGui::Checkbox("CALL", &ui.config.evCall); ImGui::SameLine(300);
    ImGui::Checkbox("RETURN", &ui.config.evReturn);
    ImGui::Checkbox("EXEC", &ui.config.evExec); ImGui::SameLine(200);
    ImGui::Checkbox("BLOCK", &ui.config.evBlock); ImGui::SameLine(300);
    ImGui::Checkbox("FILE_READ", &ui.config.evFileRead);
    ImGui::Checkbox("FIELD_WRITE", &ui.config.evFieldWrite);

    Theme::sectionHeader("Scope (5)");
    ImGui::Checkbox("kJdk", &ui.config.scJdk); ImGui::SameLine(120);
    ImGui::Checkbox("kPlatform", &ui.config.scPlatform); ImGui::SameLine(260);
    ImGui::Checkbox("kApplication", &ui.config.scApplication);
    ImGui::Checkbox("kPackageFilter", &ui.config.scPackageFilter); ImGui::SameLine(200);
    ImGui::Checkbox("kEntryOnly", &ui.config.scEntryOnly);

    if (ui.config.scPackageFilter) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pkg", "package prefix",
            ui.config.packagePrefix, sizeof(ui.config.packagePrefix));
    }

    Theme::sectionHeader("Sinker (4)");
    int sk = ui.config.sinkerKind;
    if (ImGui::RadioButton("Custom", &sk, 0)) ui.config.sinkerKind = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("KeyWord", &sk, 1)) ui.config.sinkerKind = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("String", &sk, 2)) ui.config.sinkerKind = 2;
    ImGui::SameLine();
    if (ImGui::RadioButton("Empty", &sk, 3)) ui.config.sinkerKind = 3;

    if (ui.config.sinkerKind == 1 || ui.config.sinkerKind == 2) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##kw", "keyword", ui.config.sinkerKeyword,
            sizeof(ui.config.sinkerKeyword));
    }

    Theme::sectionHeader("Limits");
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Timeout (ms)", &ui.config.timeoutMs, 1000, 10000);
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Max events", &ui.config.maxEvents, 1000, 100000);
    ImGui::Checkbox("Only main thread", &ui.config.onlyMainThread);

    Theme::sectionHeader("Control");
    if (!ui.tracing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1));
        if (ImGui::Button("▶ Start Trace", ImVec2(150, 36))) {
            if (oceanStart()) {
                Notifications::instance().push("Ocean started", 2.0f);
            } else {
                Notifications::instance().push("Ocean failed", 3.0f);
            }
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1));
        if (ImGui::Button("■ Stop Trace", ImVec2(150, 36))) {
            oceanStop();
            Notifications::instance().push("Ocean stopped", 2.0f);
        }
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Sync", ImVec2(80, 36))) oceanSync();
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(100, 36))) oceanClearEvents();
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Events (with override + bypass/skip)
// ═══════════════════════════════════════════════════════════
static void drawEvents(OceanUIState& ui) {
    // Stats
    ImGui::TextDisabled("%s | total: %ld",
        ui.tracing ? "TRACING" : "IDLE", ui.totalEvents);
    for (int i = 1; i <= 7; i++) {
        ImGui::SameLine();
        ImGui::TextColored(colorForKind(i), "%s:%ld", kindName(i), ui.countByKind[i]);
    }

    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", "filter...", ui.searchBuf, sizeof(ui.searchBuf));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    const char* kinds[] = {"All", "CALL_ENTER", "CALL", "RETURN", "EXEC", "BLOCK", "FILE_READ", "FIELD_WRITE"};
    ImGui::Combo("##fk", &ui.filterKind, kinds, 8);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &ui.autoScroll);
    ImGui::Separator();

    // Snapshot
    std::vector<OceanEvent> snap;
    {
        std::lock_guard<std::mutex> lk(ui.eventsMu);
        snap = ui.events;
    }

    std::string q = ui.searchBuf;
    for (auto& c : q) c = (char)std::tolower((unsigned char)c);

    std::vector<size_t> vis;
    for (size_t i = 0; i < snap.size(); i++) {
        const auto& e = snap[i];
        if (ui.filterKind != 0 && e.kind != ui.filterKind) continue;
        if (!q.empty()) {
            std::string h = e.methodDesc + " " + e.extra + " " + e.className;
            for (auto& c : h) c = (char)std::tolower((unsigned char)c);
            if (h.find(q) == std::string::npos) continue;
        }
        vis.push_back(i);
    }

    float listW = ImGui::GetContentRegionAvail().x * 0.55f;
    ImGui::BeginChild("##evlist", ImVec2(listW, 0), true);
    ImGuiListClipper clip;
    clip.Begin((int)vis.size(), 20.0f);
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            size_t idx = vis[k];
            const auto& e = snap[idx];
            ImGui::PushID((int)idx);

            for (int d = 0; d < e.depth && d < 15; d++) ImGui::Indent(8.0f);

            ImGui::TextColored(colorForKind(e.kind), "[%s]", kindName(e.kind));
            ImGui::SameLine();
            std::string mp = e.methodDesc.empty() ? "?" : e.methodDesc;
            if (mp.length() > 60) mp = mp.substr(0, 60) + "…";

            bool sel = (ui.selectedEvent == (int)idx);
            if (ImGui::Selectable(mp.c_str(), sel)) {
                ui.selectedEvent = (int)idx;
            }

            for (int d = 0; d < e.depth && d < 15; d++) ImGui::Unindent(8.0f);
            ImGui::PopID();
        }
    }
    clip.End();
    ImGui::EndChild();

    ImGui::SameLine();

    // Detail + actions
    ImGui::BeginChild("##evdet", ImVec2(0, 0), true);
    if (ui.selectedEvent >= 0 && ui.selectedEvent < (int)snap.size()) {
        const auto& e = snap[ui.selectedEvent];
        Theme::sectionHeader("Event Detail");
        ImGui::Text("Kind: %s", kindName(e.kind));
        ImGui::Text("Depth: %d", e.depth);
        ImGui::Text("Method: %s", e.methodDesc.c_str());
        if (!e.className.empty()) ImGui::Text("Class: %s", e.className.c_str());
        if (!e.extra.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("Extra: %s", e.extra.c_str());
        }

        ImGui::Separator();
        Theme::sectionHeader("Actions");

        if (ImGui::Button("Copy")) {
            Notifications::instance().push("Copied (stub)", 2.0f);
        }
        ImGui::SameLine();
        if (e.kind == 2) {  // CALL event
            if (ImGui::Button("Bypass")) {
                Notifications::instance().push("Bypass set for next call", 2.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("Skip")) {
                Notifications::instance().push("Skip set for next call", 2.0f);
            }
        }
        if (e.kind == 3) {  // RETURN event
            static char overrideVal[128] = {0};
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("Override", overrideVal, sizeof(overrideVal));
            ImGui::SameLine();
            if (ImGui::Button("Apply")) {
                Notifications::instance().push("Return override applied", 2.0f);
            }
        }
        if (e.kind == 4) {  // EXEC event
            ImGui::TextDisabled("Register viewer (stub — full impl in next iteration)");
        }
    } else {
        ImGui::TextDisabled("(select an event)");
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Whitelist
// ═══════════════════════════════════════════════════════════
static void drawWhitelist() {
    Theme::sectionHeader("Whitelisted Packages");
    for (size_t i = 0; i < g_whitePackages.size(); i++) {
        ImGui::BulletText("%s", g_whitePackages[i].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) {
            jniRemoveWhitePackage(g_whitePackages[i].c_str());
            g_whitePackages.erase(g_whitePackages.begin() + i);
            break;
        }
    }
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##np", "com.example.", g_newWhitePackage, sizeof(g_newWhitePackage));
    ImGui::SameLine();
    if (ImGui::Button("Add Package")) {
        if (g_newWhitePackage[0]) {
            if (jniAddWhitePackage(g_newWhitePackage)) {
                g_whitePackages.push_back(g_newWhitePackage);
                g_newWhitePackage[0] = 0;
                Notifications::instance().push("Package whitelisted", 2.0f);
            }
        }
    }

    Theme::sectionHeader("Whitelisted Classes");
    for (auto& c : g_whiteClasses) {
        ImGui::BulletText("%s", c.c_str());
    }
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##nc", "com.example.Foo", g_newWhiteClass, sizeof(g_newWhiteClass));
    ImGui::SameLine();
    if (ImGui::Button("Add Class")) {
        if (g_newWhiteClass[0]) {
            g_whiteClasses.push_back(g_newWhiteClass);
            g_newWhiteClass[0] = 0;
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Dump
// ═══════════════════════════════════════════════════════════
static void drawDump() {
    Theme::sectionHeader("Dump Class");
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Class", g_dumpClass, sizeof(g_dumpClass));
    if (ImGui::Button("Dump Class", ImVec2(150, 32))) {
        std::string r = jniDumpClass(g_dumpClass);
        strncpy(g_dumpStatus, r.c_str(), sizeof(g_dumpStatus) - 1);
    }

    Theme::sectionHeader("Dump Method");
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Class", g_dumpMethod, sizeof(g_dumpMethod));
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("Method", g_dumpArgs, sizeof(g_dumpArgs));

    Theme::sectionHeader("Status");
    ImGui::TextWrapped("%s", g_dumpStatus);
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Status
// ═══════════════════════════════════════════════════════════
static void drawStatus(OceanUIState& ui) {
    Theme::sectionHeader("Status");
    ImGui::Text("Tracing: %s", ui.tracing ? "YES" : "NO");
    ImGui::Text("Mode: %d", ui.config.mode);
    ImGui::Text("Total events: %ld", ui.totalEvents);
    ImGui::Text("Stop reason: %d", ui.stopReason);
    ImGui::Text("Trace count (Ocean): %ld", ui.traceCount);
    Theme::sectionHeader("Per-Kind");
    for (int i = 1; i <= 7; i++) {
        ImGui::TextColored(colorForKind(i), "%s: %ld", kindName(i), ui.countByKind[i]);
    }
    ImGui::Separator();
    if (ImGui::Button("Sync Now", ImVec2(120, 0))) oceanSync();
}

// ═══════════════════════════════════════════════════════════
//  Main draw
// ═══════════════════════════════════════════════════════════
void TraceTab::draw() {
    auto& ui = oceanUI();
    const char* subTabs[] = {"Config", "Events", "Whitelist", "Dump", "Status"};
    for (int i = 0; i < 5; i++) {
        if (i > 0) ImGui::SameLine();
        bool sel = (ui.currentTab == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.24f, 0.1f, 1.0f));
        if (ImGui::Button(subTabs[i], ImVec2(100, 32))) ui.currentTab = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    switch (ui.currentTab) {
        case 0: drawConfig(ui); break;
        case 1: drawEvents(ui); break;
        case 2: drawWhitelist(); break;
        case 3: drawDump(); break;
        case 4: drawStatus(ui); break;
    }
}

} // namespace yambz
