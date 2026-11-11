// See reui_joyride.h. REUI's editor and layouts drawn over the game at the window's resolution (shapes made on the
// CPU, SVG art, vector fonts, the menus' pictures from data.bpk), the Flash menus' screens as layout tokens and the
// F7 debug page of their calls (fe_debug.cpp).
#include "reui_joyride.h"

#include "assets_joyride.h"
#include "fe_debug.h"
#include "flash_motion.h"
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
#include <mutex>
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
bool g_feOpen = false; // the menus / Flash commands page (F7)
std::mutex g_modeLock;
std::string g_modeKey;  // what the port says is being played ("" none)

int ModeIndex() {
    std::string key;
    { std::lock_guard<std::mutex> l(g_modeLock); key = g_modeKey; }
    if (key.empty()) return -1;
    for (size_t i = 0; i < uied::modes.size(); ++i) if (uied::modes[i].key == key) return (int)i;
    return -1;
}

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
    FeFrame();
    uied::DrawOverlay(ModeIndex());
    if (g_feOpen) FeDebugWindow(&g_feOpen);
    if (g_open) {
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(900.f, io.DisplaySize.x - 80), std::min(1000.f, io.DisplaySize.y - 80)), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("REUI - Joy Ride Turbo (F6 closes)", &g_open)) uied::Panel();
        ImGui::End();
    }
}

} // namespace

bool Create(rex::ui::Window* window, rex::ui::Presenter* presenter, rex::ui::GraphicsProvider* provider,
            const std::filesystem::path& dataDir, const std::filesystem::path& gameData) {
    if (g_overlay) return true;
    const std::string data = Dir(dataDir);
    CreateDirectoryW(dataDir.wstring().c_str(), nullptr);
    uied::dataDir = data;
    uied::prefabDir = data + "prefabs\\";
    uied::modes = { {"freeroam", "Free roam"}, {"race", "Racing"}, {"timetrial", "Time trial"},
                    {"stunt", "Stunt park"},   {"tutorial", "Tutorial"}, {"frontend", "Front end"},
                    {"menus", "Menus"} };
    uied::menusMode = 6;
    uied::nfsu2Tools = false;
    uied::tokens = [](const std::string& key, std::string& out) { return FeToken(key, out); };
    FeInit(data);
    reui_rex::OverlaySetup s;
    s.window = window;
    s.presenter = presenter;
    s.provider = provider;
    s.zOrder = 0;
    s.frame = Frame;
    s.keyDown = [](int vk) {
        if (vk == 0x76) { g_feOpen = !g_feOpen; return true; }   // F7: menus / Flash commands
        if (vk == 0x77) { uied::SetAnimMode(!uied::AnimModeOn()); return true; }   // F8: animation mode
        if (vk != 0x75) return false;   // F6: the editor
        g_open = !g_open;
        REXLOG_INFO("REUI: editor {}", g_open ? "open" : "closed");
        return true;
    };
    s.wantsInput = [] { return g_open || g_feOpen || uied::AnimModeOn(); };
    s.shareKeys = [] { return !g_open && !uied::AnimModeOn() && g_feOpen; };   // the F7 page alone: the game keeps the keyboard (not while typing)
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
    assets_joyride::Install(data, gameData.empty() ? std::filesystem::path() : gameData / "data.bpk");
    flash_motion::Install(data + "cache\\movies\\");   // (the picture cache keeps the movies there)
    uied::Init(data, data + "layout.txt", [](const char* line) { REXLOG_INFO("REUI: {}", line); });
    ImGui::SetCurrentContext(prev);
    REXLOG_INFO("REUI: ready ({}) - F6 opens the editor, F7 the menus / Flash commands page, F8 animation mode", data);
    return true;
}

void Destroy() {
    if (!g_overlay) return;
    assets_joyride::Shutdown();
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(reui_rex::OverlayContext(g_overlay.get()));
    uied::Release();
    ImGui::SetCurrentContext(prev);
    g_overlay.reset();
}

bool EditorOpen() { return g_open; }

void SetMode(const std::string& key) {
    std::lock_guard<std::mutex> l(g_modeLock);
    if (key != g_modeKey) REXLOG_INFO("REUI: game mode {}", key.empty() ? "(none)" : key);
    g_modeKey = key;
}
void SetEditorOpen(bool open) { g_open = open; }
void SetAnimSpeed(float speed) { uied::AnimSpeed(speed); }
void SetAnimLive(bool live) { uied::AnimLive(live); }
void AnimKeysTest(float at) { uied::AnimKeysTest(at); }
void AnimUndoTest() { uied::AnimUndoTest(); }
void AnimCopyMotionTest(const std::string& spec) { uied::AnimCopyMotionTest(spec); }
void FromGameTest(const std::string& spec) { uied::FromGameTest(spec); }
void StartAnimMode(bool focusFirst, float ms, bool play, bool bare) {
    uied::SetAnimMode(true);
    if (focusFirst) uied::AnimFocus(0);
    uied::AnimPlayhead(ms, play);
    uied::AnimBare(bare);
}
bool FeDebugOpen() { return g_feOpen; }
void SetFeDebugOpen(bool open) { g_feOpen = open; }

} // namespace reui_joyride
