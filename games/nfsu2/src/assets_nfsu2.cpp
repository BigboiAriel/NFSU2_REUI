// NFSU2's own textures and fonts for REUI's editor (core/assets.h): its texture packs (GLOBAL, FRONTEND, LANGUAGES,
// the track maps) and FEng fonts, read from the game's files.
//
// File formats (NFSU2 PC, reverse engineered Oct 24 2026):
//  * Chunked files: u32 id, u32 size, data; ids with the top bit set hold more chunks.
//  * Texture pack B3300000: B3310000 { 33310001 header (+0xC pack name), 33310004 texture infos (0x7C each),
//    33310005 formats (32 each, D3D format / FourCC at +0x14), or 33310003 for packs whose textures are each
//    JDLZ-compressed (24 each: hash, file offset, size) }, B3320000 { 33320002 data, starting at the next 0x80 }.
//    Texture info: +0x0C name (24), +0x30 data offset, +0x34 palette offset, +0x38 data size, +0x3C palette size,
//    +0x40 top level size, +0x44 u16 width, height. Formats: DXT1/DXT3/DXT5, 0x15 A8R8G8B8, 0x29 P8 (+ BGRA palette).
//    A compressed texture decompresses to: data (size), palette, then its 0x7C info and 0x20 format record.
//  * .lzc files are one JDLZ block of a whole chunked file.
//  * Font 30201: +0 name; "FNTF" at +0x200: +0x0A u16 glyph count, +0x14 glyph table, +0x18 kerning table,
//    +0x24 line height. Glyph (16): u16 char, u8 w, h, u16 u, v, u8 -, s8 x offset, s8 y offset, u8 kerning count,
//    u16 kerning start, u16 advance. Kerning table: u32 count, then entries (4): u16 PREVIOUS char, s8 adjust,
//    u8 this char (low byte); a glyph's entries are table+4+ks*4, kc of them (header +0x0C flag 0x40000).
//    Its texture is the pack texture with the font's name in capitals (LANGUAGES\LanguageTextures.bin).

#include "assets_nfsu2.h"
#include "assets.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace assets_nfsu2 {
namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = fread(out.data(), 1, (size_t)n, f); fclose(f);
    return got == (size_t)n;
}
bool ReadPart(const std::string& path, uint32_t off, uint32_t size, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    out.resize(size);
    bool ok = fseek(f, (long)off, SEEK_SET) == 0 && fread(out.data(), 1, size, f) == size;
    fclose(f); return ok;
}
bool Jdlz(const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
    if (n < 16 || memcmp(in, "JDLZ", 4) != 0) return false;
    uint32_t outLen; memcpy(&outLen, in + 8, 4);
    if (outLen > 64u * 1024 * 1024) return false;
    out.assign(outLen, 0);
    unsigned flags1 = 1, flags2 = 1; size_t ip = 16, op = 0;
    while (ip < n && op < outLen) {
        if (flags1 == 1) { flags1 = in[ip++] | 0x100; if (ip >= n) break; }
        if (flags2 == 1) { flags2 = in[ip++] | 0x100; if (ip >= n) break; }
        if (flags1 & 1) {
            if (ip + 1 >= n) break;
            size_t len, t;
            if (flags2 & 1) { len = (in[ip + 1] | ((in[ip] & 0xF0) << 4)) + 3; t = (in[ip] & 0x0F) + 1; }
            else { t = (in[ip + 1] | ((in[ip] & 0xE0) << 3)) + 17; len = (in[ip] & 0x1F) + 3; }
            ip += 2;
            if (t > op) return false;
            for (size_t i = 0; i < len && op < outLen; ++i, ++op) out[op] = out[op - t];
            flags2 >>= 1;
        } else out[op++] = in[ip++];
        flags1 >>= 1;
    }
    return op == outLen;
}

struct SrcFile { std::string rel, path; bool lzc = false; std::vector<uint8_t> cache; };
struct Tex {   // a texture in the packs: where its bytes are
    std::string name, pack; int file = -1;
    bool blob = false; uint32_t blobOff = 0, blobSize = 0;   // compressed texture (33310003)
    uint32_t dataOff = 0, palOff = 0, size = 0, palSize = 0, top = 0;
    uint16_t w = 0, h = 0; uint32_t fmt = 0;
};
using assets::Font;
using assets::Glyph;
std::string g_dir;
std::vector<SrcFile> g_files;
std::vector<Tex> g_tex;   // (the index's ids)

