#pragma once
// REUI's Direct3D 9 backend (core/gfx.h): a gfx::Tex is the IDirect3DTexture9 itself, so textures the game made can be
// handed to the core with Wrap.
#include <d3d9.h>
#include "gfx.h"

namespace gfx {
namespace d3d9 {

void SetDevice(IDirect3DDevice9* dev);   // the game's device (every Present, before the core's FrameStart)
IDirect3DDevice9* Device();
inline Tex Wrap(IDirect3DTexture9* t) { return reinterpret_cast<Tex>(t); }
inline IDirect3DTexture9* Native(Tex t) { return reinterpret_cast<IDirect3DTexture9*>(t); }

} // namespace d3d9
} // namespace gfx
