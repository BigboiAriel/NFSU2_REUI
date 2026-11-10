#include "shapes.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>

namespace shapes {

namespace {

struct Entry { gfx::Tex fill = nullptr; gfx::Tex stroke = nullptr; float margin = 0; int pw = 0, ph = 0; float ox = 0, oy = 0, hw = 0, hh = 0; DWORD used = 0; size_t bytes = 0; };
std::unordered_map<std::string, Entry> g_cache;
// A shape that animates (pulse, keyframes) or is being resized is a new size - a new entry - every frame: kept to a
// budget, the least recently drawn let go first (code review, 2026-11-03: a pulsing 600x300 panel made ~260 MB).
constexpr size_t kMaxBytes = 128u << 20; constexpr size_t kMaxEntries = 192;
// kept this long after last drawn (a menu's panels: made again when it reopens would hitch the game - each is computed
// pixel by pixel at screen size); the byte / entry budget lets the least recently drawn go first meanwhile
constexpr DWORD kKeepMs = 10 * 60 * 1000;
size_t g_bytes = 0;
void Drop(std::unordered_map<std::string, Entry>::iterator it) {
    gfx::ReleaseLater(it->second.fill); gfx::ReleaseLater(it->second.stroke);
    g_bytes -= std::min(g_bytes, it->second.bytes); g_cache.erase(it);
}
void MakeRoom(size_t need) {
    while (!g_cache.empty() && (g_bytes + need > kMaxBytes || g_cache.size() >= kMaxEntries)) {
        auto old = g_cache.begin();
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it) if (it->second.used < old->second.used) old = it;
        Drop(old);
    }
}
const float kPi = 3.14159265f;

// signed distance to a rounded box centred at 0 (half size b), corner radius by quadrant (y down)
inline float SdRoundBox(float px, float py, float bx, float by, const float r[4]) {
    float rr = px > 0 ? (py > 0 ? r[2] : r[1]) : (py > 0 ? r[3] : r[0]);
    rr = std::max(0.f, std::min(rr, std::min(bx, by)));
    float qx = fabsf(px) - bx + rr, qy = fabsf(py) - by + rr;
    float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::min(std::max(qx, qy), 0.f) + sqrtf(ox * ox + oy * oy) - rr;
}
inline float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Where on the outline the point is nearest, 0..1 clockwise (screen) from the top edge's left end: the outline is
// 4 straight edges and 4 quarter arcs (corner radii clamped like the distance field's).
struct Outline {
    float bx, by, r[4], seg[8], total;
    Outline(float bx_, float by_, const float rr[4]) : bx(bx_), by(by_) {
        for (int i = 0; i < 4; ++i) r[i] = std::max(0.f, std::min(rr[i], std::min(bx, by)));
        seg[0] = std::max(0.f, 2 * bx - r[0] - r[1]); seg[1] = kPi / 2 * r[1];   // top, top-right arc
        seg[2] = std::max(0.f, 2 * by - r[1] - r[2]); seg[3] = kPi / 2 * r[2];   // right, bottom-right arc
        seg[4] = std::max(0.f, 2 * bx - r[2] - r[3]); seg[5] = kPi / 2 * r[3];   // bottom, bottom-left arc
        seg[6] = std::max(0.f, 2 * by - r[3] - r[0]); seg[7] = kPi / 2 * r[0];   // left, top-left arc
        total = 0; for (float s : seg) total += s;
        if (total < 1e-3f) total = 1e-3f;
    }
    float T(float px, float py) const {
        float best = 1e30f, at = 0, off = 0;
        auto line = [&](int i, float ax, float ay, float ex, float ey) {   // segment a -> e
            float dx = ex - ax, dy = ey - ay, l2 = dx * dx + dy * dy;
            float u = l2 > 1e-6f ? Clamp01(((px - ax) * dx + (py - ay) * dy) / l2) : 0;
            float qx = ax + u * dx - px, qy = ay + u * dy - py, d = qx * qx + qy * qy;
            if (d < best) { best = d; at = off + u * seg[i]; }
        };
        auto arc = [&](int i, float cx, float cy, float rr, float a0) {   // quarter arc from angle a0 (clockwise)
            if (rr <= 0) return;
            float a = atan2f(py - cy, px - cx) - a0;
            while (a < -kPi) a += 2 * kPi;
            while (a > kPi) a -= 2 * kPi;
            float u = Clamp01(a / (kPi / 2));
            float qa = a0 + u * kPi / 2, qx = cx + cosf(qa) * rr - px, qy = cy + sinf(qa) * rr - py, d = qx * qx + qy * qy;
            if (d < best) { best = d; at = off + u * seg[i]; }
        };
        line(0, -bx + r[0], -by, bx - r[1], -by); off += seg[0];
        arc(1, bx - r[1], -by + r[1], r[1], -kPi / 2); off += seg[1];
        line(2, bx, -by + r[1], bx, by - r[2]); off += seg[2];
        arc(3, bx - r[2], by - r[2], r[2], 0); off += seg[3];
        line(4, bx - r[2], by, -bx + r[3], by); off += seg[4];
        arc(5, -bx + r[3], by - r[3], r[3], kPi / 2); off += seg[5];
        line(6, -bx, by - r[3], -bx, -by + r[0]); off += seg[6];
        arc(7, -bx + r[0], -by + r[0], r[0], kPi);
        return at / total;
    }
};

// the gradient's colour at t (0..1), straight lines between stops, held past the ends
void Sample(const Gradient& g, float t, float out[4]) {
    const auto& s = g.stops;
    if (s.empty()) { out[0] = out[1] = out[2] = out[3] = 1; return; }
    if (t <= s.front()[0]) { for (int i = 0; i < 4; ++i) out[i] = s.front()[1 + i]; return; }
    if (t >= s.back()[0]) { for (int i = 0; i < 4; ++i) out[i] = s.back()[1 + i]; return; }
    for (size_t j = 0; j + 1 < s.size(); ++j)
        if (t >= s[j][0] && t <= s[j + 1][0]) {
            float span = s[j + 1][0] - s[j][0], f = span > 1e-6f ? (t - s[j][0]) / span : 0;
            for (int i = 0; i < 4; ++i) out[i] = s[j][1 + i] + (s[j + 1][1 + i] - s[j][1 + i]) * f;
            return;
        }
    for (int i = 0; i < 4; ++i) out[i] = s.back()[1 + i];
}
inline uint32_t Argb(const float c[4], float cover) {
    auto b = [](float v) { return (uint32_t)lroundf(Clamp01(v) * 255); };
    return (b(c[3] * cover) << 24) | (b(c[0]) << 16) | (b(c[1]) << 8) | b(c[2]);
}

std::string Key(const Box& s, float stroke, int align, const Gradient& sg, const Gradient& fg, const std::vector<Box>& cuts, const std::vector<Box>& merges) {
    char b[160]; std::string k;
    auto put = [&](const Box& x) {
        snprintf(b, sizeof b, "%.2f %.2f %.2f %.2f %.4f %.2f %.2f %.2f %.2f %.2f|", x.cx, x.cy, x.w, x.h, x.rot, x.r[0], x.r[1], x.r[2], x.r[3], x.gap);
        k += b;
    };
    auto grad = [&](const Gradient& g) {
        snprintf(b, sizeof b, "g%d %.2f %.2f:", g.mode, g.angle, g.depth); k += b;
        if (g.mode) for (auto& st : g.stops) { snprintf(b, sizeof b, "%.3f %.3f %.3f %.3f %.3f,", st[0], st[1], st[2], st[3], st[4]); k += b; }
        k += '|';
    };
    snprintf(b, sizeof b, "%.2f %d|", stroke, align); k += b;
    grad(sg); grad(fg);
    put(s); for (auto& c : cuts) put(c);
    if (!merges.empty()) { k += 'm'; for (auto& m : merges) put(m); }
    return k;
}

gfx::Tex Upload(const std::vector<uint32_t>& px, int w, int h) {   // (0xAARRGGBB: B G R A in memory)
    return gfx::Create(w, h, gfx::BGRA8, px.data(), w * 4);
}

} // namespace

