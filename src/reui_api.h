// NFSU2_REUI's API for other mods (NFSU2-UniPad, the multiplayer mod). Plain C: look the functions up with
// GetProcAddress(GetModuleHandleA("NFSU2_REUI.asi"), "REUI_...") at your first frame (all ASIs are loaded by then).
// Every call is on the game thread. Strings passed in are copied; strings returned by your callbacks must stay valid
// until the next call of the same callback (a static buffer is fine).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define REUI_API_VERSION 1

struct IDirect3DTexture9;
struct IDirect3DDevice9;

// Something a menu row can change. kind: 0 number (slider), 1 choice (picker), 2 action (button), 3 binding (a
// button that waits for an input: icon / current / beginCapture / capture). Unused callbacks may be null.
typedef struct REUI_Setting {
    const char* key;      // saved in layouts ("steer.deadzone")
    const char* label;    // shown in the editor ("Steering deadzone")
    int kind;
    float min, max, step;
    float (*get)(void* ctx);                     // number, or choice index
    void (*set)(void* ctx, float v);
    int (*choiceCount)(void* ctx);               // choice
    const char* (*choice)(void* ctx, int i);
    const char* (*format)(void* ctx, float v);   // number -> text (null: percent of the range)
    void (*run)(void* ctx);                      // action
    int (*icon)(void* ctx);                      // binding: icon id of what it's on now (-1 none)
    const char* (*current)(void* ctx);           // binding: its name
    void (*beginCapture)(void* ctx);
    int (*capture)(void* ctx, double dt);        // each frame while waiting: 0 waiting, 1 bound, -1 cancelled
    void* ctx;
} REUI_Setting;

typedef int (*REUI_TokenFn)(const char* key, char* out, int size);   // {key} in texts -> value; 0 = not yours
typedef IDirect3DTexture9* (*REUI_IconTexFn)(IDirect3DDevice9* dev, int family, int id, int pixels);   // family -1 = active pad's
typedef const char* (*REUI_IconLabelFn)(int family, int id);
typedef int (*REUI_IconGameTexFn)(const char* gameTextureName);   // a captured texture that's one of your icons -> id, else -1

typedef int (*PFN_REUI_Version)(void);
typedef void (*PFN_REUI_AddTokenProvider)(REUI_TokenFn fn);
typedef void (*PFN_REUI_AddSetting)(const REUI_Setting* s);
typedef void (*PFN_REUI_SetSound)(void (*fn)(int sound));   // 0 up 1 down 2 left 3 right 4 slide left 5 slide right 6 accept 7 back 8 error
typedef void (*PFN_REUI_SetMenuChanged)(void (*fn)(void));  // a setting changed through a menu (save on close)
typedef void (*PFN_REUI_SetIcons)(int families, const char* const* familyNames, int count, const char* const* keys,
                                  REUI_IconTexFn tex, REUI_IconLabelFn label, REUI_IconGameTexFn forGameTexture, void (*reload)(void));
typedef void (*PFN_REUI_SetMenuDriver)(int external);       // 1: you drive the menus (key, pad); REUI's own menu key is off
typedef int (*PFN_REUI_HasMenuPages)(void);
typedef int (*PFN_REUI_MenuOpen)(const char* page);         // "" / null = the start page; 0 if it can't open now
typedef void (*PFN_REUI_MenuClose)(void);
typedef int (*PFN_REUI_MenuIsOpen)(void);
typedef void (*PFN_REUI_MenuFrame)(int up, int down, int left, int right, int accept, int back, double dt);   // held buttons
// the editor's state a value provider may need: testing a layout (no real race: show sample values), and the
// leaderboard's row count / follow-me setting. Null pointers are skipped.
typedef void (*PFN_REUI_EditorState)(int* testMode, int* boardRows, int* boardFollow);
// An element flag (a checkbox on every element while your mod is loaded): hideNow(ctx) returns 1 while flagged elements
// are to be hidden. hiddenWhenMissing: what flagged elements do when your mod isn't installed. group > 0: only one flag
// of the group per element (e.g. "hide online" / "online only").
typedef int (*REUI_FlagFn)(void* ctx);
typedef void (*PFN_REUI_AddFlag)(const char* key, const char* label, const char* tooltip, int group, int hiddenWhenMissing, REUI_FlagFn hideNow, void* ctx);

// Minimap markers (REUI's minimap element, "minimap" on an image): a provider fills out[0..max) with markers in the
// game's world (metres: the car's x / y, as Car+0x50 / +0x54) and returns how many. Asked every frame a minimap is
// drawn. Markers further out than the map shows are held on its rim (flags bit 0: hidden instead).
typedef struct REUI_MapMarker {
    float x, y;           // world position
    float dirX, dirY;     // heading: the world forward vector (e.g. the car matrix's first row); 0, 0 = none
    unsigned color;       // 0xAARRGGBB
    float size;           // 1 = a stock car icon's size
    int flags;            // bit 0: hidden off the map (else held on the rim); bit 1: drawn upright (no heading)
    const char* icon;     // a texture: a game texture name ("MINIMAP_ICON_CAR": one the minimap itself shows is drawn
                          // at the game's size for it, size x 1), "vec:<name>" (REUI\vector), or null / "" = REUI's
                          // arrow (outlined triangle). Tinted by color.
} REUI_MapMarker;
typedef int (*REUI_MapMarkerFn)(REUI_MapMarker* out, int max, void* ctx);
typedef void (*PFN_REUI_AddMapMarkers)(REUI_MapMarkerFn fn, void* ctx);
// 1 while a REUI minimap is on screen (drawn in the last 0.3 s): put your markers on it instead of drawing your own
typedef int (*PFN_REUI_MinimapShown)(void);

// A colour token for your pages (REUI's "Colour tokens": elements take a colour by its role, so a look changes in one
// place): name like "mp.player", a label for the editor, its default colour 0xAARRGGBB. The layout may change it;
// REUI's built-in tokens (hud.*, car.*) can't be redefined. Call at your first frame.
typedef void (*PFN_REUI_SetColorToken)(const char* name, const char* label, unsigned argb);

#ifdef __cplusplus
}
#endif
