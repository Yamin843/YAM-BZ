#pragma once
#include <jni.h>
#include <string>
#include <vector>

// ============ ALL YAM-BZ HELPERS: GLOBAL SCOPE ============

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

// CallTab helpers
void        yambz_doInvoke();
std::string yambz_getLastResultJson();

// Classes/Detail/Heap
void yambz_openDetail(unsigned int classIdx);
void yambz_openTraceForClass(unsigned int classIdx);
void yambz_openCallFor(unsigned int classIdx);
void yambz_openHeapPicker(const char* type);
void yambz_useInstanceFromPicker(int idx);

// Settings / FPS
bool yambz_showFps();
int  yambz_getBacktraceDepth();
int  yambz_getObjectDumpDepth();

// UI managers
void fab_setJavaVM(JavaVM* vm);
void panel_setJavaVM(JavaVM* vm);
