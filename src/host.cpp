// The editor's host: game-mode detection, live values ({fps} {speed}...), "Capture current screen" (FEng objects
// to editor elements) and the hooks between the panel and the editor (src/editor/uied.cpp).
#include "host.h"

#include "config.h"
#include "log.h"
#include "panel.h"
#include "editor/uied.h"
#include "game/feng.h"

#include "imgui.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace uiedit {

namespace fe {
enum { kImage = 1, kString = 2, kGroup = 5, kMultiImage = 7, kColoredImage = 9 };
constexpr uintptr_t LocalizeString = 0x512570;  // cdecl(wchar_t* buf, int size, u32 languageHash) -> bool
constexpr uintptr_t GetTextureInfo = 0x4901D0;  // cdecl(u32 hash, int returnDefault, int includeUnloaded) -> TextureInfo*
// TextureInfo: +0x0C name[24], +0x18 IDirect3DTexture9* (in its 0x20-byte platform part), +0x24 hash
} // namespace fe

static bool g_ready = false;

static bool Readable(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }
static uint8_t* TexInfo(uint32_t hash) {
    uint8_t* ti = ((uint8_t*(__cdecl*)(uint32_t, int, int))fe::GetTextureInfo)(hash, 0, 0);
    return Readable(ti, 0x28) && *(uint32_t*)(ti + 0x24) == hash ? ti : nullptr;
}
static uint32_t BHash(const char* s) { uint32_t h = 0xFFFFFFFF; for (; *s; ++s) h = h * 33 + (uint8_t)*s; return h; }

// What a group hands its children (the game's render context, 0x51B0B0): a 2D affine transform (screen =
// m * local + t, from the screen centre) and a colour that multiplies theirs, alpha included.
struct Xf {
    float m[2][2] = {{1, 0}, {0, 1}}, t[2] = {0, 0}, z = 0;
    float col[4] = {1, 1, 1, 1};
};

// The object's own transform, as the game builds it: the unit quad is sized, rotated about the pivot (+0x10)
// by the quaternion (+0x28), then placed (+0x1C). Only the screen-plane part of the rotation is kept, which is
// what the game shows too (a 180-degree turn about Y is a mirror).
static Xf Local(const float* f, const Xf& parent, const int* c, bool& rotated) {
    float qx = f[0x28 / 4], qy = f[0x2C / 4], qz = f[0x30 / 4], qw = f[0x34 / 4];
    float qn = qx * qx + qy * qy + qz * qz + qw * qw;
    float r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (qn > 1e-6f && !(qx == 0 && qy == 0 && qz == 0)) {
        float k = 2 / qn;
        r[0][0] = 1 - k * (qy * qy + qz * qz); r[0][1] = k * (qx * qy - qz * qw); r[0][2] = k * (qx * qz + qy * qw);
        r[1][0] = k * (qx * qy + qz * qw); r[1][1] = 1 - k * (qx * qx + qz * qz); r[1][2] = k * (qy * qz - qx * qw);
        r[2][0] = k * (qx * qz - qy * qw); r[2][1] = k * (qy * qz + qx * qw); r[2][2] = 1 - k * (qx * qx + qy * qy);
    }
    float sx = f[0x38 / 4], sy = f[0x3C / 4];
    float px = f[0x10 / 4], py = f[0x14 / 4], pz = f[0x18 / 4];
    // local: v -> R * (S v - p) + p + pos
    float lm[2][2] = {{r[0][0] * sx, r[0][1] * sy}, {r[1][0] * sx, r[1][1] * sy}};
    float lt[2] = {f[0x1C / 4] + px - (r[0][0] * px + r[0][1] * py + r[0][2] * pz),
                   f[0x20 / 4] + py - (r[1][0] * px + r[1][1] * py + r[1][2] * pz)};
    // off-axis turns in the screen plane can't be copied (elements are axis-aligned); flips and 180s can
    float ox = fabsf(r[0][1]) + fabsf(r[1][0]);
    if (ox > 0.02f) rotated = true;
    Xf me;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) me.m[i][j] = parent.m[i][0] * lm[0][j] + parent.m[i][1] * lm[1][j];
        me.t[i] = parent.t[i] + parent.m[i][0] * lt[0] + parent.m[i][1] * lt[1];
    }
    float own[4] = {c[2] / 255.0f, c[1] / 255.0f, c[0] / 255.0f, c[3] / 255.0f};
    for (int i = 0; i < 4; ++i) me.col[i] = parent.col[i] * own[i];
    me.z = parent.z + f[0x24 / 4];
    return me;
}

