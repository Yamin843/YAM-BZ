#include <jni.h>
#include "call/TracePresets.h"
#include <android/log.h>
#include "embedded_loader.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "YAMBZ", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "YAMBZ", __VA_ARGS__)

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    LOGI("JNI_OnLoad — YAM-BZ");

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("GetEnv failed");
        return JNI_VERSION_1_6;
    }

    // تهيئة ImGui/Renderer (مبكرة للتحقق من عدم وجود مشاكل EGL)
    // سيُستدعى Renderer::instance() لاحقاً عند أول surface

    // تحميل everything (dex + albatross)
    // ⚠️ لا يمكن أن نفشل هنا — وإلا كل شيء سيتعطل
    if (!g_yambz_vm = vm;
    yambz::EmbeddedLoader::initialize(env, vm)) {
        LOGE("EmbeddedLoader failed — mod will not work");
        // لا نرمي استثناء — نظهر فقط في logs
    }

    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM* vm, void* /*reserved*/) {
    LOGI("JNI_OnUnload — YAM-BZ");
}

// expose to other translation units
static JavaVM* g_yambz_vm = nullptr;
JavaVM* yambz_getGlobalVM() { return g_yambz_vm; }
