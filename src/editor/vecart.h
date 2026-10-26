#pragma once
// Vector art: SVG files in scripts\NFSU2_REUI_vector (and its subfolders), drawn with nanosvg at the size they're
// shown on screen, so they stay sharp at any scale. Elements use them as the texture "vec:<name>" (the file's path
// in that folder, '/'-separated, without .svg). Separate from the controller button icons (UniPad's glyphs).
#include <d3d9.h>
#include <string>
#include <vector>

namespace vecart {

struct Art { std::string name; float w = 0, h = 0; };   // its own size (the SVG's width / height, px)

void SetDir(const std::string& dir);   // the folder (made if missing)
const std::string& Dir();
const std::vector<Art>& List();        // scanned on first use and by Rescan
void Rescan();
const Art* Find(const std::string& name);
// the art drawn about px pixels on its longest side (cached; redrawn when the file changes); nullptr if missing
IDirect3DTexture9* Texture(IDirect3DDevice9* dev, const std::string& name, int px);
void Release();                        // device going away: every texture made

inline bool IsRef(const std::string& tex) { return tex.compare(0, 4, "vec:") == 0; }
inline std::string RefName(const std::string& tex) { return tex.substr(4); }
inline std::string Ref(const std::string& name) { return "vec:" + name; }

} // namespace vecart
