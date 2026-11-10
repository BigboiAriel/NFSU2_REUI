// See reui_joyride.h (FeBridge): the Flash menus as REUI sees them.
//   * tokens for layouts: {screen} = the top screen's key ("menus/helpandoptionsmenu"), screen:<key> = 1 while that
//     screen is up (showif=screen:jra_menus/hangoutmainmenu), {screens} = all of them;
//   * the debug page (F7): the screens per level and which of the game's screens have been seen; every call between
//     the pages and the game as it happens; the commands the pages can send (fe_commands.txt, made by
//     tools/fe/fe_commands.py from the game's movies) with a sender that sends any of them, as the page or as the game.
#include "fe_debug.h"
#include "reui_joyride.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace reui_joyride {
namespace {

FeBridge g_bridge;
std::vector<FeScreen> g_screens;          // this frame's
std::string g_top;                        // the top screen's key
std::deque<FeCall> g_log;                 // kept here (the port keeps fewer)
uint64_t g_next = 0;
std::map<std::string, int> g_seenCalls;   // method -> times (page -> game)
std::set<std::string> g_seenScreens;

struct Cmd { std::string name, kinds, movies; int args = 0; };
std::vector<Cmd> g_cmds;                  // fe_commands.txt
std::vector<std::string> g_allScreens;    // its screen lines
bool g_cmdsLoaded = false;
std::string g_cmdsFile;

// sender
int g_dir = 0;
char g_method[256] = "";
char g_args[512] = "";
char g_filter[128] = "";
bool g_showPage = true, g_showGame = true, g_pause = false, g_hideAudio = true;
std::string g_lastSent;

bool Leaderboards(const std::string& key) { return key.find("leaderboard") != std::string::npos; }

void LoadCommands() {
    g_cmdsLoaded = true;
    std::ifstream f(g_cmdsFile);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> c;
        std::stringstream ss(line);
        std::string x;
        while (std::getline(ss, x, '\t')) c.push_back(x);
        if (c.size() >= 2 && c[0] == "screen") {
            std::string k = c[1];
            if (k.rfind("screens/", 0) == 0) k = k.substr(8);
            g_allScreens.push_back(k);
        } else if (c.size() >= 2 && c[0] == "ei") {
            Cmd m;
            m.name = c[1];
            m.args = c.size() > 2 ? atoi(c[2].c_str()) : 0;
            m.kinds = c.size() > 3 ? c[3] : "";
            m.movies = c.size() > 4 ? c[4] : "";
            if (!m.name.empty() && m.name[0] != '(') g_cmds.push_back(m);
        }
    }
}

std::string Module(const std::string& name) {
    const size_t c = name.find(':');
    return c == std::string::npos ? "(other)" : name.substr(0, c);
}

void Use(int dir, const std::string& method, const std::string& args) {
    g_dir = dir;
    snprintf(g_method, sizeof g_method, "%s", method.c_str());
    snprintf(g_args, sizeof g_args, "%s", args.c_str());
}

// "a, "b, c", 3" -> a / "b, c" / 3 (commas inside quotes stay)
std::vector<std::string> SplitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (char ch : s) {
        if (ch == '"') q = !q;
        if (ch == ',' && !q) { out.push_back(cur); cur.clear(); continue; }
        cur += ch;
    }
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    for (auto& a : out) {
        while (!a.empty() && a.front() == ' ') a.erase(a.begin());
        while (!a.empty() && a.back() == ' ') a.pop_back();
    }
    return out;
}

void Sender() {
    ImGui::TextDisabled("Send a call");
    ImGui::RadioButton("as the page (ExternalInterface.call -> game)", &g_dir, 0);
    ImGui::SameLine();
    ImGui::RadioButton("as the game (Invoke -> movie)", &g_dir, 1);
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##method", g_dir == 0 ? "Module:method" : "_root.function", g_method, sizeof g_method);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##args", "args: \"text\", 12, true, null (comma separated)", g_args, sizeof g_args);
    ImGui::SameLine();
    const bool can = g_bridge.send && g_method[0];
    if (!can) ImGui::BeginDisabled();
    if (ImGui::Button("Send", ImVec2(80, 0))) {
        g_bridge.send(g_dir, g_method, SplitArgs(g_args));
        g_lastSent = std::string(g_method) + "(" + g_args + ")";
    }
    if (!can) ImGui::EndDisabled();
    if (!g_lastSent.empty()) ImGui::TextDisabled("Sent: %s (it shows in the log, marked [sent])", g_lastSent.c_str());
}

