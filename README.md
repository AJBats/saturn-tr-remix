# Tomb Raider for the Sega Saturn, remastered

A new engine for the 1996 game, running on a real Saturn: OpenLara's game logic,
corrected against the retail game, on a renderer written for the Saturn's two
SH-2s, DSP and VDP1. It plays the game's data from your own disc.

**No game data is in this repository.** You need your own copy of the game to
build a disc, and a disc you build contains that game's data, so don't
distribute it.

This is a work in progress. [OPEN_ISSUES.md](OPEN_ISSUES.md) lists what is known
to differ from the retail game or to be broken.

## What you need

| | |
|---|---|
| Retail Saturn *Tomb Raider* (USA) | Required: levels, pictures, videos, CD audio |
| PlayStation *Tomb Raider* (USA) (Rev 1) | Optional: better sound effects, see [below](#with-or-without-the-playstation-disc) |
| Python 3.10 or newer | The level packer and other build tools |
| GNU Make 4.3 or newer, and a POSIX shell | |
| Git | |

The compilers come from `tools/setup_toolchain.py`, about 310 MB of downloads.

**On Windows**, use Git Bash from [Git for Windows](https://git-scm.com/download/win),
and get make with `winget install ezwinports.make`. Make won't run from PowerShell or
`cmd` unless a `sh` is on PATH. Keep the checkout's own path to 130 characters or
fewer: GCC can't reach its headers past that.

**On Linux** (x86-64), `sudo apt install make python3-venv binutils-m68k-linux-gnu`
or your system's equivalent, and use `python3` for the first `python` below.

**On macOS**, see [Other systems](#other-systems).

## Building

```bash
git clone https://github.com/AJBats/saturn-tr-remix
cd saturn-tr-remix

python -m venv .venv
source .venv/Scripts/activate       # Linux: source .venv/bin/activate
pip install -r requirements.txt

python tools/setup_toolchain.py

# put your disc images in external_resources/ (next section), then:
make extract-ps1                    # only with the PlayStation disc
make -j8 disc
```

The result is `build/disc/tomb.cue`, with its data track and 56 audio tracks beside
it. Activate the virtual environment again in each new shell before running `make`.

Add the PlayStation disc later if you like: `make extract-ps1`, then `make disc`
again, packs the levels again with its sounds.

## The discs

Images must be raw, one `.bin` per track, with these names (those of
[Redump](http://redump.org) dumps):

```
external_resources/
    tomb_raider_usa/
        Tomb Raider (USA).cue
        Tomb Raider (USA) (Track 01).bin
        ...
        Tomb Raider (USA) (Track 57).bin
    ps1/
        Tomb Raider (USA) (Rev 1) (Track 01).bin
```

Only the PlayStation disc's first track is read. Another dump of it can be named
with `make extract-ps1 PS1TRACK="path/to/track01.bin"`.

The build has been tested with these data tracks; other regions and revisions
haven't been tried.

| Track 01 | SHA-1 |
|---|---|
| Saturn | `4ecca92d2c381edbe72acc32ac7dce2ffe6bac1d` |
| PlayStation | `83fd6516fc658cfc12cb1941907cfed054b72c13` |

## With or without the PlayStation disc

The disc changes only the sound effects. Levels, textures and everything else are
byte for byte the same either way.

| | Sound effect samples |
|---|---|
| With | The PlayStation's, at 11025 Hz |
| Without | The retail Saturn's: 5512.5 Hz, or 2500 Hz for 56 of the sounds |

Every level has all its sounds either way. A few cutscene sounds stay the Saturn's in
both, as the PlayStation disc has none for them. So far, only discs built with the
PlayStation disc have been played on a real Saturn.

## Playing

- **Emulator:** open `build/disc/tomb.cue` in [Mednafen](https://mednafen.github.io/),
  which needs a Saturn BIOS image of your own.
- **Saturn:** this is tested with a [Satiator](https://www.satiator.net/): copy the cue,
  the `.iso` and the `.bin` files into one folder on its card.

## Troubleshooting

| Message | What to do |
|---|---|
| `No POSIX shell on PATH` | Run make from Git Bash |
| `GNU Make 4.3 or newer is needed` | Install a newer make; on macOS, Homebrew's is `gmake` |
| `can't import numpy, scipy and PIL` | Activate the virtual environment, or `pip install -r requirements.txt`. An install outside a virtual environment has to reach Python's own packages: make runs Python with `-I`, which ignores `pip install --user` |
| `No SH toolchain` | `python tools/setup_toolchain.py` |
| `No 68000 binutils` | Windows: `python tools/setup_toolchain.py`. Elsewhere: install them, and if their prefix isn't `m68k-linux-gnu-`, use `make M68K_PREFIX=prefix-` |
| `nothing to unpack them with` | `pip install -r requirements.txt` in the virtual environment, then run the setup again |
| `could not download` | A pinned download has moved. The compilers' URLs and checksums are at the top of `tools/setup_toolchain.py`; `make SH_BIN=... M68K_PREFIX=...` builds with others of your own |
| `checkout's path is too long` | Move the checkout to a shorter path. If you see `No such file or directory` for a C++ header with `SH_BIN` set, it's the same limit |
| `no such file` or `not a raw 2352-byte data track` | See [The discs](#the-discs). A single-file `.bin`, an `.iso`, or another name won't do |

Only the build needs what's above. The tools for working on the engine, such as the
debugging fork of Mednafen (`git submodule update --init tools/mednafen`), have needs of
their own, and some assume Windows and the author's setup.

## Other systems

**Linux** builds on x86-64: the prebuilt compiler needs a recent glibc (tested on
Ubuntu 24.04). On other machines, build
[the compiler](https://github.com/willll/Saturn-SDK-GCC-SH2) yourself and pass
`SH_BIN=its/bin` to make.

**macOS has never been tried.** The compiler release's only macOS build is GCC 14.1.0
for Apple silicon, not the 14.2.0 used elsewhere, so the program it builds may differ.
macOS's own make is too old (`brew install make`, then use `gmake`), and you'd need to
find a 68000 GNU binutils and pass `M68K_PREFIX`.

## Licence

The engine is GPL-3.0-or-later ([LICENSE](LICENSE)). It builds on
[OpenLara](https://github.com/XProger/OpenLara) (BSD-2-Clause, [openlara/LICENSE](openlara/LICENSE))
and the [SlaveDriver engine](https://github.com/Lobotomy-Software/SlaveDriver-Engine)
(GPL-3.0-or-later). Tomb Raider's game data is © Core Design / Eidos, and comes
from your own disc.
