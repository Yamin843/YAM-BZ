#pragma once
#include <jni.h>
#include <string>

// ═══════════════════════════════════════════════════════════
//  Global VM access
// ═══════════════════════════════════════════════════════════
JavaVM* yambz_getVM();
void    yambz_setVM(JavaVM* vm);

// ═══════════════════════════════════════════════════════════
//  Storage
// ═══════════════════════════════════════════════════════════
std::string yambz_getCacheDir();
void        yambz_setCacheDir(const char* dir);

// ═══════════════════════════════════════════════════════════
//  Notify (toast)
// ═══════════════════════════════════════════════════════════
void yambz_notify(const char* msg);

// ═══════════════════════════════════════════════════════════
//  UI lifecycle (from jni_ui.cpp, called by Java ModBootstrap)
// ═══════════════════════════════════════════════════════════
void fab_setJavaVM(JavaVM* vm);
void panel_setJavaVM(JavaVM* vm);

// ═══════════════════════════════════════════════════════════
//  Ocean (from trace/OceanState.cpp — called by UI)
// ═══════════════════════════════════════════════════════════
