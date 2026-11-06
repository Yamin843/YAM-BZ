#pragma once
namespace yambz {
class LogsTab {
public:
    static LogsTab& instance();
    void draw();
private:
    LogsTab() = default;
};
}
