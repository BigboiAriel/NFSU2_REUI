#pragma once
#include <windows.h>
#include <d3d9.h>

namespace reui {

void PanelInit(HWND hwnd, IDirect3DDevice9* device);
void PanelPollKey();      // the toggle key, once per Present
bool PanelWantsFrame();   // panel open, or the editor has something to draw (HUD, menu, preview)
void PanelFrame();        // once per Present, inside BeginScene/EndScene
void PanelBeforeReset();
void PanelAfterReset();
void PanelShutdown();
bool PanelOpen();
void PanelSetOpen(bool open);

} // namespace reui
