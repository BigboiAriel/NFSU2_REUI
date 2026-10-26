#pragma once
#include <string>

namespace uiedit {

struct Config {
    int panelKey = 0x75;   // VK_F6: the editor panel
    bool logFile = true;
    bool replaceHud = true;   // hide the game's HUD where the layout draws its own (per element, later)
};
extern Config g_config;
extern std::string g_scriptsDir;   // folder that holds the .asi, with trailing backslash

void LoadConfig(const std::string& iniPath);
void IniSet(const char* section, const char* key, const std::string& value);   // keeps comments / layout

} // namespace uiedit
