// REUI's Direct3D 9 backend: textures (D3DPOOL_MANAGED) and the draw-state callbacks of core/gfx.h.
#include "gfx_d3d9.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gfx {

namespace {
IDirect3DDevice9* g_dev = nullptr;
// released at the start of the next frame, not mid-frame: an ImGui draw list built earlier in the same frame may
// still point at a texture, and it's only used when the frame is rendered
std::vector<IDirect3DTexture9*> g_later;
void FlushLater() { for (IDirect3DTexture9* t : g_later) t->Release(); g_later.clear(); }
} // namespace

namespace d3d9 {
void SetDevice(IDirect3DDevice9* dev) { g_dev = dev; }
IDirect3DDevice9* Device() { return g_dev; }
} // namespace d3d9

bool Ready() { return g_dev != nullptr; }
Tex Create(int w, int h, Format fmt, const void* data, int pitch) {
    if (!g_dev || w <= 0 || h <= 0 || !data) return nullptr;
    D3DFORMAT df = D3DFMT_A8R8G8B8;
    size_t row = (size_t)w * 4;
    int rows = h;
    switch (fmt) {
    case BGRA8: break;
    case DXT1: df = D3DFMT_DXT1; row = (size_t)((w + 3) / 4) * 8; rows = (h + 3) / 4; break;
    case DXT3: df = D3DFMT_DXT3; row = (size_t)((w + 3) / 4) * 16; rows = (h + 3) / 4; break;
    case DXT5: df = D3DFMT_DXT5; row = (size_t)((w + 3) / 4) * 16; rows = (h + 3) / 4; break;
    default: return nullptr;
    }
    IDirect3DTexture9* tx = nullptr;
    if (FAILED(g_dev->CreateTexture(w, h, 1, 0, df, D3DPOOL_MANAGED, &tx, nullptr)) || !tx) return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(tx->LockRect(0, &lr, nullptr, 0))) { tx->Release(); return nullptr; }
    for (int y = 0; y < rows; ++y) memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, (const uint8_t*)data + (size_t)y * pitch, row);
    tx->UnlockRect(0);
    return d3d9::Wrap(tx);
}
void Release(Tex t) { if (t) d3d9::Native(t)->Release(); }
void ReleaseLater(Tex t) { if (t) g_later.push_back(d3d9::Native(t)); }
void FrameStart() { FlushLater(); }
void ReleaseAll() { FlushLater(); }
ImTextureID Id(Tex t) { return (ImTextureID)d3d9::Native(t); }

void CbAdditive(const ImDrawList*, const ImDrawCmd*) {
    if (g_dev) { g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE); }
}
void CbNoAlpha(const ImDrawList*, const ImDrawCmd*) {   // ALPHAARG2 = diffuse
    if (g_dev) g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
}

// the map outside the texture is empty, not its edge pixels repeated: border addressing while the map is drawn, and
// the game's own mip settings (its last ones are still set) don't apply: level 0 down, linear between levels.
static DWORD g_mapSamp[6], g_mapAlphaOp, g_mapBlend[2];
static const D3DSAMPLERSTATETYPE kMapSamp[6] = {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_BORDERCOLOR, D3DSAMP_MIPFILTER, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS};
void CbMapSamplingOn(const ImDrawList*, const ImDrawCmd* cb) {
    if (!g_dev) return;
    for (int i = 0; i < 6; ++i) g_dev->GetSamplerState(0, kMapSamp[i], &g_mapSamp[i]);
    g_dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &g_mapAlphaOp);
    float bias = 0;
    const DWORD set[6] = {D3DTADDRESS_BORDER, D3DTADDRESS_BORDER, 0, D3DTEXF_LINEAR, 0, *(DWORD*)&bias};
    for (int i = 0; i < 6; ++i) g_dev->SetSamplerState(0, kMapSamp[i], set[i]);
    g_dev->GetRenderState(D3DRS_SRCBLEND, &g_mapBlend[0]); g_dev->GetRenderState(D3DRS_DESTBLEND, &g_mapBlend[1]);
    if (cb && cb->UserCallbackData) g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);   // (ALPHAARG2 = diffuse)
}
void CbMapSamplingOff(const ImDrawList*, const ImDrawCmd*) {
    if (!g_dev) return;
    for (int i = 0; i < 6; ++i) g_dev->SetSamplerState(0, kMapSamp[i], g_mapSamp[i]);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, g_mapAlphaOp);
    g_dev->SetRenderState(D3DRS_SRCBLEND, g_mapBlend[0]); g_dev->SetRenderState(D3DRS_DESTBLEND, g_mapBlend[1]);
}

} // namespace gfx
