// See swf_fonts.h. A Flash DefineFont3 glyph is a shape of quadratic curves on a 20480-unit em (1024 x 20, y down);
// a TrueType glyph is quadratic too, so its contours carry over point for point: on 1024 units, y up, each contour
// turned the TrueType way (outer clockwise, holes the other way - by how deep it sits in the others, as Flash fills).
#include "swf_fonts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace swf_fonts {
namespace {

// ---------------------------------------------------------------- inflate (RFC 1951; zlib wrapper RFC 1950)

struct BitIn {
    const uint8_t* p; size_t n, i = 0; uint64_t buf = 0; int cnt = 0; bool over = false;
    uint32_t bits(int k) {
        while (cnt < k) { uint64_t b = 0; if (i < n) b = p[i]; else over = true; ++i; buf |= b << cnt; cnt += 8; }
        uint32_t v = (uint32_t)(buf & ((1ull << k) - 1)); buf >>= k; cnt -= k; return v;
    }
};
struct Huff { uint16_t count[16]; uint16_t sym[320]; };
void Build(Huff& h, const uint8_t* len, int n) {
    memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i) h.count[len[i]]++;
    h.count[0] = 0;
    uint16_t offs[16]; offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = offs[l] + h.count[l];
    for (int i = 0; i < n; ++i) if (len[i]) h.sym[offs[len[i]]++] = (uint16_t)i;
}
int Decode(BitIn& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= (int)b.bits(1);
        const int count = h.count[len];
        if (code - count < first) return h.sym[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}
const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                 4097, 6145, 8193, 12289, 16385, 24577 };
const uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

// ---------------------------------------------------------------- SWF bits

struct Bits {   // most significant bit first (SWF records)
    const uint8_t* d; size_t n, pos; int bit = 0;
    uint32_t u(int k) {
        uint32_t v = 0;
        for (int j = 0; j < k; ++j) {
            const uint8_t byte = pos < n ? d[pos] : 0;
            v = (v << 1) | ((byte >> (7 - bit)) & 1);
            if (++bit == 8) { bit = 0; ++pos; }
        }
        return v;
    }
    int32_t s(int k) { if (!k) return 0; uint32_t v = u(k); return (v & (1u << (k - 1))) ? (int32_t)(v - (1ull << k)) : (int32_t)v; }
};
uint16_t Le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// ---------------------------------------------------------------- TrueType writing

struct Out {
    std::vector<uint8_t> b;
    void u8(uint32_t v) { b.push_back((uint8_t)v); }
    void u16(uint32_t v) { u8(v >> 8); u8(v); }
    void u32(uint32_t v) { u16(v >> 16); u16(v); }
    void pad4() { while (b.size() & 3) b.push_back(0); }
};
uint32_t Sum(const std::vector<uint8_t>& t) {
    uint32_t s = 0;
    for (size_t i = 0; i < t.size(); i += 4) {
        uint32_t w = 0;
        for (int k = 0; k < 4; ++k) w = w << 8 | (i + k < t.size() ? t[i + k] : 0);
        s += w;
    }
    return s;
}

struct Pt { float x, y; bool on; };
using Contour = std::vector<Pt>;

