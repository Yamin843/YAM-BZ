#include "FabManager.h"
#include "PanelManager.h"
#include "MainWindow.h"
#include "Notifications.h"
#include "../core/Logger.h"
#include <mutex>

#include <jni.h>
#include "core/Helpers.h"

namespace yambz {

static JavaVM* g_vm = nullptr;

// يجب أن يُستدعى من jni_bridge.cpp


FabManager& FabManager::instance() {
    static FabManager f;
    return f;
}

FabManager::~FabManager() {
    detach();
}

static JNIEnv* getEnv() {
    if (!g_vm) return nullptr;
    JNIEnv* e = nullptr;
    if (g_vm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
    }
    return e;
}

void FabManager::attach(JNIEnv* env, jobject activity) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!env) env = getEnv();
    if (!env || !activity) return;

    if (m_fab) {
        // بالفعل مرفق — فقط تأكد من ظهوره
        setVisible(true);
        return;
    }

    // ── DecorView ──
    jclass cls_Activity = env->FindClass("android/app/Activity");
    jmethodID mid_getWindow = env->GetMethodID(cls_Activity, "getWindow", "()Landroid/view/Window;");
    jobject window = env->CallObjectMethod(activity, mid_getWindow);
    jclass cls_Window = env->FindClass("android/view/Window");
    jmethodID mid_getDecor = env->GetMethodID(cls_Window, "getDecorView", "()Landroid/view/View;");
    jobject decorView = env->CallObjectMethod(window, mid_getDecor);

    if (!decorView) {
        LOGE("FabManager: decorView null");
        return;
    }

    // ── FabBridge ──
    jclass cls_Fab = env->FindClass("com/yambz/bridge/FabBridge");
    if (!cls_Fab) {
        LOGE("FabManager: FabBridge class not found");
        return;
    }
    jmethodID mid_ctor = env->GetMethodID(cls_Fab, "<init>", "(Landroid/content/Context;)V");
    jobject fab = env->NewObject(cls_Fab, mid_ctor, activity);
    if (!fab) {
        LOGE("FabManager: FabBridge ctor failed");
        return;
    }

    // إضافة إلى DecorView
    jclass cls_ViewGroup = env->FindClass("android/view/ViewGroup");
    jmethodID mid_add = env->GetMethodID(cls_ViewGroup, "addView", "(Landroid/view/View;)V");
    env->CallVoidMethod(decorView, mid_add, fab);

    // ── حفظ GlobalRefs ──
    m_fab       = env->NewGlobalRef(fab);
    m_decorView = env->NewGlobalRef(decorView);
    m_activity  = env->NewGlobalRef(activity);

    m_visible = true;

    env->DeleteLocalRef(fab);
    env->DeleteLocalRef(decorView);
    env->DeleteLocalRef(window);

    LOGI("FabManager: FAB attached ✓");
}

void FabManager::detach() {
    std::lock_guard<std::mutex> lk(m_mu);
    JNIEnv* env = getEnv();
    if (!env) return;

    if (m_fab && m_decorView) {
        jclass cls_ViewGroup = env->FindClass("android/view/ViewGroup");
        jmethodID mid_rm = env->GetMethodID(cls_ViewGroup, "removeView", "(Landroid/view/View;)V");
        env->CallVoidMethod(m_decorView, mid_rm, m_fab);
    }
    if (m_fab)       { env->DeleteGlobalRef(m_fab);       m_fab = nullptr; }
    if (m_decorView) { env->DeleteGlobalRef(m_decorView); m_decorView = nullptr; }
    if (m_activity)  { env->DeleteGlobalRef(m_activity);  m_activity = nullptr; }
    m_visible = false;
    LOGI("FabManager: detached");
}

void FabManager::setVisible(bool visible) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_fab) return;
    JNIEnv* env = getEnv();
    if (!env) return;

    jclass cls_Fab = env->GetObjectClass(m_fab);
    jmethodID mid = env->GetMethodID(cls_Fab, "setVisibleFromAnyThread", "(Z)V");
    if (mid) {
        env->CallVoidMethod(m_fab, mid, visible ? JNI_TRUE : JNI_FALSE);
    }
    m_visible = visible;
}

void FabManager::onFabTapped() {
    LOGI("FabManager: tapped → opening panel");
    PanelManager::instance().show();
    setVisible(false);
}

} // namespace yambz

// Global scope: matches Helpers.h declaration
void fab_setJavaVM(JavaVM* vm) { g_vm = vm; }
