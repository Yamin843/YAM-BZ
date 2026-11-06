#include "CompileTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"

#include <imgui.h>
#include <cstring>

namespace yambz {

CompileTab& CompileTab::instance() { static CompileTab t; return t; }

static char g_className[256] = {0};
static char g_methodSpec[512] = {0};
static char g_result[1024] = "idle";
static int g_compileOption = 4; // JIT_OPTIMIZED

static JNIEnv* getEnv() {
    JavaVM* vm = yambz_getVM();
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

static std::string jniCompileClass(const char* className, int option) {
    JNIEnv* env = getEnv();
    if (!env) return "no VM";
    jclass c = env->FindClass("com/yambz/bridge/CompileBridge");
    if (!c) { env->ExceptionClear(); return "no class"; }
    jmethodID m = env->GetStaticMethodID(c, "compileClassByName",
        "(Ljava/lang/String;I)Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return "no method"; }
    jstring jc = env->NewStringUTF(className);
    jstring r = (jstring) env->CallStaticObjectMethod(c, m, jc, option);
    env->DeleteLocalRef(jc);
    std::string out = "error";
    if (env->ExceptionCheck()) { env->ExceptionClear(); return out; }
    if (r) {
        const char* s = env->GetStringUTFChars(r, nullptr);
        if (s) out = s;
        if (s) env->ReleaseStringUTFChars(r, s);
        env->DeleteLocalRef(r);
    }
    return out;
}

static std::string jniCallSpec(const char* method, const char* spec) {
    JNIEnv* env = getEnv();
    if (!env) return "no VM";
    jclass c = env->FindClass("com/yambz/bridge/CompileBridge");
    if (!c) { env->ExceptionClear(); return "no class"; }
    jmethodID m = env->GetStaticMethodID(c, method,
        "(Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return "no method"; }
    jstring js = env->NewStringUTF(spec);
    jstring r = (jstring) env->CallStaticObjectMethod(c, m, js);
    env->DeleteLocalRef(js);
    std::string out = "error";
    if (env->ExceptionCheck()) { env->ExceptionClear(); return out; }
    if (r) {
        const char* s = env->GetStringUTFChars(r, nullptr);
        if (s) out = s;
        if (s) env->ReleaseStringUTFChars(r, s);
        env->DeleteLocalRef(r);
    }
    return out;
}

void CompileTab::draw() {
    Theme::sectionHeader("Compile Class");

    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Class Name", g_className, sizeof(g_className));

    ImGui::SetNextItemWidth(250);
    const char* options[] = {"DO_NOTHING", "JIT_OSR", "JIT_BASELINE", "JIT_OPTIMIZED",
                             "AOT", "NATIVE_CODE", "INTERPRETER", "DECOMPILE"};
    int optVals[] = {0, 1, 2, 4, 8, 0x8A, 8, 8};
    int current = 3;
    ImGui::Combo("Option", &current, options, 8);
    g_compileOption = optVals[current];

    if (ImGui::Button("Compile Class", ImVec2(150, 32))) {
        std::string r = jniCompileClass(g_className, g_compileOption);
        strncpy(g_result, r.c_str(), sizeof(g_result) - 1);
    }

    ImGui::Separator();
    Theme::sectionHeader("Compile/Decompile Method");

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("Method Spec", g_methodSpec, sizeof(g_methodSpec));
    ImGui::TextDisabled("Format: com.Foo#bar(int)");

    if (ImGui::Button("Compile", ImVec2(120, 32))) {
        std::string r = jniCallSpec("compileMethodByName", g_methodSpec);
        strncpy(g_result, r.c_str(), sizeof(g_result) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Decompile", ImVec2(120, 32))) {
        std::string r = jniCallSpec("decompileMethodByName", g_methodSpec);
        strncpy(g_result, r.c_str(), sizeof(g_result) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Get Info", ImVec2(120, 32))) {
        std::string r = jniCallSpec("getMethodInfo", g_methodSpec);
        strncpy(g_result, r.c_str(), sizeof(g_result) - 1);
    }

    ImGui::Separator();
    Theme::sectionHeader("Result");
    ImGui::TextWrapped("%s", g_result);
}

} // namespace yambz
