// See gfx_rex.h.
#include "gfx_rex.h"

#include <rex/ui/immediate_drawer.h>

#include <cstring>
#include <deque>
#include <memory>
#include <vector>

namespace gfx {

struct Texture { std::unique_ptr<::rex::ui::ImmediateTexture> t; };

namespace {
::rex::ui::ImmediateDrawer* g_drawer = nullptr;
uint64_t g_frame = 0;
std::vector<Texture*> g_later;                              // ReleaseLater'd this frame
std::deque<std::pair<uint64_t, Texture*>> g_dying;          // (still possibly in flight on the GPU)

uint32_t Rgb565(uint16_t c) {   // -> 0xAABBGGRR-ordered bytes R G B A (alpha 255)
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
    return r | (g << 8) | (b << 16) | 0xFF000000u;
}
// a DXT1/3/5 block's 16 colours (RGBA bytes), DXT1 alpha from the colour mode
void DecodeColours(const uint8_t* b, uint32_t out[16], bool dxt1) {
    uint16_t c0, c1; memcpy(&c0, b, 2); memcpy(&c1, b + 2, 2);
    uint32_t p[4]; p[0] = Rgb565(c0); p[1] = Rgb565(c1);
    auto mix = [](uint32_t a, uint32_t c, int wa, int wc, int d) {
        uint32_t o = 0xFF000000u;
        for (int s = 0; s < 24; s += 8) o |= ((((a >> s) & 255) * wa + ((c >> s) & 255) * wc) / d) << s;
        return o;
    };
    if (!dxt1 || c0 > c1) { p[2] = mix(p[0], p[1], 2, 1, 3); p[3] = mix(p[0], p[1], 1, 2, 3); }
    else { p[2] = mix(p[0], p[1], 1, 1, 2); p[3] = 0; }
    uint32_t idx; memcpy(&idx, b + 4, 4);
    for (int i = 0; i < 16; ++i) out[i] = p[(idx >> (2 * i)) & 3];
}
std::vector<uint8_t> ToRgba(int w, int h, Format fmt, const uint8_t* data, int pitch) {
    std::vector<uint8_t> o((size_t)w * h * 4);
    if (fmt == BGRA8) {
        for (int y = 0; y < h; ++y) {
            const uint8_t* s = data + (size_t)y * pitch; uint8_t* d = &o[(size_t)y * w * 4];
            for (int x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
        }
        return o;
    }
    const int bw = (w + 3) / 4, bh = (h + 3) / 4, bs = fmt == DXT1 ? 8 : 16;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* b = data + (size_t)by * pitch + (size_t)bx * bs;
            uint32_t c[16];
            DecodeColours(fmt == DXT1 ? b : b + 8, c, fmt == DXT1);
            if (fmt == DXT3) {
                for (int i = 0; i < 16; ++i) { uint32_t a = (b[i / 2] >> ((i & 1) * 4)) & 15; c[i] = (c[i] & 0xFFFFFFu) | ((a * 17) << 24); }
            } else if (fmt == DXT5) {
                uint32_t a[8]; a[0] = b[0]; a[1] = b[1];
                if (a[0] > a[1]) for (int i = 1; i < 7; ++i) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
                else { for (int i = 1; i < 5; ++i) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5; a[6] = 0; a[7] = 255; }
                uint64_t bits = 0; for (int i = 0; i < 6; ++i) bits |= (uint64_t)b[2 + i] << (8 * i);
                for (int i = 0; i < 16; ++i) c[i] = (c[i] & 0xFFFFFFu) | (a[(bits >> (3 * i)) & 7] << 24);
            }
            for (int py = 0; py < 4; ++py)
                for (int px = 0; px < 4; ++px) {
                    int x = bx * 4 + px, y = by * 4 + py;
                    if (x < w && y < h) memcpy(&o[((size_t)y * w + x) * 4], &c[py * 4 + px], 4);
                }
        }
    return o;
}
void Flush(uint64_t upTo) {
    while (!g_dying.empty() && g_dying.front().first + 4 <= upTo) { delete g_dying.front().second; g_dying.pop_front(); }
}
} // namespace

namespace rex {
void SetDrawer(::rex::ui::ImmediateDrawer* drawer) { g_drawer = drawer; }
void FrameStart(uint64_t frame) { g_frame = frame; }
void ReleaseEverything() {
    for (Texture* t : g_later) delete t;
    g_later.clear();
    for (auto& d : g_dying) delete d.second;
    g_dying.clear();
}
} // namespace rex

bool Ready() { return g_drawer != nullptr; }
Tex Create(int w, int h, Format fmt, const void* data, int pitch) {
    if (!g_drawer || w <= 0 || h <= 0 || !data) return nullptr;
    std::vector<uint8_t> rgba = ToRgba(w, h, fmt, (const uint8_t*)data, pitch);
    auto t = g_drawer->CreateTexture((uint32_t)w, (uint32_t)h, ::rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
    if (!t) return nullptr;
    Texture* tex = new Texture;
    tex->t = std::move(t);
    return tex;
}
void Release(Tex t) { delete t; }
void ReleaseLater(Tex t) { if (t) g_later.push_back(t); }
void FrameStart() {   // the core's frame start: what was let go last frame waits out the frames in flight
    for (Texture* t : g_later) g_dying.emplace_back(g_frame, t);
    g_later.clear();
    Flush(g_frame);
}
void ReleaseAll() { rex::ReleaseEverything(); }
ImTextureID Id(Tex t) { return t ? (ImTextureID)(uintptr_t)t->t.get() : (ImTextureID)0; }

void CbAdditive(const ImDrawList*, const ImDrawCmd*) {}
void CbNoAlpha(const ImDrawList*, const ImDrawCmd*) {}
void CbMapSamplingOn(const ImDrawList*, const ImDrawCmd*) {}
void CbMapSamplingOff(const ImDrawList*, const ImDrawCmd*) {}

} // namespace gfx
