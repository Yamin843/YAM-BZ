#include "LogsTab.h"
#include <functional>
#include <string>
#include <vector>
#include "Theme.h"
#include "core/ErrorContext.h"
#include "core/Logger.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include "core/Helpers.h"

namespace yambz {

LogsTab& LogsTab::instance() {
    static LogsTab t;
    return t;
}

// ─── filters ───
static bool s_filterInfo  = true;
static bool s_filterWarn  = true;
static bool s_filterError = true;
static bool s_filterFatal = true;
static char s_search[512] = {0};
static int  s_selected = -1;
static bool s_autoScroll = true;
static int  s_selectedDomain = -1;  // -1 = all

static const char* domainName(ErrDomain d) {
    switch (d) {
        case ErrDomain::Reflection: return "Reflection";
        case ErrDomain::Hook:       return "Hook";
        case ErrDomain::Dump:       return "Dump";
        case ErrDomain::Build:      return "Build";
        case ErrDomain::Invoke:     return "Invoke";
        case ErrDomain::Native:     return "Native";
        case ErrDomain::Io:         return "Io";
        case ErrDomain::Internal:   return "Internal";
    }
    return "?";
}

static ImVec4 severityColor(ErrSeverity s) {
    switch (s) {
        case ErrSeverity::Info:    return ImVec4(0.55f, 0.75f, 0.90f, 1.0f);
        case ErrSeverity::Warning: return ImVec4(0.95f, 0.75f, 0.30f, 1.0f);
        case ErrSeverity::Error:   return ImVec4(0.95f, 0.40f, 0.30f, 1.0f);
        case ErrSeverity::Fatal:   return ImVec4(1.00f, 0.20f, 0.20f, 1.0f);
    }
    return ImVec4(1,1,1,1);
}

static bool matches(const ErrorRecord& r) {
    if (r.severity == ErrSeverity::Info    && !s_filterInfo)  return false;
    if (r.severity == ErrSeverity::Warning && !s_filterWarn)  return false;
    if (r.severity == ErrSeverity::Error   && !s_filterError) return false;
    if (r.severity == ErrSeverity::Fatal   && !s_filterFatal) return false;
    if (s_selectedDomain >= 0 && (int) r.domain != s_selectedDomain) return false;
    if (s_search[0]) {
        std::string q = s_search;
        for (auto& c : q) c = (char) std::tolower((unsigned char)c);
        auto lower = [](const std::string& s) {
            std::string o = s;
            for (auto& c : o) c = (char) std::tolower((unsigned char)c);
            return o;
        };
        if (lower(r.message).find(q) == std::string::npos &&
            lower(r.target).find(q) == std::string::npos &&
            lower(r.operation).find(q) == std::string::npos)
            return false;
    }
    return true;
}

// ─── export to file ───
static void exportLogs(const std::string& path) {
    auto records = ErrorContext::instance().recent(2000);
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "# YAM-BZ logs — %zu entries\n\n", records.size());
    for (const auto& r : records) {
        fprintf(f, "[%llu] [%s/%s] %s | %s: %s\n",
            (unsigned long long) r.timestampMs,
            domainName(r.domain),
            r.severity == ErrSeverity::Info ? "INFO" :
            r.severity == ErrSeverity::Warning ? "WARN" :
            r.severity == ErrSeverity::Error ? "ERROR" : "FATAL",
            r.operation.c_str(),
            r.target.c_str(),
            r.message.c_str());
        if (!r.stackTrace.empty()) {
            fprintf(f, "  %s\n", r.stackTrace.c_str());
        }
    }
    fclose(f);
}