static std::string Narrow(const wchar_t* w) {
    std::string o;
    for (int i = 0; w && w[i] && i < 2048; ++i) {
        wchar_t c = w[i];
        if (c == L'^') o += '\n';
        else if (c == L'\n' || (c >= 32 && c < 256)) o += (char)c;
        else if (c >= 256) o += '?';
    }
    return o;
}

// Draw order (0x51B850): every object goes in one of 256 depth buckets, round(z) clamped to 0..255 (z = its
// position's z plus its groups'), and the buckets are drawn from 255 down to 0 - in a bucket, in list order.
static int DepthBucket(float z) { int b = (int)lroundf(z); return b < 0 ? 0 : b > 255 ? 255 : b; }

struct CaptureStats { int images = 0, texts = 0, hidden = 0, rotated = 0, offscreen = 0; };

static void Walk(uint8_t* obj, const Xf& parent, const std::string& pkgName, std::vector<uied::ImportItem>& out, CaptureStats& st, int depth) {
    for (int guard = 0; obj && guard < 4096; ++guard, obj = *(uint8_t**)(obj + 4)) {
        if (!Readable(obj, 0x78)) return;
        uint32_t type = *(uint32_t*)(obj + 0x18), flags = *(uint32_t*)(obj + 0x1C);
        uint8_t* d = *(uint8_t**)(obj + 0x2C);
        if (flags & 1) { ++st.hidden; continue; }
        if (!Readable(d, 0x54)) continue;
        const float* f = (const float*)d;
        bool rotated = false;
        Xf me = Local(f, parent, (const int*)d, rotated);
        char nm[48];
        uint32_t nameHash = *(uint32_t*)(obj + 0x10);
        if (type == fe::kGroup) {
            uint8_t* child = *(uint8_t**)(obj + 0x64);
            // a faded-out group hides everything in it
            if (me.col[3] > 0.004f && depth < 16 && *(int*)(obj + 0x60) > 0) Walk(child, me, pkgName, out, st, depth + 1);
            continue;
        }
        if (me.col[3] <= 0.004f) { ++st.hidden; continue; }
        // screen axes of the piece: a = where its x goes, b = where its y goes. Split into a mirror (det < 0),
        // a turn and the two sizes; a 180 turn is kept as a double mirror
        float ax = me.m[0][0], ay = me.m[1][0], bx = me.m[0][1], by = me.m[1][1];
        float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
        bool flipX = ax * by - ay * bx < 0, flipY = false;
        if (flipX) ax = -ax, ay = -ay;
        float deg = atan2f(ay, ax) * 180 / 3.14159265f;
        if (fabsf(deg) > 135) { flipX = !flipX; flipY = true; deg += deg > 0 ? -180 : 180; }
        if (fabsf(deg) < 0.5f) deg = 0;
        (void)rotated;
        if (deg != 0) ++st.rotated;
        for (float& v : me.col) v = v > 1 ? 1 : v;
        // parked off the screen (unused list rows, slide-in pieces): not part of what's shown
        float ext = (la + lb) / 2;
        if (fabsf(me.t[0]) - ext > 330 || fabsf(me.t[1]) - ext > 250) { ++st.offscreen; continue; }
        if (type == fe::kImage || type == fe::kMultiImage || type == fe::kColoredImage) {
            uied::ImportItem it;
            it.res = *(uint32_t*)(obj + 0x24);
            if (uint8_t* ti = TexInfo(it.res)) { char n[25] = {}; memcpy(n, ti + 0x0C, 24); it.texName = n; }
            it.x = me.t[0]; it.y = me.t[1];
            it.w = la; it.h = lb; it.rot = deg;
            it.u0 = f[0x44 / 4]; it.v0 = f[0x48 / 4]; it.u1 = f[0x4C / 4]; it.v1 = f[0x50 / 4];
            if (flipX) std::swap(it.u0, it.u1);
            if (flipY) std::swap(it.v0, it.v1);
            memcpy(it.col, me.col, sizeof it.col);
            snprintf(nm, sizeof nm, "image %08X", nameHash);
            it.name = nm;
            it.layer = DepthBucket(me.z);
            out.push_back(it);
            ++st.images;
        } else if (type == fe::kString) {
            std::string s;
            if (flags & 2) {
                const wchar_t* w = *(const wchar_t**)(obj + 0x64);
                if (Readable(w, 2)) s = Narrow(w);
            } else {
                static wchar_t buf[0x800];
                buf[0] = 0;
                if (((bool(__cdecl*)(wchar_t*, int, uint32_t))fe::LocalizeString)(buf, 0x800, *(uint32_t*)(obj + 0x60))) s = Narrow(buf);
            }
            if (s.empty()) continue;
            uied::ImportItem it;
            it.text = true;
            it.res = *(uint32_t*)(obj + 0x24);
            it.x = me.t[0]; it.y = me.t[1]; it.scale = la > 0 ? la : 1.0f;
            it.just = *(int*)(obj + 0x6C);
            it.str = s;
            memcpy(it.col, me.col, sizeof it.col);
            it.name = s.substr(0, s.find('\n')).substr(0, 32);
            it.layer = DepthBucket(me.z);
            out.push_back(it);
            ++st.texts;
        }
    }
}


