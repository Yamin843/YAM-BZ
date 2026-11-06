#include "OceanState.h"
#include "../core/Helpers.h"
#include "../core/Logger.h"

#include <jni.h>
#include <chrono>

namespace yambz {

OceanUIState& oceanUI() {
    static OceanUIState s;
    return s;
}

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void oceanPushEvent(OceanEvent ev) {
    auto& ui = oceanUI();
    std::lock_guard<std::mutex> lk(ui.eventsMu);
    ui.events.push_back(std::move(ev));
    while (ui.events.size() > ui.eventsCapacity) {
        ui.events.erase(ui.events.begin());
    }
    // Update counts inline
    const auto& back = ui.events.back();
    if (back.kind >= 0 && back.kind < 8) ui.countByKind[back.kind]++;
    ui.totalEvents++;
}

void oceanClearEvents() {
    auto& ui = oceanUI();
    std::lock_guard<std::mutex> lk(ui.eventsMu);
    ui.events.clear();
    for (int i = 0; i < 8; i++) ui.countByKind[i] = 0;
    ui.totalEvents = 0;
    ui.selectedEvent = -1;
}

int oceanKindMask() {
    auto& c = oceanUI().config;
    int m = 0;
    if (c.evCallEnter)  m |= 0x100;
    if (c.evCall)       m |= 0x02;
    if (c.evReturn)     m |= 0x10;
    if (c.evExec)       m |= 0x01;
    if (c.evBlock)      m |= 0x08;
    if (c.evFileRead)   m |= 0x20;
    if (c.evFieldWrite) m |= 0x40;
    return m;
}

static int scopeMask() {
    auto& c = oceanUI().config;
    int s = 0;
    if (c.scJdk)           s |= 1;
    if (c.scPlatform)      s |= 2;
    if (c.scApplication)   s |= 4;
    if (c.scPackageFilter) s |= 8;
    if (c.scEntryOnly)     s |= 16;
    return s;
}

bool oceanStart() {
    auto& ui = oceanUI();
    if (ui.tracing) return false;

    JavaVM* vm = yambz_getVM();
    if (!vm) return false;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }

    jclass cls = env->FindClass("com/yambz/feature/OceanController");
    if (!cls) { env->ExceptionClear(); LOGE("OceanController not found"); return false; }

    int mask = oceanKindMask();
    int scope = scopeMask();
    jstring pkgFilter = env->NewStringUTF(ui.config.packagePrefix);
    jstring sinkerKw = env->NewStringUTF(ui.config.sinkerKeyword);

    bool ok = false;
    jboolean bret = JNI_FALSE;

    if (ui.config.mode == 0) {
        jmethodID mid = env->GetStaticMethodID(cls, "startGlobal",
            "(IIIIILjava/lang/String;Ljava/lang/String;I)Z");
        if (mid) {
            bret = env->CallStaticBooleanMethod(cls, mid,
                mask, scope, ui.config.timeoutMs, ui.config.maxEvents,
                ui.config.sinkerKind, sinkerKw, pkgFilter,
                ui.config.onlyMainThread ? 1 : 0);
        }
    } else if (ui.config.mode == 1) {
        jmethodID mid = env->GetStaticMethodID(cls, "startThread",
            "(IIIIILjava/lang/String;Ljava/lang/String;I)Z");
        if (mid) {
            bret = env->CallStaticBooleanMethod(cls, mid,
                mask, scope, ui.config.timeoutMs, ui.config.maxEvents,
                ui.config.sinkerKind, sinkerKw, pkgFilter,
                ui.config.onlyMainThread ? 1 : 0);
        }
    } else {
        jmethodID mid = env->GetStaticMethodID(cls, "startMember",
            "(Ljava/lang/String;IIIIILjava/lang/String;I)Z");
        if (mid) {
            jstring spec = env->NewStringUTF(ui.config.memberSpec);
            bret = env->CallStaticBooleanMethod(cls, mid,
                spec, mask, scope, ui.config.timeoutMs, ui.config.maxEvents,
                ui.config.sinkerKind, sinkerKw,
                ui.config.onlyMainThread ? 1 : 0);
            env->DeleteLocalRef(spec);
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    env->DeleteLocalRef(pkgFilter);
    env->DeleteLocalRef(sinkerKw);

    ok = (bret == JNI_TRUE);
    if (ok) {
        ui.tracing = true;
        ui.startTs = nowMs();
        ui.stopReason = 0;
        oceanClearEvents();
        LOGI("Ocean started: mode=%d mask=0x%x scope=0x%x", ui.config.mode, mask, scope);
    } else {
        LOGE("Ocean start failed");
    }
    return ok;
}

void oceanStop() {
    auto& ui = oceanUI();
    if (!ui.tracing) return;

    JavaVM* vm = yambz_getVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    jclass cls = env->FindClass("com/yambz/feature/OceanController");
    if (cls) {
        jmethodID mid = env->GetStaticMethodID(cls, "stop", "()V");
        if (mid) env->CallStaticVoidMethod(cls, mid);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    ui.tracing = false;
    LOGI("Ocean stopped");
}

void oceanSync() {
    JavaVM* vm = yambz_getVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return;
    jclass cls = env->FindClass("com/yambz/feature/OceanController");
    if (!cls) { env->ExceptionClear(); return; }

    jmethodID mid_sync = env->GetStaticMethodID(cls, "syncTrace", "()V");
    if (mid_sync) env->CallStaticVoidMethod(cls, mid_sync);
    jmethodID mid_reason = env->GetStaticMethodID(cls, "getStopReason", "()I");
    if (mid_reason) {
        jint r = env->CallStaticIntMethod(cls, mid_reason);
        oceanUI().stopReason = (int) r;
    }
    jmethodID mid_cnt = env->GetStaticMethodID(cls, "getTraceCount", "()J");
    if (mid_cnt) {
        jlong c = env->CallStaticLongMethod(cls, mid_cnt);
        oceanUI().traceCount = (long) c;
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

} // namespace yambz
