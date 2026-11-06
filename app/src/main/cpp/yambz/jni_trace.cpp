#include <jni.h>
#include <vector>
#include <android/log.h>
#include <sstream>
#include <chrono>

#include "trace/TraceStore.h"
#include "trace/TraceEngine.h"
#include "trace/TraceEvent.h"
#include "core/Logger.h"

using namespace yambz;

// ═══════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════
static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// استدعاء ObjectDumper.dump(obj, Options) عبر JNI
static std::string dumpObject(JNIEnv* env, jobject obj, int maxDepth) {
    if (!obj) return "null";
    jclass cls = env->FindClass("com/yambz/bridge/ObjectDumper");
    if (!cls) { env->ExceptionClear(); return "<no-dumper>"; }
    jmethodID mid = env->GetStaticMethodID(cls, "dump",
        "(Ljava/lang/Object;)Ljava/lang/String;");
    if (!mid) { env->ExceptionClear(); return "<no-method>"; }
    jstring js = (jstring) env->CallStaticObjectMethod(cls, mid, obj);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe(); env->ExceptionClear();
        return "<dump-ex>";
    }
    if (!js) return "null";
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string out = c ? c : "";
    env->ReleaseStringUTFChars(js, c);
    env->DeleteLocalRef(js);
    return out;
}

// backtrace من Thread.getStackTrace
static std::vector<StackFrame> captureBacktrace(JNIEnv* env, int maxDepth) {
    std::vector<StackFrame> out;
    jclass cls = env->FindClass("java/lang/Thread");
    if (!cls) return out;
    jmethodID mid = env->GetStaticMethodID(cls, "currentThread", "()Ljava/lang/Thread;");
    jobject th = env->CallStaticObjectMethod(cls, mid);
    jmethodID mid2 = env->GetMethodID(cls, "getStackTrace",
        "()[Ljava/lang/StackTraceElement;");
    jobjectArray frames = (jobjectArray) env->CallObjectMethod(th, mid2);
    if (!frames) return out;
    jsize len = env->GetArrayLength(frames);
    jclass cls_frame = env->FindClass("java/lang/StackTraceElement");
    jmethodID m_cn  = env->GetMethodID(cls_frame, "getClassName", "()Ljava/lang/String;");
    jmethodID m_mn  = env->GetMethodID(cls_frame, "getMethodName", "()Ljava/lang/String;");
    jmethodID m_fn  = env->GetMethodID(cls_frame, "getFileName", "()Ljava/lang/String;");
    jmethodID m_ln  = env->GetMethodID(cls_frame, "getLineNumber", "()I");
    jmethodID m_nat = env->GetMethodID(cls_frame, "isNativeMethod", "()Z");

    int captured = 0;
    for (jsize i = 0; i < len && captured < maxDepth; i++) {
        auto frame = env->GetObjectArrayElement(frames, i);
        // skip internal
        auto jcn = (jstring) env->CallObjectMethod(frame, m_cn);
        const char* cn = env->GetStringUTFChars(jcn, nullptr);
        std::string scn = cn ? cn : "";
        env->ReleaseStringUTFChars(jcn, cn);
        env->DeleteLocalRef(jcn);
        if (scn.find("com.yambz.bridge.TraceCapture") != std::string::npos ||
            scn.find("qing.albatross") != std::string::npos) {
            env->DeleteLocalRef(frame);
            continue;
        }

        StackFrame sf;
        sf.className = scn;
        auto jmn = (jstring) env->CallObjectMethod(frame, m_mn);
        const char* mn = env->GetStringUTFChars(jmn, nullptr);
        sf.methodName = mn ? mn : "";
        env->ReleaseStringUTFChars(jmn, mn);
        env->DeleteLocalRef(jmn);
        auto jfn = (jstring) env->CallObjectMethod(frame, m_fn);
        const char* fn = jfn ? env->GetStringUTFChars(jfn, nullptr) : nullptr;
        sf.fileName = fn ? fn : "";
        if (fn) env->ReleaseStringUTFChars(jfn, fn);
        if (jfn) env->DeleteLocalRef(jfn);
        sf.lineNumber = env->CallIntMethod(frame, m_ln);
        sf.isNative = env->CallBooleanMethod(frame, m_nat) != 0;
        out.push_back(std::move(sf));
        env->DeleteLocalRef(frame);
        captured++;
    }
    return out;
}

