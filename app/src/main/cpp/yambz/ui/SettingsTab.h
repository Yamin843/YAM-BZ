#pragma once
namespace yambz {
class SettingsTab {
public:
    static SettingsTab& instance();
    void draw();
    void load();
    void save();
private:
    SettingsTab() = default;
};
}
