#include "embedded_loader.h"
#include "core/Helpers.h"
#include "core/Logger.h"
#include "generated/embedded_assets.h"

#include <jni.h>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace yambz {

// ═══════════════════════════════════════════════════════════
//  writeFile — write bytes to path, chmod 0700
// ═══════════════════════════════════════════════════════════
static bool writeFile(const std::string& path, const void* data, size_t len) {
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
            LOGE("write(%s) failed at %zu", path.c_str(), written);
            ::close(fd);
            ::unlink(path.c_str());
            return false;
        }
        written += (size_t)w;
    }
    ::fsync(fd);
    ::close(fd);
    ::chmod(path.c_str(), 0700);
    return true;
}

// ═══════════════════════════════════════════════════════════
//  getCacheDir — via ActivityThread.currentApplication()
// ═══════════════════════════════════════════════════════════
static std::string getCacheDir(JNIEnv* env) {
    jclass cls_AT = env->FindClass("android/app/ActivityThread");
    if (!cls_AT) { env->ExceptionClear(); return "/data/local/tmp"; }

    jmethodID mid = env->GetStaticMethodID(cls_AT, "currentApplication",
        "()Landroid/app/Application;");
    if (!mid) { env->ExceptionClear(); return "/data/local/tmp"; }

    jobject app = env->CallStaticObjectMethod(cls_AT, mid);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return "/data/local/tmp"; }
    if (!app) return "/data/local/tmp";

    jclass cls_ctx = env->FindClass("android/content/Context");
    jmethodID mid_ccd = env->GetMethodID(cls_ctx, "getCodeCacheDir",
        "()Ljava/io/File;");
    jobject file = env->CallObjectMethod(app, mid_ccd);
    if (!file) { env->DeleteLocalRef(app); return "/data/local/tmp"; }

    jclass cls_f = env->FindClass("java/io/File");
    jmethodID mid_ap = env->GetMethodID(cls_f, "getAbsolutePath",
        "()Ljava/lang/String;");
    jstring js = (jstring) env->CallObjectMethod(file, mid_ap);
    std::string out = "/data/local/tmp";
    if (js) {
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c) out = c;
        env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(file);
    env->DeleteLocalRef(app);
    return out;
}

// ═══════════════════════════════════════════════════════════
//  loadDexAndCallBootstrap
// ═══════════════════════════════════════════════════════════
static bool loadDexAndBootstrap(JNIEnv* env, const std::string& albSoPath,
                                 const std::string& cacheDir) {
    // 1) Build ByteBuffer from embedded dex
    jclass cls_BB = env->FindClass("java/nio/ByteBuffer");
    jmethodID mid_wrap = env->GetStaticMethodID(cls_BB, "wrap", "([B)Ljava/nio/ByteBuffer;");
    jbyteArray arr = env->NewByteArray((jsize)YAMBZ_DEX_LEN);
    env->SetByteArrayRegion(arr, 0, (jsize)YAMBZ_DEX_LEN,
        (const jbyte*)YAMBZ_DEX_BYTES);
    jobject buffer = env->CallStaticObjectMethod(cls_BB, mid_wrap, arr);
    env->DeleteLocalRef(arr);

    // 2) InMemoryDexClassLoader
    jclass cls_IMDCL = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    jmethodID mid_ctor = env->GetMethodID(cls_IMDCL, "<init>",
        "([Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");

    jclass cls_ClassLoader = env->FindClass("java/lang/ClassLoader");
    jmethodID mid_sys = env->GetStaticMethodID(cls_ClassLoader, "getSystemClassLoader",
        "()Ljava/lang/ClassLoader;");
    jobject parent = env->CallStaticObjectMethod(cls_ClassLoader, mid_sys);

    jobjectArray bufs = env->NewObjectArray(1, cls_BB, buffer);
    jobject loader = env->NewObject(cls_IMDCL, mid_ctor, bufs, parent);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(bufs);
        return false;
    }

    // 3) Load ModBootstrap from that loader
    jclass cls_loader = env->GetObjectClass(loader);
    jmethodID mid_loadClass = env->GetMethodID(cls_loader, "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jname = env->NewStringUTF("com.yambz.ModBootstrap");
    jclass cls_boot = (jclass) env->CallObjectMethod(loader, mid_loadClass, jname);
    env->DeleteLocalRef(jname);

    if (!cls_boot) {
        LOGE("ModBootstrap not found in dex");
        env->ExceptionClear();
        env->DeleteLocalRef(loader);
        return false;
    }

    // 4) ModBootstrap.bootstrap(albSoPath, cacheDir)
    jmethodID mid_boot = env->GetStaticMethodID(cls_boot, "bootstrap",
        "(Ljava/lang/String;Ljava/lang/String;)Z");
    jstring jso = env->NewStringUTF(albSoPath.c_str());
    jstring jcd = env->NewStringUTF(cacheDir.c_str());
    jboolean ok = env->CallStaticBooleanMethod(cls_boot, mid_boot, jso, jcd);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        ok = JNI_FALSE;
    }

    env->DeleteLocalRef(jso);
    env->DeleteLocalRef(jcd);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(bufs);
    return ok == JNI_TRUE;
}

// ═══════════════════════════════════════════════════════════
//  embedded_initialize — main entry
// ═══════════════════════════════════════════════════════════
bool embedded_initialize(JNIEnv* env, JavaVM* /*vm*/) {
    LOGI("embedded_initialize: starting");

    // 1) Get code cache dir
    std::string cacheDir = getCacheDir(env);
    yambz_setCacheDir(cacheDir.c_str());
    LOGI("  cache dir: %s", cacheDir.c_str());
    ::mkdir(cacheDir.c_str(), 0700);

    // 2) Extract libalbatross_base.so
    std::string albSo = cacheDir + "/libalbatross_base.so";
    ::unlink(albSo.c_str());
    if (!writeFile(albSo, YAMBZ_ALB_BYTES, YAMBZ_ALB_LEN)) {
        LOGE("failed to extract libalbatross_base.so");
        return false;
    }
    LOGI("  extracted albatross.so (%zu bytes) → %s",
         YAMBZ_ALB_LEN, albSo.c_str());

    // 3) Load dex + call ModBootstrap
    if (!loadDexAndBootstrap(env, albSo, cacheDir)) {
        LOGE("loadDexAndBootstrap failed");
        return false;
    }

    LOGI("embedded_initialize: success");
    return true;
}

} // namespace yambz
