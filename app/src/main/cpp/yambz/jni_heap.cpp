#include <jni.h>
#include "call/HeapPicker.h"
#include "core/Logger.h"

using namespace yambz;

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_call_HeapScannerCallback_nativeOnBatch(
    JNIEnv* env, jclass, jobjectArray entries)
{
    // حوّل BridgeEntry[] إلى Object[] ونادِ pushBatch
    // لكن pushBatch يتوقع Object[] فيه getInt0/getLong0/... مباشرة
    jsize n = env->GetArrayLength(entries);
    jclass cls_Obj = env->FindClass("java/lang/Object");
    jobjectArray wrapper = env->NewObjectArray(n, cls_Obj, nullptr);
    for (jsize i = 0; i < n; i++) {
        jobject be = env->GetObjectArrayElement(entries, i);
        env->SetObjectArrayElement(wrapper, i, be);
        env->DeleteLocalRef(be);
    }
    HeapPicker::instance().pushBatch(env, wrapper);
    env->DeleteLocalRef(wrapper);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_call_HeapScannerCallback_nativeOnComplete(JNIEnv*, jclass, jint total) {
    HeapPicker::instance().onComplete(total);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_call_HeapScannerCallback_nativeOnError(JNIEnv* env, jclass, jobject t) {
    HeapPicker::instance().onError(env, t);
}
