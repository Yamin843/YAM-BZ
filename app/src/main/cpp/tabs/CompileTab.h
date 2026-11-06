#pragma once
namespace yambz {
class CompileTab {
public:
    static CompileTab& instance();
    void draw();
private:
    CompileTab() = default;
};
}