// تحويل CallFrame إلى ParamValue
static ParamValue extractParam(JNIEnv* env, jobject frame, int i, jobject rawArg, int maxDepth) {
    ParamValue pv;
    if (!rawArg) { pv.kind = ParamValue::Kind::Null; return pv; }
    jclass cls = env->GetObjectClass(rawArg);
    // primitives boxing (Integer, Long, ...)
    jclass cls_num = env->FindClass("java/lang/Number");
    if (env->IsInstanceOf(rawArg, cls_num)) {
        jmethodID m_dbl = env->GetMethodID(cls, "doubleValue", "()D");
        jmethodID m_lng = env->GetMethodID(cls, "longValue", "()J");
        double d = env->CallDoubleMethod(rawArg, m_dbl);
        long l = env->CallLongMethod(rawArg, m_lng);
        if (d == (double)l) {
            pv.kind = ParamValue::Kind::Long;
            pv.i64 = l;
        } else {
            pv.kind = ParamValue::Kind::Double;
            pv.f64 = d;
        }
        return pv;
    }
    jclass cls_bool = env->FindClass("java/lang/Boolean");
    if (env->IsInstanceOf(rawArg, cls_bool)) {
        jmethodID m = env->GetMethodID(cls, "booleanValue", "()Z");
        pv.kind = ParamValue::Kind::Bool;
        pv.b = env->CallBooleanMethod(rawArg, m) != 0;
        return pv;
    }
    jclass cls_str = env->FindClass("java/lang/String");
    if (env->IsInstanceOf(rawArg, cls_str)) {
        pv.kind = ParamValue::Kind::String;
        pv.str = dumpObject(env, rawArg, maxDepth);
        return pv;
    }
    // object
    pv.kind = ParamValue::Kind::Object;
    pv.str = dumpObject(env, rawArg, maxDepth);
    return pv;
}

