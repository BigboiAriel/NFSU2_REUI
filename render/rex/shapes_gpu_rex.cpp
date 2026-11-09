// GPU shapes on a ReXGlue ImmediateDrawer: not available (one fixed pipeline) - shapes are made on the CPU (shapes.cpp).
#include "shapes_gpu.h"

#include <algorithm>

namespace shapes {
bool GpuOn() { return false; }
void GpuFrameStart() {}
void GpuRelease() {}
bool GpuDraw(ImDrawList*, const Box&, float, int, const Gradient&, const Gradient&, const std::vector<Box>&, float, const ImVec2[4], ImU32, ImU32) { return false; }
float GpuMargin(float stroke, int align) {   // (as render/d3d9's)
    float sw = std::max(0.f, std::min(stroke, 1e4f));
    float lo = align == ALIGN_INSIDE ? -sw : align == ALIGN_CENTRE ? -sw / 2 : 0.f;
    return 1.5f + std::max(0.f, lo + sw);
}
} // namespace shapes
