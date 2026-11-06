#include "History.h"
#include <vector>
#include <fstream>
#include <chrono>
#include <sstream>

namespace yambz {

History& History::instance() {
    static History h;
    return h;
}

static uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void History::init(const std::string& path) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_path = path;
    m_inited = true;
    loadFromDisk();
}

void History::push(const HistoryEntry& e) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_entries.push_back(e);
    while (m_entries.size() > m_capacity) m_entries.pop_front();
}

std::vector<HistoryEntry> History::recent(size_t n) const {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<HistoryEntry> out;
    size_t start = m_entries.size() > n ? m_entries.size() - n : 0;
    for (size_t i = start; i < m_entries.size(); i++) out.push_back(m_entries[i]);
    return out;
}

void History::clear() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_entries.clear();
    saveToDisk();
}

void History::saveToDisk() {
    if (!m_inited) return;
    std::ofstream f(m_path, std::ios::trunc);
    if (!f.is_open()) return;
    for (const auto& e : m_entries) {
        f << e.ts << '|' << e.methodDesc << '|' << e.success << '|' << e.elapsedMs << '\n';
    }
}

void History::loadFromDisk() {
    if (!m_inited) return;
    std::ifstream f(m_path);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        HistoryEntry e;
        std::string tok;
        if (std::getline(ss, tok, '|')) e.ts = std::stoull(tok);
        if (std::getline(ss, tok, '|')) e.methodDesc = tok;
        if (std::getline(ss, tok, '|')) e.success = (tok == "1");
        if (std::getline(ss, tok, '|')) e.elapsedMs = std::stol(tok);
        m_entries.push_back(std::move(e));
    }
}

} // namespace yambz
