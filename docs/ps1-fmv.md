# Playing the PlayStation's videos on the Saturn: design

2026-11-21. Status: design, nothing built.

This document covers playing Tomb Raider's ten full-motion videos (FMVs) from the
player's own PlayStation disc, decoded in software on the Saturn. The PlayStation's
picture is kept exactly, and none of it is re-encoded.

## Why

Both discs carry the same ten videos, cut from the same master: same edit, same
timing (15 frames a second, frame n is frame n), same 320x120 picture. They were
encoded separately, and the PlayStation's encode is the better one.

| | PlayStation (`FMV/*.FMV`) | Saturn retail (`FMV/*.CPK`) |
|---|---|---|
| Video format | MDEC (Sony's, see below) | Cinepak (Sega's variant) |
| Frame | 320x208, letterboxed: picture on rows 44-164 | 320x120 |
| Picture data | 193-219 KB/s | about 105 KB/s |
| Sound | XA ADPCM, 37.8 kHz stereo, 4 bits a sample | PCM, 22.25 kHz stereo, 16 bits a sample |
| Colour on screen | 24-bit | 15-bit |

How we know:
- **Comparison:** all ten videos were decoded with ffmpeg 7.0 and compared frame by
  frame. The mean brightness difference between the two discs' frames is 1.6-4.7 out
  of 255. Stills (PlayStation above, Saturn below, 2x) are in `build/fmv_compare/`.
  The Saturn shows 4x4 blocks and loses fine text (CAFE frame 700); the PlayStation
  doesn't.
- **Data rates:** read from the STR headers (frame sizes) and from the sector
  subheaders (sound sectors).
- **Sound:** the two mixes are the same: correlation 0.80-0.97 at zero offset. The
  PlayStation's higher sample rate carries almost nothing more: 0.01-0.04% of its
  energy lies above the Saturn's 11.1 kHz limit.
- **24-bit colour on the PlayStation:** its player (`FUN_80059964` in
  `build/ghidra_ps1/decomp.c`, the one that prints "FMV file %s not found") calls
  libpress's `DecDCTin` (`FUN_800606d8`) with mode 3. Bit 0 clears bit 27 of the
  MDEC command, which selects 24-bit output.

The Saturn's encode lost out mostly to its sound: 87 KB/s of uncompressed PCM leaves
the picture half the PlayStation's data.

Two smaller differences: CORELOGO is 320x208 full-frame on the PlayStation against
320x200 on the Saturn, and SNOW has 1,529 frames against 1,521.

TrekkiesUnite118 re-encoded Resident Evil 2's PlayStation videos into Cinepak for a
Saturn engine. We decode the PlayStation's own format instead. That avoids losing
quality a second time and needs no encoder in the builder. His Cinepak tricks
(double interlace, VDP2 Gradation to soften blocks, variable frame rate) answer
Cinepak's problems, which MDEC doesn't have.

## The PlayStation's format

Sources: psx-spx by Martin Korth (problemkaputt.de/psx-spx.htm), the chapters "MDEC
Decompression", "MDEC Data Format", "CDROM File Video STR Streaming and BS Picture
Compression", "CDROM XA Subheader" and "CDROM XA Audio ADPCM Compression". psx-spx
names Tomb Raider's `.FMV` files as standard STR with BS version 2. Our measurements
agree for all ten videos.

**Rule for this work:** the decoder is written from psx-spx and our own measurements.
ffmpeg is used only as an oracle: we run it and compare outputs, and never read its
source. The file header credits psx-spx. (The Cinepak player followed FFmpeg's code
without saying so; see the FMV provenance note.)

**Sectors.** Each video sector starts with a 32-byte STR header: ID `0160h`, type
`8001h`, sector number within the frame, sectors in the frame, frame number, frame
size in bytes, width, height. The other 2,016 bytes carry the frame's data. Every
8th sector is sound instead: XA, file 1, channel 1, coding info `01h` (stereo,
37,800 Hz, 4-bit). This holds in all ten videos.

**A frame** is a BS v2 bitstream:
- **Header:** 8 bytes: MDEC size/4, `3800h`, quantiser scale (1-5 in ours), version 2.
- **Bitstream:** 16-bit little-endian words, read from the top bit down.
- **Picture layout:** the picture is cut into 16x16 macroblocks, stored column by
  column (top to bottom, then left to right; psx-spx says native files do this, and
  step 1 checks it). Each macroblock is six 8x8 blocks: Cr and Cb (colour, at half
  resolution), then Y1-Y4 (brightness).

**A block** is coded like this:
- **DC:** 10 bits, signed. This is the block's average. The value `1FFh` in place of
  a Cr means the frame has ended.
- **AC values:** Huffman codes for (run of zeros, level) pairs, from psx-spx's table.
  The longest is 17 bits. `000001` escapes to a raw 16-bit value. `10` ends the
  block.
- **Dequantising:** the DC is multiplied by `qt[0]` (= 2). Each AC value becomes
  `(level x qt[k] x scale + 4) / 8`, saturated to -400h..+3FFh, and is stored at its
  zigzag position. `qt` is the standard 64-byte table psx-spx gives.
