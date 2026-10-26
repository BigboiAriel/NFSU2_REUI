// UI editor: browse the game's own UI textures (every texture pack in GLOBAL, FRONTEND and LANGUAGES) and fonts
// (the FEng fonts in LANGUAGES\English.bin / GLOBAL\GLOBALA.BUN), and build an overlay out of them.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <functional>
#include <vector>

namespace uied {
    // gameDir: the folder SPEED2.EXE is in; layoutPath: where the overlay is saved
    void Init(const std::string& gameDir, const std::string& layoutPath, void (*log)(const char* line));
    void SetDevice(IDirect3DDevice9* dev);   // every Present, before the ImGui frame is built
    bool WantsFrames();                      // UniPad: something to draw even with the panel closed
    void Panel();                            // the "UI editor" tab
    // game modes, each with its own overlay layout
    enum Mode { FREEROAM = 0, RACE, DRAG, DRIFT, OUTRUN, NMODES };
    void DrawOverlay(int mode);              // every frame, after NewFrame; mode: one of Mode, -1 = not driving (menus)
    void Release();                          // device going away for good: drop our textures
    // {token} values for text elements (fps, name, speed, ...); return false for an unknown token
    extern std::function<bool(const std::string& key, std::string& out)> tokens;
    // leaderboard rows: how many the layout shows, and whether they scroll so you're always on the last one
    extern int boardRows; extern bool boardFollow;
    // UniPad: menu workspace (no game-mode layouts, race import or in-game overlay); Test = Preview
    extern bool menuMode;
    // a captured FEng object (UniPad's "Capture current screen"): positions from the screen centre in 640x480
    // units, images by their centre, text by its justification (1 centre, 2 right, 4 middle, 8 bottom)
    struct ImportItem {
        bool text = false; unsigned res = 0;     // texture / font name hash (bStringHash)
        float x = 0, y = 0, w = 0, h = 0, scale = 1, u0 = 0, v0 = 0, u1 = 1, v1 = 1, rot = 0 /* degrees clockwise */, col[4] = {1, 1, 1, 1};
        int just = 0, layer = 0; std::string str, name;
        std::string texName;                     // the game's own name for the texture (from its TextureInfo)
    };
    // UniPad: textures the editor's file scan doesn't have (made or loaded by the game) drawn from the game's
    // live copy, by name; nullptr when it isn't loaded right now. *su / *sv: the picture's share of the texture
    // (the game pads textures to powers of two)
    extern std::function<IDirect3DTexture9*(const std::string& name, float* su, float* sv)> liveTexture;
    // UniPad: controller button icons, as image textures named "icon:<family>:<key>" ("icon:auto:South" = the
    // active controller's family, so it switches between Xbox / PlayStation / ... art). Families and keys by index.
    extern std::vector<std::string> iconFamilies, iconKeys;
    extern std::function<IDirect3DTexture9*(int family, int id, int pixels)> iconTexture;   // family -1 = the active controller's; pixels = drawn size
    extern std::function<std::string(int family, int id)> iconLabel;           // the family's name for it ("Cross")
    extern std::function<int(const std::string& gameTexture)> iconForGameTexture;   // captured UniPad icon -> id
    extern std::function<void()> iconReload;
    // adds them to the layout being edited under `group` (once the game's textures / fonts are read)
    void Import(const std::string& group, const std::vector<ImportItem>& items);
    // test: show the layout being edited right now (menus, free roam), with sample race values when there's no race
    extern bool testMode;

    // ---- menus (UniPad): a top-level group made a "menu page" holds rows (groups inside it made widgets: button,
    // slider, picker) and a highlight that follows the selected row; the host drives it with the pad / keyboard.
    struct Setting {                       // something a widget can change, supplied by the host
        std::string key, label;            // key: saved in the layout
        int kind = 0;                      // 0 number (slider), 1 choice (picker), 2 action (button)
        float min = 0, max = 1, step = 0.05f;
        std::function<float()> get;                         // number, or choice index
        std::function<void(float)> set;
        std::function<std::vector<std::string>()> choices;  // choice
        std::function<std::string(float)> format;           // number -> text (default: percent of the range)
        std::function<void()> run;                          // action
        // kind 3 (binding): a button that, accepted, waits for an input on the controller ("???" meanwhile)
        std::function<int()> icon;                          // icon id of what it's bound to now (-1: none)
        std::function<std::string()> current;               // its name ("Left stick")
        std::function<void()> beginCapture;
        std::function<int(double dt)> capture;              // each frame while waiting: 0 waiting, 1 bound, -1 cancelled
    };
    extern std::vector<Setting> settings;
    enum Sound { SND_UP, SND_DOWN, SND_LEFT, SND_RIGHT, SND_SLIDE_LEFT, SND_SLIDE_RIGHT, SND_ACCEPT, SND_BACK, SND_ERROR };
    extern std::function<void(int sound)> menuSound;
    extern std::function<void()> menuChanged;          // a setting was changed (the host saves on close)
    struct MenuInput { bool up = false, down = false, left = false, right = false, accept = false, back = false; };
    bool MenuOpen(const std::string& page = "");       // "" = the start page; false if there's no such page
    void MenuClose();
    bool MenuIsOpen();
    void MenuFrame(const MenuInput& held, double dt);  // every frame while open: held buttons (repeat is handled here)
    std::vector<std::string> MenuPages();
}
