#pragma once
// "Copy motion from the game" for REUI's animation mode: the menus' Flash movies (kept by the picture cache in
// <REUI>\cache\movies\*.gfx) played frame by frame - sprites with their own playheads, stop(), loops - and each
// piece's world position, scale, turn, alpha and colour add read out. The editor (uied::motionSource) fits keys to it.
// (A C++ port of the Joy Ride port's tools/fe/flashplay.py.)
#include <string>

namespace flash_motion {
void Install(const std::string& movieDir);   // sets uied::motionSource; movieDir: <REUI>\cache\movies\ (trailing slash)
} // namespace flash_motion
