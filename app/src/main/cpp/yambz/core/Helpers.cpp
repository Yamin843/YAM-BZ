#include "core/Helpers.h"
#include <string>

// Weak fallbacks — real definitions in other files override these.
// If a helper is missing at link, stub prevents error (but behavior degrades).

__attribute__((weak)) void yambz_notify(const char*) {}
__attribute__((weak)) std::string yambz_getCacheDir() { return "/data/local/tmp"; }
__attribute__((weak)) void yambz_setCacheDir(const char*) {}
__attribute__((weak)) JavaVM* yambz_getGlobalVM() { return nullptr; }
__attribute__((weak)) void yambz_setGlobalVM(JavaVM*) {}
__attribute__((weak)) void yambz_callEngineInvokeRaw(JNIEnv*, jobject,
    const std::string&, const std::string&, const std::vector<std::string>&) {}
__attribute__((weak)) bool yambz_callEngineIsRunning() { return false; }
__attribute__((weak)) std::string yambz_callEngineLastResultJson() { return "{}"; }
__attribute__((weak)) void yambz_callEngineClear() {}
__attribute__((weak)) void yambz_stopTrace() {}
__attribute__((weak)) void yambz_saveTracePreset(const char*) {}
__attribute__((weak)) void yambz_loadTracePreset(const char*) {}
__attribute__((weak)) void yambz_startDeepBacktrace(int, int) {}
__attribute__((weak)) bool yambz_showFps() { return false; }
__attribute__((weak)) int yambz_getBacktraceDepth() { return 5; }
__attribute__((weak)) int yambz_getObjectDumpDepth() { return 5; }
__attribute__((weak)) void yambz_openHeapPicker(const char*) {}
__attribute__((weak)) void yambz_useInstanceFromPicker(int) {}
__attribute__((weak)) void fab_setJavaVM(JavaVM*) {}
__attribute__((weak)) void panel_setJavaVM(JavaVM*) {}
