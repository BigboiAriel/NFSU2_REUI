#pragma once
// Shapes: rounded rectangles drawn from a signed distance field at their size on screen - a stroke (inside the edge)
// and a fill, each its own white-with-alpha texture so their colours are applied when drawn (no redraw for a colour
// change). Cutters subtract from the shape as a real boolean (the stroke follows the cut). Cached by geometry.
#include <d3d9.h>
#include <vector>

namespace shapes {

struct Box {                 // in the shape's own pixels: centre (from the shape's centre), size, turn
    float cx = 0, cy = 0, w = 0, h = 0, rot = 0;   // rot: radians, clockwise (screen, y down)
    float r[4] = {0, 0, 0, 0};                     // corner radii: top-left, top-right, bottom-right, bottom-left
    float gap = 0;                                 // cutters: cut this much wider than themselves
};
struct Result { IDirect3DTexture9* fill = nullptr; IDirect3DTexture9* stroke = nullptr; float margin = 0; int pw = 0, ph = 0; };

// The shape (w x h pixels, corner radii, stroke width in pixels) minus the cutters. Its textures cover the shape's
// box plus `margin` pixels on every side. false: nothing to draw (no device / too small / out of memory).
bool Get(IDirect3DDevice9* dev, const Box& shape, float stroke, const std::vector<Box>& cuts, Result& out);
void Trim();                 // each frame: drops shapes not drawn for a while
void Release();              // device going away

} // namespace shapes
