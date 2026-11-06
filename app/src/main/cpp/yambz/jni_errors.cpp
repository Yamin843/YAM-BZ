#include <jni.h>
#include "call/TracePresets.h"
#include "core/ErrorContext.h"
#include "core/Logger.h"
#include "core/Helpers.h"

using namespace yambz;

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_util_ErrorBridge_nativeReport(
    JNIEnv* env, jclass, jint domain, jstring op, jstring target,
    jstring msg, jstring stack)
{
    auto toStr = [&](jstring js) -> std::string {
        if (!js) return "";
        const char* c = env->GetStringUTFChars(js, nullptr);
        std::string r = c ? c : "";
        env->ReleaseStringUTFChars(js, c);
        return r;
    };
    ErrDomain dom = (ErrDomain) (domain >= 0 && domain <= 7 ? domain : 7);
    std::string sop = toStr(op);
    std::string stgt = toStr(target);
    std::string smsg = toStr(msg);
    std::string sstk = toStr(stack);
    ErrorContext::instance().report(ErrSeverity::Error, dom, sop, stgt, smsg, sstk);
}

// ─── Query APIs للـ UI ───
extern "C" JNIEXPORT jstring JNICALL
Java_com_yambz_bridge_PanelBridge_nativeGetRecentErrors(JNIEnv* env, jclass, jint maxCount) {
    auto records = ErrorContext::instance().recent((size_t) maxCount);
    std::string out = "[";
    for (size_t i = 0; i < records.size(); i++) {
        if (i) out += ',';
        const auto& r = records[i];
        out += "{\"id\":" + std::to_string(r.id);
        out += ",\"ts\":" + std::to_string(r.timestampMs);
        out += ",\"sev\":" + std::to_string((int)r.severity);
        out += ",\"dom\":" + std::to_string((int)r.domain);
        auto esc = [](const std::string& s) {
            std::string o;
            for (char c : s) {
                if (c == '"' || c == '\\') { o += '\\'; o += c; }
                else if (c == '\n') o += "\\n";
                else if (c == '\r') o += "\\r";
                else o += c;
            }
            return o;
        };
        out += ",\"op\":\"" + esc(r.operation) + "\"";
        out += ",\"tgt\":\"" + esc(r.target) + "\"";
        out += ",\"msg\":\"" + esc(r.message) + "\"";
        out += ",\"stack\":\"" + esc(r.stackTrace) + "\"}";
    }
    out += "]";
    return env->NewStringUTF(out.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_yambz_bridge_PanelBridge_nativeErrorCount(JNIEnv*, jclass) {
    return (jint) ErrorContext::instance().totalCount();
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_PanelBridge_nativeClearErrors(JNIEnv*, jclass) {
    ErrorContext::instance().clear();
}

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModBootstrap_nativeSetCacheDir(JNIEnv* env, jclass, jstring dir) {
    if (!dir) return;
    const char* c = env->GetStringUTFChars(dir, nullptr);
    if (c) {

        yambz_setCacheDir(c);
        env->ReleaseStringUTFChars(dir, c);
    }
}

#include "core/Favorites.h"
#include <string>

extern "C" JNIEXPORT void JNICALL
Java_com_yambz_bridge_ModBootstrap_nativeInitFavorites(JNIEnv* env, jclass, jstring dir) {
    if (!dir) return;
    const char* c = env->GetStringUTFChars(dir, nullptr);
    if (c) {
        Favorites::instance().init(c);
        TracePresets::instance().init(c);
        env->ReleaseStringUTFChars(dir, c);
    }
}
