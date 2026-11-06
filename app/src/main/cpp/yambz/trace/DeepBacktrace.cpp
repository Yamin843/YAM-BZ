#include "DeepBacktrace.h"
#include <vector>
#include "../core/Logger.h"
#include <mutex>
#include "core/Helpers.h"

namespace yambz {

DeepBacktrace& DeepBacktrace::instance() {
    static DeepBacktrace d;
    return d;
}

// بسيط JSON parser للframes
static std::vector<DeepFrame> parseFrames(const std::string& json) {
    std::vector<DeepFrame> out;
    size_t p = 0;
    while (p < json.size()) {
        size_t obj = json.find('{', p);
        if (obj == std::string::npos) break;
        size_t end = json.find('}', obj);
        if (end == std::string::npos) break;
        std::string body = json.substr(obj + 1, end - obj - 1);

        auto getStr = [&](const char* key) -> std::string {
            std::string k = "\"" + std::string(key) + "\":\"";
            size_t i = body.find(k);
            if (i == std::string::npos) return "";
            size_t s = i + k.size();
            size_t e = body.find('"', s);
            return body.substr(s, e - s);
        };
        auto getInt = [&](const char* key) -> long long {
            std::string k = "\"" + std::string(key) + "\":";
            size_t i = body.find(k);
            if (i == std::string::npos) return 0;
            size_t s = i + k.size();
            size_t e = body.find_first_of(",}", s);
            try { return std::stoll(body.substr(s, e - s)); }
            catch (...) { return 0; }
        };

        DeepFrame f;
        auto loc = getStr("loc");
        auto tgt = getStr("tgt");
        if (!loc.empty()) f.location = std::stoull(loc, nullptr, 16);
        if (!tgt.empty()) f.target = std::stoull(tgt, nullptr, 16);
        f.depth = (int) getInt("d");
        out.push_back(f);

        p = end + 1;
    }
    return out;
}

void DeepBacktrace::setFrames(const std::string& json) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_frames = parseFrames(json);
    LOGI("DeepBacktrace: %zu frames stored", m_frames.size());
}

std::vector<DeepFrame> DeepBacktrace::getFrames() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_frames;
}

void DeepBacktrace::clear() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_frames.clear();
}

} // namespace yambz
