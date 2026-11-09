# REUI architecture

REUI is one UI system shared by several games: an in-game editor, HUD elements, menus, vector art, shapes, colour
tokens and animation. Each game adds only how REUI reaches it (hooks, the game's data, its textures and fonts) and how
it draws (the graphics API).

```
core/               the editor, elements, layouts / prefabs, menus, animation, tokens, vector art (nanosvg), shapes
  gfx.h             the graphics interface the core draws through (textures, draw-state callbacks)
  assets.h          the game's own textures and fonts (a source the game connection supplies)
  shapes_gpu.h      GPU shapes, implemented per backend
render/d3d9/        Direct3D 9 backend: gfx_d3d9.cpp (textures, states), shapes_gpu_d3d9.cpp (ps_3_0 shapes)
render/rex/          ReXGlue presenters (Joy Ride Turbo's PC port): rex_overlay (a UI drawer with its own ImGui context,
                    ImGui 1.92 textures over an ImmediateDrawer, window input), gfx_rex (RGBA8 textures; BGRA8 / DXT
                    converted), shapes_gpu_rex (off: shapes made on the CPU)
games/nfsu2/        Need for Speed Underground 2: NFSU2_REUI.asi (build.sh)
  src/              dllmain, D3D9 hook, FEng, car data, host (the game's textures / objects / minimap), mod API,
                    assets_nfsu2.cpp (its texture packs and FEng fonts as the assets source)
  prefabs/          built-in prefabs (baked by tools/bake_prefabs.py into build/gen/baked_prefabs.inc)
  docs/             PLAN.md (the NFSU2 plan and feature log), RE_NOTES.md
games/joyride/      Joy Ride Turbo (compiled into its PC port, vendored there under thirdparty/reui)
  src/reui_joyride  Create / Destroy from the port's app; F6 = the editor; data in <user data>\REUI\
  baked_prefabs.inc its built-in prefabs (tools/bake_prefabs.py games/joyride/prefabs games/joyride/baked_prefabs.inc)
tools/              shared tools (bake_prefabs.py)
```

## Rules

- `core/` names no graphics API and no game. Textures are `gfx::Tex` handles; a backend makes them (`gfx::Create`)
  and turns them into what ImGui draws (`gfx::Id`). Game-owned textures come in through the host's hooks
  (`uied::liveTexture`, `GameObject::tex`, `MapView::tex`), wrapped by the game's backend (`gfx::d3d9::Wrap`).
- Draw-state changes the core needs (additive blending, vertex-only alpha, minimap border sampling) are ImGui draw-list
  callbacks in `gfx.h`, implemented by the backend.
- A game's build compiles `core/*.cpp`, one `render/<api>/`, and its `games/<game>/` files.
- The game's own textures and fonts (the editor's Textures / Fonts pages, image and text elements) come from
  `assets::source`, set by the game before `uied::Init`: a background `scan` filling the index and `pixels` for one
  texture, ready for `gfx::Create`. Without one the lists are empty.
- Where REUI keeps its files: `uied::prefabDir` / `uied::dataDir` (empty: NFSU2's `scripts\NFSU2_REUI_prefabs\` and
  `<game>\REUI\`).