void ScreensTab() {
    ImGui::Text("Up now (the current screen, then the root movie's levels):");
    if (g_screens.empty()) ImGui::TextDisabled("  none (the game has not loaded a screen yet)");
    if (ImGui::BeginTable("##levels", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("showif key");
        ImGui::TableSetupColumn("Movie", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (auto& s : g_screens) {
            const std::string k = ScreenKey(s.path);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (s.level < 0) ImGui::TextUnformatted("screen"); else ImGui::Text("%d", s.level);
            ImGui::TableNextColumn();
            ImGui::TextColored(k == g_top ? ImVec4(1, .85f, .3f, 1) : ImVec4(1, 1, 1, 1), "screen:%s", k.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.path.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Text("Top screen ({screen}): %s", g_top.empty() ? "-" : g_top.c_str());
    ImGui::Separator();
    if (g_allScreens.empty()) {
        ImGui::TextDisabled("The list of the game's screens comes from %s (tools/fe/fe_commands.py).", g_cmdsFile.c_str());
        ImGui::TextDisabled("Seen this session: %d", (int)g_seenScreens.size());
        for (auto& k : g_seenScreens) ImGui::BulletText("%s", k.c_str());
        return;
    }
    int menus = 0, seen = 0;
    for (auto& k : g_allScreens) {
        if (Leaderboards(k) || k.rfind("debug/", 0) == 0) continue;
        ++menus;
        seen += g_seenScreens.count(k) ? 1 : 0;
    }
    ImGui::Text("The game's screens: %d seen of %d this session (leaderboards and debug screens not counted)", seen, menus);
    if (ImGui::BeginTable("##all", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, 0))) {
        for (auto& k : g_allScreens) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool up = std::any_of(g_screens.begin(), g_screens.end(), [&](const FeScreen& s) { return ScreenKey(s.path) == k; });
            const bool skip = Leaderboards(k) || k.rfind("debug/", 0) == 0;
            if (up) ImGui::TextColored(ImVec4(1, .85f, .3f, 1), "%s", k.c_str());
            else if (g_seenScreens.count(k)) ImGui::TextColored(ImVec4(.5f, 1, .5f, 1), "%s", k.c_str());
            else ImGui::TextDisabled("%s", k.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", up ? "up now" : g_seenScreens.count(k) ? "seen" : skip ? "(not counted)" : "");
        }
        ImGui::EndTable();
    }
}

void LogTab() {
    ImGui::Checkbox("page -> game", &g_showPage); ImGui::SameLine();
    ImGui::Checkbox("game -> page", &g_showGame); ImGui::SameLine();
    ImGui::Checkbox("hide Audio:", &g_hideAudio); ImGui::SameLine();
    ImGui::Checkbox("pause", &g_pause); ImGui::SameLine();
    if (ImGui::Button("Clear")) g_log.clear();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter", g_filter, sizeof g_filter);
    ImGui::TextDisabled("Click a call to put it in the sender.");
    std::vector<const FeCall*> rows;
    for (auto& c : g_log) {
        if ((c.dir == 0 && !g_showPage) || (c.dir == 1 && !g_showGame)) continue;
        if (g_hideAudio && c.method.rfind("Audio:", 0) == 0) continue;
        if (g_filter[0] && (c.method + c.args).find(g_filter) == std::string::npos) continue;
        rows.push_back(&c);
    }
    if (ImGui::BeginChild("##log", ImVec2(0, 0), 0, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const FeCall& c = *rows[i];
                char line[1400];
                snprintf(line, sizeof line, "%8.2f  %s %s(%s)%s##%d", c.t, c.dir ? "<-" : "->", c.method.c_str(),
                         c.args.c_str(), c.sent ? "  [sent]" : "", i);
                ImGui::PushStyleColor(ImGuiCol_Text, c.dir ? ImVec4(.6f, .85f, 1, 1) : ImVec4(1, .9f, .6f, 1));
                if (ImGui::Selectable(line, false)) {
                    std::string a = c.args;
                    Use(c.dir, c.method, a);
                }
                ImGui::PopStyleColor();
            }
        }
        if (!g_pause && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void CommandsTab() {
    if (g_cmds.empty()) {
        ImGui::TextWrapped("No command list: %s is missing. Make it with tools/fe/fe_commands.py (from the game's data.bpk). "
                           "Commands seen while playing are listed below anyway.", g_cmdsFile.c_str());
    } else {
        ImGui::TextDisabled("%d commands the pages send to the game (from their ActionScript). Arg kinds: s text, n number, "
                            "b true/false, v a variable. Green: seen this session.", (int)g_cmds.size());
    }
    ImGui::SetNextItemWidth(-1);
    static char filter[128] = "";
    ImGui::InputTextWithHint("##cfilter", "filter", filter, sizeof filter);
    if (ImGui::BeginChild("##cmds")) {
        std::map<std::string, std::vector<const Cmd*>> byModule;
        for (auto& c : g_cmds) {
            if (filter[0] && (c.name + c.movies).find(filter) == std::string::npos) continue;
            byModule[Module(c.name)].push_back(&c);
        }
        for (auto& [mod, list] : byModule) {
            char hdr[128];
            snprintf(hdr, sizeof hdr, "%s (%d)", mod.c_str(), (int)list.size());
            if (!ImGui::TreeNode(hdr)) continue;
            for (const Cmd* c : list) {
                ImGui::PushID(c->name.c_str());
                if (ImGui::SmallButton("Use")) {
                    std::string a;
                    for (int i = 0; i < c->args; ++i) {
                        const char k = i < (int)c->kinds.size() ? c->kinds[i] : 'v';
                        a += (i ? ", " : "") + std::string(k == 's' ? "\"\"" : k == 'n' ? "0" : k == 'b' ? "false" : "?");
                    }
                    Use(0, c->name, a);
                }
                ImGui::SameLine();
                auto it = g_seenCalls.find(c->name);
                const ImVec4 col = it != g_seenCalls.end() ? ImVec4(.5f, 1, .5f, 1) : ImVec4(1, 1, 1, 1);
                ImGui::TextColored(col, "%s(%s)", c->name.c_str(), c->kinds.c_str());
                if (it != g_seenCalls.end()) { ImGui::SameLine(); ImGui::TextDisabled("x%d", it->second); }
                if (ImGui::IsItemHovered() || ImGui::IsItemHovered()) ImGui::SetTooltip("Sent by: %s", c->movies.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        // seen while playing but not in the list (built names, other movies)
        std::vector<std::string> extra;
        for (auto& [name, n] : g_seenCalls)
            if (std::none_of(g_cmds.begin(), g_cmds.end(), [&](const Cmd& c) { return c.name == name; })) extra.push_back(name);
        if (!extra.empty() && ImGui::TreeNode("Seen while playing, not in the list")) {
            for (auto& name : extra) {
                ImGui::PushID(name.c_str());
                if (ImGui::SmallButton("Use")) Use(0, name, "");
                ImGui::SameLine();
                ImGui::Text("%s  x%d", name.c_str(), g_seenCalls[name]);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
}

} // namespace

std::string ScreenKey(const std::string& path) {
    std::string k = path;
    std::replace(k.begin(), k.end(), '\\', '/');
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return (char)tolower(c); });
    if (k.rfind("screens/", 0) == 0) k = k.substr(8);
    const size_t dot = k.rfind('.');
    if (dot != std::string::npos && k.find('/', dot) == std::string::npos) k.resize(dot);
    return k;
}

void SetFeBridge(const FeBridge& b) { g_bridge = b; }

void FeInit(const std::string& dataDir) { g_cmdsFile = dataDir + "fe_commands.txt"; }

const std::string& FeTopScreen() { return g_top; }

bool FeToken(const std::string& key, std::string& out) {
    if (key == "screen") { out = g_top; return true; }
    if (key == "screens") {
        out.clear();
        for (auto& s : g_screens) out += (out.empty() ? "" : ",") + ScreenKey(s.path);
        return true;
    }
    if (key.rfind("screen:", 0) == 0) {
        const std::string want = key.substr(7);
        out = std::any_of(g_screens.begin(), g_screens.end(), [&](const FeScreen& s) { return ScreenKey(s.path) == want; }) ? "1" : "0";
        return true;
    }
    return false;
}

void FeFrame() {
    if (g_bridge.screens) {
        g_screens = g_bridge.screens();
        g_top.clear();
        if (!g_screens.empty() && g_screens.front().level < 0) g_top = ScreenKey(g_screens.front().path);   // the current one
        for (auto it = g_screens.rbegin(); g_top.empty() && it != g_screens.rend(); ++it) {   // else the highest level
            const std::string k = ScreenKey(it->path);
            if (k.rfind("controls/", 0) == 0 || k.rfind("asclasses/", 0) == 0 || k.empty()) continue;
            g_top = k;
            break;
        }
        for (auto& s : g_screens) g_seenScreens.insert(ScreenKey(s.path));
    }
    if (g_bridge.calls) {
        uint64_t next = g_next;
        for (auto& c : g_bridge.calls(g_next, next)) {
            if (c.dir == 0) g_seenCalls[c.method]++;
            if (!g_pause) g_log.push_back(c);
        }
        g_next = next;
        while (g_log.size() > 5000) g_log.pop_front();
    }
}

void FeDebugWindow(bool* open) {
    if (!g_cmdsLoaded) LoadCommands();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(1000.f, io.DisplaySize.x * 0.48f), std::min(900.f, io.DisplaySize.y - 80)),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("REUI - Menus and Flash commands (F7 closes)", open)) {
        ImGui::Text("Screen: %s", g_top.empty() ? "-" : g_top.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(layouts: showif=screen:%s, text {screen})", g_top.empty() ? "<key>" : g_top.c_str());
        Sender();
        ImGui::Separator();
        if (ImGui::BeginTabBar("##fe")) {
            if (ImGui::BeginTabItem("Screens")) { ScreensTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Log")) { LogTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Commands")) { CommandsTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace reui_joyride
