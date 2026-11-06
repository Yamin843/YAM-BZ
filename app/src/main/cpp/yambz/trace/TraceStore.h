#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "TraceEvent.h"
#include <deque>
#include <mutex>
#include <memory>
#include <functional>

namespace yambz {

/**
 * Ring buffer thread-safe للأحداث.
 *
 * - كل الأحداث تُخزَّن (configurable cap، افتراضي 100,000)
 * - يُخطر listeners عند كل دفعة (لا لكل حدث)
 * - يمكن استرجاع snapshot للـ UI
 * - عند تجاوز السعة، الأقدم يُحذف
 */
class TraceStore {
public:
    static TraceStore& instance();

    /// إضافة حدث واحد
    void push(TraceEvent ev);

    /// دفعة
    void pushBatch(std::vector<TraceEvent> evs);

    /// snapshot للـ UI (copy، مؤقت)
    std::vector<TraceEvent> snapshot() const;

    /// عدد الأحداث
    size_t count() const;

    /// مسح
    void clear();

    /// ضبط السعة
    void setCapacity(size_t cap);

    /// listener (يُستدعى مرة كل دفعة — thread-safe)
    using Listener = std::function<void()>;
    void setListener(Listener l);

    /// تعطيل/تفعيل التسجيل (لتقليل الضغط عند الإغلاق)
    void setRecording(bool rec);
    bool recording() const;

    // إحصائيات
    size_t totalPushed() const;
    size_t dropped() const;

private:
    TraceStore() = default;

    mutable std::mutex m_mu;
    std::deque<TraceEvent> m_events;
    size_t m_capacity = 100000;
    size_t m_totalPushed = 0;
    size_t m_dropped = 0;
    bool   m_recording = true;
    Listener m_listener;
};

} // namespace yambz
