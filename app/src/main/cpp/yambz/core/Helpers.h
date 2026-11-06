#pragma once
#include <jni.h>
#include <string>
#include <vector>

// ============ ALL HELPERS: global scope, C++ linkage ============
// Naming: yambz_<component>_<action>. NO namespaces. NO extern "C".
// Every declaration MUST have a real definition in some .cpp.
// Missing definition = link error = must fix.

JavaVM* yambz_getGlobalVM();
void    yambz_setGlobalVM(JavaVM* vm);

std::string yambz_getCacheDir();
void        yambz_setCacheDir(const char* dir);

void yambz_notify(const char* msg);

void yambz_stopTrace();
void yambz_saveTracePreset(const char* name);
void yambz_loadTracePreset(const char* name);
void yambz_startDeepBacktrace(int ms, int max);

// CallEngine — InvokeRaw returns true if invocation was dispatched
bool        yambz_callEngineInvokeRaw(JNIEnv* env, jobject method,
                                       const std::string& methodDesc,
                                       const std::string& thisJson,
                                       const std::vector<std::string>& argsJson);
bool        yambz_callEngineIsRunning();
std::string yambz_callEngineLastResultJson();
void        yambz_callEngineClear();

void        yambz_doInvoke();
std::string yambz_getLastResultJson();

void yambz_openDetail(unsigned int classIdx);
void yambz_openTraceForClass(unsigned int classIdx);
void yambz_openCallFor(unsigned int classIdx);
void yambz_openHeapPicker(const char* type);
void yambz_useInstanceFromPicker(int idx);
void yambz_startEnumFromUI();

bool yambz_showFps();
int  yambz_getBacktraceDepth();
int  yambz_getObjectDumpDepth();

void fab_setJavaVM(JavaVM* vm);
void panel_setJavaVM(JavaVM* vm);
