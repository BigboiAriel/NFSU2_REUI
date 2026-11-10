#pragma once
// Joy Ride Turbo's assets for REUI (core/assets.h):
//  - the menus' pictures: the .dds images inside the Scaleform menu packs (data.bpk's data\fe\**\*.bpk), read from
//    the player's own data.bpk. The first scan decodes data.bpk once (a few seconds, a background thread) and keeps
//    the images in <REUI>\cache\fe_images.bin + .idx; later runs read the cache (rebuilt when data.bpk changes).
//    Textures are named like the image file without "bpk_fe_" / ".dds" (eventselect_i11), their pack is the menu
//    movie (screens/common/eventselect); "hud" for data\fe\hud\...
//  - vector fonts: the TrueType / OpenType files in REUI's fonts\ folder and a few of Windows' own (not shipped).
#include <filesystem>
#include <string>

namespace assets_joyride {
// before uied::Init. dataDir: REUI's folder with a trailing backslash. bpk: the game's data.bpk ("" = no pictures).
void Install(const std::string& dataDir, const std::filesystem::path& bpk);
void Shutdown();   // stops a cache build in progress, closes the cache
} // namespace assets_joyride
