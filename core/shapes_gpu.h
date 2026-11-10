#pragma once
// Shapes drawn by the graphics card: the same rounded box, stroke, gradients and cuts as shapes.cpp's textures, worked
// out per screen pixel by a pixel shader - nothing made on the CPU, nothing kept, any size or animation at no cost. If
// the shaders can't be made (or a shape has more cuts / stops than the shader takes) the textures are used as before.
// Implemented by each render backend (render/d3d9/shapes_gpu_d3d9.cpp: compiled at start from d3dcompiler_47.dll).
#include "shapes.h"
#include "imgui.h"

namespace shapes {

constexpr int kGpuMaxCuts = 16, kGpuMaxStops = 8, kGpuMaxMerges = 16;

bool GpuOn();                          // the shaders are ready (made on first ask)
void GpuFrameStart();                  // each frame, before anything's drawn (last frame's lists are rendered)
void GpuRelease();                     // device going away

// The shape - with the shapes merged into it (a union) - on the quad q (UnionBox's box plus `margin` on every side,
// clockwise from top-left), fill / stroke coloured by fillCol / strokeCol (times the gradients' colours). false: not
// drawn (too many cuts / merges / stops, or no shaders).
bool GpuDraw(ImDrawList* dl, const Box& shape, float stroke, int align, const Gradient& strokeGrad, const Gradient& fillGrad,
             const std::vector<Box>& cuts, const std::vector<Box>& merges, float margin, const ImVec2 q[4], ImU32 fillCol, ImU32 strokeCol);
// the margin the quad needs around the box (the stroke past the edge and a pixel for the soft edge)
float GpuMargin(float stroke, int align);

} // namespace shapes
