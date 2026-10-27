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
    IDirect3DDevice9* Device();
    bool WantsFrames();                      // UniPad: something to draw even with the panel closed
    void Panel();                            // the "UI editor" tab
    // game modes, each with its own overlay layout
    // MENUS: the menu pages' workspace (menu pages, rows, templates); never drawn as a HUD
    enum Mode { FREEROAM = 0, RACE, DRAG, DRIFT, OUTRUN, MENUS, NMODES };
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
    // A game HUD object as it is on screen this frame, by its FEng name hash (the host walks the loaded packages):
    // centre x, y from the screen centre in 640x480 units (y down), size, turn (degrees clockwise), uv, colour, its
    // texture (su / sv: the picture's share of a padded texture). Not found / hidden: false.
    struct GameObject { float x = 0, y = 0, w = 0, h = 0, rot = 0, u0 = 0, v0 = 0, u1 = 1, v1 = 1, col[4] = {1, 1, 1, 1}, su = 1, sv = 1; IDirect3DTexture9* tex = nullptr; };
    extern std::function<bool(unsigned nameHash, GameObject& out)> gameObject;
    // The minimap (an image with "minimap" on): the track map around the car, its icons and other mods' markers,
    // supplied by the host every frame. World = the game's x / y in metres (y north); the map texture covers
    // x0 .. x0 + size east and y0 .. y0 - size south (uv = ((x - x0) / size, (y0 - y) / size)).
    struct MapBlip {
        float x = 0, y = 0;                 // world
        float dirX = 0, dirY = 0;           // heading (world forward); 0, 0 = upright
        IDirect3DTexture9* tex = nullptr;   // the game's icon, or
        std::string texName;                // a texture by name (an icon / "vec:" / game texture), or neither = an arrow
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        float w = 14.5f, h = 14.5f;         // size in the game's units on a 128-unit minimap
        float col[4] = {1, 1, 1, 1};
        bool hideOff = false;               // hidden past the rim (else held on it)
        bool farOk = false;                 // not dropped however far (other mods' markers)
        bool player = false;                // the player's own icon (drawn on top)
    };
    struct MapView {
        IDirect3DTexture9* tex = nullptr; float su = 1, sv = 1;
        float x0 = 0, y0 = 0, size = 1;     // the map texture's world square
        float px = 0, py = 0, bearing = 0;  // the player: position, heading (degrees clockwise from north)
        float span = 0.2f;                  // uv across the minimap at the game's zoom
        bool rotate = false;                // heading-up (the game's radar option), else north-up
        bool alphaOk = true;                // false: the texture's alpha is empty (drawn with the element's alpha)
        float limit = 1;                    // game icons further than this (in minimap widths) are dropped
        std::vector<MapBlip> blips;
    };
    extern std::function<bool(MapView& out)> mapView;   // false: no minimap now (menus, drag races, loading)
    bool MinimapShown(double withinSeconds = 0.3);      // a minimap element was drawn in game lately
    // Element flags other mods add (the multiplayer mod: "hide online", "online only"): a checkbox on every element while
    // the mod is loaded; hide() says whether flagged elements are hidden now. Flags in a layout whose mod isn't loaded
    // keep what they said when it was (hiddenWhenMissing, saved with the layout). group > 0: one flag of a group per element.
    void AddFlag(const std::string& key, const std::string& label, const std::string& tooltip, int group, bool hiddenWhenMissing, std::function<bool()> hide);
    // "Replace stock HUD [x]": a stock HUD piece an element can stand in for. Elements store the flag "replace:<id>";
    // while one is drawn (shown in game: its showif / flags pass), ReplaceWanted(id) is true and the host hides
    // that piece of the game's HUD.
    void AddReplaceTarget(const std::string& id, const std::string& label, const std::string& tooltip);
    bool ReplaceWanted(const std::string& id, double withinSeconds = 0.3);
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
