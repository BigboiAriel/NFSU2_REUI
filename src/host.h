#pragma once
#include <d3d9.h>

namespace uiedit {

void HostInit();                         // once, after ImGui is up
void HostTick(bool focused);             // every Present
bool HostWantsFrames();                  // something to draw with the panel closed (the HUD layout, a menu, a test)
void HostBeginFrame(IDirect3DDevice9*);  // before ImGui::NewFrame
void HostDraw();                         // inside the ImGui frame, after the panel
void HostPanel();                        // the panel's contents
void HostRelease();                      // device going away for good

} // namespace uiedit