double Area(const Contour& c) {
    double a = 0;
    for (size_t i = 0; i < c.size(); ++i) { const Pt &p = c[i], &q = c[(i + 1) % c.size()]; a += (double)p.x * q.y - (double)q.x * p.y; }
    return a / 2;
}
bool Inside(const Contour& c, float x, float y) {   // (the control points as polygon corners: close enough to tell nesting)
    bool in = false;
    for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
        const Pt &a = c[i], &b = c[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

struct Glyph { std::vector<Contour> cs; int adv = 0; };

// one glyph's shape (SHAPE: fill / line bit counts, then shape records) -> its contours in TrueType units (y up)
void ReadGlyph(const uint8_t* d, size_t n, Glyph& g) {
    Bits b{ d, n, 0 };
    int fillBits = (int)b.u(4), lineBits = (int)b.u(4);
    float x = 0, y = 0;
    Contour* cur = nullptr;
    auto add = [&](float px, float py, bool on) {
        if (!cur) { g.cs.emplace_back(); cur = &g.cs.back(); cur->push_back({ x / 20.f, -y / 20.f, true }); }
        cur->push_back({ px / 20.f, -py / 20.f, on });
    };
    for (int guard = 0; guard < 100000 && b.pos < n; ++guard) {
        if (b.u(1) == 0) {
            const uint32_t f = b.u(5);
            if (!f) break;
            if (f & 1) { const int k = (int)b.u(5); x = (float)b.s(k); y = (float)b.s(k); g.cs.emplace_back(); cur = &g.cs.back(); cur->push_back({ x / 20.f, -y / 20.f, true }); }
            if (f & 2) b.u(fillBits);
            if (f & 4) b.u(fillBits);
            if (f & 8) b.u(lineBits);
            if (f & 16) break;   // (new styles: never in a font)
        } else if (b.u(1)) {   // straight
            const int k = (int)b.u(4) + 2;
            float dx = 0, dy = 0;
            if (b.u(1)) { dx = (float)b.s(k); dy = (float)b.s(k); }
            else if (b.u(1)) dy = (float)b.s(k); else dx = (float)b.s(k);
            x += dx; y += dy; add(x, y, true);
        } else {   // curved: control, then anchor
            const int k = (int)b.u(4) + 2;
            const float cx = x + (float)b.s(k), cy = y + (float)b.s(k);
            add(cx, cy, false);
            x = cx + (float)b.s(k); y = cy + (float)b.s(k);
            add(x, y, true);
        }
    }
    // closed contours repeat their start: drop it; tiny ones go
    std::vector<Contour> keep;
    for (auto& c : g.cs) {
        if (c.size() > 1 && c.back().on && std::fabs(c.back().x - c[0].x) < 0.01f && std::fabs(c.back().y - c[0].y) < 0.01f) c.pop_back();
        if (c.size() >= 3) keep.push_back(std::move(c));
    }
    g.cs.swap(keep);
    // TrueType: outer contours clockwise (y up: negative area), holes counter-clockwise; nesting depth decides
    for (size_t i = 0; i < g.cs.size(); ++i) {
        int depth = 0;
        const Pt& t = g.cs[i][0];
        for (size_t j = 0; j < g.cs.size(); ++j) if (j != i && Inside(g.cs[j], t.x, t.y)) ++depth;
        const bool wantCw = depth % 2 == 0;
        if ((Area(g.cs[i]) < 0) != wantCw) std::reverse(g.cs[i].begin(), g.cs[i].end());
    }
    // a contour must start on-curve
    for (auto& c : g.cs) {
        auto on = std::find_if(c.begin(), c.end(), [](const Pt& p) { return p.on; });
        if (on != c.end()) std::rotate(c.begin(), on, c.end());
    }
}

void NameRecord(Out& recs, std::vector<uint8_t>& strings, int id, const std::string& s) {
    recs.u16(3); recs.u16(1); recs.u16(0x409); recs.u16(id); recs.u16((uint32_t)s.size() * 2); recs.u16((uint32_t)strings.size());
    for (unsigned char c : s) { strings.push_back(0); strings.push_back(c); }
}

} // namespace

bool Inflate(const uint8_t* in, size_t size, std::vector<uint8_t>& out, size_t expect) {
    if (size < 2 || (in[0] & 0x0F) != 8) return false;
    BitIn b{ in + 2, size - 2 };
    out.clear(); out.reserve(expect);
    Huff lit, dist;
    for (int last = 0; !last;) {
        last = (int)b.bits(1);
        const int type = (int)b.bits(2);
        if (type == 0) {
            const int drop = b.cnt & 7; b.bits(drop);
            const uint32_t len = b.bits(16), nlen = b.bits(16);
            if ((len ^ 0xFFFF) != nlen) return false;
            for (uint32_t k = 0; k < len; ++k) out.push_back((uint8_t)b.bits(8));
        } else if (type == 1 || type == 2) {
            uint8_t lens[320];
            if (type == 1) {
                for (int i = 0; i < 144; ++i) lens[i] = 8;
                for (int i = 144; i < 256; ++i) lens[i] = 9;
                for (int i = 256; i < 280; ++i) lens[i] = 7;
                for (int i = 280; i < 288; ++i) lens[i] = 8;
                Build(lit, lens, 288);
                for (int i = 0; i < 30; ++i) lens[i] = 5;
                Build(dist, lens, 30);
            } else {
                const int nlen = (int)b.bits(5) + 257, ndist = (int)b.bits(5) + 1, ncode = (int)b.bits(4) + 4;
                static const uint8_t ord[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                uint8_t cl[19] = {};
                for (int i = 0; i < ncode; ++i) cl[ord[i]] = (uint8_t)b.bits(3);
                Huff ch; Build(ch, cl, 19);
                int i = 0;
                while (i < nlen + ndist) {
                    const int sym = Decode(b, ch);
                    if (sym < 0) return false;
                    if (sym < 16) { lens[i++] = (uint8_t)sym; continue; }
                    int rep = 0; uint8_t v = 0;
                    if (sym == 16) { if (!i) return false; v = lens[i - 1]; rep = 3 + (int)b.bits(2); }
                    else if (sym == 17) rep = 3 + (int)b.bits(3);
                    else rep = 11 + (int)b.bits(7);
                    if (i + rep > nlen + ndist) return false;
                    while (rep--) lens[i++] = v;
                }
                Build(lit, lens, nlen);
                Build(dist, lens + nlen, ndist);
            }
            for (;;) {
                const int sym = Decode(b, lit);
                if (sym < 0 || b.over) return false;
                if (sym < 256) { out.push_back((uint8_t)sym); continue; }
                if (sym == 256) break;
                const int li = sym - 257;
                if (li >= 29) return false;
                const int len = kLenBase[li] + (int)b.bits(kLenExtra[li]);
                const int di = Decode(b, dist);
                if (di < 0 || di >= 30) return false;
                const size_t d = kDistBase[di] + b.bits(kDistExtra[di]);
                if (d > out.size()) return false;
                const size_t from = out.size() - d;
                for (int k = 0; k < len; ++k) out.push_back(out[from + k]);
            }
        } else return false;
        if (b.over) return false;
    }
    return true;
}

bool Body(const std::vector<uint8_t>& m, std::vector<uint8_t>& body, std::string& err) {
    if (m.size() < 8) { err = "too short"; return false; }
    const bool zip = m[0] == 'C', plain = m[0] == 'F' || m[0] == 'G';
    if (!(zip || plain) || !((m[1] == 'W' && m[2] == 'S') || (m[1] == 'F' && m[2] == 'X'))) { err = "not a Flash movie"; return false; }
    const uint32_t len = Le32(&m[4]);
    if (plain) { body.assign(m.begin() + 8, m.end()); return true; }
    if (!Inflate(m.data() + 8, m.size() - 8, body, len > 8 ? len - 8 : 0)) { err = "its zlib data is bad"; return false; }
    return true;
}

bool FontToTtf(const uint8_t* t, size_t n, const std::string& family, std::vector<uint8_t>& ttf, std::string& name) {
    if (n < 8) return false;
    const uint8_t flags = t[2];
    const bool layout = flags & 0x80, wideOff = flags & 0x08, wideCodes = flags & 0x04;
    const int nameLen = t[4];
    if (5 + (size_t)nameLen + 2 > n) return false;
    name.assign((const char*)t + 5, (size_t)nameLen);
    while (!name.empty() && name.back() == 0) name.pop_back();
    size_t p = 5 + nameLen;
    const int ng = Le16(t + p); p += 2;
    const size_t table = p, ow = wideOff ? 4 : 2;
    if (table + ow * (ng + 1) > n) return false;
    auto off = [&](int i) { return (size_t)(wideOff ? Le32(t + table + i * 4) : Le16(t + table + i * 2)); };
    const size_t codeTable = table + off(ng);   // (the CodeTableOffset follows the glyph offsets)
    std::vector<Glyph> gl(ng);
    for (int i = 0; i < ng; ++i) {
        const size_t a = table + off(i), b = i + 1 < ng ? table + off(i + 1) : codeTable;
        if (a > n || b > n || b < a) return false;
        ReadGlyph(t + a, b - a, gl[i]);
    }
    std::vector<uint16_t> codes(ng);
    p = codeTable;
    for (int i = 0; i < ng; ++i) { codes[i] = wideCodes ? Le16(t + p) : t[p]; p += wideCodes ? 2 : 1; }
    int ascent = 800, descent = 224, leading = 0;
    if (layout && p + 6 + 2 * (size_t)ng <= n) {
        ascent = Le16(t + p) / 20; descent = Le16(t + p + 2) / 20; leading = (int16_t)Le16(t + p + 4) / 20; p += 6;
        for (int i = 0; i < ng; ++i) gl[i].adv = (int16_t)Le16(t + p + i * 2) / 20;
    }

    // glyphs: 0 = .notdef (empty), then the font's in order
    Out glyf; std::vector<uint32_t> loca;
    int xMin = 32767, yMin = 32767, xMax = -32768, yMax = -32768, maxPts = 0, maxCs = 0, advMax = 0, minLsb = 32767, minRsb = 32767, maxExt = -32768;
    std::vector<std::pair<int, int>> hm;   // advance, lsb
    loca.push_back(0); hm.push_back({ 512, 0 });
    for (auto& g : gl) {
        loca.push_back((uint32_t)glyf.b.size());
        int npts = 0; for (auto& c : g.cs) npts += (int)c.size();
        const int adv = g.adv ? g.adv : 0;
        if (!npts) { hm.push_back({ adv, 0 }); advMax = std::max(advMax, adv); continue; }
        int gx0 = 32767, gy0 = 32767, gx1 = -32768, gy1 = -32768;
        std::vector<int> xs, ys; std::vector<uint8_t> fl; std::vector<int> ends;
        for (auto& c : g.cs) {
            for (auto& q : c) {
                const int X = (int)std::lround(q.x), Y = (int)std::lround(q.y);
                xs.push_back(X); ys.push_back(Y); fl.push_back(q.on ? 1 : 0);
                gx0 = std::min(gx0, X); gy0 = std::min(gy0, Y); gx1 = std::max(gx1, X); gy1 = std::max(gy1, Y);
            }
            ends.push_back((int)xs.size() - 1);
        }
        glyf.u16((uint32_t)g.cs.size()); glyf.u16((uint16_t)gx0); glyf.u16((uint16_t)gy0); glyf.u16((uint16_t)gx1); glyf.u16((uint16_t)gy1);
        for (int e : ends) glyf.u16((uint32_t)e);
        glyf.u16(0);   // no instructions
        for (uint8_t f : fl) glyf.u8(f);   // (each x / y: a 2-byte delta)
        int last = 0; for (int X : xs) { glyf.u16((uint16_t)(X - last)); last = X; }
        last = 0; for (int Y : ys) { glyf.u16((uint16_t)(Y - last)); last = Y; }
        glyf.pad4();
        xMin = std::min(xMin, gx0); yMin = std::min(yMin, gy0); xMax = std::max(xMax, gx1); yMax = std::max(yMax, gy1);
        maxPts = std::max(maxPts, npts); maxCs = std::max(maxCs, (int)g.cs.size());
        hm.push_back({ adv, gx0 });
        advMax = std::max(advMax, adv); minLsb = std::min(minLsb, gx0); minRsb = std::min(minRsb, adv - gx1); maxExt = std::max(maxExt, gx1);
    }
    loca.push_back((uint32_t)glyf.b.size());
    if (xMin > xMax) { xMin = yMin = 0; xMax = yMax = 1; minLsb = minRsb = 0; maxExt = 1; }
    const int nGlyphs = ng + 1;

    std::map<std::string, std::vector<uint8_t>> tables;
    {   Out o;   // head
        o.u32(0x00010000); o.u32(0x00010000); o.u32(0); o.u32(0x5F0F3CF5); o.u16(0x000B); o.u16(1024);
        o.u32(0); o.u32(0); o.u32(0); o.u32(0);
        o.u16((uint16_t)xMin); o.u16((uint16_t)yMin); o.u16((uint16_t)xMax); o.u16((uint16_t)yMax);
        o.u16(((flags & 1) ? 1 : 0) | ((flags & 2) ? 2 : 0)); o.u16(8); o.u16(2); o.u16(1); o.u16(0);
        tables["head"] = o.b; }
    {   Out o;   // hhea
        o.u32(0x00010000); o.u16((uint16_t)ascent); o.u16((uint16_t)-descent); o.u16((uint16_t)leading);
        o.u16((uint16_t)advMax); o.u16((uint16_t)minLsb); o.u16((uint16_t)minRsb); o.u16((uint16_t)maxExt);
        o.u16(1); o.u16(0); o.u16(0); o.u32(0); o.u32(0); o.u16(0); o.u16((uint32_t)nGlyphs);
        tables["hhea"] = o.b; }
    {   Out o;   // maxp 1.0
        o.u32(0x00010000); o.u16((uint32_t)nGlyphs); o.u16((uint32_t)maxPts); o.u16((uint32_t)maxCs); o.u16(0); o.u16(0);
        o.u16(2); o.u16(0); o.u16(0); o.u16(0); o.u16(0); o.u16(0); o.u16(0); o.u16(0); o.u16(0);
        tables["maxp"] = o.b; }
    {   Out o; for (auto& h : hm) { o.u16((uint32_t)std::max(0, h.first)); o.u16((uint16_t)h.second); } tables["hmtx"] = o.b; }
    {   Out o; for (uint32_t v : loca) o.u32(v); tables["loca"] = o.b; }
    tables["glyf"] = glyf.b;
    {   // cmap: format 4, one segment a character (and the closing 0xFFFF)
        std::vector<std::pair<uint16_t, uint16_t>> map;   // code, glyph
        for (int i = 0; i < ng; ++i) if (codes[i] && codes[i] != 0xFFFF) map.push_back({ codes[i], (uint16_t)(i + 1) });
        std::sort(map.begin(), map.end());
        map.erase(std::unique(map.begin(), map.end(), [](auto& a, auto& b) { return a.first == b.first; }), map.end());
        const int seg = (int)map.size() + 1;
        int sr = 1, es = 0; while (sr * 2 <= seg) { sr *= 2; ++es; }
        Out s;
        s.u16(4); s.u16((uint32_t)(16 + 8 * seg)); s.u16(0); s.u16((uint32_t)seg * 2); s.u16((uint32_t)sr * 2); s.u16((uint32_t)es); s.u16((uint32_t)(seg - sr) * 2);
        for (auto& m : map) s.u16(m.first);   // end codes
        s.u16(0xFFFF);
        s.u16(0);
        for (auto& m : map) s.u16(m.first);   // start codes
        s.u16(0xFFFF);
        for (auto& m : map) s.u16((uint16_t)(m.second - m.first));   // deltas
        s.u16(1);
        for (size_t k = 0; k <= map.size(); ++k) s.u16(0);   // range offsets
        Out o; o.u16(0); o.u16(1); o.u16(3); o.u16(1); o.u32(12); o.b.insert(o.b.end(), s.b.begin(), s.b.end());
        tables["cmap"] = o.b; }
    {   // name
        const bool bold = flags & 1, italic = flags & 2;
        const std::string sub = bold && italic ? "Bold Italic" : bold ? "Bold" : italic ? "Italic" : "Regular";
        std::string ps; for (char c : family) if (isalnum((unsigned char)c) || c == '_') ps += c;
        Out recs; std::vector<uint8_t> str;
        NameRecord(recs, str, 1, family); NameRecord(recs, str, 2, sub); NameRecord(recs, str, 4, family + " " + sub); NameRecord(recs, str, 6, ps.empty() ? "SwfFont" : ps);
        Out o; o.u16(0); o.u16(4); o.u16(6 + 4 * 12); o.b.insert(o.b.end(), recs.b.begin(), recs.b.end()); o.b.insert(o.b.end(), str.begin(), str.end());
        tables["name"] = o.b; }
    {   Out o; o.u32(0x00030000); o.u32(0); o.u16((uint16_t)-100); o.u16(50); o.u32(0); o.u32(0); o.u32(0); o.u32(0); o.u32(0); tables["post"] = o.b; }

    // the file: offset table, the directory (sorted by tag), the tables (4-aligned)
    const int nt = (int)tables.size();
    int sr = 1, es = 0; while (sr * 2 <= nt) { sr *= 2; ++es; }
    Out f; f.u32(0x00010000); f.u16((uint32_t)nt); f.u16((uint32_t)sr * 16); f.u16((uint32_t)es); f.u16((uint32_t)(nt - sr) * 16);
    size_t at = 12 + 16 * (size_t)nt, headAt = 0;
    std::vector<uint8_t> data;
    for (auto& [tag, tb] : tables) {   // (std::map: sorted by tag, as the directory wants)
        f.b.insert(f.b.end(), tag.begin(), tag.end());
        f.u32(Sum(tb)); f.u32((uint32_t)(at + data.size())); f.u32((uint32_t)tb.size());
        if (tag == "head") headAt = at + data.size();
        data.insert(data.end(), tb.begin(), tb.end());
        while (data.size() & 3) data.push_back(0);
    }
    f.b.insert(f.b.end(), data.begin(), data.end());
    const uint32_t adj = 0xB1B0AFBAu - Sum(f.b);
    f.b[headAt + 8] = (uint8_t)(adj >> 24); f.b[headAt + 9] = (uint8_t)(adj >> 16); f.b[headAt + 10] = (uint8_t)(adj >> 8); f.b[headAt + 11] = (uint8_t)adj;
    ttf.swap(f.b);
    return true;
}

bool ToTtf(const std::vector<uint8_t>& movie, const std::string& outDir, std::vector<std::pair<std::string, std::string>>& made, std::string& err) {
    std::vector<uint8_t> body;
    if (!Body(movie, body, err)) return false;
    if (body.empty()) { err = "empty"; return false; }
    const int nb = body[0] >> 3;
    size_t p = (5 + 4 * (size_t)nb + 7) / 8 + 4;
    std::map<int, std::pair<size_t, size_t>> fonts;   // id -> tag contents
    std::vector<std::pair<int, std::string>> exports;
    while (p + 2 <= body.size()) {
        const uint16_t h = Le16(&body[p]); p += 2;
        const int code = h >> 6; size_t len = h & 0x3F;
        if (len == 0x3F) { if (p + 4 > body.size()) break; len = Le32(&body[p]); p += 4; }
        if (p + len > body.size()) break;
        const uint8_t* x = &body[p];
        if (code == 75 && len >= 2) fonts[Le16(x)] = { p, len };
        else if (code == 56 && len >= 2) {
            const int cnt = Le16(x); size_t q = 2;
            for (int i = 0; i < cnt && q + 2 < len; ++i) {
                const int id = Le16(x + q); q += 2;
                const char* s = (const char*)x + q; const size_t sl = strnlen(s, len - q);
                exports.push_back({ id, std::string(s, sl) }); q += sl + 1;
            }
        }
        p += len;
        if (code == 0) break;
    }
    for (auto& [id, exp] : exports) {
        auto it = fonts.find(id);
        if (it == fonts.end()) continue;
        std::vector<uint8_t> ttf; std::string fname;
        if (!FontToTtf(&body[it->second.first], it->second.second, exp, ttf, fname)) continue;
        std::string file = exp; file.erase(std::remove(file.begin(), file.end(), '$'), file.end());
        const std::string path = outDir + file + ".ttf";
        FILE* o = fopen(path.c_str(), "wb");
        if (!o) { err = "cannot write " + path; continue; }
        fwrite(ttf.data(), 1, ttf.size(), o); fclose(o);
        made.push_back({ exp, path });
    }
    if (made.empty() && err.empty()) err = "no exported fonts";
    return !made.empty();
}

} // namespace swf_fonts
