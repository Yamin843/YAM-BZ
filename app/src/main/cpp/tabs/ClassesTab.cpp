#include "ClassesTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "core/MemberSpec.h"
#include "trace/OceanState.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <vector>

namespace yambz {

struct ClassesTab::State {
    // Classes
    std::vector<std::string> classes;
    mutable std::mutex classesMu;
    bool enumerating = false;
    bool complete = false;

    // Selected
    std::string selectedClass;
    std::vector<std::string> selectedMembers;
    mutable std::mutex membersMu;
    int selectedMemberIdx = -1;

    // UI
    int currentTab = 0;  // 0=Browse, 1=Search
    int filterKind = 0;  // 0=all, 1=classes, 2=methods, 3=fields
    char searchBuf[128] = {0};
    char memberSearchBuf[128] = {0};

    // Search sub-tab
    int searchSubTab = 0; // 0=Field, 1=MethodCaller, 2=SubClass
    char searchFieldCls[256] = {0};
    char searchFieldName[128] = {0};
    int searchFieldOp = 3;  // 1=read, 2=write, 3=all
    char searchMethodCls[256] = {0};
    char searchMethodName[128] = {0};
    char searchMethodArgs[256] = {0};
    char searchSubClassCls[256] = {0};
    char searchResult[4096] = {0};
};

ClassesTab& ClassesTab::instance() { static ClassesTab t; return t; }
ClassesTab::State* ClassesTab::st() { static State s; return &s; }

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

static jclass getClassBridge(JNIEnv* env) {
    jclass cls = env->FindClass("com/yambz/bridge/ClassBridge");
    if (!cls) env->ExceptionClear();
    return cls;
}

static void jniStartEnum() {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass cls = getClassBridge(env);
    if (!cls) return;
    jmethodID mid = env->GetStaticMethodID(cls, "startEnum", "()V");
    if (mid) env->CallStaticVoidMethod(cls, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static bool jniIsEnumerating() {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass cls = getClassBridge(env);
    if (!cls) return false;
    jmethodID mid = env->GetStaticMethodID(cls, "isEnumerating", "()Z");
    return mid && env->CallStaticBooleanMethod(cls, mid) == JNI_TRUE;
}

static bool jniIsComplete() {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass cls = getClassBridge(env);
    if (!cls) return false;
    jmethodID mid = env->GetStaticMethodID(cls, "isComplete", "()Z");
    return mid && env->CallStaticBooleanMethod(cls, mid) == JNI_TRUE;
}

static int jniGetTotal() {
    JNIEnv* env = getEnv();
    if (!env) return 0;
    jclass cls = getClassBridge(env);
    if (!cls) return 0;
    jmethodID mid = env->GetStaticMethodID(cls, "getTotal", "()I");
    return mid ? env->CallStaticIntMethod(cls, mid) : 0;
}

static std::vector<std::string> jniNextBatch(int n) {
    std::vector<std::string> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass cls = getClassBridge(env);
    if (!cls) return out;
    jmethodID mid = env->GetStaticMethodID(cls, "nextBatch", "(I)[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return out; }
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid, n);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return out; }
    jsize len = env->GetArrayLength(arr);
    out.reserve(len);
    for (jsize i = 0; i < len; i++) {
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

static std::vector<std::string> jniGetMembers(const std::string& className) {
    std::vector<std::string> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass cls = getClassBridge(env);
    if (!cls) return out;
    jmethodID mid = env->GetStaticMethodID(cls, "getMembers",
        "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return out; }
    jstring jcls = env->NewStringUTF(className.c_str());
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid, jcls);
    env->DeleteLocalRef(jcls);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return out; }
    jsize len = env->GetArrayLength(arr);
    out.reserve(len);
    for (jsize i = 0; i < len; i++) {
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

static std::string jniCallStringArr(const char* method, const char* a1, const char* a2, int i) {
    JNIEnv* env = getEnv();
    if (!env) return "";
    jclass cls = getClassBridge(env);
    if (!cls) return "";
    jmethodID mid = env->GetStaticMethodID(cls, method,
        "(Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return ""; }
    jstring j1 = env->NewStringUTF(a1);
    jstring j2 = env->NewStringUTF(a2);
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid, j1, j2, i);
    env->DeleteLocalRef(j1);
    env->DeleteLocalRef(j2);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return ""; }
    jsize len = env->GetArrayLength(arr);
    std::string result;
    for (jsize k = 0; k < len; k++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, k);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) { result += c; result += "\n"; }
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return result;
}

static std::string jniCallString1(const char* method, const char* arg) {
    JNIEnv* env = getEnv();
    if (!env) return "";
    jclass cls = getClassBridge(env);
    if (!cls) return "";
    jmethodID mid = env->GetStaticMethodID(cls, method,
        "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return ""; }
    jstring ja = env->NewStringUTF(arg);
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid, ja);
    env->DeleteLocalRef(ja);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return ""; }
    jsize len = env->GetArrayLength(arr);
    std::string result;
    for (jsize k = 0; k < len; k++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, k);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) { result += c; result += "\n"; }
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return result;
}

static std::string jniCallMethodCaller(const char* cls, const char* m, const char* args) {
    JNIEnv* env = getEnv();
    if (!env) return "";
    jclass jc = getClassBridge(env);
    if (!jc) return "";
    jmethodID mid = env->GetStaticMethodID(jc, "searchMethodCaller",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return ""; }
    jstring j1 = env->NewStringUTF(cls);
    jstring j2 = env->NewStringUTF(m);
    jstring j3 = env->NewStringUTF(args);
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(jc, mid, j1, j2, j3);
    env->DeleteLocalRef(j1); env->DeleteLocalRef(j2); env->DeleteLocalRef(j3);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return ""; }
    jsize len = env->GetArrayLength(arr);
    std::string result;
    for (jsize k = 0; k < len; k++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, k);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) { result += c; result += "\n"; }
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return result;
}

