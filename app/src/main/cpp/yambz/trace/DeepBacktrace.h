#pragma once
#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>
#include <atomic>

namespace yambz {

struct DeepFrame {
    uint64_t location = 0;
    uint64_t target   = 0;
    int      depth    = 0;
};

class DeepBacktrace {
public:
    static DeepBacktrace& instance();

    void setFrames(const std::string& json);
    std::vector<DeepFrame> getFrames() const;

    bool isCollecting() const { return m_collecting.load(); }
    void setCollecting(bool v) { m_collecting.store(v); }

    std::vector<DeepFrame> frames() const;
    void clear();

private:
    DeepBacktrace() = default;
    mutable std::mutex m_mu;
    std::vector<DeepFrame> m_frames;
    std::atomic<bool> m_collecting{false};
};

} // namespace yambz
