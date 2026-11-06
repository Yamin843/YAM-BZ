#include <jni.h>
#include <android/log.h>
#include "embedded_loader.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "YAMBZ", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "YAMBZ", __VA_ARGS__)

// ═══════════════════════════════════════════════════════════
//  Global JavaVM — must be declared BEFORE JNI_OnLoad
// ═══════════════════════════════════════════════════════════
static JavaVM* g_yambz_vm = nullptr;

JavaVM* yambz_getGlobalVM() { return g_yambz_vm; }
void    yambz_setGlobalVM(JavaVM* vm) { g_yambz_vm = vm; }

// ═══════════════════════════════════════════════════════════
//  JNI lifecycle
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    LOGI("JNI_OnLoad — YAM-BZ");
    g_yambz_vm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("GetEnv failed");
        return JNI_VERSION_1_6;
    }

    if (!yambz::EmbeddedLoader::initialize(env, vm)) {
        LOGE("EmbeddedLoader failed — mod will not work");
    }

    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM* vm, void* /*reserved*/) {
    LOGI("JNI_OnUnload — YAM-BZ");
}
