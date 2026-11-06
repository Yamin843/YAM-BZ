#include "CallEngine.h"
#include "ParamNode.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>

// ═══════════════════════════════════════════════════════════
//  Global state (file-scope, no namespace)
// ═══════════════════════════════════════════════════════════
static std::atomic<bool> g_rawRunning{false};
static std::mutex        g_rawMu;
static yambz::CallResult g_rawResult;

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ═══════════════════════════════════════════════════════════
//  Static worker
// ═══════════════════════════════════════════════════════════
static void callEngineWorker(JavaVM* vm, jobject methodG,
                              std::string methodDesc,
                              std::string thisJson,
                              std::vector<std::string> argsJson) {
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        g_rawRunning.store(false);
        return;
    }

    long t0 = nowMs();
    yambz::CallResult res;

    // 1) ModBootstrap.getClassLoader
    jclass cls_BS = env->FindClass("com/yambz/bridge/ModBootstrap");
    jmethodID m_getLoader = cls_BS ? env->GetStaticMethodID(cls_BS, "getClassLoader",
        "()Ljava/lang/ClassLoader;") : nullptr;
    jobject loader = (m_getLoader && cls_BS) ? env->CallStaticObjectMethod(cls_BS, m_getLoader) : nullptr;

    // 2) ParameterBuilder.build
    jclass cls_PB = env->FindClass("com/yambz/call/ParameterBuilder");
    jmethodID m_build = cls_PB ? env->GetStaticMethodID(cls_PB, "build",
        "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/Object;") : nullptr;

    // 3) Build this
    jobject thisObj = nullptr;
    if (!thisJson.empty() && m_build) {
        jstring js = env->NewStringUTF(thisJson.c_str());
        thisObj = env->CallStaticObjectMethod(cls_PB, m_build, js, loader);
        env->DeleteLocalRef(js);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            res.errorDump = "this construction failed";
        }
    }

    // 4) Build args array
    jobjectArray argsArr = nullptr;
    if (!argsJson.empty() && m_build) {
        jclass cls_Obj = env->FindClass("java/lang/Object");
        argsArr = env->NewObjectArray((jsize)argsJson.size(), cls_Obj, nullptr);
        for (size_t i = 0; i < argsJson.size(); i++) {
            jstring js = env->NewStringUTF(argsJson[i].c_str());
            jobject o = env->CallStaticObjectMethod(cls_PB, m_build, js, loader);
            env->DeleteLocalRef(js);
            if (env->ExceptionCheck()) { env->ExceptionClear(); o = nullptr; }
            env->SetObjectArrayElement(argsArr, (jsize)i, o);
            if (o) env->DeleteLocalRef(o);
        }
    }

    // 5) MethodInvoker.invoke
    jclass cls_MI = env->FindClass("com/yambz/call/MethodInvoker");
    jmethodID m_inv = cls_MI ? env->GetStaticMethodID(cls_MI, "invoke",
        "(Ljava/lang/reflect/Member;Ljava/lang/Object;[Ljava/lang/Object;)Lcom/yambz/call/MethodInvoker$Result;") : nullptr;
    jobject result = (m_inv && cls_MI) ? env->CallStaticObjectMethod(cls_MI, m_inv, methodG, thisObj, argsArr) : nullptr;

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        res.errorDump = "invoke exception";
    } else if (result) {
        jclass cls_R = env->GetObjectClass(result);
        jfieldID f_succ = env->GetFieldID(cls_R, "success", "Z");
        jfieldID f_val  = env->GetFieldID(cls_R, "value", "Ljava/lang/Object;");
        jfieldID f_err  = env->GetFieldID(cls_R, "error", "Ljava/lang/Throwable;");
        res.success = env->GetBooleanField(result, f_succ) != 0;
        jobject v = env->GetObjectField(result, f_val);
        jobject e = env->GetObjectField(result, f_err);

        jclass cls_OD = env->FindClass("com/yambz/bridge/ObjectDumper");
        jmethodID m_dump = cls_OD ? env->GetStaticMethodID(cls_OD, "dump",
            "(Ljava/lang/Object;)Ljava/lang/String;") : nullptr;

        if (v && m_dump) {
            jstring js = (jstring) env->CallStaticObjectMethod(cls_OD, m_dump, v);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                res.valueDump = c ? c : "";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
            env->DeleteLocalRef(v);
        }
        if (e && m_dump) {
            jstring js = (jstring) env->CallStaticObjectMethod(cls_OD, m_dump, e);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                res.errorDump = c ? c : "";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
            env->DeleteLocalRef(e);
        }
        env->DeleteLocalRef(result);
    }

    if (thisObj) env->DeleteLocalRef(thisObj);
    if (argsArr) env->DeleteLocalRef(argsArr);
    if (loader) env->DeleteLocalRef(loader);
    env->DeleteGlobalRef(methodG);

    res.elapsedMs = nowMs() - t0;
    {
        std::lock_guard<std::mutex> lk(g_rawMu);
        g_rawResult = res;
    }
    g_rawRunning.store(false);
    yambz::LOGI("CallEngine: %s → %s in %ldms",
                methodDesc.c_str(), res.success ? "OK" : "FAIL", res.elapsedMs);

    vm->DetachCurrentThread();
}

// ═══════════════════════════════════════════════════════════
//  CallEngine class methods (namespace yambz)
// ═══════════════════════════════════════════════════════════
namespace yambz {

CallEngine& CallEngine::instance() {
    static CallEngine e;
    return e;
}

void CallEngine::cancel() {
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);
    jclass cls_MI = env->FindClass("com/yambz/call/MethodInvoker");
    jmethodID mid = cls_MI ? env->GetStaticMethodID(cls_MI, "cancel", "()V") : nullptr;
    if (mid) env->CallStaticVoidMethod(cls_MI, mid);
}

CallResult CallEngine::lastResult() const {
    std::lock_guard<std::mutex> lk(g_rawMu);
    return g_rawResult;
}

void CallEngine::clearResult() {
    std::lock_guard<std::mutex> lk(g_rawMu);
    g_rawResult = CallResult{};
}

} // namespace yambz

// ═══════════════════════════════════════════════════════════
//  GLOBAL HELPERS (real definitions, matching Helpers.h)
// ═══════════════════════════════════════════════════════════
bool yambz_callEngineInvokeRaw(JNIEnv* env, jobject method,
                                const std::string& methodDesc,
                                const std::string& thisJson,
                                const std::vector<std::string>& argsJson) {
    if (g_rawRunning.exchange(true)) {
        // already running
        return false;
    }

    jobject methodG = env->NewGlobalRef(method);
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    if (!vm) {
        env->DeleteGlobalRef(methodG);
        g_rawRunning.store(false);
        return false;
    }

    std::thread t(callEngineWorker, vm, methodG, methodDesc, thisJson, argsJson);
    t.detach();
    return true;
}

bool yambz_callEngineIsRunning() {
    return g_rawRunning.load();
}

std::string yambz_callEngineLastResultJson() {
    std::lock_guard<std::mutex> lk(g_rawMu);
    std::string out = "{";
    out += "\"success\":";
    out += (g_rawResult.success ? "true" : "false");
    out += ",\"value\":\"";
    for (char c : g_rawResult.valueDump) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\",\"error\":\"";
    for (char c : g_rawResult.errorDump) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\",\"elapsedMs\":";
    out += std::to_string(g_rawResult.elapsedMs);
    out += "}";
    return out;
}

void yambz_callEngineClear() {
    std::lock_guard<std::mutex> lk(g_rawMu);
    g_rawResult = yambz::CallResult{};
}
