# VDP2 lab: what Sonic World does, and what a plane would ask of TR

Research on branch `vdp2-lab`, worktree `../trs_lod`. Mednafen unless it says Saturn.
Sonic Jam's disc: `external_resources/sonic_jam/`. Tools: `tools/vdp2spy/`.

## Sonic World in one paragraph

One plane. VDP2 shows RBG0, NBG1 and NBG2, and RBG1 is off; NBG1 and NBG2 are the HUD. The ground is
a single 4096x4096 RBG0 map. The sky is the same RBG0 under its second rotation parameter set, chosen by a
line window: the lines above the horizon get set B, the rest set A. The river is not on VDP2 at all. It is
VDP1's gouraud-textured quads painted over a plane that runs on unbroken underneath, and with the sprites
hidden the grass carries straight across where the river was. The banks' grass fringe hides the join. The
depth is a trick of the eye; there is no second depth layer.

## "Below the plane" is a priority written into VDP1's pixels

VDP1's pixels come in two kinds, set by the command's colour bank (sprite type 0, mixed palette and RGB):

| VDP1 pixel | Priority register | Priority | Against RBG0 (priority 5) |
|---|---|---|---|
| RGB (the normal textured faces) | 0 | 6 | in front |
| palette, bit 14 set (colour bank 0x48A0, an 8x2 texture) | 1 | 4 | behind |

There's no depth test, so a later command simply overwrites the frame buffer. A quad written at priority 4
erases the river pieces already drawn there, and the grass shows through. Those quads run along the near
bank in the list's depth order, so the ground in front hides the river behind it. That is the whole of "below".
`build/vdp2/sonic_jam/bend_4up.png` shows it: the black wedge in the "RBG0 off" panel is that quad, which is
grass in the real frame. At the bends up to about 1,900 pixels of the frame are like this. The two lists decoded
hold 3 and 5 such quads, and nearly all of what they cover on the frame lies over faces drawn earlier
(`tools/vdp2spy/maskcheck.py`), so erasing really is their job.

