#include "StalkerTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <cstring>

namespace yambz {

StalkerTab& StalkerTab::instance() { static StalkerTab t; return t; }

static char g_libName[128] = {0};
static char g_symbol[128] = {0};
static char g_status[256] = "Idle";
static bool g_following = false;

static void callStalkerJava(const char* method, const char* arg1, const char* arg2) {
    JavaVM* vm = yambz_getVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass cls = env->FindClass("com/yambz/feature/StalkerController");
    if (!cls) { env->ExceptionClear(); return; }

    if (arg1 && arg2) {
        jmethodID mid = env->GetStaticMethodID(cls, method,
            "(Ljava/lang/String;Ljava/lang/String;)Z");
        if (mid) {
            jstring j1 = env->NewStringUTF(arg1);
            jstring j2 = env->NewStringUTF(arg2);
            env->CallStaticBooleanMethod(cls, mid, j1, j2);
            env->DeleteLocalRef(j1);
            env->DeleteLocalRef(j2);
        }
    } else {
        jmethodID mid = env->GetStaticMethodID(cls, method, "()V");
        if (mid) env->CallStaticVoidMethod(cls, mid);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void StalkerTab::draw() {
    Theme::sectionHeader("Stalker");

    ImGui::Text("Follow Function:");
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("Library", "libc.so", g_libName, sizeof(g_libName));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("Symbol", "malloc", g_symbol, sizeof(g_symbol));

    if (!g_following) {
        if (ImGui::Button("▶ Follow", ImVec2(150, 36))) {
            callStalkerJava("followSymbol", g_libName, g_symbol);
            g_following = true;
            strcpy(g_status, "Following");
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1));
        if (ImGui::Button("■ Unfollow", ImVec2(150, 36))) {
            callStalkerJava("unfollow", nullptr, nullptr);
            g_following = false;
            strcpy(g_status, "Stopped");
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1), "Status: %s", g_status);

    ImGui::Separator();
    Theme::sectionHeader("Follow Current Thread");
    if (ImGui::Button("▶ Follow Current Thread", ImVec2(250, 36))) {
        callStalkerJava("followCurrentThread", nullptr, nullptr);
        g_following = true;
        strcpy(g_status, "Following thread");
    }

    ImGui::Spacing();
    Theme::sectionHeader("Notes");
    ImGui::TextWrapped(
        "Stalker follows instruction-level execution. "
        "Full event UI is coming in a future update. "
        "Events are logged to logcat with tag 'YAMBZ'.");
}

} // namespace yambz
