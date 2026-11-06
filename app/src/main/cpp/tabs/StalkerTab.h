#pragma once
namespace yambz {
class StalkerTab {
public:
    static StalkerTab& instance();
    void draw();
private:
    StalkerTab() = default;
};
}
