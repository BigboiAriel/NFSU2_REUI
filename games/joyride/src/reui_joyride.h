#pragma once
// REUI in Joy Ride Turbo's PC port (ReXGlue): the editor (F6) and the layouts it draws, on the window's presenter.
// The port calls Create from its app's OnCreateDialogs (the UI thread, presenter made) and Destroy on shutdown.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rex {
namespace ui {
class Window;
class Presenter;
class GraphicsProvider;
}  // namespace ui
}  // namespace rex

namespace reui_joyride {

// dataDir: REUI's folder (layout.txt, prefabs\, vector\ art, cache\) - the port's user data folder \REUI.
// gameData: the game's files (data.bpk: the menus' pictures); empty = none.
bool Create(rex::ui::Window* window, rex::ui::Presenter* presenter, rex::ui::GraphicsProvider* provider,
            const std::filesystem::path& dataDir, const std::filesystem::path& gameData);
void Destroy();
bool EditorOpen();
void SetEditorOpen(bool open);

// The layout modes (each has its own layout in the editor):
//   freeroam   Free roam   (FreeRoamGameMode)
//   race       Racing      (speed / item races, cups, online races: ItemRace / CupRace / OnlineRace GameMode)
//   timetrial  Time trial  (TimeTrialGameMode)
//   stunt      Stunt park  (StuntPark / OnlineStuntPark GameMode)
//   tutorial   Tutorial    (TutorialGameMode)
//   frontend   Front end   (no event: the menus; elements pick screens with showif=screen:<key>)
//   menus      Menus       (the menu pages' workspace)
// The port says which one is being played (any thread); "" = none (front end / loading): only the editor's
// Test or on-screen editing shows a layout then.
void SetMode(const std::string& key);

// The Flash menus, from the port (fe_screens.cpp): the screens in the root movie's levels, the calls between the pages
// and the game, and sending calls (fe_debug.cpp: layout tokens, the F7 debug page).
struct FeScreen { int level; std::string path; };
struct FeCall { double t; int dir; bool sent; std::string method, args; };   // dir 0 page -> game, 1 game -> page
struct FeBridge {
    std::function<std::vector<FeScreen>()> screens;
    std::function<std::vector<FeCall>(uint64_t since, uint64_t& next)> calls;   // after call number `since`
    std::function<void(int dir, const std::string& method, const std::vector<std::string>& args)> send;
};
void SetFeBridge(const FeBridge& bridge);
void StartAnimMode(bool focusFirst, float ms = 0, bool play = false, bool bare = false);   // animation mode (F8);
    // focusFirst: its first element; the playhead at ms, playing; bare: only the timeline (tests)
void SetAnimSpeed(float speed);   // live animation clips at this speed (slow motion to check them; 1 normally)
void AnimKeysTest(float at);      // (tests) copy the focused element's shown keys, paste at `at` ms
void AnimCopyMotionTest(const std::string& spec);   // (tests) "<element>.<movie clip>.<clip>": copy motion
void SetAnimLive(bool live);      // animation mode: the layout played live with stand-in menu values (tests)
bool FeDebugOpen();
void SetFeDebugOpen(bool open);

} // namespace reui_joyride