uint32_t U32(const std::vector<uint8_t>& d, size_t o) { uint32_t v = 0; if (o + 4 <= d.size()) memcpy(&v, &d[o], 4); return v; }
uint16_t U16(const std::vector<uint8_t>& d, size_t o) { uint16_t v = 0; if (o + 2 <= d.size()) memcpy(&v, &d[o], 2); return v; }
std::string Str(const std::vector<uint8_t>& d, size_t o, size_t max) {
    std::string s; for (size_t i = 0; i < max && o + i < d.size() && d[o + i]; ++i) s += (char)d[o + i]; return s;
}

struct PackCtx { std::string name; size_t info = 0, infoN = 0, fmt = 0, fmtN = 0, comp = 0, compN = 0; };
void AddInfoTex(const std::vector<uint8_t>& d, size_t e, uint32_t fmt, int file, const std::string& pack, size_t data0,
                       bool blob, uint32_t blobOff, uint32_t blobSize, std::vector<Tex>& out) {
    Tex t; t.name = Str(d, e + 0x0C, 24); t.pack = pack; t.file = file; t.fmt = fmt;
    t.dataOff = U32(d, e + 0x30); t.palOff = U32(d, e + 0x34); t.size = U32(d, e + 0x38); t.palSize = U32(d, e + 0x3C);
    t.top = U32(d, e + 0x40); t.w = U16(d, e + 0x44); t.h = U16(d, e + 0x46);
    if (t.name.empty() || !t.w || !t.h || t.w > 4096 || t.h > 4096) return;
    {   // names are cut at 24 characters ("TACH_..._CUSTOM_04" variants then share one): the full name's hash is at +0x24
        uint32_t want = U32(d, e + 0x24);
        auto bh = [](const std::string& s) { uint32_t h = 0xFFFFFFFF; for (char c : s) h = h * 33 + (uint8_t)c; return h; };
        if (t.name.size() >= 23 && bh(t.name) != want) {
            size_t cut = t.name.rfind("_C");
            if (cut != std::string::npos)
                for (int i = 0; i < 100; ++i) {
                    char n[64]; snprintf(n, sizeof n, "%s_CUSTOM_%02d", t.name.substr(0, cut).c_str(), i);
                    if (bh(n) == want) { t.name = n; break; }
                }
        }
    }
    if (blob) { t.blob = true; t.blobOff = blobOff; t.blobSize = blobSize; t.palOff = t.size; t.dataOff = 0; }
    else t.dataOff += (uint32_t)data0, t.palOff += (uint32_t)data0;
    out.push_back(t);
}
void Walk(const std::vector<uint8_t>& d, size_t off, size_t end, int file, PackCtx& pk, std::vector<Tex>& tex, std::vector<Font>& fonts, int depth) {
    while (off + 8 <= end) {
        uint32_t id = U32(d, off), sz = U32(d, off + 4);
        size_t o = off + 8;
        if (sz > end - o) return;
        if (id == 0xB3300000) pk = PackCtx();
        else if (id == 0x33310001) pk.name = Str(d, o + 0x0C, 28);
        else if (id == 0x33310004) { pk.info = o; pk.infoN = sz / 0x7C; }
        else if (id == 0x33310005) { pk.fmt = o; pk.fmtN = sz / 32; }
        else if (id == 0x33310003) { pk.comp = o; pk.compN = sz / 24; }
        else if (id == 0x33320002) {
            size_t data0 = (o + 0x7F) & ~(size_t)0x7F;
            std::string pack = pk.name.empty() ? std::string("?") : pk.name;
            for (size_t k = 0; k < pk.infoN; ++k) {
                uint32_t fmt = k < pk.fmtN ? U32(d, pk.fmt + k * 32 + 0x14) : 0;
                AddInfoTex(d, pk.info + k * 0x7C, fmt, file, pack, data0, false, 0, 0, tex);
            }
            for (size_t k = 0; k < pk.compN; ++k) {
                uint32_t boff = U32(d, pk.comp + k * 24 + 4), bsz = U32(d, pk.comp + k * 24 + 8);
                if ((size_t)boff + bsz > d.size() || bsz < 16) continue;
                std::vector<uint8_t> x;
                if (!Jdlz(&d[boff], bsz, x) || x.size() < 0x9C) continue;
                uint32_t fmt = U32(x, x.size() - 0x20 + 0x14);
                AddInfoTex(x, x.size() - 0x9C, fmt, file, pack, 0, true, boff, bsz, tex);
            }
        } else if (id == 0x30201 && sz > 0x280) {
            Font f; f.name = Str(d, o, 32); f.source = g_files[file].rel;
            size_t F = o + 0x200;
            if (F + 0x80 <= o + sz && memcmp(&d[F], "FNTF", 4) == 0) {
                uint16_t n = U16(d, F + 0x0A); uint32_t go = U32(d, F + 0x14), ko = U32(d, F + 0x18), end2 = U32(d, F + 0x1C);
                f.line = (int)U32(d, F + 0x24); if (f.line <= 0 || f.line > 200) f.line = 20;
                for (uint16_t i = 0; i < n; ++i) {
                    size_t gp = F + go + i * 16; if (gp + 16 > o + sz) break;
                    Glyph g; g.c = U16(d, gp); g.w = d[gp + 2]; g.h = d[gp + 3]; g.u = U16(d, gp + 4); g.v = U16(d, gp + 6);
                    g.xo = (int8_t)d[gp + 9]; g.yo = (int8_t)d[gp + 10]; g.kc = d[gp + 11]; g.ks = U16(d, gp + 12); g.adv = U16(d, gp + 14);
                    f.g[g.c] = g;
                }
                if (ko && end2 > ko && F + end2 <= o + sz) f.kern.assign(d.begin() + F + ko, d.begin() + F + end2);
                if (!f.g.empty()) fonts.push_back(f);
            }
        }
        if ((id & 0x80000000) && depth < 6) Walk(d, o, o + sz, file, pk, tex, fonts, depth + 1);
        off = o + sz;
    }
}
void Scan(assets::Index& out) {
    std::vector<std::string> rels = {
        "GLOBAL\\GLOBALA.BUN", "GLOBAL\\GLOBALB.BUN", "GLOBAL\\InGameCommon.bun", "GLOBAL\\InGameRace.bun",
        "GLOBAL\\InGameDrag.bun", "GLOBAL\\InGameDrift.bun", "GLOBAL\\InGameFreeRoam.bun", "GLOBAL\\InGamePerformanceTuning.bun",
        "GLOBAL\\HUD_CustomTextures_ALL.bin", "GLOBAL\\GlobalTexturesNA.bin", "GLOBAL\\DYNTEX.BIN",
        "FRONTEND\\FrontB.lzc", "FRONTEND\\MAGAZINES_FRONTEND.BIN", "FRONTEND\\MAGAZINES_SHOWCASE.BIN",
        "LANGUAGES\\LanguageTextures.bin", "LANGUAGES\\English.bin" };
    {   // the track maps (TRACKMAP<track>: free roam's 4000, each race's own), each loaded on its own by the game
        std::vector<std::string> maps;
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((g_dir + "\\TRACKS\\TRACKMAP*.BIN").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) maps.push_back(std::string("TRACKS\\") + fd.cFileName); } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        std::sort(maps.begin(), maps.end());
        rels.insert(rels.end(), maps.begin(), maps.end());
    }
    std::vector<SrcFile> files; std::vector<Tex> tex; std::vector<Font> fonts;
    g_files.clear();
    for (auto& r : rels) {
        SrcFile sf; sf.rel = r; sf.path = g_dir + "\\" + r;
        std::vector<uint8_t> d;
        if (!ReadFile(sf.path, d)) continue;
        if (d.size() >= 16 && memcmp(d.data(), "JDLZ", 4) == 0) {
            std::vector<uint8_t> x; if (!Jdlz(d.data(), d.size(), x)) { reui::Log("ui editor: couldn't unpack %s", r.c_str()); continue; }
            d.swap(x); sf.lzc = true;
        }
        int idx = (int)g_files.size();
        g_files.push_back(sf);
        PackCtx pk; size_t before = tex.size(), fb = fonts.size();
        Walk(d, 0, d.size(), idx, pk, tex, fonts, 0);
        if (g_files[idx].lzc) g_files[idx].cache.swap(d);   // keep unpacked frontend data for loading its textures
        if (r.compare(0, 7, "TRACKS\\") != 0)   // (the track maps: counted once below)
            reui::Log("ui editor: %s - %d texture(s), %d font(s)", r.c_str(), (int)(tex.size() - before), (int)(fonts.size() - fb));
    }
    { int n = 0; for (auto& t : tex) if (g_files[t.file].rel.compare(0, 7, "TRACKS\\") == 0) ++n; reui::Log("ui editor: TRACKS\\TRACKMAP*.BIN - %d track map(s)", n); }
    g_tex = tex;
    for (size_t i = 0; i < tex.size(); ++i) {
        const Tex& t = tex[i];
        const std::string& rel = g_files[t.file].rel;
        assets::Tex a; a.name = t.name; a.pack = t.pack; a.source = rel; a.w = t.w; a.h = t.h; a.fmt = t.fmt; a.id = (int)i;
        a.hud = t.pack.find("Hud") != std::string::npos || t.pack.find("HUD") != std::string::npos || rel.find("InGame") != std::string::npos ||
                rel.compare(0, 7, "TRACKS\\") == 0;   // (the track maps: the minimap's)
        out.tex.push_back(a);
    }
    out.fonts.swap(fonts);
}

