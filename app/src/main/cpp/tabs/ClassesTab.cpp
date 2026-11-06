#include "ClassesTab.h"
#include "ui/Theme.h"
#include <imgui.h>

namespace yambz {
ClassesTab& ClassesTab::instance() { static ClassesTab t; return t; }
void ClassesTab::draw() {
    Theme::sectionHeader("Classes");
    ImGui::TextWrapped("سيُبنى في الرد القادم.");
}
}
