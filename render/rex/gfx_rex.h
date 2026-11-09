#pragma once
// REUI's backend on a ReXGlue ImmediateDrawer (core/gfx.h): textures are the drawer's RGBA8 ImmediateTextures (BGRA8
// and DXT1/3/5 are converted when made). The draw-state callbacks do nothing here (the immediate drawer has one
// pipeline), and the GPU shapes are off (shapes are made on the CPU, shapes.cpp).
#include "gfx.h"
#include <cstdint>

namespace rex {
namespace ui {
class ImmediateDrawer;
}
}  // namespace rex

namespace gfx {
namespace rex {
void SetDrawer(::rex::ui::ImmediateDrawer* drawer);   // the overlay's (nullptr when it goes)
void FrameStart(uint64_t frame);                       // the overlay's frame counter: textures released 4 frames later
void ReleaseEverything();                              // the overlay going away
} // namespace rex
} // namespace gfx
