// See fe_menus.h.
#include "fe_menus.h"
#include "fe_debug.h"
#include "uied.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
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

// ---- a REUI menu page standing in for the screen (its "stands in for"): the screen's up / down / left / right / select
// go to the page (one a frame, each a press and a release); back stays the movie's. The page's tiles run the game's own
// calls ("fe:" commands, below).
std::atomic<bool> g_pageOn{false};   // a page is open for the screen on top (the game thread asks)
std::string g_pageScreen;            // (locked) that screen
std::deque<std::string> g_events;    // (locked) its presses, for REUI's frame
bool g_ownOpen = false;              // (REUI's thread) the open page is ours (opened for a screen)
bool g_pulse = false;                // (REUI's thread) last frame held a press: this one lets go

// ---- the game's strings: <REUI>\cache\locale\<lang>.loc (data.bpk's data\locale, kept by the picture cache) -
// "8COL", the count (u32 LE) at 20, then (djb2(key), offset) big-endian pairs sorted by key, then the UTF-8 strings
std::string g_dataDir, g_lang = "en";
std::map<uint32_t, std::string> g_loc;
int g_locTries = 0; double g_locAt = -100;
uint32_t Djb2(const std::string& s) { uint32_t h = 5381; for (unsigned char c : s) h = h * 33 + c; return h; }
uint32_t Be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void LocLoad() {   // (until it's there: the cache is written in the background on the first start)
    if (!g_loc.empty() || g_locTries > 60 || ImGui::GetTime() - g_locAt < 2.0) return;
    ++g_locTries; g_locAt = ImGui::GetTime();
    FILE* f = fopen((g_dataDir + "cache\\locale\\" + g_lang + ".loc").c_str(), "rb");
    if (!f) return;
    std::vector<unsigned char> d;
    unsigned char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    if (d.size() < 24 || memcmp(d.data(), "8COL", 4) != 0) return;
    const uint32_t count = d[20] | d[21] << 8 | d[22] << 16 | (uint32_t)d[23] << 24;
    const size_t strs = 24 + (size_t)count * 8;
    if (count > 200000 || strs > d.size()) return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t key = Be32(&d[24 + i * 8]), off = Be32(&d[28 + i * 8]);
        if (strs + off >= d.size()) continue;
        const char* p = (const char*)&d[strs + off];
        g_loc[key] = std::string(p, strnlen(p, d.size() - strs - off));
    }
}

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
    if (g_enabled && g_pageOn && !g_popup) {   // a REUI page stands in for the screen: its presses are the page's
        const std::string key = ScreenKey(screenPath);
        std::lock_guard<std::mutex> l(g_mu);
        if (key == g_pageScreen) {
            if (code == "UP" || code == "DOWN" || code == "LEFT" || code == "RIGHT" || code == "ADVANCE") {
                if (g_events.size() < 16) g_events.push_back(code);
                return true;
            }
            if (code == "BACK") { g_backAt = Now(); return false; }   // (the movie's own back)
        }
    }
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

namespace {
// "fe:" a tile's game command: calls split at ';' (outside quotes), each "Module:method(args)" - the movie asking the game
// (ExternalInterface) - or "@function(args)" - the game calling the screen's movie (_level0.feScreen.function; a path
// from "_" as it is). {tile} is the picked tile's number.
std::vector<std::string> SplitOutside(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur; bool q = false;
    for (char c : s) {
        if (c == '"') q = !q;
        if (c == sep && !q) { out.push_back(cur); cur.clear(); } else cur += c;
    }
    out.push_back(cur);
    return out;
}
std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}
void RunCommand(std::string cmd, int tile) {
    for (size_t k; (k = cmd.find("{tile}")) != std::string::npos;) cmd.replace(k, 6, std::to_string(std::max(0, tile)));
    for (std::string call : SplitOutside(cmd, ';')) {
        call = Trim(call);
        if (call.empty()) continue;
        const size_t a = call.find('('), b = call.rfind(')');
        std::string name = Trim(call.substr(0, a)), in = a != std::string::npos && b != std::string::npos && b > a ? call.substr(a + 1, b - a - 1) : "";
        std::vector<std::string> args;
        if (!Trim(in).empty()) for (auto& x : SplitOutside(in, ',')) args.push_back(Trim(x));
        if (name.empty()) continue;
        if (name[0] == '@') {
            name = name.substr(1);
            FeSend(1, name[0] == '_' ? name : "_level0.feScreen." + name, args);
        } else FeSend(0, name, args);
    }
}

} // namespace

