#include "Notifications.h"
#include <imgui.h>

namespace yambz {

Notifications& Notifications::instance() {
    static Notifications n;
    return n;
}

void Notifications::push(const std::string& msg, float duration) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_items.push_back({msg, std::chrono::steady_clock::now(), duration});
    if (m_items.size() > 5) m_items.pop_front();
}

void Notifications::draw() {
    std::lock_guard<std::mutex> lk(m_mu);
    auto now = std::chrono::steady_clock::now();
    while (!m_items.empty()) {
        auto el = std::chrono::duration<float>(now - m_items.front().born).count();
        if (el >= m_items.front().duration) m_items.pop_front();
        else break;
    }
    if (m_items.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float y = 60;
    for (auto& it : m_items) {
        float el = std::chrono::duration<float>(now - it.born).count();
        float rem = it.duration - el;
        float alpha = rem < 0.6f ? rem / 0.6f : 1.0f;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowBgAlpha(0.92f * alpha);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.83f, 0.63f, 0.09f, alpha));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

        ImGui::Begin("##notif", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(it.text.c_str());
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        y += 50;
    }
}

} // namespace yambz
