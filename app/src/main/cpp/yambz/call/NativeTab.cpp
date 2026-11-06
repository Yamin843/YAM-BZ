#include "NativeTab.h"
#include "ui/Theme.h"
#include "core/Logger.h"
#include <imgui.h>
#include <cstring>
#include <vector>
#include <string>

#include <jni.h>

namespace yambz {

NativeTab& NativeTab::instance() {
    static NativeTab t;
    return t;
}

struct NativeUIState {
    std::vector<std::string> libs;
    std::vector<std::string> symbols;
    int selectedLib = -1;
    int selectedSymbol = -1;
    int symbolOffset = 0;
    char search[256] = {0};
    bool loadedLibs = false;
};
static NativeUIState s_state;

// JNI helpers
static void loadLibs() {
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls = env->FindClass("com/yambz/bridge/NativeCallBridge");
    jmethodID mid = env->GetStaticMethodID(cls, "listLibraries", "()[Ljava/lang/String;");
    if (!mid) return;
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid);
    if (!arr) return;
    jsize n = env->GetArrayLength(arr);
    s_state.libs.clear();
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) s_state.libs.emplace_back(c);
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    s_state.loadedLibs = true;
}

static void loadSymbols(const std::string& lib) {
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls = env->FindClass("com/yambz/bridge/NativeCallBridge");
    jmethodID mid = env->GetStaticMethodID(cls, "listSymbols",
        "(Ljava/lang/String;II)[Ljava/lang/String;");
    if (!mid) return;
    jstring jlib = env->NewStringUTF(lib.c_str());
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid, jlib,
        0, 5000);
    env->DeleteLocalRef(jlib);
    if (!arr) return;
    jsize n = env->GetArrayLength(arr);
    s_state.symbols.clear();
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) s_state.symbols.emplace_back(c);
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
}

void NativeTab::draw() {
    if (!s_state.loadedLibs) loadLibs();

    // ─── lib list ───
    ImGui::BeginChild("##libs", ImVec2(ImGui::GetContentRegionAvail().x * 0.35f, 0), true);
    Theme::drawSectionHeader("Libraries");
    for (size_t i = 0; i < s_state.libs.size(); i++) {
        bool sel = (s_state.selectedLib == (int) i);
        if (ImGui::Selectable(s_state.libs[i].c_str(), sel)) {
            s_state.selectedLib = (int) i;
            loadSymbols(s_state.libs[i]);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ─── symbol list ───
    ImGui::BeginChild("##syms", ImVec2(0, 0), true);
    Theme::drawSectionHeader("Symbols");

    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##q", "search symbol", s_state.search, sizeof s_state.search);
    ImGui::SameLine();
    if (ImGui::Button("Reload") && s_state.selectedLib >= 0) {
        loadSymbols(s_state.libs[s_state.selectedLib]);
    }

    ImGui::Separator();

    if (s_state.symbols.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "select a library first");
    } else {
        std::string q_lower = s_state.search;
        for (auto& c : q_lower) c = (char) std::tolower((unsigned char)c);
        ImGuiListClipper clip;
        clip.Begin((int) s_state.symbols.size(), 20.0f);
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const auto& s = s_state.symbols[i];
                if (!q_lower.empty()) {
                    std::string sl = s;
                    for (auto& c : sl) c = (char) std::tolower((unsigned char)c);
                    if (sl.find(q_lower) == std::string::npos) continue;
                }
                bool sel = s_state.selectedSymbol == i;
                if (ImGui::Selectable(s.c_str(), sel)) {
                    s_state.selectedSymbol = i;
                }
            }
        }
        clip.End();
    }
    ImGui::EndChild();
}

} // namespace yambz
