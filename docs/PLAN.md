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
  HUD layouts per game mode (Free roam, Racing, Drag, Drift, Outrun) + a **Menus** workspace (custom menus of grids /
  carousels; never drawn as a HUD; menu runtime always reads it). Import: NFSU2Multiplayer_overlay.txt -> the HUD
  modes (UniPad's menus file import was removed 2026-11-03). Without UniPad: MenuKey F7 + arrows / Enter / Esc drive menus.
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
- **Redrawn minimap art (2026-10-31).** `REUI\vector\minimap\<game texture name>.svg` replaces that texture on REUI's
  minimap: the game's icons (blip `gameName` from its TextureInfo) and other mods' markers by icon name (the whole SVG,
  sharp at its size, tinted as before; names with no game texture work too - the multiplayer mod's
  MINIMAP_ICON_GARAGE for players in a shop). A redrawn MINIMAP_MASK sets the map's opacity (its first shape's
  opacity; stock 72%). The Minimap prefab's frame is one 160 x 160 ring (vec:minimap/MINIMAP_BORDER) centred on the map
  instead of four quarters, the north marker vec:minimap/MINIMAP_NORTH_INDICATOR.
- **Minimap page (2026-10-31).** A "Minimap" section in the editor: a row per minimap texture (this track's map
  TRACKMAP<n>, MINIMAP_MASK / BORDER / BORDER_2 / NORTH_INDICATOR / GPS_SELECTOR / the ICON_ textures, other mods'
  markers once seen) with the game's picture, "Replace with" (Automatic = REUI\vector\minimap\<name>.svg if there,
  else stock / the game's own / any vector art / the texture selected in Textures) and what's drawn now. Saved with the
  layout as `mapart=<name>;<texture>`. Applies to the minimap's icons, the map (its own alpha; same world square) and
  image elements whose texture is one of the names or vec:minimap/<name>; a replaced MASK's first shape sets the map
  opacity. Vector art names match without regard to case; the art list is rescanned when the section opens.
  In tabs: Blips (the minimap textures and other mods' markers), Maps (free roam's TRACKMAP4000 - the minimap at every
  stage - and the career World Map's five stage maps TRACKMAP4500..4504, regions not open yet dimmed; that screen is
  the game's, not REUI's, so their replacements don't show yet) and Tracks (each race's TRACKMAP<id> with its name
  from the track table [0x883DA0] x [0x883DA4], 0x128 each: +0 name, +0x8A id; searchable).
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
- **Whole-row sliders / pickers (2026-10-28).** In a grid of more than one column, a slider or picker tile takes a row
  of its own (GridLayout: the row before it ends early, the next tile starts a new row); its pieces spread across the
  row (places scaled to the row's width; bars / boxes - wider than 3x their height - stretched; the rest keep their
  size). Rows can scroll past the total rows when this adds rows. Up / down go to the tile under the same column (a
  whole-row tile remembers the column you came from); left / right on one leave the grid (or change it while held).
  Slider / picker pieces (tilekinds only 2 / 4: RowPiece) are laid out across the grid's whole row, not a tile: made
  across the row, drawn as they are on whole-row tiles, and on resize their places follow the grid's width, bars (wider
  than 2x their height) stretch, the rest keep their size - changing the columns no longer squeezes them. Grid line
  field 16 = 1 marks this; older grids' slider / picker pieces are spread from the tile to the row on load.
