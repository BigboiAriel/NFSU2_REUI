// See rex_overlay.h. The ImGui renderer is ImGui 1.92's texture protocol over ImmediateDrawer textures (RGBA8; a
// texture ImGui updates - the font atlas growing - is made again whole).
#include "rex_overlay.h"
#include "gfx_rex.h"

#include <rex/ui/immediate_drawer.h>
#include <rex/ui/graphics_provider.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_drawer.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <imgui.h>

#include <SDL3/SDL_mouse.h>   // (the game's window is SDL's: REUI's cursor shapes - resize arrows, the text beam)

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace reui_rex {


// a key press's character (Windows virtual key, US layout), 0: none
static unsigned CharOf(int vk, bool shift) {
    if (vk >= 'A' && vk <= 'Z') return shift ? vk : vk - 'A' + 'a';
    if (vk >= '0' && vk <= '9') return shift ? (unsigned)")!@#$%^&*("[vk - '0'] : vk;
    if (vk >= 0x60 && vk <= 0x69) return '0' + (vk - 0x60);   // (number pad)
    switch (vk) {
    case 0x20: return ' ';
    case 0x6A: return '*'; case 0x6B: return '+'; case 0x6D: return '-'; case 0x6E: return '.'; case 0x6F: return '/';
    case 0xBA: return shift ? ':' : ';'; case 0xBB: return shift ? '+' : '='; case 0xBC: return shift ? '<' : ',';
    case 0xBD: return shift ? '_' : '-'; case 0xBE: return shift ? '>' : '.'; case 0xBF: return shift ? '?' : '/';
    case 0xC0: return shift ? '~' : '`'; case 0xDB: return shift ? '{' : '['; case 0xDC: return shift ? '|' : '\\';
    case 0xDD: return shift ? '}' : ']'; case 0xDE: return shift ? '"' : '\'';
    default: return 0;
    }
}
static ImGuiKey KeyOf(int vk) {
    if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= 0x70 && vk <= 0x7B) return (ImGuiKey)(ImGuiKey_F1 + (vk - 0x70));
    switch (vk) {
    case 0x09: return ImGuiKey_Tab;        case 0x25: return ImGuiKey_LeftArrow;  case 0x27: return ImGuiKey_RightArrow;
    case 0x26: return ImGuiKey_UpArrow;    case 0x28: return ImGuiKey_DownArrow;  case 0x21: return ImGuiKey_PageUp;
    case 0x22: return ImGuiKey_PageDown;   case 0x24: return ImGuiKey_Home;       case 0x23: return ImGuiKey_End;
    case 0x2D: return ImGuiKey_Insert;     case 0x2E: return ImGuiKey_Delete;     case 0x08: return ImGuiKey_Backspace;
    case 0x20: return ImGuiKey_Space;      case 0x0D: return ImGuiKey_Enter;      case 0x1B: return ImGuiKey_Escape;
    case 0x10: case 0xA0: return ImGuiKey_LeftShift;  case 0xA1: return ImGuiKey_RightShift;
    case 0x11: case 0xA2: return ImGuiKey_LeftCtrl;   case 0xA3: return ImGuiKey_RightCtrl;
    case 0x12: case 0xA4: return ImGuiKey_LeftAlt;    case 0xA5: return ImGuiKey_RightAlt;
    case 0xBD: return ImGuiKey_Minus;      case 0xBB: return ImGuiKey_Equal;      case 0xBC: return ImGuiKey_Comma;
    case 0xBE: return ImGuiKey_Period;     case 0xBF: return ImGuiKey_Slash;      case 0xBA: return ImGuiKey_Semicolon;
    default: return ImGuiKey_None;
    }
}

