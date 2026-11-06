#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace yambz {

struct TracePreset {
    std::string name;
    std::string methodDesc;
    bool        captureBacktrace;
    int         backtraceDepth;
    int         objectDumpDepth;
    bool        captureArgs;
    bool        captureThis;
    uint64_t    createdAt;
};

class TracePresets {
public:
    static TracePresets& instance();

    void init(const std::string& dir);
    bool save(const TracePreset& p);
    std::vector<TracePreset> listAll();
    std::vector<TracePreset> listForMethod(const std::string& methodDesc);
    bool remove(const std::string& name);
    bool load(const std::string& name, TracePreset& out);

private:
    TracePresets() = default;
    std::string sanitize(const std::string& s);

    mutable std::mutex m_mu;
    std::string m_dir;
    bool m_inited = false;
};

} // namespace yambz
