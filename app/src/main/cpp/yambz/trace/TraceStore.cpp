#include "TraceStore.h"
#include "../core/Logger.h"

namespace yambz {

TraceStore& TraceStore::instance() {
    static TraceStore s;
    return s;
}

void TraceStore::push(TraceEvent ev) {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_recording) return;
        m_totalPushed++;
        while (m_events.size() >= m_capacity) {
            m_events.pop_front();
            m_dropped++;
        }
        m_events.push_back(std::move(ev));
    }
    Listener l;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        l = m_listener;
    }
    if (l) l();
}

void TraceStore::pushBatch(std::vector<TraceEvent> evs) {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_recording) return;
        for (auto& ev : evs) {
            m_totalPushed++;
            while (m_events.size() >= m_capacity) {
                m_events.pop_front();
                m_dropped++;
            }
            m_events.push_back(std::move(ev));
        }
    }
    Listener l;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        l = m_listener;
    }
    if (l) l();
}

std::vector<TraceEvent> TraceStore::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return {m_events.begin(), m_events.end()};
}

size_t TraceStore::count() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_events.size();
}

void TraceStore::clear() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_events.clear();
    m_dropped = 0;
}

void TraceStore::setCapacity(size_t cap) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_capacity = std::max<size_t>(cap, 1000);
    while (m_events.size() > m_capacity) {
        m_events.pop_front();
        m_dropped++;
    }
}

void TraceStore::setListener(Listener l) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_listener = std::move(l);
}

void TraceStore::setRecording(bool rec) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_recording = rec;
}

bool TraceStore::recording() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_recording;
}

size_t TraceStore::totalPushed() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_totalPushed;
}

size_t TraceStore::dropped() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_dropped;
}

} // namespace yambz
