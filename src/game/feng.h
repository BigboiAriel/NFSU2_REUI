#pragma once
#include <string>
#include <utility>
#include <vector>

namespace uiedit {

// The game's loaded FEng packages (menus, HUD screens), from its package init / unload.
void FengInstall();
std::vector<std::pair<void*, std::string>> FengPackages();   // {FEPackage*, name}

} // namespace uiedit
