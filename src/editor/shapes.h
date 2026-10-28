#pragma once
// Shapes: rounded rectangles drawn from a signed distance field at their size on screen - a stroke and a fill, each
// its own texture. Without a gradient a texture is white with alpha (coloured when drawn, so a colour change needs no
// redraw); with one, its colours are in the texture. Cutters subtract from the shape as a real boolean (the stroke
// follows the cut). Cached by geometry and gradients.
#include <d3d9.h>
#include <array>
#include <vector>

namespace shapes {

// A gradient: stops (position 0..1, r g b a), straight lines between them.
// Stroke modes: 1 across the stroke (outer edge -> inner edge, following the curve), 2 along it (around the outline
// clockwise from the top-left, `angle` = start offset in degrees of a turn).
// Fill modes: 1 linear (`angle` degrees, 0 = left to right), 2 radial (centre -> edge), 3 from the edge (the path) inward
// over `depth` pixels (following the curve). The fill reaches the path; the stroke is drawn over it.
struct Gradient {
    int mode = 0; float angle = 0, depth = 0;
    std::vector<std::array<float, 5>> stops;
};
enum { ALIGN_INSIDE = 0, ALIGN_CENTRE = 1, ALIGN_OUTSIDE = 2 };
struct Box {                 // in the shape's own pixels: centre (from the shape's centre), size, turn
    float cx = 0, cy = 0, w = 0, h = 0, rot = 0;   // rot: radians, clockwise (screen, y down)
    float r[4] = {0, 0, 0, 0};                     // corner radii: top-left, top-right, bottom-right, bottom-left
    float gap = 0;                                 // cutters: cut this much wider than themselves
};
// fill / stroke: white with alpha (or the gradient's colours). A cut's edge has no stroke of the shape's: cutters are
// drawn as shapes themselves (their own border outlines the cut).
struct Result { IDirect3DTexture9* fill = nullptr; IDirect3DTexture9* stroke = nullptr; float margin = 0; int pw = 0, ph = 0; };

// The shape (w x h pixels, corner radii) minus the cutters, its stroke `stroke` pixels wide on the edge (align), with
// optional gradients. Its textures cover the shape's box plus `margin` pixels on every side.
bool Get(IDirect3DDevice9* dev, const Box& shape, float stroke, int align, const Gradient& strokeGrad, const Gradient& fillGrad,
         const std::vector<Box>& cuts, Result& out);
void Trim();                 // each frame: drops shapes not drawn for a while
void Release();              // device going away

} // namespace shapes
