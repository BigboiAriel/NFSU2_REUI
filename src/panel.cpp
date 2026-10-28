// The editor's ImGui window (PanelKey, F6 by default) and the frame everything is drawn in.
#include "panel.h"

#include "config.h"
#include "host.h"
#include "log.h"
#include "editor/uied.h"

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace reui {

static bool g_ready = false, g_open = false, g_keyWasDown = false;
static HWND g_hwnd = nullptr;
static WNDPROC g_origWndProc = nullptr;
static IDirect3DDevice9* g_dev = nullptr;

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (g_ready && g_open) ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
    return CallWindowProcA(g_origWndProc, h, msg, w, l);
}

void PanelInit(HWND hwnd, IDirect3DDevice9* device) {
    g_hwnd = hwnd;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(device);
    g_origWndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
    g_dev = device;
    g_ready = true;
    HostInit();
    Log("panel ready (hwnd=%p, key=0x%02X)", hwnd, g_config.panelKey);
}
void PanelBeforeReset() { if (g_ready) ImGui_ImplDX9_InvalidateDeviceObjects(); }
void PanelAfterReset() { if (g_ready) ImGui_ImplDX9_CreateDeviceObjects(); }
void PanelShutdown() {
    if (!g_ready) return;
    HostRelease();
    if (g_origWndProc) SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_ready = false;
}
bool PanelOpen() { return g_open; }
void PanelSetOpen(bool open) { if (g_open != open) { g_open = open; Log("panel %s", open ? "opened" : "closed"); } }

// scripts\NFSU2_REUI.quit: closes the game (so a new build can be put in place)
static void PollQuitFile() {
    static DWORD next = 0;
    DWORD now = GetTickCount();
    if (now < next) return;
    next = now + 1000;
    std::string q = g_scriptsDir + "NFSU2_REUI.quit";
    if (GetFileAttributesA(q.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(q.c_str());
    if (uied::Unsaved()) { Log("NFSU2_REUI.quit found: not closing - the UI editor has unsaved changes"); return; }
    Log("NFSU2_REUI.quit found: closing the game");
    if (g_hwnd) PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
}

void PanelPollKey() {
    if (!g_ready) return;
    PollQuitFile();
    bool focused = GetForegroundWindow() == g_hwnd;
    bool down = focused && (GetAsyncKeyState(g_config.panelKey) & 0x8000) != 0;
    if (down && !g_keyWasDown) PanelSetOpen(!g_open);
    g_keyWasDown = down;
    HostTick(focused);
}

bool PanelWantsFrame() { return g_ready && (g_open || HostWantsFrames()); }

void PanelFrame() {
    if (!PanelWantsFrame()) return;
    HostBeginFrame(g_dev);
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = g_open;
    if (g_open) {   // the game may own the mouse through DirectInput: read it directly
        POINT pt;
        if (GetCursorPos(&pt) && ScreenToClient(g_hwnd, &pt)) io.AddMousePosEvent((float)pt.x, (float)pt.y);
    }
    ImGui::NewFrame();
    if (g_open) {
        ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("NFSU2 REUI", &g_open)) HostPanel();
        ImGui::End();
    }
    HostDraw();
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

} // namespace reui