// ---------------------------------------------------------------- textures
bool BytesOf(const Tex& t, std::vector<uint8_t>& data, std::vector<uint8_t>& pal) {
    const SrcFile& f = g_files[t.file];
    uint32_t need = t.top ? t.top : t.size;
    if (t.blob) {
        std::vector<uint8_t> c, x;
        if (!ReadPart(f.path, t.blobOff, t.blobSize, c) || !Jdlz(c.data(), c.size(), x)) return false;
        if (need > x.size()) return false;
        data.assign(x.begin(), x.begin() + need);
        if (t.fmt == 0x29 && (size_t)t.palOff + 1024 <= x.size()) pal.assign(x.begin() + t.palOff, x.begin() + t.palOff + 1024);
        return true;
    }
    if (f.lzc) {
        if ((size_t)t.dataOff + need > f.cache.size()) return false;
        data.assign(f.cache.begin() + t.dataOff, f.cache.begin() + t.dataOff + need);
        if (t.fmt == 0x29 && (size_t)t.palOff + 1024 <= f.cache.size()) pal.assign(f.cache.begin() + t.palOff, f.cache.begin() + t.palOff + 1024);
        return true;
    }
    if (!ReadPart(f.path, t.dataOff, need, data)) return false;
    if (t.fmt == 0x29) ReadPart(f.path, t.palOff, 1024, pal);
    return true;
}

