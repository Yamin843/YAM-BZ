#include "ClassesTab_jni.h"
#include "core/Helpers.h"
#include "core/Logger.h"

#include <jni.h>
#include <string>

namespace yambz {
namespace ClassesTab_jni {

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
    if (!cls) { env->ExceptionClear(); LOGE("ClassBridge not found"); }
    return cls;
}

void startEnum() {
    JNIEnv* env = getEnv();
    if (!env) return;
    jclass cls = getClassBridge(env);
    if (!cls) return;
    jmethodID mid = env->GetStaticMethodID(cls, "startEnum", "()V");
    if (mid) env->CallStaticVoidMethod(cls, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

bool isEnumerating() {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass cls = getClassBridge(env);
    if (!cls) return false;
    jmethodID mid = env->GetStaticMethodID(cls, "isEnumerating", "()Z");
    if (!mid) return false;
    return env->CallStaticBooleanMethod(cls, mid) == JNI_TRUE;
}

bool isComplete() {
    JNIEnv* env = getEnv();
    if (!env) return false;
    jclass cls = getClassBridge(env);
    if (!cls) return false;
    jmethodID mid = env->GetStaticMethodID(cls, "isComplete", "()Z");
    if (!mid) return false;
    return env->CallStaticBooleanMethod(cls, mid) == JNI_TRUE;
}

int getTotal() {
    JNIEnv* env = getEnv();
    if (!env) return 0;
    jclass cls = getClassBridge(env);
    if (!cls) return 0;
    jmethodID mid = env->GetStaticMethodID(cls, "getTotal", "()I");
    if (!mid) return 0;
    return env->CallStaticIntMethod(cls, mid);
}

std::vector<std::string> nextBatch(int n) {
    std::vector<std::string> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass cls = getClassBridge(env);
    if (!cls) return out;
    jmethodID mid = env->GetStaticMethodID(cls, "nextBatch", "(I)[Ljava/lang/String;");
    if (!mid) return out;
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid, n);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return out; }
    if (!arr) return out;
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

std::vector<std::string> getMembers(const std::string& className) {
    std::vector<std::string> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass cls = getClassBridge(env);
    if (!cls) return out;
    jmethodID mid = env->GetStaticMethodID(cls, "getMembers",
        "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid) return out;
    jstring jcls = env->NewStringUTF(className.c_str());
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(cls, mid, jcls);
    env->DeleteLocalRef(jcls);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return out; }
    if (!arr) return out;
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

} // namespace ClassesTab_jni
} // namespace yambz
