#include "Presets.h"
#include "../core/ErrorContext.h"

#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <sstream>
#include <chrono>

namespace yambz {

Presets& Presets::instance() {
    static Presets p;
    return p;
}

static std::string sanitize(const std::string& s) {
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

void Presets::init(const std::string& dir) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_dir = dir;
    ::mkdir(dir.c_str(), 0700);
    m_inited = true;
}

bool Presets::writeJsonFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) {
        YAMBZ_ERR(ErrDomain::Io, "preset.write", path, "open failed");
        return false;
    }
    f << data;
    f.close();
    return true;
}

std::string Presets::readJsonFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Presets::save(const std::string& methodDesc, const std::string& name,
                   const std::string& json) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_inited) return false;
    std::string methodDir = m_dir + "/" + sanitize(methodDesc);
    ::mkdir(methodDir.c_str(), 0700);
    std::string path = methodDir + "/" + sanitize(name) + ".json";
    // wrap json with metadata
    std::string wrapper = "{\"name\":\"" + name + "\",\"methodDesc\":\"" + methodDesc +
                          "\",\"data\":" + json + "}";
    return writeJsonFile(path, wrapper);
}

std::vector<Preset> Presets::listFor(const std::string& methodDesc) {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<Preset> out;
    if (!m_inited) return out;
    std::string methodDir = m_dir + "/" + sanitize(methodDesc);
    DIR* d = ::opendir(methodDir.c_str());
    if (!d) return out;
    struct dirent* ent;
    while ((ent = ::readdir(d))) {
        std::string n = ent->d_name;
        if (n.size() < 6 || n.substr(n.size()-5) != ".json") continue;
        std::string path = methodDir + "/" + n;
        std::string content = readJsonFile(path);
        if (content.empty()) continue;
        // naive parse — نستخرج name و data
        size_t np = content.find("\"name\":\"");
        size_t mp = content.find("\"methodDesc\":\"");
        size_t dp = content.find("\"data\":");
        if (np == std::string::npos || dp == std::string::npos) continue;
        Preset p;
        p.methodDesc = methodDesc;
        size_t np2 = content.find('"', np + 8);
        if (np2 != std::string::npos) p.name = content.substr(np + 8, np2 - (np+8));
        // data: rest from dp+7 to last }
        p.json = content.substr(dp + 7);
        // strip trailing wrapper }
        if (!p.json.empty() && p.json.back() == '}') p.json.pop_back();
        p.createdAt = (uint64_t) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        out.push_back(std::move(p));
    }
    ::closedir(d);
    return out;
}

bool Presets::remove(const std::string& methodDesc, const std::string& name) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_inited) return false;
    std::string path = m_dir + "/" + sanitize(methodDesc) + "/" + sanitize(name) + ".json";
    return ::unlink(path.c_str()) == 0;
}

std::vector<std::string> Presets::knownMethods() {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<std::string> out;
    if (!m_inited) return out;
    DIR* d = ::opendir(m_dir.c_str());
    if (!d) return out;
    struct dirent* ent;
    while ((ent = ::readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        out.emplace_back(ent->d_name);
    }
    ::closedir(d);
    return out;
}

} // namespace yambz
