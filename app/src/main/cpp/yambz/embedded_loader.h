#pragma once
#include <jni.h>
#include <string>

namespace yambz {

/**
 * استخراج dex/albatross من الذاكرة وتهيئة كل شيء.
 *
 * الخطوات (بالترتيب الحرج):
 *   1. الحصول على Application context من نشاط جارٍ (يوفره ModBootstrap)
 *   2. كتابة libalbatross_base.so في getCodeCacheDir()
 *   3. كتابة classes.dex في getCodeCacheDir()
 *   4. استدعاء ModBootstrap.bootstrap(albatrossPath, dexPath)
 *      ← داخل Java: System.load(albatrossPath) ثم InMemoryDexClassLoader(dex)
 *      ثم Albatross.init(0)
 *
 * لماذا الترتيب هكذا؟
 *   - Albatross.init() يحتاج native methods من libalbatross_base.so
 *   - libalbatross_base.so يجب أن تُحمَّل عبر System.load (لا dlopen)
 *     لأن ART على Android 7+ يحتاج namespace الـ app
 *   - dex يجب أن تُحمَّل أولاً ليتوفر ModBootstrap
 */
class EmbeddedLoader {
public:
    /// يُستدعى من JNI_OnLoad بعد الحصول على JavaVM.
    static bool initialize(JNIEnv* env, JavaVM* vm);

    /// مسار code_cache النهائي.
    static const std::string& albatrossSoPath() { return s_albatrossSoPath; }
    static const std::string& dexPath()         { return s_dexPath; }
    static bool  isReady()                      { return s_ready; }

private:
    static bool writeFile(const std::string& path, const void* data, size_t len);
    static std::string getCodeCacheDir(JNIEnv* env);
    static bool loadDexIntoVm(JNIEnv* env, const std::string& dexPath,
                              jobject* outLoader);
    static bool invokeBootstrap(JNIEnv* env, jobject loader,
                                const std::string& albSoPath,
                                const std::string& dexPath);
    static jobject currentApplication(JNIEnv* env);

    static std::string s_albatrossSoPath;
    static std::string s_dexPath;
    static bool s_ready;
};

} // namespace yambz
