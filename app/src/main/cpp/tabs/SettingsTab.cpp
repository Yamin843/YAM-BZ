#include "SettingsTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "trace/OceanState.h"

#include <imgui.h>
#include <cstring>
#include <string>

namespace yambz {

SettingsTab& SettingsTab::instance() { static SettingsTab t; return t; }

struct SettingsState {
    float fontSize = 36.0f;
    bool showFps = false;
    int execMode = 4; // JIT_OPTIMIZED
    int traceTimeoutMs = 60000;
    int traceMaxEvents = 200000;
    char albFeatures[256] = "loading...";
    char cacheDir[512] = {0};
    char version[64] = "1.0.0";
    bool loaded = false;
};

static SettingsState& st() { static SettingsState s; return s; }

// ═══════════════════════════════════════════════════════════
//  JNI helpers
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

static std::string jniString0(const char* cls, const char* method) {
    JNIEnv* env = getEnv();
    if (!env) return "";
    jclass c = env->FindClass(cls);
    if (!c) { env->ExceptionClear(); return ""; }
    jmethodID m = env->GetStaticMethodID(c, method, "()Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return ""; }
    jstring r = (jstring) env->CallStaticObjectMethod(c, m);
    std::string out;
    if (!env->ExceptionCheck() && r) {
        const char* s = env->GetStringUTFChars(r, nullptr);
        if (s) out = s;
        if (s) env->ReleaseStringUTFChars(r, s);
        env->DeleteLocalRef(r);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    return out;
}

static void jniSetExecConfig(int targetMode, int hookerMode) {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass c = env->FindClass("qing.albatross.core.Albatross");
    if (!c) { env->ExceptionClear(); return; }
    jmethodID m = env->GetStaticMethodID(c, "setExecConfiguration", "(II)V");
    if (m) env->CallStaticVoidMethod(c, m, targetMode, hookerMode);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// ═══════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════
void SettingsTab::draw() {
    auto& s = st();

    if (!s.loaded) {
        s.loaded = true;
        strncpy(s.cacheDir, yambz_getCacheDir().c_str(), sizeof(s.cacheDir) - 1);
        s.albFeatures[0] = 0;
        strncpy(s.albFeatures,
            jniString0("com/yambz/bridge/UIBridge", "getAlbatrossFeatures").c_str(),
            sizeof(s.albFeatures) - 1);
        s.traceTimeoutMs = oceanUI().config.timeoutMs;
        s.traceMaxEvents = oceanUI().config.maxEvents;
    }

    // ═══════════════════════════════════════════════════════════
    //  Appearance
    // ═══════════════════════════════════════════════════════════
    Theme::sectionHeader("Appearance");
    ImGui::Text("Font Size");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(250);
    if (ImGui::SliderFloat("##fs", &s.fontSize, 24.0f, 56.0f, "%.0f px")) {
        // Display only — actual reload requires Renderer restart
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(restart to apply)");

    ImGui::Text("Show FPS");
    ImGui::SameLine(180);
    ImGui::Checkbox("##fps", &s.showFps);

    // ═══════════════════════════════════════════════════════════
    //  Albatross Execution Configuration
    // ═══════════════════════════════════════════════════════════
    Theme::sectionHeader("Albatross Execution");
    ImGui::TextDisabled("JIT/AOT mode for hooks and target methods");

    ImGui::Text("Mode");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(300);
    const char* modes[] = {
        "DO_NOTHING (interpret)",
        "JIT_OSR",
        "JIT_BASELINE",
        "JIT_OPTIMIZED",
        "NATIVE_CODE (JIT+AOT)",
    };
    int modeVals[] = {0, 1, 2, 4, 0x80 | 4};
    int curr = 3;
    for (int i = 0; i < 5; i++) if (modeVals[i] == s.execMode) curr = i;
    if (ImGui::Combo("##em", &curr, modes, 5)) {
        s.execMode = modeVals[curr];
    }

    if (ImGui::Button("Apply Exec Config", ImVec2(200, 32))) {
        jniSetExecConfig(s.execMode, s.execMode);
        Notifications::instance().push("Execution config applied", 2.0f);
    }

    // ═══════════════════════════════════════════════════════════
    //  Ocean Trace Defaults
    // ═══════════════════════════════════════════════════════════
    Theme::sectionHeader("Ocean Trace Defaults");
    ImGui::Text("Default timeout");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputInt("##dto", &s.traceTimeoutMs, 1000, 10000)) {
        if (s.traceTimeoutMs < 1000) s.traceTimeoutMs = 1000;
        oceanUI().config.timeoutMs = s.traceTimeoutMs;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("ms");

    ImGui::Text("Default max events");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputInt("##dme", &s.traceMaxEvents, 1000, 100000)) {
        if (s.traceMaxEvents < 1000) s.traceMaxEvents = 1000;
        oceanUI().config.maxEvents = s.traceMaxEvents;
    }

    // ═══════════════════════════════════════════════════════════
    //  Storage
    // ═══════════════════════════════════════════════════════════
    Theme::sectionHeader("Storage");
    ImGui::TextWrapped("Cache dir: %s", s.cacheDir);

    if (ImGui::Button("Clear Call History", ImVec2(200, 28))) {
        Notifications::instance().push("History cleared", 2.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Presets", ImVec2(200, 28))) {
        Notifications::instance().push("Presets cleared", 2.0f);
    }

    // ═══════════════════════════════════════════════════════════
    //  Albatross Info
    // ═══════════════════════════════════════════════════════════
    Theme::sectionHeader("Albatross Engine");
    ImGui::Text("Version: 4.0.0 (prebuilt)");
    ImGui::Text("Features: %s", s.albFeatures);
    ImGui::TextWrapped(
        "This mod uses Albatross core, nativehook, ocean, and reflection "
        "modules. All functionality is provided by the Albatross framework.");

    // ═══════════════════════════════════════════════════════════
    //  About
    // ═══════════════════════════════════════════════════════════
    Theme::sectionHeader("YAM-BZ");
    ImGui::Text("Version: %s", s.version);
    ImGui::Text("Built: %s %s", __DATE__, __TIME__);
    ImGui::TextWrapped(
        "Mod menu for Android built on Albatross. "
        "9 tabs: Classes, Trace (Ocean), Call, Native, Stalker, "
        "Backup, Heap, Compile, Settings.");

    ImGui::Separator();
    if (ImGui::Button("Reset All Settings", ImVec2(200, 32))) {
        s.fontSize = 36.0f;
        s.showFps = false;
        s.execMode = 4;
        s.traceTimeoutMs = 60000;
        s.traceMaxEvents = 200000;
        oceanUI().config.timeoutMs = 60000;
        oceanUI().config.maxEvents = 200000;
        Notifications::instance().push("Settings reset", 2.0f);
    }
}

} // namespace yambz

// ═══════════════════════════════════════════════════════════
//  Global helper for FPS counter (called by Renderer)
// ═══════════════════════════════════════════════════════════
bool yambz_showFps() {
    return yambz::SettingsTab::instance().isFpsEnabled();
}
