# Floors and ceilings on VDP2, in the game

The lab proved the plane on the Saturn (`docs/vdp2-design.md`, `docs/vdp2-lab.md`; its code stays on branch `vdp2-lab`). This is how it went into the game, in the Cistern and Lost Valley, with the user's decisions of 2026-11-04. Numbers are from the Saturn only where they say so.

## The short version

- **Each level names a floor height and a ceiling height.** The pack step takes those faces out of the level for good, dry rooms and flooded copies alike, and writes them as VDP2 tiles. Nothing in the room or face code changes or branches: the faces just aren't there. That saves their CPU time, their VDP1 time and their memory together.
- **Everything else stays polygons:** pits, dips, ramps, water surfaces, walls. Only a flat, opaque face at exactly that height moves.
- **It's a pack-time step.** `tools/pack_level.py` makes every level from the retail disc's files at build time, and this is part of it: the repo holds the rules and the hand's changes, never level data.
- **No exceptions to start with.** We look in the game. If a spot looks wrong, a line in the level's list keeps that face on VDP1.
- **New code is one file, `saturn/plane.cpp`, about 200 lines:** load, on and off, and a few numbers a frame.

## Which flats (`tools/vdp2spy/planesurvey.py`, `docs/vdp2-plane-maps.png`)

| | Faces | What it is | Notes |
|---|---|---|---|
| **Cistern floor, y 0** | 209 + their 209 flooded copies | The ring round the great pool, 22 x 21 sectors | Below |
| **Cistern ceiling, y -7168** | 340 | The hall's roof, 31 x 27 sectors | Dry, and no room of it flips |
| **Lost Valley floor, y 3584** | 462 of its 482 | The main stretch, from the mouth to the T-rex: x 35-66, z 4-41 | A deep pool in room 38, dips in 51, 56 and 15, all polygons. The 20 elsewhere (rooms 15, 18, 19, 21, 24) stay polygons |
| Lost Valley ceiling | none | Open sky | The second plane is free: issue 272's skybox could use it later |

