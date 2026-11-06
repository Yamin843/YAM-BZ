#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace yambz {

/// إعدادات trace الجارية
struct OceanTraceConfig {
    bool eventCallEnter = true;
    bool eventCall       = true;
    bool eventReturn     = true;
    bool eventExec       = false;   // مكلف
    bool eventBlock      = false;
    bool eventFileRead   = true;
    bool eventFieldWrite = true;

    int  scope = 4;         // kApplication
    int  timeoutMs = 60000;
    int  maxEvents = 200000;

    // Filtering
    std::string packageFilter;      // prefix
    bool        onlyMainThread = false;

    // Sinkers
    int         sinkerKind = 0;     // 0=DefaultSinker, 1=KeyWord, 2=String, 3=Empty
    std::string sinkerKeyword;
};

/// حالة Ocean Trace UI (singleton)
struct OceanTraceUIState {
    bool show = false;
    bool tracing = false;
    bool initialized = false;

    // Active session
    int  sessionId = 0;

    // Config
    OceanTraceConfig config;

    // Statistics (من Java bridge)
    long eventCount = 0;
    long callCount = 0;
    long returnCount = 0;
    long execCount = 0;
    long blockCount = 0;
    long fieldReadCount = 0;
    long fieldWriteCount = 0;

    // Events (latest N for display)
    struct Event {
        uint64_t ts;
        int      kind;      // 1=CALL, 2=RETURN, 3=EXEC, 4=BLOCK, 5=READ, 6=WRITE
        int      depth;
        std::string method;
        std::string extra;   // args/return/field
    };
    std::vector<Event> events;
    size_t             eventsCapacity = 5000;

    // UI state
    int  selectedEvent = -1;
    bool autoScroll    = true;
    char searchFilter[256] = {0};
};

OceanTraceUIState& oceanTraceUI();

/// يُستدعى من ClassesTab لفتح panel مع target method
void oceanTraceOpenForMethod(void* jmethod);

/// يبدأ trace عبر Ocean (يستدعي Java bridge)
void oceanTraceStart();

/// يوقف trace
void oceanTraceStop();

/// يُستدعى من jni_ocean.cpp عند حدث جديد
void oceanTracePushEvent(int kind, int depth,
                          const std::string& method,
                          const std::string& extra);

/// يرسم الـ panel (يُستدعى من Renderer)
void drawOceanTracePanel();

} // namespace yambz
