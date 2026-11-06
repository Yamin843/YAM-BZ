#include <jni.h>
#include <vector>
#include <cstring>
#include <android/log.h>
#include "call/ParamNode.h"
#include "call/CallEngine.h"
#include "core/Logger.h"

using namespace yambz;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_CallBridge_nativeInvoke(
    JNIEnv* env, jclass,
    jobject method, jstring methodDesc,
    jstring thisJson, jobjectArray argsJson)
{
    CallSpec spec;
    spec.methodMember = method;

    const char* md = env->GetStringUTFChars(methodDesc, nullptr);
    spec.methodDesc = md ? md : "";
    if (md) env->ReleaseStringUTFChars(methodDesc, md);

    // this (نُمرّره JSON مباشرة — نبني ParamNode من JSON ليس ضرورياً)
    // لكن CallEngine يحتاج ParamNode... للتبسيط، سنمرّر JSON خاماً
    // بإعادة التصميم: نُوسّع CallEngine ليأخذ JSON مباشرة

    // هذا التنفيذ المبسّط يستخدم CallEngine.invokeAsyncRaw (سنضيفها)
    extern bool yambz_invokeRaw(JNIEnv*, jobject method, const std::string& desc,
                                const std::string& thisJson,
                                const std::vector<std::string>& argsJson);

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

    return yambz_invokeRaw(env, method, spec.methodDesc, thisStr, args) ? JNI_TRUE : JNI_FALSE;
}

// يُنفذ الاستدعاء بنفس منطق CallEngine لكن من JSON خام
bool yambz_invokeRaw(JNIEnv* callerEnv, jobject method,
                                 const std::string& methodDesc,
                                 const std::string& thisJson,
                                 const std::vector<std::string>& argsJson) {
    // نُعيد استخدام CallEngine لكن بمسار مختلف
    extern void yambz_callEngineInvokeRaw(JNIEnv*, jobject, const std::string&,
                                           const std::string&, const std::vector<std::string>&);
    yambz_callEngineInvokeRaw(callerEnv, method, methodDesc, thisJson, argsJson);
    return true;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_CallBridge_nativeIsRunning(JNIEnv*, jclass) {
    extern bool yambz_callEngineIsRunning();
    return yambz_callEngineIsRunning() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_yambz_bridge_CallBridge_nativeLastResultJson(JNIEnv* env, jclass) {
    extern std::string yambz_callEngineLastResultJson();
    std::string j = yambz_callEngineLastResultJson();
    return env->NewStringUTF(j.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_CallBridge_nativeClear(JNIEnv*, jclass) {
    extern void yambz_callEngineClear();
    yambz_callEngineClear();
}

#include "call/CallState.h"
#include "core/ClassEnumerator.h"

void yambz_openCallFor(uint32_t classIdx) {
    auto classes = yambz::ClassEnumerator::instance().snapshot();
    if (!classes || classIdx >= classes->size()) return;
    const auto& c = (*classes)[classIdx];
    const auto& members = yambz::ClassEnumerator::instance().membersOf(classIdx);

    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    // اعثر على أول Method في الكلاس
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
                if (m.kind == yambz::MemberKind::Method && name && m.name == name) {
                    match = true; break;
                }
            }
            if (name) env->ReleaseStringUTFChars(jname, name);
            env->DeleteLocalRef(jname);
            env->DeleteLocalRef(cls_M);
            if (match) {
                yambz::openCallForMethod(env, jm);
                env->DeleteLocalRef(jm);
                break;
            }
            env->DeleteLocalRef(jm);
        }
        env->DeleteLocalRef(jmethods);
    }
    env->DeleteLocalRef(jcls);
}

#include "ui/Notifications.h"
#include "call/HeapPicker.h"
#include "call/HeapPickerUI.h"
#include "call/Presets.h"
#include "call/History.h"

void yambz_openHeapPicker(const char* type) {
    auto& ui = yambz::heapPickerUI();
    std::strncpy(ui.searchType, type, sizeof(ui.searchType) - 1);
    ui.searchType[sizeof(ui.searchType)-1] = '\0';
    ui.show = true;
    ui.selectedIdx = -1;
}

void yambz_useInstanceFromPicker(int idx) {
    jobject ref = yambz::HeapPicker::instance().pick(idx);
    if (!ref) return;

    // احصل على الوصف
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    // اعرضه في شريط الإشعارات (مؤقتاً — سيتحسن في الرد 8)
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

    // TODO: application إلى CallTab thisNode (سيُعالج في رد 8 wiring)
}

// ─── Presets / History JNI ───
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
    return yambz::Presets::instance().save(toStr(methodDesc), toStr(name), toStr(json))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_yambz_bridge_CallBridge_nativeListPresets(
    JNIEnv* env, jclass, jstring methodDesc)
{
    const char* c = env->GetStringUTFChars(methodDesc, nullptr);
    auto list = yambz::Presets::instance().listFor(c ? c : "");
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

// ─── init dirs (يُستدعى من ModBootstrap) ───
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModBootstrap_nativeInitStorage(JNIEnv* env, jclass, jstring cacheDir)
{
    const char* c = env->GetStringUTFChars(cacheDir, nullptr);
    std::string base = c ? c : "/data/local/tmp";
    env->ReleaseStringUTFChars(cacheDir, c);
    yambz::Presets::instance().init(base + "/yambz/presets");
    yambz::History::instance().init(base + "/yambz/history.txt");
    LOGI("Storage initialized: %s", base.c_str());

/* yambz_notify removed - re-added at end of file */
}

// ═══════════════════════════════════════════════════════════
//  yambz_notify — file-scope helper
//  (do NOT insert this before any closing brace)
// ═══════════════════════════════════════════════════════════
void yambz_notify(const char* msg) {
    if (msg) yambz::Notifications::instance().push(msg, 2.5f);
}