static char g_captureName[64] = "";
static std::string g_lastCapture;

static void CaptureScreen(const std::string& name) {
    auto pkgs = FengPackages();
    int total = 0;
    CaptureStats st;
    std::vector<std::pair<std::string, std::vector<uied::ImportItem>>> caps;
    for (auto& p : pkgs) {
        uint8_t* pkg = (uint8_t*)p.first;
        if (!Readable(pkg, 0x50)) continue;
        std::string pn = p.second;
        size_t dot = pn.rfind('.');
        if (dot != std::string::npos) pn.resize(dot);
        if (_stricmp(pn.c_str(), "UI_PC_Help_Bar") == 0) continue;   // the PC mouse bar
        std::vector<uied::ImportItem> items;
        Walk(*(uint8_t**)(pkg + 0x4C), Xf(), pn, items, st, 0);
        if (items.empty()) continue;
        total += (int)items.size();
        caps.emplace_back(name + "/" + pn, std::move(items));
    }
    // depth buckets -> layers 0, 1, 2... (deepest = 0)
    std::vector<int> used;
    for (auto& c : caps) for (auto& it : c.second) used.push_back(it.layer);
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    for (auto& c : caps) {
        for (auto& it : c.second) it.layer = (int)(used.end() - std::upper_bound(used.begin(), used.end(), it.layer));
        uied::Import(c.first, c.second);
    }
    char b[200];
    snprintf(b, sizeof b, "Captured \"%s\": %d images, %d texts from %d screen(s), %d turned, %d off-screen skipped", name.c_str(), st.images,
             st.texts, (int)pkgs.size(), st.rotated, st.offscreen);
    g_lastCapture = b;
    Log("ui editor: %s", b);
}

// ---------------------------------------------------------------------------------------------
// What the game is doing, as the HUD it loads (0x57F354): drag if byte [0x89E7D8] or [0x890108], drift if byte
// [0x89E7D9] or [0x890104], free roam when no race is set up ([0x89E7B0] == 0), outrun (race type 7), else a race.
// -1: not driving (menus, loading). In the world = game state [0x8654A4] 6 and the race coordinator [0x8900D8] in 4.
static int GameMode() {
    if (*(int*)0x8654A4 != 6) return -1;
    void* rc = *(void**)0x8900D8;
    if (!rc || *(int*)rc != 4) return -1;
    if (*(int*)0x89E7B0 == 7) return uied::OUTRUN;
    if (*(uint8_t*)0x89E7D8 || *(int*)0x890108) return uied::DRAG;
    if (*(uint8_t*)0x89E7D9 || *(int*)0x890104) return uied::DRIFT;
    if (*(int*)0x89E7B0 == 0) return uied::FREEROAM;
    return uied::RACE;
}

