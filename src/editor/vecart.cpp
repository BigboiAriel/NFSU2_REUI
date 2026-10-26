#include "vecart.h"
#include "deferfree.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace vecart {

namespace {

std::string g_dir;
std::vector<Art> g_list;
bool g_scanned = false;

struct Made { std::string name; int px; IDirect3DTexture9* tex; DWORD used; };
std::vector<Made> g_made;
struct Stamp { std::string name; FILETIME mt; DWORD checked; };
std::vector<Stamp> g_stamps;

std::string PathOf(const std::string& name) {
    std::string p = g_dir + name + ".svg";
    for (char& c : p) if (c == '/') c = '\\';
    return p;
}
bool MTime(const std::string& path, FILETIME& mt) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa)) return false;
    mt = fa.ftLastWriteTime;
    return true;
}
NSVGimage* Parse(const std::string& name) { return nsvgParseFromFile(PathOf(name).c_str(), "px", 96.0f); }

void Walk(const std::string& rel, int depth) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((g_dir + rel + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 4) Walk(rel + n + "\\", depth + 1); continue; }
        if (n.size() < 5 || _stricmp(n.c_str() + n.size() - 4, ".svg") != 0) continue;
        Art a; a.name = rel + n.substr(0, n.size() - 4);
        for (char& c : a.name) if (c == '\\') c = '/';
        if (NSVGimage* img = Parse(a.name)) { a.w = img->width; a.h = img->height; nsvgDelete(img); }
        g_list.push_back(a);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void Drop(const std::string& name) {   // every size made of one art
    for (size_t i = 0; i < g_made.size();)
        if (g_made[i].name == name) { deferfree::Later(g_made[i].tex); g_made.erase(g_made.begin() + i); }
        else ++i;
}

// redrawn when its file changes (checked about once a second)
void CheckFile(const std::string& name) {
    DWORD now = GetTickCount();
    for (auto& s : g_stamps) {
        if (s.name != name) continue;
        if (now - s.checked < 1000) return;
        s.checked = now;
        FILETIME mt;
        if (MTime(PathOf(name), mt) && CompareFileTime(&mt, &s.mt) != 0) {
            s.mt = mt; Drop(name);
            for (auto& a : g_list) if (a.name == name) if (NSVGimage* img = Parse(name)) { a.w = img->width; a.h = img->height; nsvgDelete(img); }
        }
        return;
    }
    Stamp s; s.name = name; s.checked = now;
    if (!MTime(PathOf(name), s.mt)) memset(&s.mt, 0, sizeof s.mt);
    g_stamps.push_back(s);
}

IDirect3DTexture9* Rasterize(IDirect3DDevice9* dev, const std::string& name, int px) {
    NSVGimage* img = Parse(name);
    if (!img) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    if (img->width > 0 && img->height > 0) {
        float scale = px / std::max(img->width, img->height);
        int w = std::max(1, std::min(2048, (int)ceilf(img->width * scale)));
        int h = std::max(1, std::min(2048, (int)ceilf(img->height * scale)));
        std::vector<unsigned char> rgba((size_t)w * h * 4);
        if (NSVGrasterizer* r = nsvgCreateRasterizer()) {
            nsvgRasterize(r, img, 0, 0, scale, rgba.data(), w, h, w * 4);
            nsvgDeleteRasterizer(r);
            if (SUCCEEDED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) {
                D3DLOCKED_RECT lr;
                if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
                    for (int y = 0; y < h; ++y) {
                        const unsigned char* s = rgba.data() + (size_t)y * w * 4;
                        unsigned char* d = (unsigned char*)lr.pBits + (size_t)y * lr.Pitch;
                        for (int x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                    }
                    tex->UnlockRect(0);
                } else { tex->Release(); tex = nullptr; }
            }
        }
    }
    nsvgDelete(img);
    return tex;
}

} // namespace

void SetDir(const std::string& dir) {
    g_dir = dir;
    if (!g_dir.empty() && g_dir.back() != '\\') g_dir += '\\';
    CreateDirectoryA(g_dir.c_str(), nullptr);
    g_scanned = false;
}
const std::string& Dir() { return g_dir; }

void Rescan() {
    g_list.clear();
    if (!g_dir.empty()) Walk("", 0);
    std::sort(g_list.begin(), g_list.end(), [](const Art& a, const Art& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    for (auto& m : g_made) deferfree::Later(m.tex);
    g_made.clear(); g_stamps.clear();
    g_scanned = true;
}
const std::vector<Art>& List() { if (!g_scanned) Rescan(); return g_list; }
const Art* Find(const std::string& name) {
    for (auto& a : List()) if (a.name == name) return &a;
    return nullptr;
}

IDirect3DTexture9* Texture(IDirect3DDevice9* dev, const std::string& name, int px) {
    if (!dev || name.empty()) return nullptr;
    CheckFile(name);
    int bucket = std::max(32, std::min(2048, (px + 31) / 32 * 32));   // redrawn when the size changes by a step
    DWORD now = GetTickCount();
    for (auto& m : g_made) if (m.name == name && m.px == bucket) { m.used = now; return m.tex; }
    Made m; m.name = name; m.px = bucket; m.used = now; m.tex = Rasterize(dev, name, bucket);
    g_made.push_back(m);   // (a failed one is kept as nullptr: not retried every frame until the file changes)
    // drop sizes not used for a while
    for (size_t i = 0; i < g_made.size();)
        if (now - g_made[i].used > 10000) { deferfree::Later(g_made[i].tex); g_made.erase(g_made.begin() + i); }
        else ++i;
    return m.tex;
}

void Release() {   // device going away: nothing is being drawn, released now
    for (auto& m : g_made) if (m.tex) m.tex->Release();
    g_made.clear();
    deferfree::Flush();
}

} // namespace vecart