- **Pause menu replacement (2026-11-03).** A page's "replaces the pause menu" box (`pausemenu=1`, one page at a time;
  export REUI_PausePage; set with the "replaces" dropdown in the menu button's right-click menu, 2026-11-04). UniPad (menu driver) opens that page whenever the game's pause screen comes up (the stock one
  stays underneath, hidden), Back on it picks the stock resume item; tiles take UniPad's "Pause: ..." actions (Resume,
  Restart race, Jump to Garage, Options, World map, Replay, Statistics, Quit, Free Cam), which close the page and pick
  the stock item (its current item set, then the pause screen's own accept: confirm dialogs and screens as stock).
  UniPad presses accept through the game's input and gives the press the item's own FE button (the scroller only runs
  an item whose button the accept carries, React 0x50D980 vs icon+0x10).
- **Sub menus (2026-11-03).** A custom menu can sit inside another (its group inside the other's: "Pause 1/Unipad
  Options"), any depth. A page's pieces are only those whose nearest page group is it (OwnerPage), so a menu never
  draws, lists or edits its sub menus' pieces. The menu bar shows two rows: the menus at one level (the top, or inside
  g_menuRoot) and the picked one's sub menus, each with "+" (a new menu there); picking a sub menu moves down a level
  (its row becomes the first), "<" at the end of the first row goes back up; a button with sub menus reads "Name >".
  Right-click a menu's button: Move into (any other custom menu, not itself / its own sub menus) or Move to the top
  level - a rename of its group, so its pieces, rows, sub menus and every "open" link to it go along (no opener tile
  is added). The same menu has its name box (Enter renames; it stays where it is) and Delete menu (a confirm
  sub-item; its sub menus go too) - no longer under the rows.
- **Pause Options page (2026-11-04).** A page's "replaces the pause Options menu" box (`pauseopts=1`, one page;
  export REUI_PauseOptionsPage): UniPad opens it whenever the pause menu's Options screen (UI_PauseOptionsMain: Audio /
  Display / Gameplay / Player, OPTIONS_ICON_AUDIO / _DISPLAY / _GAMEPLAY_SETTINGS / _PLAYER_SETTINGS) comes up - so the
  pause page's Options tile (pause.options) lands on it. Its tiles take "Pause Options: ..." (pauseopt.audio / display
  / gameplay / player: the stock item picked, its settings panel opens) or open a page; Back on it presses the game's
  back (to a new pause screen, where the pause page opens again). A pause screen left for its Options isn't stood in
  for again (only the new one built on the way back). Menu cutters cut only shapes in their own menu (OwnerPage).
