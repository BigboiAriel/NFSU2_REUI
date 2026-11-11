// UI editor - see uied.h. Copied from nfsu2-multiplayer (src/uied.cpp); keep the two in step when changing it.
//
// The game's own textures and fonts come from its connection (core/assets.h; NFSU2: games/nfsu2/src/assets_nfsu2.cpp).
#include "uied.h"
#include "vecart.h"
#include "gfx.h"
#include "assets.h"
#include "shapes.h"
#include "shapes_gpu.h"
#include <windows.h>
#include <shellapi.h>
#include "imgui.h"
#if IMGUI_VERSION_NUM >= 19110   // (renamed in ImGui 1.91.1: NFSU2 builds with 1.90.4, Joy Ride's port with 1.92)
#define REUI_CHILD_BORDERS ImGuiChildFlags_Borders
#else
#define REUI_CHILD_BORDERS ImGuiChildFlags_Border
#endif
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <ctime>

namespace assets { Source source; }   // (set by the game before uied::Init)

namespace uied {
std::function<bool(const std::string&, std::string&)> tokens;
int boardRows = 4; bool boardFollow = true; bool testMode = false; bool menuMode = false;
std::function<gfx::Tex(const std::string&, float*, float*)> liveTexture;
std::function<bool(gfx::Tex)> textureAlphaOk;
std::function<bool(unsigned, GameObject&)> gameObject;
std::function<bool(MapView&)> mapView;
std::function<bool(MapView&)> worldView;
std::function<std::vector<std::pair<int, std::string>>()> trackNames;
std::vector<std::string> iconFamilies, iconKeys;
std::function<gfx::Tex(int, int, int)> iconTexture;
std::function<std::string(int, int)> iconLabel;
std::function<int(const std::string&)> iconForGameTexture;
std::function<void()> iconReload;
// "icon:<family folder | auto>:<key>" -> family (-1 = auto) and id; "icon:bind:<game event>" (FENG_LTRIGGER...): the
// controller's icon for what that event is bound to now (the menu driver's {bindicon.<event>}; LT / RT if it gives none)
static bool IconRef(const std::string& tex, int& fam, int& id) {
    if (tex.compare(0, 5, "icon:") != 0) return false;
    size_t c = tex.find(':', 5); if (c == std::string::npos) return false;
    std::string f = tex.substr(5, c - 5), k = tex.substr(c + 1);
    fam = -2; id = -1;
    if (f == "bind") {
        std::string v;
        if (tokens && tokens("bindicon." + k, v)) k = v;
        else k = k == "FENG_LTRIGGER" ? "LT" : k == "FENG_RTRIGGER" ? "RT" : "";
        f = "auto";
    }
    if (f == "auto") fam = -1; else for (size_t i = 0; i < iconFamilies.size(); ++i) if (iconFamilies[i] == f) fam = (int)i;
    for (size_t i = 0; i < iconKeys.size(); ++i) if (iconKeys[i] == k) id = (int)i;
    return fam > -2 && id >= 0;
}
static std::string IconName(int fam, int id) {   // what the list shows
    std::string l = iconLabel ? iconLabel(fam < 0 ? 0 : fam, id) : std::string();
    if (fam < 0) return "icon " + (id >= 0 && id < (int)iconKeys.size() ? iconKeys[id] : std::string("?")) + " (follows the controller)";
    return "icon " + l + " (" + iconFamilies[fam] + ")";
}

static std::string g_dir, g_layoutPath;
static void (*g_log)(const char*) = nullptr;
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    g_log(b);
}

// ---------------------------------------------------------------- index
using assets::Tex;
using assets::Glyph;
using assets::Font;
static std::vector<Tex> g_tex;
static std::vector<Font> g_fonts;
static std::vector<std::string> g_packs;   // "source: pack" for the filter
static std::atomic<int> g_state{0};        // 0 not scanned, 1 scanning, 2 ready
static std::string g_scanMsg;

static void Scan() {   // (a background thread)
    assets::Index idx;
    if (assets::source.scan) assets::source.scan(idx);
    std::vector<Tex>& tex = idx.tex; std::vector<Font>& fonts = idx.fonts;
    // fonts' textures
    for (auto& f : fonts) {
        if (f.tex >= 0) continue;
        std::string up = f.name; for (auto& c : up) c = (char)toupper((unsigned char)c);
        for (size_t i = 0; i < tex.size(); ++i) if (tex[i].name == up) { f.tex = (int)i; break; }
    }
    std::vector<std::string> packs;
    for (auto& t : tex) { std::string p = t.source + ": " + t.pack; if (std::find(packs.begin(), packs.end(), p) == packs.end()) packs.push_back(p); }
    g_tex.swap(tex); g_fonts.swap(fonts); g_packs.swap(packs);
    char b[160]; snprintf(b, sizeof b, "%d textures in %d packs, %d fonts", (int)g_tex.size(), (int)g_packs.size(), (int)g_fonts.size());
    g_scanMsg = b; Log("ui editor: %s", b);
    g_state = 2;
}

// ---------------------------------------------------------------- textures
static int g_loadsThisFrame = 0;
static gfx::Tex TexOf(int i, bool mayLoad = true) {
    if (i < 0 || i >= (int)g_tex.size()) return nullptr;
    Tex& t = g_tex[i];
    t.usedAt = (uint32_t)GetTickCount();
    if (t.tex || t.failed || !mayLoad || !gfx::Ready()) return t.tex;
    if (g_loadsThisFrame >= 12) return nullptr;   // spread the work over frames
    ++g_loadsThisFrame;
    gfx::Format fmt = gfx::BGRA8; std::vector<uint8_t> px; int pitch = 0;
    if (!assets::source.pixels || !assets::source.pixels(t, fmt, px, pitch)) { t.failed = true; return nullptr; }
    gfx::Tex tx = gfx::Create(t.w, t.h, fmt, px.data(), pitch);
    if (!tx) { t.failed = true; return nullptr; }
    t.tex = tx;
    return tx;
}
// A texture's white silhouette (its alpha, every colour white): drawn over it with normal blending, a clip's
// colour + (brightness flashes) on any backend - additive blending isn't on every one (rex). DXT: both colour
// endpoints of every block made white (alpha untouched; DXT1's one-bit alpha too).
static std::map<int, gfx::Tex> g_white;
static gfx::Tex WhiteOf(int i) {
    if (i < 0 || i >= (int)g_tex.size() || !gfx::Ready()) return nullptr;
    auto it = g_white.find(i);
    if (it != g_white.end()) return it->second;
    gfx::Tex tx = nullptr;
    gfx::Format fmt = gfx::BGRA8; std::vector<uint8_t> px; int pitch = 0;
    if (assets::source.pixels && assets::source.pixels(g_tex[i], fmt, px, pitch)) {
        if (fmt == gfx::BGRA8) { for (size_t k = 0; k + 3 < px.size(); k += 4) px[k] = px[k + 1] = px[k + 2] = 255; }
        else {
            const size_t bs = fmt == gfx::DXT1 ? 8 : 16, at = fmt == gfx::DXT1 ? 0 : 8;
            for (size_t k = 0; k + bs <= px.size(); k += bs) for (int j = 0; j < 4; ++j) px[k + at + j] = 0xFF;
        }
        tx = gfx::Create(g_tex[i].w, g_tex[i].h, fmt, px.data(), pitch);
    }
    g_white[i] = tx;   // (null: tried, not again)
    return tx;
}
static int FindTex(const std::string& name, const std::string& pack) {
    int any = -1;
    for (size_t i = 0; i < g_tex.size(); ++i) if (g_tex[i].name == name) { if (pack.empty() || g_tex[i].pack == pack) return (int)i; if (any < 0) any = (int)i; }
    return any;
}
static int FindFont(const std::string& name) { for (size_t i = 0; i < g_fonts.size(); ++i) if (g_fonts[i].name == name) return (int)i; return -1; }
void Release() {
    for (auto& t : g_tex) if (t.tex) { gfx::Release(t.tex); t.tex = nullptr; }
    for (auto& w : g_white) if (w.second) gfx::Release(w.second);
    g_white.clear();
    vecart::Release();
    shapes::Release();
    shapes::GpuRelease();
    gfx::ReleaseAll();
}
static void Trim() {   // keep at most ~400 textures made; drop the least recently used
    int made = 0; for (auto& t : g_tex) if (t.tex) ++made;
    if (made <= 400) return;
    std::vector<int> idx; for (size_t i = 0; i < g_tex.size(); ++i) if (g_tex[i].tex) idx.push_back((int)i);
    std::sort(idx.begin(), idx.end(), [](int a, int b) { return g_tex[a].usedAt < g_tex[b].usedAt; });
    for (int k = 0; k < made - 300; ++k) { gfx::ReleaseLater(g_tex[idx[k]].tex); g_tex[idx[k]].tex = nullptr; }   // (may be drawn this frame)
}

// ---------------------------------------------------------------- text
// Kerning as the game does it (0x6F45F0, used by the width / draw code 0x50BCE0): a glyph's list (kerning table
// +4 + ks*4, kc entries of u16 char, s8 adjust, u8 this char) holds the pairs that END with it, keyed by the
// character BEFORE it; the adjustment is added before the glyph is placed.
static int Kern(const Font& f, const Glyph& g, uint16_t prev) {
    if (!g.kc || f.kern.empty()) return 0;
    for (int j = 0; j < g.kc; ++j) {
        size_t o = 4 + ((size_t)g.ks + j) * 4;
        if (o + 4 > f.kern.size()) break;
        uint16_t n; memcpy(&n, &f.kern[o], 2);
        if (n == prev) return (int8_t)f.kern[o + 2];
    }
    return 0;
}
// A vector font's ImFont, loaded into the current ImGui context's atlas the first time it's needed (ImGui 1.92 draws
// it at any size from the file; older ImGui at the size loaded, scaled).
static ImFont* VecFont(const Font& f) {
    if (f.file.empty()) return nullptr;
    Font& m = const_cast<Font&>(f);
    if (!m.im) {
        ImFontConfig fc; fc.OversampleH = 2;
        m.im = ImGui::GetIO().Fonts->AddFontFromFileTTF(f.file.c_str(), (float)std::max(8, f.line), &fc);
        if (!m.im) { m.file.clear(); Log("ui editor: font %s couldn't be read", f.source.c_str()); }
    }
    return (ImFont*)m.im;
}
static float TextWidth(const Font& f, const std::string& s) {
    if (ImFont* im = VecFont(f)) return im->CalcTextSizeA((float)f.line, FLT_MAX, 0.f, s.c_str()).x;
    float x = 0, best = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') { best = std::max(best, x); x = 0; continue; }
        auto it = f.g.find(c); if (it == f.g.end()) it = f.g.find('?'); if (it == f.g.end()) continue;
        x += it->second.adv + (i > 0 && s[i - 1] != '\n' ? Kern(f, it->second, (unsigned char)s[i - 1]) : 0);
    }
    return std::max(best, x);
}
static int Lines(const std::string& s) { return 1 + (int)std::count(s.begin(), s.end(), '\n'); }
// s broken at spaces into lines no wider than maxW (font pixels); its own line breaks kept, a word wider than the line
// left whole
static std::string WrapText(const Font& f, const std::string& s, float maxW) {
    if (maxW <= 0) return s;
    std::string out, line;
    size_t i = 0;
    while (i <= s.size()) {
        size_t e = s.find_first_of(" \n", i); if (e == std::string::npos) e = s.size();
        std::string word = s.substr(i, e - i);
        std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && TextWidth(f, tryLine) > maxW) { out += line + "\n"; line = word; }
        else line = tryLine;
        if (e < s.size() && s[e] == '\n') { out += line + "\n"; line.clear(); }
        i = e + 1;
    }
    return out + line;
}
// draw s with its top-left at p, glyphs scaled by k (screen pixels per font pixel)
static void DrawText(ImDrawList* dl, const Font& f, ImVec2 p, float k, ImU32 col, const std::string& s, int align, float boxW) {
    if (ImFont* im = VecFont(f)) {   // a vector font: line by line at its size on screen
        size_t start = 0; float y = p.y, size = f.line * k;
        while (start <= s.size()) {
            size_t e = s.find('\n', start); if (e == std::string::npos) e = s.size();
            std::string line = s.substr(start, e - start);
            float lw = TextWidth(f, line) * k;
            float x = p.x + (align == 1 ? (boxW - lw) * 0.5f : align == 2 ? boxW - lw : 0.f);
            dl->AddText(im, size, ImVec2(x, y), col, line.c_str());
            y += size;
            start = e + 1;
        }
        return;
    }
    gfx::Tex tx = TexOf(f.tex); if (!tx) return;
    const Tex& t = g_tex[f.tex];
    float iw = 1.f / t.w, ih = 1.f / t.h;
    size_t start = 0; float y = p.y;
    while (start <= s.size()) {
        size_t e = s.find('\n', start); if (e == std::string::npos) e = s.size();
        std::string line = s.substr(start, e - start);
        float lw = TextWidth(f, line) * k;
        float x = p.x + (align == 1 ? (boxW - lw) * 0.5f : align == 2 ? boxW - lw : 0.f);
        for (size_t i = 0; i < line.size(); ++i) {
            unsigned char c = (unsigned char)line[i];
            auto it = f.g.find(c); if (it == f.g.end()) it = f.g.find('?'); if (it == f.g.end()) continue;
            const Glyph& g = it->second;
            if (i > 0) x += Kern(f, g, (unsigned char)line[i - 1]) * k;
            if (g.w && g.h) {
                ImVec2 a(x + g.xo * k, y + g.yo * k), b(a.x + g.w * k, a.y + g.h * k);
                dl->AddImage(gfx::Id(tx), a, b, ImVec2(g.u * iw, g.v * ih), ImVec2((g.u + g.w) * iw, (g.v + g.h) * ih), col);
            }
            x += g.adv * k;
        }
        y += f.line * k;
        start = e + 1;
    }
}
static std::string g_titleNow;   // {page.title}: the header text of the menu being drawn (ElemText sets it; "" = none)
static std::string g_tokGroup;   // the group of the element being drawn (the menu {page.prompt...} values are of)
static bool PageToken(const std::string& key, std::string& v);   // {page.prompt...}: the menu's button prompts
static std::string TitleFor(const std::string& group);           // {page.title}: the header text of a group's menu
// {key} -> live value. In an element bound to leaderboard row N, {name} {pos} ... mean that row's ({rN.name} ...).
static std::string Expand(const std::string& s, int row = 0) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{') {
            size_t e = s.find('}', i);
            if (e != std::string::npos) {
                std::string key = s.substr(i + 1, e - i - 1), v;
                if (key == "page.title") v = g_titleNow;
                else if (key.compare(0, 11, "page.prompt") == 0) PageToken(key, v);
                else if (key == "clock") { time_t now = time(nullptr); tm* lt = localtime(&now); char b[16]; strftime(b, sizeof b, "%H:%M", lt); v = b; }
                else if (row > 0 && key.find('.') == std::string::npos && tokens && tokens("r" + std::to_string(row) + "." + key, v)) {}
                else if (!(tokens && tokens(key, v))) v = "{" + key + "}";
                out += v; i = e; continue;
            }
        }
        out += s[i];
    }
    return out;
}

// ---------------------------------------------------------------- overlay
enum { E_IMAGE = 0, E_TEXT = 1, E_SHAPE = 2 };
// Gradients as the editor keeps them: stops with a colour, or a colour token (its colour now, times the stop's
// opacity); a gradient can instead use a gradient token's stops (tok). Resolved to shapes::Gradient when drawn.
struct UStop { float t = 0; float c[4] = {1, 1, 1, 1}; std::string tok; };
struct UGrad { int mode = 0; float angle = 0, depth = 0; std::vector<UStop> stops; std::string tok; };
// A cut gap of 0 cuts 0.1 units inside the cutter (closes the hairline seam an exact cut leaves); the gap shown / saved
// is counted from there.
constexpr float kCutZero = -0.1f;
// An element's animation clips (uied_anim.inc: clips of tracks of eased keys), owned by it and copied with it
// (duplicates, prefabs, undo snapshots). Null: none.
namespace anim { struct ElemAnim; }
struct AnimBox {
    anim::ElemAnim* p = nullptr;
    AnimBox() = default;
    AnimBox(const AnimBox& o);
    AnimBox(AnimBox&& o) noexcept : p(o.p) { o.p = nullptr; }
    AnimBox& operator=(const AnimBox& o);
    AnimBox& operator=(AnimBox&& o) noexcept;
    ~AnimBox();
};
struct Elem {
    int type = E_IMAGE; std::string name = "element", tex, pack, font, text = "Text";
    int anchor = 0;                       // 0..8: top-left, top, top-right, left, centre, right, bottom-left, bottom, bottom-right
    float x = 20, y = 20, w = 128, h = 64, scale = 1.f;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    float rot = 0;                        // images: degrees clockwise about the centre
    int layer = 0;                        // draw order: higher layers on top; same layer: later in the list on top
    float col[4] = { 1, 1, 1, 1 };
    int align = 0; bool visible = true;
    int row = 0;                          // > 0: bound to leaderboard row N (hidden when there's no such row)
    std::string group;                    // elements in a group move / show / duplicate together
    bool hiMine = false; float hiCol[4] = { 202 / 255.f, 244 / 255.f, 138 / 255.f, 1.f };   // the game's SetPlayerColor green   // colour when that row is you
    int mode = 0;                         // (only used while loading)
    int texIdx = -2, fontIdx = -2;        // resolved lazily (-2: not yet)
    std::string role;                     // menus: what it does in its row / page (uied_menu.inc)
    bool selOnly = false;                 // menus: shown on the selected row only
    bool wrap = false;                    // text: broken into lines to fit its box width (a game screen's long message)
    bool swapAnim = false;                // text: a change of its text animates as the game's help bar ticker does (the
                                          // name / cash: Fade_Out the old, Fade_In the new - SwapText)
    bool afterLabel = false;              // menus, a grid's piece: moves with the end of its tile's label text (a binding's icon)
    int tileKinds = 0;                    // menus, a grid's piece: the tiles it shows on (1 buttons, 2 sliders, 4 pickers, 8 bindings; 0 all)
    std::string kit;                      // (loading only: an older widget-kit template piece - left out)
    // live-value rules: showIf = a value key ("!key": the opposite) - shown only while it's set (not blank / "0");
    // bind = a value key driving the element: bindKind 1 fill left to right, 2 fill bottom to top (images: cut, not
    // squashed), 3 rotation (images; rotMin..rotMax degrees), 4 reveal, 5 / 6 move across / up (rotMin..rotMax units);
    // the value is mapped from bindMin..bindMax
    std::string showIf, bind;
    int bindKind = 0;
    float bindMin = 0, bindMax = 100, rotMin = 0, rotMax = 270;
    float cutLo = 0, cutHi = 1;           // fills: the part of the image the value fills (0 = empty sits here, 1 = full)
    float pivX = 0, pivY = 0;             // images: the point turned about, from the centre (units; e.g. a needle's hub)
    // follow: a game HUD object (FEng name hash) this image copies every frame - its place (by the anchor's screen
    // edge, x / y added), size (times scale), turn (rot added), uv and visibility; its texture unless one is set
    // here; colours multiply. circle: drawn clipped to the circle inside its box (a minimap).
    unsigned follow = 0;
    bool circle = false;
    bool additive = false;                // images: added onto what's under them (glows; black = nothing)
    std::string tint;                     // a value key giving "R G B" (0-255) that multiplies the colour (the car's HUD paints)
    int anim = 0;                         // 1 pulse (grows and fades, ~1 s), 2 blink (on / off, ~0.5 s), 3 keyframes, 4 bounce
    float bounceScale = 1.2f, bounceMs = 300.f, bounceGlow = 0.18f, bounceAlpha = 1.f;   // bounce: grows to this and back,
                                          // once, over bounceMs; dimmed by bounceGlow at its ends, full at its peak (the stock
                                          // menus' arrows); its opacity times bounceAlpha at the peak (the map's LT / RT: 1.7)
    std::string keys;                     // anim 3: "period ms;t sx sy a;t sx sy a;..." - size and alpha multipliers, linear, looping
    AnimBox an;                           // animation mode's clips (In plays when it shows, then Idle; the rest: planned)
    double shownSince = -1;               // (running) when its show-only-while value last came on: animations start there
    std::string playOn;                   // a value key: its animation (re)starts each time it comes on (a button press: bindheld.<event>)
    bool playWas = false;                 // (running) that value last frame
    std::vector<std::string> flags;       // other mods' flags (AddFlag), by key
    // minimap: drawn as the track map around the car (the host's mapView), round, with the game's icons and other
    // mods' markers on it; mapZoom multiplies the game's zoom, mapBlip the icons' size; mapPlayer: the player's icon
    bool minimap = false, mapPlayer = true;
    bool worldMap = false;   // drawn as the world map (the whole map, this box): see DrawWorldMap
    float mapZoom = 1, mapBlip = 1;
    // colour tokens: the colour (and my-row colour) taken from a named token every frame, times an opacity of its own;
    // col / hiCol then hold the resolved colour (saved too, so a build without tokens still shows it)
    std::string colTok, hiTok;
    float colTokA = 1, hiTokA = 1;
    // shapes (E_SHAPE): a rounded rectangle - corner radii (units: top-left, top-right, bottom-right, bottom-left),
    // a stroke (inside its edge, units; 0 = none) in col, a fill in fillCol (its own token). cut: draws nothing itself
    // and is subtracted from cutTarget (an element name; "group:<group>" = every shape in that group and the groups in it;
    // "" = every shape in its own group), grown by cutGap (negative: shrunk)
    float rad[4] = {12, 12, 12, 12}; bool radLink = true;
    float stroke = 2;
    float fillCol[4] = {0, 0, 0, 1};
    std::string fillTok; float fillTokA = 1;
    bool cut = false; float cutGap = 0; std::string cutTarget;
    bool merge = false;                   // merge: draws nothing itself and is added to cutTarget (the same choices), grown by
                                          // cutGap - one outline, one stroke, its target's fill / gradients over them all

    int strokeAlign = 0;                  // 0 inside the edge, 1 centred on it, 2 outside
    UGrad strokeGrad, fillGrad;           // gradients (shapes.h modes): with one, its colours replace the colour (alpha still fades)
};
static float Clamp01f(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static const char* TypeKey(int t) { return t == 1 ? "text" : t == 2 ? "shape" : "image"; }
struct FlagDef { std::string key, label, tip; int group = 0; bool hideMissing = false, live = false; std::function<bool()> hide; };
static std::vector<FlagDef> g_flagDefs;   // registered ones (live) and ones known from the layout file
static FlagDef* FlagOf(const std::string& k) { for (auto& f : g_flagDefs) if (f.key == k) return &f; return nullptr; }
void AddFlag(const std::string& key, const std::string& label, const std::string& tooltip, int group, bool hiddenWhenMissing, std::function<bool()> hide) {
    FlagDef* f = FlagOf(key); if (!f) { g_flagDefs.push_back(FlagDef()); f = &g_flagDefs.back(); }
    f->key = key; f->label = label; f->tip = tooltip; f->group = group; f->hideMissing = hiddenWhenMissing; f->live = true; f->hide = hide;
}
// "replace:<id>" flags: the stock HUD piece an element stands in for (the host hides it while the element shows)
struct ReplaceTarget { std::string id, label, tip; double seen = -1; };
static std::vector<ReplaceTarget> g_replace;
static ReplaceTarget* ReplaceOf(const std::string& id) { for (auto& r : g_replace) if (r.id == id) return &r; return nullptr; }
void AddReplaceTarget(const std::string& id, const std::string& label, const std::string& tooltip) {
    ReplaceTarget* r = ReplaceOf(id); if (!r) { g_replace.push_back(ReplaceTarget()); r = &g_replace.back(); }
    r->id = id; r->label = label; r->tip = tooltip;
}
bool ReplaceWanted(const std::string& id, double within) {
    ReplaceTarget* r = ReplaceOf(id);
    return r && r->seen >= 0 && ImGui::GetCurrentContext() && ImGui::GetTime() - r->seen <= within;
}
static void ReplaceSeen(const Elem& e) {   // drawn for real this frame: its replace flags count
    for (auto& k : e.flags)
        if (k.compare(0, 8, "replace:") == 0) if (ReplaceTarget* r = ReplaceOf(k.substr(8))) r->seen = ImGui::GetTime();
}
static bool HasFlag(const Elem& e, const std::string& k) { return std::find(e.flags.begin(), e.flags.end(), k) != e.flags.end(); }
static void SetFlag(Elem& e, const FlagDef& f, bool on) {
    e.flags.erase(std::remove(e.flags.begin(), e.flags.end(), f.key), e.flags.end());
    if (!on) return;
    if (f.group > 0) for (auto& o : g_flagDefs) if (o.group == f.group) e.flags.erase(std::remove(e.flags.begin(), e.flags.end(), o.key), e.flags.end());
    e.flags.push_back(f.key);
}
static bool FlagsHide(const Elem& e) {   // any of its flags says hidden now
    for (auto& k : e.flags) { FlagDef* f = FlagOf(k); if (f && (f->live ? (f->hide && f->hide()) : f->hideMissing)) return true; }
    return false;
}

std::vector<ModeDef> modes = { {"freeroam", "Free roam"}, {"race", "Racing"}, {"drag", "Drag racing"},
                               {"drift", "Drift racing"}, {"outrun", "Outrun"}, {"menus", "Menus"} };
int menusMode = MENUS;
bool nfsu2Tools = true;
static std::vector<Elem> g_layout[kMaxModes];   // one overlay per game mode
static int g_editMode = 0, g_nowMode = -1;
static int NumModes() { return std::max(1, std::min((int)modes.size(), kMaxModes)); }
static const char* ModeKey(int m) { return m >= 0 && m < (int)modes.size() ? modes[m].key.c_str() : "mode"; }
static const char* ModeName(int m) { return m >= 0 && m < (int)modes.size() ? modes[m].name.c_str() : "?"; }
static bool IsMenus(int m) { return menusMode >= 0 && m == menusMode; }
// the menu pages live in the Menus workspace (UniPad's own copy: its single layout)
static std::vector<Elem>& MenuElems() { return menuMode || menusMode < 0 || menusMode >= NumModes() ? g_layout[0] : g_layout[menusMode]; }
static std::vector<Elem>& Elems() { return g_layout[g_editMode]; }
static int g_sel = -1;
static std::string g_selGroup;            // a group picked in the list (then g_sel = -1)
static std::vector<ImVec2> g_groupOrig;   // members' positions when a group drag started
// Groups nest: a group is a path, "Race HUD/Leaderboard" is the group Leaderboard inside Race HUD. An element is in
// group G when its group is G or any group inside G.
static bool InGroup(const std::string& eg, const std::string& g) {
    return !g.empty() && (eg == g || (eg.size() > g.size() && eg.compare(0, g.size(), g) == 0 && eg[g.size()] == '/'));
}
static bool PickedOf(int i);   // picked, on its own or in a picked group
static std::string GroupParent(const std::string& g) { size_t k = g.rfind('/'); return k == std::string::npos ? "" : g.substr(0, k); }
static std::string GroupLeaf(const std::string& g) { size_t k = g.rfind('/'); return k == std::string::npos ? g : g.substr(k + 1); }
static std::string CleanGroup(std::string g) {   // no empty parts: "/a//b/" -> "a/b"
    std::string o, part;
    for (size_t i = 0; i <= g.size(); ++i) {
        if (i == g.size() || g[i] == '/') { if (!part.empty()) { if (!o.empty()) o += '/'; o += part; } part.clear(); }
        else part += g[i];
    }
    return o;
}
// g_dirty: unsaved changes. Setting it counts an edit (gen), which is how undo knows something changed.
struct DirtyFlag {
    bool v = false; unsigned gen = 0;
    DirtyFlag& operator=(bool b) { if (b) ++gen; v = b; return *this; }
    operator bool() const { return v; }
};
static bool g_show = true, g_edit = false, g_onlyWorld = false;
static DirtyFlag g_dirty;
// the live flags over a set of elements (one element, a group's members, a multi-pick): one compact button that
// names the flags set and opens a checklist
static void FlagBoxes(std::vector<Elem*> els, const char* idSuffix) {
    if (els.empty()) return;
    std::string sum;
    int live = 0;
    for (auto& f : g_flagDefs) {
        if (!f.live) continue;
        ++live;
        bool all = true, any = false; for (Elem* e : els) { bool h = HasFlag(*e, f.key); all &= h; any |= h; }
        if (any) { if (!sum.empty()) sum += ", "; sum += f.label + (all ? "" : " (some)"); }
    }
    for (auto& r : g_replace) {
        std::string k = "replace:" + r.id;
        bool all = true, any = false; for (Elem* e : els) { bool h = HasFlag(*e, k); all &= h; any |= h; }
        if (any) { if (!sum.empty()) sum += ", "; sum += "replaces " + r.label + (all ? "" : " (some)"); }
    }
    if (!live && g_replace.empty()) return;
    ImGui::SameLine();
    std::string pop = std::string("flags") + idSuffix;
    if (ImGui::Button(("Flags: " + (sum.empty() ? std::string("none") : sum) + "###flb" + idSuffix).c_str())) ImGui::OpenPopup(pop.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("When it's hidden: in a wreck, until the green light, online / offline (from the multiplayer mod)...");
    if (ImGui::BeginPopup(pop.c_str())) {
        for (auto& f : g_flagDefs) {
            if (!f.live) continue;
            bool all = true; for (Elem* e : els) all &= HasFlag(*e, f.key);
            if (ImGui::Checkbox(f.label.c_str(), &all)) { for (Elem* e : els) SetFlag(*e, f, all); g_dirty = true; }
            if (!f.tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.tip.c_str());
        }
        if (!g_replace.empty()) {
            ImGui::SeparatorText("Replace stock HUD");
            ImGui::TextDisabled("While this shows, the game's own piece is hidden.");
            for (auto& r : g_replace) {
                std::string k = "replace:" + r.id;
                bool all = true; for (Elem* e : els) all &= HasFlag(*e, k);
                if (ImGui::Checkbox(("replace " + r.label + "##rp" + r.id).c_str(), &all)) {
                    for (Elem* e : els) { e->flags.erase(std::remove(e->flags.begin(), e->flags.end(), k), e->flags.end()); if (all) e->flags.push_back(k); }
                    g_dirty = true;
                }
                if (!r.tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.tip.c_str());
            }
        }
        ImGui::EndPopup();
    }
}

// more than one element picked (Ctrl / Shift + click); g_sel stays the last one clicked
static std::vector<int> g_multi;
static bool InMulti(int i) { return std::find(g_multi.begin(), g_multi.end(), i) != g_multi.end(); }
static void MultiToggle(int i) { auto it = std::find(g_multi.begin(), g_multi.end(), i); if (it != g_multi.end()) g_multi.erase(it); else g_multi.push_back(i); }
static std::vector<std::string> g_multiG;   // groups picked along with them (as groups)
static bool InMultiG(const std::string& g) { return std::find(g_multiG.begin(), g_multiG.end(), g) != g_multiG.end(); }
static void MultiToggleG(const std::string& g) { auto it = std::find(g_multiG.begin(), g_multiG.end(), g); if (it != g_multiG.end()) g_multiG.erase(it); else g_multiG.push_back(g); }
static size_t PickCount() { return g_multi.size() + g_multiG.size(); }
static void ClearMulti() { g_multi.clear(); g_multiG.clear(); }
static bool PickedOf(int i) {
    if (InMulti(i)) return true;
    if (i < 0 || i >= (int)Elems().size()) return false;
    for (auto& g : g_multiG) if (InGroup(Elems()[i].group, g)) return true;
    return false;
}
static std::vector<int> PickedElems() {   // every element picked (on its own or in a picked group), in list order
    std::vector<int> v; for (int i = 0; i < (int)Elems().size(); ++i) if (PickedOf(i)) v.push_back(i); return v;
}
static float g_listH = 230.f;   // height of the element list and properties boxes (the bar under them)
static bool g_hiSel = true;              // green outline around the selected element / group on screen
static bool g_panelOpen = false;          // the tab was drawn this frame
static int g_dragging = -1, g_dragMode = 0; static ImVec2 g_dragStart, g_dragOrig, g_dragOrigWH; static float g_dragOrigScale = 1.f;

static const char* kAnchors[] = { "top left", "top", "top right", "left", "centre", "right", "bottom left", "bottom", "bottom right" };
struct Elem; static std::string ElemText(const Elem& e);   // (a text's text as shown: its tokens, a header's menu title)
static float Unit() { ImVec2 ds = ImGui::GetIO().DisplaySize; return ds.y / 480.f; }   // 1 unit = 1/480 of the screen height
static ImVec2 ElemSize(Elem& e) {
    if (e.type == E_TEXT) {
        if (e.fontIdx == -2) e.fontIdx = FindFont(e.font);
        if (e.fontIdx < 0) return ImVec2(40, 12);
        const Font& f = g_fonts[e.fontIdx]; std::string s = ElemText(e);
        if (e.wrap && e.w > 0 && e.scale > 0) s = WrapText(f, s, e.w / e.scale);
        return ImVec2(std::max(e.w, TextWidth(f, s) * e.scale), Lines(s) * f.line * e.scale);
    }
    return ImVec2(e.w, e.h);
}
// a followed game object's box on screen (false: not on screen now). 640x480 game units put by the anchor's edge:
// left / centre / right columns keep their distance from that edge (the widescreen fix does the same).
static bool FollowBox(const Elem& e, GameObject& g, ImVec2& a, ImVec2& b) {
    if (!e.follow || !gameObject || !gameObject(e.follow, g)) return false;
    ImVec2 ds = ImGui::GetIO().DisplaySize; float k = ds.y / 480.f;
    int col = e.anchor % 3;
    float cx = col == 0 ? (g.x + 320) * k : col == 1 ? ds.x / 2 + g.x * k : ds.x - (320 - g.x) * k;
    float cy = (g.y + 240) * k;
    cx += e.x * k; cy += e.y * k;
    float hw = g.w * e.scale * k / 2, hh = g.h * e.scale * k / 2;
    a = ImVec2(cx - hw, cy - hh); b = ImVec2(cx + hw, cy + hh);
    return true;
}
static void ElemRect(Elem& e, ImVec2& a, ImVec2& b) {
    if (e.follow && (e.type == E_IMAGE || e.type == E_SHAPE)) { GameObject g; if (FollowBox(e, g, a, b)) return; }
    ImVec2 ds = ImGui::GetIO().DisplaySize; float k = Unit();
    float ax = (e.anchor % 3) * 0.5f, ay = (e.anchor / 3) * 0.5f;
    ImVec2 sz = ElemSize(e);
    // text without a box width: its position is where the text starts / is centred / ends (by its alignment),
    // so changing text doesn't move it
    float px = (e.type == E_TEXT && e.w <= 0.f) ? e.align * 0.5f : ax;
    a = ImVec2(ax * ds.x + e.x * k - px * sz.x * k, ay * ds.y + e.y * k - ay * sz.y * k);
    b = ImVec2(a.x + sz.x * k, a.y + sz.y * k);
}
// a new anchor without moving it on screen
static void SetAnchorKeep(Elem& e, int na) {
    if (na == e.anchor || na < 0 || na > 8) return;
    ImVec2 a0, b0, a1, b1; ElemRect(e, a0, b0);
    e.anchor = na;
    ElemRect(e, a1, b1);
    float k = std::max(0.001f, ImGui::GetIO().DisplaySize.y / 480.f);
    e.x = roundf((e.x + (a0.x - a1.x) / k) * 10) / 10; e.y = roundf((e.y + (a0.y - a1.y) / k) * 10) / 10;
}
static ImU32 Col(const float c[4]) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3])); }
static bool RowShown(const Elem& e, bool& mine) {
    mine = false;
    if (e.row <= 0) return true;
    std::string v;
    if (e.row > boardRows) return false;
    if (!(tokens && tokens("r" + std::to_string(e.row) + ".exists", v)) || v.empty()) return false;
    if (tokens("r" + std::to_string(e.row) + ".me", v)) mine = !v.empty();
    return true;
}
static bool ValueOf(const std::string& key, float& out) {
    std::string v;
    if (key.empty() || !(tokens && tokens(key, v)) || v.empty()) return false;
    char* end = nullptr; out = strtof(v.c_str(), &end);
    return end != v.c_str();
}
static bool ShowIfOk(const Elem& e) {
    if (e.showIf.empty()) return true;
    bool neg = e.showIf[0] == '!';
    std::string v, key = neg ? e.showIf.substr(1) : e.showIf;
    if (key.compare(0, 11, "page.prompt") == 0 || key == "page.title") {   // (REUI's own: the menu's prompts / header text)
        g_tokGroup = e.group;
        if (key == "page.title") v = g_titleNow = TitleFor(e.group); else PageToken(key, v);
        bool set = !v.empty() && v != "0";
        return neg ? !set : set;
    }
    bool set = tokens && tokens(key, v) && !v.empty() && v != "0";
    return neg ? !set : set;
}
// 0..1 along the element's bind range (-1: no value now)
static float BindFrac(const Elem& e) {
    float v;
    if (e.bindKind == 0 || !ValueOf(e.bind, v)) return -1.f;
    float d = e.bindMax - e.bindMin;
    float f = d != 0 ? (v - e.bindMin) / d : 0.f;
    return f < 0 ? 0.f : f > 1 ? 1.f : f;
}
// ---- minimap
static double g_mapDrawnAt = -1;   // a minimap element was drawn in game (ImGui time)
bool MinimapShown(double within) { return g_mapDrawnAt >= 0 && ImGui::GetCurrentContext() && ImGui::GetTime() - g_mapDrawnAt <= within; }
// a texture by name, as image elements take it: a button icon, "vec:" art, a file texture or the game's live one
static gfx::Tex NamedTex(const std::string& name, int px, float& su, float& sv) {
    su = sv = 1;
    if (name.empty()) return nullptr;
    int fam, id;
    if (IconRef(name, fam, id)) return iconTexture ? iconTexture(fam, id, px) : nullptr;
    if (vecart::IsRef(name)) return vecart::Texture(vecart::RefName(name), px);
    static std::unordered_map<std::string, int> found;
    static size_t foundFor = 0;   // (indices into the texture list: forgotten when it's rescanned)
    if (foundFor != g_tex.size()) { found.clear(); foundFor = g_tex.size(); }
    auto it = found.find(name);
    int i = it != found.end() ? it->second : (found[name] = FindTex(name, ""));
    if (gfx::Tex t = TexOf(i)) return t;
    return liveTexture ? liveTexture(name, &su, &sv) : nullptr;
}
// ---- the Minimap page: what's drawn for each of the game's minimap textures. Per name (MINIMAP_ICON_CIRCUIT, the
// map TRACKMAP<track>, other mods' markers): "" automatic - REUI\vector\minimap\<name>.svg when that file is there,
// else the game's own; "-" the game's own always; anything else a texture as image elements take it ("vec:<art>",
// a game texture's name, "icon:..."). Used for the minimap's icons and map, and for image elements whose texture is
// one of these names (or vec:minimap/<name>: the frame / north pieces). Saved with the layout ("mapart=").
static std::map<std::string, std::string> g_mapArt;
static std::set<std::string> g_mapSeen;   // names the minimap has shown (other mods' markers, this track's map)
static const char* const kMapTex[] = {"MINIMAP_MASK", "MINIMAP_BORDER", "MINIMAP_BORDER_2", "MINIMAP_NORTH_INDICATOR",
    "MINIMAP_GPS_SELECTOR", "MINIMAP_ICON_CAR", "MINIMAP_ICON_CIRCUIT", "MINIMAP_ICON_COP_STAR", "MINIMAP_ICON_CROSS",
    "MINIMAP_ICON_SAFE_HOUSE", "MINIMAP_ICON_START_LINE", "MINIMAP_ICON_TICK"};
