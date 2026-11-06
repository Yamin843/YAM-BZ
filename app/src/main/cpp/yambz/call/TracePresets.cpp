#include "TracePresets.h"
#include <vector>
#include "../core/ErrorContext.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <mutex>
#include "core/Helpers.h"

namespace yambz {

TracePresets& TracePresets::instance() {
    static TracePresets p;
    return p;
}

std::string TracePresets::sanitize(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == ' ')
            o += '_';
        else o += c;
    }
    if (o.size() > 80) o.resize(80);
    return o;
}

void TracePresets::init(const std::string& dir) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_dir = dir + "/trace_presets";
    ::mkdir(m_dir.c_str(), 0700);
    m_inited = true;
}

bool TracePresets::save(const TracePreset& p) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_inited) return false;
    std::string path = m_dir + "/" + sanitize(p.name) + ".json";
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;
    f << "{\"name\":\"" << p.name
      << "\",\"methodDesc\":\"" << p.methodDesc
      << "\",\"backtrace\":" << (p.captureBacktrace ? 1 : 0)
      << ",\"btDepth\":" << p.backtraceDepth
      << ",\"objDepth\":" << p.objectDumpDepth
      << ",\"args\":" << (p.captureArgs ? 1 : 0)
      << ",\"this\":" << (p.captureThis ? 1 : 0)
      << ",\"ts\":" << p.createdAt << "}";
    f.close();
    YAMBZ_INFO(ErrDomain::Io, "trace.preset.save", p.name, "saved");
    return true;
}

std::vector<TracePreset> TracePresets::listAll() {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<TracePreset> out;
    if (!m_inited) return out;
    DIR* d = ::opendir(m_dir.c_str());
    if (!d) return out;
    struct dirent* ent;
    while ((ent = ::readdir(d))) {
        std::string n = ent->d_name;
        if (n.size() < 6 || n.substr(n.size()-5) != ".json") continue;
        std::string path = m_dir + "/" + n;
        std::ifstream f(path);
        if (!f.is_open()) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string body = ss.str();
        f.close();
        TracePreset p;
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
            try { return std::stoll(body.substr(s, e - s)); } catch (...) { return 0; }
        };
        p.name = getStr("name");
        p.methodDesc = getStr("methodDesc");
        p.captureBacktrace = getInt("backtrace") != 0;
        p.backtraceDepth = (int) getInt("btDepth");
        p.objectDumpDepth = (int) getInt("objDepth");
        p.captureArgs = getInt("args") != 0;
        p.captureThis = getInt("this") != 0;
        p.createdAt = (uint64_t) getInt("ts");
        if (!p.name.empty()) out.push_back(std::move(p));
    }
    ::closedir(d);
    return out;
}

std::vector<TracePreset> TracePresets::listForMethod(const std::string& methodDesc) {
    auto all = listAll();
    std::vector<TracePreset> out;
    for (auto& p : all) if (p.methodDesc == methodDesc) out.push_back(p);
    return out;
}

bool TracePresets::remove(const std::string& name) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_inited) return false;
    std::string path = m_dir + "/" + sanitize(name) + ".json";
    return ::unlink(path.c_str()) == 0;
}

bool TracePresets::load(const std::string& name, TracePreset& out) {
    auto all = listAll();
    for (auto& p : all) if (p.name == name) { out = p; return true; }
    return false;
}

} // namespace yambz
