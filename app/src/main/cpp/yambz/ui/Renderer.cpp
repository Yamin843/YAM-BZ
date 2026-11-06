#include "Renderer.h"
#include "MainWindow.h"
#include "TraceDialog.h"
#include "DetailView.h"
#include "../call/HeapPickerUI.h"
#include "Theme.h"
#include "SettingsTab.h"
#include "Notifications.h"
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

// ── android keycode → ImGuiKey ─────────────────────────────
static ImGuiKey mapKey(int code) {
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
        case 122: return ImGuiKey_Home;
        case 123: return ImGuiKey_End;
        case 92:  return ImGuiKey_PageUp;
        case 93:  return ImGuiKey_PageDown;
        default:  return ImGuiKey_None;
    }
}

void Renderer::onSurfaceCreated() {
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_init.load()) {
        LOGI("Renderer: already initialized");
        m_ready.store(true);
        return;
    }

    LOGI("Renderer: creating ImGui context");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    Theme::apply();
    SettingsTab::instance().load();  // تحميل من الملف

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("Renderer: ImGui_ImplOpenGL3_Init failed");
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
    m_w = w;
    m_h = h;
    if (!m_init.load()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    LOGI("Renderer: surface %dx%d", w, h);
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

    // delta time
    auto now = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(now.time_since_epoch()).count();
    float dt = (m_lastTime > 0.0) ? (float)(t - m_lastTime) : 1.0f/60.0f;
    if (dt <= 0.0f) dt = 1.0f/60.0f;
    if (dt > 0.25f) dt = 0.25f;
    io.DeltaTime = dt;
    m_lastTime = t;

    ImGui_ImplOpenGL3_NewFrame();
    drainInputQueues();

    ImGui::NewFrame();
    MainWindow::instance().draw();

    // FPS counter (اختياري)
    extern bool yambz_showFps();
    if (yambz_showFps()) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("##fps", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", 1.0f / dt);
        ImGui::End();
    }
    TraceDialog::draw();
    drawDetailView();
    drawHeapPicker();
    Notifications::instance().draw();

    {
        bool cap = io.WantCaptureMouse || io.WantCaptureMouseUnlessPopupClose;
        m_wantMouse.store(cap, std::memory_order_relaxed);
    }
    m_wantText.store(io.WantTextInput, std::memory_order_relaxed);

    ImGui::Render();

    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Renderer::drainInputQueues() {
    ImGuiIO& io = ImGui::GetIO();
    // chars
    {
        std::lock_guard<std::mutex> lk(m_charMu);
        for (unsigned int cp : m_charQueue) io.AddInputCharacter(cp);
        m_charQueue.clear();
    }
    // scroll inertia
    {
        std::lock_guard<std::mutex> lk(m_scrollMu);
        const float kThreshold = 0.001f;
        if (std::fabs(m_scrollVelX) > kThreshold || std::fabs(m_scrollVelY) > kThreshold) {
            io.AddMouseWheelEvent(m_scrollVelX, m_scrollVelY);
            m_scrollVelX *= 0.55f;
            m_scrollVelY *= 0.55f;
            if (std::fabs(m_scrollVelX) < kThreshold) m_scrollVelX = 0.0f;
            if (std::fabs(m_scrollVelY) < kThreshold) m_scrollVelY = 0.0f;
        }
    }
}

void Renderer::onTouch(int action, float x, float y, int /*pid*/) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_init.load()) return;
    ImGuiIO& io = ImGui::GetIO();
    switch (action) {
        case 0:  // DOWN
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
            break;
        case 1:  // UP
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            break;
        case 2:  // MOVE
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            break;
        case 3:  // CANCEL
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            io.AddMouseButtonEvent(0, false);
            break;
        case 5:  // POINTER_DOWN
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
            break;
        case 6:  // POINTER_UP
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            break;
        default: break;
    }
}

void Renderer::onScroll(float dx, float dy) {
    if (!m_init.load()) return;
    std::lock_guard<std::mutex> lk(m_scrollMu);
    m_scrollVelX += dx;
    m_scrollVelY += dy;
}

void Renderer::onChar(unsigned int cp) {
    std::lock_guard<std::mutex> lk(m_charMu);
    m_charQueue.push_back(cp);
}

void Renderer::onKey(int keyCode, int action) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_init.load()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiKey k = mapKey(keyCode);
    if (k != ImGuiKey_None) io.AddKeyEvent(k, action == 0);
}

bool Renderer::wantsAnotherFrame() const {
    if (!m_ready.load()) return false;
    // popup / modal / notification نشط → إطار آخر
    if (Notifications::instance().active()) return true;
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g) {
        if (g->OpenPopupStack.Size > 0) return true;
        if (g->BeginPopupStack.Size > 0) return true;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return true;
    }
    if (MainWindow::instance().animating()) return true;
    return false;
}

bool Renderer::wantsCaptureMouse() const { return m_wantMouse.load(); }
bool Renderer::wantsTextInput() const { return m_wantText.load(); }

void Renderer::shutdown() {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_init.load()) return;
    if (m_backendInit) {
        ImGui_ImplOpenGL3_Shutdown();
        m_backendInit = false;
    }
    ImGui::DestroyContext();
    m_init.store(false);
    m_ready.store(false);
    LOGI("Renderer: shutdown");
}

} // namespace yambz
