#include "StalkerTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

namespace yambz {

StalkerTab& StalkerTab::instance() { static StalkerTab t; return t; }

// ═══════════════════════════════════════════════════════════
//  State
// ═══════════════════════════════════════════════════════════
struct StalkerState {
    int subTab = 0; // 0=Follow, 1=Events, 2=Filter, 3=Status

    // Follow params
    int followMode = 0; // 0=Symbol, 1=Function, 2=Thread, 3=CurrentThread
    char lib[128] = "libc.so";
    char symbol[128] = "malloc";
    char funcAddr[32] = {0};
    long threadId = 0;

    // Events
    bool evCall = true;
    bool evRet = true;
    bool evExec = false;
    bool evBlock = false;
    bool evCompile = false;

    // Limits
    int timeoutMs = 5000;
    int maxEvents = 100000;

    // Events buffer (pulled from Java)
    struct Ev {
        std::string type;
        uint64_t loc;
        uint64_t tgt;
        int depth;
    };
    std::vector<Ev> events;
    int selectedEvent = -1;
    char searchBuf[128] = {0};

    // Filter
    char includeModule[128] = {0};
    char excludeModule[128] = {0};
    char excludeNamed[128] = {0};
    std::vector<std::string> includes;
    std::vector<std::string> excludes;

