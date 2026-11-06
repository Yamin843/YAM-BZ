#include "NativeTab.h"
#include "ui/Theme.h"
#include <imgui.h>

namespace yambz {
NativeTab& NativeTab::instance() { static NativeTab t; return t; }
void NativeTab::draw() {
    Theme::sectionHeader("Native");
    ImGui::TextWrapped("سيُبنى في الرد القادم.");
}
}
