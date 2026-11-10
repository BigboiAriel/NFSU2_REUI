#pragma once
// REUI in Joy Ride Turbo's PC port (ReXGlue): the editor (F6) and the layouts it draws, on the window's presenter.
// The port calls Create from its app's OnCreateDialogs (the UI thread, presenter made) and Destroy on shutdown.
#include <filesystem>
#include <string>

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
//   menus      Menus       (the menu pages' workspace)
// The port says which one is being played (any thread); "" = none (front end / loading): only the editor's
// Test or on-screen editing shows a layout then.
void SetMode(const std::string& key);

} // namespace reui_joyride