- **Inverse DCT:** turns the block's 64 frequency values back into pixels.
  psx-spx's fast version uses 80 multiplies and fills a column straight from its
  first value when the column's other values are zero.
- **YUV to RGB:** R = Y + 1.402 Cr, G = Y - 0.3437 Cb - 0.7143 Cr, B = Y + 1.772 Cb.
  Each result is clamped to -128..127, then 128 is added. One Cr/Cb value colours a
  2x2 square of pixels.

The hardware's exact rounding is unknown even to psx-spx, so we don't try to match
the PlayStation's chip bit for bit. Our Saturn decoder matches our own PC reference
decoder bit for bit, and the reference must agree with ffmpeg's output to within
rounding.

**XA sound** comes in 128-byte groups:
- **Header:** 16 bytes, holding a shift and a filter for each of 8 sound units.
- **Samples:** 112 bytes, 28 per unit, 4 bits each.
- **Stereo:** units alternate left and right, so a group holds 112 samples a channel,
  and a sector holds 18 groups.
- **Decoding:** sample = (nibble << shift) + (old x f0 + older x f1 + 32) / 64,
  clamped to 16 bits. f0/f1 come from the filter: (0,0), (60,0), (115,-52), (98,-55).

## What changes, end to end

```
  PlayStation rip (raw MODE2/2352 track)
        |  tools/fmvpack.py, run by `make disc` and by the builder
        v
  FMV/<NAME>.MDV on our disc: the PS1's frames and XA groups, repacked, never re-encoded
        |  CD streaming into the ring (as now)
        v
  master SH-2: decodes XA into the sound rings, schedules frames by the sound's clock,
               decodes its half of each frame
  slave SH-2:  decodes the other half
        |  straight into the hidden half of a VDP2 bitmap (or via RAM, if the bench says so)
        v
  VDP2 shows the new half from the next vertical blank
```

Without the PlayStation disc (or with a rip lacking raw sectors), nothing changes:
`FMV/<NAME>.CPK` and today's player.

## The file: `FMV/<NAME>.MDV`

It keeps the Sega FILM layout our player already streams: a header, a table of
chunks, then the chunks. `fmv.cpp`'s ring, chunk loop, skip handling (issue 308) and
HW_LOG sector checks therefore carry over.

- **Header:** `FILM`, its size, and `FDSC` with codec `MDEC`, the picture's height
  and width, and the sound as 2 channels, 4 bits, compression `X` (XA), 37,800 Hz.
