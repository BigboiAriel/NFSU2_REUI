// See fe_menus.h.
#include "fe_menus.h"
#include "fe_debug.h"
#include "uied.h"

#include <imgui.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace reui_joyride {
namespace {

// The Flash menu screens REUI can drive: the screen's key, its rows, whether up / down wrap round as the movie's own
// do, and its RowControl (the movie's list: setIndex(row) is what it's told before the press).
struct MenuDef { const char* key; int rows; bool wrap; const char* rowControl; };
const MenuDef kMenus[] = {
    { "jra_menus/hangoutmainmenu", 8, false, "_level0.feScreen.mcRowControlArea.mcRowControl" },
};
const MenuDef* Find(const std::string& key) {
    for (auto& m : kMenus) if (key == m.key) return &m;
    return nullptr;
}

std::mutex g_mu;                  // (the game thread's events and REUI's frame)
std::string g_key;                // the driven screen now ("" none)
int g_sel = 0;
std::map<std::string, int> g_last;   // each screen's row when it was left by a pick (the game comes back to it)
std::atomic<bool> g_enabled{true}, g_drive{false};   // allowed (cvar); and a layout draws a driven menu
std::atomic<bool> g_popup{false};  // a Flash popup is up (its buttons on): it has the controller, REUI's screen steps aside
double g_enteredAt = -1;          // (REUI's clock) when the driven screen came up
// presses, for Press clips (menu.press:<row> / menu.press / menu.back): on for a moment after each (the game thread's clock)
int g_pressRow = -1; double g_pressAt = -1e9, g_backAt = -1e9;
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
const double kPressOn = 0.2;      // s a press reads on (a few frames, whatever the frame rate)

void Enter(const std::string& key) {   // (locked)
    if (key == g_key) return;
    g_key = key;
    auto it = g_last.find(key);
    g_sel = it != g_last.end() ? it->second : 0;
    g_enteredAt = -1;   // (REUI's frame times it)
}

} // namespace

void SetMenusEnabled(bool on) { g_enabled = on; }

bool FeMenuInput(const std::string& screenPath, const std::string& code, int controller) {
    if (!g_enabled || !g_drive || g_popup) return false;
    const std::string key = ScreenKey(screenPath);
    const MenuDef* m = Find(key);
    if (!m) return false;
    std::lock_guard<std::mutex> l(g_mu);
    Enter(key);
    if (code == "UP" || code == "DOWN") {
        const int was = g_sel, d = code == "UP" ? -1 : 1;
        g_sel = m->wrap ? (g_sel + d + m->rows) % m->rows : std::max(0, std::min(m->rows - 1, g_sel + d));
        // the movie's own row follows (unseen; it's what a popup over it shows) - and its RowControl plays the move's
        // sound (UI_Up / UI_Down), as when it moves itself
        if (g_sel != was) FeSend(1, std::string(m->rowControl) + ".setIndex", { std::to_string(g_sel) });
        return true;
    }
    if (code == "BACK") { g_backAt = Now(); return false; }   // (the movie's, as before: just noted)
    if (code == "ADVANCE") {   // the movie on our row, then the press: the game does the rest
        g_last[key] = g_sel;
        g_pressRow = g_sel; g_pressAt = Now();
        FeSend(1, std::string(m->rowControl) + ".setIndex", { std::to_string(g_sel) });
        FeSend(1, "_root.inputEventHandler", { "\"ADVANCE\"", std::to_string(controller) });
        return true;
    }
    return false;
}

void FeMenuFrame() {
    g_drive = uied::ShowIfUsed("menu.sel:");
    const std::string& top = FeTopScreen();
    std::lock_guard<std::mutex> l(g_mu);
    if (top == "boot/pressstart") g_last.clear();   // (it resets the game's breadcrumbs: the menus start at the top)
    if (Find(top)) {
        Enter(top);
        if (g_enteredAt < 0) g_enteredAt = ImGui::GetTime();
    } else if (!g_key.empty()) {
        g_key.clear();
    }
}

void FeMenuCall(const std::string& method, const std::string& args) {
    if (method == "UIGestureMediator:setEnabled" && args.find("/mcPopup/mcPopupBtn0") != std::string::npos)
        g_popup = args.find("true") != std::string::npos;
}

bool FeMenuHides(const std::string& screenKey) { return g_popup && g_enabled && g_drive && Find(screenKey); }

bool FeMenuToken(const std::string& key, std::string& out) {
    if (key.rfind("menu.", 0) != 0) return false;
    std::lock_guard<std::mutex> l(g_mu);
    if (key == "menu.press" || key.rfind("menu.press:", 0) == 0) {   // (not tied to the screen: the game moves on at once)
        const bool on = Now() - g_pressAt < kPressOn && (key.size() == 10 || atoi(key.c_str() + 11) == g_pressRow);
        out = on ? "1" : "0"; return true;
    }
    if (key == "menu.back") { out = Now() - g_backAt < kPressOn ? "1" : "0"; return true; }
    const bool on = g_enabled && g_drive && !g_popup && !g_key.empty() && g_key == FeTopScreen();
    // the highlight waits for the screen's own fade-in (0.33 s), then shows with its row
    const bool settled = on && g_enteredAt >= 0 && ImGui::GetTime() - g_enteredAt >= 0.33;
    if (key == "menu.on") { out = on ? "1" : "0"; return true; }
    if (key == "menu.row") { out = on ? std::to_string(g_sel) : ""; return true; }
    if (key.rfind("menu.sel:", 0) == 0) { out = settled && atoi(key.c_str() + 9) == g_sel ? "1" : "0"; return true; }
    if (key.rfind("menu.unsel:", 0) == 0) { out = on && atoi(key.c_str() + 11) != g_sel ? "1" : "0"; return true; }
    return false;
}

} // namespace reui_joyride
