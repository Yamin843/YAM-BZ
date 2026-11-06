#include "ErrorContext.h"
#include <vector>
#include "Logger.h"
#include <mutex>
#include <algorithm>
#include <chrono>

namespace yambz {

ErrorContext& ErrorContext::instance() {
    static ErrorContext e;
    return e;
}

static uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t ErrorContext::report(ErrSeverity sev, ErrDomain dom,
                              const std::string& op, const std::string& tgt,
                              const std::string& msg, const std::string& trace) {
    uint64_t id;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        id = m_nextId++;
        ErrorRecord r;
        r.id = id;
        r.timestampMs = nowMs();
        r.severity = sev;
        r.domain = dom;
        r.operation = op;
        r.target = tgt;
        r.message = msg;
        r.stackTrace = trace;
        while (m_records.size() >= m_capacity) m_records.pop_front();
        m_records.push_back(std::move(r));
    }
    Listener l;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        l = m_listener;
    }
    // log to logcat too
    switch (sev) {
        case ErrSeverity::Info:    LOGI("[%s] %s: %s", op.c_str(), tgt.c_str(), msg.c_str()); break;
        case ErrSeverity::Warning: LOGW("[%s] %s: %s", op.c_str(), tgt.c_str(), msg.c_str()); break;
        case ErrSeverity::Error:   LOGE("[%s] %s: %s", op.c_str(), tgt.c_str(), msg.c_str()); break;
        case ErrSeverity::Fatal:   LOGE("[FATAL][%s] %s: %s", op.c_str(), tgt.c_str(), msg.c_str()); break;
    }
    if (l) l();
    return id;
}

void ErrorContext::clear() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_records.clear();
}

void ErrorContext::setCapacity(size_t cap) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_capacity = std::max<size_t>(cap, 100);
    while (m_records.size() > m_capacity) m_records.pop_front();
}

std::vector<ErrorRecord> ErrorContext::recent(size_t count) const {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<ErrorRecord> out;
    size_t start = m_records.size() > count ? m_records.size() - count : 0;
    out.reserve(m_records.size() - start);
    for (size_t i = start; i < m_records.size(); i++)
        out.push_back(m_records[i]);
    return out;
}

size_t ErrorContext::countBySeverity(ErrSeverity sev) const {
    std::lock_guard<std::mutex> lk(m_mu);
    size_t n = 0;
    for (const auto& r : m_records) if (r.severity == sev) n++;
    return n;
}

size_t ErrorContext::totalCount() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_records.size();
}

void ErrorContext::setListener(Listener l) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_listener = std::move(l);
}

} // namespace yambz
