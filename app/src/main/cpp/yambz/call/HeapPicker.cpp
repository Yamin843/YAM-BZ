#include "HeapPicker.h"
#include <algorithm>
#include "../core/Logger.h"
#include "../core/ErrorContext.h"

#include <jni.h>
#include <mutex>
#include <vector>

namespace yambz {

struct HeapPicker::State {
    std::vector<HeapEntry> entries;
    std::string lastError;
    bool completed = false;
    mutable std::mutex mu;
};

HeapPicker& HeapPicker::instance() {
    static HeapPicker h;
    return h;
}

HeapPicker::HeapPicker() : m_state(std::make_unique<State>()) {}
HeapPicker::~HeapPicker() = default;

static JavaVM* getVM() {
    extern JavaVM* yambz_getGlobalVM();
    return yambz_getGlobalVM();
}

void HeapPicker::startSearch(const std::string& typeName) {
    if (m_searching.exchange(true)) {
        LOGW("HeapPicker: already searching");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_state->mu);
        m_state->entries.clear();
        m_state->lastError.clear();
        m_state->completed = false;
    }
    m_seen.store(0);

    std::thread([this, typeName]() {
        JavaVM* vm = getVM();
        if (!vm) { m_searching.store(false); return; }
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            m_searching.store(false);
            return;
        }

        // find class
        jclass cls_Class = env->FindClass("java/lang/Class");
        jmethodID mid_forName = env->GetStaticMethodID(cls_Class, "forName",
            "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring jn = env->NewStringUTF(typeName.c_str());
        jobject jcls = env->CallStaticObjectMethod(cls_Class, mid_forName, jn);
        env->DeleteLocalRef(jn);
        if (!jcls) {
            env->ExceptionClear();
            m_searching.store(false);
            vm->DetachCurrentThread();
            return;
        }

        // call HeapScanner.start
        jclass cls_HS = env->FindClass("com/yambz/call/HeapScanner");
        jmethodID mid_start = env->GetStaticMethodID(cls_HS, "start",
            "(Ljava/lang/Class;Lcom/yambz/call/HeapScanner$Callback;)Lcom/yambz/call/HeapScanner;");
        // نحتاج Callback impl — نستخدم NativeCallback.java
        jclass cls_NC = env->FindClass("com/yambz/call/HeapScannerCallback");
        jmethodID mid_ctor = env->GetMethodID(cls_NC, "<init>", "()V");
        jobject cb = env->NewObject(cls_NC, mid_ctor);
        env->CallStaticObjectMethod(cls_HS, mid_start, jcls, cb);

        env->DeleteLocalRef(cb);
        env->DeleteLocalRef(jcls);

        vm->DetachCurrentThread();
    }).detach();
}

void HeapPicker::cancel() {
    JavaVM* vm = getVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);
    jclass cls_NC = env->FindClass("com/yambz/call/HeapScannerCallback");
    jmethodID mid = env->GetStaticMethodID(cls_NC, "cancelCurrent", "()V");
    if (mid) env->CallStaticVoidMethod(cls_NC, mid);
    m_searching.store(false);
}

void HeapPicker::pushBatch(JNIEnv* env, jobjectArray entries) {
    if (!entries) return;
    jsize n = env->GetArrayLength(entries);
    std::lock_guard<std::mutex> lk(m_state->mu);
    m_state->entries.reserve(m_state->entries.size() + n);

    // كل entry هو Object[] {int idx, long addr, String display, Object ref}
    for (jsize i = 0; i < n; i++) {
        jobject entry = env->GetObjectArrayElement(entries, i);
        if (!entry) continue;
        jclass cls = env->GetObjectClass(entry);
        jmethodID m0 = env->GetMethodID(cls, "getInt0", "()I");
        jmethodID m1 = env->GetMethodID(cls, "getLong0", "()J");
        jmethodID m2 = env->GetMethodID(cls, "getStr0", "()Ljava/lang/String;");
        jmethodID m3 = env->GetMethodID(cls, "getRef0", "()Ljava/lang/Object;");

        HeapEntry e;
        if (m0) e.idx = (uint32_t) env->CallIntMethod(entry, m0);
        if (m1) e.addr = (uint64_t) env->CallLongMethod(entry, m1);
        if (m2) {
            jstring js = (jstring) env->CallObjectMethod(entry, m2);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                e.display = c ? c : "";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        if (m3) {
            jobject ref = env->CallObjectMethod(entry, m3);
            if (ref) e.ref = env->NewGlobalRef(ref);
        }
        m_state->entries.push_back(std::move(e));
        env->DeleteLocalRef(entry);
    }
    m_seen.store((int) m_state->entries.size());
}

void HeapPicker::onComplete(int total) {
    m_seen.store(total);
    m_searching.store(false);
    {
        std::lock_guard<std::mutex> lk(m_state->mu);
        m_state->completed = true;
    }
    LOGI("HeapPicker: complete — %d instances", total);
}

void HeapPicker::onError(JNIEnv* env, jobject throwable) {
    m_searching.store(false);
    std::string msg = "unknown";
    if (throwable) {
        jclass cls_T = env->FindClass("java/lang/Throwable");
        jmethodID mid_toStr = env->GetMethodID(cls_T, "toString", "()Ljava/lang/String;");
        jstring js = (jstring) env->CallObjectMethod(throwable, mid_toStr);
        if (js) {
            const char* c = env->GetStringUTFChars(js, nullptr);
            msg = c ? c : "unknown";
            env->ReleaseStringUTFChars(js, c);
            env->DeleteLocalRef(js);
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_state->mu);
        m_state->lastError = msg;
    }
    YAMBZ_ERR(ErrDomain::Reflection, "heap.scan", "", msg);
}

std::vector<HeapEntry> HeapPicker::snapshot(size_t offset, size_t limit) const {
    std::lock_guard<std::mutex> lk(m_state->mu);
    std::vector<HeapEntry> out;
    size_t n = m_state->entries.size();
    if (offset >= n) return out;
    size_t end = std::min(n, offset + limit);
    out.reserve(end - offset);
    for (size_t i = offset; i < end; i++)
        out.push_back(m_state->entries[i]);
    return out;
}

size_t HeapPicker::totalResults() const {
    std::lock_guard<std::mutex> lk(m_state->mu);
    return m_state->entries.size();
}

jobject HeapPicker::pick(int idx) {
    std::lock_guard<std::mutex> lk(m_state->mu);
    if (idx < 0 || idx >= (int) m_state->entries.size()) return nullptr;
    return m_state->entries[idx].ref;
}

void HeapPicker::clear(JNIEnv* env) {
    std::lock_guard<std::mutex> lk(m_state->mu);
    if (env) {
        for (auto& e : m_state->entries)
            if (e.ref) env->DeleteGlobalRef(e.ref);
    }
    m_state->entries.clear();
    m_state->lastError.clear();
    m_state->completed = false;
    m_seen.store(0);
}

} // namespace yambz
