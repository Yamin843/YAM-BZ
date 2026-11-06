#pragma once
namespace yambz {
class TraceTab {
public:
    static TraceTab& instance();
    void draw();
private:
    TraceTab() = default;
};
}
