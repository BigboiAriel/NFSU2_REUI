# REUI architecture

REUI is one UI system shared by several games: an in-game editor, HUD elements, menus, vector art, shapes, colour
tokens and animation. Each game adds only how REUI reaches it (hooks, the game's data, its textures and fonts) and how
it draws (the graphics API).

```
core/               the editor, elements, layouts / prefabs, menus, animation, tokens, vector art (nanosvg), shapes
  gfx.h             the graphics interface the core draws through (textures, draw-state callbacks)
  shapes_gpu.h      GPU shapes, implemented per backend
render/d3d9/        Direct3D 9 backend: gfx_d3d9.cpp (textures, states), shapes_gpu_d3d9.cpp (ps_3_0 shapes)
render/d3d12/       Direct3D 12 backend (Joy Ride Turbo) - planned
games/nfsu2/        Need for Speed Underground 2: NFSU2_REUI.asi (build.sh)
  src/              dllmain, D3D9 hook, FEng, car data, host (the game's textures / objects / minimap), mod API
  prefabs/          built-in prefabs (baked by tools/bake_prefabs.py into build/gen/baked_prefabs.inc)
  docs/             PLAN.md (the NFSU2 plan and feature log), RE_NOTES.md
games/joyride/      Joy Ride Turbo (built into its PC port) - planned
tools/              shared tools (bake_prefabs.py)
```

## Rules

- `core/` names no graphics API and no game. Textures are `gfx::Tex` handles; a backend makes them (`gfx::Create`)
  and turns them into what ImGui draws (`gfx::Id`). Game-owned textures come in through the host's hooks
  (`uied::liveTexture`, `GameObject::tex`, `MapView::tex`), wrapped by the game's backend (`gfx::d3d9::Wrap`).
- Draw-state changes the core needs (additive blending, vertex-only alpha, minimap border sampling) are ImGui draw-list
  callbacks in `gfx.h`, implemented by the backend.
- A game's build compiles `core/*.cpp`, one `render/<api>/`, and its `games/<game>/` files.
- The core still reads NFSU2's texture packs and FEng fonts itself (uied.cpp "files" / "index" / "text"). Moving that
  behind an asset interface is the next step, so other games can supply their own textures and fonts.
