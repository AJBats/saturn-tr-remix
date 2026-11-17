#!/bin/sh
# The game's logic alone, for a big-endian 32-bit host: PowerPC Linux under
# qemu-ppc (WSL: apt install g++-powerpc-linux-gnu qemu-user). Built as the
# Saturn build is (__SATURN__, the same structures, -Os for the game, -O2 for
# common.cpp), with the hardware left out (host/host.cpp).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
CXX=${CXX:-powerpc-linux-gnu-g++}
FLAGS="${HOST_DEBUG:+-g} -D__SATURN__ -DTRS_HOST -fsigned-char -Wall -Iopenlara -Isaturn -Irenderer -Ihost
 -include host/host_platform.h
 -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit -fno-lifetime-dse -Wno-class-memaccess -Wno-invalid-offsetof"
$CXX $FLAGS -Os -c host/host.cpp -o build/host/host.o
$CXX $FLAGS -O2 -c openlara/common.cpp -o build/host/common.o
$CXX -static build/host/host.o build/host/common.o -o build/host/logic
