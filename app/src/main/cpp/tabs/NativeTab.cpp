#include "NativeTab.h"
#include "ui/Theme.h"
#include "ui/Notifications.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <imgui.h>
#include <cstring>
#include <string>
#include <vector>

namespace yambz {

NativeTab& NativeTab::instance() { static NativeTab t; return t; }

// ═══════════════════════════════════════════════════════════
//  State
// ═══════════════════════════════════════════════════════════
struct NativeState {
    int subTab = 0; // 0=Libs, 1=Hooks, 2=CModule, 3=Libc, 4=CallByAddr

    // Libs
    std::vector<std::string> libraries;
    std::vector<std::string> symbols;
    int selectedLib = -1;
    char libSearch[128] = {0};

    // Hooks
    char hookLib[128] = {0};
    char hookSymbol[128] = {0};
    std::vector<std::string> activeHooks;

    // CModule
    char cmoduleSource[8192] = {
        "#include <albatross.h>\n"
        "#include <string.h>\n"
        "\n"
        "int add(int a, int b) { return a + b; }\n"
        "int strlen_hex(const char* s) { return (int) strlen(s); }\n"
    };
    char cmoduleResult[2048] = {0};
    char callSymbol[128] = {0};
    char callSig[128] = "int(int,int)";
    char callArgs[512] = "[2, 3]";
    char callResult[512] = {0};

    // Libc
    char libcPath[512] = {0};
    int libcFlags = 0; // O_RDONLY
    int libcFd = -1;
    char libcWriteData[512] = "hello\n";
    int libcReadLen = 256;
    char libcResult[1024] = {0};

    // CallByAddr
    char addrInput[32] = {0};
    char addrSig[128] = "int(int,int)";
    char addrArgs[256] = "[1, 2]";
    char addrResult[512] = {0};

