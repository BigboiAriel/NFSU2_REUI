#pragma once
// The Flash menus in REUI (fe_debug.cpp): screen tokens for layouts and the F7 debug page.
#include <string>
#include <vector>

namespace reui_joyride {
void FeInit(const std::string& dataDir);   // fe_commands.txt is looked for there
void FeFrame();                            // every frame: the screens and new calls from the port
void FeDebugWindow(bool* open);
bool FeToken(const std::string& key, std::string& out);   // screen, screens, screen:<key>
const std::string& FeTopScreen();
std::string ScreenKey(const std::string& path);           // "screens\\menus\\x.swf" -> "menus/x"
// a call to the game (dir 0, as a page: ExternalInterface) or to the movies (1, as the game: Invoke); any thread
void FeSend(int dir, const std::string& method, const std::vector<std::string>& args);
} // namespace reui_joyride
