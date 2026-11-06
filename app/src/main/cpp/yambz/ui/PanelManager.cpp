#include "PanelManager.h"
#include "FabManager.h"
#include "MainWindow.h"
#include "../core/Logger.h"
#include <mutex>

#include <jni.h>

namespace yambz {

static JavaVM* g_vm_p = nullptr;
void panel_setJavaVM(JavaVM* vm) { g_vm_p = vm; }

PanelManager& PanelManager::instance() {
    static PanelManager p;
    return p;
}

PanelManager::~PanelManager() { detach(); }

static JNIEnv* getEnvP() {
    if (!g_vm_p) return nullptr;
    JNIEnv* e = nullptr;
    if (g_vm_p->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm_p->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
    }
    return e;
}

void PanelManager::attach(JNIEnv* env, jobject activity) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!env) env = getEnvP();
    if (!env || !activity) return;
    if (m_panel) return;  // موجود

    // DecorView
    jclass cls_Activity = env->FindClass("android/app/Activity");
    jmethodID mid_getWindow = env->GetMethodID(cls_Activity, "getWindow", "()Landroid/view/Window;");
    jobject window = env->CallObjectMethod(activity, mid_getWindow);
    jclass cls_Window = env->FindClass("android/view/Window");
    jmethodID mid_getDecor = env->GetMethodID(cls_Window, "getDecorView", "()Landroid/view/View;");
    jobject decorView = env->CallObjectMethod(window, mid_getDecor);

    if (!decorView) {
        LOGE("PanelManager: decorView null");
        return;
    }

    // PanelBridge
    jclass cls_Panel = env->FindClass("com/yambz/bridge/PanelBridge");
    if (!cls_Panel) {
        LOGE("PanelManager: PanelBridge class not found");
        return;
    }
    jmethodID mid_ctor = env->GetMethodID(cls_Panel, "<init>", "(Landroid/content/Context;)V");
    jobject panel = env->NewObject(cls_Panel, mid_ctor, activity);
    if (!panel) {
        LOGE("PanelManager: PanelBridge ctor failed");
        return;
    }

    // إضافة إلى DecorView (MATCH_PARENT)
    jclass cls_VG = env->FindClass("android/view/ViewGroup");
    jmethodID mid_add = env->GetMethodID(cls_VG, "addView", "(Landroid/view/View;)V");
    env->CallVoidMethod(decorView, mid_add, panel);

    m_panel     = env->NewGlobalRef(panel);
    m_decorView = env->NewGlobalRef(decorView);

    env->DeleteLocalRef(panel);
    env->DeleteLocalRef(decorView);
    env->DeleteLocalRef(window);

    LOGI("PanelManager: PanelBridge attached ✓");
}

void PanelManager::detach() {
    std::lock_guard<std::mutex> lk(m_mu);
    JNIEnv* env = getEnvP();
    if (!env) return;

    if (m_panel && m_decorView) {
        jclass cls_VG = env->FindClass("android/view/ViewGroup");
        jmethodID mid_rm = env->GetMethodID(cls_VG, "removeView", "(Landroid/view/View;)V");
        env->CallVoidMethod(m_decorView, mid_rm, m_panel);
    }
    if (m_panel)     { env->DeleteGlobalRef(m_panel);     m_panel = nullptr; }
    if (m_decorView) { env->DeleteGlobalRef(m_decorView); m_decorView = nullptr; }
    m_visible = false;
    LOGI("PanelManager: detached");
}

void PanelManager::show() {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_panel) {
        LOGE("PanelManager: cannot show — panel not attached");
        return;
    }
    m_visible = true;
    MainWindow::instance().show();

    JNIEnv* env = getEnvP();
    if (!env) return;
    jclass cls = env->GetObjectClass(m_panel);
    jmethodID mid = env->GetMethodID(cls, "requestRenderFromAnyThread", "()V");
    if (mid) env->CallVoidMethod(m_panel, mid);
}

void PanelManager::hide() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_visible = false;
    MainWindow::instance().hide();

    // أعد إظهار FAB
    FabManager::instance().setVisible(true);

    // render إطار أخير (لإفراغ الشاشة)
    JNIEnv* env = getEnvP();
    if (env && m_panel) {
        jclass cls = env->GetObjectClass(m_panel);
        jmethodID mid = env->GetMethodID(cls, "requestRenderFromAnyThread", "()V");
        if (mid) env->CallVoidMethod(m_panel, mid);
    }
}

} // namespace yambz
