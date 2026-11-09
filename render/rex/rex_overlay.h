#pragma once
// REUI on a ReXGlue (Xenia-derived) presenter: a UI drawer of its own with its own ImGui context, drawing through an
// ImmediateDrawer made from the graphics provider, and taking mouse / keyboard input from the window. Used by games
// recompiled with ReXGlue (Joy Ride Turbo). Everything here runs on the UI thread.
#include <functional>
#include <memory>

namespace rex {
namespace ui {
class Window;
class Presenter;
class GraphicsProvider;
}  // namespace ui
}  // namespace rex
struct ImGuiContext;

namespace reui_rex {

struct OverlaySetup {
    rex::ui::Window* window = nullptr;
    rex::ui::Presenter* presenter = nullptr;
    rex::ui::GraphicsProvider* provider = nullptr;
    size_t zOrder = 0;
    std::function<void()> frame;               // between ImGui::NewFrame and Render (REUI's drawing)
    std::function<bool(int vk)> keyDown;       // a key pressed (Windows virtual-key code): true = taken
    std::function<bool()> wantsInput;          // mouse / keyboard go to REUI (else they're only watched)
    std::function<void()> fonts;               // the context's fonts (io.Fonts), once
};

class Overlay;
struct OverlayDeleter { void operator()(Overlay* o) const; };
using OverlayPtr = std::unique_ptr<Overlay, OverlayDeleter>;
OverlayPtr CreateOverlay(const OverlaySetup& setup);   // nullptr on failure
ImGuiContext* OverlayContext(Overlay* overlay);

} // namespace reui_rex