static bool PlainName(const std::string& n) { return !n.empty() && n.find(':') == std::string::npos && n.find('{') == std::string::npos; }
static bool MinimapName(const std::string& n) { return n.compare(0, 8, "MINIMAP_") == 0 || n.compare(0, 8, "TRACKMAP") == 0; }
// the texture drawn for a minimap name now: "" = the game's own
static std::string MapArtNow(const std::string& name) {
    auto it = g_mapArt.find(name);
    if (it != g_mapArt.end() && !it->second.empty()) return it->second == "-" ? std::string() : it->second;
    return vecart::Find("minimap/" + name) ? vecart::Ref("minimap/" + name) : std::string();
}
// an image element's texture that stands for a minimap name: the name (else "")
static std::string MapKeyOf(const std::string& tex) {
    std::string k = tex;
    if (vecart::IsRef(k)) { k = vecart::RefName(k); if (k.compare(0, 8, "minimap/") != 0) return ""; k = k.substr(8); }
    return PlainName(k) && MinimapName(k) ? k : "";
}
static ImU32 Mul(ImU32 a, const float c[4]) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(a);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x * c[0], v.y * c[1], v.z * c[2], v.w * c[3]));
}
// a quad of size w x h centred on p, turned deg clockwise
static void TurnedQuad(ImDrawList* dl, gfx::Tex tx, ImVec2 p, float w, float h, float deg, ImVec2 uv0, ImVec2 uv1, ImU32 col) {
    float r = deg * 3.14159265f / 180, cs = cosf(r), sn = sinf(r), hw = w / 2, hh = h / 2;
    auto at = [&](float x, float y) { return ImVec2(p.x + x * cs - y * sn, p.y + x * sn + y * cs); };
    dl->AddImageQuad(gfx::Id(tx), at(-hw, -hh), at(hw, -hh), at(hw, hh), at(-hw, hh), uv0, ImVec2(uv1.x, uv0.y), uv1, ImVec2(uv0.x, uv1.y), col);
}
static void Arrow(ImDrawList* dl, ImVec2 p, float w, float h, float deg, ImU32 fill) {   // the multiplayer mod's look: a flat-backed triangle
    float r = deg * 3.14159265f / 180;
    ImVec2 f(sinf(r), -cosf(r)), sd(-f.y, f.x);
    ImVec2 tip(p.x + f.x * h * 0.5f, p.y + f.y * h * 0.5f);
    ImVec2 bl(p.x - f.x * h * 0.3f + sd.x * w * 0.5f, p.y - f.y * h * 0.3f + sd.y * w * 0.5f);
    ImVec2 br(p.x - f.x * h * 0.3f - sd.x * w * 0.5f, p.y - f.y * h * 0.3f - sd.y * w * 0.5f);
    dl->AddTriangleFilled(tip, bl, br, fill);
    ImVec2 poly[3] = {tip, bl, br};
    dl->AddPolyline(poly, 3, IM_COL32(0, 0, 0, (int)(220 * ImGui::ColorConvertU32ToFloat4(fill).w)), ImDrawFlags_Closed, std::max(1.f, h * 0.08f));
}
// The game's minimap (0x4CA840), drawn standalone: the window of the track map around the car (span uv across),
// turned heading-up when the game's radar option says so; icons placed by their distance in minimap widths -
// past `limit` dropped, past 0.55 (the rim) held on it or hidden; the player's on top.
static void DrawMinimap(ImDrawList* dl, Elem& e, ImVec2 a, ImVec2 b, ImU32 col, bool editing) {
    ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
    float R = std::min(b.x - a.x, b.y - a.y) / 2;
    if (R < 2) return;
    MapView mv;
    bool ok = mapView && mapView(mv) && mv.tex && mv.size > 0 && mv.span > 0;
    if (!ok) {   // nothing to show: in the editor, where it goes
        if (!editing) return;
        dl->AddCircleFilled(c, R, Mul(IM_COL32(20, 32, 40, 200), e.col), 64);
        dl->AddCircle(c, R, Mul(IM_COL32(120, 200, 255, 200), e.col), 64, 1.5f);
        dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), IM_COL32(120, 200, 255, 70));
        dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y + R), IM_COL32(120, 200, 255, 70));
        Arrow(dl, c, R * 0.2f, R * 0.25f, 0, IM_COL32(48, 207, 48, 255));
        dl->AddText(ImVec2(c.x - ImGui::CalcTextSize("minimap").x / 2, c.y + R * 0.35f), IM_COL32(200, 230, 255, 200), "minimap");
        return;
    }
    g_mapDrawnAt = ImGui::GetTime();   // (other mods put their markers here instead of drawing their own)
    if (!mv.mapName.empty()) {   // the map itself, replaced on the Minimap page (its own alpha is used then)
        if (g_mapSeen.size() < 64) g_mapSeen.insert(mv.mapName);
        std::string r = MapArtNow(mv.mapName);
        float rsu = 1, rsv = 1;
        int px = (int)std::min(2048.f, 2 * R / std::max(0.01f, mv.span / e.mapZoom));
        if (!r.empty()) if (gfx::Tex t = NamedTex(r, px, rsu, rsv)) { mv.tex = t; mv.su = rsu; mv.sv = rsv; mv.alphaOk = true; }
    }
    const float kPi = 3.14159265f;
    float span = mv.span / e.mapZoom;
    float psi = mv.rotate ? -mv.bearing : 0;   // the world's turn on screen (degrees clockwise)
    float pr = psi * kPi / 180, pc = cosf(pr), ps = sinf(pr);
    float pu = (mv.px - mv.x0) / mv.size, pv = (mv.y0 - mv.py) / mv.size;
    // the map: a fan over the circle, each rim point's uv turned back by the world's turn
    const int N = 64;
    ImU32 mapCol = col;
    if (!mv.alphaOk) {   // over the screen at the game's mask opacity (about 72%; a redrawn minimap/MINIMAP_MASK: its opacity)
        static float maskA = 0.72f; static double maskAt = -100;
        if (ImGui::GetTime() - maskAt > 2) {   // (a redrawn MINIMAP_MASK on the Minimap page: its first shape's opacity)
            maskAt = ImGui::GetTime();
            std::string r = MapArtNow("MINIMAP_MASK");
            maskA = vecart::IsRef(r) ? vecart::Opacity(vecart::RefName(r), 0.72f) : 0.72f;
        }
        ImVec4 mc = ImGui::ColorConvertU32ToFloat4(col); mc.w *= maskA; mapCol = ImGui::ColorConvertFloat4ToU32(mc);
    }
    dl->AddCallback(gfx::CbMapSamplingOn, mv.alphaOk ? nullptr : (void*)1);
    dl->PushTextureID(gfx::Id(mv.tex));
    dl->PrimReserve(N * 3, N + 1);
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, ImVec2(pu * mv.su, pv * mv.sv), mapCol);
    for (int i = 0; i < N; ++i) {
        float t = i * 2 * kPi / N, sx = cosf(t), sy = sinf(t);   // on screen, unit circle
        float du = (sx * pc + sy * ps) * span / 2, dv = (-sx * ps + sy * pc) * span / 2;
        dl->PrimWriteVtx(ImVec2(c.x + sx * R, c.y + sy * R), ImVec2((pu + du) * mv.su, (pv + dv) * mv.sv), mapCol);
    }
    for (int i = 0; i < N; ++i) { dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i)); dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (i + 1) % N)); }
    dl->PopTextureID();
    dl->AddCallback(gfx::CbMapSamplingOff, nullptr);
    // icons and markers (the player's last)
    float k = 2 * R / 128 * e.mapBlip;   // game units on a 128-unit minimap -> pixels
    ImVec4 ec = ImGui::ColorConvertU32ToFloat4(col);
    for (int pass = 0; pass < 2; ++pass)
        for (auto& bl : mv.blips) {
            if (bl.player != (pass == 1) || (bl.player && !e.mapPlayer)) continue;
            float ox = (bl.x - mv.px) / mv.size / span, oy = (mv.py - bl.y) / mv.size / span;   // minimap widths, y down
            float d = sqrtf(ox * ox + oy * oy);
            if (!bl.farOk && d > mv.limit) continue;
            if (d > 0.55f) { if (bl.hideOff) continue; ox *= 0.55f / d; oy *= 0.55f / d; }
            ImVec2 p(c.x + (ox * pc - oy * ps) * 2 * R, c.y + (ox * ps + oy * pc) * 2 * R);
            p.x += bl.ox * bl.w * k; p.y += bl.oy * bl.h * k;   // (a badge on another marker)
            float deg = (bl.dirX != 0 || bl.dirY != 0) ? atan2f(bl.dirX, bl.dirY) * 180 / kPi + psi : 0;
            float bc[4] = {bl.col[0] * ec.x, bl.col[1] * ec.y, bl.col[2] * ec.z, bl.col[3] * ec.w};
            ImU32 bcol = Col(bc);
            float su = 1, sv = 1;
            gfx::Tex tx = bl.tex;
            ImVec2 uv0(bl.u0, bl.v0), uv1(bl.u1, bl.v1);
            // replaced on the Minimap page (automatic: REUI\vector\minimap\<the game texture's name>.svg) - the whole picture
            const std::string& gn = !bl.gameName.empty() ? bl.gameName : bl.texName;
            if (PlainName(gn)) {
                if (g_mapSeen.size() < 64) g_mapSeen.insert(gn);
                std::string r = MapArtNow(gn);
                float rsu = 1, rsv = 1;
                if (!r.empty()) if (gfx::Tex vt = NamedTex(r, (int)ceilf(std::max(bl.w, bl.h) * k), rsu, rsv)) { tx = vt; uv0 = ImVec2(0, 0); uv1 = ImVec2(rsu, rsv); }
            }
            if (!tx && !bl.texName.empty()) tx = NamedTex(bl.texName, (int)ceilf(std::max(bl.w, bl.h) * k), su, sv);
            if (tx) TurnedQuad(dl, tx, p, bl.w * k, bl.h * k, deg, ImVec2(uv0.x * su, uv0.y * sv), ImVec2(uv1.x * su, uv1.y * sv), bcol);
            else if (bl.texName.empty()) Arrow(dl, p, bl.w * k, bl.h * k, deg, bcol);
        }
}
// The world map (the game's World Map screen, 0x4B09E0): the track map's window [u0, u0 + frac] x [v0, v0 + frac] (Full
// Map: all of it; a region: the game's table at 0x7F42C8) in this box, square; the game's icons where they are in it
// (racers, shops / events - their types hidden on the World Map left out -, your car on top) and other mods' markers;
// the selected icon's ring (MINIMAP_GPS_SELECTOR). Tokens (UniPad): map.view.uv "u0 v0 frac", map.sel "x y", map.hidden
// " 6 7 " (the icon types hidden).
static void DrawWorldMap(ImDrawList* dl, Elem& e, ImVec2 a, ImVec2 b, ImU32 col, bool editing) {
    if (b.x - a.x < 4 || b.y - a.y < 4) return;
    MapView mv;
    bool ok = worldView && worldView(mv) && mv.tex && mv.size > 0;
    if (!ok) {   // nothing to show: in the editor, where it goes
        if (!editing) return;
        dl->AddRectFilled(a, b, Mul(IM_COL32(20, 32, 40, 200), e.col));
        dl->AddRect(a, b, Mul(IM_COL32(120, 200, 255, 200), e.col), 0, 0, 1.5f);
        ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
        dl->AddText(ImVec2(c.x - ImGui::CalcTextSize("world map").x / 2, c.y - 7), IM_COL32(200, 230, 255, 200), "world map");
        return;
    }
    std::string v;
    float u0 = 0, v0 = 0, frac = 1;
    if (tokens && tokens("map.view.uv", v)) { float x, y, f; if (sscanf(v.c_str(), "%f %f %f", &x, &y, &f) == 3 && f > 0.01f && f <= 1.5f) { u0 = x; v0 = y; frac = f; } }
    std::string hidden; if (tokens) tokens("map.hidden", hidden);
    hidden = " " + hidden + " ";
    bool hasSel = false; float sx = 0, sy = 0;
    if (tokens && tokens("map.sel", v) && sscanf(v.c_str(), "%f %f", &sx, &sy) == 2) hasSel = true;
    // a square in the box (centred)
    float side = std::min(b.x - a.x, b.y - a.y);
    ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2), A(c.x - side / 2, c.y - side / 2), B(c.x + side / 2, c.y + side / 2);
    if (!mv.mapName.empty()) {   // the map itself, replaced on the Minimap page
        if (g_mapSeen.size() < 64) g_mapSeen.insert(mv.mapName);
        std::string r = MapArtNow(mv.mapName);
        float rsu = 1, rsv = 1;
        if (!r.empty()) if (gfx::Tex t = NamedTex(r, (int)std::min(2048.f, side / frac), rsu, rsv)) { mv.tex = t; mv.su = rsu; mv.sv = rsv; mv.alphaOk = true; }
    }
    ImU32 mapCol = col;
    if (!mv.alphaOk) { ImVec4 mc = ImGui::ColorConvertU32ToFloat4(col); mc.w *= 0.72f; mapCol = ImGui::ColorConvertFloat4ToU32(mc); }
    dl->PushClipRect(A, B, true);
    dl->AddCallback(gfx::CbMapSamplingOn, mv.alphaOk ? nullptr : (void*)1);
    dl->AddImage(gfx::Id(mv.tex), A, B, ImVec2(u0 * mv.su, v0 * mv.sv), ImVec2((u0 + frac) * mv.su, (v0 + frac) * mv.sv), mapCol);
    dl->AddCallback(gfx::CbMapSamplingOff, nullptr);
    auto at = [&](float wx, float wy) {
        float u = (wx - mv.x0) / mv.size, w = (mv.y0 - wy) / mv.size;
        return ImVec2(A.x + (u - u0) / frac * side, A.y + (w - v0) / frac * side);
    };
    const float kPi = 3.14159265f;
    float k = side / 353.f * e.mapBlip * 0.9f;   // (the stock icons against its 353-unit map)
    ImVec4 ec = ImGui::ColorConvertU32ToFloat4(col);
    auto draw = [&](const MapBlip& bl, ImVec2 p, float scale) {
        p.x += bl.ox * bl.w * k; p.y += bl.oy * bl.h * k;
        float deg = (bl.dirX != 0 || bl.dirY != 0) ? atan2f(bl.dirX, bl.dirY) * 180 / kPi : 0;
        float bc[4] = {bl.col[0] * ec.x, bl.col[1] * ec.y, bl.col[2] * ec.z, bl.col[3] * ec.w};
        ImU32 bcol = Col(bc);
        float su = 1, sv = 1, w = bl.w * k * scale, h = bl.h * k * scale;
        gfx::Tex tx = bl.tex;
        ImVec2 uv0(bl.u0, bl.v0), uv1(bl.u1, bl.v1);
        const std::string& gn = !bl.gameName.empty() ? bl.gameName : bl.texName;
        if (PlainName(gn)) {
            if (g_mapSeen.size() < 64) g_mapSeen.insert(gn);
            std::string r = MapArtNow(gn);
            float rsu = 1, rsv = 1;
            if (!r.empty()) if (gfx::Tex vt = NamedTex(r, (int)ceilf(std::max(w, h)), rsu, rsv)) { tx = vt; uv0 = ImVec2(0, 0); uv1 = ImVec2(rsu, rsv); }
        }
        if (!tx && !bl.texName.empty()) tx = NamedTex(bl.texName, (int)ceilf(std::max(w, h)), su, sv);
        if (tx) TurnedQuad(dl, tx, p, w, h, deg, ImVec2(uv0.x * su, uv0.y * sv), ImVec2(uv1.x * su, uv1.y * sv), bcol);
        else if (bl.texName.empty()) Arrow(dl, p, w, h, deg, bcol);
    };
    for (int pass = 0; pass < 2; ++pass)
        for (auto& bl : mv.blips) {
            if (bl.player != (pass == 1) || (bl.player && !e.mapPlayer)) continue;
            if (bl.type >= 0 && hidden.find(" " + std::to_string(bl.type) + " ") != std::string::npos) continue;
            draw(bl, at(bl.x, bl.y), 1.f);
        }
    // the selected icon's ring
    if (hasSel) {
        // the game's (UI_InGame_WorldMap.fng, its two GPS selectors' looping script): two rings growing out from the
        // place and fading, 1.6 s, the second 0.4 s behind - sizes in the stock map's units (353 across)
        ImVec2 p = at(sx, sy);
        float ku = side / 353.f * e.mapBlip;
        static const float kRing[2][6][3] = {   // ms, size, alpha (0-255)
            {{0, 28.75f, 0}, {200, 37.83f, 255}, {600, 56, 180}, {1200, 70, 0}, {1600, 70, 0}, {1600, 70, 0}},
            {{0, 28.75f, 0}, {400, 28.75f, 255}, {1000, 56, 180}, {1600, 70, 0}, {1600, 70, 0}, {1600, 70, 0}}};
        float ph = (float)fmod(ImGui::GetTime() * 1000.0, 1600.0);
        float big = 70 * ku;
        std::string r = MapArtNow("MINIMAP_GPS_SELECTOR"); float su = 1, sv = 1;
        gfx::Tex tx = r.empty() ? nullptr : NamedTex(r, (int)ceilf(big), su, sv);
        if (!tx) tx = NamedTex("MINIMAP_GPS_SELECTOR", (int)ceilf(big), su, sv);
        for (auto& ring : kRing) {
            float sz = ring[5][1], al = ring[5][2];
            for (int i = 0; i < 5; ++i)
                if (ph >= ring[i][0] && ph <= ring[i + 1][0]) {
                    float f = ring[i + 1][0] > ring[i][0] ? (ph - ring[i][0]) / (ring[i + 1][0] - ring[i][0]) : 0.f;
                    sz = ring[i][1] + (ring[i + 1][1] - ring[i][1]) * f; al = ring[i][2] + (ring[i + 1][2] - ring[i][2]) * f;
                    break;
                }
            if (al < 1) continue;
            float rr = sz * ku;
            if (tx) dl->AddImage(gfx::Id(tx), ImVec2(p.x - rr / 2, p.y - rr / 2), ImVec2(p.x + rr / 2, p.y + rr / 2), ImVec2(0, 0), ImVec2(su, sv), Mul(IM_COL32(255, 255, 255, (int)al), e.col));
            else dl->AddCircle(p, rr / 2, Mul(IM_COL32(0, 128, 255, (int)al), e.col), 32, std::max(1.5f, rr * 0.08f));
        }
    }
    dl->PopClipRect();
}

