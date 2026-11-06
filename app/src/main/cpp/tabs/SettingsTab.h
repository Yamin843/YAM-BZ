#pragma once
namespace yambz {
class SettingsTab {
public:
    static SettingsTab& instance();
    void draw();
private:
    SettingsTab() = default;
};
}
