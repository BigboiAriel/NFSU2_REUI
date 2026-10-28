// The exported API (reui_api.h): other mods' settings, values, icons and menu driving, handed to the editor.
#include "reui_api.h"

#include "log.h"
#include "editor/uied.h"

#include <windows.h>
#include <string>
#include <vector>

namespace reui {
std::vector<REUI_TokenFn> g_tokenProviders;   // asked after REUI's own values (host.cpp)
bool g_menuDriverExternal = false;            // another mod drives the menus (host.cpp leaves its menu key off)
std::vector<std::pair<REUI_MapMarkerFn, void*>> g_mapMarkers;   // minimap marker providers (host.cpp asks them)
}
using namespace reui;

#define EXPORT extern "C" __declspec(dllexport)

static std::vector<std::string> g_iconFamilies, g_iconKeys;   // (copies)

EXPORT int REUI_Version() { return REUI_API_VERSION; }

EXPORT void REUI_AddTokenProvider(REUI_TokenFn fn) { if (fn) g_tokenProviders.push_back(fn); }

EXPORT void REUI_AddSetting(const REUI_Setting* in) {
    if (!in || !in->key) return;
    REUI_Setting c = *in;   // callbacks + ctx, kept by value
    uied::Setting s;
    s.key = in->key; s.label = in->label ? in->label : in->key; s.kind = in->kind;
    s.min = in->min; s.max = in->max; s.step = in->step;
    if (c.get) s.get = [c] { return c.get(c.ctx); };
    if (c.set) s.set = [c](float v) { c.set(c.ctx, v); };
    if (c.choiceCount && c.choice) s.choices = [c] {
        std::vector<std::string> v;
        int n = c.choiceCount(c.ctx);
        for (int i = 0; i < n; ++i) { const char* t = c.choice(c.ctx, i); v.push_back(t ? t : ""); }
        return v;
    };
    if (c.format) s.format = [c](float v) { const char* t = c.format(c.ctx, v); return std::string(t ? t : ""); };
    if (c.run) s.run = [c] { c.run(c.ctx); };
    if (c.icon) s.icon = [c] { return c.icon(c.ctx); };
    if (c.current) s.current = [c] { const char* t = c.current(c.ctx); return std::string(t ? t : ""); };
    if (c.beginCapture) s.beginCapture = [c] { c.beginCapture(c.ctx); };
    if (c.capture) s.capture = [c](double dt) { return c.capture(c.ctx, dt); };
    for (auto& o : uied::settings) if (o.key == s.key) { o = s; return; }   // registered again: replaced
    uied::settings.push_back(s);
}

EXPORT void REUI_SetSound(void (*fn)(int)) { uied::menuSound = fn ? std::function<void(int)>(fn) : std::function<void(int)>(); }
EXPORT void REUI_SetMenuChanged(void (*fn)(void)) { uied::menuChanged = fn ? std::function<void()>(fn) : std::function<void()>(); }

EXPORT void REUI_SetIcons(int families, const char* const* familyNames, int count, const char* const* keys, REUI_IconTexFn tex,
                          REUI_IconLabelFn label, REUI_IconGameTexFn forGameTexture, void (*reload)(void)) {
    uied::iconFamilies.clear(); uied::iconKeys.clear();
    for (int i = 0; i < families; ++i) uied::iconFamilies.push_back(familyNames[i] ? familyNames[i] : "");
    for (int i = 0; i < count; ++i) uied::iconKeys.push_back(keys[i] ? keys[i] : "");
    uied::iconTexture = tex ? std::function<IDirect3DTexture9*(int, int, int)>([tex](int f, int id, int px) { return tex(uied::Device(), f, id, px); })
                            : std::function<IDirect3DTexture9*(int, int, int)>();
    uied::iconLabel = label ? std::function<std::string(int, int)>([label](int f, int id) { const char* t = label(f, id); return std::string(t ? t : ""); })
                            : std::function<std::string(int, int)>();
    uied::iconForGameTexture = forGameTexture ? std::function<int(const std::string&)>([forGameTexture](const std::string& n) { return forGameTexture(n.c_str()); })
                                              : std::function<int(const std::string&)>();
    uied::iconReload = reload ? std::function<void()>(reload) : std::function<void()>();
    reui::Log("api: %d icon families, %d icons", families, count);
}

EXPORT void REUI_SetMenuDriver(int external) { g_menuDriverExternal = external != 0; reui::Log("api: menus driven by %s", external ? "another mod" : "REUI"); }
EXPORT int REUI_HasMenuPages() { return !uied::MenuPages().empty(); }
EXPORT int REUI_MenuOpen(const char* page) { return uied::MenuOpen(page ? page : "") ? 1 : 0; }
EXPORT void REUI_MenuClose() { uied::MenuClose(); }
EXPORT int REUI_MenuIsOpen() { return uied::MenuIsOpen() ? 1 : 0; }
EXPORT void REUI_AddFlag(const char* key, const char* label, const char* tooltip, int group, int hiddenWhenMissing, REUI_FlagFn hideNow, void* ctx) {
    if (!key || !*key) return;
    uied::AddFlag(key, label ? label : key, tooltip ? tooltip : "", group, hiddenWhenMissing != 0,
                  hideNow ? std::function<bool()>([hideNow, ctx] { return hideNow(ctx) != 0; }) : std::function<bool()>());
    reui::Log("api: element flag \"%s\"", key);
}
EXPORT void REUI_EditorState(int* testMode, int* boardRows, int* boardFollow) {
    if (testMode) *testMode = uied::testMode;
    if (boardRows) *boardRows = uied::boardRows;
    if (boardFollow) *boardFollow = uied::boardFollow;
}
EXPORT void REUI_MenuFrame(int up, int down, int left, int right, int accept, int back, double dt) {
    uied::MenuInput in; in.up = up; in.down = down; in.left = left; in.right = right; in.accept = accept; in.back = back;
    uied::MenuFrame(in, dt);
}
EXPORT void REUI_AddMapMarkers(REUI_MapMarkerFn fn, void* ctx) {
    if (!fn) return;
    for (auto& p : g_mapMarkers) if (p.first == fn && p.second == ctx) return;
    g_mapMarkers.push_back({fn, ctx});
    reui::Log("api: minimap markers from another mod");
}
EXPORT int REUI_MinimapShown() { return uied::MinimapShown() ? 1 : 0; }
EXPORT void REUI_SetColorToken(const char* name, const char* label, unsigned argb) {
    if (!name || !*name) return;
    float c[4] = {((argb >> 16) & 255) / 255.f, ((argb >> 8) & 255) / 255.f, (argb & 255) / 255.f, (argb >> 24) / 255.f};
    uied::AddColorToken(name, label ? label : "", c);
    reui::Log("api: colour token \"%s\"", name);
}