// ---- shapes
static std::vector<Elem>* g_drawList = nullptr;   // the elements being drawn now (where a shape's cutters are)
// a shape: its stroke and fill textures (shapes.cpp) on its box, turned; cutters found among the elements drawn now
static bool ResolveGrad(const UGrad& g, float depthScale, shapes::Gradient& out);
static std::string OwnerPage(const std::string& g);   // (uied_menu.inc)
static bool InGroup(const std::string& eg, const std::string& g);
static std::string RelGroup(const std::string& g);   // (uied_menu.inc)
static bool MenuEdOff(const std::string& group);   // (uied_menu.inc)
static std::vector<std::string> GroupNames();
// does cutter o cut shape e: its own group's shapes, a whole group's, or the shape it names
static bool CutHits(const Elem& o, const Elem& e) {
    if (o.cutTarget.empty()) return o.group == e.group;
    if (o.cutTarget.compare(0, 6, "group:") == 0) return InGroup(e.group, o.cutTarget.substr(6));
    return o.cutTarget == e.name;
}
static void DrawShape(ImDrawList* dl, Elem& e, ImVec2 a, ImVec2 b, ImU32 col, bool editing) {
    float pw = b.x - a.x, ph = b.y - a.y;
    if (pw < 1 || ph < 1) return;
    float rot = e.rot * 3.14159265f / 180, cs = cosf(rot), sn = sinf(rot);
    ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
    auto at = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
    if (e.merge) {   // merged into another shape: drawn as a part of it (marked with a dashed cyan outline while editing)
        if (editing) {
            ImVec2 q[4] = {at(-pw / 2, -ph / 2), at(pw / 2, -ph / 2), at(pw / 2, ph / 2), at(-pw / 2, ph / 2)};
            for (int i = 0; i < 4; ++i) {
                ImVec2 p0 = q[i], p1 = q[(i + 1) % 4];
                float len = sqrtf((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
                for (float t = 0; t < len; t += 8) {
                    float t1 = std::min(len, t + 4);
                    dl->AddLine(ImVec2(p0.x + (p1.x - p0.x) * t / len, p0.y + (p1.y - p0.y) * t / len), ImVec2(p0.x + (p1.x - p0.x) * t1 / len, p0.y + (p1.y - p0.y) * t1 / len), IM_COL32(80, 220, 255, 170), 1.f);
                }
            }
        }
        return;
    }
    float k = e.w > 0 ? pw / e.w : 1;   // units -> pixels (scale and animation included)
    shapes::Box box; box.w = pw; box.h = ph;
    for (int i = 0; i < 4; ++i) box.r[i] = std::max(0.f, e.rad[i]) * k;
    std::vector<shapes::Box> cuts;
    if (g_drawList && !e.cut)   // (a cutter is drawn as itself, not cut by others)
        for (auto& o : *g_drawList) {
            if (o.type != E_SHAPE || !o.cut || !o.visible || !ShowIfOk(o)) continue;
            if (!CutHits(o, e)) continue;
            if (e.layer > o.layer) continue;   // (a cutter cuts what's on its layer and under it - not shapes laid over it)
            if (OwnerPage(o.group) != OwnerPage(e.group)) continue;   // (menus: a cutter cuts in its own menu only; HUD pieces have none)
            ImVec2 oa, ob; ElemRect(o, oa, ob);
            float ow = ob.x - oa.x, oh = ob.y - oa.y, ok = o.w > 0 ? ow / o.w : 1;
            ImVec2 oc((oa.x + ob.x) / 2, (oa.y + ob.y) / 2);
            float dx = oc.x - c.x, dy = oc.y - c.y, gap = (std::max(-1000.f, std::min(1000.f, o.cutGap)) + kCutZero) * ok;   // (negative: the cut is smaller than the cutter; 0 = kCutZero)
            // (out of reach: left out, so moving it elsewhere doesn't redraw the shape)
            if (sqrtf(dx * dx + dy * dy) > (sqrtf(pw * pw + ph * ph) + sqrtf(ow * ow + oh * oh)) / 2 + gap + 2) continue;
            shapes::Box x;
            x.cx = dx * cs + dy * sn; x.cy = -dx * sn + dy * cs;   // into the shape's frame
            x.w = ow; x.h = oh; x.rot = (o.rot - e.rot) * 3.14159265f / 180; x.gap = gap;
            for (int i = 0; i < 4; ++i) x.r[i] = std::max(0.f, o.rad[i]) * ok;
            cuts.push_back(x);
        }
    std::vector<shapes::Box> merges;   // shapes merged into it (a union): in its frame, grown by their gaps
    if (g_drawList && !e.cut)
        for (auto& o : *g_drawList) {
            if (o.type != E_SHAPE || !o.merge || &o == &e || !o.visible || !ShowIfOk(o)) continue;
            if (!CutHits(o, e)) continue;
            if (OwnerPage(o.group) != OwnerPage(e.group)) continue;
            ImVec2 oa, ob; ElemRect(o, oa, ob);
            float ow = ob.x - oa.x, oh = ob.y - oa.y, ok = o.w > 0 ? ow / o.w : 1;
            if (ow < 0.5f || oh < 0.5f) continue;
            ImVec2 oc((oa.x + ob.x) / 2, (oa.y + ob.y) / 2);
            float dx = oc.x - c.x, dy = oc.y - c.y;
            shapes::Box x;
            x.cx = dx * cs + dy * sn; x.cy = -dx * sn + dy * cs;
            x.w = ow; x.h = oh; x.rot = (o.rot - e.rot) * 3.14159265f / 180; x.gap = std::max(-1000.f, std::min(1000.f, o.cutGap)) * ok;
            for (int i = 0; i < 4; ++i) x.r[i] = std::max(0.f, o.rad[i]) * ok;
            merges.push_back(x);
        }
    shapes::Gradient sg, fg;   // (tokens resolved now: a token's change redraws the shape)
    bool sOn = ResolveGrad(e.strokeGrad, 1, sg), fOn = ResolveGrad(e.fillGrad, k, fg);
    float sw = std::max(0.f, e.stroke) * k;
    // the fill keeps its own colour, faded with the element (animations, blink) as the stroke colour is
    float fade = e.col[3] > 0.001f ? std::min(1.f, ImGui::ColorConvertU32ToFloat4(col).w / e.col[3]) : 1.f;
    float fc[4] = {e.fillCol[0], e.fillCol[1], e.fillCol[2], e.fillCol[3] * fade};
    if (fOn) fc[0] = fc[1] = fc[2] = 1;   // the gradient's colours, at the fill's opacity
    if (sOn) { ImVec4 v = ImGui::ColorConvertU32ToFloat4(col); col = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, v.w)); }
    bool drawn = false;
    float uox, uoy, uhw, uhh; shapes::UnionBox(box, merges, uox, uoy, uhw, uhh);   // (the box around it and what's merged into it)
    auto uquad = [&](float m, ImVec2* q) {
        float l = uox - uhw - m, rt = uox + uhw + m, t = uoy - uhh - m, bt = uoy + uhh + m;
        q[0] = at(l, t); q[1] = at(rt, t); q[2] = at(rt, bt); q[3] = at(l, bt);
    };
    if (shapes::GpuOn()) {   // the graphics card works it out per pixel (nothing made, nothing kept)
        float m = shapes::GpuMargin(sw, e.strokeAlign); ImVec2 q[4]; uquad(m, q);
        drawn = shapes::GpuDraw(dl, box, sw, e.strokeAlign, sg, fg, cuts, merges, m, q, fc[3] > 0.002f ? Col(fc) : 0u, col);
    }
    if (!drawn) {   // (no shaders, or more cuts / gradient stops than they take): a texture made on the CPU
        shapes::Result r;
        if (!shapes::Get(box, sw, e.strokeAlign, sg, fg, cuts, merges, r)) return;
        ImVec2 q[4]; uquad(r.margin, q);
        if (r.fill && fc[3] > 0.002f) dl->AddImageQuad(gfx::Id(r.fill), q[0], q[1], q[2], q[3], ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), Col(fc));
        if (r.stroke) dl->AddImageQuad(gfx::Id(r.stroke), q[0], q[1], q[2], q[3], ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
    }
    if (e.cut && editing) {   // a cutter: marked as one while editing (an invisible one - opacity 0 - is still found)
        ImVec2 q[4] = {at(-pw / 2, -ph / 2), at(pw / 2, -ph / 2), at(pw / 2, ph / 2), at(-pw / 2, ph / 2)};
        dl->AddPolyline(q, 4, IM_COL32(255, 80, 200, 150), ImDrawFlags_Closed, 1.f);
    }
}
// A text's change animated as the game's help bar ticker swaps the profile name and the cash (UI_PC_Customize_Options /
// the help bar strings' scripts Fade_Out and Fade_In, 320 ms, straight lines): the old text's size goes from 1 to half
// across and to nothing (-1/6) down while it fades out; the new one's the other way while it fades in - both at once,
// about the text's alignment point across and its middle down. true: drawn here (while a swap runs).
static bool SwapText(ImDrawList* dl, const Elem& e, ImVec2 a, ImVec2 b, ImU32 col, const std::string& s) {
    struct SwapSt { std::string cur, prev; double at = -100, seen = -100; };
    static std::unordered_map<std::string, SwapSt> st;
    SwapSt& w = st[e.group + '\x1f' + e.name + '\x1f' + e.text];
    double now = ImGui::GetTime();
    if (s != w.cur) {   // (not when it first shows, or comes back after a while: just there)
        if (!w.cur.empty() && !s.empty() && now - w.seen < 0.5) { w.prev = w.cur; w.at = now; } else w.at = -100;
        w.cur = s;
    }
    w.seen = now;
    float f = (float)((now - w.at) / 0.32);
    if (f >= 1.f) return false;
    const Font& fo = g_fonts[e.fontIdx];
    auto draw = [&](const std::string& txt, float sx, float sy, float al) {
        if (txt.empty() || sy <= 0.001f || al <= 0.004f) return;
        int v0 = dl->VtxBuffer.Size;
        ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col); cv.w *= al;
        DrawText(dl, fo, a, e.scale * Unit(), ImGui::ColorConvertFloat4ToU32(cv), txt, e.align, b.x - a.x);
        int v1 = dl->VtxBuffer.Size; if (v1 <= v0) return;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int i = v0; i < v1; ++i) { ImVec2 p = dl->VtxBuffer[i].pos; x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
        float cx = e.align == 0 ? x0 : e.align == 2 ? x1 : (x0 + x1) / 2, cy = (y0 + y1) / 2;
        for (int i = v0; i < v1; ++i) { ImVec2& p = dl->VtxBuffer[i].pos; p.x = cx + (p.x - cx) * sx; p.y = cy + (p.y - cy) * sy; }
    };
    draw(w.prev, 1.f - 0.5f * f, 1.f - 7.f / 6.f * f, 1.f - f);   // Fade_Out
    draw(s, 0.5f + 0.5f * f, -1.f / 6.f + 7.f / 6.f * f, f);      // Fade_In
    return true;
}
// Colour token swaps in force while a menu page is drawn (its "out of the world" swaps when you're in the garage / front
// end: from -> to). Elements drawn then take the "to" token's colour where they name the "from" one (colour, fill,
// gradient stops and gradient tokens). Only on the copies the menus draw (never the layout's own elements).
static const std::vector<std::pair<std::string, std::string>>* g_tokSwap = nullptr;
static const std::string& SwapTok(const std::string& n) {
    if (g_tokSwap && !n.empty()) for (auto& s : *g_tokSwap) if (s.first == n) return s.second;
    return n;
}
static void ApplyTokSwap(Elem& e);
namespace anim {   // (uied_anim.inc) an element's clips now: the change they make to it this frame
struct Pose { float dx = 0, dy = 0, sx = 1, sy = 1, rot = 0, a = 1, mul[3] = { 1, 1, 1 }, add[3] = { 0, 0, 0 }; };
static bool PoseOf(Elem& e, bool editing, Pose& p);   // false: nothing playing on it
static bool Previewing();   // animation mode is on: every element shows (its show-only-while rule waits)
}
static void DrawElem(ImDrawList* dl, Elem& e, bool editing) {
    if (!e.visible) return;
    if (g_tokSwap && !editing) ApplyTokSwap(e);
    g_tokGroup = e.group;
    if (!editing && !e.flags.empty() && FlagsHide(e)) return;
    struct AnimSave { Elem& e; bool on = false; float scale = 1, x = 0, y = 0; ~AnimSave() { if (on) { e.scale = scale; e.x = x; e.y = y; } } } animSave{ e };
    if (!ShowIfOk(e)) { e.shownSince = -1; if (!editing && !anim::Previewing()) return; }
    else if (e.shownSince < 0 && e.playOn.empty()) e.shownSince = ImGui::GetTime();   // (play on: only when its value comes on)
    if (!e.playOn.empty() && !editing) {   // replays from its start when its value comes on (pressed)
        std::string v; bool on = tokens && tokens(e.playOn, v) && !v.empty() && v != "0";
        if (on && !e.playWas) e.shownSince = ImGui::GetTime();
        e.playWas = on;
    }
    if (e.row > boardRows) return;               // past the rows shown (also while editing)
    bool mine = false;
    if (!RowShown(e, mine) && !editing) return;   // (while editing, rows nobody fills are drawn so they can be placed)
    if (!e.flags.empty() && ShowIfOk(e) && !FlagsHide(e)) ReplaceSeen(e);
    ImVec2 a, b; ElemRect(e, a, b);
    if (e.bindKind == 5 || e.bindKind == 6) {   // moves across / up with its value (from / to move: rotMin / rotMax, units)
        float f = BindFrac(e);
        if (f >= 0.f) {
            float d = (e.rotMin + (e.rotMax - e.rotMin) * f) * Unit();
            if (e.bindKind == 5) { a.x += d; b.x += d; } else { a.y -= d; b.y -= d; }   // (up: a positive move goes up the screen)
        }
    }
    ImU32 col = Col(mine && e.hiMine ? e.hiCol : e.col);
    float tintK[3] = {1, 1, 1}; bool tinted = false;   // (a shape's fill takes it too)
    if (!e.tint.empty()) {   // colour times a value's "R G B"
        std::string v; int r, g, b2;
        if (tokens && tokens(e.tint, v) && sscanf(v.c_str(), "%d %d %d", &r, &g, &b2) == 3) {
            const float* c0 = mine && e.hiMine ? e.hiCol : e.col;
            float c[4] = {c0[0] * r / 255.f, c0[1] * g / 255.f, c0[2] * b2 / 255.f, c0[3]};
            col = Col(c);
            tintK[0] = r / 255.f; tintK[1] = g / 255.f; tintK[2] = b2 / 255.f; tinted = true;
        }
    }
    if (e.anim && !editing) {   // pulse: up to 30% bigger and half faded; blink: hidden half the time
        double t = ImGui::GetTime() - (e.shownSince >= 0 ? e.shownSince : 0);   // from when it came on, like the game's scripts
        if (e.anim == 2 && fmod(t, 0.6) > 0.3) return;
        std::string bounceKeys;   // (bounce: its keys, made from its settings - plays once, then as it was)
        if (e.anim == 4) {
            float ms = std::max(20.f, e.bounceMs), d = 1.f - std::max(0.f, std::min(1.f, e.bounceGlow)), sc = e.bounceScale;
            char kb[256];
            snprintf(kb, sizeof kb, "!%g;0 1 1 1 0 0 %g %g %g;%g %g %g %g 0 0 1 1 1;%g 1 1 1 0 0 %g %g %g;%g 1 1 1 0 0 1 1 1",
                     ms, d, d, d, ms / 2, sc, sc, std::max(0.f, e.bounceAlpha), ms - 1, d, d, d, ms);
            bounceKeys = kb;
        }
        const std::string& keys = e.anim == 4 ? bounceKeys : e.keys;
        if ((e.anim == 3 || e.anim == 4) && !keys.empty()) {   // the game's FEng script keys (looping): "t sx sy a [dx dy [r g b]]"
            bool once = keys[0] == '!';   // "!600;...": plays once and holds its last key (else it loops)
            float period = (float)atof(keys.c_str() + (once ? 1 : 0));
            std::vector<float> k;   // 9 per key: t sx sy a dx dy r g b
            for (size_t p = keys.find(';'); p != std::string::npos; p = keys.find(';', p + 1)) {
                float v[9] = {0, 1, 1, 1, 0, 0, 1, 1, 1};
                if (sscanf(keys.c_str() + p + 1, "%f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) >= 4) k.insert(k.end(), v, v + 9);
            }
            if (period > 0 && k.size() >= 18) {
                float ph = once ? (float)std::min(t * 1000.0, (double)period) : (float)fmod(t * 1000.0, period), v[9];
                for (int j = 0; j < 9; ++j) v[j] = k[j];
                if (ph >= k[k.size() - 9]) for (int j = 0; j < 9; ++j) v[j] = k[k.size() - 9 + j];
                for (size_t i = 0; i + 9 < k.size(); i += 9)
                    if (ph >= k[i] && ph <= k[i + 9]) {
                        float f = k[i + 9] > k[i] ? (ph - k[i]) / (k[i + 9] - k[i]) : 0.f;
                        for (int j = 1; j < 9; ++j) v[j] = k[i + j] + (k[i + 9 + j] - k[i + j]) * f;
                        break;
                    }
                float sx = v[1], sy = v[2], al = v[3], dx = v[4], dy = v[5];
                if (e.type == E_TEXT) {   // text grows from its own place (its alignment point), like the game's
                    animSave.on = true; animSave.scale = e.scale; animSave.x = e.x; animSave.y = e.y;
                    e.scale *= sx; e.x += dx; e.y += dy;
                    ElemRect(e, a, b);
                } else {
                    float k2 = Unit();
                    ImVec2 c((a.x + b.x) / 2 + dx * k2, (a.y + b.y) / 2 + dy * k2), h((b.x - a.x) / 2 * sx, (b.y - a.y) / 2 * sy);
                    a = ImVec2(c.x - h.x, c.y - h.y); b = ImVec2(c.x + h.x, c.y + h.y);
                }
                ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
                cv.x = std::min(1.f, cv.x * v[6]); cv.y = std::min(1.f, cv.y * v[7]); cv.z = std::min(1.f, cv.z * v[8]); cv.w *= al;
                col = ImGui::ColorConvertFloat4ToU32(cv);
                if (al <= 0.004f) return;
            }
        }
        if (e.anim == 1) {
            float s = 0.5f - 0.5f * cosf((float)fmod(t, 1.0) * 6.2831853f);   // 0..1..0
            ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2), h((b.x - a.x) / 2 * (1 + 0.3f * s), (b.y - a.y) / 2 * (1 + 0.3f * s));
            a = ImVec2(c.x - h.x, c.y - h.y); b = ImVec2(c.x + h.x, c.y + h.y);
            ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col); cv.w *= 1 - 0.5f * s; col = ImGui::ColorConvertFloat4ToU32(cv);
        }
    }
    // animation mode's clips (uied_anim.inc): moved (units), scaled about its pivot (images / shapes: the centre unless a
    // pivot is set; text: its box's centre), turned (images / shapes), faded and coloured (x: times, +: added - images)
    struct PoseKeep { Elem& e; float rot, px, py, fill[4]; bool on = false;
        ~PoseKeep() { if (on) { e.rot = rot; e.pivX = px; e.pivY = py; for (int i = 0; i < 4; ++i) e.fillCol[i] = fill[i]; } } }
        poseKeep{ e, e.rot, e.pivX, e.pivY, { e.fillCol[0], e.fillCol[1], e.fillCol[2], e.fillCol[3] } };
    anim::Pose pose;
    const bool posed = anim::PoseOf(e, editing, pose);
    const bool poseAdd = posed && e.type == E_IMAGE && pose.add[0] + pose.add[1] + pose.add[2] > 0.004f;
    if (posed) {
        if (pose.a <= 0.004f) return;
        poseKeep.on = true;
        const float k = Unit();
        if (e.type == E_TEXT) {
            const ImVec2 c0((a.x + b.x) / 2, (a.y + b.y) / 2);
            if (!animSave.on) { animSave.on = true; animSave.scale = e.scale; animSave.x = e.x; animSave.y = e.y; }
            e.scale *= pose.sx;
            ImVec2 a1, b1; ElemRect(e, a1, b1);
            e.x += (c0.x - (a1.x + b1.x) / 2) / k + pose.dx; e.y += (c0.y - (a1.y + b1.y) / 2) / k + pose.dy;
            ElemRect(e, a, b);
        } else {
            const ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2), pv(c.x + e.pivX * k, c.y + e.pivY * k);
            a = ImVec2(pv.x + (a.x - pv.x) * pose.sx + pose.dx * k, pv.y + (a.y - pv.y) * pose.sy + pose.dy * k);
            b = ImVec2(pv.x + (b.x - pv.x) * pose.sx + pose.dx * k, pv.y + (b.y - pv.y) * pose.sy + pose.dy * k);
            e.pivX *= pose.sx; e.pivY *= pose.sy;   // (the same point of it, from its new centre)
            e.rot += pose.rot;
        }
        ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
        cv.x = std::min(1.f, cv.x * pose.mul[0]); cv.y = std::min(1.f, cv.y * pose.mul[1]); cv.z = std::min(1.f, cv.z * pose.mul[2]); cv.w *= std::min(1.f, pose.a);
        col = ImGui::ColorConvertFloat4ToU32(cv);
        for (int i = 0; i < 3; ++i) e.fillCol[i] = std::min(1.f, e.fillCol[i] * pose.mul[i]);
        e.fillCol[3] *= std::min(1.f, pose.a);
    }
    GameObject go; bool following = false;
    if ((e.type == E_IMAGE || e.type == E_SHAPE) && e.follow) {   // (a shape: the game object's box, its fill tinted by the game's colour)
        following = FollowBox(e, go, a, b);
        if (!following && !editing) return;   // the game isn't showing it
        if (following) {
            const float* c0 = mine && e.hiMine ? e.hiCol : e.col;
            float c[4]; for (int i = 0; i < 4; ++i) c[i] = c0[i] * go.col[i];
            col = Col(c);
        }
    }
    if (e.type == E_SHAPE) {
        float bf = (e.bindKind == 1 || e.bindKind == 2) ? BindFrac(e) : -1.f;   // a meter: the shape drawn as long as its value
        if (bf >= 0.f) bf = e.cutLo + (e.cutHi - e.cutLo) * bf;
        if (bf >= 0.f && e.bindKind == 1) { if (bf <= 0.001f && !editing) return; b.x = a.x + (b.x - a.x) * std::max(bf, editing ? 0.02f : 0.f); }
        if (bf >= 0.f && e.bindKind == 2) { if (bf <= 0.001f && !editing) return; a.y = b.y - (b.y - a.y) * std::max(bf, editing ? 0.02f : 0.f); }
        if (tinted || following) {   // (its fill too: for the call only)
            float keep[4] = {e.fillCol[0], e.fillCol[1], e.fillCol[2], e.fillCol[3]};
            for (int i = 0; i < 3; ++i) e.fillCol[i] *= (tinted ? tintK[i] : 1.f) * (following ? go.col[i] : 1.f);
            if (following) e.fillCol[3] *= go.col[3];
            DrawShape(dl, e, a, b, col, editing);
            for (int i = 0; i < 4; ++i) e.fillCol[i] = keep[i];
            return;
        }
        DrawShape(dl, e, a, b, col, editing); return;
    }
    if (e.type == E_IMAGE && e.minimap) { DrawMinimap(dl, e, a, b, col, editing); return; }
    if (e.type == E_IMAGE && e.worldMap) { DrawWorldMap(dl, e, a, b, col, editing); return; }
    if (e.type == E_IMAGE) {
        int ifam, iid;
        bool icon = IconRef(e.tex, ifam, iid);   // a controller button icon (its own textures)
        bool vec = !icon && vecart::IsRef(e.tex);   // "vec:<name>": vector art (REUI\vector)
        bool texVal = !icon && !vec && e.tex.find('{') != std::string::npos;   // "{tach_fill}": the texture a live value names
        if (texVal) { std::string x = Expand(e.tex); if (IconRef(x, ifam, iid)) icon = true; }   // ({page.prompt.1.icon}: a controller icon)
        if (e.texIdx == -2) e.texIdx = icon || vec ? -3 : texVal ? -1 : FindTex(e.tex, e.pack);
        // icons and vector art: drawn for their size on screen (an SVG is re-rasterised to it)
        int onScreen = (int)ceilf(std::max(b.x - a.x, b.y - a.y));
        gfx::Tex tx = icon ? (iconTexture ? iconTexture(ifam, iid, onScreen) : nullptr)
                              : vec ? vecart::Texture(vecart::RefName(e.tex), onScreen) : TexOf(e.texIdx);
        float su = 1, sv = 1;   // a live texture padded to a power of two: the picture is its top-left part
        bool noAlpha = false;   // a live texture with nothing in its alpha (a track map): drawn with the element's alpha only
        if (!tx && !icon && e.texIdx < 0 && liveTexture && !e.tex.empty()) {
            std::string tn = texVal ? Expand(e.tex) : e.tex;
            if (!tn.empty() && tn.find('{') == std::string::npos) tx = liveTexture(tn, &su, &sv);
            if (tx && tn[0] != '@' && textureAlphaOk && !textureAlphaOk(tx)) noAlpha = true;
        }
        {   // a minimap texture (or its vec:minimap/ art): what the Minimap page says to draw for it
            std::string key = MapKeyOf(e.tex);
            if (!key.empty()) {
                std::string r = MapArtNow(key), want = r.empty() ? key : r;
                float rsu = 1, rsv = 1;
                if (want != e.tex) if (gfx::Tex rt = NamedTex(want, onScreen, rsu, rsv)) { tx = rt; su = rsu; sv = rsv; }
            }
        }
        ImVec2 uv0(e.u0 * su, e.v0 * sv), uv1(e.u1 * su, e.v1 * sv);
        float rotSave = e.rot, bf = BindFrac(e);
        if (following) {   // the game's texture and uv (unless this element has its own art), and its turn
            if (e.tex.empty()) { tx = go.tex; uv0 = ImVec2(go.u0 * go.su, go.v0 * go.sv); uv1 = ImVec2(go.u1 * go.su, go.v1 * go.sv); }
            e.rot = rotSave + go.rot;
        }
        if (bf >= 0.f && (e.bindKind == 1 || e.bindKind == 2)) bf = e.cutLo + (e.cutHi - e.cutLo) * bf;
        if (bf >= 0.f && e.bindKind == 1) { b.x = a.x + (b.x - a.x) * bf; uv1.x = uv0.x + (uv1.x - uv0.x) * bf; }
        if (bf >= 0.f && e.bindKind == 2) { a.y = b.y - (b.y - a.y) * bf; uv0.y = uv1.y - (uv1.y - uv0.y) * bf; }
        if (bf >= 0.f && e.bindKind == 3) e.rot = e.rotMin + (e.rotMax - e.rotMin) * bf;
        if (e.bindKind == 4 && bf < 0.f) tx = nullptr;   // reveal with no value: nothing
        struct Restore { Elem& e; float r; ~Restore() { e.rot = r; } } restore{ e, rotSave };
        if (tx && (bf == 0.f && (e.bindKind == 1 || e.bindKind == 2))) tx = nullptr;   // empty bar: nothing
        if (tx && e.additive) dl->AddCallback(gfx::CbAdditive, nullptr);
        if (tx && noAlpha) dl->AddCallback(gfx::CbNoAlpha, nullptr);
        if (tx && bf >= 0.f && e.bindKind == 4) {   // reveal: only the half on the "up" side of a line through the pivot, turned by the value
            float ku = Unit(), th = (e.rotMin + (e.rotMax - e.rotMin) * bf) * 3.14159265f / 180;
            ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2), pv(c.x + e.pivX * ku, c.y + e.pivY * ku), up(sinf(th), -cosf(th));
            struct V { ImVec2 p, uv; };
            V in[4] = {{a, uv0}, {ImVec2(b.x, a.y), ImVec2(uv1.x, uv0.y)}, {b, uv1}, {ImVec2(a.x, b.y), ImVec2(uv0.x, uv1.y)}}, o[8];
            int n = 0;
            auto side = [&](const V& v) { return (v.p.x - pv.x) * up.x + (v.p.y - pv.y) * up.y; };
            for (int i = 0; i < 4; ++i) {
                const V& p = in[i]; const V& q = in[(i + 1) % 4];
                float sp = side(p), sq = side(q);
                if (sp >= 0) o[n++] = p;
                if ((sp >= 0) != (sq >= 0)) {
                    float t = sp / (sp - sq);
                    o[n++] = {ImVec2(p.p.x + (q.p.x - p.p.x) * t, p.p.y + (q.p.y - p.p.y) * t), ImVec2(p.uv.x + (q.uv.x - p.uv.x) * t, p.uv.y + (q.uv.y - p.uv.y) * t)};
                }
            }
            if (n >= 3) {
                dl->PushTextureID(gfx::Id(tx));
                dl->PrimReserve((n - 2) * 3, n);
                ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
                for (int i = 0; i < n; ++i) dl->PrimWriteVtx(o[i].p, o[i].uv, col);
                for (int i = 1; i + 1 < n; ++i) { dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + i)); dl->PrimWriteIdx((ImDrawIdx)(base + i + 1)); }
                dl->PopTextureID();
            }
        }
        else if (tx && e.circle) {   // clipped to the circle in its box: a fan, uv following the (turned) picture
            float r = e.rot * 3.14159265f / 180, cs = cosf(r), sn = sinf(r);
            ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
            float hw = (b.x - a.x) / 2, hh = (b.y - a.y) / 2, rad = std::min(hw, hh);
            const int N = 64;
            dl->PushTextureID(gfx::Id(tx));
            dl->PrimReserve(N * 3, N + 1);
            ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
            ImVec2 uvc((uv0.x + uv1.x) / 2, (uv0.y + uv1.y) / 2);
            dl->PrimWriteVtx(c, uvc, col);
            for (int i = 0; i < N; ++i) {
                float t = i * 6.2831853f / N, lx = cosf(t) * rad, ly = sinf(t) * rad;   // in the picture's own frame
                ImVec2 uv(uv0.x + (lx / (2 * hw) + 0.5f) * (uv1.x - uv0.x), uv0.y + (ly / (2 * hh) + 0.5f) * (uv1.y - uv0.y));
                dl->PrimWriteVtx(ImVec2(c.x + lx * cs - ly * sn, c.y + lx * sn + ly * cs), uv, col);
            }
            for (int i = 0; i < N; ++i) { dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i)); dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (i + 1) % N)); }
            dl->PopTextureID();
        }
        else if (tx) {
            auto quad = [&](ImU32 c4) {
                if (e.rot == 0) { dl->AddImage(gfx::Id(tx), a, b, uv0, uv1, c4); return; }
                float r = e.rot * 3.14159265f / 180, cs = cosf(r), sn = sinf(r);
                float ku = Unit();
                ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2), hw((b.x - a.x) / 2, (b.y - a.y) / 2);
                ImVec2 pv(c.x + e.pivX * ku, c.y + e.pivY * ku);   // turned about the pivot (corners relative to it)
                auto P = [&](float x, float y) { x += c.x - pv.x; y += c.y - pv.y; return ImVec2(pv.x + x * cs - y * sn, pv.y + x * sn + y * cs); };
                dl->AddImageQuad(gfx::Id(tx), P(-hw.x, -hw.y), P(hw.x, -hw.y), P(hw.x, hw.y), P(-hw.x, hw.y),
                                 uv0, ImVec2(uv1.x, uv0.y), uv1, ImVec2(uv0.x, uv1.y), c4);
            };
            quad(col);
            if (poseAdd) {   // a clip's colour +: its white silhouette over it, as opaque as the add (towards white / the add's tint)
                const float al = ImGui::ColorConvertU32ToFloat4(col).w;
                const float k = std::min(1.f, std::max(pose.add[0], std::max(pose.add[1], pose.add[2])));
                const float ca[4] = { std::min(1.f, pose.add[0] / k), std::min(1.f, pose.add[1] / k), std::min(1.f, pose.add[2] / k), al * k };
                if (gfx::Tex wt = e.texIdx >= 0 && !e.additive ? WhiteOf(e.texIdx) : nullptr) {
                    gfx::Tex keep = tx; tx = wt; quad(Col(ca)); tx = keep;
                } else {   // (no silhouette: the picture added on, where the backend can add)
                    const float cb[4] = { std::min(1.f, pose.add[0]), std::min(1.f, pose.add[1]), std::min(1.f, pose.add[2]), al };
                    if (!e.additive) dl->AddCallback(gfx::CbAdditive, nullptr);
                    quad(Col(cb));
                    if (!e.additive && !noAlpha) dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
                }
            }
        }
        if (tx && (e.additive || noAlpha)) dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    } else {
        if (e.fontIdx == -2) e.fontIdx = FindFont(e.font);
        std::string s = ElemText(e);
        if (editing && s.empty()) s = e.text;   // show the template where there's no value yet
        if (e.fontIdx >= 0 && e.wrap && e.w > 0 && e.scale > 0) s = WrapText(g_fonts[e.fontIdx], s, e.w / e.scale);
        if (e.fontIdx >= 0 && e.swapAnim && !editing && SwapText(dl, e, a, b, col, s)) return;
        if (e.fontIdx >= 0) {
            const int v0 = dl->VtxBuffer.Size;
            DrawText(dl, g_fonts[e.fontIdx], a, e.scale * Unit(), col, s, e.align, b.x - a.x);
            if (e.rot != 0) {   // turned (as the game's tilted menus): its glyphs' corners about its box's centre (+ pivot)
                const float r = e.rot * 3.14159265f / 180, cs = cosf(r), sn = sinf(r), ku = Unit();
                const ImVec2 pv((a.x + b.x) / 2 + e.pivX * ku, (a.y + b.y) / 2 + e.pivY * ku);
                for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
                    ImVec2& q = dl->VtxBuffer[i].pos;
                    const float x = q.x - pv.x, y = q.y - pv.y;
                    q = ImVec2(pv.x + x * cs - y * sn, pv.y + x * sn + y * cs);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- save / load
static std::string Esc(const std::string& s) { std::string o; for (char c : s) { if (c == '\n') o += "\\n"; else if (c == '\\') o += "\\\\"; else o += c; } return o; }
static std::string Unesc(const std::string& s) {
    std::string o; for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\\' && i + 1 < s.size()) { char n = s[++i]; o += n == 'n' ? '\n' : n; } else o += s[i]; } return o;
}
static void SaveMenus(FILE* f);
static void SaveLinks(FILE* f);
static void LoadLinkLine(const std::string& v, int intoMode);
static void LinksReset();
static void LoadMenusReset();
static int LoadMenuSection(const std::string& s);
static bool LoadMenuKey(int sect, const std::string& k, const std::string& v);
static void LoadMenuEnd();
// ---------------------------------------------------------------- colour tokens
// A token is a colour by its role ("hud.accent"): elements that use it follow it, so a look is changed in one place.
// Built-in ones are the game's own HUD palette; a token can also follow a live value giving "R G B" (0-255, as
// tint keys do: the car's HUD paints). Other mods register theirs (REUI_SetColorToken). Saved in the layout as
// token=<name>;<r g b a>;<live key>;<label> (built-in ones only when changed).
struct ColorToken {
    std::string name, label, live;
    float col[4] = {1, 1, 1, 1}, def[4] = {1, 1, 1, 1};
    std::string defLive;
    bool builtIn = false, fromMod = false;
    float now[4] = {1, 1, 1, 1};   // this frame's colour (live value applied)
};
static std::vector<ColorToken> g_tokens;
static ColorToken* TokenOf(const std::string& n) { for (auto& t : g_tokens) if (t.name == n) return &t; return nullptr; }
static void TokenDefine(const std::string& n, const std::string& label, float r, float g, float b, float a, const std::string& live, bool builtIn, bool fromMod) {
    ColorToken* t = TokenOf(n);
    bool fresh = !t;
    if (fresh) { g_tokens.push_back(ColorToken()); t = &g_tokens.back(); t->name = n; }
    t->label = label; t->def[0] = r; t->def[1] = g; t->def[2] = b; t->def[3] = a; t->defLive = live;
    t->builtIn |= builtIn; t->fromMod |= fromMod;
    if (fresh) { memcpy(t->col, t->def, sizeof t->col); t->live = live; memcpy(t->now, t->col, sizeof t->now); }
}
static void TokenBuiltIns() {
    TokenDefine("hud.text", "HUD text grey (192 192 192)", 0.7529f, 0.7529f, 0.7529f, 1, "", true, false);
    TokenDefine("hud.accent", "HUD green: the game's player colour (202 244 138)", 0.7922f, 0.9569f, 0.5412f, 1, "", true, false);
    TokenDefine("hud.accent_soft", "HUD pale green (197 218 165)", 0.7725f, 0.8549f, 0.6471f, 1, "", true, false);
    TokenDefine("hud.line", "lines and dividers (225 225 225)", 0.8824f, 0.8824f, 0.8824f, 1, "", true, false);
    TokenDefine("hud.shade", "dark panels (black; elements give the opacity)", 0, 0, 0, 1, "", true, false);
    TokenDefine("hud.orange", "HUD orange: minimap markers, north (207 128 48)", 0.8118f, 0.502f, 0.1882f, 1, "", true, false);
    TokenDefine("hud.red", "tach redline red (117 11 11)", 0.4588f, 0.0431f, 0.0431f, 1, "", true, false);
    TokenDefine("hud.bright_green", "bright green (99 178 34)", 0.3882f, 0.698f, 0.1333f, 1, "", true, false);
    for (int i = 1; i <= 3; ++i)
        TokenDefine("car.paint" + std::to_string(i), "the car's HUD paint " + std::to_string(i) + " (live: tach_paint_" + std::to_string(i) + ")", 1, 1, 1, 1, "tach_paint_" + std::to_string(i), true, false);
}
static bool TokenModified(const ColorToken& t) { return memcmp(t.col, t.def, sizeof t.col) != 0 || t.live != t.defLive; }
static std::string TokenLine(const ColorToken& t) {
    char b[96]; snprintf(b, sizeof b, "%g %g %g %g", t.col[0], t.col[1], t.col[2], t.col[3]);
    std::string lab = t.label; for (char& c : lab) if (c == ';' || c == '\n' || c == '\r') c = ',';
    return t.name + ";" + b + ";" + t.live + ";" + lab;
}
static bool TokenParse(const std::string& v, ColorToken& t) {   // "name;r g b a;live;label"
    size_t a = v.find(';'); if (a == std::string::npos || a == 0) return false;
    size_t b = v.find(';', a + 1), c = b == std::string::npos ? std::string::npos : v.find(';', b + 1);
    t.name = v.substr(0, a);
    std::string cs = v.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    if (sscanf(cs.c_str(), "%f %f %f %f", &t.col[0], &t.col[1], &t.col[2], &t.col[3]) != 4) return false;
    t.live = b == std::string::npos ? "" : v.substr(b + 1, c == std::string::npos ? std::string::npos : c - b - 1);
    t.label = c == std::string::npos ? "" : v.substr(c + 1);
    return true;
}
static void TokenLoad(const ColorToken& in) {   // a token= line from the layout / a prefab (layout: wins over defaults)
    ColorToken* t = TokenOf(in.name);
    if (!t) { g_tokens.push_back(in); t = &g_tokens.back(); memcpy(t->def, in.col, sizeof t->def); t->defLive = in.live; }
    else { memcpy(t->col, in.col, sizeof t->col); t->live = in.live; if (!t->builtIn && !t->fromMod && !in.label.empty()) t->label = in.label; }
}
static void GradTokensReset();
static void TokensReset() {   // before a layout is read: built-in / mod tokens back to their defaults, the layout's own gone
    std::vector<ColorToken> keep;
    for (auto& t : g_tokens) if (t.builtIn || t.fromMod) { ColorToken k = t; memcpy(k.col, k.def, sizeof k.col); k.live = k.defLive; keep.push_back(k); }
    g_tokens = keep;
    TokenBuiltIns();
    GradTokensReset();
}
struct GradToken { std::string name, label; std::vector<UStop> stops, def; bool builtIn = false; };   // (gradient tokens, below)
static std::vector<GradToken> g_gradTokens;
static int TokenUses(const std::string& n) {
    int u = 0;
    for (auto& l : g_layout) for (auto& e : l) u += (e.colTok == n) + (e.hiTok == n) + (e.fillTok == n);
    auto stops = [&](const std::vector<UStop>& v) { for (auto& st : v) u += st.tok == n; };   // gradient stops
    for (auto& l : g_layout) for (auto& e : l) { stops(e.strokeGrad.stops); stops(e.fillGrad.stops); }
    for (auto& t : g_gradTokens) stops(t.stops);
    return u;
}
// every frame, before anything is drawn (menus copy elements after this): tokens' live values, then their users
static void ResolveTokens() {
    if (g_tokens.empty()) TokenBuiltIns();
    for (auto& t : g_tokens) {
        memcpy(t.now, t.col, sizeof t.now);
        std::string v; int r, g, b;
        if (!t.live.empty() && tokens && tokens(t.live, v) && sscanf(v.c_str(), "%d %d %d", &r, &g, &b) == 3) { t.now[0] = r / 255.f; t.now[1] = g / 255.f; t.now[2] = b / 255.f; }
    }
    auto apply = [](const std::string& n, float a, float* out) {
        if (n.empty()) return;
        ColorToken* t = TokenOf(n);
        if (!t) return;   // (an unknown token: its saved colour stays)
        out[0] = t->now[0]; out[1] = t->now[1]; out[2] = t->now[2]; out[3] = std::min(1.f, t->now[3] * a);
    };
    auto each = [&](Elem& e) { apply(e.colTok, e.colTokA, e.col); apply(e.hiTok, e.hiTokA, e.hiCol); apply(e.fillTok, e.fillTokA, e.fillCol); };
    for (auto& l : g_layout) for (auto& e : l) each(e);
}
static void ApplyTokSwap(Elem& e) {   // (see g_tokSwap)
    auto one = [](const std::string& n, float a, float* out) {
        const std::string& to = SwapTok(n);
        if (&to == &n) return;
        if (ColorToken* t = TokenOf(to)) { out[0] = t->now[0]; out[1] = t->now[1]; out[2] = t->now[2]; out[3] = std::min(1.f, t->now[3] * a); }
    };
    one(e.colTok, e.colTokA, e.col); one(e.hiTok, e.hiTokA, e.hiCol); one(e.fillTok, e.fillTokA, e.fillCol);
}
void AddColorToken(const std::string& name, const std::string& label, const float rgba[4]) {
    if (name.empty()) return;
    if (g_tokens.empty()) TokenBuiltIns();
    ColorToken* t = TokenOf(name);
    if (t && t->builtIn) return;   // (a mod can't redefine the game's palette)
    TokenDefine(name, label, rgba[0], rgba[1], rgba[2], rgba[3], "", false, true);
}
// the colour edit of an element: its own colour, or a token (then its opacity, and the token's colour itself)
// ---- gradient tokens: a named set of stops ("hud.glow"); shapes use it by name and follow it. Its stops can use colour
// tokens. Saved as gradtoken=<name>;<label>;<stops> (stops "t r g b a[@colour token]" separated by |).
static GradToken* GradTokenOf(const std::string& n) { for (auto& t : g_gradTokens) if (t.name == n) return &t; return nullptr; }
static UStop MkStop(float t, const char* tok, float a, float r = 1, float g = 1, float b = 1) { UStop s; s.t = t; s.c[0] = r; s.c[1] = g; s.c[2] = b; s.c[3] = a; if (tok) s.tok = tok; return s; }
static void GradBuiltIns() {
    auto def = [](const char* n, const char* label, std::vector<UStop> st) {
        if (GradTokenOf(n)) return;
        GradToken t; t.name = n; t.label = label; t.stops = t.def = st; t.builtIn = true; g_gradTokens.push_back(t);
    };
    def("hud.glow", "clear -> HUD green -> clear (a soft line across a wide stroke)", {MkStop(0, "hud.accent", 0), MkStop(0.4f, "hud.accent", 1), MkStop(0.51f, "hud.accent", 1), MkStop(1, "hud.accent", 0)});
    def("hud.fade", "dark panel fading out", {MkStop(0, "hud.shade", 1), MkStop(1, "hud.shade", 0)});
    def("hud.edge_light", "a light edge fading inward (with \"from the edge\")", {MkStop(0, "hud.line", 0.8f), MkStop(1, "hud.line", 0)});
}
static bool SameStops(const std::vector<UStop>& a, const std::vector<UStop>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].t != b[i].t || a[i].tok != b[i].tok || memcmp(a[i].c, b[i].c, sizeof a[i].c) != 0) return false;
    return true;
}
static std::string StopText(const UStop& st) {
    char b[96]; snprintf(b, sizeof b, "%g %g %g %g %g", st.t, st.c[0], st.c[1], st.c[2], st.c[3]);
    return st.tok.empty() ? std::string(b) : std::string(b) + "@" + st.tok;
}
static bool StopParse(const std::string& s, UStop& st) {
    if (sscanf(s.c_str(), "%f %f %f %f %f", &st.t, &st.c[0], &st.c[1], &st.c[2], &st.c[3]) != 5) return false;
    size_t at = s.find('@'); st.tok = at == std::string::npos ? "" : s.substr(at + 1);
    while (!st.tok.empty() && (st.tok.back() == ' ' || st.tok.back() == '\r')) st.tok.pop_back();
    return true;
}
static void SortStops(std::vector<UStop>& v) { std::stable_sort(v.begin(), v.end(), [](const UStop& a, const UStop& b) { return a.t < b.t; }); }
static std::string GradTokenLine(const GradToken& t) {
    std::string lab = t.label; for (char& c : lab) if (c == ';' || c == '|' || c == '\n') c = ',';
    std::string l = t.name + ";" + lab + ";";
    for (size_t i = 0; i < t.stops.size(); ++i) l += (i ? "|" : "") + StopText(t.stops[i]);
    return l;
}
static bool GradTokenParse(const std::string& v, GradToken& t) {
    size_t a = v.find(';'), b = a == std::string::npos ? a : v.find(';', a + 1);
    if (a == std::string::npos || a == 0 || b == std::string::npos) return false;
    t.name = v.substr(0, a); t.label = v.substr(a + 1, b - a - 1);
    std::string s = v.substr(b + 1);
    for (size_t p = 0; p <= s.size();) {
        size_t q = s.find('|', p); if (q == std::string::npos) q = s.size();
        UStop st; if (q > p && StopParse(s.substr(p, q - p), st)) t.stops.push_back(st);
        p = q + 1;
    }
    SortStops(t.stops);
    return t.stops.size() >= 1;
}
static void GradTokenLoad(const GradToken& in) {
    GradToken* t = GradTokenOf(in.name);
    if (!t) { GradToken n = in; n.def = in.stops; n.builtIn = false; g_gradTokens.push_back(n); }
    else { t->stops = in.stops; if (!t->builtIn && !in.label.empty()) t->label = in.label; }
}
static void GradTokensReset() {
    std::vector<GradToken> keep;
    for (auto& t : g_gradTokens) if (t.builtIn) { GradToken k = t; k.stops = k.def; keep.push_back(k); }
    g_gradTokens = keep;
    GradBuiltIns();
}
static int GradTokenUses(const std::string& n) {
    int u = 0;
    for (auto& l : g_layout) for (auto& e : l) u += (e.strokeGrad.tok == n) + (e.fillGrad.tok == n);
    return u;
}
// the gradient to draw: its stops (or its token's), colour tokens' colours now; false: no gradient
static void StopColour(const UStop& st, float out[4]) {
    memcpy(out, st.c, sizeof st.c);
    if (!st.tok.empty()) if (ColorToken* t = TokenOf(SwapTok(st.tok))) { out[0] = t->now[0]; out[1] = t->now[1]; out[2] = t->now[2]; out[3] = st.c[3] * t->now[3]; }
}
static bool ResolveGrad(const UGrad& g, float depthScale, shapes::Gradient& out) {
    out = shapes::Gradient();
    if (!g.mode) return false;
    if (g_gradTokens.empty()) GradBuiltIns();
    const std::vector<UStop>* st = &g.stops;
    if (!g.tok.empty()) if (GradToken* t = GradTokenOf(SwapTok(g.tok))) st = &t->stops;
    if (st->empty()) return false;
    out.mode = g.mode; out.angle = g.angle; out.depth = g.depth * depthScale;
    for (auto& s2 : *st) { float c[4]; StopColour(s2, c); out.stops.push_back({s2.t, c[0], c[1], c[2], c[3]}); }
    std::stable_sort(out.stops.begin(), out.stops.end(), [](const std::array<float, 5>& a, const std::array<float, 5>& b) { return a[0] < b[0]; });
    return true;
}
static void TokenSwatch(const ColorToken& t, float size = 0) {
    ImGui::ColorButton(("##sw" + t.name).c_str(), ImVec4(t.now[0], t.now[1], t.now[2], t.now[3]), ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
                       size > 0 ? ImVec2(size, size) : ImVec2(0, 0));
}
static bool TokenColourEdit(const char* label, float* col, std::string& tok, float& tokA) {
    bool changed = false;
    ImGui::PushID(label);
    ColorToken* t = tok.empty() ? nullptr : TokenOf(tok);
    if (!t) {
        if (ImGui::ColorEdit4(label, col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) changed = true;
        if (!tok.empty()) { ImGui::SameLine(); ImGui::TextDisabled("(token %s isn't defined: its saved colour)", tok.c_str()); }
    } else {
        if (ImGui::ColorEdit4("##tokcol", t->col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The token's colour: changes every element that uses %s.", t->name.c_str());
        ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##toka", &tokA, 0.005f, 0.f, 4.f, "x%.2f")) { tokA = std::max(0.f, tokA); changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("This element's opacity, times the token's.");
        ImGui::SameLine(); ImGui::Text("%s", label);
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##tok", tok.empty() ? "own colour" : tok.c_str())) {
        if (ImGui::Selectable("own colour", tok.empty()) && !tok.empty()) { tok.clear(); changed = true; }   // (keeps what it shows now)
        for (auto& o : g_tokens) {
            TokenSwatch(o, ImGui::GetTextLineHeight()); ImGui::SameLine();
            if (ImGui::Selectable(o.name.c_str(), tok == o.name)) {
                tok = o.name; tokA = o.col[3] > 0.001f ? std::min(4.f, col[3] / o.col[3]) : 1.f; changed = true;
            }
            if (ImGui::IsItemHovered() && !o.label.empty()) ImGui::SetTooltip("%s", o.label.c_str());
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A colour token: the colour by its role, shared with every element using it (Colour tokens below).");
    ImGui::PopID();
    if (changed) { g_dirty = true; ResolveTokens(); }
    return changed;
}
// "Colour tokens": the table, adding one, and turning repeated colours in the layout into tokens
static bool StopsEdit(const char* id, std::vector<UStop>& stops, const float baseCol[4], const char* baseTok);
static void TokensPanel() {
    if (!ImGui::CollapsingHeader(("Colour tokens (" + std::to_string(g_tokens.size()) + ")###coltok").c_str())) return;
    ImGui::TextDisabled("A colour by its role: elements using a token follow it, so the look changes in one place. A token can\nfollow a live value giving \"R G B\" (0-255), like the car's HUD paints.");
    std::string drop;
    if (ImGui::BeginTable("tok", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("colour"); ImGui::TableSetupColumn("token"); ImGui::TableSetupColumn("used");
        ImGui::TableSetupColumn("live value"); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (auto& t : g_tokens) {
            ImGui::PushID(t.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::ColorEdit4("##c", t.col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) { g_dirty = true; ResolveTokens(); }
            if (!t.live.empty() && memcmp(t.now, t.col, 3 * sizeof(float)) != 0) { ImGui::SameLine(); TokenSwatch(t); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Now (from %s)", t.live.c_str()); }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(t.name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s%s", t.label.c_str(), t.builtIn ? "\n(built in)" : t.fromMod ? "\n(from another mod)" : "", "");
            ImGui::TableNextColumn(); ImGui::Text("%d", TokenUses(t.name));
            ImGui::TableNextColumn();
            char lb[64]; snprintf(lb, sizeof lb, "%s", t.live.c_str());
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputTextWithHint("##live", "(none)", lb, sizeof lb)) { t.live = lb; g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A value key giving \"R G B\" (0-255): the colour follows it while it's set (tach_paint_1, sms_color, ...).");
            ImGui::TableNextColumn();
            if ((t.builtIn || t.fromMod) && TokenModified(t)) { if (ImGui::SmallButton("reset")) { memcpy(t.col, t.def, sizeof t.col); t.live = t.defLive; g_dirty = true; ResolveTokens(); } }
            else if (!t.builtIn && !t.fromMod) { if (ImGui::SmallButton("delete")) drop = t.name; if (ImGui::IsItemHovered()) ImGui::SetTooltip("Elements using it keep the colour they show now."); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!drop.empty()) {   // its users keep their resolved colour, as their own
        for (auto& l : g_layout) for (auto& e : l) { if (e.colTok == drop) e.colTok.clear(); if (e.hiTok == drop) e.hiTok.clear(); if (e.fillTok == drop) e.fillTok.clear(); }
        auto unstop = [&](std::vector<UStop>& v) { for (auto& st : v) if (st.tok == drop) { StopColour(st, st.c); st.tok.clear(); } };
        for (auto& l : g_layout) for (auto& e : l) { unstop(e.strokeGrad.stops); unstop(e.fillGrad.stops); }
        for (auto& t : g_gradTokens) unstop(t.stops);
        g_tokens.erase(std::remove_if(g_tokens.begin(), g_tokens.end(), [&](const ColorToken& t) { return t.name == drop; }), g_tokens.end());
        g_dirty = true;
    }
    {   // a new one
        static char nb[48] = ""; static float nc[4] = {1, 1, 1, 1};
        ImGui::ColorEdit4("##newc", nc, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs); ImGui::SameLine();
        ImGui::SetNextItemWidth(160); ImGui::InputTextWithHint("##newn", "my.colour", nb, sizeof nb); ImGui::SameLine();
        std::string n = nb; n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == ' ' || c == ';'; }), n.end());
        ImGui::BeginDisabled(n.empty() || TokenOf(n));
        if (ImGui::Button("Add token")) { TokenDefine(n, "", nc[0], nc[1], nc[2], nc[3], "", false, false); ColorToken* t = TokenOf(n); t->builtIn = t->fromMod = false; nb[0] = 0; g_dirty = true; }
        ImGui::EndDisabled();
    }
    // gradient tokens: named stops shapes use by name
    if (g_gradTokens.empty()) GradBuiltIns();
    if (ImGui::TreeNode(("Gradient tokens (" + std::to_string(g_gradTokens.size()) + ")###gradtok").c_str())) {
        std::string gdrop;
        for (auto& t : g_gradTokens) {
            ImGui::PushID(t.name.c_str());
            ImVec2 p0 = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
            shapes::Gradient rg; UGrad tmp; tmp.mode = 1; tmp.tok = t.name; ResolveGrad(tmp, 1, rg);   // a small preview bar
            for (int i = 0; i < 24; ++i) {
                auto col = [&](float x) { float c[4] = {1, 1, 1, 0};
                    const auto& v = rg.stops;
                    if (v.empty()) return IM_COL32(0, 0, 0, 0);
                    if (x <= v.front()[0]) { for (int k = 0; k < 4; ++k) c[k] = v.front()[1 + k]; }
                    else if (x >= v.back()[0]) { for (int k = 0; k < 4; ++k) c[k] = v.back()[1 + k]; }
                    else for (size_t j = 0; j + 1 < v.size(); ++j) if (x >= v[j][0] && x <= v[j + 1][0]) { float sp = v[j + 1][0] - v[j][0], f = sp > 1e-6f ? (x - v[j][0]) / sp : 0; for (int k = 0; k < 4; ++k) c[k] = v[j][1 + k] + (v[j + 1][1 + k] - v[j][1 + k]) * f; break; }
                    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3])); };
                ImU32 ca = col(i / 24.f), cb = col((i + 1) / 24.f);
                dl->AddRectFilled(ImVec2(p0.x + i * 4, p0.y + 2), ImVec2(p0.x + i * 4 + 4, p0.y + 14), ((i / 2) & 1) ? IM_COL32(200, 200, 200, 255) : IM_COL32(140, 140, 140, 255));
                dl->AddRectFilledMultiColor(ImVec2(p0.x + i * 4, p0.y + 2), ImVec2(p0.x + i * 4 + 4, p0.y + 14), ca, cb, cb, ca);
            }
            ImGui::Dummy(ImVec2(96, 16)); ImGui::SameLine();
            bool open = ImGui::TreeNode("##edit", "%s  (%d use%s)%s", t.name.c_str(), GradTokenUses(t.name), GradTokenUses(t.name) == 1 ? "" : "s", t.builtIn ? "  built in" : "");
            if (ImGui::IsItemHovered() && !t.label.empty()) ImGui::SetTooltip("%s", t.label.c_str());
            if (t.builtIn && !SameStops(t.stops, t.def)) { ImGui::SameLine(); if (ImGui::SmallButton("reset")) { t.stops = t.def; g_dirty = true; } }
            if (!t.builtIn) { ImGui::SameLine(); if (ImGui::SmallButton("delete")) gdrop = t.name; if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shapes using it keep its stops as their own."); }
            if (open) { const float w4[4] = {1, 1, 1, 1}; StopsEdit("stops", t.stops, w4, nullptr); ImGui::TreePop(); }
            ImGui::PopID();
        }
        if (!gdrop.empty()) {   // users keep the stops, as their own
            GradToken* t = GradTokenOf(gdrop);
            auto keep = [&](UGrad& g) { if (g.tok == gdrop) { if (t) g.stops = t->stops; g.tok.clear(); } };
            for (auto& l : g_layout) for (auto& e : l) { keep(e.strokeGrad); keep(e.fillGrad); }
            g_gradTokens.erase(std::remove_if(g_gradTokens.begin(), g_gradTokens.end(), [&](const GradToken& x) { return x.name == gdrop; }), g_gradTokens.end());
            g_dirty = true;
        }
        static char gn[48] = "";
        ImGui::SetNextItemWidth(160); ImGui::InputTextWithHint("##newg", "my.gradient", gn, sizeof gn); ImGui::SameLine();
        std::string n = gn; n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == ' ' || c == ';' || c == '|'; }), n.end());
        ImGui::BeginDisabled(n.empty() || GradTokenOf(n));
        if (ImGui::Button("Add gradient token")) {
            GradToken t; t.name = n; t.stops = t.def = {MkStop(0, "hud.accent", 1), MkStop(1, "hud.accent", 0)};
            g_gradTokens.push_back(t); gn[0] = 0; g_dirty = true;
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    // repeated colours -> tokens: elements (every layout, the menus, templates) with the same colour as their own,
    // white images left out (white = the texture as it is)
    if (ImGui::TreeNode("Repeated colours in this layout file")) {
        struct Use { Elem* e; bool hi; };
        struct Group { int r, g, b; std::vector<Use> uses; };
        std::vector<Group> groups;
        auto add = [&](Elem& e, bool hi) {
            const float* c = hi ? e.hiCol : e.col;
            int r = (int)lroundf(c[0] * 255), g = (int)lroundf(c[1] * 255), b = (int)lroundf(c[2] * 255);
            if (!hi && e.type == E_IMAGE && r == 255 && g == 255 && b == 255) return;
            for (auto& gr : groups) if (abs(gr.r - r) <= 1 && abs(gr.g - g) <= 1 && abs(gr.b - b) <= 1) { gr.uses.push_back({&e, hi}); return; }
            groups.push_back({r, g, b, {{&e, hi}}});
        };
        auto scan = [&](Elem& e) { if (e.colTok.empty()) add(e, false); if (e.hiMine && e.hiTok.empty()) add(e, true); };
        for (auto& l : g_layout) for (auto& e : l) scan(e);
        std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.uses.size() > b.uses.size(); });
        int shown = 0;
        for (auto& gr : groups) {
            if (gr.uses.size() < 2) continue;
            ++shown;
            ImGui::PushID(shown);
            ImGui::ColorButton("##g", ImVec4(gr.r / 255.f, gr.g / 255.f, gr.b / 255.f, 1)); ImGui::SameLine();
            ImGui::Text("%3d %3d %3d  x%d", gr.r, gr.g, gr.b, (int)gr.uses.size()); ImGui::SameLine();
            ColorToken* match = nullptr;   // a token with the same colour
            for (auto& t : g_tokens) if (abs((int)lroundf(t.col[0] * 255) - gr.r) <= 2 && abs((int)lroundf(t.col[1] * 255) - gr.g) <= 2 && abs((int)lroundf(t.col[2] * 255) - gr.b) <= 2) { match = &t; break; }
            static char nb[64][48];
            char* buf = nb[shown % 64];
            std::string use;
            if (match) { if (ImGui::SmallButton(("use " + match->name).c_str())) use = match->name; }
            else {
                ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##n", "new token name", buf, 48); ImGui::SameLine();
                std::string n = buf; n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == ' ' || c == ';'; }), n.end());
                ImGui::BeginDisabled(n.empty() || TokenOf(n));
                if (ImGui::SmallButton("make token")) { TokenDefine(n, "", gr.r / 255.f, gr.g / 255.f, gr.b / 255.f, 1, "", false, false); ColorToken* t = TokenOf(n); t->builtIn = t->fromMod = false; use = n; buf[0] = 0; }
                ImGui::EndDisabled();
            }
            if (ImGui::IsItemHovered()) {
                std::string names; int k = 0;
                for (auto& u : gr.uses) { if (k++ == 12) { names += "\n..."; break; } names += "\n" + u.e->name + (u.hi ? " (my-row colour)" : ""); }
                ImGui::SetTooltip("Every element with this colour follows the token (each keeps its own opacity):%s", names.c_str());
            }
            if (!use.empty()) {
                ColorToken* t = TokenOf(use);
                float ta = t && t->col[3] > 0.001f ? t->col[3] : 1.f;
                for (auto& u : gr.uses) {
                    if (u.hi) { u.e->hiTok = use; u.e->hiTokA = u.e->hiCol[3] / ta; }
                    else { u.e->colTok = use; u.e->colTokA = u.e->col[3] / ta; }
                }
                g_dirty = true; ResolveTokens();
                Log("ui editor: %d elements now use colour token %s", (int)gr.uses.size(), use.c_str());
            }
            ImGui::PopID();
        }
        if (!shown) ImGui::TextDisabled("No colour is repeated outside the tokens.");
        ImGui::TreePop();
    }
}
// A gradient's stops on a bar: click a stop to pick it, drag it along, click the bar to add one (its colour there);
// the picked one's colour (or colour token) / opacity / location below.
static bool StopsEdit(const char* id, std::vector<UStop>& stops, const float baseCol[4], const char* baseTok) {
    bool ch = false;
    ImGui::PushID(id);
    static std::map<std::string, int> picked;   // the picked stop per bar
    std::string me = std::string(id) + "@" + std::to_string((uintptr_t)&stops);
    int& sel = picked[me];
    if (stops.size() < 2) { stops = {MkStop(0, baseTok, baseCol[3], baseCol[0], baseCol[1], baseCol[2]), MkStop(1, baseTok, 0, baseCol[0], baseCol[1], baseCol[2])}; ch = true; }
    sel = std::max(0, std::min((int)stops.size() - 1, sel));
    auto colourAt = [&](float t, float c[4]) {   // as drawn (colour tokens now)
        float a[4], b[4];
        if (t <= stops.front().t) { StopColour(stops.front(), c); return; }
        if (t >= stops.back().t) { StopColour(stops.back(), c); return; }
        for (size_t j = 0; j + 1 < stops.size(); ++j) if (t >= stops[j].t && t <= stops[j + 1].t) {
            StopColour(stops[j], a); StopColour(stops[j + 1], b);
            float sp = stops[j + 1].t - stops[j].t, f = sp > 1e-6f ? (t - stops[j].t) / sp : 0;
            for (int i = 0; i < 4; ++i) c[i] = a[i] + (b[i] - a[i]) * f;
            return;
        }
        StopColour(stops.back(), c);
    };
    auto resort = [&](int& keepIdx) {   // sorted again, keepIdx following its stop
        if (keepIdx < 0 || keepIdx >= (int)stops.size()) { SortStops(stops); return; }
        UStop keep = stops[keepIdx];
        SortStops(stops);
        for (int i = 0; i < (int)stops.size(); ++i) if (stops[i].t == keep.t && stops[i].tok == keep.tok && !memcmp(stops[i].c, keep.c, sizeof keep.c)) { keepIdx = i; break; }
    };
    float W = 300, H = 18, hs = 7;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int cx = 0; cx * 6 < W; ++cx) for (int cy = 0; cy * 6 < H; ++cy)   // checker under the colours
        dl->AddRectFilled(ImVec2(p0.x + cx * 6, p0.y + cy * 6), ImVec2(std::min(p0.x + W, p0.x + cx * 6 + 6), std::min(p0.y + H, p0.y + cy * 6 + 6)), ((cx + cy) & 1) ? IM_COL32(200, 200, 200, 255) : IM_COL32(140, 140, 140, 255));
    for (int i = 0; i < 60; ++i) {
        float c0[4], c1[4]; colourAt(i / 60.f, c0); colourAt((i + 1) / 60.f, c1);
        ImU32 ca = ImGui::ColorConvertFloat4ToU32(ImVec4(c0[0], c0[1], c0[2], c0[3])), cb = ImGui::ColorConvertFloat4ToU32(ImVec4(c1[0], c1[1], c1[2], c1[3]));
        dl->AddRectFilledMultiColor(ImVec2(p0.x + W * i / 60, p0.y), ImVec2(p0.x + W * (i + 1) / 60, p0.y + H), ca, cb, cb, ca);
    }
    dl->AddRect(p0, ImVec2(p0.x + W, p0.y + H), IM_COL32(0, 0, 0, 160));
    for (int i = 0; i < (int)stops.size(); ++i) {   // the stops: little houses under the bar
        float x = p0.x + stops[i].t * W, y = p0.y + H + 1, c[4]; StopColour(stops[i], c);
        ImVec2 pts[5] = {ImVec2(x, y), ImVec2(x + hs, y + hs), ImVec2(x + hs, y + 2 * hs), ImVec2(x - hs, y + 2 * hs), ImVec2(x - hs, y + hs)};
        dl->AddConvexPolyFilled(pts, 5, ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], 1)));
        dl->AddPolyline(pts, 5, i == sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, i == sel ? 2.f : 1.f);
        if (!stops[i].tok.empty()) dl->AddCircleFilled(ImVec2(x, y + 1.6f * hs), 2, IM_COL32(0, 0, 0, 220));   // (a dot: a colour token)
    }
    ImGui::InvisibleButton("bar", ImVec2(W, H + 2 * hs + 2));
    static int dragging = -1; static std::string dragFor;
    if (ImGui::IsItemActivated()) {
        float mx = ImGui::GetIO().MousePos.x - p0.x, my = ImGui::GetIO().MousePos.y - p0.y;
        int hit = -1; float bd = hs + 2;
        for (int i = 0; i < (int)stops.size(); ++i) { float d = fabsf(stops[i].t * W - mx); if (d < bd && my > H - 2) { bd = d; hit = i; } }
        if (hit < 0 && my <= H) {   // a new stop with the colour there (the token of its neighbour, if both sides share one)
            float t = Clamp01f(mx / W), c[4]; colourAt(t, c);
            UStop ns; ns.t = t; memcpy(ns.c, c, sizeof c);
            for (size_t j = 0; j + 1 < stops.size(); ++j)
                if (t >= stops[j].t && t <= stops[j + 1].t && !stops[j].tok.empty() && stops[j].tok == stops[j + 1].tok) {
                    ns.tok = stops[j].tok;
                    float sp = stops[j + 1].t - stops[j].t, f = sp > 1e-6f ? (t - stops[j].t) / sp : 0;
                    ns.c[3] = stops[j].c[3] + (stops[j + 1].c[3] - stops[j].c[3]) * f;
                }
            stops.push_back(ns); hit = (int)stops.size() - 1; resort(hit); ch = true;
        }
        if (hit >= 0) { sel = hit; dragging = hit; dragFor = me; }
    }
    if (ImGui::IsItemActive() && dragging >= 0 && dragFor == me && dragging < (int)stops.size()) {
        float t = Clamp01f((ImGui::GetIO().MousePos.x - p0.x) / W);
        if (t != stops[dragging].t) { stops[dragging].t = t; resort(dragging); sel = dragging; ch = true; }
    }
    if (ImGui::IsItemDeactivated() && dragFor == me) dragging = -1;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a stop to pick it, drag it along; click the bar to add a stop. A dot = the stop uses a colour token.");
    UStop& st = stops[sel];
    if (st.tok.empty()) {
        if (ImGui::ColorEdit4("##stopc", st.c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) ch = true;
    } else if (ColorToken* t = TokenOf(st.tok)) TokenSwatch(*t);
    ImGui::SameLine(); ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo("##stoptok", st.tok.empty() ? "own colour" : st.tok.c_str())) {
        if (ImGui::Selectable("own colour", st.tok.empty()) && !st.tok.empty()) { StopColour(st, st.c); st.tok.clear(); ch = true; }   // (keeps what it shows)
        for (auto& o : g_tokens) {
            TokenSwatch(o, ImGui::GetTextLineHeight()); ImGui::SameLine();
            if (ImGui::Selectable(o.name.c_str(), st.tok == o.name)) { st.tok = o.name; ch = true; }
            if (ImGui::IsItemHovered() && !o.label.empty()) ImGui::SetTooltip("%s", o.label.c_str());
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The stop's colour: its own, or a colour token's (it follows the token; the opacity is the stop's).");
    ImGui::SameLine(); ImGui::SetNextItemWidth(70);
    float op = st.c[3] * 100;
    if (ImGui::DragFloat("opacity", &op, 0.5f, 0.f, 100.f, "%.0f%%")) { st.c[3] = Clamp01f(op / 100); ch = true; }
    ImGui::SameLine(); ImGui::SetNextItemWidth(70);
    float loc = st.t * 100;
    if (ImGui::DragFloat("location", &loc, 0.2f, 0.f, 100.f, "%.1f%%")) { st.t = Clamp01f(loc / 100); resort(sel); ch = true; }
    ImGui::SameLine(); ImGui::BeginDisabled(stops.size() <= 2);
    if (ImGui::SmallButton("delete stop")) { stops.erase(stops.begin() + sel); sel = std::max(0, sel - 1); ch = true; }
    ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::SmallButton("reverse")) { for (auto& s2 : stops) s2.t = 1 - s2.t; std::reverse(stops.begin(), stops.end()); ch = true; }
    ImGui::PopID();
    if (ch) g_dirty = true;
    return ch;
}
// a shape's gradient: its own stops or a gradient token's (then the bar edits the token, for every shape using it)
static bool GradientEdit(const char* id, UGrad& g, const float baseCol[4], const char* baseTok) {
    bool ch = false;
    ImGui::PushID(id);
    if (g_gradTokens.empty()) GradBuiltIns();
    GradToken* t = g.tok.empty() ? nullptr : GradTokenOf(g.tok);
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("##gtok", t ? t->name.c_str() : g.tok.empty() ? "own stops" : (g.tok + " (missing)").c_str())) {
        if (ImGui::Selectable("own stops", g.tok.empty()) && !g.tok.empty()) { if (t) g.stops = t->stops; g.tok.clear(); ch = true; }   // (a copy to change)
        for (auto& o : g_gradTokens) {
            if (ImGui::Selectable(o.name.c_str(), g.tok == o.name)) { g.tok = o.name; ch = true; }
            if (ImGui::IsItemHovered() && !o.label.empty()) ImGui::SetTooltip("%s", o.label.c_str());
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A gradient token: named stops shared by every shape that uses it (Colour tokens > Gradient tokens).");
    t = g.tok.empty() ? nullptr : GradTokenOf(g.tok);
    if (t) {
        int u = GradTokenUses(t->name);
        ImGui::SameLine(); ImGui::TextDisabled("editing %s: %d use%s", t->name.c_str(), u, u == 1 ? "" : "s");
        if (StopsEdit("stops", t->stops, baseCol, baseTok)) ch = true;
    } else {
        static char nb[48] = "";
        ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::InputTextWithHint("##gname", "my.gradient", nb, sizeof nb);
        std::string n = nb; n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == ' ' || c == ';' || c == '|'; }), n.end());
        ImGui::SameLine(); ImGui::BeginDisabled(n.empty() || GradTokenOf(n) || g.stops.size() < 2);
        if (ImGui::SmallButton("save as token")) {
            GradToken nt; nt.name = n; nt.stops = nt.def = g.stops; g_gradTokens.push_back(nt);
            g.tok = n; nb[0] = 0; ch = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("These stops become a gradient token other shapes can use too.");
        if (StopsEdit("stops", g.stops, baseCol, baseTok)) ch = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("glow")) {   // clear -> solid -> clear in the shape's colour (its token, if it has one)
            g.stops = {MkStop(0, baseTok, 0, baseCol[0], baseCol[1], baseCol[2]), MkStop(0.4f, baseTok, 1, baseCol[0], baseCol[1], baseCol[2]),
                       MkStop(0.51f, baseTok, 1, baseCol[0], baseCol[1], baseCol[2]), MkStop(1, baseTok, 0, baseCol[0], baseCol[1], baseCol[2])};
            ch = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear -> solid -> clear in the shape's colour, following its colour token if it has one\n(a soft glowing line across a wide stroke). The hud.glow token is the same in HUD green.");
    }
    ImGui::PopID();
    if (ch) g_dirty = true;
    return ch;
}
static void ShapeProps(Elem& e) {
    ImGui::SetNextItemWidth(220); if (ImGui::DragFloat2("size", &e.w, 0.5f, 1.f, 4000.f, "%.0f")) g_dirty = true;
    ImGui::SetNextItemWidth(220); if (ImGui::DragFloat("rotation", &e.rot, 0.5f, -360.f, 360.f, "%.1f deg")) g_dirty = true;
    float maxR = std::min(e.w, e.h) / 2;
    if (ImGui::Checkbox("same corners", &e.radLink)) { if (e.radLink) e.rad[1] = e.rad[2] = e.rad[3] = e.rad[0]; g_dirty = true; }
    ImGui::SameLine();
    if (e.radLink) { ImGui::SetNextItemWidth(150); if (ImGui::DragFloat("corner radius", &e.rad[0], 0.25f, 0.f, maxR, "%.1f")) { e.rad[1] = e.rad[2] = e.rad[3] = e.rad[0]; g_dirty = true; } }
    else { ImGui::SetNextItemWidth(260); if (ImGui::DragFloat4("corners (TL TR BR BL)", e.rad, 0.25f, 0.f, maxR, "%.1f")) g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rounding of each corner (units; half the short side makes a pill / circle).");
    ImGui::SameLine(); if (ImGui::SmallButton("pill")) { for (float& r : e.rad) r = maxR; e.radLink = true; g_dirty = true; }
    {
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderFloat("border thickness", &e.stroke, 0.f, std::max(4.f, std::min(60.f, std::min(e.w, e.h) / 2)), "%.1f")) { e.stroke = std::max(0.f, e.stroke); g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The outline's thickness (units; 0 = no border; Ctrl + click to type any value).\nIts colour is the stroke colour below (or its gradient).");
        ImGui::SameLine(); if (ImGui::SmallButton("-")) { e.stroke = std::max(0.f, roundf(e.stroke * 2 - 1) / 2); g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("+")) { e.stroke = roundf(e.stroke * 2 + 1) / 2; g_dirty = true; }
        static const char* aligns[] = {"inside", "centred", "outside"};
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); if (ImGui::Combo("on the edge", &e.strokeAlign, aligns, 3)) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where the stroke sits: inside the edge (the shape keeps its size), centred on it, or outside it.");
        if (e.stroke > 0) {
            static const char* sm[] = {"no gradient", "gradient across", "gradient along"};
            ImGui::SetNextItemWidth(150); if (ImGui::Combo("stroke gradient", &e.strokeGrad.mode, sm, 3)) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Across: from the stroke's outer side to its inner side, following the curve of the corners.\nAlong: around the outline, clockwise from the top-left (start offset turns where it begins).");
            if (e.strokeGrad.mode == 2) { ImGui::SameLine(); ImGui::SetNextItemWidth(90); if (ImGui::DragFloat("start", &e.strokeGrad.angle, 0.5f, -360.f, 360.f, "%.0f deg")) g_dirty = true; }
            if (e.strokeGrad.mode) GradientEdit("sgrad", e.strokeGrad, e.col, e.colTok.empty() ? nullptr : e.colTok.c_str());
        }
        TokenColourEdit("fill colour", e.fillCol, e.fillTok, e.fillTokA);
        {
            static const char* fm[] = {"no gradient", "linear", "radial", "from the edge"};
            ImGui::SetNextItemWidth(150); if (ImGui::Combo("fill gradient", &e.fillGrad.mode, fm, 4)) { if (e.fillGrad.mode == 3 && e.fillGrad.depth <= 0) e.fillGrad.depth = 20; g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Linear: across the shape at an angle. Radial: centre to edge.\nFrom the edge: inward from the edge over a depth, following the curve (an inner glow / bevel).\nThe fill colour's opacity still applies.");
            if (e.fillGrad.mode == 1) { ImGui::SameLine(); ImGui::SetNextItemWidth(90); if (ImGui::DragFloat("angle", &e.fillGrad.angle, 0.5f, -360.f, 360.f, "%.0f deg")) g_dirty = true; }
            if (e.fillGrad.mode == 3) { ImGui::SameLine(); ImGui::SetNextItemWidth(90); if (ImGui::DragFloat("depth", &e.fillGrad.depth, 0.1f, 0.5f, 500.f, "%.1f")) g_dirty = true; }
            if (e.fillGrad.mode) { float fc[4] = {e.fillCol[0], e.fillCol[1], e.fillCol[2], 1}; GradientEdit("fgrad", e.fillGrad, fc, e.fillTok.empty() ? nullptr : e.fillTok.c_str()); }
        }
        int cutters = 0, mergers = 0;
        if (!e.cut && !e.merge) for (auto& o : Elems()) if (o.type == E_SHAPE && &o != &e && (o.cut || o.merge) && CutHits(o, e)) ++(o.cut ? cutters : mergers);
        if (cutters) { ImGui::SameLine(); ImGui::TextDisabled("(%d cutter%s on it)", cutters, cutters == 1 ? "" : "s"); }
        if (mergers) { ImGui::SameLine(); ImGui::TextDisabled("(%d shape%s merged in)", mergers, mergers == 1 ? "" : "s"); }
    }
    if (ImGui::Checkbox("cut", &e.cut)) { if (e.cut) e.merge = false; g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A boolean: this shape is cut out of another shape (a notch, a window, a panel or button sitting\nin a frame) and drawn itself over the hole, with its own fill and border - its border outlines the cut.\nFor an empty cut, set its fill and stroke opacity to 0. Cutters keep a pink outline while editing.");
    ImGui::SameLine();
    if (ImGui::Checkbox("merge", &e.merge)) { if (e.merge) e.cut = false; g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A boolean: this shape is merged into another shape (a union) - it draws nothing itself; the shape it\nmerges into takes in its outline, so they're one: one fill, one border around the both of them, the gradients\nacross the whole. Several can merge into the same shape. Merged shapes keep a dashed cyan outline while editing.");
    if (e.cut || e.merge) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(220);
        bool grpT = e.cutTarget.compare(0, 6, "group:") == 0;
        std::string shown = e.cutTarget.empty() ? "every shape in its group" : grpT ? "group: " + RelGroup(e.cutTarget.substr(6)) : e.cutTarget;
        if (ImGui::BeginCombo(e.merge ? "merges into" : "cuts into", shown.c_str())) {
            if (ImGui::Selectable("every shape in its group", e.cutTarget.empty())) { e.cutTarget.clear(); g_dirty = true; }
            ImGui::SeparatorText("a whole group");
            for (auto& n : GroupNames()) {   // (not its own - that's the first choice; not hidden by the menu being edited)
                if (n == e.group || MenuEdOff(n) || RelGroup(n).empty()) continue;
                std::string lb = "group: " + RelGroup(n) + "##cg" + n;
                if (ImGui::Selectable(lb.c_str(), e.cutTarget == "group:" + n)) { e.cutTarget = "group:" + n; g_dirty = true; }
            }
            ImGui::SeparatorText("one shape");
            for (auto& o : Elems()) if (o.type == E_SHAPE && !o.cut && !o.merge && &o != &e && !MenuEdOff(o.group) && ImGui::Selectable(o.name.c_str(), e.cutTarget == o.name)) { e.cutTarget = o.name; g_dirty = true; }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(e.merge ? "What it merges into: every shape in its own group, every shape in another group (and the groups inside it),\nor one shape by name (usually: one shape - with several, each draws it as a part of itself)."
                                                              : "What it cuts: every shape in its own group, every shape in another group (and the groups inside it),\nor one shape by name.\nOnly shapes on its layer or lower: a shape on a higher layer sits over the cutter and isn't cut.");
        ImGui::SameLine(); ImGui::SetNextItemWidth(110);
        if (ImGui::DragFloat("gap", &e.cutGap, 0.05f, -200.f, 200.f, "%.1f")) { e.cutGap = std::max(-200.f, std::min(200.f, e.cutGap)); g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(e.merge ? "Merged in this much bigger than the shape itself (units; negative: smaller).\nCtrl + click to type a value."
                                                              : "Cuts this much wider than the cutter itself (units): room around what sits in the cut-out.\n0 is 0.1 inside the cutter (no seam).\nNegative: the cut is that much smaller than the cutter (the cutter overlaps the shape).\nCtrl + click to type a value.");

    }
}
static void Pf(std::string& o, const char* fmt, ...) {   // printf onto a string
    char buf[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (n < 0) return;
    if (n < (int)sizeof buf) { o.append(buf, n); return; }
    std::string big(n + 1, 0); va_start(ap, fmt); vsnprintf(&big[0], n + 1, fmt, ap); va_end(ap); o.append(big.c_str(), n);
}
namespace anim {   // (uied_anim.inc) its clips as "aclip=" / "akey=" lines, and reading them back
static void PutAnim(std::string& o, const ElemAnim& a);
static void AnimKey(Elem& e, const std::string& k, const std::string& v);
}
static void PutElemS(std::string& o, int m, const Elem& e) {   // one element as the layout / prefab files keep it
    Pf(o, "\n[element]\nmode=%s\ntype=%s\nname=%s\nanchor=%d\nx=%g\ny=%g\nw=%g\nh=%g\nscale=%g\ncolor=%g %g %g %g\nvisible=%d\n",
            ModeKey(m), TypeKey(e.type), Esc(e.name).c_str(), e.anchor, e.x, e.y, e.w, e.h, e.scale, e.col[0], e.col[1], e.col[2], e.col[3], e.visible ? 1 : 0);
    if (e.row > 0) Pf(o, "row=%d\n", e.row);
    if (!e.group.empty()) Pf(o, "group=%s\n", Esc(e.group).c_str());
    if (e.hiMine) Pf(o, "highlight=%g %g %g %g\n", e.hiCol[0], e.hiCol[1], e.hiCol[2], e.hiCol[3]);
    if (e.layer != 0) Pf(o, "layer=%d\n", e.layer);
    if (e.type == E_IMAGE) {
        Pf(o, "texture=%s\npack=%s\nuv=%g %g %g %g\n", e.tex.c_str(), e.pack.c_str(), e.u0, e.v0, e.u1, e.v1);
        if (e.rot != 0) Pf(o, "rot=%g\n", e.rot);
    } else if (e.type == E_TEXT && e.rot != 0) {
        Pf(o, "rot=%g\n", e.rot);
    } else if (e.type == E_SHAPE) {
        Pf(o, "shape=%g %g %g %g %g\nfill=%g %g %g %g\n", e.rad[0], e.rad[1], e.rad[2], e.rad[3], e.stroke, e.fillCol[0], e.fillCol[1], e.fillCol[2], e.fillCol[3]);
        if (e.rot != 0) Pf(o, "rot=%g\n", e.rot);
        if (!e.fillTok.empty()) Pf(o, "filltoken=%s %g\n", e.fillTok.c_str(), e.fillTokA);
        if (e.cut) Pf(o, "cut=%g;%s\n", e.cutGap, Esc(e.cutTarget).c_str());
        else if (e.merge) Pf(o, "merge=%g;%s\n", e.cutGap, Esc(e.cutTarget).c_str());
        if (e.strokeAlign) Pf(o, "strokealign=%d\n", e.strokeAlign);
        auto grad = [&](const char* key, const UGrad& g) {   // "mode angle depth;t r g b a[@token];...", its gradient token on its own line
            if (!g.mode) return;
            Pf(o, "%s=%d %g %g", key, g.mode, g.angle, g.depth);
            for (auto& st : g.stops) Pf(o, ";%s", StopText(st).c_str());
            Pf(o, "\n");
            if (!g.tok.empty()) Pf(o, "%stoken=%s\n", key, g.tok.c_str());
        };
        grad("strokegrad", e.strokeGrad); grad("fillgrad", e.fillGrad);
    } else Pf(o, "font=%s\ntext=%s\nalign=%d\n", e.font.c_str(), Esc(e.text).c_str(), e.align);
    if (!e.role.empty()) Pf(o, "role=%s\n", e.role.c_str());
    if (e.selOnly) Pf(o, "selonly=1\n");
    if (e.tileKinds) Pf(o, "tilekinds=%d\n", e.tileKinds);
    if (e.afterLabel) Pf(o, "afterlabel=1\n");
    if (e.wrap) Pf(o, "wrap=1\n");
    if (e.swapAnim) Pf(o, "swapanim=1\n");
    if (!e.showIf.empty()) Pf(o, "showif=%s\n", e.showIf.c_str());
    if (e.bindKind) Pf(o, "bind=%s\nbindkind=%d\nbindrange=%g %g\n", e.bind.c_str(), e.bindKind, e.bindMin, e.bindMax);
    if (e.bindKind >= 3 && e.bindKind <= 6) Pf(o, "bindrot=%g %g\n", e.rotMin, e.rotMax);
    if (e.cutLo != 0 || e.cutHi != 1) Pf(o, "bindcut=%g %g\n", e.cutLo, e.cutHi);
    if (e.pivX != 0 || e.pivY != 0) Pf(o, "pivot=%g %g\n", e.pivX, e.pivY);
    if (e.follow) Pf(o, "follow=%08X\n", e.follow);
    if (e.circle) Pf(o, "circle=1\n");
    if (e.minimap) Pf(o, "minimap=1\nmapzoom=%g\nmapblip=%g\nmapplayer=%d\n", e.mapZoom, e.mapBlip, e.mapPlayer ? 1 : 0);
    if (e.worldMap) Pf(o, "worldmap=1\nmapblip=%g\nmapplayer=%d\n", e.mapBlip, e.mapPlayer ? 1 : 0);
    if (e.additive) Pf(o, "additive=1\n");
    if (!e.tint.empty()) Pf(o, "tint=%s\n", e.tint.c_str());
    if (e.anim) Pf(o, "anim=%d\n", e.anim);
    if (e.anim == 4) Pf(o, "bounce=%g %g %g %g\n", e.bounceScale, e.bounceMs, e.bounceGlow, e.bounceAlpha);
    if (!e.keys.empty()) Pf(o, "keys=%s\n", e.keys.c_str());
    if (!e.playOn.empty()) Pf(o, "playon=%s\n", e.playOn.c_str());
    if (!e.flags.empty()) { std::string l; for (auto& k : e.flags) { if (!l.empty()) l += ','; l += k; } Pf(o, "flags=%s\n", l.c_str()); }
    if (!e.colTok.empty()) Pf(o, "colortoken=%s %g\n", e.colTok.c_str(), e.colTokA);
    if (!e.hiTok.empty()) Pf(o, "hitoken=%s %g\n", e.hiTok.c_str(), e.hiTokA);
    if (e.an.p) anim::PutAnim(o, *e.an.p);
}
static void PutElem(FILE* f, int m, const Elem& e) { std::string o; PutElemS(o, m, e); fputs(o.c_str(), f); }
// Written to <path>.tmp and swapped in only when the whole file was written: a crash or a full disk mid-save
// never leaves a cut-off file. false: nothing was replaced.
static FILE* BeginWrite(const std::string& path) { return fopen((path + ".tmp").c_str(), "w"); }
static bool EndWrite(FILE* f, const std::string& path) {
    bool ok = !ferror(f);
    ok = (fclose(f) == 0) && ok;
    std::string tmp = path + ".tmp";
    if (ok) ok = MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileA(tmp.c_str());
    return ok;
}
static void Save() {
    FILE* f = BeginWrite(g_layoutPath); if (!f) { Log("ui editor: couldn't save %s", g_layoutPath.c_str()); return; }
    fprintf(f, "# NFSU2 overlay (UI editor): one layout per game mode\nshow=%d\nboardrows=%d\nboardfollow=%d\nlistheight=%d\nhighlightsel=%d\nmenufade=%g\ncustommenus=%d\n", g_show ? 1 : 0, boardRows, boardFollow ? 1 : 0, (int)g_listH, g_hiSel ? 1 : 0, menuFade, customMenus ? 1 : 0);
    for (auto& fd : g_flagDefs) fprintf(f, "flagdef=%s %d\n", fd.key.c_str(), fd.hideMissing ? 1 : 0);
    for (auto& t : g_tokens) if (!t.builtIn || TokenModified(t)) fprintf(f, "token=%s\n", TokenLine(t).c_str());
    for (auto& t : g_gradTokens) if (!t.builtIn || !SameStops(t.stops, t.def)) fprintf(f, "gradtoken=%s\n", GradTokenLine(t).c_str());
    for (auto& kv : g_mapArt) if (!kv.second.empty()) fprintf(f, "mapart=%s;%s\n", kv.first.c_str(), kv.second.c_str());
    SaveLinks(f);
    int total = 0;
    auto put = [&](int m, const Elem& e) { ++total; PutElem(f, m, e); };
    for (int m = 0; m < NumModes(); ++m) for (auto& e : g_layout[m]) put(m, e);
    SaveMenus(f);
    if (!EndWrite(f, g_layoutPath)) { Log("ui editor: couldn't save %s (the old file is kept; still unsaved)", g_layoutPath.c_str()); return; }
    g_dirty = false;
    Log("ui editor: saved %d element(s) to %s", total, g_layoutPath.c_str());
}
static void ElemKey(Elem& e, const std::string& k, const std::string& v) {   // one "key=value" of an [element]
    if (k == "mode") { for (int m = 0; m < NumModes(); ++m) if (v == modes[m].key) e.mode = m; }
    else if (k == "type") e.type = v == "text" ? E_TEXT : v == "shape" ? E_SHAPE : E_IMAGE;
    else if (k == "shape") { int n = sscanf(v.c_str(), "%f %f %f %f %f", &e.rad[0], &e.rad[1], &e.rad[2], &e.rad[3], &e.stroke); (void)n; e.radLink = e.rad[0] == e.rad[1] && e.rad[1] == e.rad[2] && e.rad[2] == e.rad[3]; }
    else if (k == "fill") sscanf(v.c_str(), "%f %f %f %f", &e.fillCol[0], &e.fillCol[1], &e.fillCol[2], &e.fillCol[3]);
    else if (k == "filltoken") { char n[64] = {}; float a = 1; if (sscanf(v.c_str(), "%63s %f", n, &a) >= 1) { e.fillTok = n; e.fillTokA = a; } }
    else if (k == "cutshow") {}   // (old layouts: every cutter is drawn now)
    else if (k == "strokealign") e.strokeAlign = std::max(0, std::min(2, atoi(v.c_str())));
    else if (k == "strokegrad" || k == "fillgrad") {
        UGrad& g = k == "strokegrad" ? e.strokeGrad : e.fillGrad;
        std::string tok = g.tok; g = UGrad(); g.tok = tok;   // (its token line may come first or after)
        size_t p = v.find(';');
        sscanf(v.c_str(), "%d %f %f", &g.mode, &g.angle, &g.depth);
        while (p != std::string::npos) {
            size_t q = v.find(';', p + 1);
            UStop st; if (StopParse(v.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1), st)) g.stops.push_back(st);
            p = q;
        }
        SortStops(g.stops);
    }
    else if (k == "strokegradtoken") e.strokeGrad.tok = v;
    else if (k == "fillgradtoken") e.fillGrad.tok = v;
    else if (k == "cut") { e.cut = true; size_t sc = v.find(';'); e.cutGap = (float)atof(v.c_str()); e.cutTarget = sc == std::string::npos ? "" : Unesc(v.substr(sc + 1)); }
    else if (k == "merge") { e.merge = true; size_t sc = v.find(';'); e.cutGap = (float)atof(v.c_str()); e.cutTarget = sc == std::string::npos ? "" : Unesc(v.substr(sc + 1)); }
    else if (k == "name") e.name = Unesc(v);
    else if (k == "anchor") e.anchor = std::max(0, std::min(8, atoi(v.c_str())));
    else if (k == "x") e.x = (float)atof(v.c_str()); else if (k == "y") e.y = (float)atof(v.c_str());
    else if (k == "w") e.w = (float)atof(v.c_str()); else if (k == "h") e.h = (float)atof(v.c_str());
    else if (k == "scale") e.scale = (float)atof(v.c_str());
    else if (k == "color") sscanf(v.c_str(), "%f %f %f %f", &e.col[0], &e.col[1], &e.col[2], &e.col[3]);
    else if (k == "visible") e.visible = atoi(v.c_str()) != 0;
    else if (k == "texture") e.tex = v; else if (k == "pack") e.pack = v;
    else if (k == "uv") sscanf(v.c_str(), "%f %f %f %f", &e.u0, &e.v0, &e.u1, &e.v1);
    else if (k == "rot") e.rot = (float)atof(v.c_str());
    else if (k == "layer") e.layer = atoi(v.c_str());
    else if (k == "font") e.font = v; else if (k == "text") e.text = Unesc(v);
    else if (k == "align") e.align = atoi(v.c_str());
    else if (k == "row") e.row = std::max(0, atoi(v.c_str()));
    else if (k == "group") e.group = Unesc(v);
    else if (k == "highlight") { e.hiMine = true; sscanf(v.c_str(), "%f %f %f %f", &e.hiCol[0], &e.hiCol[1], &e.hiCol[2], &e.hiCol[3]); }
    else if (k == "role") e.role = v;
    else if (k == "selonly") e.selOnly = atoi(v.c_str()) != 0;
    else if (k == "tilekinds") e.tileKinds = atoi(v.c_str()) & 15;
    else if (k == "afterlabel") e.afterLabel = atoi(v.c_str()) != 0;
    else if (k == "wrap") e.wrap = atoi(v.c_str()) != 0;
    else if (k == "swapanim") e.swapAnim = atoi(v.c_str()) != 0;
    else if (k == "kit") e.kit = Unesc(v);
    else if (k == "showif") e.showIf = v;
    else if (k == "bind") e.bind = v;
    else if (k == "bindkind") e.bindKind = std::max(0, std::min(6, atoi(v.c_str())));
    else if (k == "bindrange") sscanf(v.c_str(), "%f %f", &e.bindMin, &e.bindMax);
    else if (k == "bindrot") sscanf(v.c_str(), "%f %f", &e.rotMin, &e.rotMax);
    else if (k == "bindcut") sscanf(v.c_str(), "%f %f", &e.cutLo, &e.cutHi);
    else if (k == "pivot") sscanf(v.c_str(), "%f %f", &e.pivX, &e.pivY);
    else if (k == "follow") e.follow = (unsigned)strtoul(v.c_str(), nullptr, 16);
    else if (k == "circle") e.circle = atoi(v.c_str()) != 0;
    else if (k == "minimap") e.minimap = atoi(v.c_str()) != 0;
    else if (k == "worldmap") e.worldMap = atoi(v.c_str()) != 0;
    else if (k == "mapzoom") e.mapZoom = std::max(0.1f, std::min(10.f, (float)atof(v.c_str())));
    else if (k == "mapblip") e.mapBlip = std::max(0.1f, std::min(10.f, (float)atof(v.c_str())));
    else if (k == "mapplayer") e.mapPlayer = atoi(v.c_str()) != 0;
    else if (k == "colortoken" || k == "hitoken") {   // "name opacity"
        char n[64] = {}; float a = 1;
        if (sscanf(v.c_str(), "%63s %f", n, &a) >= 1) { if (k == "colortoken") { e.colTok = n; e.colTokA = a; } else { e.hiTok = n; e.hiTokA = a; } }
    }
    else if (k == "additive") e.additive = atoi(v.c_str()) != 0;
    else if (k == "tint") e.tint = v;
    else if (k == "anim") e.anim = std::max(0, std::min(4, atoi(v.c_str())));
    else if (k == "bounce") sscanf(v.c_str(), "%f %f %f %f", &e.bounceScale, &e.bounceMs, &e.bounceGlow, &e.bounceAlpha);
    else if (k == "keys") e.keys = v;
    else if (k == "aclip" || k == "akey") anim::AnimKey(e, k, v);
    else if (k == "playon") e.playOn = v;
    else if (k == "flags") { e.flags.clear(); size_t p = 0; while (p <= v.size()) { size_t q = v.find(',', p); if (q == std::string::npos) q = v.size(); if (q > p) e.flags.push_back(v.substr(p, q - p)); p = q + 1; } }
}
// intoMode >= 0: every element goes in that layout; merge: added to what's there (an import), not instead
static bool LoadFile(const std::string& path, int intoMode, bool merge) {
    FILE* f = fopen(path.c_str(), "r"); if (!f) return false;
    if (!merge) { for (auto& l : g_layout) l.clear(); LoadMenusReset(); TokensReset(); g_mapArt.clear(); LinksReset(); }
    g_sel = -1; ClearMulti();
    std::vector<Elem> all;
    char line[2048]; Elem* cur = nullptr; int msect = 0;
    while (fgets(line, sizeof line, f)) {
        std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        if (s == "[element]") { all.push_back(Elem()); cur = &all.back(); msect = 0; continue; }
        if (s[0] == '[') { msect = LoadMenuSection(s); cur = nullptr; continue; }
        size_t eq = s.find('='); if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (msect) { LoadMenuKey(msect, k, v); continue; }
        if (!cur) {
            if (k == "token") { ColorToken t; if (TokenParse(v, t) && (!merge || !TokenOf(t.name))) TokenLoad(t); continue; }   // (an import adds its missing ones)
            if (k == "gradtoken") { GradToken t; if (GradTokenParse(v, t) && (!merge || !GradTokenOf(t.name))) GradTokenLoad(t); continue; }
            if (merge) continue;
            if (k == "show") g_show = atoi(v.c_str()) != 0;
            else if (k == "highlightsel") g_hiSel = atoi(v.c_str()) != 0;
            else if (k == "boardrows") boardRows = std::max(1, std::min(22, atoi(v.c_str())));
            else if (k == "boardfollow") boardFollow = atoi(v.c_str()) != 0;
            else if (k == "grouplink") LoadLinkLine(v, intoMode);
            else if (k == "mapart") { size_t sc = v.find(';'); if (sc != std::string::npos && sc > 0) g_mapArt[v.substr(0, sc)] = v.substr(sc + 1); }
            else if (k == "listheight") g_listH = std::max(80.f, std::min(1400.f, (float)atof(v.c_str())));
            else if (k == "menufade") menuFade = std::max(0.f, std::min(1.f, (float)atof(v.c_str())));
            else if (k == "custommenus") customMenus = atoi(v.c_str()) != 0;
            else if (k == "flagdef") {   // "key hidden-when-missing": a mod's flag, for when that mod isn't loaded
                char key[64] = {}; int hm = 0;
                if (sscanf(v.c_str(), "%63s %d", key, &hm) >= 1 && !FlagOf(key)) { FlagDef d; d.key = key; d.hideMissing = hm != 0; g_flagDefs.push_back(d); }
            }
            continue;
        }
        ElemKey(*cur, k, v);
    }
    fclose(f);
    if (merge) g_dirty = true; else g_dirty = false;
    int oldKit = 0;
    for (auto& e : all) { if (intoMode >= 0) e.mode = intoMode; if (!e.kit.empty()) ++oldKit; else g_layout[e.mode].push_back(e); }   // (widget-kit pieces: gone)
    if (oldKit) Log("ui editor: %d widget-kit piece(s) left out (the widget kit is gone)", oldKit);
    LoadMenuEnd();
    return true;
}
static void Load() { LoadFile(g_layoutPath, -1, false); }
// ---------------------------------------------------------------- prefabs
// A group (with the groups inside it) saved to scripts\NFSU2_REUI_prefabs\<name>.txt; adding one puts a copy in the
// game mode being edited, as a new top-level group (renamed "<name> 2"... if that name is taken).
// Built-in prefabs: the repo's prefabs/*.txt, baked into the .asi at build time (tools/bake_prefabs.py). A file of the
// same name on disk overrides one. Delete removes the file and logs the name in _deleted.log, which hides a built-in
// one now and drops it from the repo at the next build (tools/sync_prefabs.py).
struct BakedPrefab { const char* name; const char* text; };
#include "baked_prefabs.inc"
std::string prefabDir, dataDir;
static std::string PrefabDir() { return prefabDir.empty() ? g_dir + "\\scripts\\NFSU2_REUI_prefabs\\" : prefabDir; }
static std::string g_prefabMsg;
static const char* BakedPrefabText(const std::string& name) {
    for (const BakedPrefab* b = kBakedPrefabs; b->name; ++b) if (name == b->name) return b->text;
    return nullptr;
}
static std::vector<std::string> DeletedPrefabs() {
    std::vector<std::string> out;
    FILE* f = fopen((PrefabDir() + "_deleted.log").c_str(), "r");
    if (!f) return out;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
    fclose(f);
    return out;
}
static void WriteDeletedPrefabs(const std::vector<std::string>& names) {
    std::string path = PrefabDir() + "_deleted.log";
    if (names.empty()) { DeleteFileA(path.c_str()); return; }
    CreateDirectoryA(PrefabDir().c_str(), nullptr);
    FILE* f = BeginWrite(path);
    if (!f) { Log("ui editor: couldn't write %s", path.c_str()); return; }
    for (auto& n : names) fprintf(f, "%s\n", n.c_str());
    if (!EndWrite(f, path)) Log("ui editor: couldn't write %s", path.c_str());
}
static bool PrefabOnDisk(const std::string& name) { return GetFileAttributesA((PrefabDir() + name + ".txt").c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool PrefabBuiltIn(const std::string& name) { return BakedPrefabText(name) != nullptr; }
static bool SavePrefab(const std::string& group, std::string name) {
    for (char& c : name) if (strchr("\\/:*?\"<>|", c)) c = '_';
    if (name.empty()) return false;
    CreateDirectoryA(PrefabDir().c_str(), nullptr);
    std::string path = PrefabDir() + name + ".txt";
    FILE* f = BeginWrite(path);
    if (!f) { g_prefabMsg = "couldn't write " + path; return false; }
    fprintf(f, "# NFSU2_REUI prefab\nroot=%s\n", Esc(group).c_str());
    {   // the tokens its elements use that aren't built in (added where they're missing when the prefab is added)
        std::vector<std::string> used;
        std::vector<std::string> grads;
        auto use = [&](std::vector<std::string>& v, const std::string& n) { if (!n.empty() && std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n); };
        for (auto& e : Elems()) if (e.group == group || InGroup(e.group, group)) {
            for (const std::string* n : {&e.colTok, &e.hiTok, &e.fillTok}) use(used, *n);
            for (const UGrad* g : {&e.strokeGrad, &e.fillGrad}) { use(grads, g->tok); for (auto& st : g->stops) use(used, st.tok); }
        }
        for (auto& n : grads) if (GradToken* t = GradTokenOf(n)) for (auto& st : t->stops) use(used, st.tok);
        for (auto& n : used) if (ColorToken* t = TokenOf(n)) if (!t->builtIn) fprintf(f, "token=%s\n", TokenLine(*t).c_str());
        for (auto& n : grads) if (GradToken* t = GradTokenOf(n)) if (!t->builtIn) fprintf(f, "gradtoken=%s\n", GradTokenLine(*t).c_str());
    }
    int n = 0;
    for (auto& e : Elems()) if (e.group == group || InGroup(e.group, group)) { PutElem(f, g_editMode, e); ++n; }
    if (!EndWrite(f, path)) { g_prefabMsg = "couldn't write " + path; return false; }
    {   // saved again under a deleted name: it's back
        auto del = DeletedPrefabs();
        auto it = std::find(del.begin(), del.end(), name);
        if (it != del.end()) { del.erase(it); WriteDeletedPrefabs(del); }
    }
    g_prefabMsg = "saved \"" + name + "\" (" + std::to_string(n) + " elements)";
    Log("ui editor: prefab %s", g_prefabMsg.c_str());
    return true;
}
static std::vector<std::string> PrefabList() {   // built-in ones and the files on disk, minus deleted names
    std::vector<std::string> out;
    for (const BakedPrefab* b = kBakedPrefabs; b->name; ++b) out.push_back(b->name);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((PrefabDir() + "*.txt").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName; n = n.substr(0, n.size() - 4);
            if (!n.empty() && n[0] != '_' && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    auto del = DeletedPrefabs();
    out.erase(std::remove_if(out.begin(), out.end(), [&](const std::string& n) {
        return !PrefabOnDisk(n) && std::find(del.begin(), del.end(), n) != del.end(); }), out.end());
    std::sort(out.begin(), out.end());
    return out;
}
static std::vector<ColorToken> g_prefabTokens;   // the last parsed prefab's own tokens
static std::vector<GradToken> g_prefabGrads;
static void ParsePrefab(const std::string& text, std::string& root, std::vector<Elem>& els) {
    g_prefabTokens.clear(); g_prefabGrads.clear();
    Elem* cur = nullptr;
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at); if (nl == std::string::npos) nl = text.size();
        std::string s = text.substr(at, nl - at); at = nl + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        if (s == "[element]") { els.push_back(Elem()); cur = &els.back(); continue; }
        size_t eq = s.find('='); if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (!cur) {
            if (k == "root") root = Unesc(v);
            else if (k == "token") { ColorToken t; if (TokenParse(v, t)) g_prefabTokens.push_back(t); }
            else if (k == "gradtoken") { GradToken t; if (GradTokenParse(v, t)) g_prefabGrads.push_back(t); }
            continue;
        }
        ElemKey(*cur, k, v);
    }
}
static bool LoadPrefab(const std::string& name, std::string& root, std::vector<Elem>& els) {   // the file, else the built-in one
    els.clear(); root.clear();
    std::string text;
    if (FILE* f = fopen((PrefabDir() + name + ".txt").c_str(), "rb")) {
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
    } else if (const char* b = BakedPrefabText(name)) text = b;
    else return false;
    ParsePrefab(text, root, els);
    return true;
}
static void DeletePrefab(const std::string& name) {
    bool file = PrefabOnDisk(name), builtIn = PrefabBuiltIn(name);
    if (file) DeleteFileA((PrefabDir() + name + ".txt").c_str());
    auto del = DeletedPrefabs();
    if (std::find(del.begin(), del.end(), name) == del.end()) { del.push_back(name); WriteDeletedPrefabs(del); }
    g_prefabMsg = "deleted \"" + name + "\"" + (builtIn ? " (built in: hidden now, removed from REUI at the next build)" : "");
    Log("ui editor: prefab %s (logged in _deleted.log)", g_prefabMsg.c_str());
}
static std::string IntoMenu(const std::string& g);   // (uied_menu.inc)
static std::string RelGroup(const std::string& g);
static void AddPrefab(const std::string& name) {
    std::string root; std::vector<Elem> els;
    if (!LoadPrefab(name, root, els)) { g_prefabMsg = "couldn't read " + name; return; }
    if (els.empty()) { g_prefabMsg = "\"" + name + "\" is empty"; return; }
    for (auto& t : g_prefabTokens) if (!TokenOf(t.name)) TokenLoad(t);   // its tokens this layout doesn't have yet
    for (auto& t : g_prefabGrads) if (!GradTokenOf(t.name)) GradTokenLoad(t);
    std::string base = IntoMenu(name), top = base;   // a free top-level group name (inside the custom menu being edited)
    auto taken = [&](const std::string& g) { for (auto& e : Elems()) if (e.group == g || InGroup(e.group, g)) return true; return false; };
    for (int i = 2; taken(top); ++i) top = base + " " + std::to_string(i);
    for (auto& e : els) {
        e.group = e.group == root ? top : InGroup(e.group, root) ? top + e.group.substr(root.size()) : top;
        e.mode = g_editMode; e.texIdx = e.fontIdx = -2; e.shownSince = -1;
        Elems().push_back(e);
    }
    g_selGroup = top; g_sel = -1; ClearMulti(); g_dirty = true;
    g_prefabMsg = "added \"" + name + "\" as " + top;
}

void Init(const std::string& gameDir, const std::string& layoutPath, void (*log)(const char*)) {
    g_dir = gameDir; g_layoutPath = layoutPath; g_log = log;
    std::string data = dataDir.empty() ? g_dir + "\\REUI\\" : dataDir;   // REUI's own folder (NFSU2: in the game's root)
    if (data.back() != '\\') data += '\\';
    CreateDirectoryA(data.c_str(), nullptr);
    vecart::SetDir(data + "vector\\");
    Load();
    // no menu pages yet: UniPad's menus (made in its own editor before REUI) come in, until this workspace is saved
    if (nfsu2Tools && !menuMode && menusMode >= 0 && g_layout[menusMode].empty()) {
        std::string p = g_dir + "\\scripts\\NFSU2UniPad_menus.txt";
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES && LoadFile(p, menusMode, true))
            Log("ui editor: no menu pages here yet - using %s (Save to keep them in this workspace)", p.c_str());
    }
    // no HUD layouts yet: the multiplayer mod's (made in its own editor before REUI) come in the same way
    bool hud = false; for (int m = 0; m < NumModes(); ++m) if (!IsMenus(m)) hud |= !g_layout[m].empty();
    if (nfsu2Tools && !hud) {
        std::string p = g_dir + "\\scripts\\NFSU2Multiplayer_overlay.txt";
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES && LoadFile(p, -1, true))
            Log("ui editor: no HUD layouts here yet - using %s (Save to keep them in this workspace)", p.c_str());
    }
    // the overlay needs the index to draw: scan in the background if there's anything to show
    bool any = false; for (auto& l : g_layout) any |= !l.empty();
    if (any && g_state == 0) { g_state = 1; std::thread(Scan).detach(); }
}
void FrameStart() {   // the start of a frame: last frame's draw lists are rendered
    g_loadsThisFrame = 0;
    gfx::FrameStart();
    ResolveTokens();
    shapes::Trim();
    shapes::GpuFrameStart();
}
static bool MenuOpenNow();
bool WantsFrames() {   // anything to draw while the host's panel is closed
    if (testMode || MenuOpenNow()) return true;
    if (!g_show || g_state != 2) return false;
    for (auto& l : g_layout) if (!l.empty()) return true;
    return false;
}

// ---------------------------------------------------------------- drawing + on-screen editing
// indices of a layout in drawing order (bottom first)
static std::vector<int> DrawOrder(const std::vector<Elem>& v) {
    std::vector<int> o(v.size());
    for (size_t i = 0; i < v.size(); ++i) o[i] = (int)i;
    std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return v[a].layer < v[b].layer; });
    return o;
}
static void LayerRange(int& lo, int& hi) {
    lo = hi = 0; bool any = false;
    for (auto& e : Elems()) { if (!any || e.layer < lo) lo = e.layer; if (!any || e.layer > hi) hi = e.layer; any = true; }
}

static std::vector<std::string> GroupNames();
static void RebaseGroup(const std::string& g, const std::string& to);
static void LinksRebase(const std::string& from, const std::string& to);
#include "uied_menu.inc"
#include "uied_links.inc"
#include "uied_anim.inc"

// ---------------------------------------------------------------- undo
// A snapshot of everything editable is kept at each settled point (no mouse button down, nothing being typed or
// dragged); the first frame that settles after edits pushes the previous one. kUndoSteps of them.
constexpr size_t kUndoSteps = 24;
struct Snap { std::vector<Elem> lay[kMaxModes]; std::vector<Widget> widgets; std::vector<Page> pages; std::vector<LinkSet> links; };
static std::deque<Snap> g_undo, g_redo;
static Snap g_base; static bool g_baseOk = false; static unsigned g_baseGen = 0;
static Snap TakeSnap() {
    Snap s; for (int m = 0; m < kMaxModes; ++m) s.lay[m] = g_layout[m];
    s.widgets = g_widgets; s.pages = g_pages; s.links = g_links; return s;
}
static void PutSnap(const Snap& s) {
    for (int m = 0; m < kMaxModes; ++m) g_layout[m] = s.lay[m];
    g_widgets = s.widgets; g_pages = s.pages;
    g_links = s.links; for (auto& l : g_links) l.refOk = false;   // (the pieces are as they were: the look is read again)
    g_dragging = -1;
}
// Undo / redo keep what's picked (2026-11-08): the same element - at its place in the list if it's still that one
// (same group and name), else the one with that group and name, else at its place if only its name or its group
// changed (the undone edit was a rename / a move to another group); the picked group if it's still there.
struct PickMark { int i; std::string group, name; };
static PickMark MarkOf(int i) { auto& els = Elems(); PickMark m{ i, "", "" }; if (i >= 0 && i < (int)els.size()) { m.group = els[i].group; m.name = els[i].name; } else m.i = -1; return m; }
static int FindMark(const PickMark& m) {
    auto& els = Elems(); int n = (int)els.size();
    if (m.i < 0) return -1;
    if (m.i < n && els[m.i].group == m.group && els[m.i].name == m.name) return m.i;
    int best = -1;
    for (int i = 0; i < n; ++i) if (els[i].group == m.group && els[i].name == m.name && (best < 0 || abs(i - m.i) < abs(best - m.i))) best = i;
    if (best >= 0) return best;
    if (m.i < n && (els[m.i].group == m.group || els[m.i].name == m.name)) return m.i;
    return -1;
}
static void PutSnapKeepPick(const Snap& s) {
    PickMark sel = MarkOf(g_sel);
    std::vector<PickMark> multi; for (int i : g_multi) multi.push_back(MarkOf(i));
    std::string grp = g_selGroup; std::vector<std::string> multiG = g_multiG;
    PutSnap(s);
    auto groupThere = [](const std::string& g) { for (auto& e : Elems()) if (InGroup(e.group, g)) return true; return false; };
    g_sel = FindMark(sel);
    g_selGroup = !grp.empty() && groupThere(grp) ? grp : std::string();
    if (!g_selGroup.empty()) g_sel = -1;
    g_multi.clear(); for (auto& m : multi) { int i = FindMark(m); if (i >= 0 && std::find(g_multi.begin(), g_multi.end(), i) == g_multi.end()) g_multi.push_back(i); }
    g_multiG.clear(); for (auto& g : multiG) if (groupThere(g)) g_multiG.push_back(g);
}
static void UndoReset() { g_undo.clear(); g_redo.clear(); g_baseOk = false; }
static void UndoCommit(bool evenIfBusy) {   // the edits since the last snapshot become one step
    if (!g_baseOk) { g_base = TakeSnap(); g_baseGen = g_dirty.gen; g_baseOk = true; return; }
    if (g_dirty.gen == g_baseGen) return;
    bool busy = ImGui::IsMouseDown(0) || ImGui::IsAnyItemActive() || g_dragging >= 0;
    if (busy && !evenIfBusy) return;
    g_undo.push_back(g_base); if (g_undo.size() > kUndoSteps) g_undo.pop_front();
    g_redo.clear();
    g_base = TakeSnap(); g_baseGen = g_dirty.gen;
}
static void Undo() {
    UndoCommit(true);
    if (g_undo.empty()) return;
    g_redo.push_back(TakeSnap());
    PutSnapKeepPick(g_undo.back()); g_undo.pop_back();
    g_dirty = true; g_base = TakeSnap(); g_baseGen = g_dirty.gen;
}
static void Redo() {
    if (g_redo.empty()) return;
    g_undo.push_back(TakeSnap()); if (g_undo.size() > kUndoSteps) g_undo.pop_front();
    PutSnapKeepPick(g_redo.back()); g_redo.pop_back();
    g_dirty = true; g_base = TakeSnap(); g_baseGen = g_dirty.gen;
}

// ---------------------------------------------------------------- several elements picked
static void DeleteSelected() {
    auto& els = Elems();
    if (PickCount() > 1) {   // the picked elements, and the picked groups with everything in them
        std::vector<int> v = PickedElems(); std::sort(v.rbegin(), v.rend());
        for (int i : v) els.erase(els.begin() + i);
        for (auto& g : g_multiG) MenuForget(g);
        ClearMulti(); g_sel = -1; g_selGroup.clear();
    } else if (!g_selGroup.empty()) {
        for (int i = (int)els.size() - 1; i >= 0; --i) if (InGroup(els[i].group, g_selGroup)) els.erase(els.begin() + i);
        MenuForget(g_selGroup); g_selGroup.clear(); g_sel = -1; ClearMulti();
    } else if (g_sel >= 0 && g_sel < (int)els.size()) {
        els.erase(els.begin() + g_sel);
        if (g_sel >= (int)els.size()) g_sel = (int)els.size() - 1;
        ClearMulti();
    } else return;
    g_dirty = true;
}
// ---------------------------------------------------------------- copy / paste (Ctrl + C / Ctrl + V, 2026-11-08)
// Copies what's picked: an element, several, groups (with what's in them: their grids / carousels' settings and any
// menus inside too). Pasting adds new copies - never linked: a copied group gets a new name ("<name> copy") - in the
// game mode being edited. On the same menu they're pasted a little down-right of the originals (further each paste);
// on another menu (or another game mode) where they were on theirs, in the same groups inside it.
struct Clip { std::vector<Elem> els; std::vector<std::string> groups; std::vector<Widget> widgets; std::vector<Page> pages;
              std::string base; int mode = -1; int pastes = 0; };
static Clip g_clip;
static std::string ClipBase() { return MenuWs() ? g_menuEd : std::string(); }   // (the menu the paths are relative to)
static std::string ClipRel(const std::string& g, const std::string& base) {
    if (base.empty()) return g;
    if (g == base) return std::string();
    return InGroup(g, base) ? g.substr(base.size() + 1) : g;
}
static std::string ClipAbs(const std::string& rel, const std::string& base) { return base.empty() ? rel : CleanGroup(base + "/" + rel); }
static void CopyPicked() {
    auto& els = Elems();
    Clip c; c.base = ClipBase(); c.mode = g_editMode;
    std::vector<std::string> gs = g_multiG;
    if (!g_selGroup.empty()) gs.push_back(g_selGroup);
    // (a group inside another copied group goes with that one)
    std::vector<std::string> top;
    for (auto& g : gs) { bool inner = false; for (auto& o : gs) if (o != g && InGroup(g, o)) inner = true; if (!inner && std::find(top.begin(), top.end(), g) == top.end()) top.push_back(g); }
    std::vector<int> loose = g_multi; if (g_sel >= 0 && g_sel < (int)els.size() && g_selGroup.empty()) loose.push_back(g_sel);
    for (int i = 0; i < (int)els.size(); ++i) {
        bool inG = false; for (auto& g : top) inG |= InGroup(els[i].group, g);
        bool lo = std::find(loose.begin(), loose.end(), i) != loose.end();
        if (!inG && !lo) continue;
        Elem e = els[i]; e.group = ClipRel(e.group, c.base);
        if (e.cutTarget.compare(0, 6, "group:") == 0) e.cutTarget = "group:" + ClipRel(e.cutTarget.substr(6), c.base);
        c.els.push_back(e);
    }
    if (c.els.empty()) return;
    for (auto& g : top) {
        c.groups.push_back(ClipRel(g, c.base));
        for (auto& w : g_widgets) if (InGroup(w.group, g)) { Widget x = w; x.group = ClipRel(w.group, c.base); c.widgets.push_back(x); }
        for (auto& p : g_pages) if (InGroup(p.group, g) && p.group != c.base) { Page x = p; x.group = ClipRel(p.group, c.base); c.pages.push_back(x); }
    }
    g_clip = c;
    Log("ui editor: copied %d element(s)%s", (int)c.els.size(), top.empty() ? "" : (" in " + std::to_string(top.size()) + " group(s)").c_str());
}
static void PasteClip() {
    if (g_clip.els.empty()) return;
    std::string base = ClipBase();
    bool here = base == g_clip.base && g_editMode == g_clip.mode;
    float off = here ? 10.f * (g_clip.pastes + 1) : 0.f;
    ++g_clip.pastes;
    auto names = GroupNames();
    auto taken = [&](const std::string& g) { return std::find(names.begin(), names.end(), g) != names.end() || PageOf(g); };
    // copied groups: new names beside the originals
    std::vector<std::pair<std::string, std::string>> ren;   // (rel group -> its new absolute path)
    for (auto& rg : g_clip.groups) {
        std::string abs = ClipAbs(rg, base), ng = abs;
        if (taken(ng)) { ng = abs + " copy"; for (int k = 2; taken(ng); ++k) ng = abs + " copy " + std::to_string(k); }
        names.push_back(ng);
        ren.push_back({ rg, ng });
    }
    auto map = [&](const std::string& rel) {
        for (auto& r : ren) if (rel == r.first || (r.first.empty() ? false : InGroup(rel, r.first))) return CleanGroup(r.second + rel.substr(r.first.size()));
        return ClipAbs(rel, base);
    };
    auto& els = Elems();
    int first = (int)els.size();
    for (auto e : g_clip.els) {
        e.group = map(e.group);
        if (e.cutTarget.compare(0, 6, "group:") == 0) e.cutTarget = "group:" + map(e.cutTarget.substr(6));
        e.x += off; e.y += off; e.mode = g_editMode;
        els.push_back(e);
    }
    for (auto w : g_clip.widgets) { w.group = map(w.group); g_widgets.push_back(w); }
    for (auto p : g_clip.pages) { p.group = map(p.group); p.start = false; g_pages.push_back(p); }
    // pick what was pasted
    g_sel = -1; g_selGroup.clear(); ClearMulti();
    std::vector<int> loose;
    for (int i = first; i < (int)els.size(); ++i) { bool inG = false; for (auto& r : ren) inG |= InGroup(els[i].group, r.second); if (!inG) loose.push_back(i); }
    if (ren.size() == 1 && loose.empty()) g_selGroup = ren[0].second;
    else if (ren.empty() && loose.size() == 1) g_sel = loose[0];
    else { g_multi = loose; for (auto& r : ren) g_multiG.push_back(r.second); }
    g_dirty = true;
    Log("ui editor: pasted %d element(s)", (int)g_clip.els.size());
}
// the picked groups into `to` ("" = top level), as groups inside it; the picked loose elements straight into it
static void PutPickedIn(const std::string& to) {
    auto& els = Elems();
    std::vector<std::string> moved;
    for (auto& g : g_multiG) {
        if (!to.empty() && InGroup(to, g)) { moved.push_back(g); continue; }   // not into itself
        std::string dest = CleanGroup((to.empty() ? "" : to + "/") + GroupLeaf(g));
        if (dest != g) RebaseGroup(g, dest);
        moved.push_back(dest);
    }
    g_multiG = moved;
    for (int i : g_multi) if (i >= 0 && i < (int)els.size()) els[i].group = to;
    g_dirty = true;
}
static void MultiPanel() {
    auto& els = Elems();
    g_multi.erase(std::remove_if(g_multi.begin(), g_multi.end(), [&](int i) { return i < 0 || i >= (int)els.size(); }), g_multi.end());
    { auto names = GroupNames(); g_multiG.erase(std::remove_if(g_multiG.begin(), g_multiG.end(), [&](const std::string& g) { return std::find(names.begin(), names.end(), g) == names.end(); }), g_multiG.end()); }
    std::vector<int> P = PickedElems();
    if (P.empty()) return;
    ImGui::Text("%d picked:", (int)PickCount());
    ImGui::SameLine();
    if (!g_multiG.empty()) { ImGui::TextDisabled("%d group(s)%s", (int)g_multiG.size(), g_multi.empty() ? "" : ","); ImGui::SameLine(); }
    if (!g_multi.empty()) { ImGui::TextDisabled("%d element(s)", (int)g_multi.size()); ImGui::SameLine(); }
    ImGui::TextDisabled("(%d elements in all)", (int)P.size());
    ImGui::SameLine(); if (ImGui::SmallButton("clear")) { ClearMulti(); return; }
    ImGui::TextDisabled("Ctrl + click: add / remove one (element or group), Shift + click: everything between.\nDrag any of them on screen to move them all.");
    float d[2] = { 0, 0 };
    ImGui::SetNextItemWidth(220);
    if (ImGui::DragFloat2("move by", d, 0.5f, -2000.f, 2000.f, "%.0f")) { for (int i : P) { els[i].x += d[0]; els[i].y += d[1]; } g_dirty = true; }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##ml", ImGuiDir_Left)) { for (int i : P) els[i].x -= 1; g_dirty = true; } ImGui::SameLine();
    if (ImGui::ArrowButton("##mr", ImGuiDir_Right)) { for (int i : P) els[i].x += 1; g_dirty = true; } ImGui::SameLine();
    if (ImGui::ArrowButton("##mu", ImGuiDir_Up)) { for (int i : P) els[i].y -= 1; g_dirty = true; } ImGui::SameLine();
    if (ImGui::ArrowButton("##md", ImGuiDir_Down)) { for (int i : P) els[i].y += 1; g_dirty = true; }
    {   // layers: shifted together, keeping their order among themselves
        int glo = els[P[0]].layer, ghi = glo;
        for (int i : P) { glo = std::min(glo, els[i].layer); ghi = std::max(ghi, els[i].layer); }
        int lo, hi; LayerRange(lo, hi); int L = glo;
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("layer (lowest)##multi", &L) && L != glo) { for (int i : P) els[i].layer += L - glo; g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("to front##multi")) { for (int i : P) els[i].layer += hi + 1 - glo; g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("to back##multi")) { for (int i : P) els[i].layer += lo - 1 - ghi; g_dirty = true; }
    }
    bool allVis = true; for (int i : P) allVis &= els[i].visible;
    if (ImGui::Checkbox("visible##multi", &allVis)) { for (int i : P) els[i].visible = allVis; g_dirty = true; }
    { std::vector<Elem*> v; for (int i : P) v.push_back(&els[i]); FlagBoxes(v, "##multi"); }
    {
        int an = els[P[0]].anchor; bool mixed = false; for (int i : P) mixed |= els[i].anchor != an;
        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("anchored to##multi", mixed ? "(mixed)" : kAnchors[an])) {
            for (int a = 0; a < 9; ++a) if (ImGui::Selectable(kAnchors[a], !mixed && a == an)) { for (int i : P) SetAnchorKeep(els[i], a); g_dirty = true; }
            ImGui::EndCombo();
        }
    }
    {   // into a group (picked groups go inside it, as groups)
        ImGui::SetNextItemWidth(180);
        if (ImGui::BeginCombo("##multigrp", "put them in a group...")) {
            if (ImGui::Selectable(MenuEdOff("") ? "(out of its groups)" : "(no group / top level)")) PutPickedIn(IntoMenu(""));
            for (auto& n : GroupNames()) {
                if (MenuEdOff(n) || RelGroup(n).empty()) continue;   // (a custom menu picked: its own groups)
                bool own = false; for (auto& g : g_multiG) own |= InGroup(n, g);   // not into one of themselves
                if (!own && ImGui::Selectable((RelGroup(n) + "##pg" + n).c_str())) PutPickedIn(n);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        static char ng[64] = "";
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputTextWithHint("##multinew", "or a new group (Enter)", ng, sizeof ng, ImGuiInputTextFlags_EnterReturnsTrue) && CleanGroup(ng)[0]) {
            PutPickedIn(IntoMenu(CleanGroup(ng))); ng[0] = 0;   // (inside the custom menu being edited)
        }
    }
    if (ImGui::Button("Duplicate them")) {
        // picked groups as groups ("<name> copy", widgets too), loose elements one by one
        std::vector<int> copies; std::vector<std::string> newGroups;
        auto names = GroupNames();
        for (auto& g : g_multiG) {
            std::string ng = g + " copy"; for (int k = 2; std::find(names.begin(), names.end(), ng) != names.end(); ++k) ng = g + " copy " + std::to_string(k);
            names.push_back(ng);
            std::vector<Elem> add; for (auto& e : els) if (InGroup(e.group, g)) { Elem c = e; c.group = ng + e.group.substr(g.size()); c.x += 10; c.y += 10; add.push_back(c); }
            for (auto& c : add) els.push_back(c);
            MenuCopy(g, ng); newGroups.push_back(ng);
        }
        for (int i : std::vector<int>(g_multi)) { Elem c = els[i]; c.x += 10; c.y += 10; c.name += " copy"; els.push_back(c); copies.push_back((int)els.size() - 1); }
        g_multi = copies; g_multiG = newGroups; g_sel = copies.empty() ? -1 : copies.back(); g_dirty = true;
    }
    ImGui::SameLine(); if (ImGui::Button("Delete them")) DeleteSelected();
}

void DrawOverlay(int mode) {
    const bool am = anim::g.on;           // animation mode: the layout being edited is shown (no on-screen editing)
    bool editing = g_edit && g_panelOpen && !am;
    bool panel = g_panelOpen || am;
    const bool testMode = uied::testMode || am;
    g_panelOpen = false;
    g_nowMode = mode;
    if (am) anim::Frame();
    if (g_state != 2) return;
    Trim();
    if (DrawMenu(ImGui::GetBackgroundDrawList())) { g_dragging = -1; return; }   // the menu, when it's open, is all that's drawn
    if (!editing && !testMode && (!g_show || mode < 0 || mode >= NumModes())) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();   // under the panel, over the game
    // while editing or testing: the layout being edited; otherwise the one for what we're doing now
    {
        auto& lay = g_layout[editing || testMode ? g_editMode : mode];
        if (MenuWs()) GridFollow();
        g_drawList = &lay;
        for (int i : DrawOrder(lay)) if (!(MenuWs() && GridPieceOff(lay[i]))) DrawElem(dl, lay[i], editing);
        // a custom menu being edited: the shared pieces it shows, as they will be over it (edited where they live)
        if (MenuWs() && !g_menuEd.empty()) if (Page* pg = PageOf(g_menuEd)) { g_titlePage = pg; DrawShared(dl, *pg, g_menuEd); g_titlePage = nullptr; }
        if ((editing || panel) && MenuWs())   // grids: the other cells faded, with sample items (the pieces are the first cell)
            for (auto& w : g_widgets) if (GridLike(w) && !MenuEdOff(w.group)) {
                bool shown = false; for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.visible) { shown = true; break; }
                if (shown) DrawGrid(dl, PageOf(OwnerPage(w.group)), w, false, 0, true);
            }
    }
    // grids: a picked grid lights up every cell (and its whole box); a picked piece of one, its copy in every cell
    auto gridOutlines = [&](ImDrawList* fg, ImVec2* ga, ImVec2* gb) {
        if (!g_hiSel || !MenuWs()) return;
        auto ring = [&](ImVec2 a, ImVec2 b, float pad, ImU32 c, float th) {
            fg->AddRect(ImVec2(a.x - pad, a.y - pad), ImVec2(b.x + pad, b.y + pad), IM_COL32(0, 0, 0, 160), 0.f, 0, th + 1.5f);
            fg->AddRect(ImVec2(a.x - pad, a.y - pad), ImVec2(b.x + pad, b.y + pad), c, 0.f, 0, th);
        };
        for (auto& w : g_widgets) {
            if (!GridLike(w) || MenuEdOff(w.group)) continue;
            bool whole = !g_selGroup.empty() && InGroup(w.group, g_selGroup);   // the grid (or a group around it) picked
            if (whole) {
                int cell = 0, pick = GridHasTiles(w) && g_tileSel.count(w.group) ? g_tileSel[w.group] : -1;   // (the tile set up in its panel: brighter)
                GridEachCell(w, nullptr, [&](ImVec2 a, ImVec2 b) {
                    if (cell++ == pick) ring(a, b, 1, IM_COL32(255, 220, 60, 255), 2.5f);
                    else ring(a, b, -1, IM_COL32(60, 235, 90, 200), 1.5f);
                });
                ImVec2 a, b;
                if (ga && gb && GridBox(w, a, b)) { ga->x = std::min(ga->x, a.x); ga->y = std::min(ga->y, a.y); gb->x = std::max(gb->x, b.x); gb->y = std::max(gb->y, b.y); }
                continue;
            }
            for (int i = 0; i < (int)Elems().size(); ++i) {   // a piece of it picked: its copies too
                Elem& e = Elems()[i];
                if (!(i == g_sel || PickedOf(i)) || !InGroup(e.group, w.group) || !GridPiece(e)) continue;
                GridEachCell(w, &e, [&](ImVec2 a, ImVec2 b) { ring(a, b, 2, IM_COL32(60, 235, 90, 150), 1.5f); });
            }
        }
    };
    if (!editing) {
        g_dragging = -1;
        if (panel && (testMode || mode == g_editMode)) gridOutlines(ImGui::GetForegroundDrawList(), nullptr, nullptr);
        // the editor tab is open without on-screen editing: still outline what's picked, when that layout is the one shown
        if (panel && g_hiSel && (testMode || mode == g_editMode)) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            for (int i = 0; i < (int)Elems().size(); ++i) {
                Elem& e = Elems()[i];
                if (i != g_sel && !InGroup(e.group, g_selGroup)) continue;
                bool mine; if (!e.visible || !RowShown(e, mine)) continue;
                ImVec2 a, b; ElemRect(e, a, b);
                fg->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), IM_COL32(0, 0, 0, 200), 0.f, 0, 4.f);
                fg->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), IM_COL32(60, 235, 90, 255), 0.f, 0, 2.5f);
            }
        }
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImVec2 ga(1e9f, 1e9f), gb(-1e9f, -1e9f);   // the selected group's box
    for (int i = 0; i < (int)Elems().size(); ++i) {
        ImVec2 a, b; ElemRect(Elems()[i], a, b);
        bool inGroup = InGroup(Elems()[i].group, g_selGroup);
        bool sel = i == g_sel || inGroup || PickedOf(i);
        if (!sel && GridPieceOff(Elems()[i])) continue;   // (a grid's piece for other kinds of tile)
        // the selected texture / text (or the selected group's members): a green outline on screen
        const ImU32 green = IM_COL32(60, 235, 90, 255);
        if (sel && g_hiSel) {
            fg->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), IM_COL32(0, 0, 0, 200), 0.f, 0, 4.f);   // dark edge so it shows on any colour
            fg->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), green, 0.f, 0, 2.5f);
        } else fg->AddRect(a, b, IM_COL32(120, 200, 255, 160), 0.f, 0, 1.f);
        if (i == g_sel) fg->AddRectFilled(ImVec2(b.x - 6, b.y - 6), ImVec2(b.x + 6, b.y + 6), green);   // size handle
        if (inGroup) { ga.x = std::min(ga.x, a.x); ga.y = std::min(ga.y, a.y); gb.x = std::max(gb.x, b.x); gb.y = std::max(gb.y, b.y); }
    }
    gridOutlines(fg, &ga, &gb);
    if (g_hiSel && gb.x > ga.x) fg->AddRect(ImVec2(ga.x - 4, ga.y - 4), ImVec2(gb.x + 4, gb.y + 4), IM_COL32(255, 150, 40, 255), 3.f, 0, 2.f);
    bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::IsAnyItemActive();
    if (g_dragging < 0 && ImGui::IsMouseClicked(0) && !overUi) {
        ImVec2 m = io.MousePos;
        // the selected element's size handle first, then the topmost element under the mouse
        if (g_sel >= 0 && g_sel < (int)Elems().size()) {
            ImVec2 a, b; ElemRect(Elems()[g_sel], a, b);
            if (fabsf(m.x - b.x) <= 8 && fabsf(m.y - b.y) <= 8) { g_dragging = g_sel; g_dragMode = 1; }
        }
        std::vector<int> order = DrawOrder(Elems());
        if (g_dragging < 0) for (int oi = (int)order.size() - 1; oi >= 0; --oi) {   // topmost first
            int i = order[oi];
            if (!Elems()[i].visible || GridPieceOff(Elems()[i])) continue;
            ImVec2 a, b; ElemRect(Elems()[i], a, b);
            if (m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y) {
                g_dragging = i; g_dragMode = 0;
                const std::string& grp = Elems()[i].group;
                // Ctrl / Shift + click: add it to (or take it out of) the picked elements
                if (io.KeyCtrl || io.KeyShift) {
                    if (PickCount() == 0 && g_sel >= 0 && g_sel != i) g_multi.push_back(g_sel);
                    if (PickCount() == 0 && !g_selGroup.empty()) g_multiG.push_back(g_selGroup);
                    MultiToggle(i); g_sel = i; g_selGroup.clear(); g_dragging = -1;
                    break;
                }
                // one of several picked: they all move
                if (PickCount() > 1 && PickedOf(i)) { g_dragMode = 3; g_sel = i; break; }
                ClearMulti();
                // a member of the picked group: that group moves; Alt + click on any grouped element: its outermost group
                if (InGroup(grp, g_selGroup)) { g_sel = -1; g_dragMode = 2; }
                else if (!RelGroup(grp).empty() && io.KeyAlt) { std::string r = RelGroup(grp); g_selGroup = IntoMenu(r.substr(0, r.find('/'))); g_sel = -1; g_dragMode = 2; }
                else { g_sel = i; g_selGroup.clear(); }
                break;
            }
        }
        if (g_dragging < 0 && MenuWs() && !(io.KeyCtrl || io.KeyShift))   // nothing under the mouse: a grid's faded cells
            for (auto& w : g_widgets) {
                ImVec2 a, b;
                if (!GridLike(w) || MenuEdOff(w.group) || !GridBox(w, a, b) || m.x < a.x || m.x > b.x || m.y < a.y || m.y > b.y) continue;
                int first = -1; for (int i = 0; i < (int)Elems().size(); ++i) if (InGroup(Elems()[i].group, w.group)) { first = i; break; }
                if (first < 0) continue;
                ClearMulti(); g_selGroup = w.group; g_sel = -1; g_dragging = first; g_dragMode = 2;
                break;
            }
        if (g_dragging >= 0) {
            Elem& e = Elems()[g_dragging];
            g_dragStart = m; g_dragOrig = ImVec2(e.x, e.y); g_dragOrigWH = ImVec2(e.w, e.h); g_dragOrigScale = e.scale;
            g_groupOrig.clear(); for (auto& x : Elems()) g_groupOrig.push_back(ImVec2(x.x, x.y));
        }
    }
    if (g_dragging >= 0 && g_dragging < (int)Elems().size()) {
        Elem& e = Elems()[g_dragging];
        float k = Unit();
        ImVec2 d((io.MousePos.x - g_dragStart.x) / k, (io.MousePos.y - g_dragStart.y) / k);
        if (g_dragMode == 0) { e.x = roundf(g_dragOrig.x + d.x); e.y = roundf(g_dragOrig.y + d.y); }
        else if (g_dragMode == 2 || g_dragMode == 3) {
            for (size_t k2 = 0; k2 < Elems().size() && k2 < g_groupOrig.size(); ++k2)
                if (g_dragMode == 2 ? InGroup(Elems()[k2].group, g_selGroup) : PickedOf((int)k2)) { Elems()[k2].x = roundf(g_groupOrig[k2].x + d.x); Elems()[k2].y = roundf(g_groupOrig[k2].y + d.y); }
        }
        else if (e.type != E_TEXT) {
            float ax = (e.anchor % 3) * 0.5f, ay = (e.anchor / 3) * 0.5f;
            float nw = std::max(2.f, roundf(g_dragOrigWH.x + d.x)), nh = std::max(2.f, roundf(g_dragOrigWH.y + d.y));
            if (io.KeyShift && g_dragOrigWH.x > 0 && g_dragOrigWH.y > 0) nh = roundf(nw * g_dragOrigWH.y / g_dragOrigWH.x);   // keep the shape
            e.x = g_dragOrig.x + ax * (nw - g_dragOrigWH.x); e.y = g_dragOrig.y + ay * (nh - g_dragOrigWH.y);
            e.w = nw; e.h = nh;
        } else {
            ImVec2 sz = ElemSize(e); float base = sz.y / std::max(0.01f, e.scale);
            e.scale = std::max(0.1f, g_dragOrigScale + d.y / std::max(1.f, base));
        }
        g_dirty = true;
        if (!ImGui::IsMouseDown(0)) g_dragging = -1;
    }
}

