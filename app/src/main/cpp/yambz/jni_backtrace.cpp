#include <jni.h>
#include <android/log.h>
#include "trace/DeepBacktrace.h"
#include "core/Logger.h"

using namespace yambz;

extern "C" JNIEXPORT jlong JNICALL
Java_com_yambz_bridge_DeepBacktrace_nativeAddressOfNextFrame(JNIEnv*, jclass) {
    // نستخدم symbol من libc — عنصر عشوائي
    return (jlong)(uintptr_t) &DeepBacktrace::instance;
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_DeepBacktrace_nativeOnComplete(JNIEnv* env, jclass, jstring json) {
    if (!json) return;
    const char* c = env->GetStringUTFChars(json, nullptr);
    if (c) {
        DeepBacktrace::instance().setFrames(c);
        env->ReleaseStringUTFChars(json, c);
    }
}

// ربط مع PanelBridge: بدء جمع
extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_PanelBridge_nativeStartDeepBacktrace(JNIEnv*, jclass, jint durationMs, jint maxFrames) {
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return JNI_FALSE;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls = env->FindClass("com/yambz/bridge/DeepBacktrace");
    jmethodID mid = env->GetStaticMethodID(cls, "start", "(II)Z");
    if (!mid) return JNI_FALSE;
    jboolean r = env->CallStaticBooleanMethod(cls, mid, durationMs, maxFrames);
    return r;
}