- **Chunk table (`STAB`):** time base 15 (a frame's time is its number), and a 16-byte
  entry for each chunk: offset, size, time (`FFFFFFFFh` for sound), 0. `fmv.cpp`
  hard-codes Cinepak's time base 30 and 22,254 Hz; both are read from the file instead.
- **Sound chunks:** the PlayStation's XA groups, unchanged, about half a second each
  (9 sectors' worth: 18,144 samples a channel, 0.48 s, 20.7 KB). They're placed ahead
  of their video, as Sega's files do. Half-second chunks keep the table under the
  player's 64 KB header limit (CANYON: 2,709 frames + 378 sound chunks = 49 KB).
- **Video chunks**, one a frame:
  - **Header:** the frame's quantiser scale, then for each CPU's part: its first
    macroblock, its macroblock count, and its byte offset.
  - **The parts:** each part's blocks in the PlayStation's order, with their codes
    copied bit for bit. They're rewritten in the Saturn's byte order (a big-endian bit
    stream), so 32-bit loads deliver bits in order with no swapping on the Saturn.
  - **The split:** the builder splits each frame where its estimate of the decoding
    work (bits plus blocks) reaches half, so the master and slave finish together.

**Black rows dropped.** Nine videos are letterboxed in a 208-line frame. Macroblock
rows 0-1 and 11-12 are black in every frame, and fmvpack drops any row that decodes
to black in all frames (checked with the reference decoder). What remains is rows
2-10: 144 lines, 180 macroblocks, 1,080 blocks a frame instead of 1,560. Version 2
codes every block's DC on its own, so dropping blocks changes no other block: the
picture that remains is bit for bit the PlayStation's. CORELOGO keeps all 13 rows.

**Size and rate.** The ten videos come to 185.1 MB (153.5 MB of frames + 31.6 MB of
XA) against the CPKs' 147.6 MB, a little less once black rows go. The data track uses
88,271 of its fixed 90,000 sectors, so `DATA_SECTORS` (`tools/mkdisc.py`) goes to
110,000 for both kinds of disc. The disc grows from about 47 to about 51 minutes. The
Satiator's cached layout (`out.desc`) has to be refreshed once when the track size
changes. The heaviest stream, CANYON (219 KB/s of frames + 43.2 KB/s of XA, about
262 KB/s), uses 87% of the 2x drive's 300 KB/s.

## Decoding on the Saturn

**Work split.**
- **Master:** keeps the CD streaming, decodes XA, decides when frames are due, and
  decodes its part of each frame.
- **Slave:** decodes the other part, started with `slaveRun(task, arg)` and waited on
  with `slaveWait` (`renderer/platform.h`). Flags shared across the CPUs are read as
  volatile.
- **Overlap:** while frame N is on screen, both CPUs decode N+1 into the hidden
  buffer.

**Per block, in this order, written in C first and in SH-2 assembly by the end**
(the production standard):
1. **Bit reader and Huffman codes:** table-driven, with a first table on the top 8
   bits and small second tables for the long codes. The tables are sized to share the
   4 KB cache with the rest. (The SH-2's cache can also be split into 2 KB of cache
   and 2 KB of fast RAM, if the tables need a fixed home.)
2. **Dequantise and zigzag** into a 64-entry block.
3. **Inverse DCT:** psx-spx's fast 80-multiply version in fixed point, skipping
   columns whose other values are zero. The PC reference runs exactly the same
   integer steps.
4. **YUV to RGB:** the colour terms are worked out once for each 2x2 square, then
   added to each brightness value through a clamp table, and the pixel is written.

**Sound.** XA is decoded on the master, not the 68000:
- **Why not the 68000:** our measurements put the 68000 at 100-328 cycles a sample
  on the PlayStation's ADPCM, depending on the filter (Saturn-checked; see the sound
  plan). XA at 37.8 kHz stereo is 75,600 samples a second, more than the 68000's
  11.3 MHz can do.
- **Cost on the SH-2:** with its fast multiply, decoding should be about 15 cycles a
  sample. Writing the samples to sound RAM costs more, and the bench measures it.
- **Playback:** the samples go to the two looping SCSP slots the player uses today
  (24 and 25). Their pitch is octave -1, frequency number 731, which gives
  37,790.8 Hz, 0.024% slow. The sound is the clock, so the picture follows it, and
  CANYON ends 43 ms later than on a PlayStation.
- **Even work:** decoding runs a frame's worth (about 23 groups) at a time, so the
  master's work stays even.

**Picture: 24-bit, double-buffered in one bitmap.**
- **Layout:** VDP2 NBG0 as a 16M-colour bitmap, 512x256 at 4 bytes a pixel, which is
  all 512 KB of VDP2's video RAM. A line is 2 KB, so each 128 KB bank holds 64 lines.
- **Two pictures:** a 120-line picture fits twice, at lines 0-119 (banks A0/A1) and
  at lines 128-247 (banks B0/B1). The hidden one is in the other pair of banks from
  the one being shown.
- **Cycle slots:** a 16M-colour bitmap needs all 8 cycle slots of a bank it's read
  from (VDP2 manual, table 3.3). The shown picture's banks give all 8 to the bitmap,
  and the hidden picture's banks give all 8 to the CPUs.
- **Flip:** when a frame is due, the cycle slots swap and the vertical scroll moves
  to the new half, at the next vertical blank.
- **Window W0** leaves only the picture's lines on screen. The other lines would come
  from banks with no bitmap slots.
- **Getting pixels there:** each macroblock row is decoded into a strip in high work
  RAM (320x16 pixels, 20 KB), and SCU DMA moves the strip into the hidden half while
  the next strip decodes. Two strips fit in the renderer's 56 KB of scratch. fmvpack
  writes macroblocks row by row for this. It can, because version 2 codes every block
  on its own.
- **CORELOGO:** 208 lines don't fit twice, so it uses the same scheme in 15-bit
  (two 512x256 16-bit bitmaps, also 512 KB). A single 24-bit picture written behind
  the beam would need about 9 MB/s into the banks being shown, and the bench measured
  2.51 MB/s.

**Measured on the Saturn, 2026-11-21** (`fmvbench/`, `make fmvbench`, shots in
`build/hw_fmvbench_*.png`):
- **The layout works.** Both pictures show cleanly and swap without garbage. The
  test card's band tops (8 bits a colour) are smooth, and its bottoms (cut to 5) are
  banded, so all 24 bits reach the screen. Colours are in the right order.
- **Write speeds,** with picture A shown:

| Write | MB/s | SH-2 cycles a word |
|---|---|---|
| CPU into the hidden picture | 8.40 | 13.6 (32-bit; reads back exact) |
| CPU into the shown picture | 2.51 | 45.6 (lands only in the blanks) |
| SCU DMA into the hidden picture, 1,280-byte rows | 41.9 | 2.7 |
| SCU DMA into the hidden picture, one 240 KB move | 52.5 | 2.2 |
| CPU into sound RAM | 2.81 | 20.3 (16-bit) |
| CPU into low work RAM | 5.74 | 19.9 |
| CPU into high work RAM | 13.58 | 8.4 |

Mednafen gives the same numbers, except CPU writes into the shown picture: 10.7 MB/s,
because it doesn't model the cycle slots.

What the numbers mean for the design:
- **DMA, not CPU writes, into VRAM:** CPU writes would cost 0.52M cycles a frame for
  a 24-bit 320x120 picture. Strips in high work RAM cost 0.32M, and the DMA runs
  beside them.
- **Hot buffers in high work RAM:** low work RAM costs 20 cycles a longword to write.
  The CD ring can stay there: about 0.1M cycles a frame.
- **Sound RAM:** 20 cycles a sample comes to about 0.1M a frame for XA's output.

**Retail behaviour kept.** `fmvPlay(name, skippable)` keeps its interface, so these
stay as they are: CORELOGO and END can't be skipped, the 30-vblank fades, the video
order, and the centring in our 352x224 screen.

**Budget (estimates; the write costs are measured).** The SH-2s run at 28.64 MHz in
our 352-wide mode. A video frame lasts 66.7 ms (15 fps), which is 1.91M cycles a CPU.

| Work a video frame (both CPUs together) | Cycles | Basis |
|---|---|---|
| Huffman codes and dequantising | ~0.6M | ~20K codes a frame (14 KB average) x ~30 |
| Inverse DCT | ~0.9M | 1,080 blocks x ~800 |
| YUV to RGB, pixels to high work RAM strips | ~0.6M | 46K pixels x ~5 + 8.4 measured a write |
| XA and the sound RAM writes | ~0.15M | 5,040 samples x (~15 + 20.3 measured) |
| Strips into VRAM | DMA | 52.5 MB/s measured: ~3 ms of DMA, beside the CPUs |
| **Total** | **~2.25M: about 39 ms of each CPU's 66.7** | |

CORELOGO has 1.44x the blocks, about 3.1M cycles: 81%. It's the tightest video and
needs watching. Frames vary: the largest is 19.5 KB against a 14 KB average. The SCU
DSP isn't used. It's a possible later lever for the inverse DCT, but its DMA slows
the CPUs' cache fills about 5x while it runs (our DSP bench).

## Building it

`tools/fmvpack.py` is one tool, run by `make disc` (from `PS1TRACK`, the raw
PlayStation track) and by the builder:
1. **Find the files:** reads the raw track's ISO9660 directory for `FMV/*.FMV`.
2. **Read the sectors:** subheader submode bit 2 means sound: the XA groups are the
   2,304 bytes at offset 24. STR video sectors give their 2,016 data bytes, joined
   into frames and cut to each frame's size.
3. **Repack:** parses every frame (checking for `3800h` and version 2), drops black
   rows, splits the frame, and rewrites it in the Saturn's bit order.
4. **Write:** the FILM header, table and chunks, deterministically, so the builder's
   SHA-1 check of the data track holds on every machine.

**The builder** (`builder/discbuild.py`):
- **Raw sectors needed:** `Rip.data_track()` keeps only 2,048 bytes a sector, which
  cuts off XA sound sectors (2,324 bytes), so the PlayStation's video sectors are read
  raw.
- **Checking the rip:** `discs.json` gains a SHA-1 for each video's extracted frames
  and sound, not for raw sectors, so rips that differ only in the checksum bytes of
  sound sectors still pass.
- **Without raw sectors:** a PlayStation rip that isn't MODE2/2352 keeps the CPKs, and
  the builder says why.
- **Release:** `release.py` regenerates the "results" hashes.

**The game:** `fmvPlay` looks for `FMV/<NAME>.MDV`, and falls back to `.CPK`. Both
decoders stay in the binary, because the PlayStation disc is optional.
`tools/codesize.py` measures what the MDEC decoder adds.

## What this path lacks, against today's

- **Needs the PlayStation disc,** and a raw rip of it. Otherwise, today's videos.
- **Sound:** 4-bit XA instead of 16-bit PCM. It's the same mix and nothing above
  11 kHz is lost, but there's a little more quantisation noise. The Saturn's PCM
  won't fit beside the PlayStation's picture: about 306 KB/s for CANYON.
- **VDP2's whole video RAM during videos.** The HW_LOG sector check keeps its sums in
  bank B, so it moves (to high work RAM).
- **More code:** the second decoder.
- **A bigger disc** (about 4 minutes) and a one-time Satiator refresh.
- **The PlayStation's own lengths:** CORELOGO is 8 lines taller and SNOW is 8 frames
  longer. Step 1 checks what SNOW's extra frames are.

## Steps, and what each one proves

1. **PC reference decoder** (`tools/mdec.py`), from psx-spx:
   - **Picture:** its Y/Cb/Cr planes against ffmpeg's (`-f psxstr`, raw planes) for
     every frame of all ten videos, within rounding. A wrong bit in the parsing shows
     up as a large error, not a small one. It also confirms the column order and the
     black rows.
   - **Sound:** its XA output against ffmpeg's `adpcm_xa` decoder. This should match
     exactly, since the format defines integer maths.

   **Done 2026-11-21** (`5cecaf1`). Every 25th frame of all ten videos is within 3-4
   levels of ffmpeg (mean 0.08-0.13). The fixed-point IDCT is within 1 of the exact
   one. The sound is bit-exact (36.9M samples). Macroblocks go column by column, and
   Y2 is top right. The picture is in macroblock rows 2-10 (all 13 for CORELOGO).
   SNOW's 8 extra frames are a fade to black that the Saturn version cut.
2. **fmvpack + `.MDV` + `make disc` + builder.** The reference decoder reads the packed
   file and gives the same pictures as from the PlayStation's own file: the repack is
   lossless. Two runs give byte-identical discs.
3. **Saturn bench**, small and unaided:
   - the 16M-colour bitmap layout shows correctly;
   - write rates into the hidden banks during display (CPU and SCU DMA);
   - sound RAM write rate.

   This decides between 24-bit and 15-bit, and between writing straight to VRAM or
   via RAM.

   **Done 2026-11-21** (`1b89ce3`): 24-bit works, CORELOGO goes 15-bit, and strips go
   to VRAM by DMA. Results under "Picture" above.
4. **Saturn decoder in C:**
   - first on the master alone: frames captured in Mednafen must match `tools/mdec.py`
     bit for bit;
   - then split across both CPUs;
   - then timed in Mednafen: work and waits per CPU, in ms a video frame.

   **The C decoder, 2026-11-21** (`9fd544e`: `saturn/mdec.cpp`, timed by `fmvbench`):
   - **Exact on the Saturn:** 8 frames of 7 videos give `tools/mdec.py`'s pixels, on
     one CPU and on two.
   - **Too slow in plain C:** 191-221 ms a letterboxed frame on two CPUs (mean 205;
     Mednafen 199), and CORELOGO 279. That's 3.1x the 66.7 ms that 15 fps allows.
   - **Where the time goes,** per block: parse 2,892 cycles, IDCT 4,386, colour
     3,466. GCC calls a library routine for every arithmetic shift, because the SH-2
     only shifts arithmetically one bit at a time.
   - **What the frames hold** (`mdec.py`, sampled across 7 videos): 19% of blocks are
     DC only, with 13.4 AC values in an average block. The IDCT's first pass needs the
     full butterfly in 3.3 of its 8 columns; the rest are fills.

   **Assembly estimate (not measured):**
   - **Parse:** about 15 codes a block x ~28 cycles + ~100, so about 520 a block.
   - **IDCT:** products by `dmuls.l` + `xtrct`, constants in 16 fraction bits, so no
     shift routines at all. About 340 for pass 1 (fills where possible) + 600 for pass
     2, and nothing for DC-only blocks, which averages about 760 a block.
   - **Colour:** about 22 cycles a pixel, 8.4 of them the measured write to high work
     RAM, so about 7K a macroblock.
   - **Total:** about 14.7K a macroblock, 2.65M a frame: about 46 ms on two CPUs,
     before the bus is shared and before sound (~5 ms). The fixed-point format may
     change to suit the SH-2; `tools/mdec.py` changes with it and must still pass
     against ffmpeg.

   **The SH-2's fixed point, 2026-11-21** (`7316331`):
   - **The new format:** every product is a 64-bit `dmuls.l` with its middle word
     taken by `xtrct` (>> 16, rounded down). Constants carry 16 fraction bits, and
     pixels carry 8 through the IDCT into the colour, rounded once at the clamp table.
   - **No shift routines left.** It's closer to ffmpeg than before: within 2-3
     levels, mean 0.06-0.10.
   - **A reference bug fixed on the way:** the reference's colour step overflowed
     32-bit numpy for strong colours. 116 frames of all ten videos now give the C
     decoder's pixels exactly, checked big-endian (PowerPC under qemu).
   - **Saturn:** two CPUs take 153.9 ms a letterboxed frame (was 204.8). A block:
     parse 2,797 cycles, IDCT 2,315 (C), colour 3,187.
   - **The assembly IDCT** (`saturn/mdecidct.s`) equals the C on every block, but
     runs 10% slower on the Saturn (2,537). GCC schedules the 64-bit products well.

   **The gap:** the budget is about 3.5M cycles a frame for both CPUs (66.7 ms, less
   sound and streaming), about 3,240 a block. Today it's about 8,040.
   - **Parse and colour in assembly** should roughly halve them (to ~700 and
     ~1,550): GCC's colour loop spills registers to the stack every pixel, and each
     code costs ~180 cycles to parse.
   - **That alone leaves ~70 ms,** just over the line.
   - **The SCU DSP as a third processor for the IDCT** looks like the margin. About
     700 DSP cycles a block with its DMA, and DC-only blocks skipped, comes to ~44 ms
     of DSP time a frame, beside the CPUs, which would keep parse and colour: about
     42 ms each.
   - **Its risk:** our DSP bench measured its DMA slowing the CPUs' cache fills
     17.5 -> 83 cycles while it runs. Here that's about 10 ms of DMA a frame.

   **Parse and colour in assembly, 2026-11-21** (`d5bfdc6`, exact against the C on
   every macroblock):
   - **`saturn/mdecparse.s`:** a shift by a register's amount, which the SH-2
     doesn't have, is a multiply by 2^n. Rare codes go to a slow path. There's no
     saturation test: fmvpack checks no value reaches it, and none does in 22M
     sampled values.
   - **`saturn/mdeccolour.s`:** every value stays in a register. A 16-bit table of
     0x8000 | blue puts blue and VDP2's top bit in place with one shift.
   - **Saturn:** two CPUs take 107.4 ms a letterboxed frame (was 153.9), and
     CORELOGO 147.7.
   - **Per block, C against assembly:** parse 2,820/1,645, colour 3,176/1,884; the
     IDCT stays 2,341 in C.

   **What's left** (budget ~3,240 cycles a block; now ~5,400):
   - **The DSP takes every IDCT** (about -1,900 a block on average).
   - **Colour:** hide load stalls by working on two pixels at once (about -15%).
   - **Parse:** for the common levels (±1, ±2), the scaled and prescaled value
     comes from a table built for each frame. That skips two multiplies a code.
   - **Projection:** about 2,970 a block on the CPUs, so ~56 ms on two CPUs plus
     ~4 ms of sound and streaming, about 60 ms of 66.7.

   **The DSP's second pass, 2026-11-21** (`12d476b`, `fmvbench/gen_idct2.py`):
   - **How it works:** each butterfly step runs across a block's 8 rows, so the
     DSP's address counters step through arrays of 8. It's 197 program words.
   - **Exact:** in the simulator (360 blocks) and on the Saturn (240 blocks).
   - **Alone,** it takes 1,411 SH-2 cycles a block.
   - **Beside the CPU's colour work, it doesn't pay:** it slows to 2,123 a block
     (+50%), and it slows the colour 19% (10,515 -> ~12,480 a macroblock). Its DMA
     and the CPUs share the bus.
   - **The trade:** it would save ~1,100 cycles on each block with AC values, but
     slow every CPU memory access while it runs. That nets perhaps 5-10 ms a frame,
     and the DSP itself would need ~60-65 ms a frame under contention. Set aside
     unless the CPU path falls just short.

   **Where 15 fps stands:** about 3,050 cycles a block are needed against 5,430
   today, so each stage must get about 45% faster.
   - **Colour:** 44 cycles a pixel measured against ~21 instructions. Its tables
     and blocks fill most of the 4 KB cache, and 17% of its pixels are letterbox
     black that needn't be made.
   - **Parse:** two multiplies a code; a per-frame table of the common values would
     skip them.
   - **IDCT:** the assembly isn't yet better than GCC's, and sparse blocks could
     take shorter paths.

   A realistic landing is 13-15 fps. Frames that miss their time would show late
   while the sound keeps the clock. CORELOGO's full frame is 1.44x the work and
   won't reach 15 on any CPU path; the retail Saturn's own CORELOGO (10 s, nearly
   the same picture) is the fallback.
5. **Assembly hot paths**, matching the C bit for bit. Then a Saturn run on CANYON
   (the highest data rate) and CORELOGO (the most blocks), counting late frames and
   checking sound and picture stay in step.
6. **Release:** discs.json, the builder's text, the README.

## The other route: re-encoding the PS1's picture to Cinepak, measured

TrekkiesUnite118's route for RE2: decode the PlayStation's video on a PC and
encode it again as Cinepak, which the Saturn decodes with table lookups and
copies. The question was whether the PS1's picture survives that second
encoding, or whether a better source would be needed. Measured 2026-11-22 with
FFmpeg 7.0's Cinepak encoder on three 10-second clips (150 frames each, the
120-line picture area). Each clip was scored against the PS1 frames themselves.
The retail Saturn's Cinepak, same frames, is the yardstick. The script and
pictures are in `build/fmv_compare/cinepak/` (not in git).