// ---------------------------------------------------------------- panel
static char g_filter[64] = ""; static int g_packSel = 0; static float g_thumb = 64.f; static int g_texSel = -1;
static char g_fontPreview[128] = "1ST  Lap 2/3  0:42.17"; static float g_fontScale = 1.f; static int g_fontSel = -1;
static bool g_onlyHud = false;

static const char* FmtName(uint32_t f) {
    static char b[8];
    if (f == 0x15) return "A8R8G8B8"; if (f == 0x29) return "P8 (palette)";
    memcpy(b, &f, 4); b[4] = 0; for (int i = 0; i < 4; ++i) if (b[i] < 32 || b[i] > 126) return "?"; return b;
}
static bool PassFilter(const Tex& t) {
    if (g_packSel > 0) { std::string p = t.source + ": " + t.pack; if (p != g_packs[g_packSel - 1]) return false; }
    if (g_onlyHud && !t.hud) return false;
    if (g_filter[0]) {
        std::string n = t.name, q = g_filter;
        for (auto& c : n) c = (char)toupper((unsigned char)c); for (auto& c : q) c = (char)toupper((unsigned char)c);
        if (n.find(q) == std::string::npos) return false;
    }
    return true;
}
static const char* kTokenHelp =
    "Live values (type them in braces):\n"
    "  anywhere: {fps} {name} {speed} (km/h) {mph} {clock}\n"
    "  race: {pos} {pos_suffix} (st/nd/rd/th) {racers} {lap} {laps} {race_time} {lap_time} {last_lap} {best_lap}\n"
    "        {gap_ahead} {gap_behind} {gap_leader} {title} {status} {score} (your drift points)\n"
    "  a leaderboard row N: {rN.pos} {rN.pos_suffix} {rN.name} {rN.gap} (to you) {rN.gap_leader} {rN.lap} {rN.best}\n"
    "        {rN.gap_name} (like the game: gaps for 10 s, then names for 2 s; your row always your name;\n"
    "        drift: points, then names) {rN.score} (drift points)\n"
    "        {rN.last} {rN.time} (finish time) {rN.status} (finished / out) {rN.me} (1 on your row) {rN.player}\n"
    "  in an element set to leaderboard row N, the same without 'rN.': {pos} {name} {gap} ...";
