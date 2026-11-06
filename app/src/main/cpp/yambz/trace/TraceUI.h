#pragma once
#include <cstdint>
#include <string>

namespace yambz {

struct TraceUIOptions {
    bool showDialog = false;
    bool autoScroll = true;
    bool captureBacktrace = true;
    int  backtraceDepth = 5;
    int  objectDumpDepth = 5;
    int  selectedEvent = -1;

    // active session (0 = none)
    uint32_t activeSessionId = 0;
    std::string activeMethodDesc;
};

TraceUIOptions& traceUI();

} // namespace yambz
