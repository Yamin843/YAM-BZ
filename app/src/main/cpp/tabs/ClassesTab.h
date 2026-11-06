#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>

namespace yambz {
class ClassesTab {
public:
    static ClassesTab& instance();
    void draw();
    void onStartClicked();
    void startClassEnum();
private:
    ClassesTab() = default;

    struct State {
        std::vector<std::string> classes;
        std::mutex classesMu;
        bool enumerating = false;
        bool complete = false;

        // Selected class + its members
        std::string selectedClass;
        std::vector<std::string> selectedMembers;
        std::mutex membersMu;

        // UI
        char searchBuf[128] = {0};
        char memberSearchBuf[128] = {0};
        int  selectedMemberIdx = -1;
    };
    State* state();
};
}
