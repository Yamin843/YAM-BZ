#pragma once
namespace yambz {
class CallTab {
public:
    static CallTab& instance();
    void draw();
private:
    CallTab() = default;
};
}
