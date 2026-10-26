#pragma once
#include <cstdint>

// SPEED2.EXE v1.2 (NA)
namespace reui::addr {
constexpr uint32_t kExeSize = 4800512;      // file size, to recognise the version
constexpr uintptr_t D3DDevice = 0x870974;   // IDirect3DDevice9*
constexpr uintptr_t GameHwnd = 0x870990;    // HWND
constexpr uintptr_t GameState = 0x8654A4;   // 6 = in the world
} // namespace reui::addr
