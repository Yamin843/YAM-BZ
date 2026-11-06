#pragma once
#include <string>
#include <mutex>

namespace yambz {
class CallTab {
public:
    static CallTab& instance();
    void draw();
private:
    CallTab() = default;
    struct State {
        char spec[512] = {0};
        char jsonArgs[2048] = {0};
        char result[4096] = {0};
        bool invoking = false;
        std::mutex mu;
    };
    State* state();
};
}
