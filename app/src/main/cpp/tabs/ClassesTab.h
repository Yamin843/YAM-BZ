#pragma once
namespace yambz {
class ClassesTab {
public:
    static ClassesTab& instance();
    void draw();
private:
    ClassesTab() = default;
};
}
