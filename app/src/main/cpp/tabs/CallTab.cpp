#include "CallTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <cstring>
#include <thread>
#include <atomic>

namespace yambz {

CallTab& CallTab::instance() { static CallTab t; return t; }
CallTab::State* CallTab::state() { static State s; return &s; }

// ═══════════════════════════════════════════════════════════
//  JNI helper
// ═══════════════════════════════════════════════════════════
static std::string callJavaInvokeSpec(const std::string& spec, const std::string& jsonArgs) {
    JavaVM* vm = yambz_getVM();
    if (!vm) return "{\"success\":false,\"error\":\"no VM\"}";
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass cls = env->FindClass("com/yambz/bridge/CallBridge");
    if (!cls) { env->ExceptionClear(); return "{\"success\":false,\"error\":\"no CallBridge\"}"; }
    jmethodID mid = env->GetStaticMethodID(cls, "invokeSpec",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return "{\"success\":false,\"error\":\"no method\"}"; }

    jstring jspec = env->NewStringUTF(spec.c_str());
    jstring jargs = env->NewStringUTF(jsonArgs.c_str());
    jstring jresult = (jstring) env->CallStaticObjectMethod(cls, mid, jspec, jargs);
    env->DeleteLocalRef(jspec);
    env->DeleteLocalRef(jargs);

    std::string out = "{\"success\":false,\"error\":\"JNI call failed\"}";
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return out;
    }
    if (jresult) {
        const char* c = env->GetStringUTFChars(jresult, nullptr);
        if (c) out = c;
        if (c) env->ReleaseStringUTFChars(jresult, c);
        env->DeleteLocalRef(jresult);
    }
    return out;
}

void CallTab::draw() {
    auto* s = state();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));

    Theme::sectionHeader("Method Spec");
    ImGui::TextDisabled("Format: com.example.Foo#method(int,java.lang.String)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##spec", s->spec, sizeof(s->spec));

    ImGui::Spacing();
    Theme::sectionHeader("Arguments (JSON array)");
    ImGui::TextDisabled("Example: [1, \"hello\", null, true]");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##args", s->jsonArgs, sizeof(s->jsonArgs),
                               ImVec2(-1, 100));

    ImGui::Spacing();
    if (ImGui::Button("▶ Invoke", ImVec2(150, 36))) {
        std::string spec = s->spec;
        std::string args = s->jsonArgs;
        if (spec.empty()) {
            Notifications::instance().push("Enter member spec first", 2.0f);
        } else {
            auto* st = state();
            st->invoking = true;
            std::thread([st, spec, args]() {
                std::string r = callJavaInvokeSpec(spec, args);
                std::lock_guard<std::mutex> lk(st->mu);
                strncpy(st->result, r.c_str(), sizeof(st->result) - 1);
                st->result[sizeof(st->result) - 1] = '\0';
                st->invoking = false;
            }).detach();
        }
    }
    ImGui::SameLine();
    if (s->invoking) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1));
        ImGui::Text("⏳ Invoking...");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    Theme::sectionHeader("Result");

    std::lock_guard<std::mutex> lk(s->mu);
    if (s->result[0]) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.07f, 1));
        ImGui::BeginChild("##result", ImVec2(0, 200), true);
        ImGui::TextWrapped("%s", s->result);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("(no result yet)");
    }

    ImGui::PopStyleVar();
}

} // namespace yambz
