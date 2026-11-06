#include "BackupTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <cstring>
#include <string>
#include <vector>

namespace yambz {

BackupTab& BackupTab::instance() { static BackupTab t; return t; }

static char g_spec[512] = {0};
static char g_label[128] = {0};
static std::vector<std::string> g_hooks;

static JNIEnv* getEnv() {
    JavaVM* vm = yambz_getVM();
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

static void jniAddHook(const char* spec, const char* label) {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass cls = env->FindClass("com/yambz/bridge/BackupBridge");
    if (!cls) { env->ExceptionClear(); return; }
    jmethodID mid = env->GetStaticMethodID(cls, "addHook",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return; }
    jstring js = env->NewStringUTF(spec);
    jstring jl = env->NewStringUTF(label);
    env->CallStaticObjectMethod(cls, mid, js, jl);
    env->DeleteLocalRef(js);
    env->DeleteLocalRef(jl);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static void jniUnhookAt(int idx) {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass cls = env->FindClass("com/yambz/bridge/BackupBridge");
    if (!cls) { env->ExceptionClear(); return; }
    jmethodID mid = env->GetStaticMethodID(cls, "unhookAt", "(I)Z");
    if (mid) env->CallStaticBooleanMethod(cls, mid, idx);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static void jniUnhookAll() {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass cls = env->FindClass("com/yambz/bridge/BackupBridge");
    if (!cls) { env->ExceptionClear(); return; }
    jmethodID mid = env->GetStaticMethodID(cls, "unhookAll", "()V");
    if (mid) env->CallStaticVoidMethod(cls, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static void refreshHooks() {
    g_hooks.clear();
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass cls = env->FindClass("com/yambz/bridge/BackupBridge");
    if (!cls) { env->ExceptionClear(); return; }
    jmethodID mid = env->GetStaticMethodID(cls, "listHooks", "()[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return; }
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return; }
    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) g_hooks.emplace_back(c);
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
}

void BackupTab::draw() {
    Theme::sectionHeader("Add Hook");
    ImGui::TextDisabled("Format: com.example.Foo#method(int,String)");

    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Member Spec", g_spec, sizeof(g_spec));
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("Label", g_label, sizeof(g_label));

    if (ImGui::Button("Add Hook", ImVec2(150, 32))) {
        if (g_spec[0]) {
            jniAddHook(g_spec, g_label[0] ? g_label : "unnamed");
            Notifications::instance().push("Hook added", 2.0f);
            g_spec[0] = 0;
            g_label[0] = 0;
            refreshHooks();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh List", ImVec2(150, 32))) {
        refreshHooks();
    }

    ImGui::Separator();
    Theme::sectionHeader("Active Hooks");
    ImGui::Text("Hooks: %zu", g_hooks.size());

    for (size_t i = 0; i < g_hooks.size(); i++) {
        ImGui::PushID((int)i);
        const auto& h = g_hooks[i];
        ImGui::Text("%s", h.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Unhook")) {
            jniUnhookAt((int)i);
            refreshHooks();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Unhook All", ImVec2(150, 32))) {
        jniUnhookAll();
        g_hooks.clear();
        Notifications::instance().push("All hooks removed", 2.0f);
    }
}

} // namespace yambz
