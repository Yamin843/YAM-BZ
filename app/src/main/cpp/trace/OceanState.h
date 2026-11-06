#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace yambz {

enum class OceanEventKind : int {
    NONE        = 0,
    CALL_ENTER  = 1,
    CALL        = 2,
    RETURN      = 3,
    EXEC        = 4,
    BLOCK       = 5,
    FILE_READ   = 6,
    FIELD_WRITE = 7,
};

struct OceanEvent {
    int64_t ts = 0;
    int     kind = 0;
    int     depth = 0;
    std::string methodDesc;
    std::string className;
    std::string methodName;
    std::string extra;
};

struct OceanConfig {
    bool evCallEnter  = true;
    bool evCall       = true;
    bool evReturn     = true;
    bool evExec       = false;
    bool evBlock      = false;
    bool evFileRead   = false;
    bool evFieldWrite = false;

    int mode = 0;

    bool scJdk = false;
    bool scPlatform = false;
    bool scApplication = true;
    bool scPackageFilter = false;
    bool scEntryOnly = false;
    char packagePrefix[256] = {0};

    int  sinkerKind = 0;
    char sinkerKeyword[256] = {0};

    int timeoutMs = 60000;
    int maxEvents = 200000;
    bool onlyMainThread = false;

    char memberSpec[512] = {0};
};

struct OceanUIState {
    bool      initialized = false;
    bool      tracing = false;
    int       sessionId = 0;
    int       mode = 0;
    OceanConfig config;

    mutable std::mutex eventsMu;
    std::vector<OceanEvent> events;
    size_t eventsCapacity = 10000;

    long countByKind[8] = {0};
    long totalEvents = 0;
    int64_t startTs = 0;

    int  selectedEvent = -1;
    char searchBuf[256] = {0};
    int  filterKind = 0;
    bool autoScroll = true;
    int  currentTab = 0;

    int stopReason = 0;
    long traceCount = 0;
};

OceanUIState& oceanUI();

bool oceanStart();
void oceanStop();
void oceanSync();

void oceanPushEvent(OceanEvent ev);
void oceanClearEvents();

void drawOceanTab();

int  oceanKindMask();

} // namespace yambz
