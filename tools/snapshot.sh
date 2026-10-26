#!/bin/sh
# Keeps the disc in build/disc as build/disc_NAME, replacing what was there:
# for an A/B comparison, snapshot one side, build the other into build/disc.
# The data track and cue are copied; the audio tracks, the same in every
# build, are hard links to the original disc's (as tools/mkdisc.py makes
# them), so a snapshot costs the data track alone.
#
# usage: tools/snapshot.sh NAME     (quit Mednafen first if it has that disc)
set -e
cd "$(dirname "$0")/.."

name=${1:?usage: tools/snapshot.sh NAME}
src=build/disc
dst=build/disc_$name

mkdir -p "$dst"
cp "$src/tomb.cue" "$src/tomb_track01.iso" "$dst/"
for f in "$src"/tomb_track*.bin; do
    ln -f "$f" "$dst/$(basename "$f")"
done
echo "$dst: $(du -sh "$dst/tomb_track01.iso" | cut -f1) data track, audio linked"
