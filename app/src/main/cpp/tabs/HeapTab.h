#pragma once
namespace yambz {
class HeapTab {
public:
    static HeapTab& instance();
    void draw();
private:
    HeapTab() = default;
};
}
