// The editor's host: game-mode detection, live values ({fps} {speed}...), "Capture current screen" (FEng objects
// to editor elements) and the hooks between the panel and the editor (src/editor/uied.cpp).
#include "host.h"

#include "config.h"
#include "log.h"
#include "panel.h"
#include "editor/uied.h"
#include "game/feng.h"
#include "reui_api.h"
#include "game/car.h"

#include "imgui.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace reui {

extern std::vector<REUI_TokenFn> g_tokenProviders;
extern bool g_menuDriverExternal;
extern std::vector<std::pair<REUI_MapMarkerFn, void*>> g_mapMarkers;

namespace fe {
enum { kImage = 1, kString = 2, kGroup = 5, kMultiImage = 7, kColoredImage = 9 };
constexpr uintptr_t LocalizeString = 0x512570;  // cdecl(wchar_t* buf, int size, u32 languageHash) -> bool
constexpr uintptr_t GetTextureInfo = 0x4901D0;  // cdecl(u32 hash, int returnDefault, int includeUnloaded) -> TextureInfo*
// TextureInfo: +0x0C name[24], +0x18 IDirect3DTexture9* (in its 0x20-byte platform part), +0x24 hash
} // namespace fe

static bool g_ready = false;
static bool g_uniPad = false;   // NFSU2UniPad.asi is loaded

static bool Readable(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }
// A Direct3D texture? (a TextureInfo's +0x18 isn't always one: unloaded / placeholder resources hold other data there)
// Its vtable has to be in d3d9.dll (the system's or a wrapper's, whichever the game loaded).
static bool IsD3DTexture(const void* p) {
    static uintptr_t lo = 0, hi = 0;
    if (!lo) {
        HMODULE m = GetModuleHandleA("d3d9.dll");
        if (!m) return false;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)m;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)m + dos->e_lfanew);
        lo = (uintptr_t)m; hi = lo + nt->OptionalHeader.SizeOfImage;
    }
    if (!p || IsBadReadPtr(p, 4)) return false;
    uintptr_t vt = *(const uintptr_t*)p;
    if (vt < lo || vt >= hi || IsBadReadPtr((const void*)vt, 0x4C)) return false;
    uintptr_t fn = ((const uintptr_t*)vt)[17];   // GetLevelDesc
    return fn >= lo && fn < hi;
}
// A TextureInfo's Direct3D texture: +0 points at its platform part, which holds the texture at +0x18
// (UniPad's RE_NOTES "Making textures at runtime"); nullptr when it isn't made / loaded yet
static IDirect3DTexture9* TexOfInfo(uint8_t* ti) {
    if (!ti || IsBadReadPtr(ti, 4)) return nullptr;
    uint8_t* plat = *(uint8_t**)ti;
    if (!plat || IsBadReadPtr(plat, 0x1C)) return nullptr;
    IDirect3DTexture9* t = *(IDirect3DTexture9**)(plat + 0x18);
    return IsD3DTexture(t) ? t : nullptr;
}
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


static int GameMode();
// The crash camera: World::SetTimeScale 0x5EA280 ([0x890080] +0x108 scale) at about 0.333 while it runs (the jump
// camera asks 0.4, a speed-up 2.0). Logged on change so the cameras can be told apart.
static bool InWreck() {
    uint8_t* w = *(uint8_t**)0x890080;
    if (!Readable(w, 0x110)) return false;
    float sc = *(float*)(w + 0x108);
    return sc > 0.3f && sc < 0.37f;
}
static void WatchCameras() {
    uint8_t* w = *(uint8_t**)0x890080;
    float sc = Readable(w, 0x110) ? *(float*)(w + 0x108) : 1.f;
    uint8_t* p1 = *(uint8_t**)0x8900AC;
    uint8_t* cam = Readable(p1, 0x23C) ? *(uint8_t**)(p1 + 0x238) : nullptr;
    int cs = Readable(cam, 0x10) ? *(int*)(cam + 0xC) : -1;
    int* rs = *(int**)0x890118;
    int race = Readable(rs, 4) ? *rs : -1;
    static float lsc = -1; static int lcs = -2, lrace = -2;
    if (fabsf(sc - lsc) > 0.001f || cs != lcs || race != lrace) {
        Log("cameras: world speed x%.3f, camera state %d, race status %d, race coordinator %d", sc, cs, race, *(void**)0x8900D8 ? **(int**)0x8900D8 : -1);
        lsc = sc; lcs = cs; lrace = race;
    }
}

