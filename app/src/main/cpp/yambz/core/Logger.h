#pragma once
#include <android/log.h>

#define YAMBZ_TAG "YAMBZ"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, YAMBZ_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,   YAMBZ_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,    YAMBZ_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,    YAMBZ_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,   YAMBZ_TAG, __VA_ARGS__)
