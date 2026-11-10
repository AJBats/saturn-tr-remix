Tomb Raider for the Sega Saturn, remastered: {release}
https://github.com/AJBats/saturn-tr-remix (branch {branch}, commit {commit})

A new engine for the 1996 Saturn Tomb Raider, running on a real Saturn: the game's
logic from OpenLara, checked against the retail game, on a renderer written for the
Saturn's two SH-2 processors, its DSP and VDP1. It aims at 30 frames a second, with
the textures reworked for the Saturn's way of drawing them.

This is an engineering sample: a snapshot of work in progress, for trying out and
reporting what you find. It is not finished. OPEN_ISSUES.md in the repository lists
what is known to differ from the retail game or to be broken.

This program builds the disc from your own copy of the game. No game data comes
with it. The disc you build holds the game's data, so please don't share it.


WHAT YOU NEED

- Windows 10 or 11, 64-bit.
- A rip of your Tomb Raider disc for the Saturn: the USA release, T-7910H. It must
  be a .cue file with one .bin file a track beside it, which is what redumper,
  DiscImageCreator and most rippers make. A .zip of the rip works too. The builder
  checks every file on the disc's data track, and refuses a rip that differs.
- Optional: a rip of Tomb Raider for the PlayStation, the USA release (SLUS-00152),
  in the same form. The disc then takes its sound effects, recorded at a higher rate
  than the Saturn's. Without it the disc keeps the Saturn's own. It was tested with
  the Rev 1 disc; the builder checks the level files it reads, and may refuse others.
- About 700 MB of free space for the disc.


HOW TO USE IT

1. Unzip this somewhere you can save files, like your Documents folder.
2. Run "Tomb Raider Saturn Builder.exe" in the "Tomb Raider Saturn Builder" folder.
3. Press Choose for your Saturn disc and pick its .cue file (or its .zip). Do the
   same for the PlayStation disc if you have it. Each is checked when you pick it.
4. Press BUILD THE DISC. It takes a few minutes: the builder repacks every level,
   with the remaster's textures, from your disc.
5. The disc is written to a "Tomb Raider Saturn - {release}" folder next to
   the builder, as tomb.cue and its tracks. Play it in an emulator, or on a real
   Saturn with an ODE such as the Satiator.


IN THIS SAMPLE

- The game from the start, as on the retail disc.
- From the title, open the passport: besides Caves and City of Vilcabamba you can
  start straight at Lost Valley or The Cistern.


IF IT WON'T START

- "Windows protected your PC": the builder isn't signed, so Windows warns about it.
  Click "More info", then "Run anyway".
- Some antivirus programs distrust programs packaged with PyInstaller, as this one
  is. Its source is in the repository (builder/), if you'd rather build it yourself.


NOTES

- Tested on a real NTSC Saturn with a Satiator, and in Mednafen. Other ODEs, the
  Saroo, burned discs and PAL Saturns haven't been tried.
- If something goes wrong, please open an issue, with the builder.log from the
  disc's folder if the builder stopped:
  https://github.com/AJBats/saturn-tr-remix/issues

The engine and this builder are GPL-3.0-only (LICENSE.txt); their source is in the
repository at the commit above. The engine builds on OpenLara (BSD-2-Clause, Timur
Gagiev) and libyaul (MIT, Israel Jacquez), and its creature AI and traps follow
TR1X/TRX (GPL-3, LostArtefacts). THIRD-PARTY-NOTICES.txt holds their licences and
those of what the builder bundles: Python, NumPy, SciPy, Pillow, Tcl/Tk and
PyInstaller's bootloader. Tomb Raider (c) Core Design / Eidos.
