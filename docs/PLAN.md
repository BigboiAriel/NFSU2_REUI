# NFSU2_REUI — project plan

Goal: one standalone ASI (`NFSU2_REUI.asi`, own repo `BigboiAriel/NFSU2_REUI`) that merges the two UI editors
(nfsu2-multiplayer's and NFSU2-UniPad's) and recreates every in-game HUD element in ImGui, replacing the game's HUD.

Decisions (Oct 25 2026): own ASI mod; replace the game's HUD; scope = car gauges, minimap, race info, free roam /
messages; new GitHub repo (user creates it; sessions can't create repos). Renamed from "NFSU2 UI Editor" to NFSU2_REUI.

## Phases
- **P0 scaffold — done (local).** D3D9 Present/Reset/EndScene hook, ImGui panel (PanelKey F6), FEng package tracking
  (PackageInit 0x54E4A0 / WillUnload 0x51C8C0), screen capture, the editor; `NFSU2_REUI.quit` closes the game.
  Files: src/{dllmain,config,log,panel,host}.cpp, src/game/{d3d9_hook,feng,addresses}, src/editor/uied*.
- **P1 merge — done (local).** UniPad's editor is the superset (multiplayer's hadn't changed since the copy).
  HUD layouts per game mode (Free roam, Racing, Drag, Drift, Outrun) + a **Menus** workspace (menu pages, rows,
  templates; never drawn as a HUD; menu runtime always reads it). Imports: UniPad's NFSU2UniPad_menus.txt -> Menus,
  NFSU2Multiplayer_overlay.txt -> the HUD modes. Without UniPad: MenuKey F7 + arrows / Enter / Esc drive menus.
  Game mode detection as the multiplayer mod's (state 0x8654A4 == 6, race coordinator [0x8900D8] state 4, race type
  0x89E7B0, drag 0x89E7D8 / 0x890108, drift 0x89E7D9 / 0x890104, outrun type 7).
- **P2 interop API.** C exports (no std::function across DLLs): token providers (multiplayer leaderboard / race
  values), settings incl. binding rows (UniPad), icon provider (UniPad's SVG / PNG button icons), menu sounds,
  menu open / close / frame (UniPad's pad + pause-menu item). UniPad and the multiplayer mod detect the module
  and turn their own editors off.
  *Done (src/reui_api.h / reui_api.cpp).* UniPad hands over its settings, binding rows, icons and sounds and drives
  the menus (its F7 key, pad and pause item); the multiplayer mod adds {name} and its race / leaderboard values and
  follows REUI's test mode and leaderboard settings (REUI_EditorState). With no menu pages / HUD layouts of its own
  yet, REUI starts from scripts\NFSU2UniPad_menus.txt / NFSU2Multiplayer_overlay.txt. UniPad no longer turns
  itself off when the multiplayer mod is loaded.
- **P3 live HUD data (RE).** *Car gauges done:* {rpm} {rpm_k} {rpm_max} {rpm_pct} {gear} {reverse} {shift_light} {speedo}
  {speed_unit} {throttle} {nos} {nos_installed} {nos_on} (game/car.cpp, docs/RE_NOTES.md); F6 > Live values lists them.
  Car: speed, rpm, gear, nitrous, speedbreaker, shift light. Race: position, laps,
  leaderboard (the multiplayer mod's raceinfo reader as a base), race / lap / best times, countdown, wrong way,
  results. Free roam: cash, SMS / phone, event icons, messages. Minimap: map texture, player / racer / shop
  positions and heading.
- **P4 replace the HUD + element types.** *Started:* elements can be shown only while a value is set (showif) and
  driven by a value (bind: fill left-right / bottom-top, rotation between two angles).
  Images can follow a stock HUD object (follow = its FEng name hash: the game's place, size, turn, uv, texture and
  visibility every frame) and be clipped to a circle - the minimap is rebuilt that way.
  The stock tachometer is rebuilt standalone: the car's tach art, HUD colour and paints as values, the needle by
  {tach_angle}, the nitrous arc by a turning-half reveal with additive glow.
  Hide the game's HUD per element through the HUD feature mask
  (0x600090, thiscall(hudOwner) -> u64; each component switched by a bit; the multiplayer tester returns 0).
  New element types: needles / rotation by value, fill bars bound to a value, conditional visibility, a minimap
  widget.

- **P5 distribution (later, once REUI is ready).** Light builds of REUI ship with the other mods: a renderer
  without the editor or HUD replacement, which carries the pages built in REUI (decided 2026-10-26).
  - **UniPad:** renders and drives the UniPad pause-menu pages. Option to look at: bake the pages into
    NFSU2UniPad.asi (the layout's `mode=menus` part, embedded at build time), with a menus file on disk overriding
    the built-in copy. Until then, UniPad's own built-in renderer and UI
    editor are left as they are. Standalone UniPad today keeps its older menu-only editor and ships no menu pages,
    so it has no "UniPad Options" item.
  - **Vector art:** the SVGs in REUI\vector (game root) are baked in only once the mod is complete; until then
    they're loose files (prefabs are the only thing baked now).
  - **Multiplayer mod:** renders the player leaderboards and the mod's new dedicated menus. Until then the
    multiplayer mod keeps its own overlay as it is.

## Status
Installed for testing next to UniPad (F6, its own scripts\NFSU2_REUI_layout.txt). Repo not created yet: local tree
/home/claude/nfsu2-reui, pushed once BigboiAriel/NFSU2_REUI exists.
