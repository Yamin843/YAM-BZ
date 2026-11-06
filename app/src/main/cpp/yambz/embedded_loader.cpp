#include "embedded_loader.h"
#include "embed/embedded_dex.h"
#include "embed/embedded_albatross.h"

#include <android/log.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <vector>

#include <jni.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "YAMBZ", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "YAMBZ", __VA_ARGS__)

namespace yambz {

std::string EmbeddedLoader::s_albatrossSoPath;
std::string EmbeddedLoader::s_dexPath;
bool        EmbeddedLoader::s_ready = false;

static JavaVM* g_vm = nullptr;
static jobject g_classLoader = nullptr;
static std::mutex g_mu;

// ═════════════════════════════════════════════════════════════
//  utils
// ═════════════════════════════════════════════════════════════
bool EmbeddedLoader::writeFile(const std::string& path, const void* data, size_t len) {
    // أنشئ ملفاً بصلاحيات 0700 + فك التنفيذ
    int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0700);
    if (fd < 0) {
        LOGE("open(%s) failed: %s", path.c_str(), strerror(errno));
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t written = 0;
    while (written < len) {
        ssize_t w = ::write(fd, p + written, len - written);
        if (w <= 0) {
            LOGE("write(%s) failed at %zu: %s", path.c_str(), written, strerror(errno));
            ::close(fd);
            ::unlink(path.c_str());
            return false;
        }
        written += (size_t)w;
    }
    ::fsync(fd);
    ::close(fd);

    // executable bit ضروري للـ .so
    if (::chmod(path.c_str(), 0700) != 0) {
        LOGE("chmod(%s) failed: %s", path.c_str(), strerror(errno));
        return false;
    }
    LOGI("wrote %zu bytes to %s", len, path.c_str());
    return true;
}

jobject EmbeddedLoader::currentApplication(JNIEnv* env) {
    // الطريقة السريعة: ActivityThread.currentApplication
    jclass cls_AT = env->FindClass("android/app/ActivityThread");
    if (!cls_AT) return nullptr;
    jmethodID mid = env->GetStaticMethodID(cls_AT, "currentApplication", "()Landroid/app/Application;");
    if (!mid) { env->ExceptionClear(); return nullptr; }
    jobject app = env->CallStaticObjectMethod(cls_AT, mid);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return app;
}

std::string EmbeddedLoader::getCodeCacheDir(JNIEnv* env) {
    jobject app = currentApplication(env);
    if (!app) {
        LOGE("currentApplication null — fallback to /data/local/tmp");
        return "/data/local/tmp/yambz";
    }
    jclass cls_ctx = env->FindClass("android/content/Context");
    jmethodID mid = env->GetMethodID(cls_ctx, "getCodeCacheDir", "()Ljava/io/File;");
    jobject file = env->CallObjectMethod(app, mid);
    if (!file) return "/data/local/tmp/yambz";

    jclass cls_file = env->FindClass("java/io/File");
    jmethodID mid_path = env->GetMethodID(cls_file, "getAbsolutePath", "()Ljava/lang/String;");
    auto js = (jstring) env->CallObjectMethod(file, mid_path);
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string out = c ? c : "/data/local/tmp/yambz";
    env->ReleaseStringUTFChars(js, c);
    env->DeleteLocalRef(js);
    env->DeleteLocalRef(file);
    env->DeleteLocalRef(app);
    return out;
}

// ═════════════════════════════════════════════════════════════
//  loadDexIntoVm — InMemoryDexClassLoader
// ═════════════════════════════════════════════════════════════
bool EmbeddedLoader::loadDexIntoVm(JNIEnv* env, const std::string& /*dexPath*/,
                                   jobject* outLoader) {
    // نبني ByteBuffer[] واحد لكل dex
    jclass cls_BB = env->FindClass("java/nio/ByteBuffer");
    jmethodID mid_wrap = env->GetStaticMethodID(cls_BB, "wrap", "([B)Ljava/nio/ByteBuffer;");

    jobjectArray bufferArray = env->NewObjectArray(
        (jsize)YAMBZ_DEX_TABLE_LEN, cls_BB, nullptr);

    for (size_t i = 0; i < YAMBZ_DEX_TABLE_LEN; i++) {
        const auto& e = YAMBZ_DEX_TABLE[i];
        jbyteArray arr = env->NewByteArray((jsize)e.len);
        env->SetByteArrayRegion(arr, 0, (jsize)e.len, (const jbyte*)e.data);
        jobject buf = env->CallStaticObjectMethod(cls_BB, mid_wrap, arr);
        env->SetObjectArrayElement(bufferArray, (jsize)i, buf);
        env->DeleteLocalRef(arr);
        env->DeleteLocalRef(buf);
    }

    // classloader أب: system classloader
    jclass cls_ClassLoader = env->FindClass("java/lang/ClassLoader");
    jmethodID mid_getSys = env->GetStaticMethodID(cls_ClassLoader, "getSystemClassLoader",
                                                   "()Ljava/lang/ClassLoader;");
    jobject parent = env->CallStaticObjectMethod(cls_ClassLoader, mid_getSys);

    // InMemoryDexClassLoader(ByteBuffer[], ClassLoader)
    jclass cls_IMDCL = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (!cls_IMDCL) {
        LOGE("InMemoryDexClassLoader not available (need API 26+)");
        env->ExceptionClear();
        return false;
    }
    jmethodID mid_ctor = env->GetMethodID(cls_IMDCL, "<init>",
        "([Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    jobject loader = env->NewObject(cls_IMDCL, mid_ctor, bufferArray, parent);

    if (env->ExceptionCheck()) {
        LOGE("InMemoryDexClassLoader ctor threw");
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }
    *outLoader = loader;
    return true;
}

// ═════════════════════════════════════════════════════════════
//  invokeBootstrap — نداء ModBootstrap.bootstrap()
// ═════════════════════════════════════════════════════════════
bool EmbeddedLoader::invokeBootstrap(JNIEnv* env, jobject loader,
                                     const std::string& albSoPath,
                                     const std::string& dexPath) {
    jclass cls_loader = env->GetObjectClass(loader);
    jmethodID mid_loadClass = env->GetMethodID(cls_loader, "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");

    jstring jname = env->NewStringUTF("com.yambz.bridge.ModBootstrap");
    jclass cls_bootstrap = (jclass) env->CallObjectMethod(loader, mid_loadClass, jname);
    env->DeleteLocalRef(jname);

    if (!cls_bootstrap || env->ExceptionCheck()) {
        LOGE("ModBootstrap class not found in dex");
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }

    jmethodID mid = env->GetStaticMethodID(cls_bootstrap, "bootstrap",
        "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (!mid) { env->ExceptionClear(); return false; }

    jstring jso  = env->NewStringUTF(albSoPath.c_str());
    jstring jdex = env->NewStringUTF(dexPath.c_str());
    jboolean ok = env->CallStaticBooleanMethod(cls_bootstrap, mid, jso, jdex);

    if (env->ExceptionCheck()) {
        LOGE("ModBootstrap.bootstrap threw");
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(jso);
        env->DeleteLocalRef(jdex);
        return false;
    }
    env->DeleteLocalRef(jso);
    env->DeleteLocalRef(jdex);
    LOGI("ModBootstrap.bootstrap returned: %d", (int)ok);
    return ok == JNI_TRUE;
}

// ═════════════════════════════════════════════════════════════
//  initialize — نقطة الدخول
// ═════════════════════════════════════════════════════════════
bool EmbeddedLoader::initialize(JNIEnv* env, JavaVM* vm) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (s_ready) return true;
    g_vm = vm;

    // 1) code_cache path
    std::string cacheDir = getCodeCacheDir(env);
    ::mkdir(cacheDir.c_str(), 0700);
    LOGI("cache dir: %s", cacheDir.c_str());

    // 2) استخراج libalbatross_base.so
    s_albatrossSoPath = cacheDir + "/libalbatross_base.so";
    // احذف نسخة قديمة لتجنب مشاكل
    ::unlink(s_albatrossSoPath.c_str());

    if (!writeFile(s_albatrossSoPath, YAMBZ_ALB, YAMBZ_ALB_LEN)) {
        LOGE("failed to extract libalbatross_base.so");
        return false;
    }

    // 3) dex path (معلوماتي فقط — التحميل في الذاكرة)
    s_dexPath = cacheDir + "/classes.dex";

    // 4) تحميل dex
    jobject loader = nullptr;
    if (!loadDexIntoVm(env, s_dexPath, &loader)) {
        LOGE("loadDexIntoVm failed");
        return false;
    }

    // 5) استدعاء ModBootstrap
    if (!invokeBootstrap(env, loader, s_albatrossSoPath, s_dexPath)) {
        LOGE("invokeBootstrap failed");
        return false;
    }

    g_classLoader = env->NewGlobalRef(loader);
    env->DeleteLocalRef(loader);

    s_ready = true;
    LOGI("EmbeddedLoader ready ✓");
    return true;
}

} // namespace yambz