    // Status
    bool following = false;
    int mode = 0;
    int stopReason = 0;
    long totalEvents = 0;
    int bufferSize = 0;
    int64_t startTs = 0;
};

static StalkerState& st() { static StalkerState s; return s; }

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

static jclass getCls(JNIEnv* env) {
    jclass cls = env->FindClass("com/yambz/feature/StalkerController");
    if (!cls) env->ExceptionClear();
    return cls;
}

static bool jniBool1(const char* method, int mask, int to, int maxE) {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass c = getCls(env);
    if (!c) return false;
    jmethodID m = env->GetStaticMethodID(c, method, "(III)Z");
    if (!m) { env->ExceptionClear(); return false; }
    jboolean r = env->CallStaticBooleanMethod(c, m, mask, to, maxE);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return r == JNI_TRUE;
}

static bool jniBool2Str(const char* method, const char* a1, const char* a2,
                         int mask, int to, int maxE) {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass c = getCls(env);
    if (!c) return false;
    jmethodID m = env->GetStaticMethodID(c, method,
        "(Ljava/lang/String;Ljava/lang/String;III)Z");
    if (!m) { env->ExceptionClear(); return false; }
    jstring j1 = env->NewStringUTF(a1);
    jstring j2 = env->NewStringUTF(a2);
    jboolean r = env->CallStaticBooleanMethod(c, m, j1, j2, mask, to, maxE);
    env->DeleteLocalRef(j1);
    env->DeleteLocalRef(j2);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return r == JNI_TRUE;
}

static bool jniBoolAddr(long addr, int mask, int to, int maxE) {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass c = getCls(env);
    if (!c) return false;
    jmethodID m = env->GetStaticMethodID(c, "followFunction", "(JIII)Z");
    if (!m) { env->ExceptionClear(); return false; }
    jboolean r = env->CallStaticBooleanMethod(c, m, (jlong) addr, mask, to, maxE);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return r == JNI_TRUE;
}

static bool jniBoolTid(long tid, int mask, int to, int maxE) {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass c = getCls(env);
    if (!c) return false;
    jmethodID m = env->GetStaticMethodID(c, "followThread", "(JIII)Z");
    if (!m) { env->ExceptionClear(); return false; }
    jboolean r = env->CallStaticBooleanMethod(c, m, (jlong) tid, mask, to, maxE);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return r == JNI_TRUE;
}

static void jniVoid(const char* method) {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass c = getCls(env);
    if (!c) return;
    jmethodID m = env->GetStaticMethodID(c, method, "()V");
    if (m) env->CallStaticVoidMethod(c, m);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static void jniVoid1Str(const char* method, const char* arg) {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass c = getCls(env);
    if (!c) return;
    jmethodID m = env->GetStaticMethodID(c, method, "(Ljava/lang/String;)V");
    if (m) {
        jstring j = env->NewStringUTF(arg);
        env->CallStaticVoidMethod(c, m, j);
        env->DeleteLocalRef(j);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static bool jniIsFollowing() {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass c = getCls(env);
    if (!c) return false;
    jmethodID m = env->GetStaticMethodID(c, "isFollowing", "()Z");
    return m && env->CallStaticBooleanMethod(c, m) == JNI_TRUE;
}

static int jniGetBufferSize() {
    JNIEnv* env = getEnv();
    if (!env) return 0;
    jclass c = getCls(env);
    if (!c) return 0;
    jmethodID m = env->GetStaticMethodID(c, "getBufferSize", "()I");
    return m ? env->CallStaticIntMethod(c, m) : 0;
}

static int jniGetStopReason() {
    JNIEnv* env = getEnv();
    if (!env) return 0;
    jclass c = getCls(env);
    if (!c) return 0;
    jmethodID m = env->GetStaticMethodID(c, "getStopReason", "()I");
    return m ? env->CallStaticIntMethod(c, m) : 0;
}

static long jniGetEventCount() {
    JNIEnv* env = getEnv();
    if (!env) return 0;
    jclass c = getCls(env);
    if (!c) return 0;
    jmethodID m = env->GetStaticMethodID(c, "getEventCount", "()J");
    return m ? (long) env->CallStaticLongMethod(c, m) : 0;
}

// Pull events as [type|loc|tgt|depth]
static std::vector<std::string> jniPullEvents(int maxCount) {
    std::vector<std::string> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass c = getCls(env);
    if (!c) return out;
    jmethodID m = env->GetStaticMethodID(c, "getEvents", "(I)[Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return out; }
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(c, m, maxCount);
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

// ═══════════════════════════════════════════════════════════
//  Event mask builder
// ═══════════════════════════════════════════════════════════
static int buildMask(StalkerState& s) {
    int m = 0;
    if (s.evCall)    m |= 0x01;
    if (s.evRet)     m |= 0x02;
    if (s.evExec)    m |= 0x04;
    if (s.evBlock)   m |= 0x08;
    if (s.evCompile) m |= 0x10;
    return m;
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Follow
// ═══════════════════════════════════════════════════════════
static void drawFollow(StalkerState& s) {
    Theme::sectionHeader("Follow Mode");
    if (ImGui::RadioButton("Symbol", &s.followMode, 0)) {}
    ImGui::SameLine();
    if (ImGui::RadioButton("Function (address)", &s.followMode, 1)) {}
    ImGui::SameLine();
    if (ImGui::RadioButton("Thread (tid)", &s.followMode, 2)) {}
    ImGui::SameLine();
    if (ImGui::RadioButton("Current thread", &s.followMode, 3)) {}

    if (s.followMode == 0) {
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Library", s.lib, sizeof(s.lib));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(250);
        ImGui::InputText("Symbol", s.symbol, sizeof(s.symbol));
    } else if (s.followMode == 1) {
        ImGui::SetNextItemWidth(250);
        ImGui::InputTextWithHint("Address", "0x7fff...", s.funcAddr, sizeof(s.funcAddr));
    } else if (s.followMode == 2) {
        ImGui::InputScalar("Thread ID", ImGuiDataType_S64, &s.threadId);
    }

    Theme::sectionHeader("Events");
    ImGui::Checkbox("CALL", &s.evCall); ImGui::SameLine(120);
    ImGui::Checkbox("RET", &s.evRet); ImGui::SameLine(240);
    ImGui::Checkbox("EXEC", &s.evExec);
    ImGui::Checkbox("BLOCK", &s.evBlock); ImGui::SameLine(120);
    ImGui::Checkbox("COMPILE", &s.evCompile);

    Theme::sectionHeader("Limits");
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Timeout (ms)", &s.timeoutMs, 1000, 10000);
    ImGui::SetNextItemWidth(200);
    ImGui::InputInt("Max events", &s.maxEvents, 1000, 100000);

    Theme::sectionHeader("Control");

    bool following = jniIsFollowing();
    s.following = following;

    if (!following) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1));
        if (ImGui::Button("▶ Follow", ImVec2(150, 36))) {
            int mask = buildMask(s);
            bool ok = false;
            switch (s.followMode) {
                case 0: ok = jniBool2Str("followSymbol", s.lib, s.symbol, mask,
                                          s.timeoutMs, s.maxEvents); break;
                case 1: {
                    try {
                        long addr = std::stoll(s.funcAddr, nullptr, 16);
                        ok = jniBoolAddr(addr, mask, s.timeoutMs, s.maxEvents);
                    } catch (...) {}
                    break;
                }
                case 2: ok = jniBoolTid(s.threadId, mask, s.timeoutMs, s.maxEvents); break;
                case 3: ok = jniBool1("followCurrentThread", mask, s.timeoutMs, s.maxEvents); break;
            }
            if (ok) {
                s.startTs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                Notifications::instance().push("Stalker following", 2.0f);
            } else {
                Notifications::instance().push("Stalker failed", 3.0f);
            }
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1));
        if (ImGui::Button("■ Unfollow", ImVec2(150, 36))) {
            jniVoid("unfollow");
            Notifications::instance().push("Stalker stopped", 2.0f);
        }
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Flush Events", ImVec2(150, 36))) {
        jniVoid("flushEvents");
    }
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Events
// ═══════════════════════════════════════════════════════════
static void drawEvents(StalkerState& s) {
    // Pull events from Java
    auto newEvents = jniPullEvents(500);
    for (auto& e : newEvents) {
        // parse "type|loc|tgt|depth"
        size_t p1 = e.find('|');
        size_t p2 = e.find('|', p1 + 1);
        size_t p3 = e.find('|', p2 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0) continue;
        StalkerState::Ev ev;
        ev.type = e.substr(0, p1);
        try {
            ev.loc = std::stoull(e.substr(p1 + 1, p2 - p1 - 1), nullptr, 16);
            ev.tgt = std::stoull(e.substr(p2 + 1, p3 - p2 - 1), nullptr, 16);
            ev.depth = std::stoi(e.substr(p3 + 1));
        } catch (...) { continue; }
        s.events.push_back(ev);
    }
    while (s.events.size() > 5000) s.events.erase(s.events.begin());

    ImGui::Text("Events: %zu", s.events.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", "filter...", s.searchBuf, sizeof(s.searchBuf));
    ImGui::SameLine();
    if (ImGui::Button("Clear")) s.events.clear();
    ImGui::Separator();

    std::string q = s.searchBuf;
    for (auto& c : q) c = (char)std::tolower((unsigned char)c);

    float listW = ImGui::GetContentRegionAvail().x * 0.55f;
    ImGui::BeginChild("##evlist", ImVec2(listW, 0), true);

    ImVec4 colorForType(const std::string&);
    ImGuiListClipper clip;
    clip.Begin((int)s.events.size(), 18.0f);
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            const auto& ev = s.events[k];
            if (!q.empty()) {
                std::string h = ev.type;
                for (auto& c : h) c = (char)std::tolower((unsigned char)c);
                if (h.find(q) == std::string::npos) continue;
            }
            ImVec4 col = ImVec4(0.7f, 0.7f, 0.7f, 1);
            if (ev.type == "CALL")    col = ImVec4(0.4f, 0.8f, 0.4f, 1);
            else if (ev.type == "RET") col = ImVec4(0.4f, 0.6f, 0.95f, 1);
            else if (ev.type == "EXEC") col = ImVec4(0.7f, 0.7f, 0.4f, 1);
            else if (ev.type == "BLOCK") col = ImVec4(0.9f, 0.6f, 0.3f, 1);
            else if (ev.type == "COMPILE") col = ImVec4(0.6f, 0.9f, 0.9f, 1);

            ImGui::PushID(k);
            ImGui::TextColored(col, "[%s]", ev.type.c_str());
            ImGui::SameLine();
            char line[128];
            snprintf(line, sizeof line, "0x%llx → 0x%llx (d=%d)",
                (unsigned long long) ev.loc,
                (unsigned long long) ev.tgt,
                ev.depth);
            bool sel = (s.selectedEvent == k);
            if (ImGui::Selectable(line, sel)) s.selectedEvent = k;
            ImGui::PopID();
        }
    }
    clip.End();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    if (s.selectedEvent >= 0 && s.selectedEvent < (int)s.events.size()) {
        const auto& ev = s.events[s.selectedEvent];
        Theme::sectionHeader("Event Detail");
        ImGui::Text("Type: %s", ev.type.c_str());
        ImGui::Text("Location: 0x%llx", (unsigned long long)ev.loc);
        ImGui::Text("Target: 0x%llx", (unsigned long long)ev.tgt);
        ImGui::Text("Depth: %d", ev.depth);
    } else {
        ImGui::TextDisabled("(select an event)");
    }
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Filter (include/exclude)
// ═══════════════════════════════════════════════════════════
static void drawFilter(StalkerState& s) {
    Theme::sectionHeader("Include / Exclude");

    ImGui::Text("Include Module:");
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("##inc", s.includeModule, sizeof(s.includeModule));
    ImGui::SameLine();
    if (ImGui::Button("Add Include")) {
        if (s.includeModule[0]) {
            jniVoid1Str("includeModule", s.includeModule);
            s.includes.push_back(s.includeModule);
        }
    }

    ImGui::Text("Exclude Module:");
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("##exc", s.excludeModule, sizeof(s.excludeModule));
    ImGui::SameLine();
    if (ImGui::Button("Add Exclude")) {
        if (s.excludeModule[0]) {
            jniVoid1Str("excludeModule", s.excludeModule);
            s.excludes.push_back(s.excludeModule);
        }
    }

    ImGui::Text("Exclude Named:");
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("##excn", s.excludeNamed, sizeof(s.excludeNamed));
    ImGui::SameLine();
    if (ImGui::Button("Add Exclude Named")) {
        if (s.excludeNamed[0]) {
            jniVoid1Str("excludeNamed", s.excludeNamed);
            s.excludes.push_back("[named] " + std::string(s.excludeNamed));
        }
    }

    ImGui::Separator();
    Theme::sectionHeader("Active Includes");
    for (auto& i : s.includes) ImGui::BulletText("%s", i.c_str());
    Theme::sectionHeader("Active Excludes");
    for (auto& e : s.excludes) ImGui::BulletText("%s", e.c_str());

    ImGui::Separator();
    if (ImGui::Button("Clear Inclusions", ImVec2(150, 32))) {
        jniVoid("clearInclusions");
        s.includes.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Exclusions", ImVec2(150, 32))) {
        jniVoid("clearExclusions");
        s.excludes.clear();
    }
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Status
// ═══════════════════════════════════════════════════════════
static void drawStatus(StalkerState& s) {
    s.following = jniIsFollowing();
    s.bufferSize = jniGetBufferSize();
    s.stopReason = jniGetStopReason();
    s.totalEvents = jniGetEventCount();

    Theme::sectionHeader("Status");
    ImGui::Text("Following: %s", s.following ? "YES" : "NO");
    ImGui::Text("Buffer size: %d", s.bufferSize);
    ImGui::Text("Total events (this window): %ld", s.totalEvents);
    ImGui::Text("Stop reason: %d", s.stopReason);

    const char* reasonName = "none";
    if (s.stopReason == 1) reasonName = "timeout";
    else if (s.stopReason == 2) reasonName = "max_events";
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", reasonName);

    ImGui::Separator();
    if (ImGui::Button("Refresh", ImVec2(120, 32))) {
        // Force refresh by re-reading
    }
    ImGui::SameLine();
    if (ImGui::Button("Flush", ImVec2(120, 32))) jniVoid("flushEvents");
}

// ═══════════════════════════════════════════════════════════
//  Main draw
// ═══════════════════════════════════════════════════════════
void StalkerTab::draw() {
    auto& s = st();
    const char* subs[] = {"Follow", "Events", "Filter", "Status"};
    for (int i = 0; i < 4; i++) {
        if (i > 0) ImGui::SameLine();
        bool sel = (s.subTab == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.24f, 0.1f, 1));
        if (ImGui::Button(subs[i], ImVec2(120, 32))) s.subTab = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    switch (s.subTab) {
        case 0: drawFollow(s); break;
        case 1: drawEvents(s); break;
        case 2: drawFilter(s); break;
        case 3: drawStatus(s); break;
    }
}

} // namespace yambz
