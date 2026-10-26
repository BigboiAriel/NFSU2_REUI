#!/bin/sh
# Builds build/NFSU2_REUI.asi (32-bit Windows DLL).
# Needs: g++-mingw-w64-i686, git. Dependencies are fetched into third_party/ on first run.
set -e
cd "$(dirname "$0")"

IMGUI_TAG=v1.90.4
MINHOOK_TAG=v1.3.4
NANOSVG_REV=239e102ec2c691f2902e20ace2ed36ee4a35cfe6   # nanosvg has no releases: pinned commit
TP=third_party
CXX=${CXX:-i686-w64-mingw32-g++}
CC=${CC:-i686-w64-mingw32-gcc}

mkdir -p "$TP" build/obj
[ -d "$TP/imgui" ] || git clone -q --depth 1 --branch "$IMGUI_TAG" https://github.com/ocornut/imgui.git "$TP/imgui"
[ -d "$TP/minhook" ] || git clone -q --depth 1 --branch "$MINHOOK_TAG" https://github.com/TsudaKageyu/minhook.git "$TP/minhook"
[ -d "$TP/nanosvg" ] || { git clone -q https://github.com/memononen/nanosvg.git "$TP/nanosvg" && git -C "$TP/nanosvg" checkout -q "$NANOSVG_REV"; }

IMGUI="$TP/imgui"
MH="$TP/minhook"
for f in buffer hook trampoline hde/hde32; do
  $CC -O2 -c "$MH/src/$f.c" -I"$MH/include" -o "build/obj/mh_$(basename $f).o"
done
SRC="src/dllmain.cpp src/log.cpp src/config.cpp src/panel.cpp src/host.cpp
     src/game/d3d9_hook.cpp src/game/feng.cpp src/editor/uied.cpp src/reui_api.cpp src/game/car.cpp
     $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp
     $IMGUI/backends/imgui_impl_dx9.cpp $IMGUI/backends/imgui_impl_win32.cpp"

$CXX -std=c++17 -O2 -shared -o build/NFSU2_REUI.asi $SRC \
  build/obj/mh_*.o -Isrc -I"$IMGUI" -I"$IMGUI/backends" -I"$MH/include" -I"$TP/nanosvg/src" \
  -ld3d9 -ldwmapi -limm32 -luser32 -lgdi32 -lkernel32 \
  -static -static-libgcc -static-libstdc++ -s -Wl,--enable-stdcall-fixup

echo "built build/NFSU2_REUI.asi ($(wc -c < build/NFSU2_REUI.asi) bytes)"
