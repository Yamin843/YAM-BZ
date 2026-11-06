#pragma once
#include <jni.h>
#include <string>
#include <vector>

// All YAM-BZ helpers: GLOBAL scope, C++ linkage (NO extern "C", NO namespace)

void yambz_notify(const char* msg);
std::string yambz_getCacheDir();
void yambz_setCacheDir(const char* dir);
JavaVM* yambz_getGlobalVM();
void yambz_setGlobalVM(JavaVM* vm);

void yambz_callEngineInvokeRaw(JNIEnv* env, jobject method,
                                const std::string& methodDesc,
                                const std::string& thisJson,
                                const std::vector<std::string>& argsJson);
bool yambz_callEngineIsRunning();
std::string yambz_callEngineLastResultJson();
void yambz_callEngineClear();

void yambz_stopTrace();
void yambz_saveTracePreset(const char* name);
void yambz_loadTracePreset(const char* name);
void yambz_startDeepBacktrace(int ms, int max);

bool yambz_showFps();
int  yambz_getBacktraceDepth();
int  yambz_getObjectDumpDepth();

void yambz_openHeapPicker(const char* type);
void yambz_useInstanceFromPicker(int idx);

void fab_setJavaVM(JavaVM* vm);
void panel_setJavaVM(JavaVM* vm);
