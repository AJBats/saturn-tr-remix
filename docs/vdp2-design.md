# Floor and ceiling on VDP2: the design, before any more code

Written after the references, because the first build was made without them (`docs/vdp2-lab.md` has the history). Scene: `hall`, Cistern, the walkway. All numbers Saturn unless marked.

**Built and run on the Saturn, 1f9e607, 2026-11-03.** The fixed table looks right and doesn't flicker. Results are under "The test, and what it showed".

## What's proven and stays

| Part | Copied from | State |
|---|---|---|
| RBG0, set A floor, set B ceiling, split by line | Sonic Jam (RPMD 3, WCTLD 0x0008) | Runs on the Saturn; W1 is a rectangle where Jam has a line table |
| Bank B: B0 cells, B1 names, spare slots F | Sonic R (RAMCTL 0x03B0) | Runs |
| Level's own textures: 16-colour cells, two-word names, a palette per texture and light step, CRAM mode 1 | Ours (Jam has CRAM mode 1, 256-colour cells) | Runs |
| Faces on the planes out of the room data, 402 quads, 391 vertices | Ours | Runs; -1.4 to -1.8 ms of stages against the skipped way |
| Vblank erase + 39-line strip at the list's head | Sonic R | Runs; 320 of 320 erases full |

## What changes

### 1. The buffer change: libyaul's rules, nothing of ours

`libyaul/scu/bus/b/vdp/vdp_sync.c`, variable mode, in full:

- **At vblank-in**, if VDP1 has ended (CEF, or the sprite end interrupt): TVMR VBE, FBCR FCM|FCT, nothing slow before them. If it hasn't: nothing, and look again next vblank-in.
- **At vblank-out:** clear VBE, and don't plot until scanline 0. Its words: "to avoid the frame buffer change from aborting plotting, resulting in a soft lock". The interrupt runs at scanline 511, before the display.
- **Upload, then plot**, after that.

Sonic R has the same shape (0601EAE0 TVMR 8, FBCR 3; 0601ECC4 plots, TVMR 0).

**Goes:** the late change (a list ending inside the vblank changes at once), and with it the whole-frame fill after one, its margin in the vblank's last lines, and my moves of the upload. Every change then comes with an erase, so every list pays the same 0.59 ms strip.

**What that lacks:** a list ending in the vblank's 2.5 ms waits a field, so the deadline for 30 fps is 30.9 ms of list from line 0. At `hall` lists are 22 ms. The game's own late change stays the main chat's business (see the end).

### 2. Depth and fog: Sonic Jam's fixed table

Today the CPU works out 224 scales a frame and writes them to VRAM (0.91-1.12 ms), then sets fog by timer interrupt (0.32-0.89 ms). No retail game surveyed does either. Sonic Jam's table never changes.

**The table**, in B0, written once, 23,552 two-word entries (92 KB, built by the CPU at start, 0x48000 up to 0x5F000): entry *i* holds, for a ray `down / height = 1.6 i`:

    bit 31       transparent: past the far cut (i < 128)
    bits 30-24   line colour index: how much of the fog is added back
    bits 23-0    k = 2^24 / (1.6 i)

**Each frame**, each set: KAst = the first line's index, DKAst = its step a line, both from the camera's pitch and height over that plane (`r[4]`, `r[7]`, a division or two). Set B walks backward, as Jam's does. The matrix stays as it is. A frame's whole cost: 48 longs of parameters in the vblank.

**Darkening by line, with no CPU.** Jam's haze adds, and TR's fog takes away, so this part is mine:

    final = clamp( min(255, plane + L(line)) - F )

