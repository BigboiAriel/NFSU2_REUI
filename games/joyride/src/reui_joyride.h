#pragma once
// REUI in Joy Ride Turbo's PC port (ReXGlue): the editor (F6) and the layouts it draws, on the window's presenter.
// The port calls Create from its app's OnCreateDialogs (the UI thread, presenter made) and Destroy on shutdown.
#include <filesystem>

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

} // namespace reui_joyride