// live values for text elements: {fps} {speed} {mph} {clock} (more come with the HUD data)
static float g_fps = 0;
static bool Token(const std::string& k, std::string& out) {
    char b[64];
    if (k == "fps") { snprintf(b, sizeof b, "%.0f", g_fps); out = b; return true; }
    if (k == "speed" || k == "mph") {
        uint8_t* p1 = *(uint8_t**)0x8900AC;
        uint8_t* car = p1 && Readable(p1 + 4, 4) ? *(uint8_t**)(p1 + 4) : nullptr;
        float sp = 0;
        if (car && Readable(car + 0x70, 12)) { const float* v = (const float*)(car + 0x70); sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
        snprintf(b, sizeof b, "%.0f", k == "mph" ? sp * 2.23694f : sp * 3.6f); out = b; return true;
    }
    return false;
}

void HostInit() {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string gameDir = exe;
    size_t sl = gameDir.find_last_of("\\/");
    if (sl != std::string::npos) gameDir.resize(sl);
    uied::menuMode = false;
    uied::tokens = Token;
    uied::liveTexture = [](const std::string& name, float* su, float* sv) -> IDirect3DTexture9* {
        uint8_t* ti = TexInfo(BHash(name.c_str()));
        IDirect3DTexture9* tx = ti ? *(IDirect3DTexture9**)(ti + 0x18) : nullptr;
        if (!tx || IsBadReadPtr(tx, 4)) return nullptr;
        D3DSURFACE_DESC dd;
        int w = *(int16_t*)(ti + 0x44), h = *(int16_t*)(ti + 0x46);
        if (SUCCEEDED(tx->GetLevelDesc(0, &dd)) && dd.Width && dd.Height && w > 0 && h > 0) { *su = (float)w / dd.Width; *sv = (float)h / dd.Height; }
        return tx;
    };
    uied::Init(gameDir, g_scriptsDir + "NFSU2UIEditor_layout.txt", [](const char* line) { Log("%s", line); });
    g_ready = true;
}
void HostTick(bool focused) { (void)focused; }
bool HostWantsFrames() { return g_ready && (uied::WantsFrames() || uied::MenuIsOpen()); }
void HostBeginFrame(IDirect3DDevice9* dev) {
    if (!g_ready) return;
    uied::SetDevice(dev);
    static LARGE_INTEGER f = {}, last = {};
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!f.QuadPart) { QueryPerformanceFrequency(&f); last = now; }
    double dt = (double)(now.QuadPart - last.QuadPart) / (double)f.QuadPart; last = now;
    if (dt > 0) g_fps = g_fps * 0.95f + (float)(1.0 / dt) * 0.05f;
}
void HostDraw() {
    if (!g_ready) return;
    uied::DrawOverlay(GameMode());
    if (uied::testMode)
        ImGui::GetForegroundDrawList()->AddText(ImVec2(12, ImGui::GetIO().DisplaySize.y - 28), IM_COL32(255, 170, 60, 255),
                                                "UI editor test - F6 > Stop test");
}
void HostPanel() {
    if (!g_ready) { ImGui::TextDisabled("UI editor not started."); return; }
    if (ImGui::CollapsingHeader("Capture a game screen")) {
        ImGui::TextDisabled("Open a menu or HUD in the game, then capture it: every visible picture and text becomes an element.");
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##capname", "name, e.g. Race HUD", g_captureName, sizeof g_captureName);
        ImGui::SameLine();
        if (ImGui::Button("Capture current screen")) CaptureScreen(g_captureName[0] ? g_captureName : "Capture");
        if (!g_lastCapture.empty()) ImGui::TextDisabled("%s", g_lastCapture.c_str());
    }
    uied::Panel();
}
void HostRelease() { if (g_ready) uied::Release(); }

} // namespace uiedit
