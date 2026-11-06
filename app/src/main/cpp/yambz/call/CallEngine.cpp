#include "CallEngine.h"
#include <vector>
#include <thread>
#include "ParamNode.h"
#include "../core/Logger.h"

#include <chrono>

namespace yambz {

CallEngine& CallEngine::instance() {
    static CallEngine e;
    return e;
}

static JavaVM* getVM() {
    extern JavaVM* yambz_getGlobalVM();
    return yambz_getGlobalVM();
}

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool CallEngine::invokeAsync(JNIEnv* callerEnv, const CallSpec& spec) {
    if (m_running.exchange(true)) {
        LOGW("CallEngine: already running");
        return false;
    }

    // نُنسخ method كـ global ref ليصمد حتى نهاية الاستدعاء
    jobject methodG = callerEnv->NewGlobalRef(spec.methodMember);

    // نسلسل this + args إلى JSON
    std::string thisJson;
    if (spec.thisNode) thisJson = nodeToJson(spec.thisNode);

    std::vector<std::string> argsJson;
    argsJson.reserve(spec.args.size());
    for (auto& a : spec.args) argsJson.push_back(nodeToJson(a));

    std::string methodDesc = spec.methodDesc;

    // نُشغّل الخيط
    std::thread([this, methodG, thisJson, argsJson, methodDesc]() {
        JavaVM* vm = getVM();
        if (!vm) { m_running.store(false); return; }

        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            m_running.store(false);
            return;
        }

        long t0 = nowMs();
        CallResult res;

        // احضار ClassLoader من ModBootstrap
        jclass cls_BS = env->FindClass("com/yambz/bridge/ModBootstrap");
        jmethodID m_getLoader = env->GetStaticMethodID(cls_BS, "getClassLoader",
            "()Ljava/lang/ClassLoader;");
        jobject loader = env->CallStaticObjectMethod(cls_BS, m_getLoader);

        // بناء this + args
        jobject thisObj = nullptr;
        jclass cls_PB = env->FindClass("com/yambz/call/ParameterBuilder");
        jmethodID m_build = env->GetStaticMethodID(cls_PB, "build",
            "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/Object;");

        if (!thisJson.empty()) {
            jstring js = env->NewStringUTF(thisJson.c_str());
            thisObj = env->CallStaticObjectMethod(cls_PB, m_build, js, loader);
            env->DeleteLocalRef(js);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe(); env->ExceptionClear();
                res.errorDump = "this construction failed";
            }
        }

        jobjectArray argsArr = nullptr;
        if (!argsJson.empty()) {
            jclass cls_Obj = env->FindClass("java/lang/Object");
            argsArr = env->NewObjectArray((jsize)argsJson.size(), cls_Obj, nullptr);
            for (size_t i = 0; i < argsJson.size(); i++) {
                jstring js = env->NewStringUTF(argsJson[i].c_str());
                jobject o = env->CallStaticObjectMethod(cls_PB, m_build, js, loader);
                env->DeleteLocalRef(js);
                if (env->ExceptionCheck()) {
                    env->ExceptionDescribe(); env->ExceptionClear();
                    o = nullptr;
                }
                env->SetObjectArrayElement(argsArr, (jsize)i, o);
                if (o) env->DeleteLocalRef(o);
            }
        }

        // استدعِ MethodInvoker.invoke
        jclass cls_MI = env->FindClass("com/yambz/call/MethodInvoker");
        jmethodID m_inv = env->GetStaticMethodID(cls_MI, "invoke",
            "(Ljava/lang/reflect/Member;Ljava/lang/Object;[Ljava/lang/Object;)Lcom/yambz/call/MethodInvoker$Result;");
        jobject result = env->CallStaticObjectMethod(cls_MI, m_inv,
            methodG, thisObj, argsArr);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe(); env->ExceptionClear();
            res.errorDump = "invoke exception";
        } else if (result) {
            jclass cls_R = env->GetObjectClass(result);
            jfieldID f_succ = env->GetFieldID(cls_R, "success", "Z");
            jfieldID f_val  = env->GetFieldID(cls_R, "value", "Ljava/lang/Object;");
            jfieldID f_err  = env->GetFieldID(cls_R, "error", "Ljava/lang/Throwable;");
            res.success = env->GetBooleanField(result, f_succ) != 0;
            jobject v = env->GetObjectField(result, f_val);
            jobject e = env->GetObjectField(result, f_err);
            if (v) {
                // dump via ObjectDumper
                jclass cls_OD = env->FindClass("com/yambz/bridge/ObjectDumper");
                jmethodID m_dump = env->GetStaticMethodID(cls_OD, "dump",
                    "(Ljava/lang/Object;)Ljava/lang/String;");
                jstring js = (jstring) env->CallStaticObjectMethod(cls_OD, m_dump, v);
                if (js) {
                    const char* c = env->GetStringUTFChars(js, nullptr);
                    res.valueDump = c ? c : "";
                    env->ReleaseStringUTFChars(js, c);
                    env->DeleteLocalRef(js);
                }
                env->DeleteLocalRef(v);
            }
            if (e) {
                jclass cls_OD = env->FindClass("com/yambz/bridge/ObjectDumper");
                jmethodID m_dump = env->GetStaticMethodID(cls_OD, "dump",
                    "(Ljava/lang/Object;)Ljava/lang/String;");
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
            std::lock_guard<std::mutex> lk(m_mu);
            m_last = res;
        }
        m_running.store(false);
        LOGI("CallEngine: invoked %s → %s in %ldms",
             methodDesc.c_str(), res.success ? "OK" : "FAIL", res.elapsedMs);

        vm->DetachCurrentThread();
    }).detach();

    return true;
}