L is the line colour screen added to RBG0 (CCCTL 0x0110, LNCLEN 0x10, KTCTL 0x1111: Jam's), F a constant colour offset on RBG0 alone (CLOFEN 0x10). Near, L = F and it cancels. Past 12,288 units L falls 8 per 256 units, the game's fog, which adds as gouraud does.

- **Rests on the colour offset coming after colour calculation.** Mednafen's mixer has it so (`vdp2_render.cpp` T_MixIt), and it was written against the hardware; I have no Sega text saying it. To be seen on the Saturn.
- **F = 112.** The Cistern's plane colours reach 144 of 255, so `plane + F` clips only that top step near the camera (255 - 112 = 143). At full fog (14 steps, 15,872 units) anything up to 112 is black and the brightest texels keep up to 32 of 255, where the game's are black by 20,480. 6% of palette colours are above 104, 1.7% above 127.
- **Line colours:** 15 greys in CRAM's last palette block, 0x780. Palette 127, today's black, moves down to make room. 107 palettes + black + this fit.

**What it lacks:** the near limit. The last entry is 111 units away, so a camera within about 40 units of a plane reads past the table; a pad of repeated entries, or the plane off, covers it. Accuracy at the far end is 1 entry in 128, as Jam's is.

### Measured (Saturn, `hall`, 1f9e607)

| ms | `VDP1 draws all` | `VDP2, timer fog` | `VDP2, fixed` | Estimated for fixed |
|---|---|---|---|---|
| VDP1 | 25.56 | 21.98 | 21.98 | 21.98 |
| CPUs' stages | 31.93 | 30.83 | 29.52 | about 30.1 |
| Tables, per frame | 0 | 1.02 | 0.10 | about 0.05 |
| CPUs in all | 31.93 | 31.85 | 29.62 | about 30.2 |
| Frame | 33.43 | 33.43 | 33.43 | |

Upload 0.70, 402 faces and 391 vertices out of the list, gave up 0.

## What the floor on VDP2 still lacks against VDP1's

- **Mips:** RBG0 samples one texel a pixel, unfiltered, so far floor may shimmer in motion. Not separately judged against VDP1 on the Saturn. A plane can't mip by line, but a far tile set can be built coarser.
- **Light per tile:** 16 x 16 texels, against VDP1's gouraud across a face. In Mednafen the user couldn't tell.
- **No caustics or moving light.** Water-lit floors stay VDP1.
- **One floor height and one ceiling height a view.** The biggest flat at this spot, the walkway, 12,500 pixels in 21 faces, stays VDP1. A plane's height is a number per frame, so it can follow Lara's room, but what lies below it then shows through.
- **What's below shows through.** No masks yet: at `hall` nothing does, at room 103 one pool polygon. The user's obstruction planes are pencilled in for the main chat.
- **No roll.** The per-line scale and fog both need it level. TR's camera not yet checked for roll (earthquakes, death camera).
- **Costs VDP2 bank B whole and all of CRAM's use.** The text stays in bank A. A HUD on VDP2 would share it.
- **Per level, by hand so far:** heights are constants, Cistern's. A pack step has to choose them, and rooms that flip need thought.

## The test, and what it showed

One disc, rows: `VDP1 draws all`, `VDP2, timer fog`, `VDP2, fixed`. All three questions below rest on the user's eye on the Saturn, with no recording of this run; the table was read from the capture card.

1. Colour offset after colour calculation on the hardware: the far floor dark, not bright or flat. **Looks right.**
2. Line colour from the coefficient's top byte with no coefficient bank in RAMCTL. Jam has a bank; Sonic R has none but no line colour in its table. **No bank needed: the fixed row looks right as it is.**
3. No vanishing, no flicker at rest or moving, on libyaul's change alone. **None in the fixed row, gave up 0.** The timer fog row still flickers on the ceiling, so the flicker belongs to that fog, and the fixed table writes nothing by line for anything to disturb.

The upload runs in the vblank in this build, and the timer fog's flicker is still there, so my earlier reading (the upload's DMA holding the bus at the display's top) is wrong or incomplete. Its cause is not known. With the fixed table the timer fog isn't needed, so it isn't being chased.

## References read, and what they gave

- **libyaul** `vdp_sync.c`: the change, above. `scu/dma.h`: LWRAM locks DMA up; level 0's busy mask includes "interrupted".
- **Sonic Jam**: plane, sets, fixed table, line colour, CRAM mode 1. Colour offset only for fades.
- **Sonic R**: bank layout, strip, line colour table by line (blue) blended at a fixed ratio (CCCTL 0x0474, CCRR 0x1C). Colour offset only for fades.
- **SlaveDriver**: does have a plane: RBG0 as the sky, a 512 x 256 bitmap (`PLAX.C`), k_contrl 1, in bank A. Colour offset for whole-screen fades. No floors.
- **Sega Rally**: RBG0 behind its menus, colour offset fading; its race not read yet.
- **SGL** (`SL_DEF.H`): `slMakeKtable`, `slKtableRA`, `K_LINECOL`: Sega's library has this same table and line colour as a named feature.
- **Mednafen** `vdp2_render.cpp`: the mixer's order.
- **Wanted:** Panzer Dragoon 1 and 2, the user's offer. A dark game's plane would be the best check of the darkening.

## For the main chat

- A plot straight after vblank-out can soft-lock (libyaul's words). A change asked for after the vblank's end comes under the next plot, which stopped VDP1 here for half a second at a time.
- The game's late change (b5d651b) is what that path is. It earned 30 fps at the Cistern water, so it isn't to be dropped lightly, but its FBCR needs to come before any slow work, and not in the vblank's last lines.
- `DSTA_D0_BUSY` wants 0x00010030: a level held by a higher one reads neither waiting nor moving.
