#include <jni.h>
#include <android/log.h>

#include "ui/Renderer.h"
#include "ui/MainWindow.h"
#include "ui/FabManager.h"
#include "ui/PanelManager.h"
#include "ui/Notifications.h"
#include "core/Logger.h"

using namespace yambz;

// forward declarations of init helpers
extern void fab_setJavaVM(JavaVM* vm);
extern void panel_setJavaVM(JavaVM* vm);

// ═══════════════════════════════════════════════════════════
//  ModManager — lifecycle
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModManager_nativeOnActivityCreated(JNIEnv*, jclass, jobject) {
    LOGD("nativeOnActivityCreated");
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModManager_nativeOnActivityReady(JNIEnv* env, jclass, jobject activity) {
    LOGI("nativeOnActivityReady → attaching FAB + Panel");

    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    fab_setJavaVM(vm);
    panel_setJavaVM(vm);

    // Panel أولاً (فوق المحتوى) — ثم FAB فوقه
    PanelManager::instance().attach(env, activity);
    FabManager::instance().attach(env, activity);

    // أخفِ الـ Panel افتراضياً (MainWindow.hide)
    MainWindow::instance().hide();
    FabManager::instance().setVisible(true);

    Notifications::instance().push("YAM-BZ loaded ✓", 2.5f);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModManager_nativeOnActivityPaused(JNIEnv*, jclass, jobject) {
    // لا نفعل شيئاً — الـ FAB يبقى، الـ Panel يبقى مخفياً
    LOGD("nativeOnActivityPaused");
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModManager_nativeOnActivityDestroyed(JNIEnv*, jclass, jobject) {
    LOGD("nativeOnActivityDestroyed → detaching");
    FabManager::instance().detach();
    PanelManager::instance().detach();
}

// ═══════════════════════════════════════════════════════════
//  FabBridge
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_FabBridge_nativeFabTapped(JNIEnv*, jclass) {
    LOGI("FabBridge.nativeFabTapped");
    FabManager::instance().onFabTapped();
}

// ═══════════════════════════════════════════════════════════
//  PanelBridge — renderer callbacks
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnSurfaceCreated(JNIEnv*, jclass) {
    Renderer::instance().onSurfaceCreated();
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnSurfaceChanged(JNIEnv*, jobject, jint w, jint h) {
    Renderer::instance().onSurfaceChanged(w, h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnDrawFrame(JNIEnv*, jobject, jint w, jint h) {
    Renderer::instance().onDrawFrame(w, h);

    // close request من زر X في MainWindow
    if (MainWindow::instance().consumeCloseRequest()) {
        PanelManager::instance().hide();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_PanelBridge_nativeHitTest(JNIEnv*, jobject, jfloat x, jfloat y) {
    if (!MainWindow::instance().visible()) return JNI_FALSE;
    return MainWindow::instance().hitTest(x, y) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnTouch(JNIEnv*, jobject, jint action,
                                                jfloat x, jfloat y, jint pid) {
    Renderer::instance().onTouch(action, x, y, pid);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeOnScroll(JNIEnv*, jobject, jfloat dx, jfloat dy) {
    Renderer::instance().onScroll(dx, dy);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_PanelBridge_nativeWantsAnotherFrame(JNIEnv*, jclass) {
    return Renderer::instance().wantsAnotherFrame() ? JNI_TRUE : JNI_FALSE;
}
