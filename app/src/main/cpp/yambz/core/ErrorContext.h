#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <deque>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace yambz {

enum class ErrSeverity : uint8_t { Info = 0, Warning = 1, Error = 2, Fatal = 3 };

enum class ErrDomain : uint8_t {
    Reflection = 0,
    Hook       = 1,
    Dump       = 2,
    Build      = 3,
    Invoke     = 4,
    Native     = 5,
    Io         = 6,
    Internal   = 7,
};

struct ErrorRecord {
    uint64_t    id = 0;
    uint64_t    timestampMs = 0;
    ErrSeverity severity = ErrSeverity::Error;
    ErrDomain   domain   = ErrDomain::Internal;
    std::string operation;      // "reflection.invoke", "hook.install", ...
    std::string target;         // class/method/etc.
    std::string message;
    std::string stackTrace;     // optional
    bool        recovered = false;
};

/**
 * ErrorContext — global thread-safe error ring buffer.
 *
 * - كل الأخطاء تُسجَّل هنا (لا تعطّل أي عملية)
 * - UI يقرأها بشكل reactive
 * - يدعم التصفية بـ severity/domain
 * - يوفر استعلامات سريعة: recent(), bySeverity(), count()
 */
class ErrorContext {
public:
    static ErrorContext& instance();

    uint64_t report(ErrSeverity sev, ErrDomain dom,
                    const std::string& op, const std::string& target,
                    const std::string& message,
                    const std::string& stackTrace = "");

    void clear();
    void setCapacity(size_t cap);

    std::vector<ErrorRecord> recent(size_t count = 500) const;
    size_t countBySeverity(ErrSeverity sev) const;
    size_t totalCount() const;

    /// listener — يُستدعى مرة عند كل دفعة
    using Listener = std::function<void()>;
    void setListener(Listener l);

private:
    ErrorContext() = default;

    mutable std::mutex m_mu;
    std::deque<ErrorRecord> m_records;
    size_t m_capacity = 2000;
    uint64_t m_nextId = 1;
    Listener m_listener;
};

// convenience macros
#define YAMBZ_INFO(dom, op, tgt, msg) \
    ::yambz::ErrorContext::instance().report(::yambz::ErrSeverity::Info,   dom, op, tgt, msg)
#define YAMBZ_WARN(dom, op, tgt, msg) \
    ::yambz::ErrorContext::instance().report(::yambz::ErrSeverity::Warning,dom, op, tgt, msg)
#define YAMBZ_ERR(dom, op, tgt, msg) \
    ::yambz::ErrorContext::instance().report(::yambz::ErrSeverity::Error,  dom, op, tgt, msg)
#define YAMBZ_FATAL(dom, op, tgt, msg) \
    ::yambz::ErrorContext::instance().report(::yambz::ErrSeverity::Fatal,  dom, op, tgt, msg)

} // namespace yambz
