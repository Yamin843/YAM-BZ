#include <jni.h>
#include <string>
#include <sstream>

#include "core/Helpers.h"
#include "core/Logger.h"
#include "trace/OceanTracePanel.h"

using namespace yambz;

// ═══════════════════════════════════════════════════════════
//  Helper: dump Java object
// ═══════════════════════════════════════════════════════════
static std::string dumpObject(JNIEnv* env, jobject obj, int maxDepth) {
    if (!obj) return "null";
    jclass cls = env->FindClass("com/yambz/bridge/ObjectDumper");
    if (!cls) { env->ExceptionClear(); return "<no-dumper>"; }
    jmethodID mid = env->GetStaticMethodID(cls, "dump",
        "(Ljava/lang/Object;)Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return "<no-dump-method>"; }
    jstring js = (jstring) env->CallStaticObjectMethod(cls, mid, obj);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return "<dump-ex>";
    }
    if (!js) return "null";
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string out = c ? c : "";
    env->ReleaseStringUTFChars(js, c);
    env->DeleteLocalRef(js);
    return out;
}

// ═══════════════════════════════════════════════════════════
//  Callbacks from Java (OceanTraceBridge)
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnCall(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jstring methodDesc,
    jstring className, jstring methodName, jint depth,
    jobjectArray args, jobject self, jlong ctxId)
{
    const char* md = env->GetStringUTFChars(methodDesc, nullptr);
    std::string sMethod = md ? md : "";
    if (md) env->ReleaseStringUTFChars(methodDesc, md);

    // Build extras: args + self
    std::stringstream extras;
    if (self) {
        std::string s = dumpObject(env, self, 3);
        if (!s.empty() && s != "null") {
            extras << "this=" << s << " ";
        }
    }
    if (args) {
        jsize n = env->GetArrayLength(args);
        if (n > 0) {
            extras << "args=[";
            for (jsize i = 0; i < n; i++) {
                if (i > 0) extras << ", ";
                jobject a = env->GetObjectArrayElement(args, i);
                if (a) {
                    extras << dumpObject(env, a, 2);
                    env->DeleteLocalRef(a);
                } else {
                    extras << "null";
                }
            }
            extras << "]";
        }
    }

    oceanTracePushEvent(1, (int) depth, sMethod, extras.str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnReturn(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc,
    jobject returnValue, jint eventKind)
{
    std::string methodDesc = "?";
    if (member) {
        jclass cls = env->GetObjectClass(member);
        jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(member, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                methodDesc = c ? c : "?";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls);
    }

    std::string extra;
    if (returnValue) {
        extra = "= " + dumpObject(env, returnValue, 3);
    } else {
        extra = "= void";
    }

    oceanTracePushEvent(2, 0, methodDesc, extra);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnReturnPrim(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc,
    jclass returnType, jlong rawValue)
{
    std::string methodDesc = "?";
    if (member) {
        jclass cls = env->GetObjectClass(member);
        jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(member, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                methodDesc = c ? c : "?";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls);
    }

    std::stringstream extra;
    extra << "= " << rawValue;
    oceanTracePushEvent(2, 0, methodDesc, extra.str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnExec(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc, jobject self)
{
    std::string methodDesc = "?";
    if (member) {
        jclass cls = env->GetObjectClass(member);
        jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(member, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                methodDesc = c ? c : "?";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls);
    }
    std::stringstream extra;
    extra << "dexPc=" << dexPc;
    oceanTracePushEvent(3, 0, methodDesc, extra.str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnBlock(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc, jint offset)
{
    std::string methodDesc = "?";
    if (member) {
        jclass cls = env->GetObjectClass(member);
        jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(member, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                methodDesc = c ? c : "?";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls);
    }
    std::stringstream extra;
    extra << "dexPc=" << dexPc << " -> " << (dexPc + offset);
    oceanTracePushEvent(4, 0, methodDesc, extra.str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnFieldRead(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc,
    jobject owner, jobject field)
{
    std::string fieldStr = "?";
    if (field) {
        jclass cls = env->GetObjectClass(field);
        jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(field, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                fieldStr = c ? c : "?";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls);
    }
    oceanTracePushEvent(5, 0, fieldStr, "");
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnFieldWritten(
    JNIEnv* env, jclass,
    jint sessionId, jobject member, jint dexPc,
    jobject owner, jobject field, jobject newValue)
{
    std::string fieldStr = "?";
    if (field) {
        jclass cls = env->GetObjectClass(field);
        jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(field, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                fieldStr = c ? c : "?";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls);
    }
    std::string val = "null";
    if (newValue) {
        val = dumpObject(env, newValue, 2);
    }
    oceanTracePushEvent(6, 0, fieldStr, "= " + val);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_OceanTraceBridge_nativeOnUnTrace(JNIEnv*, jclass, jint sessionId) {
    LOGI("OceanTraceBridge native onUnTrace: session=%d", sessionId);
}

// ═══════════════════════════════════════════════════════════
//  JNI from PanelBridge for opening the panel
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOpenOceanTrace(JNIEnv* env, jclass) {
    oceanTraceUI().show = true;
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeCloseOceanTrace(JNIEnv* env, jclass) {
    oceanTraceUI().show = false;
    if (oceanTraceUI().tracing) {
        oceanTraceStop();
    }
}
