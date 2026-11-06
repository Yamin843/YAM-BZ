#include "ClassEnumerator.h"
#include "Logger.h"

#include <thread>
#include <algorithm>
#include <cstring>

namespace yambz {

ClassEnumerator& ClassEnumerator::instance() {
    static ClassEnumerator e;
    return e;
}

// ─── helper: JNI local frame (يمنع تراكم 2000 ref) ───
struct JniFrame {
    JNIEnv* env;
    jint    capacity;
    JniFrame(JNIEnv* e, jint c = 64) : env(e), capacity(c) {
        env->PushLocalFrame(capacity);
    }
    ~JniFrame() { env->PopLocalFrame(nullptr); }
};

// ─── helper: split name → package + simpleName ───
static void splitName(const std::string& full,
                      std::string& outPkg, std::string& outSimple) {
    // نأخذ آخر '.' ليس داخل []
    size_t pos = full.rfind('.');
    if (pos == std::string::npos) {
        outPkg.clear();
        outSimple = full;
    } else {
        outPkg = full.substr(0, pos);
        outSimple = full.substr(pos + 1);
    }
    // إزالة $Prefix للـ simpleName
    size_t dollar = outSimple.rfind('$');
    if (dollar != std::string::npos && dollar + 1 < outSimple.size()) {
        outSimple = outSimple.substr(dollar + 1);
    }
}

// ─── helper: batch pull from Java helper ───
// Java helper API:
//   static boolean hasNextBatch()
//   static String[] nextBatch(int maxSize)   ← يرجع أسماء كلاسات
static std::vector<std::string> pullBatch(JNIEnv* env, jobject helper, int maxSize) {
    std::vector<std::string> out;
    if (!helper) return out;

    jclass cls = env->GetObjectClass(helper);
    jmethodID mid = env->GetMethodID(cls, "nextBatch", "(I)[Ljava/lang/String;");
    if (!mid) return out;

    jstring jempty = nullptr;
    jobjectArray arr = (jobjectArray) env->CallObjectMethod(helper, mid, maxSize);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return out;
    }
    if (!arr) return out;

    jsize len = env->GetArrayLength(arr);
    out.reserve(len);
    for (jsize i = 0; i < len; i++) {
        auto js = (jstring) env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) {
            out.emplace_back(c);
            env->ReleaseStringUTFChars(js, c);
        }
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out;
}

void ClassEnumerator::start(JNIEnv* env) {
    if (m_running.exchange(true)) {
        LOGI("ClassEnumerator: already running");
        return;
    }
    m_complete.store(false);
    m_cancelRequested.store(false);
    m_loadedBatches.store(0);

    env->GetJavaVM(&m_vm);

    // احصل على ClassBridge.getInstance() global ref
    jclass cls = env->FindClass("com/yambz/bridge/ClassBridge");
    if (!cls) {
        LOGE("ClassBridge not found");
        m_running.store(false);
        return;
    }
    jmethodID mid = env->GetStaticMethodID(cls, "getInstance",
                                            "()Lcom/yambz/bridge/ClassBridge;");
    jobject local = env->CallStaticObjectMethod(cls, mid);
    if (local) m_javaHelper = env->NewGlobalRef(local);

    std::thread([this]() {
        JNIEnv* e = nullptr;
        if (m_vm->AttachCurrentThread(&e, nullptr) != JNI_OK) {
            m_running.store(false);
            return;
        }
        workerMain(e);
        m_vm->DetachCurrentThread();
    }).detach();

    LOGI("ClassEnumerator: started");
}

void ClassEnumerator::workerMain(JNIEnv* env) {
    constexpr int BATCH = 1000;
    size_t total = 0;

    while (!m_cancelRequested.load()) {
        JniFrame frame(env, BATCH * 2);
        auto names = pullBatch(env, m_javaHelper, BATCH);
        if (names.empty()) break;

        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_classes.reserve(m_classes.size() + names.size());
            for (auto& n : names) {
                ClassInfo ci;
                ci.fullName = std::move(n);
                splitName(ci.fullName, ci.package, ci.simpleName);
                ci.memberCount = UINT32_MAX;
                ci.classLoaderIdx = -1;
                ci.flags = 0;
                m_classes.push_back(std::move(ci));
            }
            total = m_classes.size();
        }

        m_loadedBatches.fetch_add(1);

        if ((m_loadedBatches.load() & 7) == 0) {
            LOGI("ClassEnumerator: %zu classes loaded", total);
        }
    }

    // فرز حسب name (lowercase) — يسرّع البحث لاحقاً
    {
        std::lock_guard<std::mutex> lk(m_mu);
        std::sort(m_classes.begin(), m_classes.end(),
                  [](const ClassInfo& a, const ClassInfo& b) {
                      return a.fullName < b.fullName;
                  });
    }

    m_running.store(false);
    m_complete.store(true);
    LOGI("ClassEnumerator: done — %zu classes total", total);
}

