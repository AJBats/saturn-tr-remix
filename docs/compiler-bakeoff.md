# Compiler bake-off (2026-10-25)

Which SH-2 GCC to build the engine with. Candidates were what was at hand:

| Toolchain | Source |
|---|---|
| GCC 8.4.0 `sh-elf` | Saturn-SDK-GCC-SH2 installer (`~/saturndev/saturn-sdk-8-4`) |
| GCC 9.3.0 `sh-elf` | Saturn-SDK-GCC-SH2 installer (`~/saturndev/saturn-sdk`) |
| GCC 14.2.0 `sh2eb-elf` | [willll/Saturn-SDK-GCC-SH2 release gcc_14.2.0_2](https://github.com/willll/Saturn-SDK-GCC-SH2/releases/tag/gcc_14.2.0_2), the build SaturnRingLib uses |

Not candidates: Ubuntu's `gcc-sh-elf` 13.2 package (C only, no `g++`, and the
OpenLara core is C++); the Cygnus 2.7 Sega-era compiler (can't build the C++).
The willll project lists 15.x/16.x tags but publishes no binaries for them.

## Method

Same source, same flags apart from `-O2`/`-Os`, built with `tools/bakeoff.py`.

Size: `.text` of the game binary (OpenLara core + Saturn platform layer).

Speed: `saturn/bench/bench.cpp` (`make bench`) times five kernels of real
engine code on synthetic data in Mednafen, with the SH-2 free-running timer
(Pphi/128) and interrupts masked: view/node matrix chains, animation frame
interpolation, room vertex transform, room transform + face bucketing + VDP1
command build (377 polygons), and atan/sqrt/angle/rng maths. Each run is
deterministic; all six builds emitted the same 377 polygons.

## Results

| Config | .text | matrix | frameLerp | transform | room+flush | math | total |
|---|---|---|---|---|---|---|---|
| gcc8.4 -O2  | 133081 | 4.69M | 6.11M | 6.91M | 5.67M | 3.65M | 27.03M (99.7%) |
| gcc8.4 -Os  | 103649 | 4.77M | 6.22M | 7.05M | 5.72M | 3.69M | 27.45M (101.3%) |
| gcc9.3 -O2  | 149241 | 4.69M | 6.14M | 6.90M | 5.69M | 3.69M | 27.10M (100%) |
| gcc9.3 -Os  | 103833 | 4.77M | 6.23M | 7.05M | 5.77M | 3.74M | 27.55M (101.7%) |
| gcc14.2 -O2 | 137444 | 4.70M | 6.09M | 6.90M | 5.86M | 3.64M | 27.19M (100.3%) |
| gcc14.2 -Os | 102724 | 4.76M | 6.29M | 6.87M | 5.73M | 3.72M | 27.37M (101.0%) |

Cycles in millions; percentages against gcc9.3 -O2.

## Decision: GCC 14.2.0, -Os

- Speed is a wash: every build is within 3% of every other on every kernel.
  `-Os` costs 1-2% in these loops and saves about 30 KB (25%) of code, which
  also matters for the SH-2's 4 KB instruction cache once a real frame runs
  far more code than these kernels.
- GCC 14.2 gives the smallest `-Os` build, is a maintained, current compiler,
  ships Windows/Linux/macOS builds in one pinned archive, and its stricter
  analysis flagged OpenLara's construct-over-initialised-fields pattern in
  `ItemObj::init`, which led to adding `-fno-lifetime-dse` for every compiler.
- The bigger lever is elsewhere: the C room transform costs ~270 cycles per
  vertex. Hand-written SH-2 for the hot paths (as in the 32X port and
  SlaveDriver) will move frame time far more than any compiler choice.

Caveats: Mednafen's SH-2 timing is an approximation of the hardware, and the
kernels cover rendering and maths, not game logic, AI or collision (no level
loads yet). Re-run with real frame timings once a level renders:
`python tools/bakeoff.py` builds every config, `make bench` builds one.

The toolchain is pinned by `tools/setup_toolchain.py` (URL + SHA-256) into
`toolchain/` (gitignored), and is the Makefile default.
