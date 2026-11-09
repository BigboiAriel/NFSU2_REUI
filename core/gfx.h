#pragma once
// The graphics API REUI's core draws through. The core never names Direct3D (or any other API): textures are gfx::Tex
// handles, and the few draw-state changes it needs are ImGui draw-list callbacks. Each game's build links one backend
// (render/d3d9 for NFSU2, render/d3d12 for Joy Ride Turbo) that implements everything here, plus the GPU shapes
// (shapes_gpu.h).
#include "imgui.h"

namespace gfx {

struct Texture;                 // a backend's texture (D3D9: the IDirect3DTexture9 itself)
using Tex = Texture*;

enum Format {
    BGRA8,                      // 4 bytes a pixel: B G R A (D3D9 A8R8G8B8, DXGI B8G8R8A8_UNORM)
    DXT1, DXT3, DXT5,           // rows of 4x4 blocks (BC1 / BC2 / BC3)
};

bool Ready();                   // textures can be made now (the device is there)
// A texture w x h of `fmt` from `data` (rows `pitch` bytes apart; for DXT a row is a row of blocks). nullptr: not made.
Tex Create(int w, int h, Format fmt, const void* data, int pitch);
void Release(Tex t);            // now: the device is going away / nothing can be drawing it
void ReleaseLater(Tex t);       // at the next FrameStart: an ImGui draw list built this frame may still point at it
void FrameStart();              // every frame before anything is drawn (last frame's lists are rendered): ReleaseLater's go
void ReleaseAll();              // device going away: ReleaseLater's go now
ImTextureID Id(Tex t);          // what ImGui draws for it

// Draw-list callbacks (ImDrawList::AddCallback) changing how the draws after them are made.
void CbAdditive(const ImDrawList*, const ImDrawCmd*);    // added onto the screen (until ImDrawCallback_ResetRenderState)
void CbNoAlpha(const ImDrawList*, const ImDrawCmd*);     // alpha from the vertex colour only, the texture's ignored (same)
// Minimap sampling: outside the texture is transparent (border addressing), mip levels linear from level 0. Undone by
// CbMapSamplingOff. UserCallbackData != nullptr: also CbNoAlpha's (the map's own alpha is empty).
void CbMapSamplingOn(const ImDrawList*, const ImDrawCmd*);
void CbMapSamplingOff(const ImDrawList*, const ImDrawCmd*);

} // namespace gfx
