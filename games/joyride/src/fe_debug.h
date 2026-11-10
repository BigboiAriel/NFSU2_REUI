#pragma once
// The Flash menus in REUI (fe_debug.cpp): screen tokens for layouts and the F7 debug page.
#include <string>

namespace reui_joyride {
void FeInit(const std::string& dataDir);   // fe_commands.txt is looked for there
void FeFrame();                            // every frame: the screens and new calls from the port
void FeDebugWindow(bool* open);
bool FeToken(const std::string& key, std::string& out);   // screen, screens, screen:<key>
const std::string& FeTopScreen();
std::string ScreenKey(const std::string& path);           // "screens\\menus\\x.swf" -> "menus/x"
} // namespace reui_joyride