// ═══════════════════════════════════════════════════════════
//  Called from TraceEngine::onEventCaptured
// ═══════════════════════════════════════════════════════════
extern "C" void jni_trace_onEvent(uint32_t sessionId, JNIEnv* env,
                                   jobject frame, bool isBefore) {
    if (!env || !frame) return;

    // نأخذ معلومات من CallFrame
    jclass cls_cf = env->GetObjectClass(frame);

    if (isBefore) {
        TraceEvent ev;
        ev.eventId = (uint32_t) TraceStore::instance().totalPushed();
        ev.timestampMs = (uint64_t) nowMs();
        ev.hasThis = true;

        // getMethod().toString()
        jmethodID m_getMethod = env->GetMethodID(cls_cf, "getMethod",
            "()Ljava/lang/reflect/Method;");
        if (m_getMethod) {
            jobject m = env->CallObjectMethod(frame, m_getMethod);
            if (m) {
                jclass cls_m = env->GetObjectClass(m);
                jmethodID m_toStr = env->GetMethodID(cls_m, "toString",
                    "()Ljava/lang/String;");
                jstring js = (jstring) env->CallObjectMethod(m, m_toStr);
                if (js) {
                    const char* c = env->GetStringUTFChars(js, nullptr);
                    ev.methodDesc = c ? c : "?";
                    env->ReleaseStringUTFChars(js, c);
                    env->DeleteLocalRef(js);
                }
                env->DeleteLocalRef(m);
            }
        } else { env->ExceptionClear(); }

        // getArguments()
        jmethodID m_getArgs = env->GetMethodID(cls_cf, "getArguments",
            "()[Ljava/lang/Object;");
        if (m_getArgs) {
            jobjectArray arr = (jobjectArray) env->CallObjectMethod(frame, m_getArgs);
            if (arr) {
                jsize n = env->GetArrayLength(arr);
                for (jsize i = 0; i < n; i++) {
                    auto a = env->GetObjectArrayElement(arr, i);
                    ev.args.push_back(extractParam(env, frame, (int)i, a, 5));
                    if (a) env->DeleteLocalRef(a);
                }
                env->DeleteLocalRef(arr);
            }
        } else { env->ExceptionClear(); }

        // getThis()
        jmethodID m_getThis = env->GetMethodID(cls_cf, "getThis",
            "()Ljava/lang/Object;");
        if (m_getThis) {
            jobject self = env->CallObjectMethod(frame, m_getThis);
            if (self) {
                ev.thisDump = dumpObject(env, self, 5);
                env->DeleteLocalRef(self);
            }
        } else { env->ExceptionClear(); }

        // backtrace
        ev.backtrace = captureBacktrace(env, 5);

        TraceStore::instance().push(std::move(ev));
    } else {
        // after: ملء حقل completed في آخر حدث بنفس sessionId — للتبسيط نضيف حدث جديد
        TraceEvent ev;
        ev.eventId = (uint32_t) TraceStore::instance().totalPushed();
        ev.timestampMs = (uint64_t) nowMs();
        ev.completed = true;

        // getMethod()
        jmethodID m_getMethod = env->GetMethodID(cls_cf, "getMethod",
            "()Ljava/lang/reflect/Method;");
        if (m_getMethod) {
            jobject m = env->CallObjectMethod(frame, m_getMethod);
            if (m) {
                jclass cls_m = env->GetObjectClass(m);
                jmethodID m_toStr = env->GetMethodID(cls_m, "toString",
                    "()Ljava/lang/String;");
                jstring js = (jstring) env->CallObjectMethod(m, m_toStr);
                if (js) {
                    const char* c = env->GetStringUTFChars(js, nullptr);
                    ev.methodDesc = c ? c : "?";
                    env->ReleaseStringUTFChars(js, c);
                    env->DeleteLocalRef(js);
                }
                env->DeleteLocalRef(m);
            }
        } else { env->ExceptionClear(); }

        // getResult()
        jmethodID m_getResult = env->GetMethodID(cls_cf, "getResult",
            "()Ljava/lang/Object;");
        if (m_getResult) {
            jobject r = env->CallObjectMethod(frame, m_getResult);
            if (r) {
                ev.result = extractParam(env, frame, 0, r, 5);
                env->DeleteLocalRef(r);
            }
        } else { env->ExceptionClear(); }

        // getThrowable()
        jmethodID m_getThr = env->GetMethodID(cls_cf, "getThrowable",
            "()Ljava/lang/Throwable;");
        if (m_getThr) {
            jobject t = env->CallObjectMethod(frame, m_getThr);
            if (t) {
                ev.threw = true;
                ev.throwableDump = dumpObject(env, t, 3);
                env->DeleteLocalRef(t);
            }
        } else { env->ExceptionClear(); }

        TraceStore::instance().push(std::move(ev));
    }
}

