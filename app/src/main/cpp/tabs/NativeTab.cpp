#include "NativeTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <cstring>
#include <thread>

namespace yambz {

NativeTab& NativeTab::instance() { static NativeTab t; return t; }
NativeTab::State* NativeTab::state() { static State s; return &s; }

// ═══════════════════════════════════════════════════════════
//  JNI helpers
// ═══════════════════════════════════════════════════════════
static std::vector<std::string> callJavaStringArray(const char* method, const char* arg) {
    std::vector<std::string> out;
    JavaVM* vm = yambz_getVM();
    if (!vm) return out;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass cls = env->FindClass("com/yambz/bridge/NativeBridge");
    if (!cls) { env->ExceptionClear(); return out; }
    jmethodID mid = arg
        ? env->GetStaticMethodID(cls, method, "(Ljava/lang/String;)[Ljava/lang/String;")
        : env->GetStaticMethodID(cls, method, "()[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return out; }
    jobjectArray arr = arg
        ? (jobjectArray) env->CallStaticObjectMethod(cls, mid, env->NewStringUTF(arg))
        : (jobjectArray) env->CallStaticObjectMethod(cls, mid);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return out; }
    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) out.emplace_back(c);
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out;
}

static std::string callJavaString(const char* method, const char* arg1, const char* arg2) {
    JavaVM* vm = yambz_getVM();
    if (!vm) return "no VM";
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass cls = env->FindClass("com/yambz/bridge/NativeBridge");
    if (!cls) { env->ExceptionClear(); return "no class"; }
    jmethodID mid = env->GetStaticMethodID(cls, method,
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return "no method"; }
    jstring j1 = env->NewStringUTF(arg1 ? arg1 : "");
    jstring j2 = env->NewStringUTF(arg2 ? arg2 : "");
    jstring js = (jstring) env->CallStaticObjectMethod(cls, mid, j1, j2);
    env->DeleteLocalRef(j1);
    env->DeleteLocalRef(j2);
    std::string out = "error";
    if (env->ExceptionCheck()) { env->ExceptionClear(); return out; }
    if (js) {
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) out = c;
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    return out;
}

void NativeTab::draw() {
    auto* s = state();
    Theme::sectionHeader("Native");

    // Sub-tabs
    if (ImGui::Button("Libraries", ImVec2(120, 32))) s->currentSubTab = 0;
    ImGui::SameLine();
    if (ImGui::Button("CModule", ImVec2(120, 32))) s->currentSubTab = 1;
    ImGui::Separator();

    if (s->currentSubTab == 0) {
        // Libraries + symbols
        if (ImGui::Button("Refresh Libraries", ImVec2(180, 32))) {
            s->libraries = callJavaStringArray("listLibraries", nullptr);
            s->selectedLib = -1;
            s->symbols.clear();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(250);
        ImGui::InputTextWithHint("##search", "search...", s->searchBuf, sizeof(s->searchBuf));

        float leftW = ImGui::GetContentRegionAvail().x * 0.35f;
        ImGui::BeginChild("##libs", ImVec2(leftW, 0), true);
        ImGui::Text("Libraries (%zu)", s->libraries.size());
        ImGui::Separator();
        for (size_t i = 0; i < s->libraries.size(); i++) {
            bool sel = (s->selectedLib == (int)i);
            if (ImGui::Selectable(s->libraries[i].c_str(), sel)) {
                s->selectedLib = (int)i;
                s->symbols = callJavaStringArray("listSymbols", s->libraries[i].c_str());
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##syms", ImVec2(0, 0), true);
        ImGui::Text("Symbols (%zu)", s->symbols.size());
        ImGui::Separator();
        ImGuiListClipper clip;
        clip.Begin((int)s->symbols.size(), 16.0f);
        while (clip.Step()) {
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                ImGui::TextUnformatted(s->symbols[k].c_str());
            }
        }
        clip.End();
        ImGui::EndChild();
    } else {
        // CModule
        Theme::sectionHeader("C Source");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##source", s->cmoduleSource, sizeof(s->cmoduleSource),
                                  ImVec2(-1, 300));
        if (ImGui::Button("▶ Compile", ImVec2(150, 36))) {
            std::string src = s->cmoduleSource;
            std::string result = callJavaString("compileC", src.c_str(), "");
            std::lock_guard<std::mutex> lk(s->mu);
            strncpy(s->cmoduleResult, result.c_str(), sizeof(s->cmoduleResult) - 1);
            Notifications::instance().push(
                result == "OK" ? "Compiled" : "Compile error", 2.0f);
        }

        Theme::sectionHeader("Call Function");
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Symbol", s->symbolCallSig, sizeof(s->symbolCallSig));
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Signature (int(int,int))", s->symbolCallArgs, sizeof(s->symbolCallArgs));
        if (ImGui::Button("Call", ImVec2(100, 32))) {
            // Just show result
        }

        if (s->cmoduleResult[0]) {
            Theme::sectionHeader("Result");
            ImGui::TextWrapped("%s", s->cmoduleResult);
        }
    }
}

} // namespace yambz
