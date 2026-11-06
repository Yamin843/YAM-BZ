#include "SettingsTab.h"
#include "ui/Theme.h"
#include <imgui.h>

namespace yambz {
SettingsTab& SettingsTab::instance() { static SettingsTab t; return t; }
void SettingsTab::draw() {
    Theme::sectionHeader("Settings");
    ImGui::TextWrapped("سيُبنى في الرد القادم.");
}
}
