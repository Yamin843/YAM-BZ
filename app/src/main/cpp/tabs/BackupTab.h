#pragma once
namespace yambz {
class BackupTab {
public:
    static BackupTab& instance();
    void draw();
private:
    BackupTab() = default;
};
}