bool Get(const Box& s, float stroke, int align, const Gradient& sg, const Gradient& fg, const std::vector<Box>& cuts, Result& out) {
    static const std::vector<Box> none;
    return Get(s, stroke, align, sg, fg, cuts, none, out);
}
bool Get(const Box& s, float stroke, int align, const Gradient& sg, const Gradient& fg, const std::vector<Box>& cuts, const std::vector<Box>& merges, Result& out) {
    // (not a number / infinite / absurd: "s.w < 1" lets NaN through and (int)ceilf(inf) wraps past the 2048 clamp)
    if (!gfx::Ready() || !std::isfinite(s.w) || !std::isfinite(s.h) || !std::isfinite(stroke) || s.w < 1 || s.h < 1 || s.w > 1e5f || s.h > 1e5f) return false;
    float sw = std::max(0.f, std::min(stroke, 1e4f));
    if (align < ALIGN_INSIDE || align > ALIGN_OUTSIDE) align = ALIGN_INSIDE;
    std::string key = Key(s, sw, align, sg, fg, cuts, merges);
    DWORD now = GetTickCount();
    auto it = g_cache.find(key);
    if (it != g_cache.end()) {
        it->second.used = now;
        out.fill = it->second.fill; out.stroke = it->second.stroke; out.margin = it->second.margin; out.pw = it->second.pw; out.ph = it->second.ph;
        out.ox = it->second.ox; out.oy = it->second.oy; out.hw = it->second.hw; out.hh = it->second.hh;
        return out.fill || out.stroke;
    }
    // the box around the shape and what's merged into it (shape frame): the textures cover it
    float bx = s.w / 2, by = s.h / 2;
    float ox, oy, hw, hh; UnionBox(s, merges, ox, oy, hw, hh);
    if (hw > 5e4f || hh > 5e4f) return false;
    // the stroke band, in distance from the edge (negative inside): inside [-sw, 0], centre [-sw/2, sw/2], outside [0, sw];
    // the fill goes all the way to the path (the edge itself), under the stroke - as drawing programs do
    float lo = align == ALIGN_INSIDE ? -sw : align == ALIGN_CENTRE ? -sw / 2 : 0.f;
    float hi = lo + sw;
    float maxHi = std::max(0.f, hi);
    // the shape's box plus what sticks out (the stroke past the edge) and a pixel for the soft edge; at most 2048 a side
    float margin = 1.5f + maxHi;
    int pw = (int)ceilf(2 * hw + 2 * margin), ph = (int)ceilf(2 * hh + 2 * margin);
    float k = 1.f;
    if (pw > 2048 || ph > 2048) { k = 2048.f / std::max(pw, ph); pw = std::min(2048, (int)ceilf(pw * k)); ph = std::min(2048, (int)ceilf(ph * k)); }
    std::vector<uint32_t> fpx((size_t)pw * ph, 0x00FFFFFF), spx((size_t)pw * ph, 0x00FFFFFF);
    struct C { float cx, cy, cs, sn, bx, by, gap; float r[4]; };
    std::vector<C> cs, ms;
    auto prep = [](const Box& c) {
        C x; x.cx = c.cx; x.cy = c.cy; x.cs = cosf(-c.rot); x.sn = sinf(-c.rot); x.bx = c.w / 2; x.by = c.h / 2; x.gap = c.gap;
        memcpy(x.r, c.r, sizeof x.r);
        return x;
    };
    for (auto& c : cuts) cs.push_back(prep(c));
    for (auto& m : merges) ms.push_back(prep(m));
    auto sdOf = [](const C& c, float px, float py, const float* rr) {   // (a cutter's / merged shape's distance, grown by its gap)
        float lx = px - c.cx, ly = py - c.cy;
        float rx = lx * c.cs - ly * c.sn, ry = lx * c.sn + ly * c.cs;
        return SdRoundBox(rx, ry, c.bx, c.by, rr ? rr : c.r) - c.gap;
    };
    Outline outline(bx, by, s.r);
    bool sGrad = sg.mode && !sg.stops.empty(), fGrad = fg.mode && !fg.stops.empty();
    float la = fg.angle * kPi / 180, ldx = cosf(la), ldy = sinf(la);
    float lext = std::max(1e-3f, fabsf(ldx) * hw + fabsf(ldy) * hh);   // the shape's half length along the linear gradient (merged: all of it)
    const float white[4] = {1, 1, 1, 1};
    float aa = 0.5f / k;   // a pixel is 1/k shape pixels: the soft edge is one screen pixel wide
    // rows split over the CPU's cores (each pixel is its own; a big panel at 1440p is over a million of them)
    auto rows = [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < pw; ++x) {
            float px = (x + 0.5f) / k - margin - hw + ox, py = (y + 0.5f) / k - margin - hh + oy;   // from the centre, shape pixels
            float d = SdRoundBox(px, py, bx, by, s.r);
            for (auto& m : ms) d = std::min(d, sdOf(m, px, py, nullptr));   // A + B: min(dA, dB)
            if (d > maxHi + 1.f) continue;
            bool cutEdge = false;   // the nearest edge here is a cut's (no stroke of the shape's there)
            float dcut = -1e9f;
            for (auto& c : cs) {   // A minus B: max(dA, -dB), B grown by its gap
                float dc = -sdOf(c, px, py, nullptr);
                dcut = std::max(dcut, dc);
                if (dc > d) { d = dc; cutEdge = true; }
            }
            float outer = Clamp01((aa - (d - hi)) * k);   // inside the band's outer side
            float inner = Clamp01((aa - (d - lo)) * k);   // inside its inner side
            float fillCov = Clamp01((aa - d) * k);        // inside the path
            size_t i = (size_t)y * pw + x;
            if (fillCov > 0) {
                float c[4];
                if (!fGrad) memcpy(c, white, sizeof c);
                else {
                    float t = 0;
                    float gx = px - ox, gy = py - oy;   // (from the middle of it all: the shape's own, or with what's merged)
                    if (fg.mode == 1) t = ((gx * ldx + gy * ldy) / lext + 1) / 2;
                    else if (fg.mode == 2) { float u = gx / std::max(1e-3f, hw), v = gy / std::max(1e-3f, hh); t = sqrtf(u * u + v * v); }
                    else {   // from the path inward: its corners rounded to 1.5x the depth at least (no creases from them)
                        float rg[4]; for (int q = 0; q < 4; ++q) rg[q] = std::max(s.r[q], 1.5f * fg.depth);
                        float du = SdRoundBox(px, py, bx, by, rg);
                        for (auto& m : ms) { float mr[4]; for (int q = 0; q < 4; ++q) mr[q] = std::max(m.r[q], 1.5f * fg.depth); du = std::min(du, sdOf(m, px, py, mr)); }
                        float dg = std::max(du, dcut);
                        t = fg.depth > 0 ? -dg / fg.depth : 1;
                    }
                    Sample(fg, Clamp01(t), c);
                }
                fpx[i] = Argb(c, fillCov);
            }
            if (cutEdge) continue;   // the cutter draws its own border there
            float band = sw > 0 ? std::max(0.f, outer - inner) : 0.f;
            if (band > 0) {
                float c[4];
                if (!sGrad) memcpy(c, white, sizeof c);
                else {
                    float t;
                    if (sg.mode == 1) t = (hi - d) / std::max(1e-3f, sw);   // across: outer side 0 -> inner side 1
                    else { t = outline.T(px, py) + sg.angle / 360.f; t -= floorf(t); }   // along, from the start offset
                    Sample(sg, Clamp01(t), c);
                }
                spx[i] = Argb(c, band);
            }
        }
    };
    int nt = (int)std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    if ((size_t)pw * ph < 40000) nt = 1;   // (small: not worth the threads)
    if (nt == 1) rows(0, ph);
    else {
        std::vector<std::thread> th;
        int per = (ph + nt - 1) / nt;
        for (int t = 1; t < nt; ++t) { int a = t * per, b = std::min(ph, a + per); if (a < b) th.emplace_back(rows, a, b); }
        rows(0, std::min(ph, per));
        for (auto& t : th) t.join();
    }
    Entry e; e.margin = margin; e.pw = pw; e.ph = ph; e.used = now; e.ox = ox; e.oy = oy; e.hw = hw; e.hh = hh;
    e.bytes = (size_t)pw * ph * 4 * (sw > 0 ? 2 : 1);
    MakeRoom(e.bytes);
    e.fill = Upload(fpx, pw, ph);
    if (sw > 0) e.stroke = Upload(spx, pw, ph);
    g_cache[key] = e; g_bytes += e.bytes;
    out.fill = e.fill; out.stroke = e.stroke; out.margin = margin; out.pw = pw; out.ph = ph;
    out.ox = ox; out.oy = oy; out.hw = hw; out.hh = hh;
    return out.fill || out.stroke;
}

