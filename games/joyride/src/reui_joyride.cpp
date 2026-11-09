// See reui_joyride.h. First stage: REUI's editor and HUD layouts drawn over the game at the window's resolution
// (shapes made on the CPU, SVG art, the ImGui fonts); Joy Ride's own textures, fonts and screens come next.
#include "reui_joyride.h"

#include "assets_joyride.h"
#include "log.h"
#include "rex_overlay.h"
#include "uied.h"

#include <rex/logging.h>

#include <imgui.h>

#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>

namespace reui {
void Log(const char* fmt, ...) {   // (core/log.h, for the backends)
    char b[1024];
    va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    REXLOG_INFO("REUI: {}", b);
}
} // namespace reui

namespace reui_joyride {
namespace {

reui_rex::OverlayPtr g_overlay;
bool g_open = false;   // the editor panel (F6)

std::string Dir(const std::filesystem::path& p) {   // (the core's file calls are the ANSI ones)
    std::string s = p.string();
    std::replace(s.begin(), s.end(), '/', '\\');
    if (!s.empty() && s.back() != '\\') s += '\\';
    return s;
}

void Frame() {
    uied::FrameStart();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetStyle().FontScaleMain = std::max(1.f, io.DisplaySize.y / 1080.f);   // (the editor readable at 4K)
    uied::DrawOverlay(0);
    if (g_open) {
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(900.f, io.DisplaySize.x - 80), std::min(1000.f, io.DisplaySize.y - 80)), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("REUI - Joy Ride Turbo (F6 closes)", &g_open)) uied::Panel();
        ImGui::End();
    }
}

} // namespace

bool Create(rex::ui::Window* window, rex::ui::Presenter* presenter, rex::ui::GraphicsProvider* provider,
            const std::filesystem::path& dataDir) {
    if (g_overlay) return true;
    const std::string data = Dir(dataDir);
    CreateDirectoryW(dataDir.wstring().c_str(), nullptr);
    uied::dataDir = data;
    uied::prefabDir = data + "prefabs\\";
    reui_rex::OverlaySetup s;
    s.window = window;
    s.presenter = presenter;
    s.provider = provider;
    s.zOrder = 0;
    s.frame = Frame;
    s.keyDown = [](int vk) {
        if (vk != 0x75) return false;   // F6: the editor
        g_open = !g_open;
        REXLOG_INFO("REUI: editor {}", g_open ? "open" : "closed");
        return true;
    };
    s.wantsInput = [] { return g_open; };
    s.fonts = [] {
        ImGuiIO& io = ImGui::GetIO();
        // the system's Segoe UI for the editor (not shipped: Windows' own); ImGui's default if it isn't there
        if (GetFileAttributesA("C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES)
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.f);
    };
    g_overlay = reui_rex::CreateOverlay(s);
    if (!g_overlay) { REXLOG_ERROR("REUI: overlay could not be made"); return false; }
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(reui_rex::OverlayContext(g_overlay.get()));
    assets_joyride::Install(data);   // vector fonts (REUI\fonts, Windows')
    uied::Init(data, data + "layout.txt", [](const char* line) { REXLOG_INFO("REUI: {}", line); });
    ImGui::SetCurrentContext(prev);
    REXLOG_INFO("REUI: ready ({}) - F6 opens the editor", data);
    return true;
}

void Destroy() {
    if (!g_overlay) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(reui_rex::OverlayContext(g_overlay.get()));
    uied::Release();
    ImGui::SetCurrentContext(prev);
    g_overlay.reset();
}

bool EditorOpen() { return g_open; }
void SetEditorOpen(bool open) { g_open = open; }

} // namespace reui_joyride
