#pragma once
// The game's own menu fonts for REUI: the Flash fonts (DefineFont3) in the menus' font movie (gfxfontlib.gfx, from the
// player's data.bpk) written out as TrueType files, named as the menus name them ("$XR_TITLE", "$XR_MAIN_HEADER", ...).
// Nothing is shipped: they're made on the player's PC from their own game data (see assets_joyride.cpp).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace swf_fonts {
// A .gfx / .swf movie (CFX / CWS zlib, or FWS) -> its uncompressed body (after the 8-byte header); false if it isn't one.
bool Body(const std::vector<uint8_t>& movie, std::vector<uint8_t>& body, std::string& err);
// Every exported DefineFont3 of a movie as a .ttf in outDir ("<export name without $>.ttf"): (export name, file) pairs.
bool ToTtf(const std::vector<uint8_t>& movie, const std::string& outDir,
           std::vector<std::pair<std::string, std::string>>& made, std::string& err);
// (exposed for tests) one DefineFont3 tag's contents -> a TrueType file's bytes; the font's own name
bool FontToTtf(const uint8_t* tag, size_t size, const std::string& family, std::vector<uint8_t>& ttf, std::string& name);
// (exposed for tests) a zlib stream -> its bytes
bool Inflate(const uint8_t* in, size_t size, std::vector<uint8_t>& out, size_t expect);
} // namespace swf_fonts
