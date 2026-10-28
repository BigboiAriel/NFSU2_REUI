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

- **Minimap (standalone, done 2026-10-27, matched to stock in game).** An image with "minimap" on draws the track map
  around the car from live values (TRACKMAP<track>, the car's position and heading), stock behaviour: the game's
  zoom, round, heading-up or north-up as the game's radar option says, laid over at the game's ~72% opacity (the
  map texture has no alpha of its own). The game's own icons (AI racers, shops,
  events, your car) from the stock component's icon list, with its rules (dropped past 1 minimap width, 10 in a few
  free-roam states; held on the rim at 0.55). Frame (MINIMAP_BORDER x4) and north arrow ({map_north}) are plain images: the
  "Minimap" prefab has all of it, flagged replace:minimap. Free roam and races. The multiplayer mod's players come
  in through REUI_AddMapMarkers, and it stops drawing its own arrows while REUI_MinimapShown.
  *Future:* zoom out with speed; a north-up setting on the element whatever the game's option (its arrow turns
  instead). Not yet: the start-line / checkpoint ring (types 1-2 and TrackMapTargetRing are drawn elsewhere).
- **Colour tokens (built 2026-10-27).** Elements can take their colour (and my-row colour) from a named token
  instead of a literal: `colortoken=<name> <opacity>` (the resolved `color=` is still saved for older builds).
  Built in: the game's HUD palette (hud.text, hud.accent, hud.accent_soft, hud.line, hud.shade, hud.orange,
  hud.red, hud.bright_green) and live ones (car.paint1-3 = tach_paint_N). A token can follow any live value giving
  "R G B". Layout header `token=<name>;<r g b a>;<live>;<label>` (built-ins only when changed); prefabs carry the
  tokens they use. Editor: token picker on every element's colour, a Colour tokens table (edit, live value, uses,
  reset / delete), and "Repeated colours" to turn repeated literal colours into tokens in one click. Mods:
  REUI_SetColorToken. Resolved once per frame before drawing (menus' selected-row colours still apply on top).
  The built-in prefabs use them. *Later:* theme files (a token set to switch: stock, high contrast, colour-blind).
- **Grid menu widget (built 2026-10-27).** A menu row of cells, columns x rows shown, over a width x height (cell =
  width / columns by height / rows); its pieces are the first cell, repeated (and stretched to the cell when "stretch
  pieces" is on). More items than fit scroll up and down smoothly; optional own scroller (roles grid scroller tab /
  track). Items: a choice setting (picking sets it), its own list ("label;icon;action": page:<page> / back / close),
  or live values "list:<key>" (<key>.count, <key>.N, <key>.N.icon) for other mods' lists. Text {item} {i} {count};
  role "item icon"; "selected row only" = the selected cell. Arrows move inside it and leave it at its edges. Editor:
  "Add grid" on a page (4 x 3, 18 sample items) and faded ghost cells while laying it out. Rows with fewer items than
  Placed on an origin point - the grid's centre as placed, not adjustable by hand (new grids start centred): left puts
  the grid's left edge on it, centre its middle, right its right edge, and it's always centred on it up and down; resizing / columns / rows / switching alignment re-place it there, dragging the
  grid moves the origin with it, and rows with fewer items line up the same way (up / down keep the column on screen). Resizing (width / height
  sliders, columns, rows) resizes the cell's pieces with it when "stretch pieces" is on, so the editor shows the cell
  as drawn. Editor: a picked grid outlines every cell (a picked piece: its copy in each cell); clicking a faded cell
  picks the grid; the grid follows its pieces when they all move together. Layout: widget
  `grid=cols visibleRows w h x y cellW cellH stretch align originX originY spacingX spacingY totalRows`.
- **Grid tiles (2026-10-28).** Spacing across / down between tiles (tiles keep their size and move apart: the grid
  grows by the gaps, around its origin); text in tiles keeps its size when the grid is resized
  (only its place and box width follow); total rows (rows of tiles there are, items past them hidden; 0 = as many as the
  items need) and visible rows (on screen, the grid's height) - more total than visible scrolls up and down.
- **Per-tile grids (2026-10-28).** Each tile (cols x total rows, top-left to bottom-right, named Button 1, 2, 3...)
  has its own name, kind (Button / Slider / Picker (Left / Right)), what it changes (buttons: nothing / go back / close /
  open a page / an action setting; sliders: a number setting or own min / max / step; pickers: a choice setting or own
  choices) and description (the page's description text shows the selected tile's). Sliders / pickers: A takes hold,
  left / right change it, B lets go (B doesn't go back a page while held). Pieces by role per tile: value text on
  sliders / pickers only, fill bar + tab on sliders (the fill piece's full width is the range), arrows on pickers;
  `{value}` in any text. Panel: columns | visible rows | total rows; width | height | align; spacing across | down; tile
  (rename under it) | widget | changes; description; live list (values) instead of tiles. Stretch pieces always on;
  "from the pieces", items box, item count and grid-level changes / description / detect roles removed. The picked tile
  is outlined yellow in the editor. Older grids: their items become tiles ("Item N" -> "Button N"). Layout: after the
  `grid=` line, `tile=<label>` then `ttype / tbind / taction / ticon / tdesc / tchoices / tnum` for that tile.
  Each tile also has an icon (texture name / icon:auto:<button> / vec:<file>) for the grid's item icon piece, and a
  button tile can be a binding ("bind: ..." in changes): A waits for an input ("???" pulsing), {binding} / {input} in
  its texts, its binding icon pieces (or its item icon piece, when it has no icon of its own) show what it's bound to.
  New grids default to menu_big's grid: 1 column, 8 visible / 8 total rows, 550 x 293.9 (550 x 30 tiles), spacing down
  7.7, centred, with its cell / selected / icon / centred label pieces. A tile made a slider / picker / binding gives the
  grid pieces for that kind (once; "add its pieces" if they're missing), copied from the older rows - UniPad Options menu
  / Steering Deadzone (label, bar outline, fill, tab, % text), Active Gamepad (label, arrows, value), Gamepad Bindings
  menu / Steering Binding (label, binding icon); any row of that kind if those are gone, built-in copies if none - spread
  across the tile (row width over the tile less a 4% margin; bars stretch) and shown on that kind only; the grid's own
  label / icon stop showing on it. Pieces have "shows on: buttons / sliders / pickers / bindings" (element key
  `tilekinds=` bitmask 1 / 2 / 4 / 8). In the editor the first tile's pieces show as the panel's picked tile would.
  Grid panel "box backgrounds": shows / hides the grid's Cell / Cell selected pieces (by name). Grid pieces' "selected row
  only" reads "selected tile only" (a highlight of your own that shows on the selected tile).
  A kind counts as having its pieces by its fill bar / arrows / binding icon (fixed 2026-10-28: the grid's own label,
  shown on the other kinds, made pickers / bindings look done). Binding tiles: the label on the tile's left, the icon
  just after its text (`afterlabel=1`: moves with each tile's label length, keeps its shape). A binding shown in the
  grid's own icon piece is square. (Changed 2026-10-28: binding tiles are now laid out as the Gamepad Bindings rows - centred label and
  icon at their own fixed distance, the pair centred in the tile - so icons line up; "after the label's text" stays as
  an option per piece.) The quit file (scripts\NFSU2_REUI.quit) doesn't close the game while the editor has unsaved
  changes (logged).
- **Custom menus (2026-10-28).** A bar under Add image ... Undo (Menus workspace): "Add Menu +" when there are none,
  then a button per custom menu and "+". Each is a dedicated page (`[page]` with `dedicated=1`; its group is its name,
  "Menu N" to start). Picking one edits it alone: everything outside it is hidden on screen, in the list and from clicks,
  and Add image / text / shape put new pieces in it (picking it again shows everything). Under the buttons: its name
  (renames the group), Delete menu (confirm), and its page settings (rows shown, menu key opens this, selected colour,
  Add grid, Try it). Its group's properties say it's a custom menu instead of the "menu page" tick. Older pages (groups
  ticked "menu page") are left as they are, deprecated, and aren't in the bar.
  Everything that adds pieces goes into the menu being edited (IntoMenu): Add image / text / shape, prefabs (as a group
  inside it), controller icons, vector art, FNG captures, "Copy its elements into this layout"; the list's "no group"
  drop, the group field and its "add to..." list keep things inside it. The menu is the page, not a group: while it's
  picked its contents are the top of the element list, group names show without it (RelGroup), Alt + click picks the
  outermost group inside it, and the menu's own group can't be picked. With no menu picked, custom menus are hidden
  (screen, list, clicks): the rest of the layout shows.
- **Deprecated (2026-10-27): the row tools.** Build rows, Restack rows, row spacing / first row y, Start a new column,
  column distance, first column x, add to column, Add row (widget kit), row up / down, a row's column, Save as
  template and the Widget kit panel are gone from the editor; grids (menu buttons in rows x columns) replace them.
  Pages made with them still load, save and work.
- **Page highlight role (2026-10-27).** Any element on a page itself (a shape, say) can have the role "page: highlight":
  it moves to the selected row from the row it's laid out on (and across to its column), hidden when that row has
  its own highlight (a grid).
- **Shapes and cutters (built 2026-10-27).** A "shape" element: a rounded rectangle (radius per corner, rotation), a
  stroke inside its edge (stroke colour = the element colour) and a fill (its own colour / token) - menu backings,
  panels, frames. A shape with "cut" on draws nothing and is subtracted from a target shape (by name, or every shape in
  its group), grown by a gap: a true boolean on the distance field, so the stroke follows the cut (notches, windows,
  tab cut-outs). Drawn from a signed distance field at screen size (src/editor/shapes.cpp), stroke and fill as two
  alpha textures coloured when drawn; cached by geometry (moving or recolouring doesn't redraw). Layout:
  `type=shape`, `shape=tl tr br bl stroke`, `fill=r g b a`, `filltoken=`, `cut=gap;target`.
  Gradients (built 2026-10-27): stroke across (outer -> inner side, following the corners' curve)
  or along (around the outline, start offset); fill linear (angle), radial, or from the edge inward over a depth
  (following the curve); stroke inside / centred / outside the edge. Stops editor (drag / add / delete, colour,
  opacity, location, reverse, "glow" preset). Layout: `strokealign=`, `strokegrad=` / `fillgrad=mode angle depth;t r g b a;...`.
  Gradient tokens: each stop can use a colour token (its colour now, times the stop's opacity; `t r g b a@token`), and
  a whole gradient can be a named gradient token (`gradtoken=name;label;stops|...`, element `strokegradtoken=` /
  `fillgradtoken=`); built in hud.glow (clear -> hud.accent -> clear), hud.fade (hud.shade -> clear), hud.edge_light.
  Editor: per-stop token picker, "own stops" / token picker on each gradient (editing a token's stops changes every
  shape using it), "save as token", and a Gradient tokens list in Colour tokens (preview, uses, edit, reset / delete).
  Prefabs carry the gradient tokens they use and their colour tokens.
  Cutters are drawn as shapes themselves (fill, border, gradients) over the hole they make; the cut edge has no
  border of the target's - the cutter's own border outlines it. An empty cut: the cutter's fill and stroke opacity 0.
  (`cutshow=` in old layouts is ignored.)
  *Later:* smooth (rounded) joins where a cut meets the edge; ellipses / other shapes.
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