- **Confirm page (2026-11-04).** "replaces: Yes / No confirm dialogs" (`confirmdlg=1`, REUI_ConfirmPage): UniPad opens
  that page whenever the game's dialog screen (vtable 0x79C88C, handler 0x54C050; UI_InGameDialog / GenericDialog
  packages: Jump to Garage?, Quit?, Restart race?...) is up with 2 buttons. {dialog.text} (a token UniPad provides)
  is its question (the builder 0x558680's format + arguments; ^ = new line). "Dialog: Yes / No" tiles answer it
  (Button1's / Button2's result into +0x50; it closes on its next update and sends it to its parent); Back = the
  dialog's own Back (No when it has none). Dialogs with 1 or 3 buttons stay the game's. A dialog over the pause page
  takes over; once answered the pause page comes back.
- **World Map page (2026-11-04).** "replaces: the World Map" (`worldmap=1` on the page, REUI_WorldMapPage): UniPad
  opens that page whenever the game's World Map screen (vtable 0x797F20, handler 0x4E08E0) is up and sends the
  player's buttons to it as its own messages, so the game keeps its list, places, view, shown types (saved with the
  career) and GPS; the page draws its state from tokens. An image element ticked "world map" (`worldmap=1`, icon size
  `mapblip`, your car `mapplayer`) draws the whole track map (TRACKMAP<track>, Minimap page art) in its box, square:
  the window map.view.uv ("u0 v0 frac": Full Map, or the region views City Core / Beacon Hill / Jackson Heights / Coal
  Harbor East / West - the game's table 0x7F42C8, open by stage), the game's icons by world position (every one: the
  host's worldView, no minimap needed) less the types in map.hidden, other mods' markers, your car on top, and the
  selected place's ring (map.sel "x y"; MINIMAP_GPS_SELECTOR art). Blips carry their game type. Live-list grids:
  <key>.N.color tints an item's icon, <key>.N.on shows its "item on" / "item off" pieces (roles on / off), <key>.sel
  is its selection (kept by the mod). UniPad's tokens: map.legend (Your Car, Opponent Car, then the game's rows - label
  from the game's text, icon + colour from the map icons of that type, on = shown), map.legend.sel, map.view (the
  view's name), map.view.uv, map.sel, map.hidden. Buttons: up / down rows, left / right that type's places, X show /
  hide, LT / RT view, A GPS (closes the map), B back (keys: arrows, X, Enter, Page Up / Down, Esc).
- **Group cuts, groups in the active menu (2026-11-04).** A cutter's "cuts into" can be a whole group (`cut=<gap>;group:<group>`):
  every shape in that group and the groups inside it (listed apart from its own group, which stays the first choice;
  renaming / moving the group carries the target). With a custom menu picked, the multi-selection's "put them in a
  group" lists that menu's groups, "or a new group" makes it inside the menu and "(out of its groups)" leaves them at
  the menu's top.
  Grid / carousel tiles: each tile's pieces are placed first and drawn together, so its shapes are cut by its own copies
  of the grid's cutters (and by cutters outside the grid), not by the first tile's.
- **Menu fades, custom menus on / off (2026-11-04).** `menufade=<s>` (layout header; 0.15 to start, 0 = off): a page
  fades in as the menu opens, out as it closes (drawn under the HUD, the game showing through) and pages cross-fade
  when one opens another or goes back - the page's vertices' alpha scaled. `custommenus=0`: no page stands in for the
  game's menus (REUI_PausePage / PauseOptionsPage / ConfirmPage / WorldMapPage give ""), so the game's own show; the
  menu key's page still opens. Both at the top of the menu bar.
- **Icons of bindings (2026-11-04).** `icon:bind:<game event>` (e.g. icon:bind:FENG_LTRIGGER): the controller's icon for
  what that event is bound to now - the menu driver's token {bindicon.<event>} (UniPad: the event's first pad binding
  as an icon key), LT / RT for the triggers when there's none. The World Map page's LT / RT use them, and UniPad reads
  the view switch from the FENG_LTRIGGER / FENG_RTRIGGER bindings (pad and keyboard).
- **World Map GPS and animations (2026-11-04).** From the stock UI_InGame_WorldMap.fng (GLOBALB.BUN, JDLZ block, read
  with the FEngLib format): the selected place's two GPS selector rings loop 1.6 s (28.75 -> 70 units against the
  353-unit map, fading in then out; the second 0.4 s behind), now drawn that way. An "off" live list item's icon and
  label go to the game's alpha 60/255 (0x496920). `playon=<value key>` (editor: "play on"): an element's animation
  starts over each time that value comes on; UniPad's {bindheld.<event>} is "1" while that binding is held, so the
  World Map's LT / RT icons play the stock view arrows' press script (0x0280164F: 400 ms, 1.3x at 200 ms, tinted
  0.79 0.96 0.54). UniPad: the World Map's one-button "Deactivating GPS" dialog (0x558610 -> 0x558490, Button3 result
  0x34DC1BEC, no Back) gets the confirm page too - Yes turns the GPS off, No sends the map's own 0xB4623F67 ("leave
  it", ends its wait); other one-button notices: either answer is their one button. {map.gps} / {map.gps.action}
  ("Activate GPS" / "Deactivate GPS", from 0x838590+0x29 and 0x501100).
  Pages over pages: REUI_MenuOpenOver opens a page over the one shown (drawn under it, fading with it), Back /
  REUI_MenuPopOver returns to it, REUI_MenuTopPage names the top one - UniPad opens the confirm page over the World Map
  page for the GPS questions, as the game draws its dialog over its map. Menu pages draw copies of their elements, so
  an element's animation start is now copied back (play on works there). The trigger icons idle at 0.588 alpha (the
  stock arrows' 150/255) and flash to full on the press (their own colour - no tint).
