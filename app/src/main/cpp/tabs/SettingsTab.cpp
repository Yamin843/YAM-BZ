#include "SettingsTab.h"
#include "ui/Theme.h"
#include "core/Helpers.h"

#include <imgui.h>

namespace yambz {

SettingsTab& SettingsTab::instance() { static SettingsTab t; return t; }

static float g_fontSize = 36.0f;
static bool g_showFps = false;

void SettingsTab::draw() {
    Theme::sectionHeader("Appearance");
    ImGui::Text("Font Size");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderFloat("##fs", &g_fontSize, 24.0f, 56.0f, "%.0f px")) {
        // Would need renderer to reload font
    }

    ImGui::Text("Show FPS");
    ImGui::SameLine(180);
    ImGui::Checkbox("##fps", &g_showFps);

    Theme::sectionHeader("Cache");
    ImGui::TextWrapped("Cache dir: %s", yambz_getCacheDir().c_str());

    Theme::sectionHeader("About");
    ImGui::Text("YAM-BZ v1.0.0");
    ImGui::Text("Built: %s %s", __DATE__, __TIME__);
    ImGui::Text("Engine: Albatross v4.0.0");

    ImGui::Separator();
    ImGui::TextWrapped(
        "Rebuilt from scratch: all tabs now use Albatross APIs directly. "
        "Trace uses Ocean (traceMemberNative + traceGlobal + 8 event types). "
        "Call uses MethodDef/ConstructorDef. Native uses AlbNative + CModule. "
        "Stalker uses Stalker.followSymbol.");
}

} // namespace yambz
