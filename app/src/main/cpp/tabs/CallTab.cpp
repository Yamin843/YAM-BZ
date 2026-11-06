#include "CallTab.h"
#include "ui/Theme.h"
#include <imgui.h>

namespace yambz {
CallTab& CallTab::instance() { static CallTab t; return t; }
void CallTab::draw() {
    Theme::sectionHeader("Call");
    ImGui::TextWrapped("سيُبنى في الرد القادم.");
}
}