None of these faces is animated (checked against each level's `anim_ranges`). All are sector squares. Next biggest in the Cistern: ceiling -11264 (157), floor -6400 (150), the walkway -3328 (130). They stay on VDP1.

**Nearly flat:** floor heights are on TR's grid of 256 units (LEVEL3A: 9,886 vertices on it, 3 off it). So there's nothing "nearly" flat by less than 256. In the valley, by the plane: 45 flats a step above it, 8 two steps, 3 a step below, 34 + 5 ramps of one step touching it. Flattening a ramp moves its far edge by 256, a third of Lara's height, and her feet follow the collision data, not the picture. It's the user's call spot by spot, as we come on them; the list can name a face to flatten.

## What VDP1 paints over the plane

VDP1 is always in front. A floor face used to hide whatever lay beyond it on screen; on the plane it can't. So the far wall of a pit shows through the near floor when it reaches past the rim on screen, and an item down a pit shows over the floor.

What's known:

- In the lab the pool going down through the floor looked right at `hall`. At room 103 one pool polygon did show through (1,143 px, Mednafen's count), on the Saturn too.
- It's limited: a room's faces wholly outside its portal's window are culled, so what leaks stays within about a sector of the rim.
- A census over random views (`tools/occmodel`, `planes` mode), camera in the hall's rooms: 241 px a view on average against 11,371 of plane, 8% of views past 400 px, worst 27,444. In the valley 99 px, 8%, 15,636. Random views are rates to compare, not what play will see, and the model has no items.
- Sonic World meets the same thing at its river and answers it with 3 to 5 quads a list along the near bank, which write low priority so the plane shows (`docs/vdp2-lab.md`).

**Seen in Mednafen, 2026-11-04:** the user finds the plane itself without visible fault, and the faces sneaking under it the one big quirk, as expected: in the Cistern they just need trimming, which occlusion primitives would do, and masks are for a face flush against a ledge that straddles a view above and below the plane. **Pinned** until the main chat has occlusion primitives better understood.

**So: nothing kept for now.** What we can reach for, in order of work:

1. Keep that floor face on VDP1 (a line in the list). Keeping the worst 80 in each level cuts the census's mean by 60 times in the hall and 10 in the valley, at about a quarter of the floor's gain, and can show a small step of light at the join.
2. Draw it as a colour 0 polygon in its place in the list, Sonic's mask: the plane shows through it, with no seam, at about a quarter of a textured pixel's cost. It needs a kind of face the pack step can name.
3. Stop the wall being drawn at all: the user's obstruction planes (2026-11-03). A CPU saving too. It culls whole polygons, so one partly visible down the pit stays.

## How it goes into level data

`saturn/planes.txt`, read by `tools/planepack.py` from `tools/pack_level.py`:

```
LEVEL7A floor 0 ceiling -7168
LEVEL3A floor 3584 x 35 66 z 4 41
```

`x` and `z` are the stretch the plane is for. Leaving them out takes every face at that height that fits: "VDP2 everywhere it can fit", a backup for a last few drops of speed, to be judged by eye (Lost Valley: 482 faces, 17 clashes, 26 pictures, 206 KB). What the hand changes, `tools/floor_paint.py` saves in `saturn/plane_paint/LEVEL.txt`.

A face goes on the plane if it is flat at that height, faces the right way, is a sector square, is opaque, isn't animated, and is in the stretch. The pack step, before the packer runs:

1. **Takes those faces out of `room.quads`** and drops vertices nothing else uses. Everything downstream (the facing runs, counts, textures used) follows from the shorter lists. About 12 bytes a face.
2. **Writes two files:** `.PLC` (cells, colours, a small header) and `.PLN` (the tile names). No file means no plane, so no level is named in the code.

**The tile map.** A plane is infinite only because it repeats: the map holds 32 x 32 sectors, then starts again. The Cistern fits. The valley's main stretch is 32 wide and 38 long, so 7 of its 462 tiles want the same spot as another, room 31's at the far south against room 30's at the mouth. Those 7 stay on VDP1, drawn over the plane, so the spot shows right in both places. That's one fixed map, as in the Cistern, with no code at run time.

- **What this lacks:** the plane shows a tile of its map wherever VDP1 draws nothing below the horizon. A hole in the rock can show a piece of floor from 32 sectors away, where today it's black. The census has this common: 775 px a view, 630 of them bright, up close (below). **But on the Saturn (2026-11-04) the user can't tell where the ground starts looping**, and the fixed map stays unless a side-by-side look finds something obvious. The census counted views from random places, so it overstated what play shows.

### A map that moves, priced (Lost Valley): parked

Built and running in Mednafen on branch `vdp2-scroll`, parked at the user's word: not needed unless the loop shows.

`tools/occmodel` `planes` mode, 30,000 random views, the camera on the open floor; the floor is 10,310 px a view. "Wrong" counts both a false tile where the level has none and a real tile the map doesn't reach; bright means light and fog leave it above near black. Names costs are Mednafen's measure (`names us` on the frames page: 2 KB in 125-160 us at the vblank), to be replaced by the Saturn's.

| | Fixed map (built) | Slots swapped by block | Names rewritten by sector |
|---|---|---|---:|
| How | One map, repeating | 16 registers point at the blocks round the camera | The map scrolls round the camera like a 2D scroller: the far row or column rewritten at each sector crossed |
| Wrong px a view, of them bright | 775, 630 | 153, 38 | 127, 13 |
| Reach before it repeats | - | 12 to 20 sectors | 16 sectors, where the fog is at its floor along the view |
| Tiles left as polygons | 17 | 0 | 0 |
| Names in VRAM | 64 KB | 96 KB | 64 KB |
| Bank B | 208 KB | 240 KB, item textures across B0 and B1 | 208 KB + its source |
| Source data | none | about 60 bytes | 31.5 KB of low work RAM as built: reading the names from VRAM cost half as much again (362-469 us a crossing) |
| A crossing | - | 8 registers, every 8 sectors | Mean 5.9 sectors (378 bytes), 90% 13, most 32 (2 KB); 35% write nothing |
| Time a crossing, as built, Mednafen | none | a few us | 286-362 us (5 to 7 sectors), most seen 500 us. Writing is cheap (the Saturn: 2 KB in 120 us); the C that finds the tiles is most of it |
| Code | none | about 25 lines | about 80 lines |

If it's ever wanted: names rewritten by sector is the only one that scales past this level, and the far edge moves a sector at a time, not 8 sectors of floor at once. Writes go in the vblank with the list's other numbers, capped a list; after a camera cut the whole map (1,024 sectors) takes a few lists to catch up, unordered as built. Finding tiles wants a bitmap of each row before it runs on a Saturn frame budget.

**VRAM, bank B, 256 KB (from `tools/planepack.py` on the real levels):**

| | Cistern | Valley |
|---|---|---|
| Cells (16-colour, each texture and its turned copy) | 26 KB | 42 KB |
| Depth table, built at load, 8,192 entries | 32 KB | 32 KB |
| Parameters | under 1 KB | under 1 KB |
| Inventory item textures, there today | 66 KB | 59 KB |
| Tile names, blocks of 8 x 8 sectors + one blank | 104 KB | 60 KB |
| **Total** | 229 KB | 194 KB |
| Palettes, of 111, and the light's steps | 107, every step | 93, every 2nd |

The trace on test discs can't stay in bank B: writes there would disturb the plane. On plane levels it moves to bank A's last 32 lines, 32 KB, about 25 frames instead of 120.

## The flooded Cistern

One switch (room 9) flips 36 rooms. What changes, by the level's data:

- **The floor at y 0 is the same floor.** All 209 tiles have a flooded copy with the same texture, turned the same way. The corner lights differ by -0.6 of 31 on average, nearly all within 2, 6 at the very worst. So both sets of faces come out, one set of tiles serves both, and a flood costs the plane nothing: no second names, no code.
- **The ceiling's rooms don't flip.** It's dry in both.
- **The water's surface is polygons, and stays so.** In the flood 293 + 14 quads at y -3072 and -3328, one each way, four frames of animated texture, flagged transparent, so the pack step leaves them alone. Dry, the pool's surface is 55 + 4 of the same at y 0 and -1024.
- **What it lacks:** the floor's caustics (the walls keep theirs), the user's call. Walls under water ripple when seen through the surface and the plane doesn't, so wall feet slide a little over a floor that's still; the plane runs on under them, so no gap can open.

**What the flood costs today** (Mednafen, frames page, no planes, `tools/hwtest.sh`):

| | Rooms | Commands | VDP1 ms | CPU ms | fps |
|---|---|---|---:|---:|---:|
| Dry, room 99 along the hall (`cistern-straight`) | 24 | 769 | 24.4 | 25.7 | 30 |
| Flooded, the same view, above the water (`cistern-flood-above`) | 24 | 936 | 33.0 | 25-30 | 22-23 |
| Flooded, under water in the hall (`cistern-flood`), a few views | 5-7 | 290-344 | 16.7-23.4 | 12-14 | 30 |

So the flood splits in two: under water it's the lightest the hall gets, and from above the water it's the heaviest, because of the surface. With the plane, the wet floor gone from under the surface: above the water VDP1 33.0 -> 24.0 ms, 22-23 -> 30 fps; under water VDP1 10.5 ms (Mednafen).

**Colour.** The game tints a face for water if its room is wet or the camera is under water, with red fading by distance. So a flooded floor is tinted from above the water too, and the dry ceiling over it only with the camera under. Two sets of line colours sit in CRAM, wet and dry, the table's 7-bit index the depth in steps of 256, and a word for each line picks a set: the floor's lines one, the ceiling's another, the horizon between them. A frame rewrites only the words between the last horizon and this one.

## In the game

**Each frame**, when the view is known (`renderWalk`): the rotation matrix, the camera's place on the map, and where each set reads the depth table. Five divisions on the hardware divider, 0.10 ms on the Saturn in the lab. It's frame-level code beside other C, so C it stays. The game's view rows are shrunk for FOV 80 where the lab's weren't, so the matrix unscales them, and the table's far cut becomes entry 105.

**The buffer change**: the plane's numbers must change with VDP1's frame. They're written in the vblank, just after FBCR, for the list that's changing. `system.cpp` gets one hook. Everything else about the change stays as main has it, including the erase and the late change.

**Erase**: the Cistern goes on the vblank erase, as Lost Valley is. The rule "a plane level is an erased level" replaces today's level check. The one black rectangle can't be used on a plane level: it would cover the plane.

**Dark faces**: main draws faces too dark to see as black polygons in their place (c987059), which hide the plane as they hide anything else. Nothing special is built for them.

**Colour offset**: the plane takes offset A. The debug overlay leaves the offset on test discs so it isn't darkened.

**Near the plane**: the table runs out at 262 units of depth, so the height used is never less than about 250. A camera closer than that sees the near floor slightly off. If the camera is on the wrong side, the plane is transparent.

**Pictures, videos, crash screen**: taking the bitmap turns the plane off and gives bank B back to the CPU.

## The inventory

Today, as retail: the scene is drawn once, VDP1's frame is copied into a VDP2 bitmap (NBG0), that picture dims by -64 over 15 vblanks on offset A while the ring draws over it, fading in over 5 on offset B. On close the ring dims and the scene brightens over 30. On a plane level VDP1's frame has holes where the plane was. The holes are colour 0, which NBG0 shows as transparent, as Lost Valley's already do. The options:

| | How | Pause | Memory | Catches |
|---|---|---|---|---|
| **Read VDP2's picture back** | Not possible. VDP2 builds its picture on the way to the TV; nothing can read it | | | |
| **A. VDP2 keeps compositing (the user's pick, built)** | The plane stays on, frozen, under the bitmap. Nothing is copied | as today | none | Three brightnesses and two offsets (below). The item textures are read from bank B under the plane: to see on the Saturn |
| **B. The CPU paints the plane into the picture** | At pause, line by line as VDP2 would, into the holes only. The plane is off till the game resumes | about 0.1 s, up to 0.4 s with a whole frame of plane, once | none | About 80 lines of C doing what VDP2 does, kept matching by hand. Fades and layers exactly as today |
| **C. VDP1 draws the floor for that one frame** | The faces stay in the level, and draw in the capture frame only. The picture is then whole, as today | one frame | about 10 KB of level data, and their textures stay in VDP1's memory | A second set of counts for each room, and vertices no other frame uses. The room code gains its first branch for the plane |

A works in Mednafen first time. B is the fallback if the Saturn disagrees.

**A's three brightnesses:** the plane's fog needs its own constant -112 on top of the picture's fade, the picture its fade, and the ring its own: VDP2 has two offsets. So on plane levels the ring gets neither: offset A the plane (fade - 112), offset B the picture (fade), and the ring's fades go through the shade table (`gShadeGouraud`, 32 entries a frame during a fade, in the same 8 steps). Priorities: picture 4, over the plane's 3, under the frames' 6. The item textures: 66 KB read by the CPU at open, waiting on the hblanks and vblank while RBG0 holds the bank, perhaps 60 ms; to be measured on the Saturn.

## What this path lacks, against today's floors

- **No mips:** far floor may shimmer in motion. You weren't sure on the Saturn. Worth a direct look in the valley, which is brighter.
- **Light per 16 x 16 tile**, not gouraud per pixel. You couldn't tell in the Cistern. The valley's steps are every second level.
- **No caustics on plane floors.**
- **Far fog stops at 14 steps:** past 15,872 units a tile takes no more off, where a polygon goes on down to level 0 (`renderer/roomproject.s`: light and fog together stop at shade 31). A brightly lit far tile is a few steps brighter than its polygon was, a dim one a little darker.
- **Bright colours clip:** 2.4% of the valley's floor colours lose some brightness near the camera (1.8% of the Cistern's ceiling).
- **What's below can show through**, above. **Items in pits aren't hidden by the rim.**
- **Black becomes dim floor in rare spots**, where the game draws no face at all. In the hall, 420 to 477 pixels at about 10 of 255. More in the open: the valley's holes need looking at, and there it can be a false tile.
- **No camera roll.** TR's camera has none.
- **Test discs keep about 8 frames of trace on plane levels**, not 120.
- **With OpenLara's camera (the debug menu's Ours)** the plane reaches about 25,000 units past the far cut, at the fog's floor: the table's cut is retail's camera's. The fog follows the camera.

## Removed from the lab's version

None of it goes to main: the timer fog and its interrupts, the per-line scale table, the `VDP1 draws all` row, the red tint, the page, and copying faces around in low RAM at run time. `lab/plane.cpp` stays on the research branch. What came over: `tools/planepack.py` in a production form, `tools/occmodel`'s `planes` mode, and from `tools/vdp2spy/` the private Mednafen (`spy.py`, `go.py`), the VDP2 register reader and survey, and `planesurvey.py`.

## Clean-up found on main

Filed as OPEN_ISSUES 273-279, "Production code", for later: timer reads, 16 KB of `gVertices`, oversized tables, options read per face, lab paths, C beside assembly, the DMA wait's mask.

## The risks, and how each gets tested

1. **The late buffer change under the erase.** The lab's vanishing came from a late change with slow work before FBCR. Main writes FBCR first, and the lab's cure also held off the vblank's last lines, which main doesn't. Not seen on the Saturn in the first runs of either level. If it vanishes, that margin is the next thing, by libyaul's rule.
2. **NBG0's bitmap with the plane on**, for the inventory and test discs' overlay: the overlay and the first Saturn runs show nothing wrong.
3. **The CPU reading bank B under the plane**, for the item textures: the same.

## The test

Each step in Mednafen first, with the same hwtest spots packed with and without planes (`PLANES=0 tools/hwtest.sh SPOT`, nothing at run time): `cistern`, `cistern-stamp`, `cistern-water`, `cistern-flood`, `cistern-flood-above`, `valley`, `valley-plants`, `valley-trex`, `valley-sky`, `valley-cave`, and the inventory at each level. `valley-end` and `vilcabamba-end` cross out of a plane level and into one, and VDP2's registers came out right both ways.

**Mednafen, without planes and with, one standing view each, 2026-11-04 (ms, 100% = 33.4):**

| Spot | VDP1 | Slave | fps |
|---|---:|---:|---:|
| Cistern room 103 (`cistern-stamp`) | 32.4 -> 23.4 | 32.7 -> 27.7 | 25-27 -> 30 |
| Cistern room 99 along the hall (`cistern-straight`) | 24.4 -> 16.0 | 31.1 -> 21.7 | 30 -> 30 |
| Valley among the plants (`valley-plants`) | 38.3 -> 26.9 | 19.0 -> 15.4 | 20 -> 30 |
| Valley mouth (`valley`) | 28.3 -> 23.1 | 15.4 -> 13.7 | 30 -> 30 |
| Valley T-rex floor (`valley-trex`) | 27.8 -> 15.5 | 14.4 -> 10.4 | 30 -> 30 |
| Valley, the waterfall (`valley-sky`) | 27.1 -> 16.2 | 15.4 -> 12.4 | 30 -> 30 |

**Saturn, 2026-11-04, with planes:** Cistern room 103, about 22 degrees up: VDP1 20.5 ms, master 27.4, slave 24.7, 30.0 fps over 5 seconds (last without planes, about 12 degrees up: VDP1 30.7, master 40, slave 34.7, 25 fps). The inventory was not reported as a problem. Lost Valley: the user reports the whole valley and the T-rex fight sitting on 30 fps far more consistently.

**Found on the Saturn:** looking up, the floor's lines read wrong memory before the floor leaves the frame (flat green bands): a reading of the depth table that started below address 0. Fixed by the table's place at the top of B0; Mednafen showed it as a missing floor. To be read again on the Saturn.
