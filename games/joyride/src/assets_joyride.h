#pragma once
// Joy Ride Turbo's assets for REUI (core/assets.h): vector fonts for now - the TrueType / OpenType files in REUI's
// fonts\ folder and a few of Windows' own (not shipped). The game's own pictures (data.bpk) come later.
#include <string>

namespace assets_joyride {
void Install(const std::string& dataDir);   // before uied::Init (dataDir with a trailing backslash)
} // namespace assets_joyride