class Overlay final : public rex::ui::UIDrawer, public rex::ui::WindowInputListener {
 public:
    explicit Overlay(const OverlaySetup& s) : s_(s) {}
    ~Overlay() override {
        if (s_.presenter) s_.presenter->RemoveUIDrawerFromUIThread(this);
        if (s_.window) { if (textOn_) s_.window->SetTextInputActive(false); s_.window->RemoveInputListener(this); }
        ImGuiContext* prev = ImGui::GetCurrentContext();
        if (ctx_) {
            ImGui::SetCurrentContext(ctx_);
            gfx::rex::ReleaseEverything();
            for (auto& kv : textures_) kv.second.reset();
            textures_.clear(); dying_.clear();
            ImGui::DestroyContext(ctx_);
        }
        ImGui::SetCurrentContext(prev == ctx_ ? nullptr : prev);
        gfx::rex::SetDrawer(nullptr);
    }
    bool Init() {
        if (!s_.window || !s_.presenter || !s_.provider) return false;
        drawer_ = s_.provider->CreateImmediateDrawer();
        if (!drawer_) return false;
        drawer_->SetPresenter(s_.presenter);
        gfx::rex::SetDrawer(drawer_.get());
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ctx_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendRendererName = "reui_rex";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset |
                           ImGuiBackendFlags_HasMouseCursors;   // (windows size from any edge)
        ImGui::StyleColorsDark();
        if (s_.fonts) s_.fonts();
        if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
        ImGui::SetCurrentContext(prev);
        last_ = std::chrono::steady_clock::now();
        s_.presenter->AddUIDrawerFromUIThread(this, s_.zOrder);
        s_.window->AddInputListener(this, s_.zOrder);
        return true;
    }
    ImGuiContext* context() const { return ctx_; }

