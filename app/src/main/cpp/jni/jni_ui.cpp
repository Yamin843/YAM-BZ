#include <jni.h>
#include <android/log.h>
#include <cstring>

#include "core/Helpers.h"
#include "core/Logger.h"
#include "ui/Renderer.h"
#include "ui/MainWindow.h"
#include "ui/Notifications.h"

// ═══════════════════════════════════════════════════════════
//  Global state
// ═══════════════════════════════════════════════════════════
static std::string g_cacheDir = "/data/local/tmp";

// ═══════════════════════════════════════════════════════════
//  Helpers (implementations for Helpers.h)
// ═══════════════════════════════════════════════════════════
std::string yambz_getCacheDir() { return g_cacheDir; }
void yambz_setCacheDir(const char* d) { if (d) g_cacheDir = d; }

void yambz_notify(const char* msg) {
    if (msg) yambz::Notifications::instance().push(msg, 2.5f);
}

// ═══════════════════════════════════════════════════════════
//  fab_setJavaVM / panel_setJavaVM — placeholders
//  (FAB/Panel are managed from Java; C++ only receives renders)
// ═══════════════════════════════════════════════════════════
void fab_setJavaVM(JavaVM*) {}
void panel_setJavaVM(JavaVM*) {}

// ═══════════════════════════════════════════════════════════
//  UIBridge JNI
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_UIBridge_nativeSetCacheDir(JNIEnv* env, jclass, jstring dir) {
    if (!dir) return;
    const char* c = env->GetStringUTFChars(dir, nullptr);
    if (c) { g_cacheDir = c; env->ReleaseStringUTFChars(dir, c); }
    LOGI("cacheDir set: %s", g_cacheDir.c_str());
}

// ═══════════════════════════════════════════════════════════
//  PanelBridge JNI
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnSurfaceCreated(JNIEnv*, jclass) {
    yambz::Renderer::instance().onSurfaceCreated();
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnSurfaceChanged(JNIEnv*, jclass, jint w, jint h) {
    yambz::Renderer::instance().onSurfaceChanged(w, h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnDrawFrame(JNIEnv*, jclass, jint w, jint h) {
    yambz::Renderer::instance().onDrawFrame(w, h);
    yambz::Notifications::instance().draw();
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnTouch(JNIEnv*, jclass,
                                                jint action, jfloat x, jfloat y, jint pid) {
    yambz::Renderer::instance().onTouch(action, x, y, pid);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnScroll(JNIEnv*, jclass, jfloat dx, jfloat dy) {
    yambz::Renderer::instance().onScroll(dx, dy);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_PanelBridge_nativeHitTest(JNIEnv*, jclass, jfloat x, jfloat y) {
    return yambz::MainWindow::instance().hitTest(x, y) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_PanelBridge_nativeWantsAnotherFrame(JNIEnv*, jclass) {
    return yambz::Renderer::instance().wantsAnotherFrame() ? JNI_TRUE : JNI_FALSE;
}

// ═══════════════════════════════════════════════════════════
//  yambz_requestClosePanel — called from MainWindow X button
// ═══════════════════════════════════════════════════════════
void yambz_requestClosePanel() {
    JavaVM* vm = yambz_getVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass cls = env->FindClass("com/yambz/bridge/UIBridge");
    if (!cls) { env->ExceptionClear(); return; }
    jmethodID mid = env->GetStaticMethodID(cls, "hidePanel", "()V");
    if (mid) env->CallStaticVoidMethod(cls, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
}
