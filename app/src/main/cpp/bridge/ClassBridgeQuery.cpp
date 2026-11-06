#include "ClassBridgeQuery.h"
#include "../core/Helpers.h"
#include "../core/Logger.h"

#include <jni.h>

namespace yambz {
namespace ClassBridgeQuery {

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

std::vector<std::pair<std::string, std::string>>
getFieldSchema(const std::string& className) {
    std::vector<std::pair<std::string, std::string>> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass c = getClassBridge(env);
    if (!c) return out;
    jmethodID mid = env->GetStaticMethodID(c, "getFieldSchema",
        "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return out; }

    jstring jc = env->NewStringUTF(className.c_str());
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(c, mid, jc);
    env->DeleteLocalRef(jc);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return out; }

    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* s = env->GetStringUTFChars(js, nullptr);
        if (s) {
            std::string line = s;
            size_t p = line.find('|');
            if (p != std::string::npos) {
                out.emplace_back(line.substr(0, p), line.substr(p + 1));
            }
        }
        if (s) env->ReleaseStringUTFChars(js, s);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out;
}

std::vector<std::tuple<std::string, std::string, std::string>>
getMethodSchema(const std::string& className) {
    std::vector<std::tuple<std::string, std::string, std::string>> out;
    JNIEnv* env = getEnv();
    if (!env) return out;
    jclass c = getClassBridge(env);
    if (!c) return out;
    jmethodID mid = env->GetStaticMethodID(c, "getMethodSchema",
        "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return out; }

    jstring jc = env->NewStringUTF(className.c_str());
    jobjectArray arr = (jobjectArray) env->CallStaticObjectMethod(c, mid, jc);
    env->DeleteLocalRef(jc);
    if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return out; }

    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* s = env->GetStringUTFChars(js, nullptr);
        if (s) {
            std::string line = s;
            size_t p1 = line.find('|');
            size_t p2 = line.find('|', p1 + 1);
            if (p1 != std::string::npos && p2 != std::string::npos) {
                out.emplace_back(
                    line.substr(0, p1),
                    line.substr(p1 + 1, p2 - p1 - 1),
                    line.substr(p2 + 1));
            }
        }
        if (s) env->ReleaseStringUTFChars(js, s);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out;
}

} // namespace ClassBridgeQuery
} // namespace yambz
