#include "HeapTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"

#include <imgui.h>
#include <cstring>
#include <string>
#include <vector>

namespace yambz {

HeapTab& HeapTab::instance() { static HeapTab t; return t; }
static char g_className[256] = {0};
static std::vector<std::string> g_results;

static JNIEnv* getEnv() {
    JavaVM* vm = yambz_getVM();
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

static void jniStartSearch(const char* cls) {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass c = env->FindClass("com/yambz/bridge/HeapBridge");
    if (!c) { env->ExceptionClear(); return; }
    jmethodID m = env->GetStaticMethodID(c, "startSearch", "(Ljava/lang/String;)V");
    if (!m) { env->ExceptionClear(); return; }
    jstring jc = env->NewStringUTF(cls);
    env->CallStaticVoidMethod(c, m, jc);
    env->DeleteLocalRef(jc);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static void refreshResults() {
    g_results.clear();
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass c = env->FindClass("com/yambz/bridge/HeapBridge");
    if (!c) { env->ExceptionClear(); return; }
    jmethodID m = env->GetStaticMethodID(c, "getResults", "()[Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return; }
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(c, m);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return; }
    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* cc = env->GetStringUTFChars(js, nullptr);
        if (cc) g_results.emplace_back(cc);
        if (cc) env->ReleaseStringUTFChars(js, cc);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
}

void HeapTab::draw() {
    Theme::sectionHeader("Search Heap for Instances");
    ImGui::SetNextItemWidth(400);
    ImGui::InputTextWithHint("Class", "com.example.Foo", g_className, sizeof(g_className));
    if (ImGui::Button("Search", ImVec2(120, 32))) {
        if (g_className[0]) {
            jniStartSearch(g_className);
            Notifications::instance().push("Searching heap...", 2.0f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(120, 32))) refreshResults();

    ImGui::Separator();
    ImGui::Text("Results: %zu", g_results.size());
    ImGui::BeginChild("##results", ImVec2(0, 0), true);
    for (const auto& r : g_results) {
        ImGui::TextWrapped("%s", r.c_str());
    }
    ImGui::EndChild();
}

} // namespace yambz