static std::vector<std::string> GroupNames() {   // every group, nested ones and the groups around them too
    std::vector<std::string> v;
    for (auto& e : Elems())
        for (std::string g = e.group; !g.empty(); g = GroupParent(g))
            if (std::find(v.begin(), v.end(), g) == v.end()) v.push_back(g);
    std::sort(v.begin(), v.end());
    return v;
}
// move group g (and what's inside it) to path to
static void RebaseGroup(const std::string& g, const std::string& to) {
    MenuRebase(g, to);
    for (auto& l : g_layout) for (auto& e : l)   // (cutters aimed at the group follow it)
        if (e.cutTarget.compare(0, 6, "group:") == 0 && InGroup(e.cutTarget.substr(6), g)) e.cutTarget = "group:" + CleanGroup(to + e.cutTarget.substr(6 + g.size()));
    for (auto& e : Elems()) if (InGroup(e.group, g)) e.group = CleanGroup(to + e.group.substr(g.size()));
    if (InGroup(g_selGroup, g)) g_selGroup = CleanGroup(to + g_selGroup.substr(g.size()));
    g_dirty = true;
}
// Alt + Up / Down in the list: the selected element (or the selected group) swaps places with the entry above / below
// it in its group, as the list shows them (an element, or a whole group inside it: shown where its first element is).
// The group's elements are laid back into the list positions they had, in the new order, so nothing else moves.
static void MoveInList(int dir) {
    auto& els = Elems();
    std::string parent, mine;
    if (!g_selGroup.empty()) { parent = GroupParent(g_selGroup); mine = g_selGroup; }
    else if (g_sel >= 0 && g_sel < (int)els.size()) { parent = els[g_sel].group; mine = "#" + std::to_string(g_sel); }
    else return;
    // the parent's entries in list order, each with its elements (in list order)
    std::vector<int> slots;
    std::vector<std::string> keys;
    std::vector<std::vector<int>> members;
    for (int i = 0; i < (int)els.size(); ++i) {
        const std::string& g = els[i].group;
        if (!parent.empty() && !InGroup(g, parent)) continue;
        slots.push_back(i);
        std::string key;
        if (g == parent) key = "#" + std::to_string(i);
        else { std::string rest = parent.empty() ? g : g.substr(parent.size() + 1); key = (parent.empty() ? "" : parent + "/") + rest.substr(0, rest.find('/')); }
        size_t k = std::find(keys.begin(), keys.end(), key) - keys.begin();
        if (k == keys.size()) { keys.push_back(key); members.emplace_back(); }
        members[k].push_back(i);
    }
    int pos = (int)(std::find(keys.begin(), keys.end(), mine) - keys.begin());
    int to = pos + dir;
    if (pos >= (int)keys.size() || to < 0 || to >= (int)keys.size()) return;   // already first / last in its group
    std::swap(members[pos], members[to]);
    // lay them back into the same positions
    std::vector<int> order; for (auto& m : members) order.insert(order.end(), m.begin(), m.end());
    std::vector<Elem> copy; for (int i : order) copy.push_back(els[i]);
    int newSel = -1;
    for (size_t k = 0; k < slots.size(); ++k) { els[slots[k]] = copy[k]; if (g_selGroup.empty() && order[k] == g_sel) newSel = slots[k]; }
    if (g_selGroup.empty() && newSel >= 0) g_sel = newSel;
    g_dirty = true;
}
static void GroupPanel() {
    std::string g = g_selGroup;
    std::vector<int> mem; for (int i = 0; i < (int)Elems().size(); ++i) if (InGroup(Elems()[i].group, g)) mem.push_back(i);
    if (mem.empty()) { g_selGroup.clear(); return; }
    if (MenuEdOff("") && g == g_menuEd) { g_selGroup.clear(); return; }   // (a custom menu is the page, not a group)
    std::string parent = GroupParent(g);
    bool menuTop = MenuEdOff("") && parent == g_menuEd;   // (at the top of the custom menu being edited)
    int subs = 0; for (auto& n : GroupNames()) if (n != g && InGroup(n, g) && GroupParent(n) == g) ++subs;
    ImGui::Text("Group: %s  (%d elements%s)", GroupLeaf(g).c_str(), (int)mem.size(), subs ? (", " + std::to_string(subs) + " groups inside").c_str() : "");
    if (!parent.empty() && !menuTop) { ImGui::SameLine(); ImGui::TextDisabled("inside %s", RelGroup(parent).c_str()); ImGui::SameLine(); if (ImGui::SmallButton("select that")) { g_selGroup = parent; return; } }
    ImGui::TextDisabled("Drag any of its elements on screen to move the group (Alt + drag: a grouped element's outermost group).");
    static char nb[64]; static std::string nbFor;
    if (nbFor != g) { snprintf(nb, sizeof nb, "%s", GroupLeaf(g).c_str()); nbFor = g; }
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("name##grp", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue) && CleanGroup(nb)[0]) {
        RebaseGroup(g, (parent.empty() ? "" : parent + "/") + CleanGroup(nb)); nbFor = g_selGroup; return;
    }
    ImGui::SameLine(); ImGui::TextDisabled("(Enter to rename)");
    {   // linked groups: the Link button (the same items as the list's right-click menu)
        int k = -1; LinkSet* ls = LinkOf(g, g_editMode, &k);
        if (ls) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.62f, 1.f));
        if (ImGui::Button(ls ? "Linked..." : "Link...")) ImGui::OpenPopup("##grplink");
        if (ls) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Linked groups keep the same pieces (on several menus): an edit to one goes to the others.\nEach keeps its own place - moving a whole group moves only that one.");
        if (ls) {
            ImGui::SameLine(); ImGui::TextDisabled("with");
            std::string go;
            for (auto& m : ls->m) if (m.group != g) { ImGui::SameLine(); if (ImGui::SmallButton((m.group + "##lnk").c_str())) go = m.group; }
            if (!go.empty()) { LinkGoTo(go); return; }
        }
        if (ImGui::BeginPopup("##grplink")) { LinkMenuItems(g); ImGui::EndPopup(); }
        ImGui::SameLine();
        bool named = ls && !ls->name.empty();
        if (ImGui::Button(named ? ("Linked group: " + ls->name + "##lgsave").c_str() : "Save as linked group...")) ImGui::OpenPopup("##grplgsave");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(named ? "Listed on the Linked groups page. Click to rename it." : "Lists it (and what it's linked with) on the Linked groups page, under a name:\nadd more linked copies of it from there.");
        if (ImGui::BeginPopup("##grplgsave")) { if (LinkSaveUI(g)) ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
        if (g_selGroup != g) return;   // (went to another, or the group changed)
    }
    // put it inside a larger group: an existing one (not itself or one inside it), or a new one by name
    {
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##grpparent", "put inside...")) {
            if (!parent.empty() && !menuTop && ImGui::Selectable("(top level)")) { RebaseGroup(g, IntoMenu(GroupLeaf(g))); ImGui::EndCombo(); return; }
            for (auto& n : GroupNames()) {
                if (InGroup(n, g) || n == parent || MenuEdOff(n) || RelGroup(n).empty()) continue;
                if (ImGui::Selectable((RelGroup(n) + "##" + n).c_str())) { RebaseGroup(g, n + "/" + GroupLeaf(g)); ImGui::EndCombo(); return; }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        static char pb[64] = "";
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputTextWithHint("##grpnewparent", "new larger group", pb, sizeof pb, ImGuiInputTextFlags_EnterReturnsTrue) && CleanGroup(pb)[0]) {
            RebaseGroup(g, (parent.empty() ? "" : parent + "/") + CleanGroup(pb) + "/" + GroupLeaf(g)); pb[0] = 0; return;
        }
        ImGui::SameLine(); ImGui::TextDisabled("(Enter)");
    }
    // layer: the group's members keep their order among themselves; the change shifts them all
    {
        int glo = Elems()[mem[0]].layer, ghi = glo;
        for (int i : mem) { glo = std::min(glo, Elems()[i].layer); ghi = std::max(ghi, Elems()[i].layer); }
        int lo, hi; LayerRange(lo, hi);
        int L = glo;
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("layer (lowest member)##grp", &L) && L != glo) { for (int i : mem) Elems()[i].layer += L - glo; g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("to front##grp")) { int d = hi + 1 - glo; for (int i : mem) Elems()[i].layer += d; g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("to back##grp")) { int d = lo - 1 - ghi; for (int i : mem) Elems()[i].layer += d; g_dirty = true; }
        if (ghi != glo) { ImGui::SameLine(); ImGui::TextDisabled("(layers %d..%d)", glo, ghi); }
    }
    // move: the first element's position stands for the group's; the change goes to all of them
    float pos[2] = { Elems()[mem[0]].x, Elems()[mem[0]].y };
    ImGui::SetNextItemWidth(220);
    if (ImGui::DragFloat2("move (first element's position)", pos, 0.5f, -2000.f, 2000.f, "%.0f")) {
        float dx = pos[0] - Elems()[mem[0]].x, dy = pos[1] - Elems()[mem[0]].y;
        for (int i : mem) { Elems()[i].x += dx; Elems()[i].y += dy; } g_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##gl", ImGuiDir_Left)) { for (int i : mem) Elems()[i].x -= 1; g_dirty = true; } ImGui::SameLine();
    if (ImGui::ArrowButton("##gr", ImGuiDir_Right)) { for (int i : mem) Elems()[i].x += 1; g_dirty = true; } ImGui::SameLine();
    if (ImGui::ArrowButton("##gu", ImGuiDir_Up)) { for (int i : mem) Elems()[i].y -= 1; g_dirty = true; } ImGui::SameLine();
    if (ImGui::ArrowButton("##gd", ImGuiDir_Down)) { for (int i : mem) Elems()[i].y += 1; g_dirty = true; }
    bool allVis = true; for (int i : mem) allVis &= Elems()[i].visible;
    if (ImGui::Checkbox("visible##grp", &allVis)) { for (int i : mem) Elems()[i].visible = allVis; g_dirty = true; }
    { std::vector<Elem*> v; for (int i : mem) v.push_back(&Elems()[i]); FlagBoxes(v, "##grp"); }
    {   // anchor for all of them (each keeps its place on screen)
        int an = Elems()[mem[0]].anchor; bool mixed = false;
        for (int i : mem) mixed |= Elems()[i].anchor != an;
        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("anchored to##grp", mixed ? "(mixed)" : kAnchors[an])) {
            for (int a = 0; a < 9; ++a) if (ImGui::Selectable(kAnchors[a], !mixed && a == an)) { for (int i : mem) SetAnchorKeep(Elems()[i], a); g_dirty = true; }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets every element in the group (and the groups inside it) to this anchor, without moving them.");
    }
    if (ImGui::Button("Duplicate group")) {
        std::string ng = g + " copy"; int k = 2; auto names = GroupNames();
        while (std::find(names.begin(), names.end(), ng) != names.end()) ng = g + " copy " + std::to_string(k++);
        for (int i : mem) { Elem c = Elems()[i]; c.group = ng + c.group.substr(g.size()); c.x += 10; c.y += 10; Elems().push_back(c); }
        MenuCopy(g, ng);
        g_selGroup = ng; g_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ungroup")) {   // drops this level only: what's directly in it goes to the group around it
        for (int i : mem) Elems()[i].group = CleanGroup(parent + Elems()[i].group.substr(g.size()));
        g_widgets.erase(std::remove_if(g_widgets.begin(), g_widgets.end(), [&](const Widget& w) { return w.group == g; }), g_widgets.end());
        g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(), [&](const Page& p) { return p.group == g; }), g_pages.end());
        MenuRebase(g, parent);
        g_selGroup = parent; g_dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes this group only; its elements and the groups inside it move up one level.");
    ImGui::SameLine();
    if (ImGui::Button("Delete group and its elements")) {
        for (int k = (int)mem.size() - 1; k >= 0; --k) Elems().erase(Elems().begin() + mem[k]);
        MenuForget(g);
        g_selGroup.clear(); g_sel = -1; g_dirty = true;
        return;
    }
    GroupMenuProps(g);
}
static void GroupField(Elem& e) {
    static char gb[64]; static const Elem* gbFor = nullptr; static std::string gbWas;
    if (gbFor != &e || gbWas != e.group) { snprintf(gb, sizeof gb, "%s", RelGroup(e.group).c_str()); gbFor = &e; gbWas = e.group; }
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputTextWithHint("group", "(none) - type a name", gb, sizeof gb, ImGuiInputTextFlags_EnterReturnsTrue)) { e.group = IntoMenu(CleanGroup(gb)); gbWas = e.group; g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enter to set. A group inside a larger one: Larger/Smaller (e.g. Race HUD/Leaderboard).");
    auto names = GroupNames();
    if (!names.empty()) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(130);
        if (ImGui::BeginCombo("##grppick", "add to...")) {
            if (ImGui::Selectable("(no group)")) { e.group = IntoMenu(""); g_dirty = true; }
            for (auto& n : names) if (!MenuEdOff(n) && !RelGroup(n).empty() && ImGui::Selectable((RelGroup(n) + "##" + n).c_str(), n == e.group)) { e.group = n; g_dirty = true; }
            ImGui::EndCombo();
        }
    }
    if (!RelGroup(e.group).empty()) { ImGui::SameLine(); if (ImGui::SmallButton("select group")) { g_selGroup = e.group; g_sel = -1; } }
}
// "Row 1 name" and "Row 12 name" share the scheme "Row # name": every run of digits becomes #
static std::string NameScheme(const std::string& n) {
    std::string o;
    for (size_t i = 0; i < n.size(); ++i) {
        if (isdigit((unsigned char)n[i])) { o += '#'; while (i + 1 < n.size() && isdigit((unsigned char)n[i + 1])) ++i; }
        else o += n[i];
    }
    return o;
}
// copy chosen properties of e to every other element of the same kind whose name follows the same scheme
static void CopyToSimilar(Elem& e) {
    static bool cSize = true, cCol = true, cLook = true, cAlign = true, cText = false, cHi = true, cVis = true, cAnchor = true, cX = false, cY = false;
    std::string sch = NameScheme(e.name);
    std::vector<int> to;
    for (int i = 0; i < (int)Elems().size(); ++i) {
        Elem& o = Elems()[i];
        if (&o != &e && o.type == e.type && NameScheme(o.name) == sch) to.push_back(i);
    }
    if (!ImGui::CollapsingHeader(("Copy to similar names (" + std::to_string(to.size()) + ")###cpsim").c_str())) return;
    ImGui::TextDisabled("Name scheme: \"%s\" (numbers count as #), same kind (%s)", sch.c_str(), TypeKey(e.type));
    if (to.empty()) { ImGui::TextDisabled("No other element follows this name scheme."); return; }
    ImGui::Checkbox("size / scale", &cSize); ImGui::SameLine();
    ImGui::Checkbox("colour", &cCol); ImGui::SameLine();
    ImGui::Checkbox(e.type == E_TEXT ? "font" : "texture + UV", &cLook); ImGui::SameLine();
    if (e.type == E_TEXT) { ImGui::Checkbox("align + box width", &cAlign); ImGui::SameLine(); ImGui::Checkbox("text", &cText); }
    ImGui::Checkbox("my-row colour", &cHi); ImGui::SameLine();
    ImGui::Checkbox("visible", &cVis); ImGui::SameLine();
    ImGui::Checkbox("anchor", &cAnchor); ImGui::SameLine();
    ImGui::Checkbox("x", &cX); ImGui::SameLine();
    ImGui::Checkbox("y", &cY);
    if (ImGui::Button(("Copy to " + std::to_string(to.size()) + " similar").c_str())) {
        for (int i : to) {
            Elem& o = Elems()[i];
            if (cSize) { o.scale = e.scale; if (e.type != E_TEXT) { o.w = e.w; o.h = e.h; } }
            if (cLook && e.type == E_SHAPE) { memcpy(o.rad, e.rad, sizeof o.rad); o.radLink = e.radLink; o.stroke = e.stroke; memcpy(o.fillCol, e.fillCol, sizeof o.fillCol); o.fillTok = e.fillTok; o.fillTokA = e.fillTokA; o.strokeAlign = e.strokeAlign; o.strokeGrad = e.strokeGrad; o.fillGrad = e.fillGrad; }
            if (cCol) { memcpy(o.col, e.col, sizeof o.col); o.colTok = e.colTok; o.colTokA = e.colTokA; }
            if (cLook) {
                if (e.type == E_TEXT) { o.font = e.font; o.fontIdx = e.fontIdx; }
                else { o.tex = e.tex; o.pack = e.pack; o.texIdx = e.texIdx; o.u0 = e.u0; o.v0 = e.v0; o.u1 = e.u1; o.v1 = e.v1; o.rot = e.rot; }
            }
            if (e.type == E_TEXT && cAlign) { o.align = e.align; o.w = e.w; o.wrap = e.wrap; }
            if (e.type == E_TEXT && cText) o.text = e.text;
            if (cHi) { o.hiMine = e.hiMine; memcpy(o.hiCol, e.hiCol, sizeof o.hiCol); o.hiTok = e.hiTok; o.hiTokA = e.hiTokA; }
            if (cVis) o.visible = e.visible;
            if (cAnchor) o.anchor = e.anchor;
            if (cX) o.x = e.x;
            if (cY) o.y = e.y;
        }
        g_dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        std::string l; for (int i : to) l += Elems()[i].name + "\n";
        ImGui::SetTooltip("%s", l.c_str());
    }
}
static void Selected(Elem& e) {
    ImGui::PushID(&e);
    char nb[64]; snprintf(nb, sizeof nb, "%s", e.name.c_str());
    if (ImGui::InputText("name", nb, sizeof nb)) { e.name = nb; g_dirty = true; }
    GroupField(e);
    if (ImGui::Checkbox("visible", &e.visible)) g_dirty = true;
    FlagBoxes({&e}, "");
    ImGui::SameLine();
    { int an = e.anchor; ImGui::SetNextItemWidth(140); if (ImGui::Combo("anchored to", &an, kAnchors, 9)) { SetAnchorKeep(e, an); g_dirty = true; } }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The screen corner / edge it keeps its distance to when the screen's shape changes. Changing it doesn't move it.");
    {
        int lo, hi; LayerRange(lo, hi);
        ImGui::SetNextItemWidth(100); if (ImGui::InputInt("layer", &e.layer)) g_dirty = true;
        ImGui::SameLine(); if (ImGui::SmallButton("to front")) { e.layer = hi + 1; g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("to back")) { e.layer = lo - 1; g_dirty = true; }
        ImGui::SameLine(); ImGui::TextDisabled("(higher = on top)");
    }
    ImGui::SetNextItemWidth(220); if (ImGui::DragFloat2("position", &e.x, 0.5f, -2000.f, 2000.f, "%.0f")) g_dirty = true;
    if (e.type == E_IMAGE) {
        ImGui::SetNextItemWidth(220); if (ImGui::DragFloat2("size", &e.w, 0.5f, 1.f, 2000.f, "%.0f")) g_dirty = true;
        if (e.texIdx >= 0) { ImGui::SameLine(); if (ImGui::SmallButton("texture's own size")) { e.w = g_tex[e.texIdx].w; e.h = g_tex[e.texIdx].h; g_dirty = true; } }
        ImGui::SetNextItemWidth(220); if (ImGui::DragFloat4("UV (u0 v0 u1 v1)", &e.u0, 0.002f, -1.f, 2.f, "%.3f")) g_dirty = true;
        ImGui::SameLine(); if (ImGui::SmallButton("flip H")) { std::swap(e.u0, e.u1); g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("flip V")) { std::swap(e.v0, e.v1); g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("reset UV")) { e.u0 = e.v0 = 0; e.u1 = e.v1 = 1; g_dirty = true; }
        ImGui::SetNextItemWidth(220); if (ImGui::DragFloat("rotation", &e.rot, 0.5f, -360.f, 360.f, "%.1f deg")) g_dirty = true;
        ImGui::SameLine(); if (ImGui::SmallButton("+90")) { e.rot = fmodf(e.rot + 90, 360); g_dirty = true; }
        ImGui::SetNextItemWidth(220); if (ImGui::DragFloat2("pivot (from centre)", &e.pivX, 0.25f, -2000.f, 2000.f, "%.1f")) g_dirty = true;
        if (ImGui::Checkbox("minimap", &e.minimap)) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drawn as the game's minimap: the track map around your car (round, in this box), turned\nheading-up when the game's radar option is on, with the game's icons (racers, shops, events,\nyour car) and other mods' markers (the multiplayer mod's players). Its texture and UV aren't used.\nAdd the frame and north arrow as images ({map_north} turns the arrow; {map_heading} turns your own\narrow if you hide the game's). Flag it \"replace: minimap\" to hide the stock one.");
        if (e.minimap) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(90); if (ImGui::DragFloat("zoom", &e.mapZoom, 0.01f, 0.1f, 10.f, "x%.2f")) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Times the game's own zoom (x1: the stock minimap's view across this box).");
            ImGui::SameLine(); ImGui::SetNextItemWidth(90); if (ImGui::DragFloat("icons", &e.mapBlip, 0.01f, 0.1f, 10.f, "x%.2f")) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Icon size (x1: as big against the map as the stock minimap's).");
            ImGui::SameLine(); if (ImGui::Checkbox("your car's icon", &e.mapPlayer)) g_dirty = true;
        }
        if (ImGui::Checkbox("world map", &e.worldMap)) { if (e.worldMap) e.minimap = false; g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drawn as the world map: the whole city map in this box (your Minimap page art), the game's icons (shops,\nevents, racers, your car) and other mods' markers (the multiplayer mod's players). With UniPad standing a page\nin for the game's World Map: its view (Full Map / a region: LT / RT), the selected icon's ring and the types\nhidden there. Its texture and UV aren't used.");
        if (e.worldMap) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(90); if (ImGui::DragFloat("icons##wm", &e.mapBlip, 0.01f, 0.1f, 10.f, "x%.2f")) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Icon size (x1: as big against this box as the stock world map's).");
            ImGui::SameLine(); if (ImGui::Checkbox("your car's icon##wm", &e.mapPlayer)) g_dirty = true;
        }
        if (ImGui::Checkbox("clip to a circle", &e.circle)) g_dirty = true;
        ImGui::SameLine(); if (ImGui::Checkbox("glow (additive)", &e.additive)) g_dirty = true;
        ImGui::SameLine();
        {
            static char fb[16]; snprintf(fb, sizeof fb, e.follow ? "%08X" : "", e.follow);
            ImGui::SetNextItemWidth(90);
            if (ImGui::InputTextWithHint("follows game object", "name hash", fb, sizeof fb, ImGuiInputTextFlags_CharsHexadecimal)) { e.follow = (unsigned)strtoul(fb, nullptr, 16); g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A stock HUD object's FEng name hash: this image copies where the game puts it, its size, turn,\nuv and whether it's shown, every frame (position / rotation here are added; texture: its own unless set).");
            if (e.follow) { GameObject g; ImGui::SameLine(); ImGui::TextDisabled(gameObject && gameObject(e.follow, g) ? "(on screen)" : "(not shown now)"); }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The point it turns about, from its centre: a needle's hub.");
        int ifam, iid;
        if (IconRef(e.tex, ifam, iid)) {
            ImGui::Text("button icon: %s", IconName(ifam, iid).c_str());
            ImGui::SameLine(); if (ImGui::SmallButton("icon size (32)")) { e.w = e.h = 32; g_dirty = true; }
            ImGui::SameLine();
            bool follow = ifam < 0;
            if (ImGui::Checkbox("follows the controller", &follow)) {
                std::string fam = follow ? "auto" : iconFamilies.empty() ? "xbox" : iconFamilies[0];
                e.tex = "icon:" + fam + ":" + iconKeys[iid]; g_dirty = true;
            }
        } else if (vecart::IsRef(e.tex)) {
            std::string vn = vecart::RefName(e.tex);
            const vecart::Art* art = vecart::Find(vn);
            ImGui::Text("vector art: %s%s", vn.c_str(), art ? "" : " (file missing)");
            if (art && art->w > 0 && art->h > 0) {
                ImGui::SameLine(); if (ImGui::SmallButton("its own size")) { e.w = art->w; e.h = art->h; g_dirty = true; }
                ImGui::SameLine(); if (ImGui::SmallButton("fix its proportions")) { e.h = e.w * art->h / art->w; g_dirty = true; }
            }
        } else
        ImGui::Text("texture: %s (%s)", e.tex.empty() ? "(none - pick one below)" : e.tex.c_str(), e.pack.c_str());
        if (g_texSel >= 0) { ImGui::SameLine(); if (ImGui::SmallButton("use the selected texture")) { e.tex = g_tex[g_texSel].name; e.pack = g_tex[g_texSel].pack; e.texIdx = g_texSel; g_dirty = true; } }
    } else if (e.type == E_SHAPE) {
        ShapeProps(e);
        {   // (a bar the game draws: this shape takes its box and colour - the performance parts' Torque / Power meters)
            static char fb[16]; snprintf(fb, sizeof fb, e.follow ? "%08X" : "", e.follow);
            ImGui::SetNextItemWidth(90);
            if (ImGui::InputTextWithHint("follows game object##shape", "name hash", fb, sizeof fb, ImGuiInputTextFlags_CharsHexadecimal)) { e.follow = (unsigned)strtoul(fb, nullptr, 16); g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A game FEng object's name hash (from the dump): this shape takes where the game puts it and its size every\nframe (its x / y: an offset from there), its fill tinted by the game's colour; hidden while the game hides it.");
            if (e.follow) { GameObject g; ImGui::SameLine(); ImGui::TextDisabled(gameObject && gameObject(e.follow, g) ? "(on screen)" : "(not shown now)"); }
        }
    } else {
        static char tb[512];
        snprintf(tb, sizeof tb, "%s", e.text.c_str());
        if (ImGui::InputTextMultiline("text", tb, sizeof tb, ImVec2(320, 54))) { e.text = tb; g_dirty = true; }
        ImGui::SameLine(); ImGui::TextDisabled("(live values)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTokenHelp);
        ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("scale", &e.scale, 0.01f, 0.1f, 8.f, "%.2f")) g_dirty = true;
        ImGui::SameLine(); const char* al[] = { "left", "centre", "right" };
        ImGui::SetNextItemWidth(100); if (ImGui::Combo("align", &e.align, al, 3)) g_dirty = true;
        ImGui::SameLine(); ImGui::SetNextItemWidth(110); if (ImGui::DragFloat("rotation", &e.rot, 0.25f, -360.f, 360.f, "%.1f deg")) g_dirty = true;
        ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("box width (for centre/right)", &e.w, 0.5f, 0.f, 2000.f, "%.0f")) g_dirty = true;
        ImGui::SameLine(); if (ImGui::Checkbox("wrap", &e.wrap)) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Break the text into lines to fit the box width (a long message from the game).");
        ImGui::SameLine(); if (ImGui::Checkbox("animate changes", &e.swapAnim)) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("When its text changes, the old text shrinks and fades out while the new one grows and fades in\n(0.32 s) - the game's help bar swapping the profile name and the cash ({bar.ticker}).");
        std::vector<const char*> fn; int cur = -1;
        for (size_t i = 0; i < g_fonts.size(); ++i) { fn.push_back(g_fonts[i].name.c_str()); if (g_fonts[i].name == e.font) cur = (int)i; }
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("font", &cur, fn.data(), (int)fn.size())) { e.font = g_fonts[cur].name; e.fontIdx = cur; g_dirty = true; }
    }
    TokenColourEdit(e.type == E_SHAPE ? "stroke colour" : "colour / tint", e.col, e.colTok, e.colTokA);
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputInt("leaderboard row", &e.row)) { e.row = std::max(0, std::min(22, e.row)); g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = not a leaderboard row.\nN = shown only while there's an Nth racer on the board; in its text {name} {pos} {gap} ... are that racer's.");
    if (e.row > 0) {
        ImGui::SameLine(); if (ImGui::Checkbox("other colour on my row", &e.hiMine)) g_dirty = true;
        if (e.hiMine) TokenColourEdit("my-row colour", e.hiCol, e.hiTok, e.hiTokA);
    }
    {   // live-value rules
        static char sb[64], bb[64];
        snprintf(sb, sizeof sb, "%s", e.showIf.c_str());
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputTextWithHint("show only while", "value key, e.g. nos_on", sb, sizeof sb)) { e.showIf = sb; g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A live value's key (no braces). Shown only while it isn't blank or 0.\n!key: only while it is. Empty: always.");
        {
            static char tb2[64]; snprintf(tb2, sizeof tb2, "%s", e.tint.c_str());
            ImGui::SameLine(); ImGui::SetNextItemWidth(140);
            if (ImGui::InputTextWithHint("tint from", "e.g. tach_paint_1", tb2, sizeof tb2)) { e.tint = tb2; g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A live value giving \"R G B\" (0-255) that multiplies the colour - the car's custom HUD paints.");
        }
        { const char* an[] = { "still", "pulse", "blink", "keyframes", "bounce" }; ImGui::SameLine(); ImGui::SetNextItemWidth(90); if (ImGui::Combo("animation", &e.anim, an, 5)) g_dirty = true; }
        if (e.anim == 4) {   // bounce: grows and back once (from when it shows, or each time its play-on value comes on)
            ImGui::SetNextItemWidth(70); if (ImGui::DragFloat("##bsize", &e.bounceScale, 0.005f, 0.2f, 4.f, "x%.2f")) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How big it gets at the top of the bounce (x1.20: a fifth bigger; under 1: it dips).");
            ImGui::SameLine(); ImGui::SetNextItemWidth(70); if (ImGui::DragFloat("##bms", &e.bounceMs, 5.f, 20.f, 5000.f, "%.0f ms")) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long the bounce takes, up and back down.");
            ImGui::SameLine(); ImGui::SetNextItemWidth(70); if (ImGui::DragFloat("##bglow", &e.bounceGlow, 0.005f, 0.f, 1.f, "glow %.2f")) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much brighter it is at the top than at the start / end of the bounce (0: no change).\nPlays once from when it shows (or each time its play-on value comes on), then it's as it was.\nThe stock menus' arrows: x1.20, 300 ms, glow 0.18, opacity x1.");
            ImGui::SameLine(); ImGui::SetNextItemWidth(80); if (ImGui::DragFloat("bounce##balpha", &e.bounceAlpha, 0.01f, 0.f, 4.f, "opacity x%.2f")) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its opacity at the top of the bounce, times this (x1: no change; it can't go past fully solid,\nso above x1 it only shows on something that's partly see-through). The map's LT / RT: x1.30, 400 ms, glow 0, opacity x1.70.");
        }
        if (e.anim == 3) {
            static char kb[256]; snprintf(kb, sizeof kb, "%s", e.keys.c_str());
            ImGui::SetNextItemWidth(320);
            if (ImGui::InputTextWithHint("keys", "800;0 1 1 1;400 1.6 1.7 1;800 1 1 1", kb, sizeof kb)) { e.keys = kb; g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Loop length in ms (! in front: play once and hold), then keys \"time width-scale height-scale alpha [move-x move-y [red green blue]]\" separated by ;\n(scales and colours multiply, moves are units; straight lines between keys; the game's HUD animations as\nthey're stored in its .fng). Text scales from its alignment point.");
        }
        if (e.anim) {
            static char pb[96]; snprintf(pb, sizeof pb, "%s", e.playOn.c_str());
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputTextWithHint("play on", "e.g. bindheld.FENG_LTRIGGER", pb, sizeof pb)) { e.playOn = pb; g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A live value: the animation starts over each time it comes on (blank: from when the element shows).\nbindheld.<event>: while that UniPad binding is held - a button's press animation (keys with ! in front: plays once).");
        }
        {   // (images: all of them; shapes: fills and moving; text: moving)
            const char* kinds[] = { "nothing", "fill left to right", "fill bottom to top", "rotation", "reveal (turning half)", "move across", "move up" };
            ImGui::SetNextItemWidth(160);
            if (ImGui::Combo("value drives", &e.bindKind, kinds, 7)) g_dirty = true;
            if (e.bindKind == 5 && ImGui::IsItemHovered()) ImGui::SetTooltip("The element moves across with the value (from / to move, in units) - a meter's handle riding its fill.");
            if (e.bindKind == 6 && ImGui::IsItemHovered()) ImGui::SetTooltip("The element moves up with the value (from / to move, in units; up is positive) - the handle of a bar filling bottom to top.");
            if ((e.type == E_SHAPE && (e.bindKind == 3 || e.bindKind == 4)) || (e.type == E_TEXT && e.bindKind >= 1 && e.bindKind <= 4))
                { ImGui::SameLine(); ImGui::TextDisabled("(images only)"); }
            if (e.bindKind == 4 && ImGui::IsItemHovered()) ImGui::SetTooltip("Only the half on the far side of a line through the pivot is drawn; the line turns with the value\n(from / to angle; 0 = the top half shows). The game's nitrous arc works like this.");
            if (e.bindKind) {
                snprintf(bb, sizeof bb, "%s", e.bind.c_str());
                ImGui::SetNextItemWidth(160);
                if (ImGui::InputTextWithHint("value", "key, e.g. rpm_pct", bb, sizeof bb)) { e.bind = bb; g_dirty = true; }
                ImGui::SetNextItemWidth(160);
                if (ImGui::DragFloat2("from / to value", &e.bindMin, 1.f, -100000.f, 100000.f, "%.0f")) g_dirty = true;
                if (e.bindKind == 3 || e.bindKind == 4) { ImGui::SetNextItemWidth(160); if (ImGui::DragFloat2("from / to angle", &e.rotMin, 0.5f, -720.f, 720.f, "%.0f deg")) g_dirty = true; }
                if (e.bindKind == 5 || e.bindKind == 6) { ImGui::SetNextItemWidth(160); if (ImGui::DragFloat2("from / to move", &e.rotMin, 0.5f, -2000.f, 2000.f, "%.1f")) g_dirty = true; }
                if (e.bindKind == 1 || e.bindKind == 2) {
                    ImGui::SetNextItemWidth(160);
                    if (ImGui::DragFloat2("empty / full at", &e.cutLo, 0.002f, 0.f, 1.f, "%.3f")) g_dirty = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where in the picture an empty and a full value land (0..1 along the fill).\nFor art whose bar doesn't reach its edges.");
                }
                float f = BindFrac(e);
                if (f >= 0) ImGui::TextDisabled("now: %.0f%%", f * 100); else ImGui::TextDisabled("now: no value");
            }
        }
    }
    ElemMenuProps(e);
    CopyToSimilar(e);
    ImGui::PopID();
}
// The game's race HUD (Global\FEng_Screens\Stripped\HUD_SingleRace.fng in GLOBAL\InGameRace.bun), as elements:
// FEng positions are from the screen centre in 640x480 units (y down); images are placed by their centre, text by
// its justification (Sj: 1 centre, 2 right, 4 middle, 8 bottom). Converted here to "anchored top right".
// More leaderboard rows shown than the layout has: copy the last row's elements down, one row spacing at a time
// (the spacing between its last two rows, or 20). Fewer: the extra rows are just hidden, not deleted.
static void EnsureRows(std::vector<Elem>& els, int rows) {
    int have = 0; for (auto& e : els) have = std::max(have, e.row);
    if (have <= 0 || have >= rows) return;
    float dy = 20.f;
    {   // spacing: same-type elements of the last two rows, in list order
        std::vector<float> ya, yb;
        for (auto& e : els) { if (e.row == have) ya.push_back(e.y); if (e.row == have - 1) yb.push_back(e.y); }
        if (!ya.empty() && ya.size() == yb.size() && ya[0] - yb[0] > 1.f) dy = ya[0] - yb[0];
    }
    std::vector<Elem> last; for (auto& e : els) if (e.row == have) last.push_back(e);
    for (int r = have + 1; r <= rows; ++r) {
        for (auto e : last) {
            e.row = r; e.y += dy * (r - have);
            std::string old = "Row " + std::to_string(have), nw = "Row " + std::to_string(r);
            if (e.name.compare(0, old.size(), old) == 0) e.name = nw + e.name.substr(old.size());
            els.push_back(e);
        }
    }
}
static void ImportRaceHud(std::vector<Elem>& out, int rows) {
    const char* kWhite = "WHITE16X16_PURE_WHITE";
    const char* kBig = "font_conduitmditctt38bi";   // FNG font 4
    const char* kSmall = "conduitmditc_tt14i";       // FNG font 3
    std::string grp;
    auto image = [&](const char* name, const char* tex, float cx, float cy, float w, float h, int r, int g, int b, int a, int row) {
        Elem e; e.type = E_IMAGE; e.name = name; e.tex = tex; e.anchor = 2; e.row = row; e.group = grp;
        e.x = (320.f + cx + w / 2) - 640.f; e.y = 240.f + cy - h / 2; e.w = w; e.h = h;
        e.col[0] = r / 255.f; e.col[1] = g / 255.f; e.col[2] = b / 255.f; e.col[3] = a / 255.f;
        out.push_back(e); return &out.back();
    };
    auto text = [&](const char* name, const char* font, float x, float y, float scale, int sj, const char* s, int row) {
        Elem e; e.type = E_TEXT; e.name = name; e.font = font; e.text = s; e.anchor = 2; e.row = row; e.scale = scale; e.w = 0; e.group = grp;
        e.align = (sj & 2) ? 2 : (sj & 1) ? 1 : 0;
        int fi = FindFont(font); float lh = fi >= 0 ? g_fonts[fi].line * scale : 20.f * scale;
        e.x = 320.f + x - 640.f; e.y = 240.f + y - ((sj & 8) ? lh : (sj & 4) ? lh / 2 : 0.f);
        e.col[0] = e.col[1] = e.col[2] = 192 / 255.f; e.col[3] = 1.f;
        out.push_back(e); return &out.back();
    };
    // position
    grp = "Race HUD/Position";
    image("Position backing", "3RDPERSON_BOXCORNER", 257.f, -182.f, 256.f, 64.f, 255, 255, 255, 255, 0);
    text("Position", kBig, 173.f, -165.f, 0.9f, 10, "{pos}", 0);
    text("Position suffix", kBig, 175.f, -166.f, 0.7f, 8, "{pos_suffix}", 0);
    // laps
    grp = "Race HUD/Laps";
    image("Lap backing", "3RDPERSON_BOXLAPS", 253.f, -149.f, 256.f, 64.f, 0, 0, 0, 255, 0);
    text("Lap label", kBig, 139.5f, -162.2f, 0.6f, 0, "Lap", 0);
    text("Lap", kBig, 222.5f, -161.2f, 0.6f, 0, "{lap}", 0);
    text("Laps", kBig, 245.5f, -161.2f, 0.6f, 0, "/{laps}", 0);
    // leaderboard: the game's 4 rows, 20 apart, and as many more as asked for
    grp = "Race HUD/Leaderboard";
    for (int i = 1; i <= rows; ++i) {
        float dy = 20.f * (i - 1);
        char n1[32], n2[32], n3[32], n4[32];
        snprintf(n1, sizeof n1, "Row %d number box", i); snprintf(n2, sizeof n2, "Row %d bar", i);
        snprintf(n3, sizeof n3, "Row %d position", i); snprintf(n4, sizeof n4, "Row %d name", i);
        image(n1, kWhite, 143.f, -125.f + dy, 20.5f, 18.f, 0, 0, 0, 190, i);
        Elem* bar = image(n2, kWhite, 220.5f, -125.f + dy, 134.f, 18.f, 0, 0, 0, 120, i);
        // your row: the bar goes a darker, more opaque black (the game's SetPlayerColor on LBBacking)
        bar->hiMine = true; bar->hiCol[0] = 0.f; bar->hiCol[1] = 0.f; bar->hiCol[2] = 0.f; bar->hiCol[3] = 160 / 255.f;
        text(n3, kBig, 134.5f, -135.f + dy, 0.5f, 0, "{pos} :", i);
        Elem* nm = text(n4, kSmall, 285.f, -135.f + dy, 0.9f, 2, "{gap_name}", i);
        // names: the game's grey (LBData INIT 192,192,192), green on your row (SetPlayerColor 202,244,138)
        nm->col[0] = nm->col[1] = nm->col[2] = 192 / 255.f;
        nm->hiMine = true; nm->hiCol[0] = 202 / 255.f; nm->hiCol[1] = 244 / 255.f; nm->hiCol[2] = 138 / 255.f; nm->hiCol[3] = 1.f;
    }
    // race time (the game's timer box)
    float ty = -46.f + 20.f * std::max(0, rows - 4);
    grp = "Race HUD/Time";
    image("Time backing", "3RDPERSON_BOXTIME", 250.f, ty, 256.f, 32.f, 0, 0, 0, 190, 0);
    text("Time label", kSmall, 138.f, ty - 9.f, 0.9f, 0, "Time:", 0);
    text("Time", kSmall, 285.f, ty - 9.f, 0.9f, 2, "{race_time}", 0);
}
// ---------------------------------------------------------------- import (UniPad capture)
static uint32_t BHash(const std::string& s) { uint32_t h = 0xFFFFFFFF; for (unsigned char c : s) h = h * 33 + c; return h; }
static std::vector<std::pair<std::string, std::vector<ImportItem>>> g_pending;
void Import(const std::string& group, const std::vector<ImportItem>& items) {
    g_pending.emplace_back(group, items);
    if (g_state == 0) { g_state = 1; std::thread(Scan).detach(); }
}
static void ResolvePending() {
    if (g_pending.empty() || g_state != 2) return;
    bool otherEdits = g_dirty;   // unsaved editor changes: the capture isn't saved on its own (that would save them too)
    std::map<uint32_t, int> tex, font;
    std::map<std::string, int> texByName;
    for (size_t i = 0; i < g_tex.size(); ++i) {
        tex.emplace(BHash(g_tex[i].name), (int)i);
        std::string up = g_tex[i].name; for (auto& c : up) c = (char)toupper((unsigned char)c);
        texByName.emplace(up, (int)i);
    }
    std::map<std::string, int> live;   // not in the files: drawn from the game's copy
    for (size_t i = 0; i < g_fonts.size(); ++i) {
        std::string up = g_fonts[i].name; for (auto& c : up) c = (char)toupper((unsigned char)c);
        font.emplace(BHash(g_fonts[i].name), (int)i); font.emplace(BHash(up), (int)i);
    }
    int added = 0, missing = 0;
    for (auto& p : g_pending) {
        for (auto& it : p.second) {
            Elem e; e.anchor = 4; e.group = IntoMenu(p.first); e.name = it.name;
            for (int k = 0; k < 4; ++k) e.col[k] = it.col[k];
            if (!it.text) {
                auto f = tex.find(it.res);
                int ti = f != tex.end() ? f->second : -1;
                if (ti < 0 && !it.texName.empty()) {
                    std::string up = it.texName; for (auto& c : up) c = (char)toupper((unsigned char)c);
                    auto g = texByName.find(up); if (g != texByName.end()) ti = g->second;
                }
                e.type = E_IMAGE;
                int iconId = iconForGameTexture && !it.texName.empty() ? iconForGameTexture(it.texName) : -1;
                if (iconId >= 0 && iconId < (int)iconKeys.size()) { e.tex = "icon:auto:" + iconKeys[iconId]; e.pack.clear(); e.texIdx = -2; ti = -1; }
                else if (ti >= 0) { e.tex = g_tex[ti].name; e.pack = g_tex[ti].pack; e.texIdx = ti; }
                else if (iconId >= 0) {}
                else if (!it.texName.empty()) { e.tex = it.texName; e.pack.clear(); e.texIdx = -1; ++live[it.texName]; }
                else { ++missing; continue; }
                e.x = it.x; e.y = it.y; e.w = it.w; e.h = it.h; e.u0 = it.u0; e.v0 = it.v0; e.u1 = it.u1; e.v1 = it.v1; e.rot = it.rot; e.layer = it.layer;
            } else {
                auto f = font.find(it.res);
                int fi = f != font.end() ? f->second : FindFont("conduitmditc_tt14i");
                if (fi < 0) { ++missing; continue; }
                e.layer = it.layer;
                e.type = E_TEXT; e.font = g_fonts[fi].name; e.fontIdx = fi; e.text = it.str; e.scale = it.scale; e.w = 0;
                e.align = (it.just & 2) ? 2 : (it.just & 1) ? 1 : 0;
                float lh = Lines(it.str) * g_fonts[fi].line * it.scale;
                e.x = it.x; e.y = it.y + ((it.just & 8) ? -lh / 2 : (it.just & 4) ? 0.f : lh / 2);
            }
            Elems().push_back(e); ++added;
        }
    }
    Log("ui editor: imported %d element(s), %d skipped (texture / font not found)", added, missing);
    if (!live.empty()) {
        std::string l; for (auto& kv : live) { if (!l.empty()) l += ", "; l += kv.first; }
        Log("ui editor: %d texture(s) not in the game files, drawn from the game's copy when loaded: %s", (int)live.size(), l.c_str());
    }
    g_pending.clear();
    if (!otherEdits) Save();   // captures are kept right away
    else { g_dirty = true; Log("ui editor: capture added; not saved on its own because there are other unsaved changes (Save)"); }
}

// ---- the Minimap page, in tabs: Blips (the minimap's textures and markers), Maps (free roam's and the five career
// stage maps) and Tracks (each race's map). A row: the game's picture, what replaces it, what's drawn now.
static void MapArtRow(const std::string& n, const std::string& note, float th) {
    const auto& arts = vecart::List();
    auto thumb = [&](const std::string& tex) {   // a picture of a texture on a checkerboard
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float cs = th / 4;
        for (int yy = 0; yy < 4; ++yy) for (int xx = 0; xx < 4; ++xx)
            dl->AddRectFilled(ImVec2(p.x + xx * cs, p.y + yy * cs), ImVec2(p.x + (xx + 1) * cs, p.y + (yy + 1) * cs), ((xx + yy) & 1) ? IM_COL32(90, 90, 90, 255) : IM_COL32(60, 60, 60, 255));
        float su = 1, sv = 1;
        gfx::Tex tx = tex.empty() ? nullptr : NamedTex(tex, (int)th, su, sv);
        if (tx) {
            float w = 1, h = 1;
            if (vecart::IsRef(tex)) { if (const vecart::Art* a = vecart::Find(vecart::RefName(tex))) { w = a->w > 0 ? a->w : 1; h = a->h > 0 ? a->h : 1; } }
            else { int i = FindTex(tex, ""); if (i >= 0) { w = (float)g_tex[i].w; h = (float)g_tex[i].h; } }
            float sc = (th - 4) / std::max(w, h); ImVec2 isz(w * sc, h * sc), off((th - isz.x) / 2, (th - isz.y) / 2);
            dl->AddImage(gfx::Id(tx), ImVec2(p.x + off.x, p.y + off.y), ImVec2(p.x + off.x + isz.x, p.y + off.y + isz.y), ImVec2(0, 0), ImVec2(su, sv));
        } else dl->AddText(ImVec2(p.x + 4, p.y + th / 2 - 7), IM_COL32(150, 150, 150, 255), tex.empty() ? "" : "?");
        ImGui::Dummy(ImVec2(th, th));
        if (tx && ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::TextUnformatted(tex.c_str()); ImGui::Image(gfx::Id(tx), ImVec2(256, 256), ImVec2(0, 0), ImVec2(su, sv)); ImGui::EndTooltip(); }
    };
    ImGui::PushID(n.c_str());
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(n.c_str());
    if (!note.empty()) ImGui::TextDisabled("%s", note.c_str());
    ImGui::TableNextColumn(); thumb(n);   // the game's own
    ImGui::TableNextColumn();
    std::string& v = g_mapArt[n];
    std::string autoArt = vecart::Find("minimap/" + n) ? "vector art: minimap/" + n : "the game's own";
    auto label = [&](const std::string& s) -> std::string {
        if (s.empty()) return "Automatic (" + autoArt + ")";
        if (s == "-") return "The game's own";
        if (vecart::IsRef(s)) return "Vector art: " + vecart::RefName(s) + (vecart::Find(vecart::RefName(s)) ? "" : " (file missing)");
        int ifam, iid; if (IconRef(s, ifam, iid)) return "Button icon: " + s.substr(5);
        return "Texture: " + s;
    };
    ImGui::SetNextItemWidth(std::max(200.f, ImGui::GetContentRegionAvail().x - 150));
    if (ImGui::BeginCombo("##rep", label(v).c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(label("").c_str(), v.empty())) { v.clear(); g_dirty = true; }
        if (ImGui::Selectable("The game's own", v == "-")) { v = "-"; g_dirty = true; }
        if (!v.empty() && v != "-" && !vecart::IsRef(v)) ImGui::Selectable(label(v).c_str(), true);
        ImGui::SeparatorText("Vector art (REUI\\vector)");
        if (arts.empty()) ImGui::TextDisabled("(no .svg files)");
        for (auto& a : arts) {
            std::string ref = vecart::Ref(a.name);
            if (ImGui::Selectable(a.name.c_str(), v == ref)) { v = ref; g_dirty = true; }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!(g_texSel >= 0 && g_texSel < (int)g_tex.size()));
    if (ImGui::SmallButton("use the selected texture")) { v = g_tex[g_texSel].name; g_dirty = true; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The texture picked in the Textures list below (any of the game's textures).");
    ImGui::TableNextColumn(); { std::string now = MapArtNow(n); thumb(now.empty() ? n : now); }
    ImGui::PopID();
}
static bool MapArtTable(const char* id, float th) {
    if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                           ImVec2(0, std::min(520.f, std::max(200.f, ImGui::GetContentRegionAvail().y))))) return false;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Game texture", ImGuiTableColumnFlags_WidthFixed, 260);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, th);
    ImGui::TableSetupColumn("Replace with", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, th);
    ImGui::TableHeadersRow();
    return true;
}
static void MinimapPage() {
    static bool wasOpen = false;
    bool open = ImGui::CollapsingHeader("Minimap");
    if (open && !wasOpen) vecart::Rescan();   // (art added while the game runs shows up)
    wasOpen = open;
    if (!open) return;
    ImGui::TextDisabled("What REUI's minimap draws for each of the game's minimap textures and maps. Automatic: REUI\\vector\\minimap\\<name>.svg\n"
                        "when that file is there, else the game's own. Image elements using one of these textures (or its minimap vector\n"
                        "art - the frame, the north marker) follow it too. Save keeps it with the layout.");
    if (ImGui::SmallButton("reload art")) vecart::Rescan();
    ImGui::SameLine(); if (ImGui::SmallButton("open the minimap art folder")) {
        std::string d = vecart::Dir() + "minimap"; CreateDirectoryA(d.c_str(), nullptr);
        ShellExecuteA(nullptr, "open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine(); ImGui::TextDisabled(g_texSel >= 0 && g_texSel < (int)g_tex.size() ? "| selected in Textures: %s" : "| (pick a texture in Textures below to use it here)",
                                           g_texSel >= 0 && g_texSel < (int)g_tex.size() ? g_tex[g_texSel].name.c_str() : "");
    MapView mv; std::string mapNow = mapView && mapView(mv) ? mv.mapName : "";
    auto isMap = [](const std::string& n) { return n.compare(0, 8, "TRACKMAP") == 0; };
    if (ImGui::BeginTabBar("##maptabs")) {
        if (ImGui::BeginTabItem("Blips")) {   // the game's minimap textures, then other names the minimap has shown or the layout names
            std::vector<std::string> rows;
            auto add = [&](const std::string& n) { if (!n.empty() && !isMap(n) && std::find(rows.begin(), rows.end(), n) == rows.end()) rows.push_back(n); };
            for (const char* n : kMapTex) add(n);
            for (auto& n : g_mapSeen) add(n);
            for (auto& kv : g_mapArt) add(kv.first);
            if (MapArtTable("##blips", 40)) {
                for (auto& n : rows)
                    MapArtRow(n, std::find(std::begin(kMapTex), std::end(kMapTex), n) == std::end(kMapTex) ? "(another mod's marker)" : "", 40);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Maps")) {
            ImGui::TextDisabled("Free roam's minimap is TRACKMAP4000 at every stage. The five stage maps are the career World Map screen's\n"
                                "(the regions not open yet are dimmed in each) - that screen is the game's own: REUI doesn't draw it yet.");
            if (MapArtTable("##maps", 64)) {
                MapArtRow("TRACKMAP4000", std::string("Free roam (the minimap)") + (mapNow == "TRACKMAP4000" ? " - shown now" : ""), 64);
                for (int st = 0; st < 5; ++st) MapArtRow("TRACKMAP" + std::to_string(4500 + st), "Stage " + std::to_string(st + 1) + " (career World Map)", 64);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tracks")) {
            static char filt[64] = "";
            ImGui::SetNextItemWidth(220); ImGui::InputTextWithHint("##trkf", "search names / numbers", filt, sizeof filt);
            ImGui::SameLine(); ImGui::TextDisabled("Each race's own map (the city with its route drawn on), used by the minimap in that race.");
            auto tracks = trackNames ? trackNames() : std::vector<std::pair<int, std::string>>();
            std::sort(tracks.begin(), tracks.end());
            tracks.erase(std::unique(tracks.begin(), tracks.end()), tracks.end());
            std::string q = filt; for (auto& c : q) c = (char)toupper((unsigned char)c);
            std::vector<std::pair<std::string, std::string>> rows;   // texture, note
            for (auto& t : tracks) {
                if (t.first == 4000) continue;   // (free roam: Maps)
                std::string n = "TRACKMAP" + std::to_string(t.first);
                if (FindTex(n, "") < 0) continue;   // no map file
                std::string note = t.second + (mapNow == n ? " - shown now" : "");
                std::string hay = n + " " + note; for (auto& c : hay) c = (char)toupper((unsigned char)c);
                if (!q.empty() && hay.find(q) == std::string::npos) continue;
                rows.push_back({n, note});
            }
            for (auto& kv : g_mapArt) if (isMap(kv.first) && std::find_if(rows.begin(), rows.end(), [&](auto& r) { return r.first == kv.first; }) == rows.end()) {
                int id = atoi(kv.first.c_str() + 8);
                if (id != 4000 && (id < 4500 || id > 4504) && q.empty()) rows.push_back({kv.first, "(no track by that number)"});
            }
            if (tracks.empty()) ImGui::TextDisabled("(the game's track list isn't loaded yet)");
            if (MapArtTable("##tracks", 48)) {
                ImGuiListClipper clip; clip.Begin((int)rows.size(), 52);   // (only the rows on screen load their maps)
                while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) MapArtRow(rows[i].first, rows[i].second, 48);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    for (auto it = g_mapArt.begin(); it != g_mapArt.end();) { if (it->second.empty()) it = g_mapArt.erase(it); else ++it; }   // (automatic: not kept)
}

bool Unsaved() { return g_dirty; }
static void PanelBody();
void Panel() {
    g_panelOpen = true;
    if (g_state == 0) { g_state = 1; std::thread(Scan).detach(); }
    if (g_state == 1) { ImGui::TextUnformatted("Reading the game's UI textures and fonts..."); return; }
    // animation mode: the editor is greyed out until it's left
    {
        const bool am = anim::g.on;
        if (am) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1.f));
        if (ImGui::Button(am ? "Exit animation mode" : "Animation mode")) SetAnimMode(!am);
        if (am) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A workspace for animating elements: the elements on the right, their animation page, a keys + curves\ngraph and a timeline. Adding and changing elements here is greyed out until you leave it.");
        if (am) { ImGui::SameLine(); ImGui::TextDisabled("(editing is off while animating)"); }
        if (am) ImGui::BeginDisabled();
        PanelBody();
        if (am) ImGui::EndDisabled();
    }
}
static void PanelBody() {
    ResolvePending();
    ImGui::TextDisabled("Game files: %s", g_scanMsg.c_str());
    if (menuMode) {
        if (g_editMode != 0) { g_editMode = 0; g_sel = -1; }
        bool t = testMode;
        if (t) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1.f));
        if (ImGui::Button(t ? "Stop preview" : "Preview")) testMode = !testMode;
        if (t) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the layout over the game with this panel closed too.");
        ImGui::SameLine(); ImGui::Checkbox("Edit on screen", &g_edit);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to move, green corner to resize, Shift keeps the shape.");
        ImGui::SameLine(); if (ImGui::Checkbox("Highlight selection", &g_hiSel)) g_dirty = true;
    } else {
    // ---- overlay
    ImGui::SeparatorText("Overlay");
    if (ImGui::Checkbox("Show overlay", &g_show)) g_dirty = true; ImGui::SameLine();
    {
        bool t = testMode;
        if (t) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1.f));
        if (ImGui::Button(t ? "Stop test" : "Test")) testMode = !testMode;
        if (t) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the layout picked below right now - in the menus or free roam too, with the menu closed -\nand, when you're not in a race, fills the race values with a sample race (you 3rd of 6, lap 2 of 3).");
        ImGui::SameLine();
    }
    ImGui::Checkbox("Edit on screen (drag to move, green corner to resize, Shift keeps shape)", &g_edit);
    ImGui::SameLine(); if (ImGui::Checkbox("Highlight selection", &g_hiSel)) g_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Green outline around the selected texture / text (and orange box around a selected group) on screen.");
    ImGui::TextDisabled("Each game mode has its own layout. Now: %s%s", g_nowMode >= 0 && g_nowMode < NumModes() ? ModeName(g_nowMode) : "not driving (menus)",
                        g_edit ? " - while editing on screen, the layout picked below is shown" : "");
    if (ImGui::BeginTabBar("##modes")) {
        if (g_editMode >= NumModes()) { g_editMode = 0; g_sel = -1; }
        for (int m = 0; m < NumModes(); ++m) {
            char lb[96]; snprintf(lb, sizeof lb, "%s (%d)###mode%d", ModeName(m), (int)g_layout[m].size(), m);
            if (ImGui::BeginTabItem(lb)) { if (g_editMode != m) { g_editMode = m; g_sel = -1; g_dragging = -1; } ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
    {
        static int copyFrom = 0;
        std::vector<const char*> names; for (int m = 0; m < NumModes(); ++m) names.push_back(ModeName(m));
        copyFrom = std::min(copyFrom, NumModes() - 1);
        ImGui::SetNextItemWidth(130); ImGui::Combo("##copyfrom", &copyFrom, names.data(), (int)names.size()); ImGui::SameLine();
        if (ImGui::Button("Copy its elements into this layout") && copyFrom != g_editMode) {
            for (auto e : g_layout[copyFrom]) { e.group = IntoMenu(e.group); Elems().push_back(e); }
            g_dirty = true;
        }
        if (g_nowMode >= 0 && g_nowMode < NumModes() && g_nowMode != g_editMode) { ImGui::SameLine(); if (ImGui::SmallButton("edit the current mode's layout")) { g_editMode = g_nowMode; g_sel = -1; } }
    }
    // (Menus: "Import UniPad's menus file" and the old "menu page" hint are gone - menus live in REUI now)
    if (nfsu2Tools && !IsMenus(g_editMode)) {
        if (ImGui::Button("Import the multiplayer mod's overlay")) {
            std::string p = g_dir + "\\scripts\\NFSU2Multiplayer_overlay.txt";
            if (LoadFile(p, -1, true)) Log("ui editor: imported %s", p.c_str());
            else Log("ui editor: couldn't read %s", p.c_str());
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds every game mode's layout from scripts\\NFSU2Multiplayer_overlay.txt to the same modes here.");
    }
    if (nfsu2Tools && g_editMode != FREEROAM && !IsMenus(g_editMode)) {
        static int importRows = 4;
        ImGui::SetNextItemWidth(90); ImGui::InputInt("rows##imp", &importRows); importRows = std::max(1, std::min(12, importRows)); ImGui::SameLine();
        if (ImGui::Button("Import the game's race HUD (position, laps, leaderboard, time)")) {
            ImportRaceHud(Elems(), importRows); boardRows = importRows; g_dirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds the game's own race HUD pieces from HUD_SingleRace.fng - same textures, fonts, colours\nand places - with live values, and as many leaderboard rows as you ask for (the game has 4).");
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("leaderboard rows shown", &boardRows)) { boardRows = std::max(1, std::min(22, boardRows)); EnsureRows(Elems(), boardRows); g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rows past this are hidden; raising it past the rows the layout has copies its last row down.");
        ImGui::SameLine(); if (ImGui::Checkbox("pick rows like the game", &boardFollow)) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Row 1 is always the leader; the other rows show you, the racers just ahead of you\nand the one(s) just behind you - like the game's leaderboard. Off: rows 1, 2, 3 ... in order.");
        ImGui::SameLine(); ImGui::TextDisabled("(live values)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTokenHelp);
    }
    }  // !menuMode
    if (ImGui::Button("Add image")) {
        Elem e; e.type = E_IMAGE; e.name = "image " + std::to_string(Elems().size() + 1);
        if (g_texSel >= 0) { e.tex = g_tex[g_texSel].name; e.pack = g_tex[g_texSel].pack; e.texIdx = g_texSel; e.w = g_tex[g_texSel].w; e.h = g_tex[g_texSel].h; e.name = e.tex; }
        e.group = IntoMenu(e.group);   // (into the custom menu being edited)
        Elems().push_back(e); g_sel = (int)Elems().size() - 1; g_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add text")) {
        Elem e; e.type = E_TEXT; e.name = "text " + std::to_string(Elems().size() + 1); e.w = 0; e.col[0] = e.col[1] = e.col[2] = 0.753f;
        int fi = g_fontSel >= 0 ? g_fontSel : FindFont("conduitmditc_tt14i"); if (fi < 0 && !g_fonts.empty()) fi = 0;
        if (fi >= 0) { e.font = g_fonts[fi].name; e.fontIdx = fi; }
        e.group = IntoMenu(e.group);
        Elems().push_back(e); g_sel = (int)Elems().size() - 1; g_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add shape")) {
        Elem e; e.type = E_SHAPE; e.name = "shape " + std::to_string(Elems().size() + 1); e.anchor = 4; e.x = 0; e.y = 0; e.w = 240; e.h = 120;
        e.col[0] = e.col[1] = e.col[2] = 0.8824f; e.col[3] = 1; e.colTok = "hud.line"; e.colTokA = 1;
        e.fillCol[3] = 1; e.fillTok = "hud.shade"; e.fillTokA = 1;
        if (g_sel >= 0 && g_sel < (int)Elems().size()) e.group = Elems()[g_sel].group;
        e.group = IntoMenu(e.group);
        Elems().push_back(e); g_sel = (int)Elems().size() - 1; g_dirty = true; ResolveTokens();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A rounded rectangle: a stroke and a fill (menu backings, panels, frames). Tick \"cut\" on one\nto make it cut into another shape (a notch, a window, a tab) - the stroke follows the cut.");
    ImGui::SameLine(); if (ImGui::Button(g_dirty ? "Save *" : "Save")) Save();
    ImGui::SameLine(); if (ImGui::Button("Reload from file")) { Load(); UndoReset(); }
    ImGui::SameLine(); ImGui::BeginDisabled(g_undo.empty());
    if (ImGui::Button(("Undo (" + std::to_string(g_undo.size()) + ")").c_str())) Undo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl + Z (Ctrl + S saves, Ctrl + C / V copy / paste). Up to %d steps (each finished change: a drag, a typed name...).", kUndoSteps);
    ImGui::SameLine(); ImGui::BeginDisabled(g_redo.empty());
    if (ImGui::Button("Redo")) Redo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl + Y");
    ImGui::SameLine(); ImGui::TextDisabled("(%s)", g_layoutPath.c_str());
    MenuBar();   // (Menus: the custom menus)
    // element list + properties
    // element list + properties: one height for both, set with the bar under them; the list's width by its right edge
    ImGui::BeginChild("##elems", ImVec2(220, g_listH), REUI_CHILD_BORDERS | ImGuiChildFlags_ResizeX);
    static char search[64] = "";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "search names, text, textures...", search, sizeof search);
    std::string q = search; for (auto& c : q) c = (char)tolower((unsigned char)c);
    auto has = [&](const std::string& s) { std::string l = s; for (auto& c : l) c = (char)tolower((unsigned char)c); return l.find(q) != std::string::npos; };
    // drag and drop in the list: done after the list is drawn (the list is walked by index)
    struct Drop { int elem = -1; std::string group; int before = -1; std::string into; bool toTop = false; } drop;
    // the list as shown (elements and groups, top to bottom), for the arrow keys; scroll to the picked one
    struct Entry { int elem; std::string group; };
    std::vector<Entry> shownList;
    int shiftTo = -1; std::string shiftToGroup, shiftFromGroup;   // Shift + click: to an element / a group, from a group
    static bool scrollToSel = false;
    auto scrollHere = [&](bool isSel) { if (isSel && scrollToSel) { ImGui::SetScrollHereY(0.5f); scrollToSel = false; } };
    static int lastSel = -2; static std::string lastGroup;
    bool revealSel = g_sel != lastSel || g_selGroup != lastGroup;   // picked something new (on screen too)
    if (revealSel) scrollToSel = true;
    lastSel = g_sel; lastGroup = g_selGroup;
    auto dragSource = [&](int i) {
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("UIED_ELEM", &i, sizeof i);
            ImGui::TextUnformatted(Elems()[i].name.c_str());
            ImGui::EndDragDropSource();
        }
    };
    auto item = [&](int i) {
        ImGui::PushID(i);
        std::string label = std::string(Elems()[i].type == E_TEXT ? "[T] " : Elems()[i].type == E_SHAPE ? (Elems()[i].cut ? "[C] " : Elems()[i].merge ? "[M] " : "[S] ") : "[I] ") + Elems()[i].name + (Elems()[i].visible ? "" : " (hidden)") +
                            (Elems()[i].layer ? "  L" + std::to_string(Elems()[i].layer) : "");
        if (!q.empty() && !RelGroup(Elems()[i].group).empty()) label += "   in " + RelGroup(Elems()[i].group);
        if (ImGui::Selectable(label.c_str(), (g_sel == i && g_selGroup.empty()) || InMulti(i))) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyShift && g_sel >= 0) shiftTo = i;   // a range: done once the list is drawn
            else if (io.KeyShift && !g_selGroup.empty()) { shiftTo = i; shiftFromGroup = g_selGroup; }
            else if (io.KeyCtrl) {
                if (PickCount() == 0 && g_sel >= 0 && g_sel != i && g_selGroup.empty()) g_multi.push_back(g_sel);
                if (PickCount() == 0 && !g_selGroup.empty()) g_multiG.push_back(g_selGroup);
                MultiToggle(i); g_sel = i;
            }
            else { ClearMulti(); g_sel = i; }
            g_selGroup.clear();
        }
        shownList.push_back({ i, "" }); scrollHere(g_sel == i && g_selGroup.empty());
        dragSource(i);
        if (ImGui::BeginDragDropTarget()) {   // dropped on an element: goes just before it, into its group
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("UIED_ELEM")) { drop.elem = *(const int*)pl->Data; drop.before = i; drop.into = Elems()[i].group; }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    };
    if (!q.empty()) {   // search: the matching elements and groups, flat
        for (auto& g : GroupNames())
            if (has(GroupLeaf(g)) && !MenuEdOff(g)) {
                ImGui::PushID(("sg_" + g).c_str());
                if (ImGui::Selectable(("[G] " + RelGroup(g) + "##" + g).c_str(), g_selGroup == g)) { g_selGroup = g; g_sel = -1; }
                shownList.push_back({ -1, g }); scrollHere(g_selGroup == g);
                ImGui::PopID();
            }
        int n = 0;
        for (int i = 0; i < (int)Elems().size(); ++i) {
            const Elem& e = Elems()[i];
            if (MenuEdOff(e.group)) continue;
            if (has(e.name) || (e.type == E_TEXT && has(e.text)) || (e.type == E_IMAGE && has(e.tex)) || (!e.role.empty() && has(e.role))) { item(i); ++n; }
        }
        if (!n) ImGui::TextDisabled("nothing matches");
    } else {
        // one level of the tree: elements directly in group `path` and, where their first member is, the groups inside it
        std::function<void(const std::string&)> level = [&](const std::string& path) {
            std::vector<std::string> shown;
            for (int i = 0; i < (int)Elems().size(); ++i) {
                const std::string& eg = Elems()[i].group;
                if (MenuEdOff(eg)) continue;   // (a custom menu picked in the menu bar: only its own)
                if (eg == path) { item(i); continue; }
                if (!path.empty() && !InGroup(eg, path)) continue;
                std::string rest = path.empty() ? eg : eg.substr(path.size() + 1);
                std::string child = (path.empty() ? "" : path + "/") + rest.substr(0, rest.find('/'));
                if (std::find(shown.begin(), shown.end(), child) != shown.end()) continue;
                shown.push_back(child);
                int n = 0; for (auto& e : Elems()) if (InGroup(e.group, child)) ++n;
                bool gsel = g_selGroup == child || InMultiG(child);
                // collapsed to start with; a newly picked element (or group) inside opens the groups around it
                ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | (gsel ? ImGuiTreeNodeFlags_Selected : 0);
                if (revealSel && ((g_sel >= 0 && g_sel < (int)Elems().size() && g_selGroup.empty() && InGroup(Elems()[g_sel].group, child)) ||
                                  (!g_selGroup.empty() && g_selGroup != child && InGroup(g_selGroup, child))))
                    ImGui::SetNextItemOpen(true);
                int lk = LinkCount(child);   // (linked: tinted, with how many share its pieces)
                if (lk) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.f, 1.f));
                bool open = lk ? ImGui::TreeNodeEx(("grp_" + child).c_str(), fl, "[G] %s (%d)  [linked x%d]", GroupLeaf(child).c_str(), n, lk)
                               : ImGui::TreeNodeEx(("grp_" + child).c_str(), fl, "[G] %s (%d)", GroupLeaf(child).c_str(), n);
                if (lk) ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyShift && (g_sel >= 0 || !g_selGroup.empty())) { shiftToGroup = child; shiftFromGroup = g_selGroup; }
                    else if (io.KeyCtrl) {   // Ctrl + click on a group: the group joins the picked ones (as a group)
                        if (PickCount() == 0 && g_sel >= 0 && g_selGroup.empty()) g_multi.push_back(g_sel);
                        if (PickCount() == 0 && !g_selGroup.empty() && g_selGroup != child) g_multiG.push_back(g_selGroup);
                        MultiToggleG(child);
                        g_selGroup.clear(); g_sel = -1;
                    } else { g_selGroup = child; g_sel = -1; ClearMulti(); }
                }
                shownList.push_back({ -1, child }); scrollHere(gsel);
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("UIED_GROUP", child.c_str(), child.size() + 1);
                    ImGui::Text("[G] %s", GroupLeaf(child).c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {   // dropped on a group: into it
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("UIED_ELEM")) { drop.elem = *(const int*)pl->Data; drop.into = child; }
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("UIED_GROUP")) { drop.group = (const char*)pl->Data; drop.into = child; }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem(("gctx_" + child).c_str())) {   // right click: visible, link, save as prefab
                    bool vis = true; for (auto& e : Elems()) if (InGroup(e.group, child)) vis &= e.visible;
                    if (ImGui::MenuItem("Visible", nullptr, vis)) { for (auto& e : Elems()) if (InGroup(e.group, child)) e.visible = !vis; g_dirty = true; }
                    ImGui::Separator();
                    LinkMenuItems(child);
                    if (ImGui::BeginMenu("Save as linked group...")) { if (LinkSaveUI(child)) ImGui::CloseCurrentPopup(); ImGui::EndMenu(); }
                    ImGui::Separator();
                    static char pn[64]; static std::string pnFor;
                    if (pnFor != child) { pnFor = child; snprintf(pn, sizeof pn, "%s", GroupLeaf(child).c_str()); }
                    ImGui::TextDisabled("Save as prefab");
                    ImGui::SetNextItemWidth(180);
                    bool enter = ImGui::InputText("##pfname", pn, sizeof pn, ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    if (ImGui::Button("Save") || enter) { SavePrefab(child, pn); ImGui::CloseCurrentPopup(); }
                    bool exists = false; for (auto& n : PrefabList()) exists |= n == pn;
                    if (exists) ImGui::TextDisabled("(replaces the prefab with that name)");
                    ImGui::EndPopup();
                }
                if (open) { level(child); ImGui::TreePop(); }
            }
        };
        level(MenuEdOff("") ? g_menuEd : std::string());   // (a custom menu: its contents are the top of the list - it's the page, not a group)
        // the strip under the list: out of every group
        ImGui::Dummy(ImVec2(-FLT_MIN, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::Selectable(MenuEdOff("") ? "(drop here: out of its groups)" : "(drop here: no group)", false, ImGuiSelectableFlags_Disabled);
        ImGui::PopStyleColor();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("UIED_ELEM")) { drop.elem = *(const int*)pl->Data; drop.toTop = true; }
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("UIED_GROUP")) { drop.group = (const char*)pl->Data; drop.toTop = true; }
            ImGui::EndDragDropTarget();
        }
    }
    // Up / Down: the previous / next entry of the list (while the list has the focus and nothing is being typed);
    // with Alt: move the selected element / group up or down past its neighbour in the same group
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput && !shownList.empty()) {
        int d = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0;
        if (d && ImGui::GetIO().KeyAlt) { ClearMulti(); MoveInList(d); scrollToSel = true; d = 0; }
        if (d) {
            int cur = -1;
            for (int k = 0; k < (int)shownList.size(); ++k)
                if ((shownList[k].elem >= 0 && shownList[k].elem == g_sel && g_selGroup.empty()) || (shownList[k].elem < 0 && shownList[k].group == g_selGroup)) cur = k;
            int nx = cur < 0 ? (d > 0 ? 0 : (int)shownList.size() - 1) : std::max(0, std::min((int)shownList.size() - 1, cur + d));
            if (shownList[nx].elem >= 0) { g_sel = shownList[nx].elem; g_selGroup.clear(); }
            else { g_selGroup = shownList[nx].group; g_sel = -1; }
            scrollToSel = true;
        }
    }
    ImGui::EndChild();
    if (shiftTo >= 0 || !shiftToGroup.empty()) {   // Shift + click: every entry shown between the last picked and this one
        auto at = [&](int elem, const std::string& grp) {
            for (int k = 0; k < (int)shownList.size(); ++k)
                if (elem >= 0 ? shownList[k].elem == elem : (shownList[k].elem < 0 && shownList[k].group == grp)) return k;
            return -1;
        };
        int a = shiftFromGroup.empty() ? at(g_sel, "") : at(-1, shiftFromGroup);
        int b = shiftTo >= 0 ? at(shiftTo, "") : at(-1, shiftToGroup);
        if (a >= 0 && b >= 0) {
            if (a > b) std::swap(a, b);
            ClearMulti();
            for (int k = a; k <= b; ++k) {
                if (shownList[k].elem >= 0) {   // an element inside a group already picked comes with it
                    bool inPicked = false; for (auto& g : g_multiG) inPicked |= InGroup(Elems()[shownList[k].elem].group, g);
                    if (!inPicked && !InMulti(shownList[k].elem)) g_multi.push_back(shownList[k].elem);
                } else {
                    bool inPicked = false; for (auto& g : g_multiG) inPicked |= InGroup(shownList[k].group, g);
                    if (!inPicked) g_multiG.push_back(shownList[k].group);
                }
            }
        }
        g_selGroup.clear(); g_sel = shiftTo >= 0 ? shiftTo : -1;
    }
    // apply a drop
    if (drop.elem >= 0 || !drop.group.empty()) ClearMulti();
    if (drop.elem >= 0 && drop.elem < (int)Elems().size()) {
        Elem e = Elems()[drop.elem];
        e.group = drop.toTop ? IntoMenu("") : drop.into;
        int at;
        if (drop.before >= 0) at = drop.before > drop.elem ? drop.before - 1 : drop.before;
        else {   // after the group's last element (or at the end)
            at = -1;
            for (int i = 0; i < (int)Elems().size(); ++i) if (i != drop.elem && !e.group.empty() && InGroup(Elems()[i].group, e.group)) at = i;
            at = at < 0 ? (int)Elems().size() - 1 : (at > drop.elem ? at : at + 1);
        }
        Elems().erase(Elems().begin() + drop.elem);
        at = std::max(0, std::min((int)Elems().size(), at));
        Elems().insert(Elems().begin() + at, e);
        g_sel = at; g_selGroup.clear(); g_dirty = true;
    } else if (!drop.group.empty()) {
        std::string g = drop.group;
        if (drop.toTop) { std::string to = IntoMenu(GroupLeaf(g)); if (to != g && !InGroup(to, g)) RebaseGroup(g, to); }
        else if (!InGroup(drop.into, g) && GroupParent(g) != drop.into) RebaseGroup(g, drop.into + "/" + GroupLeaf(g));   // not into itself
    }
    ImGui::SameLine();
    ImGui::BeginChild("##props", ImVec2(0, g_listH), REUI_CHILD_BORDERS);
    if (PickCount() == 1) {   // down to one: an ordinary selection
        if (!g_multiG.empty()) { g_selGroup = g_multiG[0]; g_sel = -1; } else { g_sel = g_multi[0]; g_selGroup.clear(); }
        ClearMulti();
    }
    if (PickCount() > 1) MultiPanel();
    else if (!g_selGroup.empty()) GroupPanel();
    else if (g_sel >= 0 && g_sel < (int)Elems().size()) {
        if (ImGui::SmallButton("up") && g_sel > 0) { std::swap(Elems()[g_sel], Elems()[g_sel - 1]); --g_sel; g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("down") && g_sel + 1 < (int)Elems().size()) { std::swap(Elems()[g_sel], Elems()[g_sel + 1]); ++g_sel; g_dirty = true; }
        ImGui::SameLine(); ImGui::TextDisabled("(lower in the list = drawn on top)"); ImGui::SameLine();
        if (ImGui::SmallButton("duplicate")) { Elem c = Elems()[g_sel]; c.name += " copy"; c.x += 10; c.y += 10; Elems().insert(Elems().begin() + g_sel + 1, c); ++g_sel; g_dirty = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("delete")) DeleteSelected();
        if (g_sel >= 0) Selected(Elems()[g_sel]);
    } else ImGui::TextDisabled("Add an image or text, or pick one on the left. Positions are in 1/480ths of the screen height.\nPut elements in a group (their \"group\" box) to move them together.");
    ImGui::EndChild();
    {   // the bar: drag it up / down to size both boxes
        ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##listbar", ImVec2(w, 8.f));
        bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) { g_listH = std::max(80.f, std::min(1400.f, g_listH + ImGui::GetIO().MouseDelta.y)); }
        ImU32 c = hot ? IM_COL32(255, 210, 60, 255) : IM_COL32(140, 140, 150, 160);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + w * 0.5f - 30.f, p.y + 2.f), ImVec2(p.x + w * 0.5f + 30.f, p.y + 6.f), c, 2.f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 4.f), ImVec2(p.x + w, p.y + 4.f), hot ? c : IM_COL32(90, 90, 100, 120), 1.f);
    }

    {   // keys: Delete removes what's picked; Ctrl + Z / Ctrl + Y (or Ctrl + Shift + Z) undo / redo; Ctrl + C / V copy /
        // paste what's picked; Ctrl + S saves (also while typing in a box)
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelected();
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopyPicked();
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteClip();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) Redo(); else Undo(); }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
        }
    }
    LinksSync();   // (linked groups take the edits made to one of them)
    UndoCommit(false);
    TokensPanel();
    if (!menuMode) MinimapPage();
    LinkedGroupsPage();
    // ---- prefabs: a list, a preview of the picked one, Add / Delete
    if (!menuMode && ImGui::CollapsingHeader("Prefabs")) {
        static std::string pick, cacheName; static std::vector<Elem> cache; static double cacheAt = -10;
        auto list = PrefabList();
        ImGui::TextDisabled("Right-click a group in the element list to save it as a prefab (scripts\\NFSU2_REUI_prefabs).");
        float h = 230;
        ImGui::BeginChild("##pflist", ImVec2(230, h), REUI_CHILD_BORDERS);
        if (list.empty()) ImGui::TextDisabled("(no prefabs yet)");
        for (auto& n : list) {
            bool builtIn = PrefabBuiltIn(n), file = PrefabOnDisk(n);
            std::string label = builtIn ? n + (file ? "  (built in, file)" : "  (built in)") : n;
            if (ImGui::Selectable((label + "##" + n).c_str(), pick == n, ImGuiSelectableFlags_AllowDoubleClick)) { pick = n; if (ImGui::IsMouseDoubleClicked(0)) AddPrefab(n); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click: add it%s", builtIn ? (file ? "\nBuilt into REUI; the file on disk is the one used." : "\nBuilt into REUI.") : "");
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginGroup();
        bool have = std::find(list.begin(), list.end(), pick) != list.end();
        if (have && (cacheName != pick || ImGui::GetTime() - cacheAt > 2.0)) {   // (re-read now and then: it may have been replaced)
            std::string root; LoadPrefab(pick, root, cache); cacheName = pick; cacheAt = ImGui::GetTime();
        }
        { std::vector<Elem> none; ElemsPreview(have ? cache : none, ImVec2(std::max(200.f, ImGui::GetContentRegionAvail().x), h - ImGui::GetFrameHeightWithSpacing()), have ? "(empty)" : "Pick a prefab to see it here"); }
        ImGui::BeginDisabled(!have);
        if (ImGui::Button("Add")) AddPrefab(pick);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A copy in this game mode (%s), as a new group.", ModeName(g_editMode));
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete prefab?");
        ImGui::EndDisabled();
        if (have) { ImGui::SameLine(); ImGui::TextDisabled("%s - %d elements", pick.c_str(), (int)cache.size()); }
        if (ImGui::BeginPopupModal("Delete prefab?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to delete the prefab \"%s\"?", pick.c_str());
            if (PrefabBuiltIn(pick)) ImGui::TextDisabled("It's built into REUI: hidden from now on, and gone from the next build.\nGroups already added from it stay.");
            else ImGui::TextDisabled("Its file goes for good; groups already added from it stay.");
            if (ImGui::Button("Delete it")) { DeletePrefab(pick); pick.clear(); cacheName.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        if (!g_prefabMsg.empty()) ImGui::TextDisabled("%s", g_prefabMsg.c_str());
    }
    // ---- controller button icons
    if (!iconKeys.empty() && iconTexture && ImGui::CollapsingHeader("Button icons")) {
        static int fam = 0;   // 0 = follows the controller, 1.. = a family
        std::vector<std::string> names{"Follows the controller (universal)"};
        for (auto& f : iconFamilies) names.push_back(f);
        std::vector<const char*> np; for (auto& n : names) np.push_back(n.c_str());
        ImGui::SetNextItemWidth(260); ImGui::Combo("##iconfam", &fam, np.data(), (int)np.size());
        ImGui::SameLine(); if (iconReload && ImGui::SmallButton("reload icon files")) iconReload();
        ImGui::TextDisabled("Click an icon to add it (32 x 32, the size the game's prompts use).%s",
                            g_sel >= 0 && g_sel < (int)Elems().size() && Elems()[g_sel].type == E_IMAGE ? "  Shift + click: put it on the selected image." : "");
        int f = fam - 1;
        float th = 48;
        int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (th + 12)));
        for (int id = 0; id < (int)iconKeys.size(); ++id) {
            if (id % cols) ImGui::SameLine();
            ImGui::PushID(id);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + th, p.y + th), IM_COL32(55, 55, 62, 255), 4.f);
            if (gfx::Tex tx = iconTexture(f, id, (int)th - 8)) dl->AddImage(gfx::Id(tx), ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + th - 4, p.y + th - 4));
            std::string tex = "icon:" + std::string(f < 0 ? "auto" : iconFamilies[f]) + ":" + iconKeys[id];
            if (ImGui::InvisibleButton("##ic", ImVec2(th, th))) {
                bool onSel = ImGui::GetIO().KeyShift && g_sel >= 0 && g_sel < (int)Elems().size() && Elems()[g_sel].type == E_IMAGE;
                if (onSel) { Elem& e = Elems()[g_sel]; e.tex = tex; e.pack.clear(); e.texIdx = -2; e.u0 = e.v0 = 0; e.u1 = e.v1 = 1; }
                else {
                    Elem e; e.type = E_IMAGE; e.anchor = 4; e.x = 0; e.y = 0; e.w = e.h = 32; e.tex = tex; e.name = IconName(f, id);
                    if (!g_selGroup.empty()) e.group = g_selGroup;
                    e.group = IntoMenu(e.group);
                    e.layer = 0; for (auto& o : Elems()) e.layer = std::max(e.layer, o.layer);   // on top
                    Elems().push_back(e); g_sel = (int)Elems().size() - 1; g_selGroup.clear();
                }
                g_dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", IconName(f, id).c_str());
            ImGui::PopID();
        }
    }
    // ---- vector art (separate from the controller button icons)
    if (!menuMode && ImGui::CollapsingHeader("Vector art")) {
        static float vth = 64.f;
        const auto& arts = vecart::List();
        if (ImGui::SmallButton("reload")) vecart::Rescan();
        ImGui::SameLine(); if (ImGui::SmallButton("open folder")) ShellExecuteA(nullptr, "open", vecart::Dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderFloat("##vth", &vth, 32.f, 160.f, "thumbs %.0f");
        bool selImage = g_sel >= 0 && g_sel < (int)Elems().size() && Elems()[g_sel].type == E_IMAGE;
        ImGui::TextDisabled("SVG files in REUI\\vector in the game folder (subfolders too), sharp at any size; colour tints them.\n"
                            "Click one to add it at its own size%s", selImage ? ";  Shift + click: put it on the selected image." : ".");
        if (arts.empty()) ImGui::TextDisabled("(no .svg files there yet)");
        float th = vth;
        int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (th + 12)));
        for (int i = 0; i < (int)arts.size(); ++i) {
            const vecart::Art& art = arts[i];
            if (i % cols) ImGui::SameLine();
            ImGui::BeginGroup(); ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float cs = th / 4;   // checkerboard so see-through art shows
            for (int yy = 0; yy < 4; ++yy) for (int xx = 0; xx < 4; ++xx)
                dl->AddRectFilled(ImVec2(p.x + xx * cs, p.y + yy * cs), ImVec2(p.x + (xx + 1) * cs, p.y + (yy + 1) * cs), ((xx + yy) & 1) ? IM_COL32(90, 90, 90, 255) : IM_COL32(60, 60, 60, 255));
            if (gfx::Tex tx = vecart::Texture(art.name, (int)th - 8)) {
                float aw = art.w > 0 ? art.w : 1, ah = art.h > 0 ? art.h : 1, sc = (th - 8) / std::max(aw, ah);
                ImVec2 isz(aw * sc, ah * sc), off((th - isz.x) / 2, (th - isz.y) / 2);
                dl->AddImage(gfx::Id(tx), ImVec2(p.x + off.x, p.y + off.y), ImVec2(p.x + off.x + isz.x, p.y + off.y + isz.y));
            } else dl->AddText(ImVec2(p.x + 4, p.y + 4), IM_COL32(255, 90, 90, 255), "?");
            if (ImGui::InvisibleButton("##va", ImVec2(th, th))) {
                bool onSel = ImGui::GetIO().KeyShift && selImage;
                if (onSel) { Elem& e = Elems()[g_sel]; e.tex = vecart::Ref(art.name); e.pack.clear(); e.texIdx = -2; e.u0 = e.v0 = 0; e.u1 = e.v1 = 1; }
                else {
                    Elem e; e.type = E_IMAGE; e.anchor = 4; e.x = 0; e.y = 0;
                    e.w = art.w > 0 ? art.w : 64; e.h = art.h > 0 ? art.h : 64;
                    float big = std::max(e.w, e.h); if (big > 256) { e.w *= 256 / big; e.h *= 256 / big; }   // a huge canvas: fitted to 256
                    e.tex = vecart::Ref(art.name); e.name = art.name;
                    if (!g_selGroup.empty()) e.group = g_selGroup;
                    e.group = IntoMenu(e.group);
                    e.layer = 0; for (auto& o : Elems()) e.layer = std::max(e.layer, o.layer);   // on top
                    Elems().push_back(e); g_sel = (int)Elems().size() - 1; g_selGroup.clear();
                }
                g_dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%.0f x %.0f", art.name.c_str(), art.w, art.h);
            std::string nm = art.name; size_t sl = nm.rfind('/'); if (sl != std::string::npos) nm = nm.substr(sl + 1);
            if (nm.size() > (size_t)(th / 6)) nm = nm.substr(0, (size_t)(th / 6)) + "..";
            ImGui::TextDisabled("%s", nm.c_str());
            ImGui::PopID(); ImGui::EndGroup();
        }
    }
    // ---- texture browser
    if (ImGui::CollapsingHeader("Textures")) {
        ImGui::SetNextItemWidth(180); ImGui::InputTextWithHint("##filter", "search names", g_filter, sizeof g_filter); ImGui::SameLine();
        std::vector<const char*> pk; pk.push_back("all packs"); for (auto& p : g_packs) pk.push_back(p.c_str());
        ImGui::SetNextItemWidth(320); ImGui::Combo("##pack", &g_packSel, pk.data(), (int)pk.size()); ImGui::SameLine();
        ImGui::Checkbox("in-game HUD only", &g_onlyHud); ImGui::SameLine();
        ImGui::SetNextItemWidth(100); ImGui::SliderFloat("##thumb", &g_thumb, 32.f, 160.f, "thumbs %.0f");
        std::vector<int> list; for (size_t i = 0; i < g_tex.size(); ++i) if (PassFilter(g_tex[i])) list.push_back((int)i);
        ImGui::TextDisabled("%d shown", (int)list.size());
        if (g_texSel >= 0 && g_texSel < (int)g_tex.size()) {
            Tex& t = g_tex[g_texSel];
            ImGui::SameLine(); ImGui::Text("| selected: %s  %dx%d %s  (%s, %s)", t.name.c_str(), t.w, t.h, FmtName(t.fmt), t.pack.c_str(), t.source.c_str());
        }
        float avail = ImGui::GetContentRegionAvail().x;
        int cols = std::max(1, (int)(avail / (g_thumb + 10)));
        ImGui::BeginChild("##grid", ImVec2(0, std::min(420.f, (g_thumb + 26) * 4)), REUI_CHILD_BORDERS | ImGuiChildFlags_ResizeY);
        int rows = ((int)list.size() + cols - 1) / cols;
        ImGuiListClipper clip; clip.Begin(rows, g_thumb + 26);
        while (clip.Step()) for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                int li = r * cols + c; if (li >= (int)list.size()) break;
                int i = list[li]; Tex& t = g_tex[i];
                if (c) ImGui::SameLine();
                ImGui::BeginGroup(); ImGui::PushID(i);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                // checkerboard so see-through textures show
                float cs = g_thumb / 4;
                for (int yy = 0; yy < 4; ++yy) for (int xx = 0; xx < 4; ++xx)
                    dl->AddRectFilled(ImVec2(p.x + xx * cs, p.y + yy * cs), ImVec2(p.x + (xx + 1) * cs, p.y + (yy + 1) * cs), ((xx + yy) & 1) ? IM_COL32(90, 90, 90, 255) : IM_COL32(60, 60, 60, 255));
                gfx::Tex tx = TexOf(i);
                float sc = std::min(g_thumb / t.w, g_thumb / t.h);
                ImVec2 isz(t.w * sc, t.h * sc), off((g_thumb - isz.x) / 2, (g_thumb - isz.y) / 2);
                if (tx) dl->AddImage(gfx::Id(tx), ImVec2(p.x + off.x, p.y + off.y), ImVec2(p.x + off.x + isz.x, p.y + off.y + isz.y));
                else if (t.failed) dl->AddText(ImVec2(p.x + 4, p.y + 4), IM_COL32(255, 90, 90, 255), "?");
                if (ImGui::InvisibleButton("##t", ImVec2(g_thumb, g_thumb))) g_texSel = i;
                if (i == g_texSel) dl->AddRect(p, ImVec2(p.x + g_thumb, p.y + g_thumb), IM_COL32(255, 210, 60, 255), 0, 0, 2.f);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s\n%dx%d %s\n%s\n%s", t.name.c_str(), t.w, t.h, FmtName(t.fmt), t.pack.c_str(), t.source.c_str());
                    if (tx) { float s2 = std::min(1.f, std::min(256.f / t.w, 256.f / t.h)) * 1.5f; ImGui::Image(gfx::Id(tx), ImVec2(t.w * s2, t.h * s2)); }
                    ImGui::EndTooltip();
                }
                std::string nm = t.name; if (nm.size() > (size_t)(g_thumb / 6)) nm = nm.substr(0, (size_t)(g_thumb / 6)) + "..";
                ImGui::TextDisabled("%s", nm.c_str());
                ImGui::PopID(); ImGui::EndGroup();
            }
        }
        ImGui::EndChild();
    }
    // ---- fonts
    if (ImGui::CollapsingHeader("Fonts")) {
        ImGui::SetNextItemWidth(300); ImGui::InputText("preview text", g_fontPreview, sizeof g_fontPreview); ImGui::SameLine();
        ImGui::SetNextItemWidth(120); ImGui::SliderFloat("size", &g_fontScale, 0.25f, 3.f, "%.2f");
        for (int i = 0; i < (int)g_fonts.size(); ++i) {
            Font& f = g_fonts[i];
            ImGui::PushID(i);
            if (ImGui::RadioButton("##sel", g_fontSel == i)) g_fontSel = i;
            if (!f.file.empty()) { ImGui::SameLine(); ImGui::Text("%s  (vector, %s)", f.name.c_str(), f.source.c_str()); }
            else { ImGui::SameLine(); ImGui::Text("%s  (%d chars, line %d px, %s)", f.name.c_str(), (int)f.g.size(), f.line, f.source.c_str()); }
            if (g_sel >= 0 && g_sel < (int)Elems().size() && Elems()[g_sel].type == E_TEXT) {
                ImGui::SameLine(); if (ImGui::SmallButton("use for the selected text")) { Elems()[g_sel].font = f.name; Elems()[g_sel].fontIdx = i; g_dirty = true; }
            }
            ImVec2 p = ImGui::GetCursorScreenPos();
            float hgt = f.line * g_fontScale + 6;
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + hgt), IM_COL32(60, 75, 95, 255));
            if (f.tex >= 0 || !f.file.empty()) DrawText(ImGui::GetWindowDrawList(), f, ImVec2(p.x + 6, p.y + 3), g_fontScale, IM_COL32(255, 255, 255, 255), g_fontPreview, 0, 0);
            else ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 6, p.y + 3), IM_COL32(255, 120, 120, 255), "(texture not found)");
            ImGui::Dummy(ImVec2(10, hgt));
            ImGui::PopID();
        }
    }
}
}   // namespace uied