void LogsTab::draw() {
    auto records = ErrorContext::instance().recent(2000);

    // ─── toolbar ───
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));

    // search
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##search", "search...", s_search, sizeof s_search);
    ImGui::SameLine();

    // severity toggles
    auto tog = [&](const char* label, bool* v, ImVec4 col) {
        if (*v) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x, col.y, col.z, 0.35f));
        if (ImGui::Button(label, ImVec2(50, 0))) *v = !*v;
        if (*v) ImGui::PopStyleColor();
        ImGui::SameLine(0, 4);
    };
    tog("I", &s_filterInfo, severityColor(ErrSeverity::Info));
    tog("W", &s_filterWarn, severityColor(ErrSeverity::Warning));
    tog("E", &s_filterError, severityColor(ErrSeverity::Error));
    tog("F", &s_filterFatal, severityColor(ErrSeverity::Fatal));

    // domain picker
    ImGui::SetNextItemWidth(140);
    const char* domains[] = {"All domains", "Reflection", "Hook", "Dump", "Build",
                             "Invoke", "Native", "Io", "Internal"};
    int cur = s_selectedDomain + 1;
    if (ImGui::Combo("##dom", &cur, domains, 9)) {
        s_selectedDomain = cur - 1;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &s_autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        ErrorContext::instance().clear();
        s_selected = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        // حفظ إلى cache

        std::string path = yambz_getCacheDir() + "/yambz/logs_export.txt";
        exportLogs(path);

        yambz_notify(("Exported to: " + path).c_str());
    }

    ImGui::PopStyleVar();

    ImGui::Separator();

    // ─── filtered list ───
    std::vector<size_t> visible;
    visible.reserve(records.size());
    for (size_t i = 0; i < records.size(); i++) {
        if (matches(records[i])) visible.push_back(i);
    }

    ImGui::Text("%zu / %zu records", visible.size(), records.size());
    ImGui::SameLine();
    // severity counts
    size_t errCnt = 0, warnCnt = 0;
    for (const auto& r : records) {
        if (r.severity == ErrSeverity::Error || r.severity == ErrSeverity::Fatal) errCnt++;
        if (r.severity == ErrSeverity::Warning) warnCnt++;
    }
    ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.30f, 1.0f), "· %zu errors", errCnt);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "· %zu warnings", warnCnt);

    ImGui::Separator();

    // split
    float listW = ImGui::GetContentRegionAvail().x * 0.5f;

    ImGui::BeginChild("##list", ImVec2(listW, 0), true);
    {
        ImGuiListClipper clip;
        clip.Begin((int) visible.size(), 22.0f);
        while (clip.Step()) {
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                size_t idx = visible[k];
                const auto& r = records[idx];
                ImGui::PushID((int) idx);

                bool isSel = (s_selected == (int) idx);
                ImVec4 col = severityColor(r.severity);

                char ts[32];
                snprintf(ts, sizeof ts, "%llu", (unsigned long long) r.timestampMs);

                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(ts);
                ImGui::PopStyleColor();

                ImGui::SameLine(0, 6);
                ImGui::PushStyleColor(ImGuiCol_Text, isSel ?
                    ImVec4(1.0f, 0.83f, 0.30f, 1.0f) : ImVec4(0.85f, 0.85f, 0.88f, 1.0f));
                std::string preview = r.message;
                if (preview.length() > 100) preview = preview.substr(0, 100) + "…";
                if (ImGui::Selectable((r.operation + ": " + preview).c_str(), isSel)) {
                    s_selected = (int) idx;
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        clip.End();
        if (s_autoScroll && !visible.empty() && s_selected < 0) {
            // scroll to bottom on new messages
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // detail
    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    if (s_selected >= 0 && s_selected < (int) records.size()) {
        const auto& r = records[s_selected];
        Theme::drawSectionHeader("Record");
        ImGui::Text("ID: %llu", (unsigned long long) r.id);
        ImGui::Text("Time: %llu", (unsigned long long) r.timestampMs);
        ImGui::PushStyleColor(ImGuiCol_Text, severityColor(r.severity));
        ImGui::Text("Severity: %s",
            r.severity == ErrSeverity::Info ? "Info" :
            r.severity == ErrSeverity::Warning ? "Warning" :
            r.severity == ErrSeverity::Error ? "Error" : "Fatal");
        ImGui::PopStyleColor();
        ImGui::Text("Domain: %s", domainName(r.domain));
        ImGui::Text("Operation: %s", r.operation.c_str());
        ImGui::Text("Target: %s", r.target.c_str());

        Theme::drawSectionHeader("Message");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
        ImGui::BeginChild("##msg", ImVec2(0, 100), true);
        ImGui::TextWrapped("%s", r.message.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();

        if (!r.stackTrace.empty()) {
            Theme::drawSectionHeader("Stack Trace");
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1.0f));
            ImGui::BeginChild("##stack", ImVec2(0, 0), true);
            ImGui::TextWrapped("%s", r.stackTrace.c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "select a record");
    }
    ImGui::EndChild();
}

} // namespace yambz

// ─── helper: cache dir ───
static std::string g_cacheDir;
void yambz_setCacheDir(const char* d) {
    if (d) g_cacheDir = d;
}
std::string yambz_getCacheDir() {
    return g_cacheDir.empty() ? "/data/local/tmp" : g_cacheDir;
}
void yambz_notify(const char* msg);
