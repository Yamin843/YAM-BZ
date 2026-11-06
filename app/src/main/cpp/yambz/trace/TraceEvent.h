#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace yambz {

/// قيمة معامل واحدة (primitive أو object dump)
struct ParamValue {
    enum class Kind : uint8_t { Null, Int, Long, Float, Double, Bool, String, Object };
    Kind    kind = Kind::Null;
    int64_t i64 = 0;
    double  f64 = 0.0;
    bool    b   = false;
    std::string str;        // string أو object-dump
};

/// إطار backtrace
struct StackFrame {
    std::string className;
    std::string methodName;
    std::string fileName;
    int         lineNumber = -1;
    bool        isNative   = false;
};

/// حدث trace واحد
struct TraceEvent {
    uint32_t    eventId = 0;
    uint64_t    timestampMs = 0;
    std::string methodDesc;        // "com.example.Foo.bar(Ljava/lang/String;)V"

    // snapshot لحظة الاستدعاء
    bool        hasThis = false;
    std::string thisDump;          // object dump
    std::vector<ParamValue> args;
    std::vector<StackFrame> backtrace;

    // نتيجة (تُملأ عند afterCall)
    bool        completed = false;
    bool        threw     = false;
    std::string throwableDump;
    ParamValue  result;

    // hooks management
    bool        isError   = false;
    std::string errorMessage;
};

/// حالة trace نشط (لمجموعة من الأحداث)
struct TraceSession {
    uint32_t    sessionId = 0;
    std::string methodDesc;
    uint64_t    startedAtMs = 0;
    uint32_t    eventCount = 0;
    bool        enabled = true;
    bool        captureBacktrace = true;
    int         backtraceDepth = 5;
    int         objectDumpDepth = 5;   // ← عمق object tree (قابل للزيادة)
    bool        captureArgs = true;
    bool        captureThis = true;
    bool        captureResult = true;
};

} // namespace yambz