// Development aid: scripts\NFSU2_REUI.dump appears -> every FEng object on screen (hidden ones too) with its raw
// data goes to scripts\NFSU2_REUI_dump.txt (used to rebuild the game's HUD pieces).
static void DumpWalk(FILE* f, uint8_t* obj, const Xf& parent, int depth) {
    for (int guard = 0; obj && guard < 4096; ++guard, obj = *(uint8_t**)(obj + 4)) {
        if (!Readable(obj, 0x78)) return;
        uint32_t type = *(uint32_t*)(obj + 0x18), flags = *(uint32_t*)(obj + 0x1C), nameHash = *(uint32_t*)(obj + 0x10), res = *(uint32_t*)(obj + 0x24);
        uint8_t* d = *(uint8_t**)(obj + 0x2C);
        if (!Readable(d, 0x54)) { fprintf(f, "%*s? type %u name %08X (no data)\n", depth * 2, "", type, nameHash); continue; }
        const float* v = (const float*)d; const int* c = (const int*)d;
        bool rotated = false;
        Xf me = Local(v, parent, c, rotated);
        float ax = me.m[0][0], ay = me.m[1][0], bx = me.m[0][1], by = me.m[1][1];
        float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
        float deg = atan2f(ay, ax) * 180 / 3.14159265f;
        std::string tn;
        if (type != fe::kGroup && type != fe::kString) if (uint8_t* ti = TexInfo(res)) { char n[25] = {}; memcpy(n, ti + 0x0C, 24); tn = n; }
        std::string str;
        if (type == fe::kString) {
            if (flags & 2) { const wchar_t* w = *(const wchar_t**)(obj + 0x64); if (Readable(w, 2)) str = Narrow(w); }
            else { static wchar_t buf[0x800]; buf[0] = 0; if (((bool(__cdecl*)(wchar_t*, int, uint32_t))fe::LocalizeString)(buf, 0x800, *(uint32_t*)(obj + 0x60))) str = Narrow(buf); }
            for (auto& ch : str) if (ch == '\n') ch = '|';
        }
        fprintf(f, "%*s%s name %08X obj %p flags %X res %08X%s%s | screen %.1f %.1f size %.1f %.1f turn %.1f det %.2f z %.1f alpha %.2f col %d %d %d %d"
                   " | raw pos %.1f %.1f %.1f pivot %.1f %.1f size %.3f %.3f quat %.3f %.3f %.3f %.3f uv %.3f %.3f %.3f %.3f%s%s\n",
                depth * 2, "", type == fe::kGroup ? "GROUP" : type == fe::kString ? "TEXT" : type == fe::kImage ? "IMAGE" : type == fe::kMultiImage ? "MULTI" : type == fe::kColoredImage ? "CIMAGE" : "OTHER",
                nameHash, obj, flags, res, tn.empty() ? "" : " tex ", tn.c_str(), me.t[0], me.t[1], la, lb, deg, ax * by - ay * bx, me.z, me.col[3], c[2], c[1], c[0], c[3],
                v[0x1C / 4], v[0x20 / 4], v[0x24 / 4], v[0x10 / 4], v[0x14 / 4], v[0x38 / 4], v[0x3C / 4], v[0x28 / 4], v[0x2C / 4], v[0x30 / 4], v[0x34 / 4],
                v[0x44 / 4], v[0x48 / 4], v[0x4C / 4], v[0x50 / 4], str.empty() ? "" : " text: ", str.c_str());
        if (type == fe::kGroup && depth < 16 && *(int*)(obj + 0x60) > 0) DumpWalk(f, *(uint8_t**)(obj + 0x64), me, depth + 1);
    }
}
static void PollDump() {
    static DWORD next = 0;
    DWORD now = GetTickCount();
    if (now < next) return;
    next = now + 1000;
    std::string q = g_scriptsDir + "NFSU2_REUI.dump";
    if (GetFileAttributesA(q.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(q.c_str());
    std::string out = g_scriptsDir + "NFSU2_REUI_dump.txt";
    FILE* f = fopen(out.c_str(), "w");
    if (!f) return;
    CarValues cv = ReadCar();
    fprintf(f, "# screen %.0fx%.0f, mode %d, hud style [0x89E7C0] %d, car rpm %.0f max %.0f gear %d speed %.2f nos %.3f boost %.3f\n",
            ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, GameMode(), *(int*)0x89E7C0, cv.rpm, cv.rpmMax, cv.gear, cv.speed, cv.nos, cv.boost);
    for (auto& p : FengPackages()) {
        uint8_t* pkg = (uint8_t*)p.first;
        if (!Readable(pkg, 0x50)) continue;
        fprintf(f, "\n=== package %s (%p)\n", p.second.c_str(), pkg);
        DumpWalk(f, *(uint8_t**)(pkg + 0x4C), Xf(), 0);
    }
    fclose(f);
    Log("dump: %s written", out.c_str());
}

// gameObject (uied.h): every visible FEng object on screen this frame by name hash, built once per frame on first use
static std::unordered_map<uint32_t, uied::GameObject> g_objs;
static int g_objsFrame = -1;
static void ObjWalk(uint8_t* obj, const Xf& parent, int depth) {
    for (int guard = 0; obj && guard < 4096; ++guard, obj = *(uint8_t**)(obj + 4)) {
        if (!Readable(obj, 0x78)) return;
        uint32_t type = *(uint32_t*)(obj + 0x18), flags = *(uint32_t*)(obj + 0x1C);
        uint8_t* d = *(uint8_t**)(obj + 0x2C);
        if ((flags & 1) || !Readable(d, 0x54)) continue;
        const float* f = (const float*)d;
        bool rotated = false;
        Xf me = Local(f, parent, (const int*)d, rotated);
        if (me.col[3] <= 0.004f) continue;
        if (type == fe::kGroup) {
            if (depth < 16 && *(int*)(obj + 0x60) > 0) ObjWalk(*(uint8_t**)(obj + 0x64), me, depth + 1);
            continue;
        }
        if (type != fe::kImage && type != fe::kMultiImage && type != fe::kColoredImage) continue;
        uint32_t nameHash = *(uint32_t*)(obj + 0x10);
        if (g_objs.count(nameHash)) continue;
        float ax = me.m[0][0], ay = me.m[1][0], bx = me.m[0][1], by = me.m[1][1];
        uied::GameObject g;
        g.w = sqrtf(ax * ax + ay * ay); g.h = sqrtf(bx * bx + by * by);
        bool flipX = ax * by - ay * bx < 0, flipY = false;
        if (flipX) ax = -ax, ay = -ay;
        float deg = atan2f(ay, ax) * 180 / 3.14159265f;
        if (fabsf(deg) > 135) { flipX = !flipX; flipY = true; deg += deg > 0 ? -180 : 180; }
        g.rot = deg; g.x = me.t[0]; g.y = me.t[1];
        g.u0 = f[0x44 / 4]; g.v0 = f[0x48 / 4]; g.u1 = f[0x4C / 4]; g.v1 = f[0x50 / 4];
        if (flipX) std::swap(g.u0, g.u1);
        if (flipY) std::swap(g.v0, g.v1);
        for (int i = 0; i < 4; ++i) g.col[i] = me.col[i] > 1 ? 1 : me.col[i];
        if (uint8_t* ti = TexInfo(*(uint32_t*)(obj + 0x24))) {
            IDirect3DTexture9* tx = TexOfInfo(ti);
            if (tx) {
                g.tex = tx;
                D3DSURFACE_DESC dd; int w = *(int16_t*)(ti + 0x44), h = *(int16_t*)(ti + 0x46);
                if (SUCCEEDED(tx->GetLevelDesc(0, &dd)) && dd.Width && dd.Height && w > 0 && h > 0) { g.su = (float)w / dd.Width; g.sv = (float)h / dd.Height; }
            }
        }
        g_objs[nameHash] = g;
    }
}
static bool GameObjectNow(unsigned hash, uied::GameObject& out) {
    int fr = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
    if (fr != g_objsFrame) {
        g_objsFrame = fr; g_objs.clear();
        for (auto& p : FengPackages()) {
            uint8_t* pkg = (uint8_t*)p.first;
            if (Readable(pkg, 0x50)) ObjWalk(*(uint8_t**)(pkg + 0x4C), Xf(), 0);
        }
    }
    auto it = g_objs.find(hash);
    if (it == g_objs.end()) return false;
    out = it->second;
    return true;
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
    if (!rc || (*(int*)rc != 4 && *(int*)rc != 3)) return -1;   // 4 driving, 3 a race's countdown (PRERACE)
    if (*(int*)0x89E7B0 == 7) return uied::OUTRUN;
    if (*(uint8_t*)0x89E7D8 || *(int*)0x890108) return uied::DRAG;
    if (*(uint8_t*)0x89E7D9 || *(int*)0x890104) return uied::DRIFT;
    if (*(int*)0x89E7B0 == 0) return uied::FREEROAM;
    return uied::RACE;
}

// live values for text elements: {fps} {speed} {mph}, the car's gauges (game/car.cpp), other mods' (reui_api.h)
static float g_fps = 0;
// ---- the rear-view mirror
// The game renders its mirror camera (eView 3: 0x832DE0 + 3 * 0x70, +8 = active; camera 0x8718C0, render target
// struct 0x86E7BC) into a texture whose pointer is [0x8708A4]; the "RVM" art is drawn over it and RVM_MASK into its
// alpha (0x5CAE3D..0x5CAFAF), so the texture already has the mirror's shape. 0x5CBF40 then draws it on the screen
// (x 200..440, y 15..95 of 640x480) when view 3 is active: its "je" at 0x5CC0E6 (0F 84 0A 01 00 00) skips that
// draw. An element showing {rvm} replaces the game's: while one is drawn, that jump is made unconditional.
static bool RvmActive() { uint8_t* v = (uint8_t*)(0x832DE0 + 3 * 0x70); return Readable(v, 9) && v[8] != 0; }
static IDirect3DTexture9* RvmTexture() {
    IDirect3DTexture9** pp = (IDirect3DTexture9**)0x8708A4;
    return RvmActive() && Readable(pp, 4) && *pp && Readable(*pp, 4) ? *pp : nullptr;
}
static DWORD g_rvmDrawnAt = 0;   // an element showed the mirror (GetTickCount)
static void RvmStockPatch(bool hide) {
    static int state = -1;   // -1 unknown, 0 game draws, 1 skipped
    if (state == (hide ? 1 : 0)) return;
    uint8_t* at = (uint8_t*)0x5CC0E6;
    static const uint8_t orig[6] = {0x0F, 0x84, 0x0A, 0x01, 0x00, 0x00}, skip[6] = {0x90, 0xE9, 0x0A, 0x01, 0x00, 0x00};
    if (state == -1 && memcmp(at, orig, 6) != 0) { state = 2; Log("mirror: unexpected code at 0x5CC0E6, the game's mirror stays"); return; }
    if (state == 2) return;
    DWORD old;
    if (!VirtualProtect(at, 6, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(at, hide ? skip : orig, 6);
    VirtualProtect(at, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 6);
    state = hide ? 1 : 0;
    Log("mirror: the game's own mirror %s", hide ? "hidden (an element shows {rvm})" : "shown");
}

// ---- "replace stock HUD [x]"
// The HUD ([Player+0x9A4], built by 0x4F8BF0) keeps its components at fixed slots; each component has the HUD
// feature bits it shows for at +0x10 / +0x14 (set by its constructor) and the last feature mask at +0x18 / +0x1C.
// 0x4C6690 (thiscall comp, lo, hi; ret 8) shows the component's FE objects when (bits & features) != 0, else hides
// them. To hide one piece: its bits to 0 and 0x4C6690 again; to bring it back: the constructor's bits and again.
// (The global mask 0x600090 can't do this per piece: the minimap and the region popup share bit 0, the three tach
// variants bits 5 / 6 / 28.)
struct HudPiece { const char* id; const char* label; const char* tip; int slot; uint32_t lo, hi; };
static const HudPiece kHudPieces[] = {
    {"tachometer", "tachometer", "The stock tach, speedometer and gear (all three tach variants: HUD+0x18 / 0x1C / 0x34).", 0x18, 0x10000060, 0},
    {"tachometer", nullptr, nullptr, 0x1C, 0x10000060, 0},
    {"tachometer", nullptr, nullptr, 0x34, 0x10000060, 0},
    {"nitrous", "nitrous gauge", "The stock nitrous bar (HUD+0x28, feature bit 50).", 0x28, 0, 0x40000},
    {"engage", "Cingular / Engage panel", "The phone / messages / event icons at top left (HUD+0x58, bit 33).", 0x58, 0, 0x2},
    {"bank", "bank", "The bank at top right (HUD+0x80, bit 51).", 0x80, 0, 0x80000},
    {"cash_reward", "cash reward", "The Cash Reward popup (HUD+0x78, bit 39).", 0x78, 0, 0x80},
    {"region", "region popup", "The area name when you drive into a new region (HUD+0x74).", 0x74, 1, 0},
    {"style_points", "style points", "The style points above the tach (HUD+0x4C, bit 17).", 0x4C, 0x20000, 0},
    {"minimap", "minimap", "The stock minimap / track map (HUD+0x5C).", 0x5C, 1, 0},
    {"race_position", "race position", "Position / racers at top right in a race (HUD+0x20, bit 9).", 0x20, 0x200, 0},
    {"laps", "laps and lap times", "Lap number, current and best lap (HUD+0x3C, bits 3 / 8 / 10).", 0x3C, 0x508, 0},
    {"split_time", "split time", "The split time (HUD+0x48, bit 7).", 0x48, 0x80, 0},
    {"wrong_way", "wrong way", "The wrong-way sign (HUD+0x44, bit 14).", 0x44, 0x4000, 0},
    {"outrun_meter", "outrun meter", "The outrun distance bar (HUD+0x6C, bit 38).", 0x6C, 0, 0x40},
};
static void HudPieceSet(uint8_t* c, bool hide, const HudPiece& p) {
    *(uint32_t*)(c + 0x10) = hide ? 0 : p.lo;
    *(uint32_t*)(c + 0x14) = hide ? 0 : p.hi;
    ((void(__thiscall*)(void*, uint32_t, uint32_t))0x4C6690)(c, *(uint32_t*)(c + 0x18), *(uint32_t*)(c + 0x1C));
}
static void ApplyHudReplace() {
    static uint8_t* lastHud = nullptr;
    static bool hidden[sizeof kHudPieces / sizeof kHudPieces[0]] = {};
    static DWORD lastAssert = 0;
    uint8_t* p1 = *(uint8_t**)0x8900AC;
    uint8_t* hud = *(int*)0x8654A4 == 6 && Readable(p1, 0x9A8) ? *(uint8_t**)(p1 + 0x9A4) : nullptr;
    if (!Readable(hud, 0x84)) hud = nullptr;
    if (hud != lastHud) { lastHud = hud; for (bool& h : hidden) h = false; }   // a new HUD: its pieces are as built
    if (!hud) return;
    DWORD now = GetTickCount();
    bool reassert = now - lastAssert > 500;   // a piece the game turned back on is hidden again
    if (reassert) lastAssert = now;
    for (size_t i = 0; i < sizeof kHudPieces / sizeof kHudPieces[0]; ++i) {
        const HudPiece& p = kHudPieces[i];
        uint8_t* c = *(uint8_t**)(hud + p.slot);
        if (!Readable(c, 0x20)) continue;
        bool want = uied::ReplaceWanted(p.id);
        uint32_t lo = *(uint32_t*)(c + 0x10), hi = *(uint32_t*)(c + 0x14);
        if (want && !hidden[i]) {
            if (lo != p.lo || hi != p.hi) {   // not the piece the table expects: leave it alone
                static bool told[64]; if (!told[i]) { told[i] = true; Log("replace: HUD+0x%X has bits %08X:%08X, expected %08X:%08X; not touched", p.slot, hi, lo, p.hi, p.lo); }
                continue;
            }
            HudPieceSet(c, true, p); hidden[i] = true;
            Log("replace: stock %s hidden (HUD+0x%X)", p.id, p.slot);
        } else if (!want && hidden[i]) {
            HudPieceSet(c, false, p); hidden[i] = false;
            Log("replace: stock %s shown again (HUD+0x%X)", p.id, p.slot);
        } else if (want && hidden[i] && reassert) {
            HudPieceSet(c, true, p);
        }
    }
}

// ---- the minimap, standalone (uied::MapView; the game's own is 0x4CA840 on its component HUD+0x5C, docs/RE_NOTES.md)
// Component: +0 its icon list (a ring: the component is the end), +0x14 the track info cache. Track info
// 0x5D3E40(cdecl track [0x89E7A0]): +0xAC / +0xB0 the map's top-left world x / y, +0xB4 its size (m), +0xC0 zoom: the
// minimap shows 0.3 / zoom of the map across (0.3 / [0x78E098] when that is above 1). Map texture "TRACKMAP<track>".
// Heading-up when the radar option ([0x83AA1C] free roam, [0x83AA1B] racing) is 2.
// Icon node: +0 next, +8 id, +0xC / +0x10 x / y, +0x14 heading (deg), +0x18 flags (bit 0 hidden past the rim, bit 1
// hidden), +0x1C info {+0 its FE object, +4 type: 3 AI racer (car by id 0x5F0E30), 4 player (Player[id]), 5..18 shops
// / events, 0..2 never shown}. 0x4AC2B0 thiscall(node, float pos[2], float dir[2]) ret 8 gives position and heading;
// 0x4CA6D0 thiscall(node) -> al: whether it's shown now. Dropped past 1 minimap width (10 in free roam).
// Whether a map texture's alpha says anything (checked once per texture): some track maps are drawn by the game
// through its mask, with their own alpha empty - drawn as they are they'd be invisible.
static bool MapAlphaOk(IDirect3DTexture9* t) {
    static IDirect3DTexture9* last = nullptr; static bool ok = true;
    if (t == last) return ok;
    last = t; ok = true;
    D3DSURFACE_DESC dd;
    if (FAILED(t->GetLevelDesc(0, &dd)) || dd.Pool != D3DPOOL_MANAGED) return ok;
    D3DLOCKED_RECT lr;
    if (FAILED(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) return ok;
    unsigned n = 0, sum = 0, mx = 0;   // alpha 0..255
    const uint8_t* px = (const uint8_t*)lr.pBits;
    if (dd.Format == D3DFMT_A8R8G8B8) {
        for (unsigned y = 0; y < dd.Height; y += 2) for (unsigned x = 0; x < dd.Width; x += 2) { unsigned a = px[y * lr.Pitch + x * 4 + 3]; ++n; sum += a; mx = std::max(mx, a); }
    } else if (dd.Format == D3DFMT_DXT3) {   // 16-byte blocks: 8 bytes of 4-bit alphas first
        for (unsigned by = 0; by < dd.Height / 4; ++by) for (unsigned bx = 0; bx < dd.Width / 4; ++bx) {
            const uint8_t* b = px + by * lr.Pitch + bx * 16;
            for (int i = 0; i < 8; ++i) { unsigned lo = (b[i] & 15) * 17, hi = (b[i] >> 4) * 17; n += 2; sum += lo + hi; mx = std::max(mx, std::max(lo, hi)); }
        }
    } else if (dd.Format == D3DFMT_DXT5) {   // two end alphas per block: enough to see if any is set
        for (unsigned by = 0; by < dd.Height / 4; ++by) for (unsigned bx = 0; bx < dd.Width / 4; ++bx) {
            const uint8_t* b = px + by * lr.Pitch + bx * 16; n += 2; sum += b[0] + b[1]; mx = std::max(mx, (unsigned)std::max(b[0], b[1]));
        }
    } else { t->UnlockRect(0); Log("minimap: map format %08X: alpha not checked", (unsigned)dd.Format); return ok; }
    t->UnlockRect(0);
    ok = mx >= 16;
    Log("minimap: map %ux%u alpha: average %u, highest %u -> %s", dd.Width, dd.Height, n ? sum / n : 0, mx, ok ? "used" : "empty, ignored");
    return ok;
}
static const char* g_mapWhy = "";   // (diagnostics: why there's no minimap, the icon list's counts)
static int g_mapStat[8];
static bool MapViewNow2(uied::MapView& out);
static bool MapViewNow(uied::MapView& out) {
    static int frame = -1; static bool have = false; static uied::MapView cache;
    int fr = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
    if (fr == frame) { if (have) out = cache; return have; }
    frame = fr;
    g_mapWhy = ""; memset(g_mapStat, 0, sizeof g_mapStat);
    have = MapViewNow2(cache);
    static DWORD lastLog = 0;
    if (GetTickCount() - lastLog > 3000) {
        lastLog = GetTickCount();
        uint8_t* p1 = *(uint8_t**)0x8900AC;
        uint8_t* hud = Readable(p1, 0x9A8) ? *(uint8_t**)(p1 + 0x9A4) : nullptr;
        uint8_t* comp = Readable(hud, 0x60) ? *(uint8_t**)(hud + 0x5C) : nullptr;
        Log("minimap: %s%s | comp %p [0x8364D4] %p mask %08X list %p | track %d", have ? "ok" : "none: ", g_mapWhy, comp, *(void**)0x8364D4,
            Readable(comp, 0x20) ? *(uint32_t*)(comp + 0x18) : 0, Readable(comp, 4) ? *(void**)comp : nullptr, *(int*)0x89E7A0);
        if (have) {
            const uied::MapView& v = cache;
            D3DSURFACE_DESC dd = {}; v.tex->GetLevelDesc(0, &dd);
            float pu = (v.px - v.x0) / v.size, pv = (v.y0 - v.py) / v.size;
            Log("minimap: map %p fmt %08X %ux%u pool %d levels %u su %.3f sv %.3f | x0 %.1f y0 %.1f size %.1f span %.4f | car %.1f %.1f uv %.3f %.3f bearing %.1f rotate %d",
                v.tex, (unsigned)dd.Format, dd.Width, dd.Height, (int)dd.Pool, v.tex->GetLevelCount(), v.su, v.sv, v.x0, v.y0, v.size, v.span, v.px, v.py, pu, pv, v.bearing, v.rotate ? 1 : 0);
            Log("minimap: map alpha %s", v.alphaOk ? "used" : "empty: added onto the screen like the game does");
            Log("minimap: icons: nodes %d, type skipped %d, flag hidden %d, not shown %d, no position %d, no texture %d, kept %d (player %d), markers %d",
                g_mapStat[0], g_mapStat[1], g_mapStat[2], g_mapStat[3], g_mapStat[4], g_mapStat[5], g_mapStat[6], g_mapStat[7], (int)v.blips.size() - g_mapStat[6]);
        }
    }
    if (have) out = cache;
    return have;
}
static bool MapViewNow2(uied::MapView& out) {
    out = uied::MapView();
    if (*(int*)0x8654A4 != 6) { g_mapWhy = "not in the world"; return false; }
    uint8_t* p1 = *(uint8_t**)0x8900AC;
    if (!Readable(p1, 0x9A8)) { g_mapWhy = "no player"; return false; }
    uint8_t* car = *(uint8_t**)(p1 + 4);
    uint8_t* hud = *(uint8_t**)(p1 + 0x9A4);
    if (!Readable(car, 0x410) || !Readable(hud, 0x60)) { g_mapWhy = "no car / HUD"; return false; }
    uint8_t* comp = *(uint8_t**)(hud + 0x5C);
    if (!Readable(comp, 0x20) || !(*(uint32_t*)(comp + 0x18) & 1)) { g_mapWhy = "game shows no minimap"; return false; }   // the game isn't showing a minimap now
    int track = *(int*)0x89E7A0;
    uint8_t* ti = ((uint8_t*(__cdecl*)(int))0x5D3E40)(track);
    if (!Readable(ti, 0xC4)) { g_mapWhy = "no track info"; return false; }
    uied::MapView& v = out;
    v.x0 = *(float*)(ti + 0xAC); v.y0 = *(float*)(ti + 0xB0); v.size = *(float*)(ti + 0xB4);
    float zoom = *(float*)(ti + 0xC0), zg = *(float*)0x78E098;
    v.span = zg > 1 ? 0.3f / zg : zoom > 1e-4f ? 0.3f / zoom : 0;
    if (!(v.size > 1 && v.size < 1e6f) || !(v.span > 1e-4f && v.span < 10)) { g_mapWhy = "odd map size / zoom"; return false; }
    char tn[32]; snprintf(tn, sizeof tn, "TRACKMAP%d", track);
    uint8_t* tinfo = TexInfo(BHash(tn));
    v.tex = TexOfInfo(tinfo);
    if (!v.tex) { g_mapWhy = tinfo ? "map texture not loaded" : "no map texture info"; return false; }
    D3DSURFACE_DESC dd; int tw = *(int16_t*)(tinfo + 0x44), th = *(int16_t*)(tinfo + 0x46);
    if (SUCCEEDED(v.tex->GetLevelDesc(0, &dd)) && dd.Width && dd.Height && tw > 0 && th > 0) { v.su = (float)tw / dd.Width; v.sv = (float)th / dd.Height; }
    v.alphaOk = MapAlphaOk(v.tex);
    bool freeRoam = *(int*)0x89E7B0 == 0;
    v.rotate = *(uint8_t*)(freeRoam ? 0x83AA1C : 0x83AA1B) == 2;
    v.limit = freeRoam ? 10.f : 1.f;
    v.px = *(float*)(car + 0x50); v.py = *(float*)(car + 0x54);
    float hr = *(uint16_t*)(car + 0x40C) * 6.2831853f / 65536;
    v.bearing = atan2f(cosf(hr), sinf(hr)) * 180 / 3.14159265f;   // dir (cos, sin): degrees clockwise from north
    // the game's icons: on the minimap object [0x8364D4] (0x4ECC80 makes it; 0x4CA840 runs on it), not the HUD slot
    uint8_t* mm = *(uint8_t**)0x8364D4;
    uint8_t* node = Readable(mm, 0x1C) ? *(uint8_t**)mm : nullptr;
    for (int guard = 0; node && node != mm && guard < 512; ++guard, node = *(uint8_t**)node) {
        ++g_mapStat[0];
        if (!Readable(node, 0x20)) break;
        uint8_t* info = *(uint8_t**)(node + 0x1C);
        if (!Readable(info, 8)) continue;
        int type = *(int*)(info + 4);
        uint32_t fl = *(uint32_t*)(node + 0x18);
        if (type < 3 || type > 18) { ++g_mapStat[1]; continue; }
        if (fl & 2) { ++g_mapStat[2]; continue; }
        if (!((bool(__thiscall*)(void*))0x4CA6D0)(node)) { ++g_mapStat[3]; continue; }
        float pos[2] = {NAN, NAN}, dir[2] = {0, 0};
        ((void(__thiscall*)(void*, float*, float*))0x4AC2B0)(node, pos, dir);
        if (!(pos[0] == pos[0]) || !(pos[1] == pos[1])) { ++g_mapStat[4]; continue; }   // (its car is gone)
        uied::MapBlip b;
        b.x = pos[0]; b.y = pos[1]; b.dirX = dir[0]; b.dirY = dir[1];
        b.hideOff = (fl & 1) != 0; b.player = type == 4;
        uint8_t* obj = *(uint8_t**)info;   // its FE object: texture, uv, size, colour as the game set them
        uint8_t* d = Readable(obj, 0x30) ? *(uint8_t**)(obj + 0x2C) : nullptr;
        if (Readable(d, 0x54)) {
            const float* f = (const float*)d; const int* c = (const int*)d;
            b.u0 = f[0x44 / 4]; b.v0 = f[0x48 / 4]; b.u1 = f[0x4C / 4]; b.v1 = f[0x50 / 4];
            b.w = fabsf(f[0x38 / 4]); b.h = fabsf(f[0x3C / 4]);
            b.col[0] = c[2] / 255.f; b.col[1] = c[1] / 255.f; b.col[2] = c[0] / 255.f; b.col[3] = c[3] / 255.f;
            if (uint8_t* ii = TexInfo(*(uint32_t*)(obj + 0x24))) {
                b.tex = TexOfInfo(ii);
                int iw = *(int16_t*)(ii + 0x44), ih = *(int16_t*)(ii + 0x46);
                if (b.tex && SUCCEEDED(b.tex->GetLevelDesc(0, &dd)) && dd.Width && dd.Height && iw > 0 && ih > 0) {
                    float su = (float)iw / dd.Width, sv = (float)ih / dd.Height; b.u0 *= su; b.u1 *= su; b.v0 *= sv; b.v1 *= sv;
                }
            }
        }
        if (!b.tex) { ++g_mapStat[5]; continue; }
        ++g_mapStat[6]; if (b.player) ++g_mapStat[7];
        if (b.w < 1 || b.w > 128) b.w = 14.5f;
        if (b.h < 1 || b.h > 128) b.h = 14.5f;
        v.blips.push_back(b);
    }
    // other mods' markers (the multiplayer mod's players)
    for (auto& p : g_mapMarkers) {
        REUI_MapMarker m[64];
        memset(m, 0, sizeof m);
        int n = p.first(m, 64, p.second);
        for (int i = 0; i < n && i < 64; ++i) {
            uied::MapBlip b;
            b.x = m[i].x; b.y = m[i].y;
            if (!(m[i].flags & 2)) { b.dirX = m[i].dirX; b.dirY = m[i].dirY; }
            float sz = m[i].size > 0 ? m[i].size : 1;
            if (m[i].icon && *m[i].icon) { b.texName = m[i].icon; b.w = b.h = 16 * sz; }
            else { b.w = 12.5f * sz; b.h = 16 * sz; }
            unsigned cl = m[i].color;
            b.col[0] = ((cl >> 16) & 255) / 255.f; b.col[1] = ((cl >> 8) & 255) / 255.f; b.col[2] = (cl & 255) / 255.f; b.col[3] = (cl >> 24) / 255.f;
            b.hideOff = (m[i].flags & 1) != 0; b.farOk = true;
            v.blips.push_back(b);
        }
    }
    return true;
}

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
    if (k.compare(0, 6, "trick_") == 0) {
        // style points (above the tach): the style-points HUD part [Hud + 0x4C] keeps its tricks in a list at +0x20
        // (node +8 done, +0xC points, +0x10 multiplier, +0x1C name text id); the first one not done is shown (0x4DA710 /
        // 0x4AB780): "+%d" or "+%dx%d" (points per multiplier x multiplier), its name; Animate when it starts, FadeOut
        // (0.9 s) when it ends
        static std::string name, pts; static bool live = false; static double endAt = -100;
        static int lastFrame = -1; int fr = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
        double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
        if (fr != lastFrame) {
            lastFrame = fr;
            uint8_t* p1 = *(uint8_t**)0x8900AC;
            uint8_t* hud = Readable(p1, 0x9A8) ? *(uint8_t**)(p1 + 0x9A4) : nullptr;
            uint8_t* sp = Readable(hud, 0x50) ? *(uint8_t**)(hud + 0x4C) : nullptr;
            uint8_t* cur = nullptr;
            if (GameMode() >= 0 && Readable(sp, 0x28)) {
                uint8_t* head = sp + 0x20;
                int guard = 0;
                for (uint8_t* n = *(uint8_t**)head; n && n != head && guard < 64 && Readable(n, 0x2C); n = *(uint8_t**)n, ++guard)
                    if (!n[8]) { cur = n; break; }
            }
            if (cur) {
                uint32_t id = *(uint32_t*)(cur + 0x1C);
                const char* t = id ? ((const char*(__cdecl*)(uint32_t))0x4FFA80)(id) : nullptr;
                name = t && !IsBadReadPtr(t, 1) ? t : "";
                float p = *(float*)(cur + 0xC); int mult = *(int*)(cur + 0x10);
                int per = (int)(id != 0xA46AAEA8 && mult > 0 ? p / mult : p);
                char b[48];
                if (mult > 1 && id != 0xA46AAEA8) snprintf(b, sizeof b, "%s%dx%d", per >= 0 ? "+" : "", per, mult);
                else snprintf(b, sizeof b, "%s%d", per >= 0 ? "+" : "", per);
                pts = b; live = true;
            } else if (live) { live = false; endAt = now; }
        }
        bool fading = !live && now - endAt < 0.9;
        if (k == "trick_on") out = live ? "1" : "";
        else if (k == "trick_fading") out = fading ? "1" : "";
        else if (k == "trick_name") out = live || fading ? name : "";
        else if (k == "trick_points") out = live || fading ? pts : "";
        else return false;
        return true;
    }
    if (k == "region" || k == "region_new") {
        // the region popup (Region_Message, 0x4A9E70): the player's +0x9B8 is the current region's text id; when it
        // changes the HUD prints that text (0x4FFA80) and plays ZoomIn (1.8 s)
        uint8_t* p1 = *(uint8_t**)0x8900AC;
        uint32_t id = Readable(p1, 0x9BC) && GameMode() >= 0 ? *(uint32_t*)(p1 + 0x9B8) : 0;
        static uint32_t last = 0; static double at = -100;
        double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
        static int lastFrame = -1; int fr = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
        if (fr != lastFrame) { lastFrame = fr; if (id && id != last) at = now; if (id) last = id; }
        if (k == "region_new") { out = now - at < 1.8 ? "1" : ""; return true; }
        const char* t = last ? ((const char*(__cdecl*)(uint32_t))0x4FFA80)(last) : nullptr;
        out = t && !IsBadReadPtr(t, 1) ? t : "";
        return true;
    }
    if (k == "bank_reward" || k == "bank_reward_on") {
        // the stock HUD's "Bank Reward: $x" (Cash_Bank, script Cash_Animate, 2.7 s; 0x4A9EF0 when the cash HUD gets a
        // reward): here, any rise of the bank while driving in free roam starts it
        static int last = -1, amount = 0; static double at = -100;
        int c = *(int*)0x861E74;
        bool roam = GameMode() == uied::FREEROAM;
        double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
        static int lastFrame = -1; int fr = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
        if (fr != lastFrame) {
            lastFrame = fr;
            if (last >= 0 && c > last && roam) { amount = (now - at < 2.7 ? amount : 0) + (c - last); at = now; }
            last = c;
        }
        bool on = now - at < 2.7;
        if (k == "bank_reward_on") { out = on ? "1" : ""; return true; }
        if (!on) { out.clear(); return true; }
        std::string d = std::to_string(amount), g;
        for (size_t i = 0; i < d.size(); ++i) { if (i && (d.size() - i) % 3 == 0) g += ','; g += d[i]; }
        out = "$" + g; return true;
    }
    if (k == "cash" || k == "cash_raw") {   // the career bank (0x861E74, as the HUD's Cash_data shows it: 0x4A9EF0)
        int c = *(int*)0x861E74;
        if (k == "cash_raw") { snprintf(b, sizeof b, "%d", c); out = b; return true; }
        std::string d = std::to_string(c < 0 ? -c : c), g;
        for (size_t i = 0; i < d.size(); ++i) { if (i && (d.size() - i) % 3 == 0) g += ','; g += d[i]; }
        out = std::string(c < 0 ? "-" : "") + "$" + g; return true;
    }
    if (k == "map_on" || k == "map_north" || k == "map_heading") {   // the minimap's (MapViewNow)
        uied::MapView mv;
        if (!MapViewNow(mv)) { out = k == "map_on" ? "" : "0"; return true; }
        float psi = mv.rotate ? -mv.bearing : 0;
        float deg = k == "map_north" ? psi : mv.bearing + psi;
        deg = fmodf(deg + 540, 360) - 180;
        if (k == "map_on") out = "1"; else { snprintf(b, sizeof b, "%.2f", deg); out = b; }
        return true;
    }
    if (k == "rvm") { out = RvmTexture() ? "@rvm" : ""; return true; }   // texture: the live mirror
    if (k == "rvm_on") { out = RvmTexture() ? "1" : ""; return true; }
    if (CarToken(k, out)) return true;   // {rpm} {gear} {nos}... (game/car.cpp)
    for (REUI_TokenFn fn : g_tokenProviders) {   // other mods' values (the multiplayer mod's race / leaderboard)
        char buf[256] = {};
        if (fn(k.c_str(), buf, sizeof buf)) { out = buf; return true; }
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
    g_uniPad = GetModuleHandleA("NFSU2UniPad.asi") != nullptr;
    uied::tokens = Token;
    uied::gameObject = GameObjectNow;
    uied::mapView = MapViewNow;
    // REUI's own element flags (other mods add theirs through REUI_AddFlag)
    uied::AddFlag("hide_until_green", "hide until green light", "In a race: hidden through the fly-by and the countdown, shown from the green light\n(race status [0x890118]: 2 fly-by, 3 countdown, 4 racing).", 0, false, [] {
        if (*(int*)0x89E7B0 == 0) return false;   // free roam
        int* rs = *(int**)0x890118;
        return !Readable(rs, 4) || *rs < 4;
    });
    for (const HudPiece& p : kHudPieces) if (p.label) uied::AddReplaceTarget(p.id, p.label, p.tip);
    uied::AddReplaceTarget("rvm", "rear-view mirror", "The game's own rear-view mirror (also hidden by itself while an element shows {rvm}).");
    uied::AddFlag("hide_wreck", "hide in a wreck", "Hidden while the game shows a wreck: its crash camera, which slows the world to x0.33\n(World [0x890080] +0x108; the jump camera uses x0.4).", 0, false, [] { return InWreck(); });
    uied::liveTexture = [](const std::string& name, float* su, float* sv) -> IDirect3DTexture9* {
        if (name == "@rvm") {   // the live rear-view mirror ({rvm})
            IDirect3DTexture9* t = RvmTexture();
            if (t) g_rvmDrawnAt = GetTickCount();
            return t;
        }
        uint8_t* ti = TexInfo(name.size() == 9 && name[0] == '#' ? (uint32_t)strtoul(name.c_str() + 1, nullptr, 16) : BHash(name.c_str()));   // "#HHHHHHHH": by hash
        if (name[0] == '#') {   // (diagnostics: each hash once)
            static std::vector<std::string> told;
            if (std::find(told.begin(), told.end(), name) == told.end()) {
                told.push_back(name);
                Log("texture %s: %s%s", name.c_str(), ti ? "info found" : "no info", ti ? (TexOfInfo(ti) ? ", loaded" : ", not a texture yet") : "");
            }
        }
        IDirect3DTexture9* tx = TexOfInfo(ti);
        if (!tx) return nullptr;
        D3DSURFACE_DESC dd;
        int w = *(int16_t*)(ti + 0x44), h = *(int16_t*)(ti + 0x46);
        if (SUCCEEDED(tx->GetLevelDesc(0, &dd)) && dd.Width && dd.Height && w > 0 && h > 0) { *su = (float)w / dd.Width; *sv = (float)h / dd.Height; }
        return tx;
    };
    uied::Init(gameDir, g_scriptsDir + "NFSU2_REUI_layout.txt", [](const char* line) { Log("%s", line); });
    g_ready = true;
}
// Menu pages without UniPad: the menu key opens the start page, arrows / Enter / Esc drive it (UniPad, when installed,
// drives them with the pad through the API instead)
void HostTick(bool focused) {
    if (g_ready) PollDump();
    if (!g_ready || g_uniPad || g_menuDriverExternal) return;
    static bool keyWas = false;
    static LARGE_INTEGER f = {}, last = {};
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!f.QuadPart) { QueryPerformanceFrequency(&f); last = now; }
    double dt = std::min(0.25, (double)(now.QuadPart - last.QuadPart) / (double)f.QuadPart); last = now;
    bool typing = ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
    auto key = [&](int vk) { return focused && !typing && (GetAsyncKeyState(vk) & 0x8000) != 0; };
    bool down = key(g_config.menuKey);
    if (down && !keyWas) { if (uied::MenuIsOpen()) uied::MenuClose(); else if (!uied::MenuOpen()) Log("menu: no menu page yet"); }
    keyWas = down;
    if (uied::MenuIsOpen()) {
        uied::MenuInput in;
        in.up = key(VK_UP); in.down = key(VK_DOWN); in.left = key(VK_LEFT); in.right = key(VK_RIGHT);
        in.accept = key(VK_RETURN) || key(VK_SPACE); in.back = key(VK_ESCAPE) || key(VK_BACK);
        uied::MenuFrame(in, dt);
    }
}
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
    RvmStockPatch((g_rvmDrawnAt && GetTickCount() - g_rvmDrawnAt < 300) || uied::ReplaceWanted("rvm"));
    ApplyHudReplace();
    { std::string t; Token("bank_reward_on", t); }   // watched every frame (a rise during a race / menus isn't a reward)
    WatchCameras();
    { std::string t; Token("region_new", t); Token("trick_on", t); }   // (watched every frame, so a change is caught when it happens)
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
    if (ImGui::CollapsingHeader("Live values")) {
        struct E { const char* k; const char* what; };
        static const E kVals[] = {
            { "fps", "Frames per second." }, { "cash", "Your bank, as $12,345." }, { "cash_raw", "Your bank, just the number." }, { "bank_reward", "The money just added to your bank in free roam ($1,500), for 2.7 s." }, { "bank_reward_on", "1 for those 2.7 s." }, { "region", "The region you're in, as the game's popup words it." }, { "region_new", "1 for 1.8 s after you drive into a new region." },
            { "trick_name", "Style points: the trick (HANGY TIMEY!)." }, { "trick_points", "Its points: +25, or +12x3 with a multiplier." },
            { "trick_on", "1 while a trick is being scored." }, { "trick_fading", "1 for 0.9 s after it ends." }, { "speed", "Speed in km/h (from the car's velocity)." }, { "mph", "Speed in mph." },
            { "speedo", "Speed as the game's speedometer shows it, in the units picked in the game's options." },
            { "speed_unit", "KPH or MPH, following the game's options." }, { "speed_unit_hud", "KM/H or MPH, as the stock speedometer labels it." },
            { "rpm", "Engine RPM." }, { "rpm_k", "RPM in thousands (6.5)." }, { "rpm_max", "The tachometer's full scale." },
            { "rpm_pct", "RPM as 0-100 of the tachometer." }, { "gear", "R, N, 1-7." }, { "reverse", "1 in reverse." },
            { "shift_light", "1 while the game's shift light is on." }, { "throttle", "Throttle pedal, 0-100." },
            { "nos", "All nitrous left: 100 = a full tank, up to 200 with the extra." },
            { "nos_tank", "The main tank, 0-100 (the gauge's first bar)." }, { "nos_extra", "The extra above a full tank, 0-100 (the second bar)." }, { "nos_installed", "1 when the car has nitrous." },
            { "nos_on", "1 while nitrous is firing." },
            { "rvm", "Texture: the live rear-view mirror (the game's mirror camera, already mirror-shaped). While an element\nshows it, the game's own mirror is hidden. Only while the mirror is on (game options)." },
            { "rvm_on", "1 while the game's rear-view mirror is on." },
            { "map_on", "1 while the game shows a minimap (in the world, not in drag races / menus): a minimap's frame shows while it's set." },
            { "map_north", "The minimap's north, degrees clockwise (0 north-up; turns with you heading-up): bind a north arrow's rotation\nto it (range -360..360 -> -360..360) with its pivot on the minimap's centre." },
            { "map_heading", "Your heading on the minimap, degrees clockwise (0 heading-up): for your own player arrow." },
            { "boost", "Turbo boost needle, 0-100 (rests about 55; blank without a turbo)." },
            { "tach_angle", "The stock tach needle's turn, 0-235 degrees (RPM over the dial's full scale)." },
            { "tach_max", "The dial's full scale (7500 / 8500 / 9500 / 10500)." },
            { "tach_fill", "Texture: the car's tach dial (its custom HUD colour)." }, { "tach_lines", "Texture: the RPM numbers ring for this car." },
            { "tach_redline", "Texture: the red arc at this car's redline." }, { "tach_needle", "Texture: the tach needle." },
            { "turbo_fill", "Texture: the boost dial." }, { "turbo_lines", "Texture: the boost numbers." }, { "turbo_needle", "Texture: the boost needle." },
            { "hud_color", "The car's custom HUD colour, 00-10." },
            { "engage_phone", "1 while the Cingular panel shows the (pulsing) phone: a new message or an event in reach." },
            { "engage_event", "1 while an event / shop is in reach (its icon blinks, D-pad up pulses)." }, { "engage_icon", "Texture: that event's icon." },
            { "sms_new", "1 while a text message has just arrived." }, { "sms_waiting", "1 while unread messages wait (envelope + count)." },
            { "sms_any", "1 for either." }, { "sms_icon", "Texture: the newest unread message's envelope icon." }, { "sms_color", "Its colour (R G B) - Rachel, races, shops... each have their own." }, { "sms_count", "How many messages (for x3)." }, { "engage_state", "The panel's slot state (2/3 new, 4 waiting)." }, { "engage_type", "Its type (19 = text message)." },
            { "tach_paint_1", "Custom HUD paint 1 (R G B): the dial, the boost dial." }, { "tach_paint_2", "Paint 2: the needles." }, { "tach_paint_3", "Paint 3: the RPM and boost numbers." }, { "gear_grey", "1 when the stock tach shows the gear grey (else orange)." }, { "turbo", "1 when the car has a turbo." },
        };
        ImGui::TextDisabled("Put {key} in a text element. Click a key to copy it. Other mods add theirs (the multiplayer mod: F8 > Live values).");
        if (ImGui::BeginTable("##reuivals", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("What it shows");
            ImGui::TableHeadersRow();
            for (const E& e : kVals) {
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                std::string key = std::string("{") + e.k + "}";
                if (ImGui::Selectable(key.c_str())) ImGui::SetClipboardText(key.c_str());
                ImGui::TableNextColumn();
                std::string v; Token(e.k, v);
                if (v.empty()) ImGui::TextDisabled("(blank)"); else ImGui::TextUnformatted(v.c_str());
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s", e.what);
            }
            ImGui::EndTable();
        }
    }
    uied::Panel();
}
void HostRelease() { if (g_ready) uied::Release(); }

} // namespace reui