| Clip | Version | Video KB/s (mean / worst second) | Luma PSNR | SSIM |
|---|---|---|---|---|
| CAFE 600-749 (dark interior, magazine text) | retail Saturn | 87 / 139 | 37.3 dB | 0.975 |
| | Cinepak from PS1, q2 | 109 / 228 | 42.6 dB | 0.981 |
| | Cinepak from PS1, q1, 16 strips | 149 / 335 | 44.2 dB | 0.986 |
| PRISON 250-399 (dark, red light) | retail Saturn | 100 / 133 | 38.4 dB | 0.984 |
| | Cinepak from PS1, q4 | 116 / 153 | 42.2 dB | 0.988 |
| | Cinepak from PS1, q1 | 199 / 265 | 45.5 dB | 0.993 |
| CANYON 1400-1549 (rock detail, the heaviest video) | retail Saturn | 132 / 168 | 36.2 dB | 0.956 |
| | Cinepak from PS1, q4 | 175 / 312 | 38.9 dB | 0.970 |
| | Cinepak from PS1, q2 | 234 / 366 | 40.4 dB | 0.978 |

- **The PS1 picture survives the re-encode.** At data rates that fit a 2x drive
  beside sound, Cinepak lands within 39-45 dB of the PS1 picture. The retail
  Saturn's encode lands at 36-38 dB. That comparison flatters the transcode,
  because the retail was encoded from the master, not from the PS1 picture.
  Still, at 2x zoom the transcodes are hard to tell from the PS1. No better
  source is needed for this route; a better one would only help to beat the PS1's
  own picture.