void UnionBox(const Box& s, const std::vector<Box>& merges, float& ox, float& oy, float& hw, float& hh) {
    float x0 = -s.w / 2, x1 = s.w / 2, y0 = -s.h / 2, y1 = s.h / 2;
    for (auto& m : merges) {
        if (!std::isfinite(m.w) || !std::isfinite(m.h) || !std::isfinite(m.cx) || !std::isfinite(m.cy)) continue;
        float c = fabsf(cosf(m.rot)), sn = fabsf(sinf(m.rot)), g = std::max(0.f, m.gap);
        float ex = c * m.w / 2 + sn * m.h / 2 + g, ey = sn * m.w / 2 + c * m.h / 2 + g;
        x0 = std::min(x0, m.cx - ex); x1 = std::max(x1, m.cx + ex); y0 = std::min(y0, m.cy - ey); y1 = std::max(y1, m.cy + ey);
    }
    ox = (x0 + x1) / 2; oy = (y0 + y1) / 2; hw = (x1 - x0) / 2; hh = (y1 - y0) / 2;
}

void Trim() {
    DWORD now = GetTickCount();
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (now - it->second.used > kKeepMs) { auto d = it++; Drop(d); }
        else ++it;
    }
}

void Release() {
    for (auto& kv : g_cache) { gfx::Release(kv.second.fill); gfx::Release(kv.second.stroke); }
    g_cache.clear(); g_bytes = 0;
}

} // namespace shapes