    void Draw(rex::ui::UIDrawContext& dc) override {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO& io = ImGui::GetIO();
        const float w = (float)dc.render_target_width(), h = (float)dc.render_target_height();
        io.DisplaySize = ImVec2(w, h);
        auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::max(1e-4f, std::chrono::duration<float>(now - last_).count());
        last_ = now;
        ++frame_;
        while (!dying_.empty() && dying_.front().first + 4 <= frame_) dying_.pop_front();   // (no longer in flight)
        gfx::rex::FrameStart(frame_);
        ImGui::NewFrame();
        if (s_.frame) s_.frame();
        ImGui::Render();
        UpdateCursor();
        // typing: the game's window is SDL's, which sends typed characters only while its text input is on (off by
        // default; the SDK's own ImGui turns it on for itself only) - on while one of REUI's text boxes is being typed in
        if (io.WantTextInput != textOn_ && s_.window) { textOn_ = io.WantTextInput; s_.window->SetTextInputActive(textOn_); }
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd && dd->Textures) for (ImTextureData* t : *dd->Textures) UpdateTexture(t);
        if (dd && dd->CmdListsCount > 0 && w > 0 && h > 0) Render(dc, dd);
        ImGui::SetCurrentContext(prev);
        s_.presenter->RequestUIPaintFromUIThread();   // (keeps animating while nothing else repaints)
    }

    // the mouse cursor's shape while REUI has the mouse (ImGui's: resize arrows over window edges, the text beam...);
    // the window's own otherwise
    void UpdateCursor() {
        ImGuiIO& io = ImGui::GetIO();
        const int want = Wants() && io.WantCaptureMouse ? (int)ImGui::GetMouseCursor() : -2;
        if (want == cursor_) return;
        cursor_ = want;
        if (want < 0 || want >= ImGuiMouseCursor_COUNT) { SDL_SetCursor(SDL_GetDefaultCursor()); return; }
        SDL_SystemCursor sc = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (want) {
        case ImGuiMouseCursor_TextInput: sc = SDL_SYSTEM_CURSOR_TEXT; break;
        case ImGuiMouseCursor_ResizeAll: sc = SDL_SYSTEM_CURSOR_MOVE; break;
        case ImGuiMouseCursor_ResizeNS: sc = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
        case ImGuiMouseCursor_ResizeEW: sc = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
        case ImGuiMouseCursor_ResizeNESW: sc = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
        case ImGuiMouseCursor_ResizeNWSE: sc = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
        case ImGuiMouseCursor_Hand: sc = SDL_SYSTEM_CURSOR_POINTER; break;
        case ImGuiMouseCursor_NotAllowed: sc = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        default: break;
        }
        if (!cursors_[want]) cursors_[want] = SDL_CreateSystemCursor(sc);
        SDL_SetCursor(cursors_[want] ? cursors_[want] : SDL_GetDefaultCursor());
    }
    int cursor_ = -2;
    bool textOn_ = false;   // (the window's text input: on for REUI)
    bool realChars_ = false;  // the window's character events have reached REUI (else typing comes from key presses)
    SDL_Cursor* cursors_[ImGuiMouseCursor_COUNT] = {};

    // input: the window's events, given to REUI's context (and taken from the game while REUI wants them)
    void OnKeyDown(rex::ui::KeyEvent& e) override { Key(e, true); }
    void OnKeyUp(rex::ui::KeyEvent& e) override { Key(e, false); }
    void OnKeyChar(rex::ui::KeyEvent& e) override {
        if (!Wants()) return;
        bool take = true;
        realChars_ = true;   // (the window does send characters: no more made from key presses)
        With([&](ImGuiIO& io) { io.AddInputCharacter((unsigned)e.virtual_key()); take = Takes(io); });
        if (take) e.set_handled(true);
    }
    void OnMouseDown(rex::ui::MouseEvent& e) override { Button(e, true); }
    void OnMouseUp(rex::ui::MouseEvent& e) override { Button(e, false); }
    void OnMouseMove(rex::ui::MouseEvent& e) override { With([&](ImGuiIO& io) { io.AddMousePosEvent((float)e.x(), (float)e.y()); }); }
    void OnMouseWheel(rex::ui::MouseEvent& e) override {
        if (!Wants()) return;
        With([&](ImGuiIO& io) { io.AddMouseWheelEvent((float)e.scroll_x() / 120.f, (float)e.scroll_y() / 120.f); });
    }

 private:
    template <typename F> void With(F f) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ctx_);
        f(ImGui::GetIO());
        ImGui::SetCurrentContext(prev);
    }
    bool Wants() const { return s_.wantsInput && s_.wantsInput(); }
    bool Takes(const ImGuiIO& io) const { return !(s_.shareKeys && s_.shareKeys()) || io.WantTextInput; }
    void Key(rex::ui::KeyEvent& e, bool down) {
        int vk = (int)e.virtual_key();
        if (down && s_.keyDown && s_.keyDown(vk)) { e.set_handled(true); return; }
        if (!Wants()) return;
        bool take = true;
        With([&](ImGuiIO& io) {
            io.AddKeyEvent(ImGuiMod_Ctrl, e.is_ctrl_pressed());
            io.AddKeyEvent(ImGuiMod_Shift, e.is_shift_pressed());
            io.AddKeyEvent(ImGuiMod_Alt, e.is_alt_pressed());
            ImGuiKey k = KeyOf(vk);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
            // typing: the window's character events don't reach REUI (the SDK's own ImGui has them), so a REUI text box
            // being typed in gets its characters from the key presses (US layout; Ctrl / Alt shortcuts aren't text)
            if (down && io.WantTextInput && !realChars_ && !e.is_ctrl_pressed() && !e.is_alt_pressed())
                if (unsigned c = CharOf(vk, e.is_shift_pressed())) io.AddInputCharacter(c);
            take = Takes(io);
        });
        if (take) e.set_handled(true);
    }
    void Button(rex::ui::MouseEvent& e, bool down) {
        int b = -1;
        switch (e.button()) {
        case rex::ui::MouseEvent::Button::kLeft: b = 0; break;
        case rex::ui::MouseEvent::Button::kRight: b = 1; break;
        case rex::ui::MouseEvent::Button::kMiddle: b = 2; break;
        default: break;
        }
        if (b < 0) return;
        With([&](ImGuiIO& io) { io.AddMousePosEvent((float)e.x(), (float)e.y()); io.AddMouseButtonEvent(b, down && Wants()); });
    }

    void UpdateTexture(ImTextureData* t) {
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
            std::vector<uint8_t> rgba((size_t)t->Width * t->Height * 4);
            const uint8_t* src = (const uint8_t*)t->GetPixels();
            if (t->Format == ImTextureFormat_RGBA32) memcpy(rgba.data(), src, rgba.size());
            else for (size_t i = 0; i < (size_t)t->Width * t->Height; ++i) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = src[i]; }
            auto tex = drawer_->CreateTexture((uint32_t)t->Width, (uint32_t)t->Height, rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
            if (!tex) return;   // (no device yet: tried again next frame)
            auto it = textures_.find(t);
            if (it != textures_.end()) dying_.emplace_back(frame_, std::move(it->second));
            t->SetTexID((ImTextureID)(uintptr_t)tex.get());
            textures_[t] = std::move(tex);
            t->SetStatus(ImTextureStatus_OK);
        } else if (t->Status == ImTextureStatus_WantDestroy && t->UnusedFrames > 0) {
            auto it = textures_.find(t);
            if (it != textures_.end()) { dying_.emplace_back(frame_, std::move(it->second)); textures_.erase(it); }
            t->SetTexID(ImTextureID_Invalid);
            t->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    void Render(rex::ui::UIDrawContext& dc, ImDrawData* dd) {
        drawer_->Begin(dc, dd->DisplaySize.x, dd->DisplaySize.y);
        for (int n = 0; n < dd->CmdListsCount; ++n) {
            const ImDrawList* cl = dd->CmdLists[n];
            rex::ui::ImmediateDrawBatch batch;
            batch.vertices = reinterpret_cast<const rex::ui::ImmediateVertex*>(cl->VtxBuffer.Data);
            batch.vertex_count = cl->VtxBuffer.Size;
            batch.indices = cl->IdxBuffer.Data;
            batch.index_count = cl->IdxBuffer.Size;
            drawer_->BeginDrawBatch(batch);
            for (const ImDrawCmd& cmd : cl->CmdBuffer) {
                if (cmd.UserCallback) {
                    if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(cl, &cmd);   // (gfx_rex's are no-ops)
                    continue;
                }
                if (!cmd.ElemCount) continue;
                rex::ui::ImmediateDraw d;
                d.primitive_type = rex::ui::ImmediatePrimitiveType::kTriangles;
                d.count = (int)cmd.ElemCount;
                d.index_offset = (int)cmd.IdxOffset;
                d.base_vertex = (int)cmd.VtxOffset;
                d.texture = reinterpret_cast<rex::ui::ImmediateTexture*>((uintptr_t)cmd.GetTexID());
                d.scissor = true;
                d.scissor_left = cmd.ClipRect.x - dd->DisplayPos.x;
                d.scissor_top = cmd.ClipRect.y - dd->DisplayPos.y;
                d.scissor_right = cmd.ClipRect.z - dd->DisplayPos.x;
                d.scissor_bottom = cmd.ClipRect.w - dd->DisplayPos.y;
                drawer_->Draw(d);
            }
            drawer_->EndDrawBatch();
        }
        drawer_->End();
    }

    OverlaySetup s_;
    std::unique_ptr<rex::ui::ImmediateDrawer> drawer_;
    ImGuiContext* ctx_ = nullptr;
    std::chrono::steady_clock::time_point last_;
    uint64_t frame_ = 0;
    std::unordered_map<ImTextureData*, std::unique_ptr<rex::ui::ImmediateTexture>> textures_;
    std::deque<std::pair<uint64_t, std::unique_ptr<rex::ui::ImmediateTexture>>> dying_;
};

void OverlayDeleter::operator()(Overlay* o) const { delete o; }
OverlayPtr CreateOverlay(const OverlaySetup& setup) {
    OverlayPtr o(new Overlay(setup));
    if (!o->Init()) return nullptr;
    return o;
}
ImGuiContext* OverlayContext(Overlay* overlay) { return overlay ? overlay->context() : nullptr; }

} // namespace reui_rex
