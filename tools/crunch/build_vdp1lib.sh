#!/bin/sh
# Builds build/crunch/vdp1lib.dll (Windows, for the lab's Python) from the
# Mednafen fork's VDP1 drawing code. Run in WSL, which has the mingw compiler:
#   wsl -d Ubuntu -- sh tools/crunch/build_vdp1lib.sh
set -e
cd "$(dirname "$0")/../.."

fork=tools/mednafen/src/ss
out=build/crunch
obj=$out/obj
mkdir -p "$obj"

# vdp1.cpp's clip and local coordinate commands, texel fetches, line and edge
# setup: everything from CMD_SetUserClip up to the command loop
awk '/^static int32 CMD_SetUserClip/ { on = 1 } /^enum : int \{ CommandPhaseBias/ { on = 0 } on' \
    "$fork/vdp1.cpp" > "$obj/vdp1_parts.inc"
grep -q '^void EdgeStepper::Setup' "$obj/vdp1_parts.inc" || { echo "vdp1.cpp markers moved" >&2; exit 1; }

cxx="x86_64-w64-mingw32-g++ -O2 -std=gnu++17 -fsigned-char -fwrapv -fno-strict-aliasing -include tools/crunch/vdp1shim.h"
pids=
for f in vdp1_line vdp1_sprite vdp1_poly; do
    $cxx -c "$fork/$f.cpp" -o "$obj/$f.o" &
    pids="$pids $!"
done
$cxx -I "$fork" -I "$obj" -c tools/crunch/vdp1lib.cpp -o "$obj/vdp1lib.o"
for p in $pids; do
    wait "$p"
done
x86_64-w64-mingw32-g++ -shared -static -o "$out/vdp1lib.dll" "$obj"/vdp1_line.o "$obj"/vdp1_sprite.o \
    "$obj"/vdp1_poly.o "$obj"/vdp1lib.o
echo "$out/vdp1lib.dll"