void ClassEnumerator::cancel() {
    if (m_running.load()) {
        m_cancelRequested.store(true);
        LOGI("ClassEnumerator: cancel requested");
    }
}

float ClassEnumerator::progress() const {
    if (m_complete.load()) return 1.0f;
    // لا يمكن معرفة المجموع — نستخدم تقريب
    uint32_t b = m_loadedBatches.load();
    return std::min(1.0f, (float) b / 200.0f);
}

std::shared_ptr<const std::vector<ClassInfo>> ClassEnumerator::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return std::make_shared<const std::vector<ClassInfo>>(m_classes);
}

// ─── member enumeration ───
const std::vector<MemberInfo>& ClassEnumerator::membersOf(size_t classIdx) {
    {
        std::lock_guard<std::mutex> lk(m_membersMu);
        auto it = m_membersCache.find(classIdx);
        if (it != m_membersCache.end()) return it->second;
    }

    // جلب عبر Java helper
    std::vector<MemberInfo> result;
    if (m_javaHelper && m_vm) {
        JNIEnv* env = nullptr;
        if (m_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
            m_vm->AttachCurrentThread(&env, nullptr);
        }

        std::lock_guard<std::mutex> lk(m_mu);
        if (classIdx >= m_classes.size()) return result;
        const auto& ci = m_classes[classIdx];

        jclass cls = env->GetObjectClass(m_javaHelper);
        jmethodID mid = env->GetMethodID(cls, "getMembers",
            "(Ljava/lang/String;)[Ljava/lang/String;");
        if (mid) {
            jstring jname = env->NewStringUTF(ci.fullName.c_str());
            jobjectArray arr = (jobjectArray) env->CallObjectMethod(m_javaHelper, mid, jname);
            env->DeleteLocalRef(jname);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            } else if (arr) {
                jsize len = env->GetArrayLength(arr);
                result.reserve(len);
                // الشكل: "M|public|name|(sig)V|retType"
                //       "F|private|name|L...;|"
                //       "C|public|name|(args)V|"
                for (jsize i = 0; i < len; i++) {
                    auto js = (jstring) env->GetObjectArrayElement(arr, i);
                    const char* c = env->GetStringUTFChars(js, nullptr);
                    if (c) {
                        std::string s(c);
                        env->ReleaseStringUTFChars(js, c);
                        // parse
                        size_t p1 = s.find('|');
                        size_t p2 = s.find('|', p1 + 1);
                        size_t p3 = s.find('|', p2 + 1);
                        size_t p4 = s.find('|', p3 + 1);
                        if (p1 != std::string::npos && p2 != std::string::npos &&
                            p3 != std::string::npos && p4 != std::string::npos) {
                            MemberInfo mi;
                            char kind = s[0];
                            mi.kind = (kind == 'M') ? MemberKind::Method
                                    : (kind == 'C') ? MemberKind::Constructor
                                                    : MemberKind::Field;
                            std::string mods = s.substr(p1 + 1, p2 - p1 - 1);
                            mi.modifiers = 0;
                            if (mods.find("public")    != std::string::npos) mi.modifiers |= 0x01;
                            if (mods.find("private")   != std::string::npos) mi.modifiers |= 0x02;
                            if (mods.find("protected") != std::string::npos) mi.modifiers |= 0x04;
                            if (mods.find("static")    != std::string::npos) mi.modifiers |= 0x08;
                            if (mods.find("final")     != std::string::npos) mi.modifiers |= 0x10;
                            if (mods.find("abstract")  != std::string::npos) mi.modifiers |= 0x0400;
                            if (mods.find("native")    != std::string::npos) mi.modifiers |= 0x0100;
                            mi.name      = s.substr(p2 + 1, p3 - p2 - 1);
                            mi.signature = s.substr(p3 + 1, p4 - p3 - 1);
                            mi.returnType = s.substr(p4 + 1);
                            result.push_back(std::move(mi));
                        }
                    }
                    env->DeleteLocalRef(js);
                }
                env->DeleteLocalRef(arr);
            }
        }
    }

    std::lock_guard<std::mutex> lk(m_membersMu);
    auto [it, inserted] = m_membersCache.emplace(classIdx, std::move(result));
    return it->second;
}

void ClassEnumerator::clear() {
    cancel();
    std::lock_guard<std::mutex> lk(m_mu);
    m_classes.clear();
    m_complete.store(false);
    std::lock_guard<std::mutex> lk2(m_membersMu);
    m_membersCache.clear();
}

} // namespace yambz