- **What Cinepak still loses, at 4x zoom:** 4x4 blocks in fine texture (CANYON's
  rock, CAFE's background) and a faint green cast in the darkest areas. Text stays
  close to the PS1's; the retail's text is blockier.
- **Detail costs data.** CANYON needs about 230 KB/s to look as close to the PS1 as
  the dark clips do at 110-150. That fits only beside sound the size of the PS1's
  XA (43 KB/s), not the retail's 16-bit PCM (88 KB/s).
- **No rate control.** FFmpeg's Cinepak encoder takes a quality setting, not a data
  rate. The builder would pick the setting per video (or per scene). At the best
  settings the worst one-second windows reach 265-375 KB/s, so the player needs a
  few seconds of buffer, or the builder must cap each second's bytes.
- **Strips and keyframes barely matter:** 16 strips against 8 is worth +0.2-0.3 dB
  for 3-6% more data (CANYON +1.6 dB for +22%). A keyframe every 60 frames instead
  of every 12 saves 2-8%.
- **15-bit colour costs more than the re-encode.** Today's player draws 15-bit
  pixels (the low 3 bits of each channel dropped). Applied to the in-budget
  encodes, that takes CAFE from 44.2 to 36.0 dB, PRISON from 45.5 to 40.0 dB and
  CANYON from 38.9 to 35.6 dB. Either route should show 24-bit, and the 24-bit
  double-buffer from step 3 carries over unchanged.

**Decoding it on the Saturn, measured 2026-11-22** (`make cpkbench`;
`saturn/cinepak.cpp`, plain C; frames from `tools/cinepak.py benchframes`, held in
high work RAM, so this is the decoder without the CD). Each run is the heaviest
six frames of its clip, a keyframe and the five after it:

| Run | Data rate | 1 CPU, slowest / mean | 2 CPUs, slowest / mean |
|---|---|---|---|
| CANYON q1, 16 strips | 513 KB/s | 46.8 / 46.4 ms | 24.4 / 23.9 ms |
| CANYON q2, 8 strips | 365 KB/s | 36.2 / 35.9 ms | 18.3 / 18.1 ms |
| CAFE q1, 16 strips | 348 KB/s | 37.4 / 34.1 ms | 21.2 / 18.5 ms |
| Random pixels, q1, 32 strips: the worst a frame can be | 831 KB/s | 71.5 / 71.5 ms | 35.5 / 35.5 ms |

- **Exact:** every picture's checksum matches `tools/cinepak.py`, whose decoder
  matches ffmpeg's pixel for pixel (600 frames, `cinepak.py check`).
- **The picture's trip to VRAM** by SCU DMA, a row at a time: 3.49 ms.
- **Played live** at 15 fps on both CPUs through the 24-bit double buffer: no late
  frames, the random pixels included.
- **Mednafen** reads 6-9% low (one CPU: 43.2, 32.8, 34.1, 66.9 ms).
- **The CPUs aren't the limit.** On two, the worst frame Cinepak can make takes
  35.5 + 3.5 ms of the 66.7. On one, the real frames fit 15 fps and leave the slave
  free. The limit is the CD: a 2x drive reads 300 KB/s, and the best setting
  averages up to 336 KB/s (CANYON), with seconds of 513.
- **The strip rule** (`cinepak.h`): each strip's vectors use only codebook entries
  that strip sent in the same frame. ffmpeg's encoder keeps it in every frame
  checked, and the builder must check it. It lets either CPU take any strip with one
  codebook of its own. The slave takes the strips after half the frame's bytes.
- **Where the time goes,** estimated, not measured: mostly the codebooks. At q1 a
  strip can send 512 entries every frame, each converted to 4 pixels.

**The CD drive, measured 2026-11-22** (`make cdbench`). The disc holds 1500 sectors
of a pattern on the data track, and the same on a last track of Mode 2 Form 2
sectors (`tools/mkdisc.py --form2`). Each is read start to end with the CD block
checked once a frame, as a player would, and every word is checked.

| | Saturn (Satiator) | Mednafen |
|---|---|---|
| Mode 1 (2048 bytes a sector) | 150.0 sectors/s: 300 KB/s | the same |
| Mode 2 Form 2 (2324 bytes a sector) | 149.7-150.0 sectors/s: 340 KB/s | the same |
| First sector after the play command | 66 ms nearby, 183 ms across the disc | 117-133 ms, 618 ms |

The master's cycles to move one Mode 1 sector out of the CD block, the commands
included:

| How | Saturn | Mednafen | Share of a CPU at 150 sectors/s |
|---|---|---|---|
| CPU, 16-bit reads (`cd.cpp` today) | 41,263 | 32,532 | 21.6% |
| CPU, 32-bit reads of the port at 0x25818000 | 26,373 | 16,667 | 13.8% |
| The SH-2's DMA controller, 16-bit | 24,392 | 20,167 | 12.8% |
| SCU DMA from the 32-bit port (waited for) | 12,175 | 8,986 | 6.4% |

- **Form 2 sectors** come over as 2336 bytes (subheader, data, EDC): cpu32 29,858
  cycles, SCU 13,733.
- **The SCU's DMA** needn't be waited for. A player can start it and go on decoding.
- **A quirk:** reading from the Form 2 track's first sector, straight after the last
  audio track with no pregap, gets its sectors from 37 on, every time. From sector
  100 on, every sector arrives, through the track's end. Mednafen delivers all of
  them. A real disc gives a data track after audio a 2-second pregap; the videos
  should start clear of the track's first sectors.
- **Form 2 drops the third layer of error correction** (ECC). Inference, not
  measured: an SD card never needs it. On a burned disc, an error CIRC can't fix
  would spoil part of a picture rather than stop the stream.

**What's left for the picture**, at the retail Saturn's sound quality (16-bit PCM,
22.25 kHz stereo, 86.9 KB/s). The same sound compressed losslessly, every sample
kept, measured with FLAC on all ten videos' retail sound: 63.5% of the size with
LPC, 65.9% with only fixed predictors (the simplest to decode), so about 57 KB/s.

| | Retail PCM, 86.9 KB/s | Lossless, about 57 KB/s |
|---|---|---|
| Mode 1, 300 KB/s | 213 KB/s of video | 243 KB/s |
| Form 2, 340 KB/s | 253 KB/s | 283 KB/s |

Against the clips' best settings (q1: CAFE 149, PRISON 199, CANYON 277 with 8
strips, 336 with 16), Form 2 with lossless sound fits all but CANYON's 16 strips,
before any buffering.

