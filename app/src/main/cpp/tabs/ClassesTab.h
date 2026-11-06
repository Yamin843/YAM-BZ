#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace yambz {
class ClassesTab {
public:
    static ClassesTab& instance();
    void draw();

    // Target setters (called from context menu)
    void setTraceTarget(const std::string& cls, const std::string& member);
    void setCallTarget(const std::string& cls, const std::string& member);

private:
    ClassesTab() = default;
    struct State;
    State* st();
};
}
