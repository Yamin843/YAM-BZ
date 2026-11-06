#pragma once
namespace yambz {
class SettingsTab {
public:
    static SettingsTab& instance();
    void draw();
    bool isFpsEnabled() const;
private:
    SettingsTab() = default;
};
}