void CallEngine::cancel() {
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);
    jclass cls_MI = env->FindClass("com/yambz/call/MethodInvoker");
    jmethodID mid = env->GetStaticMethodID(cls_MI, "cancel", "()V");
    if (mid) env->CallStaticVoidMethod(cls_MI, mid);
}

CallResult CallEngine::lastResult() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_last;
}

void CallEngine::clearResult() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_last = CallResult{};
}

} // namespace yambz

// ─────────────────────────────────────────────────────
//  Raw JSON invoke (bypasses ParamNode)
// ─────────────────────────────────────────────────────
namespace yambz {
static std::atomic<bool> g_rawRunning{false};
static std::mutex g_rawMu;
static CallResult g_rawResult;

void callEngineInvokeRaw(JNIEnv* callerEnv, jobject method,
                         const std::string& methodDesc,
                         const std::string& thisJson,
                         const std::vector<std::string>& argsJson) {
    if (g_rawRunning.exchange(true)) return;
    jobject methodG = callerEnv->NewGlobalRef(method);

    std::thread([methodG, methodDesc, thisJson, argsJson]() {
        extern JavaVM* yambz_getGlobalVM();
        JavaVM* vm = yambz_getGlobalVM();
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            g_rawRunning.store(false);
            return;
        }

        long t0 = nowMs();
        CallResult res;

        jclass cls_BS = env->FindClass("com/yambz/bridge/ModBootstrap");
        jmethodID m_getLoader = env->GetStaticMethodID(cls_BS, "getClassLoader",
            "()Ljava/lang/ClassLoader;");
        jobject loader = env->CallStaticObjectMethod(cls_BS, m_getLoader);

        jclass cls_PB = env->FindClass("com/yambz/call/ParameterBuilder");
        jmethodID m_build = env->GetStaticMethodID(cls_PB, "build",
            "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/Object;");

        jobject thisObj = nullptr;
        if (!thisJson.empty()) {
            jstring js = env->NewStringUTF(thisJson.c_str());
            thisObj = env->CallStaticObjectMethod(cls_PB, m_build, js, loader);
            env->DeleteLocalRef(js);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe(); env->ExceptionClear();
                res.errorDump = "this construction failed";
            }
        }

        jobjectArray argsArr = nullptr;
        if (!argsJson.empty()) {
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

        jclass cls_MI = env->FindClass("com/yambz/call/MethodInvoker");
        jmethodID m_inv = env->GetStaticMethodID(cls_MI, "invoke",
            "(Ljava/lang/reflect/Member;Ljava/lang/Object;[Ljava/lang/Object;)Lcom/yambz/call/MethodInvoker$Result;");
        jobject result = env->CallStaticObjectMethod(cls_MI, m_inv, methodG, thisObj, argsArr);

        if (env->ExceptionCheck()) {
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
            if (v) {
                jclass cls_OD = env->FindClass("com/yambz/bridge/ObjectDumper");
                jmethodID m_dump = env->GetStaticMethodID(cls_OD, "dump",
                    "(Ljava/lang/Object;)Ljava/lang/String;");
                jstring js = (jstring) env->CallStaticObjectMethod(cls_OD, m_dump, v);
                if (js) {
                    const char* c = env->GetStringUTFChars(js, nullptr);
                    res.valueDump = c ? c : "";
                    env->ReleaseStringUTFChars(js, c);
                    env->DeleteLocalRef(js);
                }
                env->DeleteLocalRef(v);
            }
            if (e) {
                jclass cls_OD = env->FindClass("com/yambz/bridge/ObjectDumper");
                jmethodID m_dump = env->GetStaticMethodID(cls_OD, "dump",
                    "(Ljava/lang/Object;)Ljava/lang/String;");
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
        vm->DetachCurrentThread();
    }).detach();
}
} // namespace yambz

// ─────────────────────────────────────────────────────
//  Global helpers for JNI
// ─────────────────────────────────────────────────────
namespace yambz {
static std::mutex g_qMu;
static CallResult g_qResult;

bool callEngineIsRunning() {
    return g_rawRunning.load();
}

std::string callEngineLastResultJson() {
    std::lock_guard<std::mutex> lk(g_rawMu);
    std::string out = "{";
    out += "\"success\":"; out += (g_rawResult.success ? "true" : "false");
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
    out += "\",\"elapsedMs\":"; out += std::to_string(g_rawResult.elapsedMs);
    out += "}";
    return out;
}

void callEngineClear() {
    std::lock_guard<std::mutex> lk(g_rawMu);
    g_rawResult = CallResult{};
}
} // namespace yambz

// C linkage wrappers
bool yambz_callEngineIsRunning() { return yambz::callEngineIsRunning(); }
std::string yambz_callEngineLastResultJson() { return yambz::callEngineLastResultJson(); }
void yambz_callEngineClear() { yambz::callEngineClear(); }
void yambz_callEngineInvokeRaw(JNIEnv* env, jobject method,
                                          const std::string& desc,
                                          const std::string& thisJson,
                                          const std::vector<std::string>& args) {
    yambz::callEngineInvokeRaw(env, method, desc, thisJson, args);
}
