#pragma once
// REUI's own menu screens over the game's Flash ones (fe_menus.cpp). A layout that redraws a Flash menu screen takes
// its controller there: the game's events for that screen (_root.inputEventHandler("UP" / "DOWN" / "ADVANCE", pad))
// come here first. Up / down move REUI's selection (its rows show it with showif menu.sel:<row> / menu.unsel:<row>)
// with the game's own sounds; on "ADVANCE" the Flash menu is set to the same row (its RowControl.setIndex) and gets
// the press, so the game itself does what was picked (its own calls, screens and checks). Other events (back, ...)
// go to the movie as before.
#include <string>

namespace reui_joyride {
bool FeMenuInput(const std::string& screenPath, const std::string& code, int controller);   // (the game thread) true: taken
void FeMenuFrame();                                                // every REUI frame
bool FeMenuToken(const std::string& key, std::string& out);        // menu.sel:<row>, menu.unsel:<row>, menu.row, menu.on
void SetMenusEnabled(bool on);
void FeMenuCall(const std::string& method, const std::string& args);   // each call seen (popups opening / closing)
bool FeMenuHides(const std::string& screenKey);   // a popup is over a driven screen: its REUI layout steps aside
} // namespace reui_joyride