**The sound on the 68000, measured 2026-11-22** (`make lossbench`,
`saturn/fmvsound.68k`, `tools/lossless.py`). The sound CPU decodes the packed
sound out of sound RAM into the two rings the slots play from. So the SH-2s only
copy packed chunks in (about 63 KB/s), and sound RAM holds a 256 KB ring of them,
about 4 seconds: the sound side of the buffer.
- **The first format, Rice coding, was too slow.** It packed to 68.4% of the PCM,
  but cost 315 cycles a sample in Mednafen on the clip that packs worst, against the
  44,530 samples a second playback needs (35,804 a second). The 68000 has no barrel
  shifter, and with sound RAM's waits a bit pulled from a bit stream costs it about
  27 cycles.
- **The format now has no bit stream.** Each block of 128 samples keeps each
  residual as a byte, a 2- or 4-bit field and a byte, or 16 bits whole. A sample is
  then one table lookup and about 13 instructions. It packs all ten videos' sound
  to 72.6% (57.5% PYRAMID to 79.3% VISION), about 63 KB/s against the PCM's 86.9.
  Measured on PC, a 6/8/10/12-bit choice would reach 71.8%, and Rice 68.4%.
- **Saturn:** 91,044 samples a second, 124 cycles a sample (Mednafen 92,067), with
  the two slots playing or not, so the 68000 is about 49% busy. The output equals
  `tools/lossless.py`'s sample for sample. Played in real time from the packed ring,
  the clip's whole 8 seconds, four times over, the decoder stayed at least 16,012
  samples ahead of the slots. Both rings matched the PC at the end of every round:
  the predictors carry from chunk to chunk, so a slip anywhere would show. An OBS
  recording of the playback is the clip (waveform correlation 0.33-0.36 in 0.25 s
  windows, against 0.06-0.08 for other parts of CANYON).

