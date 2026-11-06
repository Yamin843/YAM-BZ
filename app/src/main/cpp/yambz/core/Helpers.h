#pragma once
#include <jni.h>
#include <string>
#include <vector>

// ============ HELPER DECLARATIONS (global scope) ============
// Real definitions live in their respective .cpp files.
// NO weak fallbacks. If a symbol is missing = link error = fix it.

// VM
JavaVM* yambz_getGlobalVM();
void    yambz_setGlobalVM(JavaVM* vm);

// Cache / IO
std::string yambz_getCacheDir();
void        yambz_setCacheDir(const char* dir);

// Notification
void yambz_notify(const char* msg);

// Trace
void yambz_stopTrace();
void yambz_saveTracePreset(const char* name);
void yambz_loadTracePreset(const char* name);
void yambz_startDeepBacktrace(int ms, int max);

// Call engine
void yambz_callEngineInvokeRaw(JNIEnv* env, jobject method,
                                const std::string& methodDesc,
                                const std::string& thisJson,
                                const std::vector<std::string>& argsJson);
bool        yambz_callEngineIsRunning();
std::string yambz_callEngineLastResultJson();
void        yambz_callEngineClear();

// CallTab helpers (defined in jni_call.cpp)
void        yambz_doInvoke();
std::string yambz_getLastResultJson();

// Classes / Detail / Heap
void yambz_openDetail(unsigned int classIdx);
void yambz_openTraceForClass(unsigned int classIdx);
void yambz_openCallFor(unsigned int classIdx);
void yambz_openHeapPicker(const char* type);
void yambz_useInstanceFromPicker(int idx);
void yambz_startEnumFromUI();

// Settings / FPS (defined in SettingsTab.cpp, must be global)
bool yambz_showFps();
int  yambz_getBacktraceDepth();
int  yambz_getObjectDumpDepth();

// UI managers (definitions in FabManager/PanelManager.cpp)
void fab_setJavaVM(JavaVM* vm);
void panel_setJavaVM(JavaVM* vm);