- **Shapes on the graphics card (2026-11-04).** shapes_gpu.cpp: the shape (rounded box, stroke inside / centre /
  outside, fill and stroke gradients, up to 16 cuts with their gaps, cut edges without the stroke) worked out per screen
  pixel by a ps_3_0 pixel shader with a vs_3_0 vertex shader repeating ImGui's DX9 projection - the same maths as
  shapes.cpp, one screen pixel of soft edge from the derivatives. Drawn as ImGui quads between callbacks that set the
  shaders and the shape's constants (kept per frame, cleared at SetDevice). Compiled on a background thread from
  d3dcompiler_47.dll (_43 as a fallback) the first time a shape is drawn; until then, if it fails (logged), or for a
  shape with more than 16 cuts / 8 stops per gradient, the CPU textures as before. Log: "shapes: drawn on the graphics
  card".
- **Race start page; the game's texts on any page (2026-11-04).** `{fe.<8 hex>}`: a FEng string object's text now (any
  loaded package, newest first; its buffer, else its label's language text); texture `fe:<8 hex>`: the picture a FEng
  image shows (nothing while the game hides it) - a kitbashed page can show a screen's own texts and pictures. Text
  elements: `wrap=1` breaks the text to its box width. "replaces: the race start dialog" (`racedlg=1`,
  REUI_RaceDialogPage): UniPad opens the page in place of UI_EngageRaceDialog (vtable 0x78F988, handler 0x499C00,
  filled by 0x4B53C0) with "Race: Accept" / "Race: Decline" (Back declines), the stock dialog hidden from its
  package's load. Its texts: {race.from} ("From: <giver>", 0xFECED617), {race.subject} ("Subject: <type>",
  0x2C167533), {race.type} (0x15E1351B), {race.distance} (0x080C9DAA), {race.purse} (0xB54B81A7), {race.message}
  (0xDE075B62); texture {race.map} (fe:0F365871, the race's TRACKMAP<n>). The layout's "Race Start" page was
  kitbashed from the World Map's box, legend bars and dark list panel, the Engage panel's icons and the Confirm Page's
  buttons.
- **Animated text changes (2026-11-05).** A text's "animate changes" box (`swapanim=1`): when its text changes, the
  old one plays the game's help bar Fade_Out while the new one plays Fade_In (both 320 ms, straight lines, from the
  name / cash strings' scripts in the game's packages: size across 1 -> 0.5, down 1 -> -1/6 (gone at ~274 ms), alpha
  1 -> 0; Fade_In backwards), about its alignment point across / middle down. Not on its first showing. With UniPad's
  {bar.ticker} (the game's ticker: name 0.75 s, cash 5 s) it's the stock profile name / cash swap. Layout: Shared/Glyphs
  Bar/Cash Block's Cash.
- **Glyph bars (2026-11-05).** A menu's right-click menu has "Glyph bar: ...": pick a group (any menu group that isn't
  a page or a grid) to draw over that menu and every sub menu under it (a sub menu can pick its own; "its parent
  menu's" / "Off"). Saved per page as `glyphbar=<group>`; follows renames / moves. The bar's group isn't drawn as part
  of the page it lives in - only over the menus that use it, on top (the page on top of a stack, and a page fading
  out); its grids aren't menu rows (not selectable). Editing a menu that uses a bar from elsewhere shows it as it will
  be (edit it where its group lives). Layout: Garage uses "Garage/Glyphs Bar" (so Garage/Menu 1 does too). Next: the
  bar's glyphs from the menu's context (a live list from UniPad).
- **Garage menu page (2026-11-05, untested).** "replaces: the garage menu" (`garagemenu=1`, REUI_GaragePage): UniPad
  opens it instead of the safehouse's "The Garage" menu (UI_CareerCrib.fng, screen vtable 0x797548). Tiles: "Garage: ..."
  actions pick the game's own items (Return to Explore Mode, World Map, Car Select, Change Parts, Performance Tuning /
  Dyno, View Rewards, Save, Back to Main Menu); Back asks to return to explore mode, as the game's Back does. The game's
  icons by texture hash: #A230CD88 #22F2CEAB #A964095B #511F9D75 #7A9B397A #49D2E3DE #8ADA6FD4 #FC39853F. The layout's
  "Garage" page: the user's Pause Screen copy + a carousel kitbashed from "Pause 1" (8 tiles), its description, "The
  Garage" title, {cash}, Back / Select prompts (all 120 units higher than the pause page's).
