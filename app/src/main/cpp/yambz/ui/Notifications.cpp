#include "Notifications.h"
#include "Theme.h"
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

bool Notifications::active() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return !m_items.empty();
}

void Notifications::draw() {
    std::lock_guard<std::mutex> lk(m_mu);
    auto now = std::chrono::steady_clock::now();

    // cleanup expired
    while (!m_items.empty()) {
        auto elapsed = std::chrono::duration<float>(now - m_items.front().born).count();
        if (elapsed >= m_items.front().duration)
            m_items.pop_front();
        else break;
    }

    if (m_items.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float y = 60;
    for (auto& it : m_items) {
        float elapsed = std::chrono::duration<float>(now - it.born).count();
        float remaining = it.duration - elapsed;
        float alpha = remaining < 0.6f ? remaining / 0.6f : 1.0f;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, y),
                                ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowBgAlpha(0.92f * alpha);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.83f, 0.63f, 0.09f, alpha));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 12));

        ImGui::Begin("##notif", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.55f, alpha));
        ImGui::TextUnformatted(it.text.c_str());
        ImGui::PopStyleColor();
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);

        y += 60;
    }
}

} // namespace yambz
