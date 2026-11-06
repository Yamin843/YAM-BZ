#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace yambz {
class NativeTab {
public:
    static NativeTab& instance();
    void draw();
private:
    NativeTab() = default;
    struct State {
        std::vector<std::string> libraries;
        std::vector<std::string> symbols;
        int selectedLib = -1;
        char searchBuf[128] = {0};
        char cmoduleSource[8192] = {
            "#include <albatross.h>\n"
            "int add(int a, int b) { return a + b; }\n"
        };
        char cmoduleResult[2048] = {0};
        char symbolCallSig[256] = "int(int,int)";
        char symbolCallArgs[512] = "[2, 3]";
        std::mutex mu;
        int currentSubTab = 0; // 0=libs, 1=cmodule
    };
    State* state();
};
}
