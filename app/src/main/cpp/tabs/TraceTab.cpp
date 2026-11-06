#include "TraceTab.h"
#include "ui/Theme.h"
#include <imgui.h>

namespace yambz {
TraceTab& TraceTab::instance() { static TraceTab t; return t; }
void TraceTab::draw() {
    Theme::sectionHeader("Trace");
    ImGui::TextWrapped("سيُبنى في الرد القادم.");
}
}
