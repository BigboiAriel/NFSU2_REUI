#include "shapes.h"
#include "deferfree.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace shapes {

namespace {

struct Entry { IDirect3DTexture9* fill = nullptr; IDirect3DTexture9* stroke = nullptr; float margin = 0; int pw = 0, ph = 0; DWORD used = 0; };
std::unordered_map<std::string, Entry> g_cache;

// signed distance to a rounded box centred at 0 (half size b), corner radius by quadrant (y down)
inline float SdRoundBox(float px, float py, float bx, float by, const float r[4]) {
    float rr = px > 0 ? (py > 0 ? r[2] : r[1]) : (py > 0 ? r[3] : r[0]);
    rr = std::max(0.f, std::min(rr, std::min(bx, by)));
    float qx = fabsf(px) - bx + rr, qy = fabsf(py) - by + rr;
    float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::min(std::max(qx, qy), 0.f) + sqrtf(ox * ox + oy * oy) - rr;
}
inline float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

std::string Key(const Box& s, float stroke, const std::vector<Box>& cuts) {
    char b[160]; std::string k;
    auto put = [&](const Box& x) {
        snprintf(b, sizeof b, "%.2f %.2f %.2f %.2f %.4f %.2f %.2f %.2f %.2f %.2f|", x.cx, x.cy, x.w, x.h, x.rot, x.r[0], x.r[1], x.r[2], x.r[3], x.gap);
        k += b;
    };
    snprintf(b, sizeof b, "%.2f|", stroke); k += b;
    put(s); for (auto& c : cuts) put(c);
    return k;
}

IDirect3DTexture9* Upload(IDirect3DDevice9* dev, const std::vector<uint8_t>& a, int w, int h) {
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr)) || !t) return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(t->LockRect(0, &lr, nullptr, 0))) { t->Release(); return nullptr; }
    for (int y = 0; y < h; ++y) {
        uint32_t* d = (uint32_t*)((uint8_t*)lr.pBits + (size_t)y * lr.Pitch);
        const uint8_t* s = &a[(size_t)y * w];
        for (int x = 0; x < w; ++x) d[x] = ((uint32_t)s[x] << 24) | 0x00FFFFFF;
    }
    t->UnlockRect(0);
    return t;
}

} // namespace

bool Get(IDirect3DDevice9* dev, const Box& s, float stroke, const std::vector<Box>& cuts, Result& out) {
    if (!dev || s.w < 1 || s.h < 1) return false;
    std::string key = Key(s, stroke, cuts);
    DWORD now = GetTickCount();
    auto it = g_cache.find(key);
    if (it != g_cache.end()) {
        it->second.used = now;
        out.fill = it->second.fill; out.stroke = it->second.stroke; out.margin = it->second.margin; out.pw = it->second.pw; out.ph = it->second.ph;
        return out.fill || out.stroke;
    }
    // the shape's box plus a pixel for the soft edge; at most 2048 on a side (bigger is drawn stretched)
    float margin = 1.5f;
    int pw = (int)ceilf(s.w + 2 * margin), ph = (int)ceilf(s.h + 2 * margin);
    float k = 1.f;
    if (pw > 2048 || ph > 2048) { k = 2048.f / std::max(pw, ph); pw = std::min(2048, (int)ceilf(pw * k)); ph = std::min(2048, (int)ceilf(ph * k)); }
    std::vector<uint8_t> fa((size_t)pw * ph), sa((size_t)pw * ph);
    float bx = s.w / 2, by = s.h / 2, sw = std::max(0.f, stroke);
    // cutters in the shape's frame: their centre and turn relative to it
    struct C { float cx, cy, cs, sn, bx, by, gap; float r[4]; };
    std::vector<C> cs;
    for (auto& c : cuts) {
        C x; x.cx = c.cx; x.cy = c.cy; x.cs = cosf(-c.rot); x.sn = sinf(-c.rot); x.bx = c.w / 2; x.by = c.h / 2; x.gap = c.gap;
        memcpy(x.r, c.r, sizeof x.r);
        cs.push_back(x);
    }
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x) {
            float px = (x + 0.5f) / k - margin - bx, py = (y + 0.5f) / k - margin - by;   // from the centre, shape pixels
            float d = SdRoundBox(px, py, bx, by, s.r);
            if (d > 1.f) continue;
            for (auto& c : cs) {   // A minus B: max(dA, -dB), B grown by its gap
                float lx = px - c.cx, ly = py - c.cy;
                float rx = lx * c.cs - ly * c.sn, ry = lx * c.sn + ly * c.cs;
                d = std::max(d, -(SdRoundBox(rx, ry, c.bx, c.by, c.r) - c.gap));
            }
            // a pixel is 1/k shape pixels: the soft edge is one screen pixel wide
            float aa = 0.5f / k;
            float inside = Clamp01((aa - d) * k);                             // in the shape
            float inner = sw > 0 ? Clamp01((aa - (d + sw)) * k) : inside;     // inside the stroke band
            size_t i = (size_t)y * pw + x;
            fa[i] = (uint8_t)lroundf(inner * 255);
            sa[i] = (uint8_t)lroundf(std::max(0.f, inside - inner) * 255);
        }
    Entry e; e.margin = margin; e.pw = pw; e.ph = ph; e.used = now;
    e.fill = Upload(dev, fa, pw, ph);
    if (sw > 0) e.stroke = Upload(dev, sa, pw, ph);
    g_cache[key] = e;
    out.fill = e.fill; out.stroke = e.stroke; out.margin = margin; out.pw = pw; out.ph = ph;
    return out.fill || out.stroke;
}

void Trim() {
    DWORD now = GetTickCount();
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (now - it->second.used > 3000) { deferfree::Later(it->second.fill); deferfree::Later(it->second.stroke); it = g_cache.erase(it); }
        else ++it;
    }
}

void Release() {
    for (auto& kv : g_cache) { if (kv.second.fill) kv.second.fill->Release(); if (kv.second.stroke) kv.second.stroke->Release(); }
    g_cache.clear();
}

} // namespace shapes
