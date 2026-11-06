#include "CallTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "params/ParamEditor.h"
#include "params/ParamValue.h"
#include "bridge/ClassBridgeQuery.h"

#include <imgui.h>
#include <cstring>
#include <thread>
#include <mutex>
#include <vector>
#include <string>

namespace yambz {

CallTab& CallTab::instance() { static CallTab t; return t; }

struct CallTab::State {
    char spec[512] = {0};
    std::vector<std::shared_ptr<ParamValue>> params;
    std::vector<std::string> paramTypes;
    char result[4096] = {0};
    bool invoking = false;
    std::mutex mu;

    // History
    struct HistItem {
        std::string spec;
        std::string argsJson;
        std::string result;
    };
    std::vector<HistItem> history;
};

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
    if (!cls) { env->ExceptionClear(); return "{\"success\":false,\"error\":\"no class\"}"; }
    jmethodID mid = env->GetStaticMethodID(cls, "invokeSpec",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return "{\"success\":false,\"error\":\"no method\"}"; }
    jstring jspec = env->NewStringUTF(spec.c_str());
    jstring jargs = env->NewStringUTF(jsonArgs.c_str());
    jstring jr = (jstring) env->CallStaticObjectMethod(cls, mid, jspec, jargs);
    env->DeleteLocalRef(jspec);
    env->DeleteLocalRef(jargs);
    std::string out = "{\"success\":false,\"error\":\"JNI fail\"}";
    if (env->ExceptionCheck()) { env->ExceptionClear(); return out; }
    if (jr) {
        const char* c = env->GetStringUTFChars(jr, nullptr);
        if (c) out = c;
        if (c) env->ReleaseStringUTFChars(jr, c);
        env->DeleteLocalRef(jr);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════
//  Parse member spec to determine param types
//  "com.Foo#bar(int,java.lang.String)" → ["int", "java.lang.String"]
// ═══════════════════════════════════════════════════════════
static std::vector<std::string> parseParamTypes(const std::string& spec) {
    std::vector<std::string> out;
    size_t p1 = spec.find('(');
    size_t p2 = spec.find(')', p1);
    if (p1 == std::string::npos || p2 == std::string::npos) return out;
    std::string args = spec.substr(p1 + 1, p2 - p1 - 1);
    if (args.empty()) return out;
    size_t start = 0;
    while (start <= args.length()) {
        size_t comma = args.find(',', start);
        std::string part = (comma == std::string::npos)
            ? args.substr(start)
            : args.substr(start, comma - start);
        // trim
        while (!part.empty() && (part[0] == ' ')) part.erase(0, 1);
        while (!part.empty() && (part.back() == ' ')) part.pop_back();
        if (!part.empty()) out.push_back(part);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

// ═══════════════════════════════════════════════════════════
//  Build JSON for all params
// ═══════════════════════════════════════════════════════════
static std::string buildArgsJson(const std::vector<std::shared_ptr<ParamValue>>& params) {
    std::string out = "[";
    for (size_t i = 0; i < params.size(); i++) {
        if (i > 0) out += ',';
        out += params[i]->toJson();
    }
    out += "]";
    return out;
}


// ═══════════════════════════════════════════════════════════
//  Presets — save/load/list/delete
// ═══════════════════════════════════════════════════════════
static std::vector<std::string> g_presetList;  // "name|spec"
static char g_presetName[128] = {0};
static int g_selectedPreset = -1;

static std::vector<std::string> jniListPresets() {
    std::vector<std::string> out;
    JavaVM* vm = yambz_getVM();
    if (!vm) return out;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass c = env->FindClass("com/yambz/bridge/PresetBridge");
    if (!c) { env->ExceptionClear(); return out; }
    jmethodID m = env->GetStaticMethodID(c, "list", "()[Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return out; }
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(c, m);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return out; }
    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* s = env->GetStringUTFChars(js, nullptr);
        if (s) out.emplace_back(s);
        if (s) env->ReleaseStringUTFChars(js, s);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out;
}

static bool jniSavePreset(const char* name, const char* spec, const char* args) {
    JavaVM* vm = yambz_getVM();
    if (!vm) return false;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass c = env->FindClass("com/yambz/bridge/PresetBridge");
    if (!c) { env->ExceptionClear(); return false; }
    jmethodID m = env->GetStaticMethodID(c, "save",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
    if (!m) { env->ExceptionClear(); return false; }
    jstring j1 = env->NewStringUTF(name);
    jstring j2 = env->NewStringUTF(spec);
    jstring j3 = env->NewStringUTF(args);
    jboolean r = env->CallStaticBooleanMethod(c, m, j1, j2, j3);
    env->DeleteLocalRef(j1);
    env->DeleteLocalRef(j2);
    env->DeleteLocalRef(j3);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return r == JNI_TRUE;
}

static std::string jniLoadPreset(const char* name) {
    JavaVM* vm = yambz_getVM();
    if (!vm) return "";
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass c = env->FindClass("com/yambz/bridge/PresetBridge");
    if (!c) { env->ExceptionClear(); return ""; }
    jmethodID m = env->GetStaticMethodID(c, "load", "(Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return ""; }
    jstring jn = env->NewStringUTF(name);
    jstring r = (jstring) env->CallStaticObjectMethod(c, m, jn);
    env->DeleteLocalRef(jn);
    std::string out;
    if (env->ExceptionCheck() || !r) { env->ExceptionClear(); return ""; }
    const char* s = env->GetStringUTFChars(r, nullptr);
    if (s) out = s;
    if (s) env->ReleaseStringUTFChars(r, s);
    env->DeleteLocalRef(r);
    return out;
}

static void jniDeletePreset(const char* name) {
    JavaVM* vm = yambz_getVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass c = env->FindClass("com/yambz/bridge/PresetBridge");
    if (!c) { env->ExceptionClear(); return; }
    jmethodID m = env->GetStaticMethodID(c, "delete", "(Ljava/lang/String;)Z");
    if (!m) { env->ExceptionClear(); return; }
    jstring jn = env->NewStringUTF(name);
    env->CallStaticBooleanMethod(c, m, jn);
    env->DeleteLocalRef(jn);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void CallTab::draw() {
    auto* s = state();
    std::lock_guard<std::mutex> lk(s->mu);

    Theme::sectionHeader("Member Spec");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##spec", s->spec, sizeof(s->spec))) {
        // Reload params when spec changes
        auto types = parseParamTypes(s->spec);
        s->paramTypes = types;
        s->params.clear();
        for (auto& t : types) {
            s->params.push_back(ParamValue::forType(t));
        }
    }

    ImGui::TextDisabled("Format: com.example.Foo#method(int,java.lang.String)");

    ImGui::Spacing();
    Theme::sectionHeader("Parameters");

    if (s->params.empty()) {
        ImGui::TextDisabled("(no parameters — enter member spec or add args)");
    } else {
        for (size_t i = 0; i < s->params.size(); i++) {
            ImGui::PushID((int)i);
            char lbl[32];
            snprintf(lbl, sizeof lbl, "arg[%zu]", i);
            ParamEditor::draw(lbl, s->params[i], s->paramTypes[i]);
            ImGui::Spacing();
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    Theme::sectionHeader("Presets");

    // Refresh list button
    if (ImGui::Button("Refresh Presets", ImVec2(150, 28))) {
        g_presetList = jniListPresets();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##pname", "preset name", g_presetName, sizeof(g_presetName));
    ImGui::SameLine();
    if (ImGui::Button("Save Preset", ImVec2(120, 28))) {
        if (g_presetName[0] && s->spec[0]) {
            std::string args = buildArgsJson(s->params);
            if (jniSavePreset(g_presetName, s->spec, args.c_str())) {
                Notifications::instance().push("Preset saved", 2.0f);
                g_presetList = jniListPresets();
            } else {
                Notifications::instance().push("Save failed", 2.0f);
            }
        }
    }

    // Preset list
    if (!g_presetList.empty()) {
        ImGui::BeginChild("##presets", ImVec2(0, 100), true);
        for (size_t i = 0; i < g_presetList.size(); i++) {
            const auto& p = g_presetList[i];
            size_t sep = p.find('|');
            std::string pname = (sep != std::string::npos) ? p.substr(0, sep) : p;
            std::string pspec = (sep != std::string::npos) ? p.substr(sep + 1) : "";

            bool sel = (g_selectedPreset == (int)i);
            if (ImGui::Selectable((pname + "  [" + pspec + "]").c_str(), sel)) {
                g_selectedPreset = (int)i;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Load")) {
                    std::string result = jniLoadPreset(pname.c_str());
                    size_t bar = result.find('|');
                    if (bar != std::string::npos) {
                        std::string spec = result.substr(0, bar);
                        strncpy(s->spec, spec.c_str(), sizeof(s->spec) - 1);
                        // Re-parse types
                        auto types = parseParamTypes(s->spec);
                        s->paramTypes = types;
                        s->params.clear();
                        for (auto& t : types) {
                            s->params.push_back(ParamValue::forType(t));
                        }
                        Notifications::instance().push("Preset loaded", 2.0f);
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("×")) {
                    jniDeletePreset(pname.c_str());
                    g_presetList = jniListPresets();
                    ImGui::EndChild();
                    break;
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    Theme::sectionHeader("Invoke");

    if (!s->invoking) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1));
        if (ImGui::Button("▶ Invoke", ImVec2(150, 36))) {
            std::string spec = s->spec;
            std::string args = buildArgsJson(s->params);
            auto* st = state();

            // Save to history
            st->history.push_back({spec, args, ""});
            if (st->history.size() > 20) st->history.erase(st->history.begin());
            int histIdx = (int)st->history.size() - 1;

            st->invoking = true;
            std::thread([st, spec, args, histIdx]() {
                std::string r = callJavaInvokeSpec(spec, args);
                std::lock_guard<std::mutex> lk2(st->mu);
                strncpy(st->result, r.c_str(), sizeof(st->result) - 1);
                st->result[sizeof(st->result) - 1] = 0;
                if (histIdx < (int)st->history.size()) {
                    st->history[histIdx].result = r;
                }
                st->invoking = false;
            }).detach();
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.2f, 1));
        ImGui::Text("⏳ Invoking...");
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset Params", ImVec2(150, 36))) {
        for (size_t i = 0; i < s->params.size(); i++) {
            ParamEditor::reset(s->params[i], s->paramTypes[i]);
        }
        s->result[0] = 0;
    }

    ImGui::Spacing();
    Theme::sectionHeader("Result");
    if (s->result[0]) {
        if (ImGui::Button("Copy Result", ImVec2(150, 28))) {
            yambz_clipboardCopy(s->result);
            Notifications::instance().push("Result copied", 2.0f);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.07f, 1));
        ImGui::BeginChild("##result", ImVec2(0, 200), true);
        ImGui::TextWrapped("%s", s->result);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("(no result yet)");
    }

    // History
    if (!s->history.empty()) {
        Theme::sectionHeader("History (last 20)");
        ImGui::BeginChild("##hist", ImVec2(0, 150), true);
        for (int i = (int)s->history.size() - 1; i >= 0; i--) {
            ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1), "%s", s->history[i].spec.c_str());
            if (ImGui::IsItemClicked()) {
                strncpy(s->spec, s->history[i].spec.c_str(), sizeof(s->spec) - 1);
                s->spec[sizeof(s->spec) - 1] = 0;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("→ %s", s->history[i].result.c_str());
        }
        ImGui::EndChild();
    }
}

} // namespace yambz
