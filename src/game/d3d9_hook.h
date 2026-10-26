#pragma once

namespace uiedit {

// Waits (on a worker thread) for the game's IDirect3DDevice9 and patches its vtable
// (Present + Reset). Vtable patching chains cleanly with mods that hook these functions inline.
void StartD3D9Hook();
void StopD3D9Hook();

} // namespace uiedit
