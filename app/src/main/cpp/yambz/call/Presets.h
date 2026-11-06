#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace yambz {

struct Preset {
    std::string name;       // user-given
    std::string methodDesc; // target
    std::string json;       // full ParamNode serialized
    uint64_t    createdAt = 0;
};

class Presets {
public:
    static Presets& instance();

    /// init from dir (e.g. code_cache/yambz/presets)
    void init(const std::string& dir);

    /// save preset (methodDesc + name)
    bool save(const std::string& methodDesc, const std::string& name,
              const std::string& json);

    /// load all for method
    std::vector<Preset> listFor(const std::string& methodDesc);

    /// delete by name
    bool remove(const std::string& methodDesc, const std::string& name);

    /// list all methods with presets
    std::vector<std::string> knownMethods();

private:
    Presets() = default;

    bool writeJsonFile(const std::string& path, const std::string& data);
    std::string readJsonFile(const std::string& path);

    mutable std::mutex m_mu;
    std::string m_dir;
    bool m_inited = false;
};

} // namespace yambz
