#pragma once
#include <imgui.h>

namespace yambz {
class Theme {
public:
    static void apply();
    static ImU32 gold();
    static ImU32 textPrimary();
    static ImU32 textDim();
    static void sectionHeader(const char* title);
};
}