void FeMenusInit(const std::string& dataDir) {
    g_dataDir = dataDir;
    for (auto& m : kMenus) uied::standInKeys.push_back(m.key);
    uied::menuAction = [](const std::string& a) {
        if (a.compare(0, 3, "fe:") != 0) return false;
        const int tile = uied::MenuSelectedTile();
        {
            std::lock_guard<std::mutex> l(g_mu);
            g_pressRow = tile; g_pressAt = Now();   // (menu.press / menu.press:<tile>)
            if (!g_pageScreen.empty()) g_last[g_pageScreen] = std::max(0, tile);   // (back on this screen: the same tile)
        }
        RunCommand(a.substr(3), tile);
        return true;
    };
    uied::menuSound = [](int s) {   // the page's moves: the game's own sounds (its row lists play UI_Up / UI_Down)
        if (!g_pageOn) return;
        const char* n = s == uied::SND_UP ? "UI_Up" : s == uied::SND_DOWN ? "UI_Down"
                      : (s == uied::SND_LEFT || s == uied::SND_RIGHT) ? "UI_LeftRight" : nullptr;
        if (n) FeSend(0, "Audio:playSound", { std::string("\"") + n + "\"" });
    };
}

bool LocText(const std::string& key, std::string& out) {
    LocLoad();
    auto it = g_loc.find(Djb2(key));
    if (it == g_loc.end()) return false;
    out = it->second;
    return true;
}

namespace {
void PageFrame(const std::string& top) {   // (REUI's thread) the page standing in for the screen on top
    const std::string page = g_enabled && !g_popup ? uied::MenuPageFor(top) : std::string();
    if (!page.empty()) {
        if (!uied::MenuIsOpen() || uied::MenuTopPage() != page) {
            if (uied::MenuOpen(page)) {
                g_ownOpen = true; g_pulse = false;
                int row = 0;
                { std::lock_guard<std::mutex> l(g_mu); g_events.clear(); auto it = g_last.find(top); if (it != g_last.end()) row = it->second; }
                uied::MenuSelectTile(row);
            }
        }
    } else if (g_ownOpen) {
        if (uied::MenuIsOpen()) uied::MenuClose();
        g_ownOpen = false;
    }
    const bool on = !page.empty() && uied::MenuIsOpen() && uied::MenuTopPage() == page;
    { std::lock_guard<std::mutex> l(g_mu); g_pageScreen = on ? top : std::string(); }
    g_pageOn = on;
    if (!on) return;
    uied::MenuInput in;
    if (g_pulse) g_pulse = false;   // (a press, then a frame let go: each of the game's events is one press)
    else {
        std::string ev;
        { std::lock_guard<std::mutex> l(g_mu); if (!g_events.empty()) { ev = g_events.front(); g_events.pop_front(); } }
        if (!ev.empty()) {
            in.up = ev == "UP"; in.down = ev == "DOWN"; in.left = ev == "LEFT"; in.right = ev == "RIGHT"; in.accept = ev == "ADVANCE";
            g_pulse = true;
        }
    }
    uied::MenuFrame(in, ImGui::GetIO().DeltaTime);
}
} // namespace

void FeMenuFrame() {
    g_drive = uied::ShowIfUsed("menu.sel:");
    const std::string& top = FeTopScreen();
    PageFrame(top);
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
    if (key.rfind("loc:", 0) == 0) { if (!LocText(key.substr(4), out)) out = key.substr(4); return true; }   // the game's own text
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
