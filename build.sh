#!/bin/sh
# Builds build/NFSU2_REUI.asi (32-bit Windows DLL): REUI's core + its Direct3D 9 drawing + the NFSU2 game connection.
# Needs: g++-mingw-w64-i686, git, python3. Dependencies are fetched into third_party/ on first run.
#
# Layout: core/ (editor, elements, menus, animation - no game, no graphics API), render/d3d9/ (drawing for D3D9 games),
# games/nfsu2/ (hooks, FEng, car data, the mod API, prefabs). Other games build core/ with their own render/ and games/.
set -e
cd "$(dirname "$0")"

IMGUI_TAG=v1.90.4
MINHOOK_TAG=v1.3.4
NANOSVG_REV=239e102ec2c691f2902e20ace2ed36ee4a35cfe6   # nanosvg has no releases: pinned commit
TP=third_party
CXX=${CXX:-i686-w64-mingw32-g++}
CC=${CC:-i686-w64-mingw32-gcc}

mkdir -p "$TP" build/obj build/gen
[ -d "$TP/imgui" ] || git clone -q --depth 1 --branch "$IMGUI_TAG" https://github.com/ocornut/imgui.git "$TP/imgui"
[ -d "$TP/minhook" ] || git clone -q --depth 1 --branch "$MINHOOK_TAG" https://github.com/TsudaKageyu/minhook.git "$TP/minhook"
[ -d "$TP/nanosvg" ] || { git clone -q https://github.com/memononen/nanosvg.git "$TP/nanosvg" && git -C "$TP/nanosvg" checkout -q "$NANOSVG_REV"; }

# built-in prefabs: games/nfsu2/prefabs/*.txt -> build/gen/baked_prefabs.inc. Before building with the game's latest
# prefabs, run games/nfsu2/tools/sync_prefabs.py on a copy of scripts\NFSU2_REUI_prefabs (adds new / changed ones,
# drops _deleted.log's).
python3 -I tools/bake_prefabs.py games/nfsu2/prefabs build/gen/baked_prefabs.inc
IMGUI="$TP/imgui"
MH="$TP/minhook"
for f in buffer hook trampoline hde/hde32; do
  $CC -O2 -c "$MH/src/$f.c" -I"$MH/include" -o "build/obj/mh_$(basename $f).o"
done
G=games/nfsu2/src
SRC="$G/dllmain.cpp $G/log.cpp $G/config.cpp $G/panel.cpp $G/host.cpp
     $G/game/d3d9_hook.cpp $G/game/feng.cpp core/uied.cpp core/vecart.cpp core/shapes.cpp render/d3d9/shapes_gpu_d3d9.cpp render/d3d9/gfx_d3d9.cpp
     $G/reui_api.cpp $G/game/car.cpp
     $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp
     $IMGUI/backends/imgui_impl_dx9.cpp $IMGUI/backends/imgui_impl_win32.cpp"

$CXX -std=c++17 -O2 -shared -o build/NFSU2_REUI.asi $SRC \
  build/obj/mh_*.o -Icore -Irender/d3d9 -I$G -Ibuild/gen -I"$IMGUI" -I"$IMGUI/backends" -I"$MH/include" -I"$TP/nanosvg/src" \
  -ld3d9 -ldwmapi -limm32 -luser32 -lgdi32 -lshell32 -lkernel32 \
  -static -static-libgcc -static-libstdc++ -s -Wl,--enable-stdcall-fixup

echo "built build/NFSU2_REUI.asi ($(wc -c < build/NFSU2_REUI.asi) bytes)"
