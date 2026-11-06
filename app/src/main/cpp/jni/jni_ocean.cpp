#include <jni.h>
#include <string>
#include <sstream>
#include <chrono>

#include "core/Helpers.h"
#include "core/Logger.h"
#include "trace/OceanState.h"

using namespace yambz;

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ═══════════════════════════════════════════════════════════
//  dump Java object via ObjectDumper (if exists), else toString
// ═══════════════════════════════════════════════════════════
static std::string dumpObject(JNIEnv* env, jobject obj, int maxDepth) {
    if (!obj) return "null";
    // Try ObjectDumper
    jclass cls = env->FindClass("com/yambz/bridge/ObjectDumper");
    if (cls) {
        jmethodID mid = env->GetStaticMethodID(cls, "dump",
            "(Ljava/lang/Object;)Ljava/lang/String;");
        if (mid) {
            jstring js = (jstring) env->CallStaticObjectMethod(cls, mid, obj);
            if (!env->ExceptionCheck() && js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                std::string out = c ? c : "";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
                return out;
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    // Fallback: toString
    jclass cls_o = env->GetObjectClass(obj);
    jmethodID mid_toStr = env->GetMethodID(cls_o, "toString", "()Ljava/lang/String;");
    if (mid_toStr) {
        jstring js = (jstring) env->CallObjectMethod(obj, mid_toStr);
        if (!env->ExceptionCheck() && js) {
            const char* c = env->GetStringUTFChars(js, nullptr);
            std::string out = c ? c : "";
            env->ReleaseStringUTFChars(js, c);
            env->DeleteLocalRef(js);
            return out;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return "<object>";
}

static std::string jstringToStd(JNIEnv* env, jstring js) {
    if (!js) return "";
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string s = c ? c : "";
    env->ReleaseStringUTFChars(js, c);
    return s;
}

static std::string dumpArgs(JNIEnv* env, jobjectArray args) {
    if (!args) return "";
    jsize n = env->GetArrayLength(args);
    if (n == 0) return "";
    std::stringstream ss;
    ss << "[";
    for (jsize i = 0; i < n; i++) {
        if (i > 0) ss << ", ";
        jobject a = env->GetObjectArrayElement(args, i);
        if (!a) { ss << "null"; continue; }
        ss << dumpObject(env, a, 2);
        env->DeleteLocalRef(a);
    }
    ss << "]";
    return ss.str();
}

// ═══════════════════════════════════════════════════════════
//  Java → C++ event callbacks
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanSinkerBridge_nativeOnCall(
    JNIEnv* env, jclass,
    jint sessionId, jobject member,
    jstring methodDesc, jstring className, jstring methodName,
    jint depth, jobjectArray args, jobject self)
{
    OceanEvent ev;
    ev.ts = nowMs();
    ev.kind = (int)OceanEventKind::CALL;
    ev.depth = (int)depth;
    ev.methodDesc = jstringToStd(env, methodDesc);
    ev.className = jstringToStd(env, className);
    ev.methodName = jstringToStd(env, methodName);
    std::string argsStr = dumpArgs(env, args);
    std::string thisStr;
    if (self) thisStr = "this=" + dumpObject(env, self, 2);
    if (!argsStr.empty()) ev.extra = argsStr;
    if (!thisStr.empty()) ev.extra += (ev.extra.empty() ? "" : " ") + thisStr;
    oceanPushEvent(std::move(ev));
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanSinkerBridge_nativeOnReturn(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc, jobject returnValue)
{
    OceanEvent ev;
    ev.ts = nowMs();
    ev.kind = (int)OceanEventKind::RETURN;
    if (member) {
        jclass cls = env->GetObjectClass(member);
        jmethodID mid = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (mid) {
            jstring js = (jstring) env->CallObjectMethod(member, mid);
            ev.methodDesc = jstringToStd(env, js);
            if (js) env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(cls);
    }
    ev.extra = "= " + dumpObject(env, returnValue, 3);
    oceanPushEvent(std::move(ev));
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanSinkerBridge_nativeOnExec(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc, jobject self)
{
    OceanEvent ev;
    ev.ts = nowMs();
    ev.kind = (int)OceanEventKind::EXEC;
    if (member) {
        jclass cls = env->GetObjectClass(member);
        jmethodID mid = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (mid) {
            jstring js = (jstring) env->CallObjectMethod(member, mid);
            ev.methodDesc = jstringToStd(env, js);
            if (js) env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(cls);
    }
    ev.extra = "pc=" + std::to_string(dexPc);
    oceanPushEvent(std::move(ev));
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanSinkerBridge_nativeOnBlock(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc, jint offset)
{
    OceanEvent ev;
    ev.ts = nowMs();
    ev.kind = (int)OceanEventKind::BLOCK;
    if (member) {
        jclass cls = env->GetObjectClass(member);
        jmethodID mid = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (mid) {
            jstring js = (jstring) env->CallObjectMethod(member, mid);
            ev.methodDesc = jstringToStd(env, js);
            if (js) env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(cls);
    }
    ev.extra = "pc=" + std::to_string(dexPc) + "→" + std::to_string((int)dexPc + (int)offset);
    oceanPushEvent(std::move(ev));
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanSinkerBridge_nativeOnFieldRead(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc, jobject owner, jobject field)
{
    OceanEvent ev;
    ev.ts = nowMs();
    ev.kind = (int)OceanEventKind::FILE_READ;
    if (field) {
        jclass cls = env->GetObjectClass(field);
        jmethodID mid = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (mid) {
            jstring js = (jstring) env->CallObjectMethod(field, mid);
            ev.methodDesc = jstringToStd(env, js);
            if (js) env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(cls);
    }
    oceanPushEvent(std::move(ev));
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanSinkerBridge_nativeOnFieldWritten(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc, jobject owner, jobject field, jobject newValue)
{
    OceanEvent ev;
    ev.ts = nowMs();
    ev.kind = (int)OceanEventKind::FIELD_WRITE;
    if (field) {
        jclass cls = env->GetObjectClass(field);
        jmethodID mid = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (mid) {
            jstring js = (jstring) env->CallObjectMethod(field, mid);
            ev.methodDesc = jstringToStd(env, js);
            if (js) env->DeleteLocalRef(js);
        }
        env->DeleteLocalRef(cls);
    }
    ev.extra = "= " + dumpObject(env, newValue, 2);
    oceanPushEvent(std::move(ev));
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanSinkerBridge_nativeOnUnTrace(JNIEnv*, jclass, jint sessionId) {
    LOGI("OceanSinker onUnTrace: session=%d", sessionId);
    oceanUI().tracing = false;
}

// ═══════════════════════════════════════════════════════════
//  UI → C++ helpers (called from TraceTab.cpp)
// ═══════════════════════════════════════════════════════════
namespace yambz {

// Used by OceanState::oceanStart via OceanController (no direct Java here — all in OceanState.cpp)

} // namespace yambz
