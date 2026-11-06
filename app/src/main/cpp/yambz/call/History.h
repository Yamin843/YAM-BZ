#pragma once
#include <string>
#include <deque>
#include <mutex>

namespace yambz {

struct HistoryEntry {
    uint64_t ts = 0;
    std::string methodDesc;
    std::string thisJson;
    std::string argsJson;   // concatenated [this, args...]
    std::string resultJson;
    bool success = false;
    long elapsedMs = 0;
};

class History {
public:
    static History& instance();

    void init(const std::string& path);
    void push(const HistoryEntry& e);
    std::vector<HistoryEntry> recent(size_t n = 50) const;
    void clear();

    /// persistence
    void saveToDisk();
    void loadFromDisk();

private:
    History() = default;
    mutable std::mutex m_mu;
    std::deque<HistoryEntry> m_entries;
    size_t m_capacity = 100;
    std::string m_path;
    bool m_inited = false;
};

} // namespace yambz
