#pragma once
// A game's own textures and fonts: the editor's Textures / Fonts pages list them, image and text elements draw with
// them. The game connection supplies them by setting assets::source before uied::Init (NFSU2: its texture packs and
// FEng fonts, games/nfsu2/src/assets_nfsu2.cpp). Without a source the lists are empty.
#include "gfx.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace assets {

struct Tex {
    std::string name, pack;
    std::string source;          // where it comes from (a file): shown, and the pack filter's "source: pack"
    uint16_t w = 0, h = 0;
    uint32_t fmt = 0;            // shown: a FourCC (DXT1 / DXT3 / DXT5), 0x15 A8R8G8B8, 0x29 P8 (palette)
    bool hud = false;            // kept by the "in-game HUD only" filter
    int id = -1;                 // the source's own
    // the editor's
    gfx::Tex tex = nullptr; bool failed = false; uint32_t usedAt = 0;
};
// A bitmap font: glyph rectangles in a texture. Kerning: the source's table (count, then 4-byte entries: u16 previous
// char, s8 adjust, u8 char's low byte); a glyph's entries are kern[4 + (ks + j) * 4], kc of them.
struct Glyph { uint16_t c; uint8_t w, h; uint16_t u, v; int8_t xo, yo; uint8_t kc; uint16_t ks; uint16_t adv; };
struct Font {
    std::string name, source; std::map<uint16_t, Glyph> g; int line = 20;
    std::vector<uint8_t> kern;
    int tex = -1;                // its texture in the index (by default the one named as the font in capitals)
};
struct Index { std::vector<Tex> tex; std::vector<Font> fonts; };

struct Source {
    std::function<void(Index& out)> scan;   // fills the index (a background thread)
    // one texture's pixels, ready for gfx::Create: format, rows `pitch` bytes apart (the game thread)
    std::function<bool(const Tex& t, gfx::Format& fmt, std::vector<uint8_t>& px, int& pitch)> pixels;
};
extern Source source;

} // namespace assets
