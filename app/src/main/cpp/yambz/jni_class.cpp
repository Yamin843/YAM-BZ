#include <jni.h>
#include <string>
#include "core/ClassEnumerator.h"
#include "core/SearchEngine.h"
#include "ui/ClassesState.h"
#include "core/Logger.h"
#include "core/Helpers.h"

using namespace yambz;

extern "C" {

JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeClassStartEnum(JNIEnv* env, jobject) {
    ClassEnumerator::instance().start(env);
}

JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeClassCancel(JNIEnv*, jobject) {
    ClassEnumerator::instance().cancel();
}

JNIEXPORT jboolean JNICALL
Java_com_yambz_bridge_PanelBridge_nativeClassIsRunning(JNIEnv*, jobject) {
    return ClassEnumerator::instance().isRunning() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_yambz_bridge_PanelBridge_nativeClassCount(JNIEnv*, jobject) {
    return (jint) ClassEnumerator::instance().count();
}

} // extern "C"