**Every video planned, 2026-11-22** (`tools/cinepak.py encode`, `tools/fmvplan.py`).
All ten videos were encoded whole at q1 (16 and 8 strips), q2, q4, q8, q16 and q32,
and the same with the PS1 picture's flicker averaged out first (ffmpeg's
`atadenoise`). Then a player was simulated sector by sector:
- the drive reads into a buffer (the RAM ring and the CD block's sectors together)
  while there is room;
- frames are due 1 s after the start plus their time, and sound 0.5 s before it plays.

Each group of 12 frames starts with a keyframe, so groups from different settings
join freely. While anything is late, the largest group in the 6 seconds before it
steps down a setting.
- **Some stretches are heavy at any setting:** CAFE's first seconds, SNOW's, END's
  and MANSION's run 320-360 KB/s even at q4. Without q8-q32 to step down to, most
  videos couldn't stream at all.
- **Shares of frames at q1** (16 or 8 strips), 1 MB of buffer:

| | Retail PCM | Lossless sound | The same, flicker averaged |
|---|---|---|---|
| Mode 1 | 71% | 75% | 80% |
| Form 2 | 83% | 89% | 92% |

  With 512 KB of buffer, Form 2 and lossless sound: 87%.
- **Averaging out the flicker** saves 6-8% of the data rate at each setting, and
  costs 0.2-0.3 dB against the PS1 picture.
- **MANSION** barely changes in size between q1 with 8 strips and q4 (220 against
  217 KB/s). Not yet looked into.

## Decisions

1. **24-bit colour** (the user, 2026-11-21), as the PlayStation shows it. 15-bit is
   the fallback only if step 3 rules 24-bit out.
2. **CORELOGO** is the Core Design logo video (the first one at boot), and the only
   one whose picture fills its whole 320x208 frame. Two 208-line 24-bit pictures need
   416 lines, and VDP2's memory holds 256, so it can't be double-buffered in 24-bit.
   **Decided by the bench: 15-bit, double-buffered.** A single 24-bit picture
   updated behind the beam would need about 9 MB/s into the banks being shown, and
   the Saturn takes 2.51 MB/s there. If its 15-bit colour shows banding,
   TrekkiesUnite118's full-screen Cinepak (RE2) shows there are other routes.
