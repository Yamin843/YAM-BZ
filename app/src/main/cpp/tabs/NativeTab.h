#pragma once
namespace yambz {
class NativeTab {
public:
    static NativeTab& instance();
    void draw();
private:
    NativeTab() = default;
};
}