// ═══════════════════════════════════════════════════════════
//  JNI from TraceCapture.java
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_TraceCapture_nativeOnBefore(JNIEnv* env, jclass,
                                                   jint sessionId, jobject frame) {
    jni_trace_onEvent((uint32_t) sessionId, env, frame, true);
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_TraceCapture_nativeOnAfter(JNIEnv* env, jclass,
                                                  jint sessionId, jobject frame) {
    jni_trace_onEvent((uint32_t) sessionId, env, frame, false);
}

// ═══════════════════════════════════════════════════════════
//  Wiring: استدعاء من ClassesTab
// ═══════════════════════════════════════════════════════════
#include "ui/DetailView.h"
#include "ui/ClassesTab.h"

void yambz_openDetail(uint32_t classIdx) {
    auto& dv = yambz::detailViewState();
    dv.classIdx = classIdx;
    dv.show = true;
}

void yambz_openTraceForClass(uint32_t classIdx) {
    auto classes = yambz::ClassEnumerator::instance().snapshot();
    if (!classes || classIdx >= classes->size()) return;
    const auto& c = (*classes)[classIdx];

    // TODO: عرض حوار لاختيار method. مؤقتاً نبدأ trace على أول method.
    const auto& members = yambz::ClassEnumerator::instance().membersOf(classIdx);
    for (const auto& m : members) {
        if (m.kind == yambz::MemberKind::Method) {
            // عبر ClassBridge (Java)
            JavaVM* vm = nullptr;
            // (يُملأ من init)
            extern JavaVM* yambz_getGlobalVM();
            vm = yambz_getGlobalVM();
            if (!vm) return;
            JNIEnv* env = nullptr;
            if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
                vm->AttachCurrentThread(&env, nullptr);

            // ابحث عن Method object
            jclass cls_Class = env->FindClass("java/lang/Class");
            jmethodID mid_forName = env->GetStaticMethodID(cls_Class, "forName",
                "(Ljava/lang/String;)Ljava/lang/Class;");
            jstring jn = env->NewStringUTF(c.fullName.c_str());
            jobject jcls = env->CallStaticObjectMethod(cls_Class, mid_forName, jn);
            env->DeleteLocalRef(jn);

            if (jcls) {
                jmethodID mid_getMethods = env->GetMethodID(cls_Class, "getDeclaredMethods",
                    "()[Ljava/lang/reflect/Method;");
                jobjectArray jmethods = (jobjectArray) env->CallObjectMethod(jcls, mid_getMethods);
                if (jmethods) {
                    jsize n = env->GetArrayLength(jmethods);
                    for (jsize i = 0; i < n; i++) {
                        auto jm = env->GetObjectArrayElement(jmethods, i);
                        jclass cls_M = env->GetObjectClass(jm);
                        jmethodID mid_name = env->GetMethodID(cls_M, "getName",
                            "()Ljava/lang/String;");
                        jstring jname = (jstring) env->CallObjectMethod(jm, mid_name);
                        const char* name = env->GetStringUTFChars(jname, nullptr);
                        if (name && m.name == name) {
                            // بدء trace
                            yambz::TraceSession cfg;
                            cfg.methodDesc = c.fullName + "." + m.name;
                            cfg.captureBacktrace = true;
                            cfg.backtraceDepth = 5;
                            uint32_t id = yambz::TraceEngine::instance().startTrace(env, jm, cfg);
                            if (id > 0) {
                                auto& ui = yambz::traceUI();
                                ui.activeSessionId = id;
                                ui.activeMethodDesc = cfg.methodDesc;
                                ui.showDialog = true;
                            }
                            env->ReleaseStringUTFChars(jname, name);
                            env->DeleteLocalRef(jname);
                            env->DeleteLocalRef(jm);
                            env->DeleteLocalRef(cls_M);
                            break;
                        }
                        if (name) env->ReleaseStringUTFChars(jname, name);
                        env->DeleteLocalRef(jname);
                        env->DeleteLocalRef(jm);
                        env->DeleteLocalRef(cls_M);
                    }
                    env->DeleteLocalRef(jmethods);
                }
                env->DeleteLocalRef(jcls);
            }
            break;
        }
    }
}

void yambz_stopTrace() {
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);
    yambz::TraceEngine::instance().stopAll(env);
    auto& ui = yambz::traceUI();
    ui.activeSessionId = 0;
    ui.showDialog = false;
}

