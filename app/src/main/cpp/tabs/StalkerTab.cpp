#include "StalkerTab.h"
#include "ui/Theme.h"
#include <imgui.h>

namespace yambz {
StalkerTab& StalkerTab::instance() { static StalkerTab t; return t; }
void StalkerTab::draw() {
    Theme::sectionHeader("Stalker");
    ImGui::TextWrapped("سيُبنى في الرد القادم.");
}
}
