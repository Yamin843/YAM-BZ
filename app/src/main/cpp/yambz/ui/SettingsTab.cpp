#include "SettingsTab.h"
#include <string>
#include "Theme.h"
#include "core/ErrorContext.h"
#include "core/Logger.h"
#include "trace/TraceStore.h"

#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace yambz {

SettingsTab& SettingsTab::instance() {
    static SettingsTab t;
    return t;
}

// ─── settings struct ───
struct Settings {
    float fontSize = 38.0f;
    bool  darkTheme = true;
    int   traceCapacity = 100000;
    int   errorCapacity = 2000;
    bool  autoStartEnum = true;
    bool  showFps = false;
    int   graphDepth = 5;
    int   objectDumpDepth = 5;
};
static Settings g_settings;
static const char* kSettingsFile = "/settings.cfg";
static bool s_dirty = false;

static std::string settingsPath() {
    extern std::string yambz_getCacheDir();
    return yambz_getCacheDir() + "/yambz" + kSettingsFile;
}

void SettingsTab::load() {
    std::ifstream f(settingsPath());
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        try {
            if (k == "fontSize")       g_settings.fontSize = std::stof(v);
            else if (k == "darkTheme") g_settings.darkTheme = (v == "1");
            else if (k == "traceCap")  g_settings.traceCapacity = std::stoi(v);
            else if (k == "errorCap")  g_settings.errorCapacity = std::stoi(v);
            else if (k == "autoEnum")  g_settings.autoStartEnum = (v == "1");
            else if (k == "showFps")   g_settings.showFps = (v == "1");
            else if (k == "graphDepth")g_settings.graphDepth = std::stoi(v);
            else if (k == "objDepth")  g_settings.objectDumpDepth = std::stoi(v);
        } catch (...) {}
    }
    Theme::setFontSize(g_settings.fontSize);
    Theme::reloadFont();
    TraceStore::instance().setCapacity((size_t) g_settings.traceCapacity);
    ErrorContext::instance().setCapacity((size_t) g_settings.errorCapacity);
}

void SettingsTab::save() {
    std::ofstream f(settingsPath(), std::ios::trunc);
    if (!f.is_open()) return;
    f << "fontSize=" << g_settings.fontSize << "\n";
    f << "darkTheme=" << (g_settings.darkTheme ? 1 : 0) << "\n";
    f << "traceCap=" << g_settings.traceCapacity << "\n";
    f << "errorCap=" << g_settings.errorCapacity << "\n";
    f << "autoEnum=" << (g_settings.autoStartEnum ? 1 : 0) << "\n";
    f << "showFps=" << (g_settings.showFps ? 1 : 0) << "\n";
    f << "graphDepth=" << g_settings.graphDepth << "\n";
    f << "objDepth=" << g_settings.objectDumpDepth << "\n";
}

void SettingsTab::draw() {
    bool changed = false;

    Theme::drawSectionHeader("Appearance");
    ImGui::Text("Font size");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderFloat("##fs", &g_settings.fontSize, 24.0f, 56.0f, "%.0f px")) {
        Theme::setFontSize(g_settings.fontSize);
        Theme::reloadFont();
        changed = true;
    }

    ImGui::Text("Theme");
    ImGui::SameLine(180);
    if (ImGui::RadioButton("Dark", g_settings.darkTheme)) {
        g_settings.darkTheme = true;
        Theme::apply();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Light", !g_settings.darkTheme)) {
        g_settings.darkTheme = false;
        Theme::applyLight();
        changed = true;
    }

    ImGui::Text("Show FPS counter");
    ImGui::SameLine(180);
    if (ImGui::Checkbox("##fps", &g_settings.showFps)) changed = true;

    Theme::drawSectionHeader("Trace");
    ImGui::Text("Trace events capacity");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(300);
    if (ImGui::DragInt("##tcap", &g_settings.traceCapacity, 1000, 1000, 1000000, "%d events")) {
        TraceStore::instance().setCapacity((size_t) g_settings.traceCapacity);
        changed = true;
    }

    ImGui::Text("Backtrace depth");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderInt("##gdepth", &g_settings.graphDepth, 1, 20)) changed = true;

    ImGui::Text("Object dump depth");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderInt("##odepth", &g_settings.objectDumpDepth, 1, 16)) changed = true;

    Theme::drawSectionHeader("Errors");
    ImGui::Text("Error records capacity");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(300);
    if (ImGui::DragInt("##ecap", &g_settings.errorCapacity, 100, 100, 20000, "%d records")) {
        ErrorContext::instance().setCapacity((size_t) g_settings.errorCapacity);
        changed = true;
    }

    Theme::drawSectionHeader("Startup");
    ImGui::Text("Auto-start class enumeration");
    ImGui::SameLine(180);
    if (ImGui::Checkbox("##auto", &g_settings.autoStartEnum)) changed = true;

    Theme::drawSectionHeader("Storage");
    extern std::string yambz_getCacheDir();
    ImGui::Text("Cache dir: %s", yambz_getCacheDir().c_str());

    Theme::drawSectionHeader("About");
    ImGui::Text("YAM-BZ v1.0.0");
    ImGui::Text("Built: %s %s", __DATE__, __TIME__);
    ImGui::Text("Albatross: %s",
        AlbatrossFeatures());  // helper below

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Save settings", ImVec2(200, 0))) {
        save();
        extern void yambz_notify(const char* msg);
        yambz_notify("Settings saved");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults", ImVec2(200, 0))) {
        g_settings = Settings{};
        Theme::setFontSize(g_settings.fontSize);
        Theme::reloadFont();
        TraceStore::instance().setCapacity((size_t) g_settings.traceCapacity);
        ErrorContext::instance().setCapacity((size_t) g_settings.errorCapacity);
        save();
    }
    if (changed) s_dirty = true;
}

// ─── helper: آلbatross features ───
extern "C" const char* AlbatrossFeatures() {
    static char buf[128] = "unknown";
    // (سنتخطى الاتصال بـ Java هنا — نطبع ثابتاً)
    snprintf(buf, sizeof buf, "jit,instruction,trace");
    return buf;
}

} // namespace yambz

bool yambz_showFps() {
    return g_settings.showFps;
}
int yambz_getBacktraceDepth() { return g_settings.graphDepth; }
int yambz_getObjectDumpDepth() { return g_settings.objectDumpDepth; }
