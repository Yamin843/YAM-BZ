#include "Renderer.h"
#include "MainWindow.h"
#include "Notifications.h"
#include "Theme.h"
#include "core/Helpers.h"
#include "../core/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <cmath>

namespace yambz {

Renderer& Renderer::instance() {
    static Renderer r;
    return r;
}

static ImGuiKey androidKey(int code) {
    switch (code) {
        case 4:   return ImGuiKey_Escape;
        case 19:  return ImGuiKey_UpArrow;
        case 20:  return ImGuiKey_DownArrow;
        case 21:  return ImGuiKey_LeftArrow;
        case 22:  return ImGuiKey_RightArrow;
        case 23:  return ImGuiKey_Enter;
        case 61:  return ImGuiKey_Tab;
        case 66:  return ImGuiKey_Enter;
        case 67:  return ImGuiKey_Backspace;
        case 111: return ImGuiKey_Escape;
        case 112: return ImGuiKey_Delete;
        default:  return ImGuiKey_None;
    }
}

void Renderer::onSurfaceCreated() {
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_init.load()) { m_ready.store(true); return; }

    LOGI("Renderer: creating ImGui context");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;

    Theme::apply();

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("ImGui_ImplOpenGL3_Init failed");
        ImGui::DestroyContext();
        return;
    }
    m_backendInit = true;
    m_init.store(true);
    m_ready.store(true);
    LOGI("Renderer: ✓ initialized");
}

void Renderer::onSurfaceChanged(int w, int h) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_w = w; m_h = h;
    if (!m_init.load()) return;
    ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);
}

void Renderer::onDrawFrame(int w, int h) {
    static thread_local bool inFrame = false;
    if (inFrame) return;
    inFrame = true;
    struct _ { bool& f; ~_() { f = false; } } guard{inFrame};

    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_init.load()) return;

    m_w = w; m_h = h;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);

    auto now = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(now.time_since_epoch()).count();
    float dt = (m_lastTime > 0.0) ? (float)(t - m_lastTime) : 1.0f/60.0f;
    if (dt <= 0.0f) dt = 1.0f/60.0f;
    if (dt > 0.25f) dt = 0.25f;
    io.DeltaTime = dt;
    m_lastTime = t;

    ImGui_ImplOpenGL3_NewFrame();

    {
        std::lock_guard<std::mutex> lk2(m_charMu);
        for (auto cp : m_charQueue) io.AddInputCharacter(cp);
        m_charQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lk2(m_scrollMu);
        const float TH = 0.001f;
        if (std::fabs(m_scrollVX) > TH || std::fabs(m_scrollVY) > TH) {
            io.AddMouseWheelEvent(m_scrollVX, m_scrollVY);
            m_scrollVX *= 0.55f;
            m_scrollVY *= 0.55f;
        }
    }

    ImGui::NewFrame();
    MainWindow::instance().draw();

    // FPS counter
    if (yambz_showFps()) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("##fps", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", 1.0f / dt);
        ImGui::End();
    }
    Notifications::instance().draw();

    bool cap = io.WantCaptureMouse || io.WantCaptureMouseUnlessPopupClose;
    m_wantMouse.store(cap);
    m_wantText.store(io.WantTextInput);

    ImGui::Render();

    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Renderer::onTouch(int action, float x, float y, int) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_init.load()) return;
    ImGuiIO& io = ImGui::GetIO();
    switch (action) {
        case 0: io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, true); break;
        case 1: io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, false); break;
        case 2: io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(x, y); break;
        case 3: io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                io.AddMouseButtonEvent(0, false); break;
    }
}

void Renderer::onScroll(float dx, float dy) {
    if (!m_init.load()) return;
    std::lock_guard<std::mutex> lk(m_scrollMu);
    m_scrollVX += dx;
    m_scrollVY += dy;
}

void Renderer::onChar(unsigned int cp) {
    std::lock_guard<std::mutex> lk(m_charMu);
    m_charQueue.push_back(cp);
}

void Renderer::onKey(int code, int action) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_init.load()) return;
    ImGuiKey k = androidKey(code);
    if (k != ImGuiKey_None) ImGui::GetIO().AddKeyEvent(k, action == 0);
}

bool Renderer::wantsAnotherFrame() const {
    if (!m_ready.load()) return false;
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g) {
        if (g->OpenPopupStack.Size > 0) return true;
        if (g->BeginPopupStack.Size > 0) return true;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return true;
    }
    return MainWindow::instance().animating();
}

bool Renderer::wantCaptureMouse() const { return m_wantMouse.load(); }
bool Renderer::wantTextInput() const { return m_wantText.load(); }

} // namespace yambz