- **Multiplayer race start page (2026-11-05).** "replaces: the race start dialog (multiplayer)" (`racedlgmp=1`,
  REUI_RaceDialogMpPage): UniPad opens it instead of the race start page while NFSU2 Multiplayer says we're in a session
  (NFSU2MP_InSession). Tiles: "Race: COOP" (UniPad tells the multiplayer mod NFSU2MP_RaceChoice(1), then accepts), "Race:
  Accept" (= SOLO there: choice 0), "Race: Decline". Race settings rows come from the multiplayer mod (REUI_AddSetting):
  race.ai / race.traffic / race.rubberband / race.collision (On / Off pickers), race.aicount (slider 0-5),
  race.difficulty (Easy / Medium / Hard). The layout's "Race Start Multiplayer" page has Coop / Solo / Race Settings;
  "Race Start Multiplayer/Race Settings" is a copy of the UniPad Options page with those six rows.
- **Cutters and layers (2026-11-04).** A cutter cuts only shapes on its own layer or lower; a shape on a higher layer
  sits over it uncut (the race start page's buttons and info rows were cut away by the panel cutter behind them).
- **No hitch when a menu opens (2026-11-04).** Shape pictures (computed per pixel at screen size) were let go 3 s after
  last drawn, so each menu open remade every panel in one frame. Now kept 10 minutes (128 MB / 192 entries, least
  recently drawn first) and made on up to 8 threads. Still to watch: a shape whose cutters move relative to it (a
  carousel sliding under an outer cutter) is a new picture every frame of the slide.
- **Carousel (2026-11-03).** A widget type of its own ("Carousel (scrolls left / right)", `type=carousel`, saved with the
  grid's `grid=` line - cols = slots on screen, rows 1, totalRows = its tiles - and `carousel=centred wrap`): a grid's
  tiles on one row, left / right scroll it. Centred (default): the selected tile stays in the middle slot and the row
  slides under it (the game's pause menu; the ends left empty unless "wraps around"); off: the cursor moves along the
  slots and the row scrolls at the ends. Tiles sliding past the end slots are cut half a gap out. Its "left / right
  arrow" pieces stay at the row's ends (light up when pressed; resizing keeps them at the ends) and its scroller tab
  (gtab) moves across its track (gtrack) by the selected tile. Tiles are buttons. "Add carousel" (page panel) lays one
  out as UI_pause.fng: 126 x 62 slots 159 apart about (0.3, -14.3), icon backing + U2_ICON_HIGHLIGHT on the selected,
  118 x 59 icons, U2_MENU_ARROW_WHITE arrows 26 x 26 at 150/255 (-250.3 / 259.1, y -12.8), the scroller's 562 x 8
  backing at y -52 and a 16 x 16 U2_SCROLLER_TAB.
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
  Pages made with them still load, save and work. **Removed (2026-11-03):** the older menu system's code is gone -
  groups ticked "menu page", one-row widgets (button / slider / picker / highlight rows) and their runtime (row values,
  row slider bars / picker arrows / binding rows, Highlight widgets, the page highlight / page scroller roles, rows
  shown, columns), the widget kit (templates, kit pieces, DetectRoles / GuessBind / BuildRows / Restack / SwapRows /
  SaveTemplate / StampRow / KitPanel), "Import UniPad's menus file", and KindRef (new tile kinds take the built-in
  pieces). Every page is a custom menu; pages are rows of grids / carousels only (MoveSel still moves between grids
  on a page). Loading an older layout: old pages become custom menus, one-row widgets / templates / kit pieces are
  left out (logged); older page lines are ignored. Saved pages: group, start, selcolor, dedicated=1, pausemenu.
- **Page highlight role (2026-10-27; removed 2026-11-03).** Any element on a page itself (a shape, say) can have the role "page: highlight":
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
