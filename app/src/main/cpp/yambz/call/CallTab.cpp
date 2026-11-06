#include "CallTab.h"
#include <vector>
#include "CallState.h"
#include "ParameterEditor.h"
#include "CallEngine.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "../core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>

#include <jni.h>

namespace yambz {

CallTab& CallTab::instance() {
    static CallTab t;
    return t;
}

void CallTab::draw() {
    auto& st = callUI();

    if (!st.show || !st.target.method) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "اختر method من قائمة Classes ثم اضغط Call");
        return;
    }

    // ─── header ───
    Theme::drawSectionHeader("Method");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.83f, 0.63f, 0.09f, 1.0f));
    ImGui::TextWrapped("%s", st.target.methodDesc.c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("%s → %s", st.target.isStatic ? "static" : "instance",
                        st.target.returnType.c_str());

    ImGui::Spacing();
    ImGui::Separator();

    // ─── invoke/clear buttons ───
    bool running = CallEngine::instance().isRunning();
    if (running) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Cancel")) {
            CallEngine::instance().cancel();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "⏳ invoking...");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.30f, 1.0f));
        if (ImGui::Button("▶ Invoke", ImVec2(120, 0))) {
            extern void yambz_doInvoke();
            yambz_doInvoke();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button("Clear result")) {
            CallEngine::instance().clearResult();
            st.lastResultJson.clear();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ─── parameter editor (this + args) ───
    ImGui::BeginChild("##params", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f), false);

    if (!st.target.isStatic) {
        Theme::drawSectionHeader("this");
        if (!st.thisNode) st.thisNode = ParamNode::makeObjectUnsafe(st.target.declaringClass);
        ParameterEditor::draw("this", st.thisNode, st.target.declaringClass);
        ImGui::Spacing();
    }

    Theme::drawSectionHeader("Arguments");
    if (st.args.empty()) {
        ImGui::TextDisabled("(no arguments)");
    }
    for (size_t i = 0; i < st.args.size(); i++) {
        ImGui::PushID((int)i);
        char lbl[32]; snprintf(lbl, sizeof lbl, "arg[%zu]", i);
        ParameterEditor::draw(lbl, st.args[i], st.target.argTypes[i]);
        ImGui::PopID();
    }
    ImGui::EndChild();

    // ─── result area ───
    Theme::drawSectionHeader("Result");
    ImGui::BeginChild("##res", ImVec2(0, 0), true);

    // poll result every frame (سريع — نستخدم JSON)
    if (!running) {
        extern std::string yambz_getLastResultJson();
        std::string json = yambz_getLastResultJson();
        if (!json.empty() && json != "{}") {
            st.lastResultJson = json;
        }
    }

    if (st.lastResultJson.empty()) {
        ImGui::TextDisabled("(invoke to see result)");
    } else {
        // بسيط: عرض JSON خام
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::TextWrapped("%s", st.lastResultJson.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

// ─── doInvoke helper ───
void yambz_doInvoke() {
    auto& st = callUI();
    if (!st.target.method) return;

    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    // سلسلة this إلى JSON
    std::string thisJson;
    if (!st.target.isStatic && st.thisNode) {
        thisJson = nodeToJson(st.thisNode);
    }

    std::vector<std::string> argsJson;
    for (auto& a : st.args) argsJson.push_back(nodeToJson(a));

    // بناء jstring array
    jclass cls_Str = env->FindClass("java/lang/String");
    jobjectArray jargs = env->NewObjectArray((jsize)argsJson.size(), cls_Str, nullptr);
    for (size_t i = 0; i < argsJson.size(); i++) {
        jstring js = env->NewStringUTF(argsJson[i].c_str());
        env->SetObjectArrayElement(jargs, (jsize)i, js);
        env->DeleteLocalRef(js);
    }

    jstring jthis = thisJson.empty() ? nullptr : env->NewStringUTF(thisJson.c_str());
    jstring jdesc = env->NewStringUTF(st.target.methodDesc.c_str());

    jclass cls_CB = env->FindClass("com/yambz/bridge/CallBridge");
    jmethodID m_invoke = env->GetStaticMethodID(cls_CB, "invoke",
        "(Ljava/lang/reflect/Member;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)Z");
    env->CallStaticBooleanMethod(cls_CB, m_invoke,
        st.target.method, jdesc, jthis, jargs);

    if (jargs) env->DeleteLocalRef(jargs);
    if (jthis) env->DeleteLocalRef(jthis);
    if (jdesc) env->DeleteLocalRef(jdesc);
}

// helper for CallTab to poll result
std::string yambz_getLastResultJson() {
    extern std::string yambz_callEngineLastResultJson();
    return yambz_callEngineLastResultJson();
}

} // namespace yambz