This is the design question in issue 269 (a plane under the frame can't hide what VDP1 drew behind it),
answered the way the issue guessed, with polygons that reveal the plane. Sonic uses low priority where the
issue said colour 0.

## The registers (`tools/vdp2spy/vdp2dec.py`)

- **VRAM:** RAMCTL 0x134B, all banks split, CRAM mode 1 (2048 colours, 15-bit). A0 = RBG0 characters, A1 =
  RBG0 names, B1 = coefficient table. B0 = HUD, rotation tables at 0x5FF20 and 0x5FFA0, line window table at
  0x5F800. Cycle word for B0: 0x1526 (N1 name, N1 cell, N2 name, N2 cell), the rest no access.
- **RBG0:** 256 colours, 2x2 cells (16x16), 1-word names, plane 2x2 pages, 16 planes. Map A has 229 distinct
  tiles and one palette; the sky page 241. Set A over-pattern 0x60E0 (grass past the map's edge); set B repeats.
- **Rotation mode:** RPMD 3, by window; WCTLD 0x0008, W1 alone, with a line window table (LWTA1). In the first
  frame lines 0-23 have the span (0,638), the rest none.
- **Coefficients:** KTCTL 0x71 both sets: table on, 2-word, kx = ky, line colour from the top byte. One fixed
  hyperbola of 32,768 entries centred on 0x70000, shared: set A walks it forward from the horizon (k 25.6 at the
  horizon, 0.099 at the bottom), set B backward. Its top bit marks the far side of the horizon as transparent.
- **Haze:** the coefficient's top 7 bits are the line colour screen's index (LNCLEN 0x10), rising 0 -> 126 toward
  the horizon, added to RBG0 (CCCTL 0x0110). Distance haze from a table that never changes.
- **A frame's CPU cost:** one rotation matrix and two parameter tables, plus the line window table. KAst and DKAst
  pick where on the fixed hyperbola each frame reads.

## What doesn't carry over

- **Its frame is erased every list.** The vblank handler at 06040D76 is the library's 2-field cycle: FBCR = 2
  (manual erase, during the next field's display) then FBCR = 3 + PTMR = 2 (change, plot at the change). That
  costs VDP1 no time but needs the next list known finished a field ahead. Sonic has 139 and 157 commands in the
  two lists decoded; we have about 1,100, and issue 251 has already been through this. So we'd write colour 0 or a low priority over
  the floor's pixels instead of relying on an erase.
- **No roll.** Its coefficients are per line (DKAx 0). A rolling camera would need DKAx; TR's not checked.
- **Its haze adds.** TR's fog darkens. A per-line darkening needs a separate test.
- **320 wide.** We run 352. VDP2's cycle patterns are the same at both; VDP1's erase isn't what we'd use anyway.
- **No mips.** RBG0 samples one texel a pixel with no filtering, and Sonic's far grass shimmers under its haze.

## The bank B mystery, and what shipped games do at 352 wide

We have never had a display read from bank B work on this Saturn (`mednafen-vdp2-gaps`). Every layout that
failed then was an NBG read from B1, at 352, Oct 25; every shipped game surveyed reads bank B at 352 without
trouble, and two of them run RBG0 from it. So the bank was never the problem.

`tools/vdp2spy/survey.py`, the words as these games write them (Mednafen, but their words, proven on this Saturn):

| Game, 352 x 224 | Layers | RAMCTL | A0 | A1 | B0 | B1 |
|---|---|---|---|---|---|---|
| Sonic R, race | N0 256, N2 16, **R0 256** | 03B0 | 012F 56FF | 44FF 0FFF | FFFF FFFF | FFFF FFFF |
| Sonic R, a menu | N0 32K cells, N3 16 | 03B0 | 4444 037F | 4444 FFFF | FFFF FFFF | FFFF FFFF |
| Sega Rally | N0-N3 256, **R0 256** | 13E0 | 6655 4FE4 | 0771 23FE | FFFF FFFF | FFFF FFFF |
| Daytona USA CCE | N0 N1 256, N2 N3 16 | 1300 | 4400 FFFF | FFFF 6FFF | F755 FFFF | 0F1F 23FF |
| Retail TR, its pictures | N1 32K bitmap | 1327 | 5555 FEEE | 5555 FEEE | 5555 FEEE | 5555 FEEE |
| Sonic Jam at 320 | N1 N2 16, **R0 256** | 134B | FFFF FFFF | FFFF FFFF | 1526 FFFF | FFFF FFFF |

- Sonic R and Sega Rally keep RBG0 wholly in bank B (B0 cells, B1 names, or the other way) and every NBG in bank A.
  That leaves our text layer where it is: 32K colours read in T0-T3 of A0 and A1, the slots Sonic R's menu gives N0.
- Sonic R has no bank for coefficients (RDBS none), so VDP2 reads one coefficient per line, from any bank, in the
  blanking. Its 224 words sit in B0 beside the rotation table. Only a bank with RDBS = coefficients reads per dot.
- Unused slots are F (no access) in every game above, not E. Ours say E. CPU slots aren't needed in a bank RDBS owns.

Against libyaul: its table layout, KTAOF (kast >> 18 at 2 words) and the order of its registers agree with
what lab/plane.cpp writes. It starts with TVMD = 0 and writes everything in the vblank from shadow registers. Its
console (NBG3, cells T3, names T7, bank B1) is one of the layouts that failed here, at 352; it runs at 320 by default.

**Saturn result, vdp2bench at 4ad3de6 (read from the capture card, 2026-11-03):**

| Page | Layout | Saturn |
|---|---|---|
| 1 | N0 32K bitmap, bank A | right |
| 2 | N1 16-colour bitmap, B1 T0 (our test 9, stripes then) | ramp the right way round, its brightest dot cut short; the same with every other slot F (second round) |
| 3 | N3 cells B1 T0 T4, bank A 4444 EEEE, the rest CPU (our test 10) | **garbage** |
| 4 | N3 cells B1 T3 T7, bank A 4444 EEEE, B0 CPU (our test 11) | **garbage** |
| 5 | N3 cells B1 T3 T7, every other slot F (libyaul's) | right |
| 6, 7, 8 | Daytona CCE, Sonic Jam, Sonic R, their words | right |
| 9, 10 | RBG0 in bank B (Sonic R's), in bank A with coefficients in B1 (Sonic Jam's) | right |
| 11 | page 4, display turned on at line 100 | garbage, as page 4 |
| 12 | page 5 on the 320-dot clock | right |

**So bank B was never the problem, and nor was the display's timing (my guess, wrong: pages 4 and 5 turn it on the same way).** Pages 4 and 5
differ only in the slots of the other banks.

**Second round, dc6cbd1: the word is CPU slots in B0** (which of them: third round, below)**.** N3 cells and names in B1 at T3, T7, every other word F but as listed:

| Other slots | Saturn |
|---|---|
| A0 A1 4444 FFFF, or A0 alone (NBG0's reads, NBG0 off) | right |
| A0 A1 FFFF EEEE (CPU in A) | right |
| B1's spare slots E | right |
| **B0 EEEE EEEE** | **garbage** |
| A 4444 EEEE, B0 EEEE, with NBG0 really on behind | garbage, as with it off |
| name T0, cell T4, all else F (the first round's failing page 3) | right |

Our layouts all had B0 at EEEE. Every game in the survey has F there. N1's bitmap in B1 is not affected, so it's cells, or their names, that it
breaks. Mednafen draws them all right. RBG0 takes its banks through RDBS, not slots, and the floor never met this.

Turning the display off makes VDP1 stop ending lists: 6 give-ups a round in the first round, around pages 11, 12 and the 1 after them. That was
the first run's freeze (all pages turned it off, and the wait had no limit). With it left on, the second round gave up on none.

**The erases on the Saturn:**

| Asked | Lines emptied of 224: least, middle, most |
|---|---|
| Vblank erase, at vblank-in | 0, 185, 185 |
| ... 4, 8, 16, 24, 32 lines later | 0, 0, 0 each |
| ... at vblank-in, with SCU DMA writing VDP1 VRAM | 0, 185, 185 |
| Manual erase | 0, 224, 224 |

- 185 is Sonic R's strip's top line to the line: that is what its authors measured. Mednafen says 192.
- Asked for late it does nothing: all or nothing, so a list ending after vblank-in needs its successor to clear by polygon.
- SCU DMA doesn't slow it. Nor does a manual erase slow a list beside it: 20.36 ms a list both ways, measured clean in the second round, 32 lists
  each way, plotted at line 0 or 8 lines in, none stopping. (The first round's 4 timeouts haven't come back, cause unknown.)
- A change under a plot with auto plot left VDP1 alive after 60 changes, 1 of 120 lists finishing: Sonic Jam's metronome cuts lists, it doesn't hang.

**Second round, dc6cbd1, each trial in order, two passes:**

| Asked | Most | What the trials read |
|---|---|---|
| At vblank-in, 4 or 16 lines before it, or with SCU DMA | 185 | # or n, about 1 in 4 # |
| 1 line late | 185 | a few #, a few 0, the rest n |
| 2 to 32 lines late | 0 | 0 or n: the buffers do change, nothing's erased |
| From line 39 | 224 | so 185 lines from anywhere |
| Half width | 224 | |
| 30 fps rhythm (no n in this row: it reads no mark) | 185 | # or 0, about half |
| Manual erase (likewise) | 224 | # or 0, about half |

Not one ?: an erase, when seen, is the whole 185 lines. The n and 0 look like the bench: it reads at the first line of the display, and if the
buffers change right at that moment each read is a coin flip. The n rows need two reads to land after the change (1 in 4), the other rows one (1 in 2),
which is just what they show; Mednafen, which can't race, shows all #. The third round proved it.

**Third round, 87a7590: the erase is reliable.** The same rows, reading 8 lines into the display, 16 trials a row, two passes:

| Asked | Reached | Trials |
|---|---|---|
| At vblank-in, 4 or 16 lines before it, with SCU DMA running | 185 | all |
| In a game's rhythm at 30 fps: list drawn, a field held, erased at the change | 185 | all |
| Window from line 39 | 224, so 185 lines | all |
| Window from line 20 | 205, so 185 lines | all |
| Window 320 wide | 203 | all |
| Window half width | 224 | all |
| Manual erase | 224 | all |
| 1 line after vblank-in | 185 or 0, nothing between | about 2 in 3 erased |
| 2, 4, 8, 32 lines after | 0 | none erased; the buffers still change |

320 vblank erases asked in time and 32 manual erases: not one fell short, and no VDP1 give-ups.

- **A budget, not a line.** 352 x 185 = 65,120 pixels, 320 x 203 = 64,960: about 65,000 pixels a vblank, from any window, in 8-dot steps across.
- **The change comes as the display starts:** 71,120 to 71,328 cycles after vblank-in, VCNT reading 0 every time, the same with or without an erase. So
  the second round's n and 0 were its reads at line 0 landing either side of it, as suspected. Reading the frame buffer needs a few lines' grace.
- **The deadline.** Any time before vblank-in, or about a line after it (63 us) at the very most. Asking early buys no extra reach.
- **The strip's price, Saturn, 352 x 224:** a full-frame polygon costs 3.39 ms (6 in a list, 20.36 ms), so 39 lines cost 0.59 ms, which is exactly what the floor
  page measured between vblank erase + strip (21.51) and manual erase (20.92).

**What a clear really costs: the deadline, not the list time.** The erase isn't free when a frame must be clear: the list has to end by vblank-in, 2.47 ms sooner
than a list that may change late. How long the list's other commands can take and still hold 30 fps (arithmetic from the Saturn costs, plot at line 0; the same
differences at 20 fps):

| Way | Those commands, up to | The catch |
|---|---|---|
| No clear (today) | 33.4 ms | What nothing covers shows the last frame |
| Manual erase | about 32.2 ms | A wrong guess, a field ahead, is a one-field flicker; its guess allows 1/32 |
| Vblank erase + strip, every frame must be clear | 30.3 ms | None on screen |
| A polygon over the frame in every list | 30.0 ms | None on screen |

So against a plain polygon clear, the vblank erase and strip buy 0.3 ms at the 30 fps deadline, and past it about 2.5 ms of lists that alternate 2 and 3 fields (24 fps)
instead of settling at 3. Its 2.8 ms saving is VDP1's busy time, which only shows as frame rate at a deadline.

**Where it can earn its keep: not asked for every frame.** Both the erase and the strip are chosen per frame, so a room that doesn't need a clear pays nothing. If a frame
after a late list may show the last frame in its holes, as all frames do today, there's no earlier deadline either: the price is the strip, 0.59 ms. Against that: the
reverted fill round open rooms, up to 69,344 pixels, about 3 ms at the bench's rate, and the airtight quads' hand work (issue 267). Not tried in the game.

**Which to use:** for holes, per room, the vblank erase and a strip, a late list skipping the next clear. For a floor on the plane, which must clear every frame, weigh
the manual erase again: nearly 2 ms more room than vblank + strip, for the flicker.

**What it doesn't do:**
- A list ending past the deadline changes with nothing erased, so the next list's first polygon must cover the frame: 3.39 ms instead of 0.59, on the frames
  that are already heavy. The floor page does this, chosen at plot time; Cistern 103 never needed it.
- It clears, nothing more: the CPUs' work is the same until faces leave the list, and what's under a floor still shows.
- Tested in this bench and on the lab's floor page, not in the game. Asked at vblank-in, the handler must run inside that one line, so interrupts can't stay masked
  long there.
- Mednafen reaches 192 lines, 210 at 320 wide: a strip from 185 is right on both, but Mednafen won't show a strip that's too low.

**Which slots, third round.** Reads in T3 and T7, the rest F:

| CPU slots (E) | Reads from | Saturn |
|---|---|---|
| B0, all | B1 | garbage |
| B0, all but T3 and T7 | B1 | **right** |
| B0, T3 and T7 only | B1 | garbage |
| B0, T0-3 | B1 | garbage |
| B0, T4-7 | B1 | garbage, milder |
| B1, all | B0 | right |
| A0, all | A1 | no layer at all, flat back colour; not chased |

**So: a CPU slot in B0 at the same T as a read from B1 breaks that read; not the other way round.** Why the hardware does this isn't known. Leave spare slots F, as every
retail game surveyed does.

**What the user saw on the Saturn, floor page, Cistern 103 (2026-11-03):** a forced miss in manual erase is a one-field flicker, nothing worse. The
camera moving draws correctly but for one inner polygon of a pool that always shows through the floor: geometry below the floor that the floor's
faces used to paint over, the masks still to be built (1,143 px of the frame, in Mednafen's count). The page didn't show its way then, so that test may have been in manual erase. Rerun at
b188667, the way now on the page: **vblank erase with the camera moving, nothing smeared at the strip's edge.** C flickers in manual erase only, as built: it does nothing in the
other ways, so a late list's whole-frame fill hasn't run on the Saturn.

**Pencilled in, for later, maybe in the main work:** rather than colour 0 faces masking what's under the floor, obstruction planes (the hand boxes of
the occlusion work) could stop those inner pool polygons being processed at all, a CPU saving as well as a picture fix. To check first: that culls whole
polygons, so one partly visible down the pool's opening stays and still needs an answer.

## Floor and ceiling together: both on one RBG0

**It works on the Saturn (b188667's successor f845d1d, 2026-11-03, the user's photo):** both planes in place at once, two rotation sets with no coefficient bank. `hall`, VDP1 ms / frame ms:

| Way | VDP1 ms | Frame ms |
|---|---|---|
| Textured | 25.62 | 39.72 |
| Colour 0 | 24.79 | 39.01 |
| Vblank erase + strip, floor only | 23.43 | 39.01 |
| Vblank erase + strip, floor and ceiling | 23.06 | 39.00 |
| Manual erase, both | 21.54 | 39.00 |

So -2.56 ms for the vblank erase: floor -2.19, ceiling -0.37. Manual is 1.52 ms under the vblank erase here, where room 103 had 0.59, the strip's price: probably some lists past the deadline, their successors filling the frame (not checked). Its one miss is the user's C or near it, so
the manual erase's trouble below is Mednafen's. The frame stays 39 ms in every way: the CPUs set it here (no pipelining in the lab), and they still do these faces.

Set A of the rotation parameters is the floor, set B the ceiling, each with its own map; a window picks by line as Sonic World picks ground and sky:
RPMD 3, WCTLD 0x0008, inside window 1 is set B, its lines 0 to the horizon. Both sets hold the same matrix and share one scale table, each line holding the scale of the plane
it shows, since a ceiling meets the same rays and only t differs. Where this differs from Sonic Jam's proven words: W1 is a plain rectangle where it has a line table (no VRAM read, two
registers), and RDBS has no coefficient bank (Sonic R's layout), where Sonic Jam has B1; Sonic R reads one set's coefficients that way, nobody surveyed reads two. **That's the thing to see on the
Saturn.** A horizon off the frame takes RPMD 0 or 1.

The first scene can't show this. From room 103's floor the camera looks down, and the ceiling, 6,000 units up, meets every ray in the frame past the far cut: 85 ceiling faces marked, 0 lines. New scenes from
the walkway at y -3328, where the hall opens above and below: `hall` (`cistern-straight`) and `walk` (`cistern`), from `../trs_cap` as before, spots added there by hand. On the page START hides the text, which sits over the ceiling.

The same in Mednafen, floor y 0, ceiling y -7168:

| Way | VDP1 ms | Frame ms |
|---|---|---|
| Textured | 29.13 | 39.00 |
| Colour 0 | 28.19 | 37.42 |
| Vblank erase + strip, floor only | 26.98 | 36.78 |
| Vblank erase + strip, floor and ceiling | 25.60 | 36.02 |
| Manual erase, both | 13.07 | 28.29: not to be believed, 420 misses, see below |

So -3.53 ms: floor -2.15, ceiling -1.38, for 105 + 28 lines. The faces' corners lie on the planes' grid. 464 pixels differ from the colour 0 way, along the far edges of both planes; here nothing shows through.

**Mednafen only:** the manual erase goes wrong at this view, about 14 lists timing out at half a second each; room 103 had 0 misses, and the Saturn here 1. Not chased.

**What the view asks of a plane:** at these two spots the biggest flat by far is the walkway itself, y -3328, 12,488 and 13,508 pixels shown in 21 and 29 faces (`occmodel one`, OCC_PLANES), against floor 0's 4,480 and 3,492 and the ceiling's 3,460 and
2,912. A plane's height is only a number in its table, so it could follow the room Lara's in. But the walkway has the whole hall below it to hide, which a plane can't.

**A bug found on the way, in my first floor:** the blank's texture index was the level's last, 2,605, far past the 288 room materials, so its material lay in another array (`gKeys`). Room 103 got away with it, and its numbers come out the same to the hundredth with it fixed: VDP1 27.00, manual 26.40.
`hall` didn't: VDP1 hung on junk commands. The blank now takes an index inside the array that no command in the game's list reads the texels of. A scene's name over 12 characters also overflowed the lab pages' rows and crashed it: labscene.py refuses them now.

## The fixed table on the Saturn (1f9e607)

The design in `docs/vdp2-design.md`, built: libyaul's buffer change, and Sonic Jam's fixed table for depth and fog. The user's read on the Saturn: `VDP2, fixed` looks right, `VDP2, timer fog` still has the ceiling flicker.

| Saturn, `hall`, ms | `VDP1 draws all` | `VDP2, timer fog` | `VDP2, fixed` |
|---|---|---|---|
| VDP1 | 25.56 | 21.98 | 21.98 |
| CPUs' stages | 31.93 | 30.83 | 29.52 |
| Tables, per frame | 0 | 1.02 | 0.10 |
| Frame | 33.43 | 33.43 | 33.43 |

Upload 0.70, 402 faces and 391 vertices out of the list, gave up 0. Stages and tables are separate timers, so they add: 31.93, 31.85 and 29.62 ms. The timer fog broke even on the CPUs; the fixed table keeps what taking the faces out earns, 2.31 ms, on top of 3.58 ms of VDP1.

A 92 KB chunk holding the table ran low work RAM out, `sceneAllocLow` failed quietly and the faces stayed in the list. The CPU builds the table in VRAM at start now. The early runs of this build read `gone: 0 faces` because of it.

The sections after this one are the road here, trial and error included. Where they disagree with the design, the design and this table win.

## Floor and ceiling as the game draws them

**Was the 39 ms real? No.** The first page's tables did two 64-bit divisions a line, 5.54 ms a frame on the master in every way, `textured` too. By the CPU's divider, one a line, they're 0.67 ms, and the same view holds 33.43 ms in Mednafen: the main game's 33.1 ms median there on the Saturn stands. What the lab still lacks against the game: the
sim, sound and pipelining. The `textured` row now keeps no plane, so it is the game's way to compare with.

**Faces gone, not skipped.** `VDP2, skipped` (first called `vblank, kept`) leaves the faces in the rooms and each makes a skip command; `VDP2, removed` (`vblank, gone`) takes them out of the room data as a pack would, runs' counts and vertex indices with them (`planeFacesGone`): 402 quads, 391 vertices at `hall`. (Hints name room faces by index; 7A has none.)

**Real art (tools/planepack.py, chunks PLCE PLNM PLCL PLMP).** The level's own room textures: they're 64 x 64 4-bit, a texel 16 world units, which is what the plane already drew. RBG0 16-colour, 2 x 2 cells, two-word names, CRAM mode 1 (RAMCTL 0x13B0, unproven on the Saturn), 13 pictures in 26 KB (a texture laid across needs a turned copy; flips are the name's), 26 pages of names, 104 KB of bank B1's 128, maps of one-page planes, 32 sectors square, repeating.
Light is per 16 x 16 tile, a palette for each texture and level: 107 of 128, at every level. VDP1's gouraud adds level - 16 to each channel, so a palette is that sum, clamped.

**Fog by line.** The game's is a shade per 256 units past 12,288, and on a plane without roll depth is the line's: scale / 4. VDP2's colour offset A, RBG0 alone, adds as gouraud adds. First built with the SCU's hblank DMA, which froze the Saturn (below); now set at the lines where the fog changes by SCU timer 0's interrupt.

Mednafen, `hall`: pixel by pixel against the textured way, 4,282 of 84,480 differ, nearly all texel detail (VDP1 samples mips, the plane its full texture); in the bands that differ the means are within 1-5 of 255. What's missing against VDP1's floor: mips, caustics, per-pixel light.

Mednafen, `hall`, ms: VDP1, frame, the CPUs' stages; the tables, 0.91, are on top of the stages but for `textured`, which keeps no plane:

| Way | VDP1 | Frame | Stages |
|---|---|---|---|
| Textured | 29.02 | 33.43 | 28.77 |
| Colour 0 | 28.11 | 33.43 | 29.30 |
| Vblank, kept | 25.03 | 33.43 | 28.91 |
| Vblank, gone | 24.93 | 33.43 | 27.32 |
| Manual, gone | 24.33 | 33.43 | 27.32 |

So VDP1 -4.09 ms, and the CPUs 28.77 -> 27.32 + 0.91 = 28.23, -0.54 ms: taking the faces out saves about 1.6 ms against keeping them, and the fog's DMA and the vblank hook cost about 1.1 ms of the stages (with them off, `textured` went 29.94 -> 28.77).

**The user's read, Mednafen, b5e3394:** they couldn't tell the plane from the real scene, nor which mode they were in: "potentially very promising, we should keep pursuing". The page's modes now say who draws (`VDP1 draws all`, `VDP2, removed`...), and d-pad LEFT tints the plane red.

**Saturn, e247391:** real art, 2048 colours and two-word names all work, numbers below, repeated within 0.1 ms over two runs, 33.43 ms a frame: VDP1 25.56 -> 21.97 (-3.59), stages 32.02 -> 29.71 (-2.31), tables not on the page's table yet.

| Way | VDP1 | Frame | Stages |
|---|---|---|---|
| VDP1 draws all | 25.56 | 33.43 | 32.02 |
| Colour 0 | 23.10 | 32.03 | 31.88 (two half-second stalls, frames with their top half black: not chased, the way dropped) |
| Skipped | 22.09 | 33.52 | 31.60 |
| Removed | 21.97 | 33.43 | 29.71 |
| Manual | 21.39 | 33.43 | 29.89 |

**The fog by hblank DMA froze the Saturn, twice.** The first build's freeze had a clear cause (a level held off reads neither waiting nor moving, 0x00010030 is level 0's busy, as in libyaul), and fixing it moved the freeze to the fog row: one run with the plane pink (colour offset about +200, 0, +60, when the table holds nothing above 0), one with the colours right. Never explained: possibly my wait on D1's status in the vblank-out handler, if an armed level reads as waiting. Mednafen showed none of it. **Fog now by SCU timer 0's interrupt** at the lines where it changes, a peripheral the game already runs on this Saturn (saturn/system.cpp) and a CPU's few register writes, no bus master.

**Saturn, 6ba24f8: the timer's fog runs, all rows, no freeze.** VDP1 / frame / stages: VDP1 draws all 25.56 / 33.43 / 32.09; skipped 22.09 / 33.51 / 31.59; removed 21.98 / 33.43 / 29.79; manual 21.39 / 33.43 / 29.70; fog 21.98 / 33.37 / 30.88; tables 1.11, upload 0.71. So with the fog, VDP1 -3.58 ms and the CPUs break even (30.88 + 1.11 against 32.09): the fog 1.09, the tables 1.11.

But stretches of a second or so with VDP1's frame gone, the plane alone, through the fog row and when the camera starts moving; the recording samples every other field, so a one-field blink can read as gone. VDP1's times stayed normal. **My reading:** the change comes 335-540 cycles after VCNT reads 0 (bench, third round). With the upload moved inside the vblank for the DMA's sake, the CPU could be waiting at line 0 and plot at once, before the change: the list draws into the buffer going on screen, which the next erase empties, and an empty one comes on. Whether it lands before or after depends on a hundred cycles, so it comes and goes with the CPUs' timing. Upload back after the change, and the plot held to line 2. Not yet seen on the Saturn. It would also hit any erase-based plot at line 0: the main chat's clear wants this.

The user saw a solid vanish and a solid return, half a second to a second, not a flicker, so a list drawn into the wrong buffer isn't the whole story: that would blink field by field. The stretches measure 0.7-1.2 s, and that build gave up on a list twice at half a second each. So more likely VDP1 stopped ending lists, as it has on this Saturn when a frame changed under a plot (46563ec, a8ac37c): the same race, the same fix, a different result of it.

**Saturn, 0b88e9f, the load:** no vanishing, the fog row included. New: the floor flickers at rest. Measured in the recording: 1 frame in about 5, far floor 39 -> 46 and near floor 15 -> 10, ceiling and VDP1's pixels steady. Far floor too light, near floor too dark: the fog's later changes didn't arrive. The upload's DMA holds the bus VDP2's registers are on for about 11 lines at the display's top, where the ceiling's fog changes are; a handler held past its next line sets a compare the timer has already passed, and the field's remaining changes never come. It now sets what came due at once.

**Saturn, 0b88e9f, moving:** the frame still goes and comes back, now in runs of exactly 0.53-0.57 s: one give-up on a list, half a second, so VDP1 stopped ending it. The lab's camera moves add CPU, the plot slips later each frame, and a list comes to end inside the vblank, the late change's path, new to this Saturn. The handler ran the vblank hook, lines of VDP2 writes, before writing FBCR: a list ending near the vblank's end asks after it, the change waits for the next vblank, and comes under the next list's plot, what stopped VDP1 in Test 3 (46563ec). So plotting past line 0 wasn't the whole of it. Now FBCR first, and no late change in the vblank's last 5 lines: that list takes the next vblank.

**Saturn, 8a84bd3: no vanishing in 48 s, moving and at rest, and the floor steady.** VDP1 / frame / stages: VDP1 draws all 25.56 / 33.43 / 32.09; skipped 22.09 / 33.51 / 31.60; removed 21.97 / 33.43 / 30.08; manual 21.38 / 33.43 / 30.13; fog 21.98 / 33.43 / 30.97; tables 1.12, upload 0.78. The late change was the cause. VDP1 -3.58 ms; the CPUs from -0.71 ms to break-even over the last two runs (the fog 0.32 and 0.89).

The user still sees the ceiling flicker a little. The recording doesn't show it: it holds one field in two, so anything alternating with the frame is either always there or never. Reading: the upload's DMA holds the bus through the display's first lines on one field in two, the ceiling's fog late in those. The upload goes inside the vblank again, which the plot's hold past the change now makes safe.

**What the references say, read only after all that (the user's call, rightly):**
- libyaul's variable mode (`vdp_sync.c`) changes buffers at vblank-in only, if VDP1 has ended; at vblank-out it clears VBE and waits for scanline 0 before anything plots: "If plotting occurs... the VDP1 will abort the plot, resulting in a soft lock". No late change. Sonic R the same shape. The late change is ours (b5d651b, main too).
- Neither Sonic Jam nor Sonic R writes anything by line from the CPU or DMA. Distance is the line colour screen: Sonic Jam adds it (CCCTL 0x0110), its index in the coefficient's top 7 bits, one fixed hyperbola, no per-frame table at all; Sonic R blends it (CCCTL 0x0474, CCRR 0x1C, per-line table, blues). Their colour offset stays 0, for fades.
- The catch: those go toward a colour, and TR's fog goes to black. Whether the hardware can darken by line with no CPU is open. Idea to check: constant negative colour offset, plus line colour added back as depth falls; rests on the offset coming after colour calculation (not confirmed) and is limited by clamping at the top.

**Cost to come back to:** planeWrite's 272 VDP2 longs and 264 fog lines in the vblank hook, and the table's division a line. Sonic World's way needs neither division nor writes: a fixed hyperbola in VRAM, KAst and DKAst picking where to read, the matrix scaled by height. B0 has the room.

## The floor on RBG0: first results

`make lab LAB_SCENE=cistern LAB_PLANE=1 LAB=build/lab_plane`, on the card as `D:\VDP2 Floor`. Scene `cistern`:
room 103, captured from `../trs_cap` (dad9b03 + the spot), exact in the lab: 800 commands, VDP1 36.51 ms.

lab/plane.cpp: Sonic R's banks; 256-colour 16 x 16 cells; a sector is 64 texels; the camera's rows go straight into
VDP2's matrix (times 4, for its 10 fraction bits); k per line = height / (16 x 4 x ray's y), two words, per-frame in C;
transparent past the far plane or above the horizon. No roll at this camera (row 0's y is 0). Written in the vblank
the list it belongs to goes on screen.

In the list, faces square up at y 0 (see-through ones left alone, or the pools' water goes) take a blank texture and
draw colour 0 in their own place. Asm and C both: a colour 0 plain face has no gouraud.

**On the Saturn (photos, 2026-11-03, disc at 5ad3866): RBG0 from bank B draws right, under VDP1's frame, the text layer in
bank A above it, at 352 wide, and both erases work.** Cistern 103, 177 faces, 147 lines, 0 misses:

| Floor | VDP1 ms | Frame ms |
|---|---:|---:|
| Textured, as now | 25.64 | 33.43 |
| Colour 0, each face in its place in the list | 25.29 | 33.32 |
| Vblank erase + colour 0 strip from line 185, faces skipped | 21.51 | 33.43 |
| Manual erase, faces skipped | 20.92 | 33.43 |

Colour 0 per face saves almost nothing on the Saturn (0.35 ms), so writing a floor's pixels costs VDP1 about what texturing them does; the
saving is in not writing them. The floor costs 4.72 ms of 25.64 here; the strip costs 0.59.

The lab's far faces look wrong on TVs (bright moss, teal water, speckled walls) in every old capture and on the Saturn: not the
renderer's fault nor the plane's. Since ebaced3 a mip level takes its texture's colour table + 4 a level, and these captures predate those
tables, so they read the next texture's. The game at this spot looks right (`build/vdp2/newlist/main103.png`). Colour tables cost VDP1 no time.
lab.py's "the room order itself moves N pixels" has this in its N.

Mednafen, Cistern 103, 177 faces, 147 lines of plane, four ways round on A. (The first table here was wrong: this capture's
list holds a full-frame clear, which no game list does here. The page now turns it off in all four.)

| Floor | VDP1 ms | Frame ms |
|---|---:|---:|
| Textured, as now | 32.97 | 50.15 |
| Colour 0, each face in its place in the list | 31.17 | 33.43 |
| Vblank erase + colour 0 strip from line 185, faces skipped | 27.00 | 33.43 |
| Manual erase, faces skipped | 26.40 | 33.43 |

Skipping the faces leaves 1,143 px wrong at this view, both ways: what used to lie behind the floor now draws over the plane (a
wall's foot below floor height, the water past the platform). Those faces need Sonic's masks: colour 0 kept on the floor faces
with something below them, or the lower faces culled. Not done. The CPUs' work is unchanged. The grid meets the walls' feet within
a pixel (`tools/vdp2spy/planecheck.py`) and holds through a camera move (`build/vdp2/plane/dolly.png`). Not yet on the Saturn.

## Clearing with no drawing time

Every plane pixel must be 0 in the buffer before it's shown. VDP1 has two erases that aren't commands:

- **Vblank erase:** TVMR VBE + FBCR 3 at vblank-in. VDP1 sweeps the erase window (EWLR, EWRR) of the buffer on screen, in
  the vblank, as it changes buffers. It only fires when the change is certain, so a late list can't blank the picture. Reach is
  limited by the vblank: 39 lines at 224. **Sonic R does this at 352 x 224** (disassembly: 0601EAE0 writes TVMR 8, FBCR 3; EWRR
  0x58DF, the whole frame; 0601ECC4 then plots and writes TVMR 0), its list's first drawn command a colour 0 polygon over lines
  185-223, what the sweep doesn't reach. libyaul's variable mode is the same sweep. Mednafen starts it only if VBE is set on entering the vblank.
- **Manual erase:** FBCR 2 in the vblank before a list's last field. The buffer on screen is erased behind the beam, a line per
  line, no reach limit. Sonic Jam and libyaul's fixed mode. It has to be asked for a field ahead: if the next list isn't done by the change,
  the erased buffer goes on showing (issue 251).

**Sonic Jam's handler is a metronome** (06040D76): FBCR 2, then FBCR 3 + PTMR 2 (auto plot), every 2 fields, and it never looks at
VDP1. It has no way to run slower than its tick. A list still drawing at the change is cut by the next plot and its last, nearest
polygons don't show; if the CPU hasn't made a new list, the old one must be drawn again (the last read from the handler, not traced
in play). So its catch is lost polygons, not a lower frame rate, and with about 150 commands it never reaches it. libyaul's fixed
mode does the same on purpose ("Force stop plotting"). We can't: our lists fill the frame, and the nearest polygons are Lara.

The lab's way (lab/platform.cpp changeStart, by interrupt): at a vblank-in, ask for the erase if the list has ended or the last list's
time says it will end by the end of the next vblank. Else wait a field. A wrong guess shows the emptied buffer until the list ends: a
miss, counted at the vblank's end. C on the floor page forces one, to see what a miss looks like. A list ending inside the vblank still
changes in that vblank. For the vblank erase, a list ending after vblank-in isn't erased and the next list's first polygon covers the whole
frame instead of the strip.

Bench in Mednafen: vblank erase 192 lines at vblank-in, 0 when asked later (Mednafen only starts it on entering the vblank), the same
with the SCU's DMA writing VDP1 VRAM; manual erase 224; a list beside a manual erase no slower. On the Saturn: the bench's three rounds above.

Our October numbers need rereading. "85-170" was the lowest and highest over a run, and 170 was our own window's end
(2abe295), so the top of the range says nothing, and one late handler would explain the bottom. Nobody looked at how often
it fell short. A game that ships trusting 185 lines says the sweep can do better than that range reads. Measured since: 185 on this Saturn, every time.

## Tiles, not a mega texture

Sonic World's island is 61,440 one-word names over 229 16 x 16 tiles. RBG0 can also be a bitmap, at most 512 x 512
at 8 bits, which fills two banks: over Cistern's 22 x 21 sectors that is 23 texels a sector, against 64. Tiles it is.

TR's environment shadows are vertex light: one value at each sector corner, gouraud between. Across a floor face at
y 0, up to 12 levels of 32 (`tools/vdp2spy/planelight.py LEVEL7A 0`; 52 of 418 faces have one light). A name can
pick a palette for its cell, which costs no cell memory:

| Cell | Steps | Palettes used | Names | Average error, levels of 32 | Worst |
|---|---:|---:|---:|---:|---:|
| 16 x 16 | 32 | 45 | 6,688 | 0.28 | 0.50 |
| 16 x 16 | 8 | 14 | 6,688 | 0.98 | 2.00 |
| 8 x 8 | 8 | 14 | 26,752 | 0.99 | 2.00 |

(Averages are the same at both sizes because this is rounding to a step: the sampling grid matters for how smooth it looks.)

- One-word names, 256-colour cells: 8 palettes. The high nibble picks a texture's colours, so 8 steps over 16 textures.
- Two-word names, 16-colour cells, CRAM mode 1: 128 palettes of 16 colours, enough for this floor at 32 steps (45).
  Names are then 4 bytes: 27 KB at 16 x 16 cells, 107 KB at 8 x 8; the bank holds 128 KB.
- A shadow with a hard edge takes unique cells: 512 at 16 x 16, 8 bits; these floors use 12-50.
- Sonic World's own tree shadows are VDP1: a dithered sprite over the plane.
- Moving lights can only rewrite names: a CPU cost, in the blanking.

## Budgets for TR (`tools/vdp2spy/planes.py LEVEL`)

Whole level, flat quads by world height, all rooms:

| Level | Plane | Sectors | Faces | Rooms | Span (sectors) | Textures | Cells | Light values |
|---|---|---:|---:|---:|---|---:|---:|---:|
| LEVEL7A | floor y 0 | 418 | 418 | 22 | 22 x 21 | 3 | ~19 | 15 |
| LEVEL7A | ceiling y -7168 | 340 | 340 | 12 | 31 x 27 | 7 | ~35 | 20 |
| LEVEL7A | floor y -6400 | 150 | 150 | 5 | 21 x 48 | 4 | ~12 | 14 |
| LEVEL3A | floor y 3584 | 502 | 502 | 22 | 37 x 82 | 15 | ~50 | 24 |
| LEVEL3A | floor y 4864 | 79 | 79 | 6 | 9 x 22 | 6 | ~25 | 15 |

VRAM isn't the limit. A0 holds 512 16x16 cells at 8 bits and these planes need 12-50. A 4096 map at 64 texels a
sector covers 64 x 64 sectors; only LEVEL3A's 82 would have to fold or drop to about 50 texels a sector.

Our textures are 4-bit with a colour table each, which suits either layout under "Tiles" above. Sonic uses one
palette for its whole map. Not built, not tested.

## Ideas this opens

- **Floor and ceiling from one layer.** Sonic's ground + sky split is floor + ceiling indoors: set A below the
  horizon, set B above it, no RBG1. Cistern's floor at 0 and ceiling at -7168 are two of its biggest planes.
- **A sky for Lost Valley's open rooms.** Issue 267's fill can show set B's sky rather than black.
- **Two mip levels of one plane.** The line window can give near lines one map and far lines a filtered one.
- **Light in the name table.** The palette number per cell, baked at packing.

## Setup

`../trs_lod`, branch `vdp2-lab` from main 80d4bc5. `tools/mednafen` and `build/captures` are junctions to the
main checkout's. `make lab LAB_SCENE=ledge`, `python -I tools/lab.py --scene ledge`, `python -I tools/gauntlet.py`.
Captures come from the old renderer (`tools/capture.py`, a worktree of d10edc4 or before: `../trs_old`) so their
camera is OpenLara's, not retail's (1736b00); the renderer code is the game's own, compiled from `renderer/`.
Of the older captures none sees Cistern's floor or Lost Valley's (`valley` is a corridor); `cistern` is new, from
`../trs_cap`: `tools/hwtest.sh cistern-stamp` (spot added by hand), `capture.py ... --frame 2800 --keys 2400:R,R,R,R`. Don't
capture from `../trs_old`: its `build/data` is a junction to the main checkout's. `seldom` in rooms seen reads 40 off at this
scene, from main's changes to the walk, in made-up views only.

Gauntlet at 80d4bc5: seven scenes exact. `first` differs in 12 commands (8,446 px), all 12 flat in its capture and
textured now: d5b488e, a texture with no room in the cache takes the next level down. The capture is out of date
there, and the renderer is doing what it was changed to do.

## Repeating Sonic World (Mednafen, about a minute)

```
python -I tools/vdp2spy/spy.py start "external_resources/sonic_jam/Sonic Jam (USA).cue"
spy.py run 1800; spy.py tap START; spy.py run 180; spy.py tap START; spy.py run 300
spy.py tap START; spy.py run 300; spy.py tap DOWN; spy.py run 20; spy.py tap START; spy.py run 900
spy.py cmd "poke 060ffa50 00 00 00 00 00 00 00 00" poke     # sprites hidden
spy.py cmd "poke 060ff980 10 06" poke                       # RBG0 hidden
```

States at `build/vdp2/sonic_jam/` (world, river, bend), loaded with `spy.py cmd "load_state PATH" load_state`.
