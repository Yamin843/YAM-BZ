#include <jni.h>
#include <string>
#include <vector>
#include <cstring>

#include "core/Helpers.h"
#include "core/Logger.h"
#include "call/CallState.h"
#include "call/HeapPicker.h"
#include "call/HeapPickerUI.h"
#include "call/Presets.h"
#include "call/History.h"
#include "ui/Notifications.h"
#include "core/ClassEnumerator.h"
#include "core/ClassTypes.h"

using namespace yambz;

// ═══════════════════════════════════════════════════════════
//  CallBridge JNI
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_CallBridge_nativeInvoke(
    JNIEnv* env, jclass,
    jobject method, jstring methodDesc,
    jstring thisJson, jobjectArray argsJson)
{
    const char* md = env->GetStringUTFChars(methodDesc, nullptr);
    std::string desc = md ? md : "";
    if (md) env->ReleaseStringUTFChars(methodDesc, md);

    std::vector<std::string> args;
    if (argsJson) {
        jsize n = env->GetArrayLength(argsJson);
        for (jsize i = 0; i < n; i++) {
            auto s = (jstring) env->GetObjectArrayElement(argsJson, i);
            if (!s) { args.emplace_back("{\"k\":\"null\"}"); continue; }
            const char* c = env->GetStringUTFChars(s, nullptr);
            args.emplace_back(c ? c : "{\"k\":\"null\"}");
            env->ReleaseStringUTFChars(s, c);
            env->DeleteLocalRef(s);
        }
    }

    const char* tj = thisJson ? env->GetStringUTFChars(thisJson, nullptr) : nullptr;
    std::string thisStr = tj ? tj : "";
    if (tj) env->ReleaseStringUTFChars(thisJson, tj);

    bool ok = yambz_callEngineInvokeRaw(env, method, desc, thisStr, args);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_CallBridge_nativeIsRunning(JNIEnv*, jclass) {
    return yambz_callEngineIsRunning() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_yambz_bridge_CallBridge_nativeLastResultJson(JNIEnv* env, jclass) {
    std::string j = yambz_callEngineLastResultJson();
    return env->NewStringUTF(j.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_CallBridge_nativeClear(JNIEnv*, jclass) {
    yambz_callEngineClear();
}

// ═══════════════════════════════════════════════════════════
//  Presets JNI
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_CallBridge_nativeSavePreset(
    JNIEnv* env, jclass, jstring methodDesc, jstring name, jstring json)
{
    auto toStr = [&](jstring js) -> std::string {
        if (!js) return "";
        const char* c = env->GetStringUTFChars(js, nullptr);
        std::string r = c ? c : "";
        env->ReleaseStringUTFChars(js, c);
        return r;
    };
    return Presets::instance().save(toStr(methodDesc), toStr(name), toStr(json))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_yambz_bridge_CallBridge_nativeListPresets(
    JNIEnv* env, jclass, jstring methodDesc)
{
    const char* c = env->GetStringUTFChars(methodDesc, nullptr);
    auto list = Presets::instance().listFor(c ? c : "");
    env->ReleaseStringUTFChars(methodDesc, c);
    jclass cls_Str = env->FindClass("java/lang/String");
    jobjectArray out = env->NewObjectArray((jsize)list.size(), cls_Str, nullptr);
    for (size_t i = 0; i < list.size(); i++) {
        jstring js = env->NewStringUTF(list[i].name.c_str());
        env->SetObjectArrayElement(out, (jsize)i, js);
        env->DeleteLocalRef(js);
    }
    return out;
}

// ═══════════════════════════════════════════════════════════
//  Helpers (definitions for Helpers.h)
// ═══════════════════════════════════════════════════════════
void yambz_notify(const char* msg) {
    if (msg) Notifications::instance().push(msg, 2.5f);
}

void yambz_openCallFor(unsigned int classIdx) {
    auto classes = ClassEnumerator::instance().snapshot();
    if (!classes || classIdx >= classes->size()) return;
    const auto& c = (*classes)[classIdx];
    const auto& members = ClassEnumerator::instance().membersOf(classIdx);

    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls_Class = env->FindClass("java/lang/Class");
    jmethodID mid_forName = env->GetStaticMethodID(cls_Class, "forName",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jn = env->NewStringUTF(c.fullName.c_str());
    jobject jcls = env->CallStaticObjectMethod(cls_Class, mid_forName, jn);
    env->DeleteLocalRef(jn);
    if (!jcls) return;

    jmethodID mid_getMethods = env->GetMethodID(cls_Class, "getDeclaredMethods",
        "()[Ljava/lang/reflect/Method;");
    jobjectArray jmethods = (jobjectArray) env->CallObjectMethod(jcls, mid_getMethods);
    if (jmethods) {
        jsize n = env->GetArrayLength(jmethods);
        for (jsize i = 0; i < n; i++) {
            auto jm = env->GetObjectArrayElement(jmethods, i);
            jclass cls_M = env->GetObjectClass(jm);
            jmethodID mid_name = env->GetMethodID(cls_M, "getName", "()Ljava/lang/String;");
            jstring jname = (jstring) env->CallObjectMethod(jm, mid_name);
            const char* name = env->GetStringUTFChars(jname, nullptr);
            bool match = false;
            for (const auto& m : members) {
                if (m.kind == MemberKind::Method && name && m.name == name) {
                    match = true; break;
                }
            }
            if (name) env->ReleaseStringUTFChars(jname, name);
            env->DeleteLocalRef(jname);
            env->DeleteLocalRef(cls_M);
            if (match) {
                openCallForMethod(env, jm);
                env->DeleteLocalRef(jm);
                break;
            }
            env->DeleteLocalRef(jm);
        }
        env->DeleteLocalRef(jmethods);
    }
    env->DeleteLocalRef(jcls);
}

void yambz_openHeapPicker(const char* type) {
    auto& ui = heapPickerUI();
    std::strncpy(ui.searchType, type, sizeof(ui.searchType) - 1);
    ui.searchType[sizeof(ui.searchType) - 1] = '\0';
    ui.show = true;
    ui.selectedIdx = -1;
}

void yambz_useInstanceFromPicker(int idx) {
    jobject ref = HeapPicker::instance().pick(idx);
    if (!ref) return;

    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls = env->GetObjectClass(ref);
    jmethodID m_toStr = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
    if (m_toStr) {
        jstring js = (jstring) env->CallObjectMethod(ref, m_toStr);
        if (js) {
            const char* c = env->GetStringUTFChars(js, nullptr);
            LOGI("Heap picked: %s", c ? c : "?");
            env->ReleaseStringUTFChars(js, c);
            env->DeleteLocalRef(js);
        }
    }
    env->DeleteLocalRef(cls);
}

// Storage helpers
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModBootstrap_nativeInitStorage(JNIEnv* env, jclass, jstring cacheDir) {
    const char* c = env->GetStringUTFChars(cacheDir, nullptr);
    std::string base = c ? c : "/data/local/tmp";
    env->ReleaseStringUTFChars(cacheDir, c);
    Presets::instance().init(base + "/yambz/presets");
    History::instance().init(base + "/yambz/history.txt");
    LOGI("Storage initialized: %s", base.c_str());
}