bool Pixels(const assets::Tex& at, gfx::Format& fmt, std::vector<uint8_t>& px, int& pitch) {
    if (at.id < 0 || at.id >= (int)g_tex.size()) return false;
    const Tex& t = g_tex[at.id];
    std::vector<uint8_t> data, pal;
    if (!BytesOf(t, data, pal)) return false;
    // the track maps' alpha is empty (the game lays them over the screen at its mask's opacity): shown opaque here,
    // so they can be seen and used - the element's own opacity sets how see-through
    if (g_files[t.file].rel.compare(0, 7, "TRACKS\\") == 0 && t.fmt == MAKEFOURCC('D', 'X', 'T', '3'))
        for (size_t b = 0; b + 16 <= data.size(); b += 16) memset(data.data() + b, 0xFF, 8);
    if (t.fmt == MAKEFOURCC('D', 'X', 'T', '1') || t.fmt == MAKEFOURCC('D', 'X', 'T', '3') || t.fmt == MAKEFOURCC('D', 'X', 'T', '5')) {
        fmt = t.fmt == MAKEFOURCC('D', 'X', 'T', '1') ? gfx::DXT1 : t.fmt == MAKEFOURCC('D', 'X', 'T', '3') ? gfx::DXT3 : gfx::DXT5;
        int bpb = fmt == gfx::DXT1 ? 8 : 16, bw = (t.w + 3) / 4, bh = (t.h + 3) / 4; size_t row = (size_t)bw * bpb;
        if (data.size() < row * bh) return false;
        px.swap(data); pitch = (int)row; return true;
    }
    if (t.fmt == 0x15) {   // A8R8G8B8: B G R A bytes
        size_t row = (size_t)t.w * 4;
        if (data.size() < row * t.h) return false;
        fmt = gfx::BGRA8; px.swap(data); pitch = (int)row; return true;
    }
    if (t.fmt == 0x29) {   // P8: a BGRA palette
        if (data.size() < (size_t)t.w * t.h || pal.size() < 1024) return false;
        px.resize((size_t)t.w * t.h * 4);
        for (size_t k = 0; k < (size_t)t.w * t.h; ++k) memcpy(&px[k * 4], &pal[data[k] * 4], 4);
        fmt = gfx::BGRA8; pitch = t.w * 4; return true;
    }
    return false;
}

} // namespace

void Install(const std::string& gameDir) {
    g_dir = gameDir;
    assets::source.scan = [](assets::Index& out) { Scan(out); };
    assets::source.pixels = Pixels;
}

} // namespace assets_nfsu2