// ═══════════════════════════════════════════════════════════
//  Constructor trace (via InstructionListener)
// ═══════════════════════════════════════════════════════════
extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ConstructorTraceCapture_nativeOnEnter(
    JNIEnv* env, jclass, jint sessionId, jobject method, jint dexPc, jobject ctx)
{
    TraceEvent ev;
    ev.timestampMs = (uint64_t) nowMs();
    ev.methodDesc = "ctor";
    ev.hasThis = false; // لم يُنشأ بعد

    // اسم الـ constructor من method.toString()
    if (method) {
        jclass cls_m = env->GetObjectClass(method);
        jmethodID m_toStr = env->GetMethodID(cls_m, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(method, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                ev.methodDesc = c ? c : "ctor";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls_m);
    }

    // args من InvocationContext
    jclass cls_ctx = env->GetObjectClass(ctx);
    jmethodID m_getArgs = env->GetMethodID(cls_ctx, "getArguments", "()[Ljava/lang/Object;");
    if (m_getArgs) {
        jobjectArray arr = (jobjectArray) env->CallObjectMethod(ctx, m_getArgs);
        if (arr) {
            jsize n = env->GetArrayLength(arr);
            for (jsize i = 0; i < n; i++) {
                jobject a = env->GetObjectArrayElement(arr, i);
                ev.args.push_back(extractParam(env, ctx, (int)i, a, 5));
                if (a) env->DeleteLocalRef(a);
            }
            env->DeleteLocalRef(arr);
        }
    }

    ev.backtrace = captureBacktrace(env, 5);
    TraceStore::instance().push(std::move(ev));
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ConstructorTraceCapture_nativeOnReturn(
    JNIEnv* env, jclass, jint sessionId, jobject method, jobject ret, jint dexPc, jobject ctx)
{
    TraceEvent ev;
    ev.timestampMs = (uint64_t) nowMs();
    ev.methodDesc = "ctor";
    ev.completed = true;

    if (method) {
        jclass cls_m = env->GetObjectClass(method);
        jmethodID m_toStr = env->GetMethodID(cls_m, "toString", "()Ljava/lang/String;");
        if (m_toStr) {
            jstring js = (jstring) env->CallObjectMethod(method, m_toStr);
            if (js) {
                const char* c = env->GetStringUTFChars(js, nullptr);
                ev.methodDesc = c ? c : "ctor";
                env->ReleaseStringUTFChars(js, c);
                env->DeleteLocalRef(js);
            }
        }
        env->DeleteLocalRef(cls_m);
    }

    // النتيجة = الكائن المُنشأ
    if (ret) {
        ev.result.kind = ParamValue::Kind::Object;
        ev.result.str = dumpObject(env, ret, 5);
    } else {
        ev.result.kind = ParamValue::Kind::Null;
    }

    TraceStore::instance().push(std::move(ev));
}

#include "call/TracePresets.h"
#include "core/ClassEnumerator.h"
#include "core/ClassTypes.h"
#include "trace/TraceUI.h"

void yambz_saveTracePreset(const char* name) {
    auto& ui = yambz::traceUI();
    yambz::TracePreset p;
    p.name = name;
    p.methodDesc = ui.activeMethodDesc;
    p.captureBacktrace = true;
    p.backtraceDepth = 5;
    p.objectDumpDepth = 5;
    p.captureArgs = true;
    p.captureThis = true;
    p.createdAt = (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    yambz::TracePresets::instance().save(p);
}

void yambz_loadTracePreset(const char* name) {
    yambz::TracePreset p;
    if (yambz::TracePresets::instance().load(name, p)) {
        extern void yambz_notify(const char*);
        yambz_notify(("Loaded preset: " + p.name).c_str());
    }
}

void yambz_startDeepBacktrace(int ms, int max) {
    extern JavaVM* yambz_getGlobalVM();
    JavaVM* vm = yambz_getGlobalVM();
    if (!vm) return;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK)
        vm->AttachCurrentThread(&env, nullptr);

    jclass cls = env->FindClass("com/yambz/bridge/DeepBacktrace");
    jmethodID mid = env->GetStaticMethodID(cls, "start", "(II)Z");
    if (mid) env->CallStaticBooleanMethod(cls, mid, ms, max);
}
