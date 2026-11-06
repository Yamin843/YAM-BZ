#include <jni.h>
#include <android/log.h>

#include "core/Helpers.h"
#include "core/Logger.h"

// ═══════════════════════════════════════════════════════════
//  Global VM
// ═══════════════════════════════════════════════════════════
static JavaVM* g_vm = nullptr;

JavaVM* yambz_getVM() { return g_vm; }
void    yambz_setVM(JavaVM* vm) { g_vm = vm; }

// ═══════════════════════════════════════════════════════════
//  Forward: embedded loader entry (defined in embedded_loader.cpp)
// ═══════════════════════════════════════════════════════════
namespace yambz {
bool embedded_initialize(JNIEnv* env, JavaVM* vm);
}

// ═══════════════════════════════════════════════════════════
//  JNI lifecycle
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    LOGI("JNI_OnLoad — YAM-BZ v%s", YAMBZ_VERSION);

    g_vm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("GetEnv failed");
        return JNI_VERSION_1_6;
    }

    if (!yambz::embedded_initialize(env, vm)) {
        LOGE("embedded_initialize failed — mod will not work");
    }

    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    LOGI("JNI_OnUnload — YAM-BZ");
}