// ═══════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════
void ClassesTab::draw() {
    auto* s = st();

    // Sub-tabs
    const char* subTabs[] = {"Browse", "Search"};
    for (int i = 0; i < 2; i++) {
        if (i > 0) ImGui::SameLine();
        bool sel = (s->currentTab == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.24f, 0.1f, 1.0f));
        if (ImGui::Button(subTabs[i], ImVec2(120, 32))) s->currentTab = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (s->currentTab == 0) {
        // ═══════════════════════════════════════════════════════
        //  Browse sub-tab
        // ═══════════════════════════════════════════════════════
        if (!s->enumerating && !s->complete) {
            if (ImGui::Button("▶ Start Enumeration", ImVec2(200, 32))) {
                {
                    std::lock_guard<std::mutex> lk(s->classesMu);
                    s->classes.clear();
                }
                s->complete = false;
                s->enumerating = true;
                jniStartEnum();
            }
        } else if (s->enumerating) {
            ImGui::Text("⏳ Enumerating... (%d found)", jniGetTotal());
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "✓ %d classes", jniGetTotal());
            ImGui::SameLine();
            if (ImGui::Button("Refresh", ImVec2(100, 32))) {
                {
                    std::lock_guard<std::mutex> lk(s->classesMu);
                    s->classes.clear();
                }
                s->complete = false;
                s->enumerating = true;
                jniStartEnum();
            }
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(250);
        ImGui::InputTextWithHint("##search", "search...",
            s->searchBuf, sizeof(s->searchBuf));

        // Drain batches
        if (s->enumerating) {
            auto batch = jniNextBatch(500);
            if (!batch.empty()) {
                std::lock_guard<std::mutex> lk(s->classesMu);
                for (auto& n : batch) s->classes.push_back(std::move(n));
            }
            s->complete = jniIsComplete();
            if (s->complete) s->enumerating = false;
        }

        ImGui::Separator();

        // Layout
        float leftW = ImGui::GetContentRegionAvail().x * 0.4f;

        // Filter
        std::string q = s->searchBuf;
        for (auto& c : q) c = (char)std::tolower((unsigned char)c);

        std::vector<size_t> visible;
        {
            std::lock_guard<std::mutex> lk(s->classesMu);
            visible.reserve(s->classes.size());
            for (size_t i = 0; i < s->classes.size(); i++) {
                if (!q.empty()) {
                    std::string sl = s->classes[i];
                    for (auto& c : sl) c = (char)std::tolower((unsigned char)c);
                    if (sl.find(q) == std::string::npos) continue;
                }
                visible.push_back(i);
            }
        }

        // Classes list
        ImGui::BeginChild("##cls", ImVec2(leftW, 0), true);
        ImGui::Text("Classes (%zu)", visible.size());
        ImGui::Separator();
        ImGuiListClipper clip;
        clip.Begin((int)visible.size(), 18.0f);
        while (clip.Step()) {
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                size_t idx = visible[k];
                std::string name;
                {
                    std::lock_guard<std::mutex> lk(s->classesMu);
                    if (idx >= s->classes.size()) continue;
                    name = s->classes[idx];
                }
                bool sel = (s->selectedClass == name);
                if (ImGui::Selectable(name.c_str(), sel)) {
                    s->selectedClass = name;
                    s->selectedMembers = jniGetMembers(name);
                    s->selectedMemberIdx = -1;
                }
            }
        }
        clip.End();
        ImGui::EndChild();
        ImGui::SameLine();

        // Members
        ImGui::BeginChild("##mem", ImVec2(0, 0), true);
        if (s->selectedClass.empty()) {
            ImGui::TextDisabled("(select a class)");
        } else {
            ImGui::TextColored(ImVec4(0.83f, 0.63f, 0.09f, 1), "%s", s->selectedClass.c_str());
            ImGui::Separator();
            ImGui::SetNextItemWidth(200);
            ImGui::InputTextWithHint("##msearch", "filter members...",
                s->memberSearchBuf, sizeof(s->memberSearchBuf));

            std::string mq = s->memberSearchBuf;
            for (auto& c : mq) c = (char)std::tolower((unsigned char)c);

            std::vector<size_t> mVis;
            for (size_t i = 0; i < s->selectedMembers.size(); i++) {
                if (!mq.empty()) {
                    std::string sl = s->selectedMembers[i];
                    for (auto& c : sl) c = (char)std::tolower((unsigned char)c);
                    if (sl.find(mq) == std::string::npos) continue;
                }
                mVis.push_back(i);
            }

            ImGui::Separator();
            ImGuiListClipper mclip;
            mclip.Begin((int)mVis.size(), 22.0f);
            while (mclip.Step()) {
                for (int k = mclip.DisplayStart; k < mclip.DisplayEnd; k++) {
                    size_t idx = mVis[k];
                    const auto& m = s->selectedMembers[idx];
                    char kind = m[0];

                    ImVec4 col = (kind == 'M') ? ImVec4(0.6f, 0.85f, 0.6f, 1)
                              : (kind == 'F') ? ImVec4(0.6f, 0.6f, 0.85f, 1)
                              : ImVec4(0.85f, 0.6f, 0.85f, 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    bool sel = (s->selectedMemberIdx == (int)idx);
                    if (ImGui::Selectable(m.c_str(), sel)) {
                        s->selectedMemberIdx = (int)idx;
                    }
                    ImGui::PopStyleColor();

                    if (ImGui::IsItemHovered()) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Trace")) {
                            setTraceTarget(s->selectedClass, m);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Call")) {
                            setCallTarget(s->selectedClass, m);
                        }
                    }
                }
            }
            mclip.End();
        }
        ImGui::EndChild();
    } else {
        // ═══════════════════════════════════════════════════════
        //  Search sub-tab
        // ═══════════════════════════════════════════════════════
        const char* searchTabs[] = {"Field", "MethodCaller", "SubClass"};
        for (int i = 0; i < 3; i++) {
            if (i > 0) ImGui::SameLine();
            bool sel = (s->searchSubTab == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.24f, 0.1f, 1.0f));
            if (ImGui::Button(searchTabs[i], ImVec2(140, 32))) s->searchSubTab = i;
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::Separator();

        if (s->searchSubTab == 0) {
            // Field
            ImGui::Text("Find field writers/readers");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("Class", s->searchFieldCls, sizeof(s->searchFieldCls));
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("Field", s->searchFieldName, sizeof(s->searchFieldName));
            ImGui::SetNextItemWidth(200);
            ImGui::Combo("Operation", &s->searchFieldOp, "All\0Read\0Write\0");

            if (ImGui::Button("Search", ImVec2(120, 32))) {
                std::string r = jniCallStringArr("searchField",
                    s->searchFieldCls, s->searchFieldName, s->searchFieldOp);
                if (r.length() > sizeof(s->searchResult) - 1) {
                    r = r.substr(0, sizeof(s->searchResult) - 1);
                }
                strncpy(s->searchResult, r.c_str(), sizeof(s->searchResult) - 1);
                s->searchResult[sizeof(s->searchResult) - 1] = '\0';
            }
        } else if (s->searchSubTab == 1) {
            // MethodCaller
            ImGui::Text("Find method callers");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("Class", s->searchMethodCls, sizeof(s->searchMethodCls));
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("Method", s->searchMethodName, sizeof(s->searchMethodName));
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("Args (int,String)", s->searchMethodArgs, sizeof(s->searchMethodArgs));

            if (ImGui::Button("Search", ImVec2(120, 32))) {
                std::string r = jniCallMethodCaller(
                    s->searchMethodCls, s->searchMethodName, s->searchMethodArgs);
                if (r.length() > sizeof(s->searchResult) - 1) {
                    r = r.substr(0, sizeof(s->searchResult) - 1);
                }
                strncpy(s->searchResult, r.c_str(), sizeof(s->searchResult) - 1);
                s->searchResult[sizeof(s->searchResult) - 1] = '\0';
            }
        } else {
            // SubClass
            ImGui::Text("Find subclasses");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("Parent Class", s->searchSubClassCls, sizeof(s->searchSubClassCls));
            if (ImGui::Button("Search", ImVec2(120, 32))) {
                std::string r = jniCallString1("searchSubClass", s->searchSubClassCls);
                if (r.length() > sizeof(s->searchResult) - 1) {
                    r = r.substr(0, sizeof(s->searchResult) - 1);
                }
                strncpy(s->searchResult, r.c_str(), sizeof(s->searchResult) - 1);
                s->searchResult[sizeof(s->searchResult) - 1] = '\0';
            }
        }

        ImGui::Separator();
        ImGui::Text("Results:");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.07f, 1));
        ImGui::BeginChild("##results", ImVec2(0, 0), true);
        ImGui::TextWrapped("%s", s->searchResult[0] ? s->searchResult : "(no results)");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

// ═══════════════════════════════════════════════════════════
//  Target setters
// ═══════════════════════════════════════════════════════════
void ClassesTab::setTraceTarget(const std::string& cls, const std::string& member) {
    auto& cfg = oceanUI().config;
    cfg.mode = 2;
    std::string spec = buildMemberSpec(cls, member);
    strncpy(cfg.memberSpec, spec.c_str(), sizeof(cfg.memberSpec) - 1);
    cfg.memberSpec[sizeof(cfg.memberSpec) - 1] = '\0';
    oceanUI().currentTab = 0;
    Notifications::instance().push(("Trace target: " + spec).c_str(), 3.0f);
    LOGI("Trace target set: %s", spec.c_str());
}

void ClassesTab::setCallTarget(const std::string& cls, const std::string& member) {
    std::string spec = buildMemberSpec(cls, member);
    // Will be wired to CallTab in next response
    Notifications::instance().push(("Call target: " + spec).c_str(), 3.0f);
    LOGI("Call target set: %s", spec.c_str());
}

} // namespace yambz