    // HookNative (annotation-based)
    char hookNativeCls[256] = {0};
    char hookNativeLib[128] = {0};
    char hookNativeResult[512] = {0};
};

static NativeState& st() { static NativeState s; return s; }

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

static std::vector<std::string> jniStrArray(const char* method, const char* arg) {
    std::vector<std::string> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
    if (!c) { env->ExceptionClear(); return out; }
    jmethodID m = arg
        ? env->GetStaticMethodID(c, method, "(Ljava/lang/String;)[Ljava/lang/String;")
        : env->GetStaticMethodID(c, method, "()[Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return out; }
    jobjectArray arr;
    if (arg) {
        jstring ja = env->NewStringUTF(arg);
        arr = (jobjectArray) env->CallStaticObjectMethod(c, m, ja);
        env->DeleteLocalRef(ja);
    } else {
        arr = (jobjectArray) env->CallStaticObjectMethod(c, m);
    }
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

static std::string jniCall1Str(const char* method, const char* a1) {
    JNIEnv* env = getEnv();
    if (!env) return "";
    jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
    if (!c) { env->ExceptionClear(); return ""; }
    jmethodID m = env->GetStaticMethodID(c, method, "(Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return ""; }
    jstring ja = env->NewStringUTF(a1);
    jstring r = (jstring) env->CallStaticObjectMethod(c, m, ja);
    env->DeleteLocalRef(ja);
    if (env->ExceptionCheck() || !r) { env->ExceptionClear(); return ""; }
    const char* s = env->GetStringUTFChars(r, nullptr);
    std::string out = s ? s : "";
    if (s) env->ReleaseStringUTFChars(r, s);
    env->DeleteLocalRef(r);
    return out;
}

static std::string jniCall2Str(const char* method, const char* a1, const char* a2) {
    JNIEnv* env = getEnv();
    if (!env) return "";
    jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
    if (!c) { env->ExceptionClear(); return ""; }
    jmethodID m = env->GetStaticMethodID(c, method,
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return ""; }
    jstring j1 = env->NewStringUTF(a1);
    jstring j2 = env->NewStringUTF(a2);
    jstring r = (jstring) env->CallStaticObjectMethod(c, m, j1, j2);
    env->DeleteLocalRef(j1);
    env->DeleteLocalRef(j2);
    if (env->ExceptionCheck() || !r) { env->ExceptionClear(); return ""; }
    const char* s = env->GetStringUTFChars(r, nullptr);
    std::string out = s ? s : "";
    if (s) env->ReleaseStringUTFChars(r, s);
    env->DeleteLocalRef(r);
    return out;
}

static std::string jniCall3Str(const char* method, const char* a1, const char* a2, const char* a3) {
    JNIEnv* env = getEnv();
    if (!env) return "";
    jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
    if (!c) { env->ExceptionClear(); return ""; }
    jmethodID m = env->GetStaticMethodID(c, method,
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { env->ExceptionClear(); return ""; }
    jstring j1 = env->NewStringUTF(a1);
    jstring j2 = env->NewStringUTF(a2);
    jstring j3 = env->NewStringUTF(a3);
    jstring r = (jstring) env->CallStaticObjectMethod(c, m, j1, j2, j3);
    env->DeleteLocalRef(j1);
    env->DeleteLocalRef(j2);
    env->DeleteLocalRef(j3);
    if (env->ExceptionCheck() || !r) { env->ExceptionClear(); return ""; }
    const char* s = env->GetStringUTFChars(r, nullptr);
    std::string out = s ? s : "";
    if (s) env->ReleaseStringUTFChars(r, s);
    env->DeleteLocalRef(r);
    return out;
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Libraries
// ═══════════════════════════════════════════════════════════
static void drawLibs(NativeState& s) {
    if (ImGui::Button("Refresh Libraries", ImVec2(180, 32))) {
        s.libraries = jniStrArray("listLibraries", nullptr);
        s.selectedLib = -1;
        s.symbols.clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("##search", "search...", s.libSearch, sizeof(s.libSearch));

    float leftW = ImGui::GetContentRegionAvail().x * 0.35f;

    ImGui::BeginChild("##libs", ImVec2(leftW, 0), true);
    ImGui::Text("Libraries (%zu)", s.libraries.size());
    ImGui::Separator();
    for (size_t i = 0; i < s.libraries.size(); i++) {
        bool sel = (s.selectedLib == (int)i);
        if (ImGui::Selectable(s.libraries[i].c_str(), sel)) {
            s.selectedLib = (int)i;
            // Parse "libfoo.so|0xADDR" — use libname part only
            std::string lib = s.libraries[i];
            size_t p = lib.find('|');
            if (p != std::string::npos) lib = lib.substr(0, p);
            s.symbols = jniStrArray("listSymbols", lib.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##syms", ImVec2(0, 0), true);
    ImGui::Text("Symbols (%zu)", s.symbols.size());
    ImGui::Separator();
    std::string q = s.libSearch;
    for (auto& c : q) c = (char)std::tolower((unsigned char)c);
    ImGuiListClipper clip;
    clip.Begin((int)s.symbols.size(), 16.0f);
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            const auto& sym = s.symbols[k];
            if (!q.empty()) {
                std::string sl = sym;
                for (auto& c : sl) c = (char)std::tolower((unsigned char)c);
                if (sl.find(q) == std::string::npos) continue;
            }
            ImGui::TextUnformatted(sym.c_str());
        }
    }
    clip.End();
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Hooks
// ═══════════════════════════════════════════════════════════
static void drawHooks(NativeState& s) {
    Theme::sectionHeader("Install hookInstruction");
    ImGui::TextDisabled("Hook a C function at entry (logs args to logcat)");
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("Library", "libc.so", s.hookLib, sizeof(s.hookLib));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("Symbol", "malloc", s.hookSymbol, sizeof(s.hookSymbol));

    if (ImGui::Button("Install Hook", ImVec2(150, 32))) {
        if (s.hookLib[0] && s.hookSymbol[0]) {
            std::string r = jniCall2Str("addHookInstruction", s.hookLib, s.hookSymbol);
            Notifications::instance().push(r.c_str(), 3.0f);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh List", ImVec2(150, 32))) {
        s.activeHooks = jniStrArray("listHooks", nullptr);
    }
    ImGui::SameLine();
    if (ImGui::Button("Unhook All", ImVec2(150, 32))) {
        std::string r = jniCall1Str("unhookAll", "");
        s.activeHooks = jniStrArray("listHooks", nullptr);
        Notifications::instance().push("All hooks removed", 2.0f);
    }

    ImGui::Separator();
    Theme::sectionHeader("Active Hooks");
    ImGui::Text("Count: %zu", s.activeHooks.size());
    for (auto& h : s.activeHooks) {
        ImGui::BulletText("%s", h.c_str());
    }

    ImGui::Separator();
    Theme::sectionHeader("Annotation-based hookNative");
    ImGui::TextDisabled("Hook a class with @TargetLibrary/@FuncHook/@FuncBackup");
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Hooker Class", s.hookNativeCls, sizeof(s.hookNativeCls));
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("Library (optional)", s.hookNativeLib, sizeof(s.hookNativeLib));

    if (ImGui::Button("Hook Native", ImVec2(150, 32))) {
        if (s.hookNativeCls[0]) {
            std::string r = jniCall2Str("hookNativeClass", s.hookNativeCls, s.hookNativeLib);
            strncpy(s.hookNativeResult, r.c_str(), sizeof(s.hookNativeResult) - 1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Unhook Native", ImVec2(150, 32))) {
        if (s.hookNativeCls[0]) {
            std::string r = jniCall2Str("unhookNativeClass", s.hookNativeCls, s.hookNativeLib);
            strncpy(s.hookNativeResult, r.c_str(), sizeof(s.hookNativeResult) - 1);
        }
    }
    if (s.hookNativeResult[0]) {
        ImGui::TextWrapped("%s", s.hookNativeResult);
    }
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: CModule
// ═══════════════════════════════════════════════════════════
static void drawCModule(NativeState& s) {
    Theme::sectionHeader("C Source (tcc in-process)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##src", s.cmoduleSource, sizeof(s.cmoduleSource),
                               ImVec2(-1, 280));

    if (ImGui::Button("Compile", ImVec2(150, 36))) {
        std::string r = jniCall1Str("compileC", s.cmoduleSource);
        strncpy(s.cmoduleResult, r.c_str(), sizeof(s.cmoduleResult) - 1);
        Notifications::instance().push(r == "OK" ? "Compiled OK" : "Compile failed", 3.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Source", ImVec2(150, 36))) {
        s.cmoduleSource[0] = 0;
    }

    ImGui::Separator();
    Theme::sectionHeader("Call Function");
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("Symbol", s.callSymbol, sizeof(s.callSymbol));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    ImGui::InputText("Signature", s.callSig, sizeof(s.callSig));
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Args (JSON)", s.callArgs, sizeof(s.callArgs));

    if (ImGui::Button("Call", ImVec2(120, 32))) {
        if (s.callSymbol[0]) {
            std::string r = jniCall3Str("callCFunction", s.callSymbol, s.callSig, s.callArgs);
            strncpy(s.callResult, r.c_str(), sizeof(s.callResult) - 1);
        }
    }

    ImGui::Separator();
    Theme::sectionHeader("Results");
    if (s.cmoduleResult[0]) {
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Compile: %s", s.cmoduleResult);
    }
    if (s.callResult[0]) {
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "Call: %s", s.callResult);
    }
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: Libc
// ═══════════════════════════════════════════════════════════
static void showIntAsString(char* buf, size_t sz, int v) {
    snprintf(buf, sz, "%d", v);
}

static void drawLibc(NativeState& s) {
    Theme::sectionHeader("Open File");
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Path", s.libcPath, sizeof(s.libcPath));
    ImGui::SetNextItemWidth(200);
    const char* flagNames[] = {"O_RDONLY", "O_WRONLY", "O_RDWR"};
    int flags[] = {0, 1, 2};
    int fi = 0;
    for (int i = 0; i < 3; i++) if (flags[i] == s.libcFlags) fi = i;
    if (ImGui::Combo("Flags", &fi, flagNames, 3)) s.libcFlags = flags[fi];

    if (ImGui::Button("Open", ImVec2(120, 32))) {
        std::string cmd = std::to_string(s.libcFlags);
        // We call libcOpen(path, flags)
        JNIEnv* env = getEnv();
        if (env) {
            jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
            if (c) {
                jmethodID m = env->GetStaticMethodID(c, "libcOpen",
                    "(Ljava/lang/String;I)Ljava/lang/String;");
                if (m) {
                    jstring jp = env->NewStringUTF(s.libcPath);
                    jstring r = (jstring) env->CallStaticObjectMethod(c, m, jp, s.libcFlags);
                    env->DeleteLocalRef(jp);
                    if (!env->ExceptionCheck() && r) {
                        const char* rs = env->GetStringUTFChars(r, nullptr);
                        std::string rsStr = rs ? rs : "";
                        if (rs) env->ReleaseStringUTFChars(r, rs);
                        env->DeleteLocalRef(r);
                        // parse fd from "fd=N"
                        size_t eq = rsStr.find('=');
                        if (eq != std::string::npos) {
                            try { s.libcFd = std::stoi(rsStr.substr(eq + 1)); } catch (...) {}
                        }
                        strncpy(s.libcResult, rsStr.c_str(), sizeof(s.libcResult) - 1);
                    }
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }
            } else env->ExceptionClear();
        }
    }

    Theme::sectionHeader("Write");
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Data", s.libcWriteData, sizeof(s.libcWriteData));
    if (ImGui::Button("Write", ImVec2(120, 32))) {
        if (s.libcFd >= 0) {
            JNIEnv* env = getEnv();
            if (env) {
                jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
                if (c) {
                    jmethodID m = env->GetStaticMethodID(c, "libcWrite",
                        "(ILjava/lang/String;)Ljava/lang/String;");
                    if (m) {
                        jstring jd = env->NewStringUTF(s.libcWriteData);
                        jstring r = (jstring) env->CallStaticObjectMethod(c, m, s.libcFd, jd);
                        env->DeleteLocalRef(jd);
                        if (!env->ExceptionCheck() && r) {
                            const char* rs = env->GetStringUTFChars(r, nullptr);
                            strncpy(s.libcResult, rs ? rs : "", sizeof(s.libcResult) - 1);
                            if (rs) env->ReleaseStringUTFChars(r, rs);
                            env->DeleteLocalRef(r);
                        }
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    }
                } else env->ExceptionClear();
            }
        }
    }

    Theme::sectionHeader("Read");
    ImGui::SetNextItemWidth(150);
    ImGui::InputInt("Max len", &s.libcReadLen);
    if (ImGui::Button("Read", ImVec2(120, 32))) {
        if (s.libcFd >= 0) {
            JNIEnv* env = getEnv();
            if (env) {
                jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
                if (c) {
                    jmethodID m = env->GetStaticMethodID(c, "libcRead",
                        "(II)Ljava/lang/String;");
                    if (m) {
                        jstring r = (jstring) env->CallStaticObjectMethod(c, m,
                            s.libcFd, s.libcReadLen);
                        if (!env->ExceptionCheck() && r) {
                            const char* rs = env->GetStringUTFChars(r, nullptr);
                            strncpy(s.libcResult, rs ? rs : "", sizeof(s.libcResult) - 1);
                            if (rs) env->ReleaseStringUTFChars(r, rs);
                            env->DeleteLocalRef(r);
                        }
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    }
                } else env->ExceptionClear();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 32))) {
        if (s.libcFd >= 0) {
            std::string fdStr = std::to_string(s.libcFd);
            std::string r = jniCall1Str("libcClose", fdStr.c_str());
            strncpy(s.libcResult, r.c_str(), sizeof(s.libcResult) - 1);
            s.libcFd = -1;
        }
    }

    ImGui::Separator();
    Theme::sectionHeader("Result");
    ImGui::TextWrapped("%s", s.libcResult[0] ? s.libcResult : "(no result)");
}

// ═══════════════════════════════════════════════════════════
//  Sub-tab: CallByAddr
// ═══════════════════════════════════════════════════════════
static void drawCallByAddr(NativeState& s) {
    Theme::sectionHeader("Call Native Address");
    ImGui::TextDisabled("Call any address with declared signature");
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("Address", "0x7fff12345678", s.addrInput, sizeof(s.addrInput));
    ImGui::SetNextItemWidth(250);
    ImGui::InputText("Signature", s.addrSig, sizeof(s.addrSig));
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("Args (JSON)", s.addrArgs, sizeof(s.addrArgs));

    if (ImGui::Button("Call", ImVec2(120, 32))) {
        if (s.addrInput[0]) {
            try {
                long addr = std::stoll(s.addrInput, nullptr, 16);
                // Call via Java with addr as string then parse
                JNIEnv* env = getEnv();
                if (env) {
                    jclass c = env->FindClass("com/yambz/bridge/NativeBridge");
                    if (c) {
                        jmethodID m = env->GetStaticMethodID(c, "callNativeAddr",
                            "(JLjava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
                        if (m) {
                            jstring js = env->NewStringUTF(s.addrSig);
                            jstring ja = env->NewStringUTF(s.addrArgs);
                            jstring r = (jstring) env->CallStaticObjectMethod(c, m,
                                (jlong) addr, js, ja);
                            env->DeleteLocalRef(js);
                            env->DeleteLocalRef(ja);
                            if (!env->ExceptionCheck() && r) {
                                const char* rs = env->GetStringUTFChars(r, nullptr);
                                strncpy(s.addrResult, rs ? rs : "", sizeof(s.addrResult) - 1);
                                if (rs) env->ReleaseStringUTFChars(r, rs);
                                env->DeleteLocalRef(r);
                            }
                            if (env->ExceptionCheck()) env->ExceptionClear();
                        }
                    } else env->ExceptionClear();
                }
            } catch (...) {
                strncpy(s.addrResult, "bad address", sizeof(s.addrResult) - 1);
            }
        }
    }

    Theme::sectionHeader("Result");
    ImGui::TextWrapped("%s", s.addrResult[0] ? s.addrResult : "(no result)");
}

// ═══════════════════════════════════════════════════════════
//  Main draw
// ═══════════════════════════════════════════════════════════
void NativeTab::draw() {
    auto& s = st();

    const char* subs[] = {"Libs", "Hooks", "CModule", "Libc", "CallAddr"};
    for (int i = 0; i < 5; i++) {
        if (i > 0) ImGui::SameLine();
        bool sel = (s.subTab == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.24f, 0.1f, 1));
        if (ImGui::Button(subs[i], ImVec2(100, 32))) s.subTab = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    switch (s.subTab) {
        case 0: drawLibs(s); break;
        case 1: drawHooks(s); break;
        case 2: drawCModule(s); break;
        case 3: drawLibc(s); break;
        case 4: drawCallByAddr(s); break;
    }
}

} // namespace yambz
