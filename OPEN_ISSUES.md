# Open issues

Every known place where our build plays, looks or sounds different from retail
Saturn Tomb Raider (T-7910H). Retail is the source of truth. The rendering
section (219 on) is the exception: the new renderer's own open work, measured
against the game rather than retail.

Most of these come from the retail audit of 2026-10-29 (code at a98d9fa).
Its terse summary is [docs/retail-audit.md](docs/retail-audit.md); each issue
here carries its audit ID. The retail addresses quoted (`FUN_` names without
the prefix, e.g. `0600d128`) are named in
[docs/retail-anchors.md](docs/retail-anchors.md). TR1X references are to
`external_resources/TR1X_2.0/src/game/`.

**Each issue has these fields:**
- **Severity**:
  - *gameplay*: changes what the player can do or what happens.
  - *visible* or *audible*: looks or sounds different.
  - *subtle*: numbers differ and it's rarely noticed.
- **Confidence**:
  - *confirmed*: both sides were read.
  - *likely*: the code is certain but the effect was inferred.
  - *suspected*: not verified.
- **Fix**: small / medium / large.

Nothing here has been checked in an emulator yet. Check *likely* and
*suspected* issues in Mednafen before fixing them.

**Keeping it up to date:** numbers are permanent. A new issue takes the next
free number. A fixed issue moves to "Closed" at the bottom, with its commit.

## Contents

| Issues | Area |
|---|---|
| 1-7 | Progression blockers |
| 8-28 | Camera |
| 29-51, 248-250 | Lara: movement, collision, water, controls |
| 52-73 | Weapons and combat |
| 74-112 | Creatures and AI |
| 113-139 | Level logic, traps and objects |
| 140-173 | Inventory, HUD, saves and game flow |
| 174-195 | Audio |
| 196-204 | Numeric primitives and timing |
| 205-218 | Scene look: lighting and effects |
| 219-247, 251-255 | Rendering: the new renderer's open work |
| 273-279 | Production code: clean-up |

The section at the very end lists the issues that need a decision first.

---

## Progression blockers

### 1. Items that start hidden never appear when their trigger fires
*Severity: gameplay (blocks progress) · Confidence: confirmed · Fix: small ·
Audit: LVL-1*

- **Retail:** TestTriggers (`06010350`), object action. Once an item's code bits
  are complete and it isn't already active, a non-creature item is set active
  unconditionally (status byte 0x20) and added to the active list. Items with no
  control routine then drop back to "not active", but they are visible and can
  be touched.
- **Ours:**
  - Items flagged "once" in the level data start invisible
    (`openlara/item.h:1058-1062`, ONCE → `ITEM_FLAG_STATUS_INVISIBLE`).
  - The trigger (`openlara/room.h:1033-1037`) calls `activate()` and sets
    `STATUS_ACTIVE` only when the status is 0, so invisible items stay
    invisible.
  - They aren't drawn (`draw.h:851`) and Lara's collision loop skips them
    (`lara.h:361`).
  - Creatures are unaffected; `Enemy::activate` (`enemy.h:714`) handles them.
- **Effect** (from the level data):
  - **Natla's Mines:** fuse item 42 (switch 79) never appears, so the fuse
    puzzle can't be finished. The pistols (82) and the boat (2) never appear
    either.
  - **Atlantis:** the Uzis (37) never appear.
  - **Tomb of Qualopec:** the ceiling slabs that collapse after the scion is
    taken (24, 25, 26, 68, 38, 39) fall invisibly and do no damage.
  - **St. Francis' Folly:** blocks 19 and 20 never appear (see 3 and 4).
- **Fix:** in the trigger's object action, clear the hidden status of
  non-creature items and set them active, as retail does.

### 2. Great Pyramid: the Scion can't be destroyed, so the exit never opens
*Severity: gameplay (blocks the ending) · Confidence: confirmed · Fix: medium ·
Audit: LVL-2*

- **Retail:**
  - SCION_ITEM3 (object 145) has 5 hit points and is drawn by
    DrawAnimatingItem. Its mesh is 123 vertices, in LEVEL10C.
  - Scion3Control (`06026640`), when its health runs out:
    1. It goes invisible and runs a heavy trigger test at its own position.
    2. It spawns an explosion every 10 frames for 90 frames (3 s), each with
       SFX 104 and camera bounce -200.
    3. It removes itself.
  - The heavy trigger under it (room 50, sector 60) opens DOOR_2 116, the only
    way out. It also starts the earthquake (108), CD track 16 and the lightning
    (107).
- **Ours:** `openlara/item.h:1642` maps SCION_TARGET to `ViewTarget`. Its draw is
  empty (`object.h:227`) and it can't be targeted. The explosion effect (object
  151) isn't implemented; its `INIT_ITEM` is commented out in `item.h`. Both
  come from upstream OpenLara.
- **Effect:** the final Scion is absent and the game can't be finished.
- **Fix:**
  - A Scion3 class: 5 hit points, targetable, with the death sequence above.
  - The explosion sprite effect, which 84-87 need too.

### 3. Thor's hammer tests its heavy trigger in the wrong sector
*Severity: gameplay (blocks St. Francis' Folly) · Confidence: confirmed · Fix:
small · Audit: LVL-3*

- **Retail:** Thor's hammer control (`06022c3c`, case 3; TR1X
  `objects/thors_hammer.c`). When the hammer reaches its "done" state, the heavy
  trigger test runs at the **handle's** own position. Only then does
  AlterFloorHeight (`060249a0`) raise the floor by 2048 at the head's position.
- **Ours:** `openlara/object.h:1824-1826` tests at the head's position.
  - That sector (room 25, sector 48) holds only a pad trigger.
  - So the heavy trigger in room 25, sector 51, which activates blocks 19 and
    20, never fires.
- **Effect:** the Folly's Thor room can't be completed. This, issue 1 and issue
  4 each block it on their own.
- **Fix:** run the trigger test at the handle's position.

### 4. Pushed and falling blocks never fall, and land with no thud
*Severity: gameplay (blocks St. Francis' Folly) · Confidence: confirmed · Fix:
small · Audit: LVL-4*

- **Retail:** MovableBlockControl (`0602409c`) turns gravity on when the block is
  above the floor (`pos.y < height`). On landing it calls DinoStomp (a camera
  bump) and plays SFX 70.
- **Ours:**
  - `openlara/object.h:1219` tests `pos.y > roomFloor`, which is inverted.
    Upstream has the same bug.
  - Line 1228 is a TODO for the landing ("EarthQuake + playSound 70").
- **Effect:**
  - Blocks pushed off a ledge hover in mid-air.
  - The Folly's blocks 19 and 20 start 4096 and 6144 units above the floor they
    must reach.
  - No block anywhere lands with a thud.
- **Fix:** invert the test, and add the stomp and SFX 70.

### 5. Pierre drops only the scion; Tihocan's second key is missing
*Severity: gameplay (likely blocks Tomb of Tihocan) · Confidence: likely · Fix:
small · Audit: AI-3*

- **Retail:** PierreControl (`06028ef0`), on death, calls SpawnItem
  (`0601aca0`) three times: the magnums (0x56), scion piece 2 (0x90) and Key 1
  (0x81). TR1X `ai/pierre.c` matches.
- **Ours:** `openlara/enemy.h:2445` drops only `ITEM_SCION_PICKUP_DROP`.
- **Level data (LEVEL7B):**
  - Two Key 1 keyholes (items 48 and 87), but only one Key 1 placed (item 11).
  - Pierre (item 82) stands in room 110, next to keyhole 87 in room 111, so the
    second key should be his.
- **Effect:** the second gold keyhole probably can never be opened, and Lara
  never gets his magnums.
- **Fix:** drop all three items.

### 6. Atlantis doppelganger: wrong mirror point, no floor triggers, no death
*Severity: gameplay (likely blocks Atlantis) · Confidence: confirmed for the
code, likely for the softlock · Fix: small to medium · Audit: AI-4*

- **Retail:** ControlEvilLara (`06020e84`; TR1X `ai/evil_lara.c`).
  - She mirrors Lara about the fixed point (36864, 61440), the centre of room
    10, the arena: x = 0x12000 - x, z = 0x1E000 - z.
  - While falling and on landing she runs TestTriggers on her floor, heavy
    triggers included.
  - Then she plays the death state (8).
- **Ours:**
  - `openlara/enemy.h:913-917` mirrors about the centre of her start room:
    room 57, centre (29184, 59904), so she lands about 15 sectors off.
  - The fall code (`enemy.h:897-910`) never runs the floor triggers and leaves
    her frozen in the fall pose.
- **Level data:** the only trigger that activates door 31 is the heavy trigger
  at room 10, sector (9, 3). Trapdoor 32 also has a switch.
- **Effect:** the double is in the wrong place, and door 31 probably never
  opens, which likely blocks Atlantis. How she is drawn is issue 212.
- **Fix:**
  - Use retail's constants.
  - Run the floor triggers (heavy) on her fall and landing.
  - Set the death goal state.

### 7. Trapdoors support the wrong second sector (mirrored)
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: LVL-5*

- **Retail:** OnTrapDoor (`0602fadc`; TR1X `objects/trapdoor.c`). A closed
  trapdoor covers its own sector plus one neighbour:

  | Yaw | Neighbour |
  |---|---|
  | 0 | z+1 |
  | 180° | z-1 |
  | 90° | x+1 |
  | -90° | x-1 |

- **Ours:** `openlara/item.h:801-805` takes dx/dz as trapdoor minus query, so it
  covers the opposite neighbour in every case.
- **Effect:** Lara falls through the far half of a closed trapdoor, and can walk
  on air on the other side. Affected items:
  - City of Vilcabamba: 27, 61, 62
  - City of Khamoon: 34, 35, 40, 41, 54, 56, 57, 59, 80
  - Atlantis: 119, 120
  - Great Pyramid: 13, 14, 114, 115
- **Fix:** flip the signs.

---

## Camera

### 16. Trigger look-ats are narrower, slower, and don't decay
*Severity: visible · Confidence: confirmed · Fix: small · Audit: CAM-10*

- **Retail:**
  - CalculateCamera's look-at accepts targets within ±50° of yaw and ±85° of
    tilt, and turns the head 4° per tick (HEAD_TURN; constants 0x238B, 0x3C6D,
    0x2D8).
  - LaraAboveWater (`0601af90`) still decays the head by 1/8 every tick, so the
    tracking settles around 32°.
- **Ours:**
  - `openlara/camera.h:461-480` uses ±44° yaw, -42..+22° pitch, 2° per tick.
  - `lara.h:2913` skips the decay while `lastItem` is set.
- **Effect:**
  - Targets above Lara are never looked at.
  - The turn is half speed and goes all the way to the target.
  - Retail data has six of these: the Caves view target, the Vilcabamba block,
    St Francis' magnums, the Cistern door, the Mines cabin and the Atlantis
    doppelganger.
- **Fix:** retail's limits, speed and decay.

### 17. The camera shake model is different
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: CAM-11,
CAM-23, LVL-19*

- **Retail:** MoveCamera (`0600c054`) handles bounce.
  - A positive bounce is a one-tick jolt of pos.y and target.y, which then
    eases back.
  - A negative bounce adds random ±|b|/2 jitter (control RNG) to pos x/y/z and
    to the target, decaying by 5 per tick, so the view direction shakes too.
  - The bounce is applied before GetFloor, so the camera room is re-resolved.
- **Retail sources:**
  - EarthQuake flip effect: -250 (`06015ac0`)
  - Explosion: -75 (`06015d44`)
  - Earthquake object: -150 or +50 (`06026800`)
  - Torso landing: +500 (`06024b34`)
  - Scion3: -200 every 10 ticks (`06026640`)
  - DinoStomp anim effect (`060159b8`): a one-frame bounce of up to +100 within
    16 sectors. Used by T-rex steps, falling ceilings landing, Thor's hammer,
    the Mines cabin and the Torso.
- **Ours:**
  - `openlara/camera.h:593-598` is a vertical-only sine bob,
    `sin(7πt)·t·48`, with no RNG.
  - Callers set 5–30 ticks (`level.h:304-413`, `object.h:2167` and `2172`,
    `enemy.h:3352`).
  - `FX_FLOOR_SHAKE`/DinoStomp (`level.h:326-350`) shakes for up to 15 frames
    within 15 sectors, using its own `>> 4` formula.
  - The bob is applied after `view.room` is chosen (`camera.h:593-600`), so
    wrong-room and water edge cases are possible.
  - The Scion3 shake is missing entirely (issue 2).
- **Effect:** earthquakes, the T-rex, explosions, the Torso boss, the Atlantis
  and Great Pyramid scions.
- **Fix:** port bounce into `move()`, with the retail strengths at each source.

### 18. Lava wedge death: no camera swing and no flames
*Severity: visible · Confidence: confirmed · Fix: small (camera) · Audit:
CAM-12*

- **Retail:** the lava wedge control (`06030bc8`; TR1X `traps/lava.c`). On
  touching Lara it:
  - sets camera.item = the wedge, flags CHASE_OBJECT, type FIXED, angle -180°,
    distance 3072;
  - then calls LavaBurn (`060308b0`).
- **Ours:** `openlara/object.h:1703-1741` (TrapLava) just applies full damage.
  CHASE_OBJECT isn't supported.
- **Effect:** death by the lava wedge in Natla's Mines.
- **Fix:** the camera set-up; the flames come with issue 120.

### 19. Cutscenes ignore their per-frame field of view
*Severity: visible · Confidence: confirmed · Fix: small, once issue 8 has a
lever · Audit: CAM-13*

- **Retail:** CalculateCinematicCamera (`0600d850`) and InGameCinematicCamera
  (`0600daa4`) call phd_AlterFOV(frame.fov) every frame, and apply the roll
  through phd_LookAt (`0603c97c`).
- **Ours:** `openlara/camera.h:43-77` and `93-130` never read fov.
- **Effect:** the retail CIN data's FOV ranges are:

  | Cutscene | FOV |
  |---|---|
  | CUT1 | 35–80° |
  | CUT2 | 47–55° |
  | CUT3 | 47.5° |
  | CUT4 | 47.5–70° |
  | Midas (LEVEL6) | 47.5° |
  | Scion pickups | 80.3° |

  Our cutscenes are framed much wider and lose their zooms.
- **Fix:** apply fov (and roll) per frame.

### 20. In-game cinematic timing and hand-back
*Severity: subtle to visible · Confidence: likely (animation lengths not
checked) · Fix: small · Audit: CAM-14*

- **Retail:**
  - InGameCinematicCamera pre-increments the frame, so frame 0 is never shown,
    and clamps on the last frame.
  - The cinematic ends only through the animation's FX_LARA_NORMAL. LaraNormal
    (`06015a7c`) sets CHASE and restores FOV 80.
  - It also updates camera.target, which is the sound listener.
- **Ours:**
  - `openlara/camera.h:106` uses `frames[timer++]`.
  - `camera.h:95-101` ends when the frames run out.
  - `item.h:396` FX_LARA_NORMAL doesn't touch the camera, and the target isn't
    updated.
- **Effect:** we run one tick behind. The hand-back happens at the wrong time
  when the CIN length differs from the animation length; Sanctuary's CIN is only
  66 frames.
- **Fix:** match retail's frame order and end condition.

### 21. CUT3 doesn't flip the map
*Severity: visible · Confidence: likely · Fix: small · Audit: CAM-15*

- **Retail:** the cutscene start routine (`0600d574`), case 0x12, calls FlipMap
  (`060112a8`).
- **Ours:** `openlara/camera.h:32` has `//level.flip()` commented out.
- **Effect:** CUT3 shows the unflipped rooms. What the flip changes on screen was
  not checked.
- **Fix:** restore the call.

### 22. Loading a save restores the old camera instead of re-initialising it
*Severity: visible · Confidence: confirmed · Fix: small · Audit: CAM-16*

- **Retail:** GameLoop (`0601636c`) calls InitialiseCamera (`0600bfe0`) for
  every session. The camera starts 1024 above Lara and 100 behind her, then
  pulls out at 1/12. Saves contain no camera position.
- **Ours:** `openlara/lara.h:4101-4106` and `4133-4140` save and restore
  `view.pos`, the room and `lastIndex`.
- **Effect:** on every load, retail's pull-back from above Lara's head is
  missing.
- **Fix:** initialise the camera on load; stop saving it.

### 24. The "already looked at" mark reuses ITEM_FLAG_ANIMATED
*Severity: subtle (latent) · Confidence: confirmed (code) · Fix: small · Audit:
CAM-19*

- **Retail:** a separate looked_at bit.
- **Ours:**
  - `openlara/camera.h:484` sets `ITEM_FLAG_ANIMATED`, and `room.h:895` tests
    it.
  - That same flag selects the violent mood (`enemy.h:195`), and the Bear's
    constructor sets it (`enemy.h:1170`).
- **Effect:** none in TR1's data today. A look-at on an enemy would make it
  violent, and a bear would count as already looked at.
- **Fix:** separate the two before fixing issue 75.

### 25. The inventory's background scene is drawn without the camera shift
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: CAM-20*

- **Retail:** the scene behind the inventory is the last game frame.
- **Ours:** `saturn/render.cpp:2589` calls `matrixSetView(view.pos)` without the
  shift or shake.
- **Effect:** opening the inventory with the camera pushed against a floor or
  ceiling shows a slightly different view.

### 27. LOS edge cases
*Severity: subtle · Confidence: confirmed · Fix: part of issue 11 · Audit:
CAM-22*

- **Retail** (LOS `06010ca8`):
  - xLOS and zLOS return -1 or 0.
  - LOS still runs ClipTarget when the second pass returns -1, and re-gets the
    target room.
- **Ours:** `openlara/room.h:808-838` bails out early, and the fallback
  directions use the non-sloped trace.

### 28. RefreshCamera returns at the first non-matching camera command
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: CAM-24*

- **Retail:** RefreshCamera (`06010270`) keeps looping through the trigger's
  commands after a TO_CAMERA that doesn't match.
- **Ours:** `openlara/room.h:854-866` returns at once.
- **Effect:** only on triggers with several camera commands.

---

### 261. A player's choice of camera
*Kind: feature · Size: small · Fix: an in-game option*

- The user's idea (2026-11-02): retail's camera is now the game's, and it would be fun to let the
  player switch to OpenLara's in the game's options. Both are in `openlara/camera.h`; `gCameraRetail`
  and the HW_LOG menu's Camera row switch them today.
- OpenLara's follow camera has the shape of TR3 and TR4's, not TR1's (found 2026-11-03). Their PS1
  chase cameras open with `if (elevation == 0) elevation = -1820` (-10 degrees): TR3 `TOMB3.EXE`
  `00023e4c`, TR4 `SLUS_008.85` `00026740`, as `updateFollow` does. TR1 on the Saturn and PS1 and TR2
  on PS1 have no such line; on the TR1 Saturn the chase sits level with Lara's shoulders. After it
  come five tries at a place and a 768 squared test, as `getBestLocation` has; TR1 follows the
  pathfinding boxes instead. TRX has the same split as its Camera mode setting (`box_camera.c` TR1
  and TR2, `los_camera.c` TR3 and TR4), so the option could carry those names.
- What separates them: 10 degrees of tilt and the field of view. Heaviest tenth of views over all
  levels (`tools/occmodel` `cameras`): FOV 80 adds 8% room vertices and 25% faces kept, the level
  view 2% and 1%. Room 103 is the exception with rooms overhead, where the tilt counts about as much.

### 262. The near plane is 64 where retail's is 80
*Kind: visible · Size: small · Fix: decide with the near-face work*

- Retail's phd_InitWindow (`0603cf1c`) sets near 80; ours is `VIEW_MIN_F` 64. Left at 64 when retail's
  camera became the default (issue 8): moving it changes which near faces are cut, near the work
  of issues 251-252.

### 263. Near the camera retail splits faces in 3D, two levels deep; ours warps and leaves holes
*Kind: visible � Size: medium � Fix: port from retail (`0603fce0`..`06040ec0`)*

- **Retail:**
  - A room quad goes to the split path (`06040038`) when any corner is near the camera or over
    128 px off the frame, or when its farthest corner is within 4096 units. Triangles go the same
    way as quads with C = D.
  - It takes each edge's midpoint and the centre in view space, projects them (`06040d58`) and
    draws 4 quadrants, each 32x32 of the full texture, from a cached 2 KB slot.
  - A quadrant with a corner near or over 128 px off, or that came out facing away, is split the
    same way into 4 tiles of 16x16 texels (`0604011c`).
  - Near is 80. A corner between 0 and 80 is projected held at 80 and still draws; only a tile with
    a corner behind the camera is dropped. Coordinates are clamped to 128 px off the frame.
- **Ours:** a quad is drawn whole unless a corner is within 64, so the texture warps across all
  1024 units of it. Then one split; a quarter with a corner within 64 is left out, a triangle
  filled. A quad 256 px off the frame is cut on the screen, which doesn't change the warp.
- Retail while walking in the Cistern's room 31: 54-92 quadrants, 0-3 tiles, 214-282 commands a
  frame, 11-13 of 69 slots in use (Mednafen).
- **Draw order:** each piece keeps its own depth key, its farthest corner (a mesh face its mean).
  The slave sorts by the whole key, a radix sort of two passes (`06008fdc` by high byte, `06008a7a`
  by low byte), one unit of depth wide; ours is 16 wide. The pieces are what shows: at the
  `cistern-warp` spot a wall drew over the floor, a whole quad sorted by its far corner.
- **Built (the game's only near path since 2026-11-03: the old one and its menu row are gone, the lab
  keeps its copy for its captures; figures below are from before):** in `facesroom.s`, with quads a plane cuts
  or with a corner past the frame's coordinates in C. Mednafen, Lara standing, against Ours: ROOMS +0.2 ms
  at room 31, +1.2 at the ledge, +1.5 at room 103; VDP1 +0.6 to +0.8 ms at those, +1.9 at `cistern-mirror`,
  +3.5 at `cistern-steps`.
- **Not done:** the pieces' turned lines (offline, Mednafen: 30.86 to 26.67 ms at `cistern-steps`, 30.28 to
  27.36 at `cistern-mirror`, below Ours at the four spots tried; 16-30 more 2 KB slots). Meshes' near plane 64.
  A quad over 128 px off the frame with a corner past 4096 stays whole. The lab doesn't cover Retail mode.
- Takes in issues 252 and 262.

### 264. Room vertices could be freed room by room
*Kind: memory, speed � Size: maybe tens of KB of high work RAM � Fix: to be costed*

- `gRoomRotated` (12 bytes each, what the DSP rotates, 43.8 KB) and `gVertices` (8 bytes each, what the
  faces read, 44 KB with the meshes') hold every listed room's vertices for the frame, and a view
  past 3,328 loses its deepest rooms (issue 254).
- Retail has one buffer, 48,000 bytes at `06094564` (1,500 of 32 bytes): a room is transformed, drawn,
  and the next overwrites it, so a view has no limit (`06037ae8`).
- The idea: a ring, a room's vertices freed once its faces are made. It would also end issue 254's
  limit, and what it frees goes to the rooms' faces (issue 244).
- **What it lacks that today's has:** the DSP rotating the frame's rooms ahead of both CPUs. It could
  run only as far ahead as the ring is long, which may cost back what the frame gains by it.
  Not costed.

### 265. The backup library sits in high work RAM, 24 KB used only to save or load
*Kind: memory � Size: 24 KB of high work RAM � Fix: small, one thing to test*

- `saturn/backup.cpp`: `gLibrary` 16 KB, where the BIOS copies its backup code and runs it from, and
  `gWork` 8 KB, its scratch. Neither is touched in play. Moved out, they would take a test disc's
  room faces at LEVEL7A from 21.8 to about 46 KB and a shipping build's from 41.4 to about 65 KB (issue 244).
- Low work RAM can hold them at most levels (free: LEVEL7A 51.7 KB, LEVEL6 69, LEVEL10B 124), not at
  LEVEL7B (1.4 KB), so they would go low when the level leaves room and stay high when it doesn't.
- **To test:** a level load overwrites low work RAM, so the library would be set up again each time
  (`BUP_INIT`, today once, `gStub` keeps it). Calling it twice is untested, in Mednafen and on the Saturn.

### 266. The saved game's buffer is bigger than any level's save
*Kind: memory � Size: about 3 KB of high work RAM � Fix: small*

- `gSaveFile` is 8 KB (`SAVEGAME_SIZE`, from OpenLara's GBA build). The Cistern's save is 2,750 bytes
  for its 109 items; the largest level, LEVEL10B, has 192, about 5 KB by scale.
- The exact worst case isn't worked out: the item data's size goes by each item's kind (`ItemSave`, plus
  `EnemySave` or `LaraSave`), over `level.itemsCount`. Size it from the levels, checked by the packer.

### 268. A mesh's face near the camera can walk thousands of lines off the frame
*Kind: speed (VDP1) � Size: 9.97 ms of one list, Mednafen, one face that drew nothing � Fix: to be costed*

- `tools/hwtest.sh valley-trex`, frame 1960: static 26, a tree: corners (319,-404) (998,-1024) (896,1023)
  (315,301), 2,048 lines, 103,395 pixels walked, 0 written. Coordinates are clamped at +-1024.
  The next dearest in that list is 2.04 ms. Room faces got retail's near pieces (issue 263); meshes didn't.
- Only when a near mesh face reaches far off the frame, so this is a spike and not the level's steady cost.

### 270. Plants walk far more pixels than they write
*Kind: speed (VDP1) � Size: among the plants, statics 12.7 of 42.7 ms for 17% of the frame's pixels � Fix: assets*

- `tools/stepwise.py`, `valley-plants`, Mednafen: statics walk 68,897 pixels to write 26,727, 0.47 ms a 1000 px
  written against the rooms' 0.24. VDP1 walks see-through texels as it does the rest. Static 21's 40x56 face walks
  5,356 for 1,884.
- For the asset study: crop see-through borders from plant textures and their quads, fewer overlapping leaves.

## Lara: movement, collision, water, controls

### 29. The button layout and control model differ from retail
*Severity: gameplay · Confidence: confirmed · Fix: medium · Audit: LARA-1,
INV-33*

- **Retail:**
  - S_UpdateInput (`060374b4`) maps the pad through a control-method table at
    `0x060637fc`. Default "Control Method 1":

    | Button | Action |
    |---|---|
    | A | Jump |
    | B | Action |
    | C | Draw |
    | X | Step Left |
    | Y | Look |
    | Z | Step Right |
    | L | Walk |
    | R | Roll |
    | START | Inventory |

  - Methods 2 and 3 can be picked in the options (issue 145). In those, L and R
    are always the sidesteps.
  - Sidestep and roll have their own input bits: 0x400 step left, 0x800 step
    right, 0x1000 roll.
  - Walk+Left/Right from standing is a slow turn.
  - Lara can turn while sidestepping, on land and at the surface.
  - Roll is read only by LaraAsStop and LaraAsRun. The step and surface handlers
    are `0601b9e0`, `0601ba68`, `06021414`, `06021494` and `06021514`.
- **Ours:**
  - `openlara/lara.h:2765-2772` maps A Action, B Jump, C Weapon, X Walk, Y = UP
    and DOWN together (roll), Z Look. L and R do nothing.
  - Sidestep is Walk+direction (`lara.h:664-676`, `944-964`, `1065-1069`), with
    no turning while stepping.
  - Because roll sets UP and DOWN, outside Stop and Run it acts as forward: a
    pull-up while hanging, a forward jump from the crouch, surface swim forward,
    nose-down underwater, keep walking.
- **Effect:** every button press differs from retail. Walk+Left gives a sidestep
  where retail gives a slow turn.
- **Fix:**
  - Add the roll, step-left and step-right input bits.
  - Map retail method 1 (optionally all three methods).
  - Port the retail state checks that use them.

### 30. Look works in every state and freezes Lara's movement
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: LARA-2,
CAM-6*

- **Retail:**
  - The look bit is read only in LaraAsStop (`0601b2e4`) and LaraAsSurfTread
    (`06021514`); there are no other reads in the decomp.
  - In any other state Look is ignored and steering keeps working.
  - LaraAboveWater (`0601af90`) has no look code.
- **Ours:**
  - `updateLook` (`openlara/lara.h:2852-2918`, called at `3940`) runs before the
    state handlers in every state and water state.
  - It turns the head and torso, then strips the directions:
    `input &= ~(IN_RIGHT|IN_LEFT|IN_UP|IN_DOWN)`.
  - The turn states also drop to Stop on Look (`lara.h:744`). That is TR1X's
    optional "enhanced look", not the original.
- **Effect:**
  - Holding Look while running stops Lara, because forward is removed.
  - It freezes steering in jumps and swims, and shimmying while hanging.
  - Retail keeps running and only looks once she stands still.
- **Fix:**
  - Do the head look only inside the Stop and Surface-tread handlers, with
    retail's limits.
  - Decay the head after the state handler, only when not looking.

### 31. Looking while treading water uses the land settings
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LARA-3*

- **Retail:** LaraAsSurfTread (`06021514`) and LaraSurface (`0602111c`).
  - The head turns 3° per tick (546), with yaw ±50° (it stops at about 9282)
    and pitch ±42°.
  - Torso yaw = head/2, torso pitch 0. The Saturn sets torso_y to head_y/2,
    where PC has a head_x/2 bug.
  - When not looking, the return to centre snaps once under 546.
- **Ours:** the shared `updateLook` (`openlara/lara.h:2886-2917`) uses 2° per
  tick, yaw ±44° and pitch -42..+22° (`common.h:2081-2084`), with torso equal to
  head.
- **Effect:** looking around while treading water.

### 32. Underwater turning is slower and ramps up
*Severity: gameplay (feel) · Confidence: confirmed · Fix: small · Audit:
LARA-4*

- **Retail:**
  - y_rot changes by ±1092 (6°) every frame, instantly, in Swim, Glide and
    Tread (`06021c9c`, `06021d58`, `06021e20`).
  - turn_rate isn't used underwater (LaraUnderWater `06021ab8`).
- **Ours:**
  - `s_turnUW` (`openlara/lara.h:585-600`) builds turnSpeed by 409 per frame up
    to 1092.
  - `updateUnder` (`lara.h:3159-3160`) takes 364 off before applying it. So the
    most she turns is 4° per frame, after a ramp of about 16 frames.
  - turnSpeed left over from land also spins her for a few frames after she
    enters the water.
- **Fix:** add ±1092 to angle.y directly, and don't apply turnSpeed underwater.

### 33. Underwater pitch is limited to ±85° instead of ±100°
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LARA-5*

- **Retail:** x_rot is clamped to ±18200 (100°) (`06021ab8`). The swim collision
  (`0602201c`) flips its facing when the pitch passes ±90°.
- **Ours:** `openlara/lara.h:3162` clamps to ±85°, and `c_swim` (`lara.h:2057`)
  always faces forward.

### 35. Surface swimming over shallow water, and climbing out, is looser than retail
*Severity: gameplay · Confidence: confirmed · Fix: small to medium · Audit:
LARA-7*

- **Retail** (LaraSurface `0602111c`, LaraSurfaceCollision `060217cc`,
  LaraTestWaterClimbOut `060218e8`):
  - Surface collision is a 700-tall box: probe at y+700, height 700, step-up
    limit -100, ceiling 100.
  - Lara is stopped when the floor under her is less than 700 below her, or on a
    front, ceiling or clamp hit.
  - Side hits turn her ±5°.
  - Climb-out works only onto a ledge between 511 above and 100 below the
    waterline (`front_floor + 0x4BB <= 0x263`).
- **Ours:**
  - `c_surf` uses an 800-tall box (`openlara/lara.h:2069`).
  - Surface swim forward allows a 384 step-up (`lara.h:2540`); the other surface
    states allow 128 (`lara.h:3085`).
  - The mid-floor stop applies only on steep slopes (`lara.h:1438`).
  - Climb-out allows a ledge up to 316 below the surface (`lara.h:1713`), and
    needs 100 more headroom.
  - Ceilings less than 100 above the water block surface swimming.
- **Effect:** in the shallow spots from issue 34, Lara swims over submerged
  ledges with her legs through the floor. She can climb onto ledges 100–316
  underwater, which retail refuses.
- **Fix:**
  - Collide at y+700 with height 700 and step-up -100.
  - Stop when the middle floor is below 0.
  - Climb-out range [-512, 100].

### 36. Climb-out isn't tested while treading water
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: LARA-8*

- **Retail:** LaraSurfaceCollision tests climb-out in every surface state, gated
  by y_rot == move_angle. That is true in both Surface-swim and Surface-tread.
- **Ours:** only the Surface-swim collision calls `c_checkWaterOut`
  (`openlara/lara.h:2544`).

### 37. Water currents don't pull Lara at the surface
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: LARA-9*

- **Retail:**
  - LaraSurface (`0602111c`) calls LaraWaterCurrent (`06022150`) just as
    underwater does.
  - After the current moves her, it runs a full collision with wall deflection
    and resets the "old" position.
  - The sink trigger sets the current regardless of water state.
- **Ours:**
  - `openlara/lara.h:3958-3960` ignores the sink unless she's underwater.
  - `updateFlow` (`lara.h:3109-3139`) does no collision after moving her.

### 38. Lava doesn't stop a walking Lara at its edge
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: LARA-10*

- **Retail:**
  - The walk collision (ColWalk `0601c1cc`, flags | 0xE0) sets
    lava_is_pit.
  - GetCollisionInfo (`0600dc00`; trigger type 5 → 0x200) then treats a lower
    lava floor ahead as a 512 drop. A walking Lara stops at the edge like a
    ledge.
- **Ours:** the lava case is commented out as a TODO in
  `CollisionInfo::setSide` (`openlara/common.cpp:1509-1511`), so `stopOnLava`
  does nothing.
- **Effect:** walking carefully beside lava walks her straight in. Natla's Mines
  has 225 lava sectors, Atlantis 386 and the Great Pyramid 200.
- **Fix:** restore the lava-is-pit case.

### 39. Lava floors kill Lara when she isn't standing on the lava
*Severity: gameplay · Confidence: likely (TR1X `lava.c`, Saturn trigger code
`06010350`) · Fix: small · Audit: LARA-11*

- **Retail:**
  - The lava floor burns Lara only when she is on it, `pos.y == floor`, not when
    heavy.
  - LavaBurn (`060308b0`) re-reads the floor height at y = 32000.
  - So standing on a bridge or block over lava, or hanging from a ledge, is
    safe.
- **Ours:** `openlara/room.h:907-913` kills her whenever she isn't in the air, or
  is swimming. It plays SND_LAVA instead of setting her alight.
- **Effect:** hanging over a lava sector, or swimming above a lava floor, kills
  Lara.
- **Fix:** retail's condition. The flames are issue 120.

### 40. Walk+Up while hanging does a handstand retail Saturn never does
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LARA-12*

- **Retail:** LaraColHang (`0601ca00`) always sets the plain pull-up (state
  0x13). The handstand state exists, but the Saturn code never selects it
  (checked in the disassembly).
- **Ours:** `openlara/lara.h:2352-2356` selects the handstand when Walk is held.

### 41. The pull-up height window is too wide
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: LARA-13*

- **Retail:** the ledge must be between 650 and 850 above Lara (`0601ca00`).
- **Ours:** anything closer than 850 either way (`openlara/lara.h:2346`).
- **Effect:** after shimmying onto a ledge more than 256 lower, ours still allows
  a pull-up from the wrong height.

### 42. Slide direction bugs
*Severity: gameplay · Confidence: confirmed (a, b), likely (c) · Fix: small ·
Audit: LARA-14, MATH-7*

- **Retail:** TestLaraSlide (`0601e404`; TR1X `lara.c`) and LaraSlideSlope
  (`0601d658`).
  - The angle difference is computed in 16-bit wrapped arithmetic.
  - The z tilt wins when |z| > |x| > 2.
  - The slide animation restarts only when the slope angle changes.
- **Ours,** `c_checkSlide` (`openlara/lara.h:1450-1487`):
  - **(a)** `abs(realAngle - angle.y)` isn't wrapped (`lara.h:1472`). On slopes
    descending toward -z (realAngle = -32768), any heading between 90° and 180°
    picks the back-slide and turns Lara around, where retail slides her forward.
    This affects 8–152 slope entries per level.
  - **(b)** When |z| > |x| ≥ 3, ours slides along x. That's 56 slopes in Lost
    Valley, 25 in Qualopec, 18 in Sanctuary, and 1–6 in the Colosseum,
    Atlantis, Tihocan and the Pyramid.
  - **(c)** Hitting a wall mid-slide sets goal Stop and doesn't restart the
    slide. Slide anim 70 has a Stop transition to 71, so Lara briefly stands up;
    retail restarts the slide. Ours also re-snaps her facing every frame.
- **Fix:** int16 wrap, retail's tilt precedence, and retail's restart rule.

### 43. Walking backwards off a step plays the wrong step-down animation
*Severity: subtle / visible · Confidence: confirmed · Fix: small · Audit:
LARA-15*

- **Retail:** ColBack (`0601cc38`) tests frames 964–993. The walk-back anims 40,
  41, 61 and 62 never use those frames (538–613, 899–960), so retail always
  plays anim 61.
- **Ours:** `openlara/lara.h:2428-2432` plays anim 62 for frames 568 and up.
- **Fix:** always 61.

### 44. A swan-dive landing takes fall damage
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: LARA-16*

- **Retail:** LaraColSwanDive (`0601d418`) just lands (goal Stop), with no
  damage and no trigger test.
- **Ours:** the swan-dive collision goes through `c_fall`, then
  `c_checkLanding` (`openlara/lara.h:1505-1531`), which applies normal fall
  damage.

### 45. The grab height from a reaching jump uses the wrong bounding box
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: LARA-17*

- **Retail:** LaraTestHangJump (`0601dfe8`) measures the grab with the reach
  animation's bounds, before the anim change. The up-jump grab
  (`0601e270`) does use the new hang anim.
- **Ours:** `c_checkHang` (`openlara/lara.h:1669`) uses the new anim's box for
  both.
- **Effect:** a one-frame height pop.

### 46. Vaulting from standing depends on a hack
*Severity: gameplay (minor) · Confidence: likely · Fix: small to medium ·
Audit: LARA-18*

- **Retail:**
  - LaraColStop (`0601c4fc`) has no vault test.
  - From standing, Up goes to Run and Walk+Up goes to Walk.
  - The walk and run collisions vault (`0601c1cc`, `0601c374`, TestLaraVault
    `0601ddd0`).
- **Ours:**
  - Stop's collision vaults only when the input is exactly Up+Action, and pushes
    her 4 units forward (`openlara/lara.h:2242-2252`).
  - The forward-clearance check (`s_checkFront`) in the Stop handler blocks
    starting to walk or run toward the block.
- **Effect:** from standing, Walk+Up+Action against a block does nothing, and so
  does Up+Action while turning.
- **Fix:** drop the clearance check and the nudge, and vault from walk/run as
  retail does.

### 47. Small movement differences
*Severity: subtle · Confidence: confirmed · Fix: small each · Audit: LARA-19*

- Stop's collision has no ceiling-hit check (`openlara/lara.h:2235`); retail
  ColStop (`0601c4fc`) has one.
- Walking into a wall leaves the state as Walk during stop anims 2 and 3, so Lara
  can still turn (`lara.h:2143`, `animSet(..., false)`). Retail sets Stop.
- Brushing a wall sideways cuts Lara's lean by 2° (`lara.h:1374`, `1378`);
  retail doesn't.
- The underwater floor-ceiling clamp restores the old position
  (`lara.h:1404-1407`); retail only stops her.
- With guns drawn and Walk held, retail still fast-turns; ours doesn't
  (`lara.h:752`).
- Air refills by 10 per frame on land (`lara.h:3954`); retail sets it full
  instantly. Not visible.

### 48. Enemy and door pushes are buggy
*Severity: gameplay / visible · Confidence: confirmed · Fix: small · Audit:
LARA-20*

- **Retail:**
  - ItemPushLara (`0600edf8`), after pushing Lara, re-collides at Lara's own
    position.
  - Creatures push with bigpush = 0, to the edge of their bounding box
    (CreatureCollision `0600ebbc`).
  - Objects and doors use bigpush = 1 (`0600ec4c`, `0600ecc0`).
- **Ours** (`openlara/item.h:1231-1300`):
  - **(a)** `collideRoom` (`item.h:1290`) runs on the pushing item, not on Lara.
    This is an upstream OpenLara bug. Pushes into walls aren't undone, and valid
    pushes can be reverted.
  - **(b)** The box always grows by Lara's radius (`item.h:1243-1246`), so
    creatures hold her 100 units further away than retail.
- **Fix:** `lara->collideRoom`, and a bigpush flag.

### 49. Pressing Jump+Draw while dead revives Lara at full health
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: LARA-21*

- **Retail:** nothing like it.
- **Ours:** `openlara/lara.h:3930-3933` calls `restore()` (`lara.h:417-425`),
  which gives full health. It is a leftover OpenLara cheat.
- **Fix:** remove it.

### 50. Lara's save block omits moveAngle, turnSpeed and swimTimer
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: LARA-22*

- **Ours:** Lara's state block in the save leaves out moveAngle, turnSpeed and
  swimTimer.
- **Effect:** after a load, Lara's movement can differ for a frame or two, for
  example mid-swim.

### 51. Switches, keyholes, puzzle holes and blocks can be used with Lara's back turned
*Severity: gameplay · Confidence: confirmed (code plus exhaustive count) · Fix:
small · Audit: MATH-7*

- **Retail:** TestLaraPosition (`0600f1bc`; TR1X `collide.c`) takes
  (int16)(lara.rot - item.rot) and compares it against signed bounds.
- **Ours:** `openlara/object.h:102-104` does
  `int16 ay = abs(lara->angle.y - angle.y)`. A raw difference of 32768 or more
  becomes negative and passes `ay > limit`.
- **Effect:** with the 30° yaw limit, the heading test is wrong for:
  - 150° of Lara's headings, for items with yaw -32768 (facing south)
  - 90°, for items with yaw ±16384
  - one value, for items with yaw 0

  Standing in a switch's or keyhole's box with her back to it, or side-on, Lara
  can still use it. This affects LIMIT_SWITCH, LIMIT_HOLE, LIMIT_BLOCK and
  LIMIT_MIDAS. Pickups are unaffected, because their item takes Lara's heading
  first.
- **Fix:** cast the difference to int16 before `abs`.

---

## Weapons and combat

### 52. Every weapon has unlimited ammo; no empty click, no switch back to pistols
*Severity: gameplay · Confidence: confirmed · Fix: medium · Audit: GUN-1*

- **Retail:** FireWeapon (`0601fd2c`; TR1X `larafire.c`).
  - Each call does `ammo->ammo--`; the shotgun loses 6 per shot.
  - At 0 it plays SFX 48 (the empty click) and requests pistols.
  - Pistols are reset to 1000 on every shot.
  - Starting ammo comes from the level's start info (InitialiseLaraInventory
    `06020b20`).
- **Ours:**
  - `weaponFire` copies the ammo into a local `int16 ammo` and decrements that,
    never the real value (`openlara/lara.h:3177`).
  - `lara.h:2722-2725` sets every weapon to -1 (infinite).
- **Effect:** magnums, Uzis and the shotgun never run out. The click and the
  switch back to pistols never happen.
- **Fix:** write the decrement back, count shotgun ammo 6 per shot, and start
  from the start info (issue 53). Ammo pickups must then add ammo, and the ring
  and HUD must show it (issues 144 and 159).

### 53. Starting inventory and weapon state come from OpenLara's GBA demo
*Severity: gameplay · Confidence: confirmed · Fix: medium · Audit: GUN-2,
INV-6*

- **Retail:**
  - Every level start clears the rings back to the compass only
    (Inv_RemoveAllItems `06019ca8`).
  - It rebuilds weapons, ammo and medipacks from per-level start info
    (InitialiseStartInfo `0602c088`, ModifyStartInfo `0602c0ec`,
    CreateStartInfo `0602c20c`). Keys, puzzle items and lead bars never carry
    over.
  - A new game in Caves has pistols only and 0 medipacks.
  - LaraInitialiseMeshes (`06020d34`) shows the holsters of the start gun type.
    The shotgun on Lara's back appears only if she owns it.
  - gun_status (drawn or not) and gun_type carry into the next level.
  - Natla's Mines takes Lara's weapons away at its start.
- **Ours:**
  - `inventory.init()` (`openlara/inventory.h:231-258`) runs once, in gameInit
    (`game.h:117`). It adds pistols, shotgun, magnums, Uzis, 10 small and 5
    large medipacks (`inventory.h:246-254`), and `gameLoadLevel` never resets
    the counts.
  - `lara.h:2712` always selects pistols, with a TODO for Natla's Mines.
  - `lara.h:2731` calls `meshSwapShotgun(false)` unconditionally.
  - Lara always starts with her guns holstered.
- **Effect:**
  - Every new game is fully armed, and the shotgun shows on Lara's back in
    level 1.
  - Keys and puzzle items carry into later levels, and survive Exit to Title and
    new games. Used medipacks stay used across new games.
  - Finishing a level with the Uzis drawn starts the next level holstered with
    pistols; retail starts it with the Uzis drawn.
  - Natla's Mines keeps Lara's weapons.
- **Fix:** per-level start info, as retail.

### 54. The target is dropped the moment it dies, and Lara retargets while Action is held
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: GUN-3,
GUN-14*

- **Retail:**
  - LaraTargetInfo (`0601f838`) has no hit-point check.
  - LaraGetNewTarget (`0601f9f4`) runs only when the target is NULL, which only
    happens when Action is released.
  - FireWeapon and HitTarget keep hitting the dead creature, with blood each
    time.
  - All 6 shotgun pellets test the same target, dead or not.
- **Ours:**
  - `openlara/lara.h:3699` drops the target
    (`if (arm.target && arm.target->health <= 0) arm.target = NULL`), and
    weaponFindTargets runs in the same frame.
  - `lara.h:3212` skips the sphere test on a dead target, so the remaining
    pellets of a killing shotgun blast ricochet as sparks.
- **Effect:** holding fire through a pack of wolves swaps to the next wolf
  automatically. Retail Lara keeps pumping bullets into the corpse until you let
  go of Action.
- **Fix:**
  - Drop the health check.
  - Allow hits on a target at 0 hp or below: blood, but no kill count.
  - Relax the ASSERT in `Enemy::hit`.

### 55. The shotgun runs OpenLara's own animation scheme
*Severity: gameplay / visible · Confidence: confirmed (timing and sound) · Fix:
medium · Audit: GUN-4, GUN-24*

- **Retail:** AnimateShotgun (`0601ea54`), UndrawShotgun (`0601e76c`),
  DrawShotgun (`0601e6f4`); TR1X `lara1gun.c`.
  - The frames are absolute:

    | Frames | Phase |
    |---|---|
    | 0–12 | AIM |
    | 13–46 | DRAW |
    | 47–79 | RECOIL |
    | 80–113 | UNDRAW |
    | 114–126 | UNAIM |

  - Locked on a target: fire, run 48 to 79, back to 47. That is a 33-frame
    cycle.
  - Not locked (Action held, no target): the recoil is cut at 59, the gun jumps
    to frame 0 (rest) and is raised again over 1–12. That is a 26-frame cycle.
  - The rest pose is frame 0. Lowering uses UNAIM.
  - Holstering uses UNDRAW, with the mesh swap and SFX 6 at frame 101 (the
    Saturn value; PC uses 100).
  - From rest, the first shot comes after 13 frames.
- **Ours:** `openlara/lara.h:3564-3641`.
  - The rest pose is the last frame of DRAW.
  - Firing always cycles in 26 frames (reloadTimer 26, `lara.h:213`).
  - Lowering plays AIM backwards, and holstering plays DRAW backwards with
    `SND_HOLSTER` (7) (`lara.h:3455`).
  - The draw starts at frame 14 instead of 13, so the first shot comes a frame
    early.
- **Effect:** against a locked enemy our shotgun fires about 27% faster (0.87 s
  against 1.1 s). Firing at nothing shows a longer pump with no lower and raise,
  and the holster sound is wrong.
- **Fix:** port the three frame machines verbatim, with absolute frame numbers.

### 57. The shotgun's own animation drives Lara's torso
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GUN-6*

- **Retail:** DrawLara (`06013438`) takes the torso from Lara's body frame plus
  torso_y/x only. The gun animation's torso key is ignored.
- **Ours:** `openlara/draw.h:413-421` and `538-546` add the shotgun animation's
  torso joint minus DEF_TORSO_ANGLE.
- **Effect:** Lara's upper body rocks during the shotgun's draw, recoil and
  reload; in retail only the arms move.
- **Fix:** delete the block.

### 58. The shot hit test uses an approximate length, so targets are 15% smaller
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: GUN-7,
MATH-6*

- **Retail:** FireWeapon (`0601fd2c`) accepts a sphere when x² + y² ≤ r²,
  exactly (a mul.l sequence).
- **Ours:** `openlara/lara.h:3230` rejects the shot when
  `fastLength(s.center.x, s.center.y) > s.radius`. `fastLength` is max + min/2,
  which overestimates by up to 12%.
- **Effect:** each sphere accepts only about 85% of its true area. There are more
  misses and ricochets, so fights take more shots; it shows most with pistols
  and Uzis at range.
- **Fix:** an exact squared test.

### 60. Selecting the current, holstered weapon in the ring draws it
*Severity: gameplay (minor) · Confidence: likely (the decomp is unambiguous;
not seen running) · Fix: small · Audit: GUN-9*

- **Retail:**
  - The Saturn's UseItem (`06020820`) only sets request_gun_type.
  - LaraGun (`0601f3f4`) draws only when the request differs from gun_type, or
    when the draw button is pressed.
  - TR1X differs here (its UseItem resets gun_type); the Saturn wins.
- **Ours:** `openlara/lara.h:3871` changeWeapon sets weapon = NONE, which forces
  a draw.
- **Effect:** picking the pistols in the ring while they're holstered draws them.
  Retail closes the ring with them still holstered.

### 61. The hit stagger lasts 5 frames past contact
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GUN-10*

- **Retail:**
  - LaraBaddieCollision (`0600e9ac`) resets hit_direction to -1 every frame.
  - ItemPushLara (`0600edf8`) increments hit_frame only while Lara is being
    touched, and resets it to 0 when not.
  - SFX 27 plays each time hit_frame is 0.
- **Ours:** `openlara/item.h:1279` calls `animHit(..., 5)`, with the timer at
  `lara.h:386-404`.
- **Effect:** brief wolf contacts give longer stagger poses, and intermittent
  contact plays fewer body-slam sounds.

### 62. Low creatures never stagger Lara
*Severity: visible · Confidence: confirmed (code), likely (which creatures) ·
Fix: small · Audit: GUN-11*

- **Retail:** CreatureCollision (`0600ebbc`) staggers whenever the creature is
  alive and the stagger is enabled. There is no height gate.
- **Ours:** `openlara/item.h:1251` requires `(box.maxY - box.minY) > 256`.
- **Effect:** rats, water rats and bats push Lara without staggering her, and
  possibly crocodiles too, depending on their box sizes.

### 63. A dying creature still staggers Lara
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: GUN-12*

- **Retail:** CreatureCollision passes spaz = 0 when the creature's hp is 0 or
  below.
- **Ours:** `openlara/enemy.h:765` calls `collidePush(lara, cinfo, true)`
  regardless of health.
- **Effect:** touching a creature during its death animation makes Lara stagger
  in ours only.

### 64. Blood from Lara's bullets is stationary, and the mummy doesn't bleed
*Severity: visible (subtle) · Confidence: confirmed · Fix: small · Audit:
GUN-13*

- **Retail:** HitTarget (`0601fff4`) calls DoBloodSplat(hit position,
  item->speed, item->y_rot) for every creature, so the blood drifts with the
  target.
- **Ours:** `openlara/enemy.h:745-747` calls `fxBlood(point, 0, 0)`, and skips
  ITEM_MUMMY.
- **Effect:** on running enemies the blood stays in place, and the mummy shows no
  blood.

### 65. Enemy gunmen: blood at their gun, no hit sound, no muzzle flash, misses in mid-air
*Severity: visible / audible · Confidence: confirmed (the ricochet height is
likely) · Fix: medium · Audit: GUN-15, AI-27, GFX-9*

- **Retail:** GunShot (`060286e4`), GunHit (`06028778`), GunMiss
  (`06028890`); TR1X `people.c`.
  - Every shot spawns an O_GUN_FLASH mesh effect: 3 frames, shade 0x1000,
    random roll.
  - A hit puts blood on a random joint of Lara and plays SFX 50 at Lara.
  - A miss puts the ricochet at Lara's floor height, within ±256 in x and z,
    using the **draw** RNG.
- **Ours:** `Gunman::shoot` (`openlara/enemy.h:2249-2267`).
  - A hit calls `bite()`, which puts the blood at the shooter's own muzzle joint
    (`enemy.h:448-449`).
  - A miss puts the ricochet 512–1535 units above Lara's feet
    (`enemy.h:2262`), using the logic RNG.
  - There is no flash; MUZZLE_FLASH is never instanced.
  - SFX 50 is never played.
- **Effect:** Larson, Pierre, the cowboy, Kold, the skater and Natla fire without
  a flash. A hit shows a red puff at their gun with no sound, and a miss shows
  sparks in the air instead of on the floor.
- **Fix:** port GunShot, GunHit and GunMiss.

### 66. Creature bite blood doesn't move with the creature
*Severity: subtle · Confidence: confirmed (PC code; the Saturn bite was not
checked) · Fix: small · Audit: GUN-16*

- **Retail:** CreatureEffect (`0600bf8c`; TR1X `box.c`) passes the creature's
  speed and y_rot to DoBloodSplat.
- **Ours:** `openlara/enemy.h:449` uses speed 0 and angle 0.

### 67. The gun flash's rotation is fixed for the whole flash
*Severity: subtle (visible) · Confidence: confirmed · Fix: small · Audit:
GUN-17, GFX-18*

- **Retail:** DrawGunFlash (`06013d5c`) re-rolls the roll angle,
  Random_GetDraw()·2, on every drawn frame.
- **Ours:** `openlara/lara.h:3531` rolls once per shot, and the draw reuses it
  (`draw.h:268`).
- **Effect:** retail's pistol, magnum and Uzi flash flickers its orientation over
  its 3 frames (2 for the Uzis); ours holds still.

### 68. Pistol draw and holster frames are off by one
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: GUN-18*

- **Retail:**
  - DrawPistols (`0601ec7c`) shows frames 5–22 and is ready at 23.
  - UndrawPistols (`0601ecf4`) shows 23 down to 6 and is armless at 5. From
    recoil it goes straight to frame 4.
- **Ours:** `openlara/lara.h:3310-3395`.
  - The draw shows 6–23, so the guns reach the hands a frame early.
  - The holster shows 23 down to 5, a frame longer.
  - From FIRE the holster takes two steps: frame 0, then AIM.
- **Fix:** port with absolute frames.

### 71. Water rats chirp when shot
*Severity: audible · Confidence: confirmed · Fix: small · Audit: GUN-21,
SND-21*

- **Retail:** HitTarget (`0601fff4`) plays SFX 95 for O_RAT (16) only.
- **Ours:** `Rat::hit` (`openlara/enemy.h:1877`) also covers ITEM_RAT_WATER.

### 72. Shotgun pellets draw random numbers in a different order
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: GUN-22*

- **Retail:** FireShotgun (`0601eb8c`) draws the y scatter, then the x scatter.
  FireWeapon then draws two more accuracy rolls (×0). That is 4 control-RNG
  calls per pellet.
- **Ours:**
  - `openlara/lara.h:3201-3202` draws 2 per pellet, x first.
  - The ± spread uses `>> 16` (floor) where retail truncates toward zero.
- **Effect:** nothing visible. The shared control-random sequence, which the AI
  also uses, drifts.

### 73. A dead Lara keeps her weapon READY
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: GUN-23*

- **Retail:** LaraGun (`0601f3f4`) forces ARMLESS when hp is 0 or below.
- **Ours:** `openlara/lara.h:3830-3835` returns early and leaves weaponState as
  it was.
- **Effect:** checks for READY (combat triggers, the health bar, the flash) still
  see a dead Lara with her guns ready. Almost never visible.

---

## Creatures and AI

### 74. Every triggered creature's collision radius becomes 0
*Severity: gameplay / visible · Confidence: confirmed · Fix: small · Audit:
AI-1*

- **Retail:** CreatureAnimation (`0600b000`; TR1X `box.c`) pushes the creature
  back from bad floor by the object's full radius (wolf 341). CreatureCreature
  uses that radius squared. The radius comes from the object table (object setup
  `0602cf08`; OBJECT_INFO +0x2a).
- **Ours:**
  - `openlara/enemy.h:35` stores `radius = _radius >> 1`, half the value.
  - Worse, `radius` shares a union with `timer` (`common.h:1736`), and the
    trigger code writes `item->timer = FD_TIMER(data)` on every item it
    activates (`room.h:1010`).
  - Every creature trigger in the levels has timer 0, so the radius is 0 from
    first activation.
  - The radius tests in `updateLocation` (`enemy.h:556-645`) and
    `overlapsCreature` (`enemy.h:482`) never fire. Commit 1164c5a's wall-slide
    and creature-parting only half-work: the edge clamp works, the radius part
    doesn't.
- **Effect:** in every level, creatures hug walls and ledges with half their body
  inside them, and pairs of wolves or rats merge into each other.
- **Fix:** give enemies their own radius field, or stop the trigger writing timer
  on enemies; store the full radius.

### 75. Only the bear uses the violent mood; 12 other creature types use the timid one
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-2*

- **Retail:**
  - CreatureMood(..., violent = 1) is called for the bear, crocodile, water
    crocodile, lions, lioness, puma, raptor, T-rex, water rat, Kold, centaur,
    mutants, Natla and the torso.
  - Violent creatures always attack when Lara is in their zone, and flee only
    when hit while she is out of it.
  - Checked at the call sites in `060119e0`, `06011698`, `06011f94`,
    `060122fc`, `06032a78`, `0602bd64`, `0602a558`, `06030cf8`, `06030ffc`,
    `0602500c` and `06024b34`.
- **Ours:** violent logic is chosen by `ITEM_FLAG_ANIMATED`
  (`openlara/enemy.h:195`), which only the Bear sets (`enemy.h:1170`).
- **Effect:** lions, raptors, the T-rex, crocodiles, Kold, centaurs and mutants
  stalk instead of charging whenever Lara is more than 3 sectors away. They also
  run away on about 1 hit in 16, or whenever hit while she is out of their zone.
  Retail never does that.
- **Fix:** a per-type violent flag. Separate it from the look-at mark first
  (issue 24).

### 76. Wolf pounce decision is inverted, with the wrong distance
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-5,
MATH-7*

- **Retail:** WolfControl (`06032664`), RUN state (TR1X `ai/wolf.c`). Within
  1536 units, the wolf pounces (ATTACK) if it is within half the squared attack
  range (about 1086 linear), **or** if Lara is facing it. It drops to stalking
  only when it is far and her back is turned.
- **Ours:** `openlara/enemy.h:1040-1049` pounces only when within 768 (half the
  linear range) **and** Lara is not facing it; otherwise it growls and stalks.
  The facing flag itself is broken half the time (issue 77).
- **Effect:** wolves in Caves, Vilcabamba, Lost Valley and Qualopec seldom
  pounce. They creep up and bite (100 damage) instead of pouncing (50).

### 77. The "Lara is facing me" test doesn't wrap the angle
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-6,
MATH-9*

- **Retail:** CreatureAIInfo (`06009950`) computes
  enemy_facing = (int16)(angle - lara_yrot + 0x8000), then tests |·| < 90°.
- **Ours:** `openlara/enemy.h:177` does
  `abs(rot - target->angle.y - ANGLE_180) < ANGLE_90` in int32. It is wrong for
  25% of all angle pairs (half of the true cases), depending on the sign of
  rot - ay.
- **Effect:** the wolf's STALK/RUN choices (`enemy.h:1042`, `1072`) and the
  T-rex's walk decision (`enemy.h:2005`) are wrong about half the time.
- **Fix:** cast the difference to int16.

### 78. The water crocodile can't move up or down
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-7*

- **Retail:** AlligatorControl (`06011698`) flies at 16 per frame, and stays at
  least 256 below the water surface.
- **Ours:** `Crocodile::initExtra` (`openlara/enemy.h:1560-1569`) never sets
  `nav.vSpeed`, so it stays 0 from `setExtra` (line 57). `updateLocation` clamps
  vertical movement to ±0.
- **Effect:** water crocodiles stay at their starting depth, and can bite Lara
  only at that depth. This affects the Folly, Colosseum, Midas, Cistern,
  Tihocan, Khamoon and Obelisk.
- **Fix:** vSpeed 16, plus the surface clamp.

### 79. Water rats crawl along the pool bottom
*Severity: gameplay / visible · Confidence: confirmed · Fix: small · Audit:
AI-8*

- **Retail:** VoleControl (`0602bd64`) moves pos.y toward the water surface by
  32 per frame.
- **Ours:** ITEM_RAT_WATER uses ground navigation and floor-following
  (`openlara/enemy.h:702-710`), so it sinks 64 per frame to the floor.
- **Effect:** in Midas and the Cistern, water rats crawl along the pool floor and
  can't bite Lara swimming at the surface.

### 80. Crocodiles and rats don't switch between land and water forms
*Severity: gameplay / visible · Confidence: confirmed · Fix: medium · Audit:
AI-9*

- **Retail:**
  - A land crocodile entering an underwater room becomes the water crocodile. A
    water crocodile with no water under it becomes the land one.
  - A rat in water becomes a water rat, and a water rat leaving water becomes a
    rat.
  - A dead water crocodile floats up at 32 per frame to the surface, then its AI
    is released.
  - `06011698`, `0602ba74`.
- **Ours:** the type is fixed (`openlara/enemy.h:1462-1490`, `1888-1914`). A
  land crocodile or rat that enters water walks along the bottom, and dead water
  crocodiles stay down.

### 81. Fleeing creatures don't turn back when they reach their flee point
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-10*

- **Retail:** CreatureMood (`0600a0fc`; TR1X `box.c`). If the target box is no
  longer valid and Lara is in the creature's zone, the mood becomes BORED. BORED
  then re-picks ATTACK or STALK.
- **Ours:** `openlara/enemy.h:186-193` sets SLEEP when Lara is **not** in the
  zone, and otherwise keeps the mood. A fleeing creature keeps picking new escape
  boxes until a 1/128-per-frame recovery roll.
- **Effect:** wolves, rats, gorillas and gunmen flee far longer than in retail.

### 82. The T-rex, centaurs and the winged mutant ignore blockable boxes
*Severity: gameplay · Confidence: likely (code certain, level effect inferred)
· Fix: small · Audit: AI-11*

- **Retail:** InitialiseSlot (`06023278`; TR1X `lot.c`) gives the T-rex, the
  winged mutant (O_WARRIOR1) and the centaur the BLOCKABLE (0x8000) block mask.
  They never path into blockable boxes, and BadFloor treats those boxes as
  walls.
- **Ours:** `openlara/nav.h:26` uses the BLOCKED (0x4000) mask for every
  creature.
- **Effect:** Lost Valley has 19 blockable boxes, so the T-rex may follow Lara
  into hiding spots it can't reach in retail.

### 83. Natla never re-aims her path at Lara
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-12*

- **Retail:** Natla's smartness is 0x7FFF (object table).
- **Ours:**
  - The Natla constructor sets `aggression = 0` (`openlara/enemy.h:3140`), then
    reuses the field as her stage flag (0 or 1).
  - `moodAttack` (`enemy.h:269`) returns at once whenever rand ≥ aggression.
- **Effect:** in stage 2 she runs to random boxes instead of at Lara.
- **Fix:** use another field for the stage.

### 84. Enemy projectiles and the explosion effect don't exist
*Severity: gameplay / visible · Confidence: confirmed · Fix: large (shared by
85-87 and issue 2) · Audit: AI-13/14/15, GFX-10*

- **Retail:**
  - Projectiles are effects that travel:

    | Projectile | Fired by | Damage | Speed |
    |---|---|---|---|
    | Shard (O_MISSILE2 mesh, shade 3584) | ShardGun | 30 | 250 |
    | Rocket (O_MISSILE3 mesh) | RocketGun | 100, plus splash within 1024 | 220 |
    | O_MISSILE1 | Natla | | |

  - A hit makes Lara flinch, with the hit-direction stagger and SFX 31.
  - Missiles turn into O_EXPLOSION1 sprites on impact.
  - Saturn: effects 0xAC and 0xAD created around decomp.c:37010/37039;
    ControlMissile `060316a0`. TR1X `effects/missile.c`. LEVEL10C holds the
    MISSILE2/3 meshes.
- **Ours:**
  - `openlara/enemy.h:2817-2820` and `3110-3115` say "strike at once rather than
    fly". Every shot is instant hitscan at 100 damage, with no visible
    projectile.
  - The NATLA/MUTANT/CENTAUR_BULLET and EXPLOSION types are not instanced
    (`item.h:1647`, `1665-1668`).
  - Lara never staggers from a projectile, and SFX 31 never plays.
- **Effect:** Atlantis and Great Pyramid enemies hurt Lara with nothing visible
  travelling, can't be dodged, and nothing explodes.
- **Fix:** missile and explosion effects, ported from missile.c.

### 85. Natla's fight is missing flight, rockets, sounds and music
*Severity: gameplay · Confidence: confirmed · Fix: large · Audit: AI-13,
SND-19*

- **Retail:** NatlaControl (`0602500c`; TR1X `ai/natla.c`).
  - **Stage 1:** she flies (fly speed 32) and fires a rocket every 30 frames.
    She lands with a 1/128 chance per frame once she has a shot, and on the
    ground fires 3 spread rockets. Each shot plays SFX 123.
  - **Lying down:** her hit points are DONT_TARGET, so Lara can't target her.
    Music track 54 plays when she rises, once the semi-death timer reaches 480.
  - **Stage 2:** her shards do 30 damage and travel at 250 per frame.
- **Ours:** `openlara/enemy.h:3116-3263`.
  - No flight and no projectiles; shots are instant hitscan at 100 damage.
  - Her health is held at 1, so Lara's guns still lock on (`lara.h:3763` only
    skips health ≤ 0).
  - No SFX 123 and no track 54.
- **Needs:** 84 and 83.

### 86. Mutants: the winged one doesn't fly, shots are instant, they don't burst
*Severity: gameplay · Confidence: confirmed · Fix: large · Audit: AI-14*

- **Retail:** FlyerControl (`06030ffc`; TR1X `ai/mutant.c`).
  - The winged mutant flies when it can't reach or shoot Lara, or when fleeing.
  - Shard and rocket are travelling projectiles (issue 84).
  - On death, ExplodingDeath(-1, 100) (`06031c64`) throws body parts that hurt
    Lara.
  - The shooter's smartness is 0x2000.
- **Ours:** `openlara/enemy.h:2822-3000`.
  - Ground only, with instant `shoot()` at 100 damage for both shot types.
  - On death it shows blood and the body vanishes (`enemy.h:2864-2875`).
  - The shooter's smartness is 0x7FFF.
- **Needs:** 84 and 213.

### 87. Centaurs: the rocket is instant, and they don't burst
*Severity: gameplay · Confidence: confirmed · Fix: large (shared with 84, 213) ·
Audit: AI-15*

- **Retail:** CentaurControl (`06030cf8`; TR1X `ai/centaur.c`) fires RocketGun,
  a travelling projectile, and dies with ExplodingDeath(-1, 100).
- **Ours:** `openlara/enemy.h:3085-3093` hits instantly, and on death the centaur
  just turns invisible (`enemy.h:3039-3047`).

### 88. The torso boss doesn't grab Lara for its kill, and steers wrongly
*Severity: gameplay / visible · Confidence: confirmed · Fix: medium · Audit:
AI-16, CAM-23*

- **Retail:** AbortionControl (`06024b34`; TR1X `ai/abortion.c`).
  - **ATTACK3** (case 6) moves Lara to the torso and sets the O_LARA_EXTRA
    animation 0 and AS_SPECIAL. It holsters her guns, sets air to -1, and sets
    the camera to target_distance 2048 with FOLLOW_CENTRE.
  - **Steering:** it steers toward the path target, not toward Lara.
  - **FORWARD:** never turns. This is a TR1 bug: the turn is added to
    goal_anim_state instead of yaw (verified on Saturn, case 7).
  - **Death:** ExplodingDeath(-1, 250).
- **Ours:** `openlara/enemy.h:3416-3423` only calls `lara->hit(1000)`, so Lara
  drops in her normal death while the torso crushes empty air.
  - FORWARD turns 3° per frame toward Lara (`enemy.h:3378`).
  - The death has no body parts.
- **Decision:** reproduce the FORWARD no-turn bug? It shapes how the fight plays.

### 89. The T-rex walks continuously; retail's stops and goes
*Severity: gameplay / visible · Confidence: confirmed · Fix: small · Audit:
AI-17*

- **Retail:** DinoControl (`060122fc`, case 2). WALK goes to STOP if
  `mood != BORED || !flags`, which gives the familiar stop-and-go stomping.
- **Ours:** `openlara/enemy.h:2023` stops only if `mood != SLEEP && !walk`.

### 90. Kold's hit chance at range is too high
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-18*

- **Retail:** BaldyControl (`0602a558`; TR1X `ai/baldy.c`) passes
  ShotLara(info.distance / 2), half of the **squared** distance.
- **Ours:** `openlara/enemy.h:2803` passes `tinfo.dist >> 1`, half of the
  **linear** distance. That is a quarter of the squared distance.
- **Effect:** at 6 sectors he hits about 57% of the time instead of 38%.

### 92. The puma has 40 hit points; retail's has 45
*Severity: gameplay (subtle) · Confidence: confirmed (object table entry 14 =
0x2D) · Fix: small · Audit: AI-20*

- **Ours:** `openlara/enemy.h:1581` sets 40.
- **Effect:** pumas in Khamoon and the Obelisk die one magnum shot early.
- **Checked:** the puma is the only difference. The hit points the Saturn's
  object setup (`0602cf08`) writes at OBJECT_INFO +0x26 match ours for every
  other creature:

  | Creature | HP | Creature | HP |
  |---|---|---|---|
  | wolf | 6 | bear | 20 |
  | bat | 1 | crocodile, alligator | 20 |
  | lion | 30 | lioness | 25 |
  | gorilla | 22 | rat, water rat | 5 |
  | T-rex | 100 | raptor | 20 |
  | mutant | 50 | centaur | 120 |
  | mummy | 18 | Larson | 50 |
  | Pierre | 70 | skater | 125 |
  | cowboy | 150 | Kold | 200 |
  | Natla | 400 | torso | 500 |
  | doppelganger | 1000 | | |

- **Damage:** the retail weapon table at `0x06062814` gives 1 (pistols), 2
  (magnums), 1 (Uzis) and 4 per shotgun pellet. These match ours too.

### 93. The bear never eats Lara's body
*Severity: visible · Confidence: confirmed · Fix: small · Audit: AI-21*

- **Retail:** BearControl (`0600947c`, case 1). With Lara dead, the bear goes
  from STOP to EAT when she is within bite range and closer than 768.
- **Ours:**
  - `updateTargetInfo` returns early when Lara is dead, and zeroes `canAttack`,
    `front` and `hitMask` (`openlara/enemy.h:155-159`).
  - So the EAT check at `enemy.h:1244` can never pass.
- **Fix:** keep computing the target info after her death, as retail does.

### 94. The bear's upright walk is cut short every frame
*Severity: visible · Confidence: confirmed · Fix: small · Audit: AI-22*

- **Retail:** BearControl, case 2. WALK keeps its goal unless roar, drop, touch
  or escape applies.
- **Ours:** the upright-walk state (`STATE_HIND`) always ends with
  `return STATE_HOWL` (`openlara/enemy.h:1270`), so the bear drops straight back
  to rearing.
- **Fix:** return goalState.

### 95. The T-rex's head and neck barely turn toward Lara
*Severity: visible · Confidence: confirmed · Fix: small · Audit: AI-23*

- **Retail:** CreatureHead(head >> 1) and neck = head, so up to 45° each.
- **Ours:** `openlara/enemy.h:1997` halves the actual head each frame and then
  lerps. It settles at about 10° for the head and 5° for the neck.

### 96. Lara walks through the dead T-rex
*Severity: visible · Confidence: confirmed · Fix: small · Audit: AI-24*

- **Retail:** DinoControl sets collidable = 1 every frame, including on the
  frame it deactivates (`|= 4` on byte 0x44).
- **Ours:** collision is cleared on deactivation (`openlara/enemy.h:809-817`).

### 97. The T-rex kill doesn't set up Lara's state or the camera
*Severity: visible · Confidence: confirmed (missing state), suspected (that her
guns stay visible) · Fix: small · Audit: AI-25*

- **Retail:** LaraDinoDeath (`060125f4`) sets Lara's guns to hands-busy and
  unarmed, sets air to -1, and sets the camera to follow-centre at 170° yaw and
  -25° pitch.
- **Ours:** `openlara/enemy.h:2048-2072` only moves Lara, swaps her meshes and
  sets the animation.

### 98. Bored flying creatures aim at the floor
*Severity: visible · Confidence: confirmed · Fix: small · Audit: AI-26*

- **Retail:** TargetBox, in CreatureMood (`0600a0fc`), sets the target y to the
  box height - 384 for flyers, and to the box height for walkers.
- **Ours:** `openlara/enemy.h:335-337` has the two swapped.
- **Effect:** after biting, a bored bat dives to floor level, and a bored water
  crocodile aims at the bottom.

### 99. The gorilla can't climb onto blocks, and its sidestep turns differ
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: AI-28*

- **Retail:**
  - ApeVault (`06029570`) plays a climb animation (anim 19), and snaps the
    gorilla onto the ledge edge, 75 units in, whenever it rises 384 or more in a
    frame.
  - The sidestep's 90° turn correction runs every frame inside ApeVault. A TR1
    bug checks `item->flags & 4` instead of the creature flag (bug present on
    Saturn, `060296b8`). The net effect: the right sidestep never visibly
    rotates, and the left one rotates once and stays rotated.
- **Ours:** no vault (`openlara/enemy.h:1701`). The sidestep rotates while
  stepping and snaps back at STOP (`enemy.h:1759-1768`, `1807-1825`).
- **Decision:** reproduce the sidestep bug?

### 100. Qualopec mummies behave like full creatures
*Severity: visible · Confidence: confirmed · Fix: small to medium · Audit:
AI-29*

- **Retail:** InitialiseMummy (`06031b68`) and MummyControl (`06031ba8`).
  - No random yaw.
  - It uses no AI slot.
  - Its head always tracks Lara, clamped to ±90°.
  - Hits on it bleed (HitTarget `0601fff4`).
- **Ours:** `Mummy` is a full Enemy (`openlara/enemy.h:2196-2218`).
  - It gets a random yaw.
  - It takes one of the 8 AI slots, so it can be evicted and vanish.
  - It turns its head only when Lara is in front and its mood isn't SLEEP.
  - It shows no blood (`enemy.h:745`).

### 101. Bored creatures never look at Lara
*Severity: visible · Confidence: confirmed · Fix: small · Audit: AI-30*

- **Retail:** every creature control does `if (info.ahead) head = info.angle`,
  whatever the mood.
- **Ours:** `openlara/enemy.h:179` requires `mood != MOOD_SLEEP`.
- **Effect:** walking wolves, a posing Larson and other bored creatures don't
  turn their heads toward Lara.

### 102. Blocking is not implemented
*Severity: subtle (gameplay in places) · Confidence: confirmed · Fix: medium ·
Audit: AI-31*

- **Retail:** CreatureAIInfo (`06009950`), CreatureMood (`0600a0fc`) and
  ValidBox (`0600a04c`).
  - Lara's zone is marked BLOCKED when her box is blocked, for example when she
    stands on a pushable block.
  - A box reachable only through blocked boxes clears the required box.
  - ValidBox rejects blocked boxes.
- **Ours:** `@TODO blocking` at `openlara/enemy.h:150`, `184` and `389`.
- **Effect:** creatures keep "attacking" a Lara standing on a block, and can pick
  target boxes behind closed doors.

### 103. Creatures hit while Lara is out of their zone rarely flee
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: AI-32*

- **Retail:** CreatureMood, attack mood: when the creature is hit and Lara is out
  of its zone, it always flees.
- **Ours:** it flees 1 time in 16, and otherwise goes bored
  (`openlara/enemy.h:367`).

### 104. Flyers in transit fly too low
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: AI-33*

- **Retail:** CalculateTarget lifts a flyer's target to at least 1024 above
  each box on the path.
- **Ours:** keeps the current height on secondary targets (`openlara/nav.h`).
- **Effect:** bats fly lower along corridors.

### 105. No random target point when there's no path
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: AI-34*

- **Retail:** with no path yet, the target is a random point in the last box.
- **Ours:** clamps instead (`openlara/nav.h:307-311`).

### 106. Fly checks: no fly-height floor test, and the ceiling test uses the origin
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: AI-35*

- **Retail:** BadFloor rejects a floor where `y > height + fly`. The ceiling
  test uses the creature's top bound.
- **Ours:** there is no fly test (`openlara/enemy.h:457-472`), and the ceiling
  test uses the origin (`enemy.h:676-688`).
- **Effect:** bats can poke into ceilings.

### 107. The creature turn is applied after the move
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: AI-36*

- **Retail:** CreatureTurn (`0600be48`) rotates before AnimateItem, so the
  creature moves along its new heading.
- **Ours:** `angle.y += turn` is applied after the move
  (`openlara/enemy.h:807`).
- **Effect:** a one-frame lag of up to 6°.

### 108. Wolf and raptor details
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: AI-37*

- Ours tilts the wolf and the raptor while walking (`openlara/enemy.h:1022`,
  `2139`); retail doesn't.
- The raptor's walk roar lacks retail's `info.ahead` check (`enemy.h:2144`).
- The wolf's bite lacks `info.ahead` (`enemy.h:1125`).
- The wolf's bite state sets its goal to RUN (`enemy.h:1129`); retail leaves the
  goal alone.

### 109. The bear's hurt flag is sticky
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: AI-38*

- **Ours:** once the bear is hurt, the injured flag is set again every frame
  (`openlara/enemy.h:1207-1214`). The violent mood therefore always treats it as
  just hit.
- **Effect:** a hurt bear flees whenever Lara is out of its zone.

### 110. Dead flyers drift toward a stale waypoint
*Severity: subtle · Confidence: likely · Fix: small · Audit: AI-39*

- **Ours:** dead bats and dead water crocodiles move vertically toward the shared
  global `tinfo.waypoint` (`openlara/enemy.h:657`). That waypoint was left by
  whichever creature updated last.

### 111. The water crocodile's smartness is 0x2000; retail's is 0x400
*Severity: subtle · Confidence: confirmed (object table) · Fix: small · Audit:
AI-40*

- **Ours:** it reuses the land crocodile's value (`openlara/enemy.h:1450`).

### 112. AI distances use an approximation, and enemy gunfire's hit chance is off
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-41,
MATH-6, MATH-8*

- **Retail:**
  - CreatureAIInfo (`06009950`; TR1X `box.c:75`) sets distance = dx² + dz², and
    every range is a squared constant. CreatureTurn compares x² + z² < range².
  - ShotLara (`060289b8`; TR1X `people.c:97-107`): hit if
    rand < (R - d²) / (R / 0x7FFF) - 0x2000, with R = 7168² and an exact d².
- **Ours:**
  - `tinfo.dist = fastLength(dx, dz)`, which is max + min/2
    (`openlara/enemy.h:174`, `common.h:2787`). `updateNavigation` uses it too
    (`enemy.h:313`).
  - It overestimates by 0% along the world axes, 6.1% at 45° and 11.8% at
    26.6°, 8.7% on average. Every bite, attack, pounce and stalk threshold
    triggers up to 10.6% closer on diagonal approaches; a wolf bite at 345
    effectively becomes 309.
  - Enemy gunfire (`enemy.h:2254-2255`) squares `fastLength >> 4`. R = 448²
    makes R/0x7FFF truncate to 6 instead of 6.125. The range gate also uses
    `fastLength` (`enemy.h:2237`, `2252`).
- **Effect on gunfire hit probability** (retail, then ours along an axis / at
  26.6° / at 45°):

  | Distance | Retail | Ours |
  |---|---|---|
  | 1 sector | 0.730 | 0.750 / 0.745 / 0.748 |
  | 3 sectors | 0.566 | 0.583 / 0.538 / 0.561 |
  | 5 sectors | 0.240 | 0.250 / 0.123 / 0.186 |

  The effective maximum range drops from 7 to 6.26 sectors on diagonals.
- **Fix:** set `tinfo.dist = phd_sqrt(dx² + dz²)` once per creature per tick. The
  linear constants already equal the square roots of retail's, except
  DIST_STALK (3069 against 3072). Exact squares in the gunfire.

---

## Level logic, traps and objects

### 113. Save crystals are disabled (reported in play)
*Severity: gameplay · Confidence: confirmed · Fix: medium (with 152-153) ·
Audit: LVL-30*

- **Ours:** `openlara/object.h:399-411` sets the crystal (item 83) invisible
  ("disable crystals for now"), its `collide` is a TODO, and it is never
  activated.
- **Retail:** save crystal init `0602ba1c`, control `0602ba34`, draw
  `0602ba5c`, collision `0602b940`; PC equivalent `savegame_crystal.c`.
  - **Where:** every level except the Gym has 1 to 7 crystals.
  - **Init:** added to the active list at level start.
  - **Control:** animates every frame. Its one animation is 60 frames at rate 8
    (8 keyframes), looping about every 2 s. The keyframes spin it about Y and bob
    it between about -268 and -404 above the item's origin.
  - **Draw:** the ordinary animated-item routine (`060130fc`). There is no
    special glow. The mesh is a 5-vertex, 6-triangle flat-coloured bipyramid,
    lit by vertex normals.
  - **Collision, every frame:**
    1. Solid push first (ObjectCollision `0600ec4c`).
    2. Then require Action pressed, guns holstered, Lara not in the air, and
       Lara standing still.
    3. Turn the crystal to Lara's yaw (x and z rotation 0), and test her position
       with the pickup bounds at `0x06062c2c`: x ±256, y ±100, z -256 to +100,
       Lara's pitch within ±10°.
    4. If she passes, set the crystal invisible **before** building the save
       (CreateSaveGameInfo `0602c35c`), so the save already has it used.
    5. Call `0603c1bc`(save buffer, 0xD7C, 1). That shows the "Save Game?"
       YES/NO prompt (default YES), writes backup RAM, and ends with "Save
       Complete!" for 60 vblanks. On failure it shows "The file could not be
       saved! Try Again? YES/NO".
    6. On success the crystal stays invisible and is removed from the room, so
       it's single use. On NO or a failure, its status goes back to active.
  - There is no Lara animation and no sound in the collision itself.
- **Related:** the save model itself (152), the prompt (153), backup RAM (155).

### 114. Boulders never reset
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: LVL-6*

- **Retail:** the rolling ball (`0602f33c`; TR1X `traps/rolling_ball.c`).
  - A stopped boulder stays in the active list. When its trigger goes inactive
    (antipad), it returns to its start position and resets its animation, ready
    to roll again.
  - A boulder death sets the camera to follow-centre at 170°, elevation -25°.
- **Ours:** `openlara/object.h:1553-1563` deactivates it for good.
- **Effect:** the antipad-reset boulders don't reset: Natla's Mines 33 and 35,
  Atlantis 127.

### 115. Centaur statues wake on proximity instead of their trigger, and don't shatter
*Severity: gameplay / visible · Confidence: confirmed · Fix: small (wake),
medium (shatter) · Audit: LVL-7, AI-27*

- **Retail:** statue init and control (`06032454`, `0603255c`; TR1X
  `ai/statue.c`).
  - The init does not add the statue to the active list, so it only works once
    triggered. In Tomb of Tihocan, statue 73 waits for switch 79 and statue 74
    for pads in room 109.
  - Until it wakes, it is solid and pushes Lara.
  - When it wakes, it shatters into pieces (ExplodingDeath `06031c64`) with SFX
    104. The centaur takes the statue's exact yaw and is enabled with force.
- **Ours:**
  - `openlara/object.h:2006` activates the statue in its constructor, and
    `object.h:1997-2023` just hides it.
  - There are no shards and no collision.
  - The spawned centaur gets the Enemy constructor's random ±45° yaw
    (`enemy.h:29`), because the yaw is set before the constructor runs.
- **Needs:** 213 for the shards.

### 116. Mutant eggs throw no shell shards, and Lara walks through them
*Severity: visible · Confidence: confirmed · Fix: medium (needs 213) · Audit:
LVL-8, AI-27*

- **Retail:** the pod (`060321e8`, `0603231c`; TR1X `ai/pod.c`) calls
  ExplodingDeath with mask 0xFFFE00, throwing shell meshes 9–23 and keeping
  0x1FF. The eggs are solid.
- **Ours:** `openlara/object.h:2028-2079` sets `visibleMask = 0xFFFFFF`
  (`object.h:2069`), so all 24 meshes stay drawn after hatching. There is no
  collision.
- **Effect:** hatched eggs keep their shell; Lara walks through eggs.

### 117. Lightning: nothing drawn, wrong strike radius, half the thunder, no flinch
*Severity: visible / gameplay · Confidence: confirmed · Fix: medium · Audit:
LVL-9, GFX-13*

- **Retail:** the lightning emitter (`0602281c`, `06022888`), DrawLightning
  (`06022358`); TR1X `traps/lightning_emitter.c`.
  - It draws the emitter's first mesh (LEVEL4's has 49 faces). When the bolt is
    on, it adds a jagged 8-step bolt with 2 branches.
  - The strike radius is 2560 only for multi-mesh emitters. The Folly's emitter
    has 6 meshes; the Pyramid's has 1, so its radius is 1024.
  - Thunder (SFX 98) plays on both the strike-on and strike-off frames.
  - A hit makes Lara flinch (the hit-direction animation), with 400 damage.
- **Ours:**
  - `openlara/object.h:1903` always uses 2560.
  - Line 1912 plays thunder only on the strike.
  - `Lightning::draw() {}` (line 1916) draws nothing, so the Folly emitter's own
    mesh is missing too.
- **Effect:** the Folly's Thor room and the Great Pyramid flash the room flip with
  no bolt and no emitter model.

### 118. Waterfall mist is missing
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LVL-10,
GFX-15*

- **Retail:** ControlWaterFall (`0601586c`; TR1X `effects/waterfall.c`). While
  triggered and with Lara within 10 sectors, each waterfall spawns an O_SPLASH1
  sprite every frame at a random point within ±1 sector.
- **Ours:** `Waterfall` has no update (`openlara/object.h:231-236`).
- **Effect:** Lost Valley's 12 waterfall emitters, and the one in Natla's Mines,
  show no spray. The splash sprite effect already exists, so this is cheap.

### 119. Lava emitters throw nothing
*Severity: gameplay / visible / audible · Confidence: confirmed · Fix: medium ·
Audit: LVL-11, GFX-14*

- **Retail:** LavaEmitterControl (`060309b8`) and the lava blob (`06030aa0`);
  TR1X `traps/lava.c`.
  - Every active frame, an emitter throws a lava blob (object 176) with random
    direction and speed, and plays SFX 149.
  - Blobs fall under gravity 6, and do 10 damage on contact with Lara.
  - The emitter is solid.
- **Ours:** `LavaEmitter` has no update and an empty draw
  (`openlara/object.h:239-244`); the lava blob isn't implemented.
- **Effect:** no lava fountains, no damage from them, no sound. That is 6
  emitters in Natla's Mines, 7 in Atlantis and 13 in the Great Pyramid.

### 120. Lara never catches fire
*Severity: gameplay / visible · Confidence: confirmed · Fix: medium · Audit:
LVL-12, GFX-16*

- **Retail:**
  - FlameControl (`0603071c`; TR1X `traps/flame.c`). Within 600 units the fire
    does 3 damage a frame. Within 300 it attaches a flame to one of Lara's
    joints, with a counter of -1.
  - That flame burns 5 a frame and loops SFX 150 until Lara is below a water
    surface or dead. The emitter's own flame then pauses for 100 frames.
  - LavaBurn (`060308b0`) puts 10 flames on random joints of her body.
  - Flame effects (0xB2) are created around decomp.c:36110/36179/36224.
- **Ours:**
  - `Flame::update` (`openlara/object.h:1928-1945`) only deals proximity damage,
    7 or 3 a frame, while she's near.
  - The lava floor (`room.h:905-913`) and the lava wedge (`object.h:1734`) just
    kill her, with no flames.
- **Effect:** in Palace Midas (5 emitters), the Great Pyramid (7) and anywhere
  with lava, Lara is never seen burning. In retail, fire is near-certain death
  unless she reaches water; in ours, stepping away stops it.
- **Fix:** an "attached to Lara" flame effect, plus LavaBurn.

### 121. Doors don't push Lara out of the way
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LVL-13*

- **Retail:** DoorCollision (`0600ecc0`) pushes Lara out of the door's bounds,
  with a hit reaction while the door is moving. The Saturn has no "Lara is near
  the door" check; that is a TR1X addition.
- **Ours:** `openlara/object.h:279-282` is a TODO.
- **Effect:** Lara can clip into door panels as they open and close.

### 122. Closed doors don't block in flipped rooms
*Severity: gameplay · Confidence: likely · Fix: medium · Audit: LVL-13*

- **Retail:** DoorControl (`06025e94`) also closes the matching sectors in the
  flipped rooms, at init and in every door update (it writes 4 sectors).
- **Ours:** `openlara/object.h:292` is a TODO, and a door that has never been
  triggered never updates.
- **Effect:** doors in flip rooms don't block when the room is flipped:
  - Lost Valley 41; Qualopec 21, 32, 37; Folly 64
  - Cistern 45, 60, 62, 101, 103; Tihocan 19, 25
  - Khamoon 73–75, 89, 90; Sanctuary 14, 15
  - Natla's Mines 43; 9 doors in Atlantis; Great Pyramid 116

### 123. Doors don't block enemy pathfinding through portal sectors
*Severity: subtle (AI) · Confidence: confirmed · Fix: small · Audit: LVL-13*

- **Retail:** InitialiseDoor (`06025960`; TR1X `objects/door.c`) blocks the box
  of the sector in the room behind the portal.
- **Ours:** `openlara/object.h:303` uses the portal sector's own box, which is
  "no box".
- **Effect:** 68 of the 93 doors sampled are affected, blockable ones included
  (Caves 28; Vilcabamba 8, 15, 22; Folly 1, 86; Atlantis 18, 21, 28, and
  others). Creatures path through closed doors.

### 124. Qualopec and Sanctuary scion pickup: the cinematic is rotated 90°
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LVL-14,
CAM*

- **Retail:** PickUpScionCollision (`0602ac10`, object 143) uses Lara's position
  and yaw as the cinematic position. Only the Atlantis holder scion
  (PickUpScion4Collision `0602ad7c`, object 146) subtracts 90°.
- **Ours:** `openlara/object.h:602` subtracts 90° for both.

### 125. Atlantis scion holder: Lara stands too far back, the scion vanishes, and the holder isn't solid
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LVL-14*

- **Retail:** PickUpScion4Collision (`0602ad7c`).
  - Lara is placed at z -407 (data at `0x06062c8c`).
  - There is no pickup frame: the scion stays in the holder and isn't counted
    as a pickup.
  - The scion holder (object 147) is solid and animates once triggered.
- **Ours:**
  - `openlara/object.h:594` uses -612.
  - `object.h:574-584` removes the scion at frame 44 of the 91-frame grab, adds
    it to the inventory and counts a pickup.
  - The holder has no class (`item.h:1644`), so Lara walks through it, in
    Atlantis and the Pyramid.

### 126. Midas hand differences
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LVL-15,
CAM-16/17*

- **Retail:** MidasCollision (`0602ae90`; TR1X `objects/midas_touch.c`).
  - Turning a lead bar into gold plays the animation with **no** cinematic
    camera.
  - The gold-death cinematic copies Lara's whole position, yaw included.
  - Choosing a wrong item makes no sound.
  - The Midas hand item is drawn as invisible (DrawDummyItem `060130f0`).
- **Ours:**
  - `openlara/object.h:843` starts a cinematic on lead-bar use. It runs the full
    500-frame LEVEL6 camera file, though Lara's animation is only 138 frames.
  - `object.h:808` passes angle 0 for the death cinematic, which is wrong unless
    Lara faces north.
  - `object.h:847` plays "no".
  - `MidasHand` has no draw override, so Palace Midas shows its placeholder model
    (mesh offset 0, 24 faces, the same mesh the camera target uses).

### 127. Several flip effects are missing or wrong
*Severity: audible / visible · Confidence: confirmed · Fix: small · Audit:
LVL-16, SND-12/15*

- **Retail:** the effect table at `0x06060c64` has 16 entries (no flicker); each
  routine is between `06015ac0` and `06015be4`.
- **Ours:** `openlara/level.h:322-418`.
- **Differences:**

  | Effect | Used in | Retail | Ours |
  |---|---|---|---|
  | 7 raising block (`06015bbc`) | Palace Midas, Khamoon | SFX 117 once | missing |
  | 9 drop sand (`06015c70`) | Khamoon | SFX 161 once, then SFX 118 every frame for 4 s, rising from the camera target | missing |
  | 15 chain block (`06015be4`) | Tihocan | SFX 173 (not loaded in that level), then splash SFX 33 at frame 55 | missing |
  | 6 flood (`06015b44`) | Lost Valley, Cistern, Tihocan | SFX 81 every frame for 4 s (121 frames), positioned at Lara's x/z with a height moving from above her to below, so it swells and fades | one positionless loop that never stops (issue 183) |
  | 8 stairs to slope (`06015c34`) | Khamoon, Obelisk, Atlantis | sound at frame 5 | plays immediately |
  | 11 explosion (`06015d44`) | Natla's Mines | camera bounce -75 | 30-frame shake |
  | 5 earthquake (`06015ac0`) | Palace Midas | bounce -250 at 0; sounds at 0, 3, 20, 35, 50, 70; ends at 105 | sounds at 0, 3, 18, 33, 48, 69, extra shakes |

- **Also:** our `FX_UNK1`/`UNK3`/`UNK4` and effect 15 do nothing.

### 128. A flip with no effect cancels a running effect
*Severity: subtle · Confidence: confirmed · Fix: trivial · Audit: LVL-17*

- **Retail:** TestTriggers (`06010350`, end) leaves the running effect alone when
  the flip has none.
- **Ours:** `openlara/room.h:1198` calls `setEffect(FX_NONE)` in that case.

### 129. Anim effect 14 (draw right gun) is missing
*Severity: visible · Confidence: confirmed · Fix: small · Audit: LVL-18*

- **Retail:** LaraDrawRightGun (`06015da4`) swaps the thigh and hand meshes with
  the pistol model's. The CUT_1 Lara uses it in CUT4.
- **Ours:** the case is commented out (`openlara/item.h:415-416`).
- **Effect:** Lara has no gun in her hand in CUT4.

### 130. The Great Pyramid earthquake object is wrong, and carries TR2 behaviour
*Severity: subtle / audible · Confidence: confirmed · Fix: small · Audit:
LVL-20, SND-14*

- **Retail:** EarthQuakeControl (`06026800`). There is a 1/256 draw-RNG chance
  (rand < 0x100) of SFX 147 with bounce -150. Otherwise there is a 1/32
  control-RNG chance (< 0x400) of SFX 70 with a one-frame bounce of +50.
- **Ours:** `openlara/object.h:2164-2189`.
  - It plays SFX 99 (a click, or absent) with a shake of 15 on < 0x400, and SFX
    70 on < 0x800.
  - It also wakes flame emitters and falling ceilings in the room. That is TRX's
    TR2 earthquake (`TRX/src/trx/game/objects/general/earthquake.c`) leaking in.
    No such items are in that room today, so the effect isn't visible yet.

### 131. Underwater switches reach too far and can be pulled again mid-animation
*Severity: subtle · Confidence: confirmed · Fix: trivial · Audit: LVL-21*

- **Retail:** SwitchCollision2 (`0602b1fc`) has a z reach of -1024 to +512
  (data at `0x06062cec`), and the switch must not already be active.
- **Ours:** `openlara/object.h:32-35` allows z up to +1024, and
  `object.h:493-520` has no status check.

### 132. Keyhole and puzzle hole feedback differs
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: LVL-22*

- **Retail:** KeyHoleCollision (`0602b30c`) and PuzzleHoleCollision
  (`0602b53c`) play "no" (SFX 2) once per Lara position when the hole is already
  used or she has no item. They require only the stand state.
- **Ours:**
  - Silent for a used hole (`openlara/object.h:715`).
  - Repeats "no" for as long as Action is held (`object.h:745`).
  - Also requires the plain-stand animation (`object.h:718`).

### 133. An item chosen away from a keyhole stays armed and auto-inserts later
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: INV-7,
LVL-23*

- **Retail:** the collision routine clears g_InvChosen at the end of every frame
  (`DAT_0606277a = 0xFFFF`, near decomp.c:10620; TR1X `collide.c`).
- **Ours:**
  - `onItem` (`openlara/inventory.h:1010-1028`) sets `useSlot`.
  - `useItem` returns false for keys, so `game.h:308-310` never clears it.
  - `Hole::apply` (`object.h:721`) then skips the Action check while `useSlot`
    is set.
- **Effect:** select a key in open space, and the next time Lara stands at any
  keyhole or puzzle hole she uses it without pressing Action. A wrong hole gives
  the "no" sound.
- **Fix:** clear `useSlot` after the frame it was chosen in.

### 134. Pushing and pulling blocks ignores static meshes
*Severity: subtle · Confidence: likely (TR1X TestBlockPush/TestBlockPull;
Saturn not checked) · Fix: small · Audit: LVL-24*

- **Retail:** rejects a push or pull blocked by a static mesh. It tests radius
  500 and height 1000 at the destination, and Lara's own radius at her position.
- **Ours:** `openlara/object.h:1092-1133` has no static test.

### 135. Rolling block extras
*Severity: subtle (no current instance) · Confidence: confirmed · Fix: trivial
· Audit: LVL-25*

- **Ours:** adds a heavy trigger test (`openlara/object.h:1664-1665`) and a push
  collision; retail's rolling block (`060248c0`) has neither.
- **Ours:** `flipBlocks` (`level.h:271`) doesn't move rolling-block floors by
  2048, as retail's flip routine does.

### 136. The Mines cabin isn't solid and sets its flip as one-shot
*Severity: subtle · Confidence: confirmed · Fix: trivial · Audit: LVL-26*

- **Retail:** the cabin (`060264f8`) is solid, and sets flipmap 3 without the
  once flag.
- **Ours:** `openlara/object.h:2111` has no collision, and adds the once flag.

### 137. Slope rounding and static collision radius
*Severity: subtle · Confidence: likely (TR1X GetHeight/GetCeiling; Saturn not
checked) · Fix: small · Audit: LVL-27*

- **Slope rounding:** on one sign of each slant axis (positive for floors,
  negative X and positive Z for ceilings), ours rounds the other way from
  retail. Heights can differ by 1 unit per axis (`openlara/room.h:46-50`,
  `82-83`).
- **Statics:** static-mesh collision gathers nearby rooms with the exact radius
  and height; retail adds 50 to each (`room.h:282`).

### 138. Small trap differences
*Severity: subtle · Confidence: confirmed · Fix: trivial each · Audit: LVL-28*

- The swinging blade doesn't push Lara when inactive (`openlara/object.h:940`).
- The teeth trap spawns 2 blood splats; retail spawns 6.
- The dart ricochet lasts 4 frames; retail's lasts 6.
- The Damocles swords recompute the floor every frame while falling; retail
  uses the floor from init.
- The Thor kill check has an extra height condition.
- Flame emitter teardown doesn't stop SFX 150 (see issue 183).

### 139. The Gym ending waits 90 trigger hits; retail waits 150
*Severity: subtle · Confidence: confirmed · Fix: trivial · Audit: LVL-29,
SND-C8*

- **Retail:** TriggerCDTrack (`06011488`) uses a static counter starting at 150
  (`DAT_06060c14`), never reset.
- **Ours:** `openlara/lara.h:4192-4194` uses `gymTimer > 90`.

---

## Inventory, HUD, saves and game flow

### 141. Several items are posed wrongly when selected
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-2*

- **Retail:** the item structs at `0x06061b40`–`0x06062280` are identical to
  TR1X `invvars.c`. The values below are in our INV_SLOTS terms:
  selRotPre = pt_xrot_sel, selRotX = −x_rot_sel, selRotY = −y_rot_sel,
  selDist = ztrans_sel.

  | Item | Retail (pre, x, y, dist) | Ours |
  |---|---|---|
  | Compass | 4352, 8192, 0, 456 | 3641, 12743, 0, 512 |
  | Shotgun | 3200, 0, 8192, 296 | 3094, 3641, 0, 296 |
  | Lead bar | 3616, 8160, 4096, 352 | Y 67°, dist 256 |
  | Keys / puzzles / scion | 7200, 4352, 0, 256 | 3641, 2731, 0, 296 (scion Y 45°) |
  | Controls | 5504, -1536, 0, 352 | X 67° |

- **Matching:** the pistols, magnums, Uzis, medipacks, passport, detail, sound
  and home items match to within a degree.
- **Ours:** `openlara/inventory.h:110-137`.
- **Fix:** copy retail's values.

### 142. The compass needle never moves, and A closes the whole inventory
*Severity: visible · Confidence: confirmed · Fix: small to medium · Audit:
INV-3*

- **Retail:**
  - DrawInventoryItem (`0601880c`; TR1X `inventry.c`), on compass mesh 1, every
    drawn frame:
    `speed = speed·19/20 + (int16)(−lara.y_rot − item.y_rot − needle)/50;
    needle += speed;`, then RotY(needle).
  - Option_Compass (`060272ec`) ignores A. B plays the compass animation forward
    to frames_total − 1 (frame 24) and returns it to the ring.
- **Ours:**
  - The compass draws only its animation frame
    (`openlara/inventory.h:1371-1396`).
  - `onCompass` (`inventory.h:822-829`) closes the inventory on held A,
    reversing the animation to frame 0.
- **Fix:**
  - Add a needle hook for ITEM_INV_COMPASS node 1 in drawNodesLerp.
    `extraAngles` won't work, because that node has no ROTY flag.
  - Make A do nothing, and B play forward to the end.

### 143. The medipack health bar appears only when you hold more than one
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-4*

- **Retail:** RingNotActive (`06018f8c`; TR1X `invfunc.c`).
  - For medipacks, it always sets the health-bar timer to 40, and draws the HUD
    health bar at its HUD spot (top left).
  - The quantity text shows only when the count is above 1. It is drawn at
    (64, −56), bottom-aligned and centred.
  - The text stays up while the item is selecting or selected.
- **Ours:** `openlara/inventory.h:1398-1418` puts both the count and a centred
  bar, at ((FRAME_WIDTH − 104)/2, 24), inside `state == READY && counts > 1`.
  The text is at (W/2 + 32, H − 32), left-aligned.
- **Effect:** with one medipack no health bar shows, and the bar is in the wrong
  place.

### 144. No ammo counts and no ammo-box items in the ring
*Severity: visible · Confidence: confirmed · Fix: medium (needs issue 52) ·
Audit: INV-5*

- **Retail:** RingNotActive (`06018f8c`) and Inv_AddItem (`060196d8`).
  - The shotgun shows "%5d A" (ammo/6), the magnums "%5d B" and the Uzis
    "%5d C", in small digits at (64, −56).
  - Ammo boxes show count × 2.
  - Ammo picked up without its gun appears as its own ring item: Shotgun Shells,
    Magnum Clips, Uzi Clips. Picking up the gun converts the boxes into ammo.
- **Ours:** the main ring never lists SLOT_AMMO_*
  (`openlara/inventory.h:467-481`), and draws no ammo text.

### 145. The options ring holds the wrong items
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: INV-8,
SND-C10*

- **Retail:** Construct_Inventory (`06018518`), with the list at `0x060623e4`
  (Game, Gamma, Controls, Gym, Sound).
  - **In game** (count 2): Game (the passport) and Gamma.
  - **At the title** (count 4): Game, Gamma, Controls and Gym. Sound and Detail
    Levels are never shown.
  - **Gamma** (Option_Gamma `06027014`): Left/Right steps a global VDP2 colour
    offset (`DAT_060627a0`) by ±8 in −127..127. It auto-repeats every 10
    vblanks, with a slider drawn on the item.
  - **Controls** (Option_Control `0602733c`/`060274b8`, labels `060276fc`):
    Up/Down cycles "Control Method 1/2/3" over a pad diagram of button glyphs
    and labels, with the background dimmed to −128. See issue 29.
  - Retail's sound option (`060271c4`, unused) has two sliders that nothing
    reads.
- **Ours:** `openlara/inventory.h:452-465`, `1262-1278`.
  - In game and on death the ring holds Passport, Controls, Detail ("Graphics")
    and Sound; the title adds Lara's Home.
  - Graphics holds Gamma bar, Show FPS and VSync. Sound holds SFX on/off and
    Music on/off. Controls holds Rumble and Swap A B.
  - On Saturn, Gamma, VSync, Rumble and Swap do nothing. osSetPalette and
    osJoyVibrate are empty stubs (`saturn/os.cpp:95-101`), and controls_swap is
    used only in the GBA input path.
- **Effect:** a different options ring, with dead and debug options.

### 146. Ring and item labels differ
*Severity: visible · Confidence: confirmed ("Gym" likely) · Fix: small · Audit:
INV-9*

- **Retail:**
  - The options ring's title is "OPTION" (`s_OPTION_06060d48`).
  - The photo item's label is "Gym" (string pointer `0x06060f1c`).
  - The death ring's title is "GAME OVER".
  - Retail spells it "Saphire Key".
- **Ours:** "OPTIONS" (`openlara/lang/en.h:51`), "Lara's Home", "Graphics", and
  no title on the death page (getTitleStr, `inventory.h:571-598`).

### 147. Obelisk of Khamoon's puzzle 3 is named "Eye of Horus"
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-10*

- **Retail:** the per-level table at `0x06060fd0` names the Obelisk's puzzle 3
  "Seal of Anubis".
- **Ours:** `openlara/inventory.h:633`,
  `LVLCHECK(LVL_TR1_8B, PUZZLE_3, STR_PUZZLE_HORUS)`. This bug comes from
  upstream OpenLara.

### 148. Menu button roles differ
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: INV-11*

- **Retail:** Input_Update (`060374b4`) and Display_Inventory (`06017420`).
  - A and C both select; B goes back.
  - On the open ring only START closes it. B does nothing in game, and at the
    title it returns to the Press START screen.
  - START is ignored inside an item.
- **Ours:** `openlara/inventory.h:1161-1164`, `1000-1004`.
  - B also closes the ring, and C does nothing.
  - START inside an item backs out.
  - B at the title does nothing.

### 149. The key ring pre-selects the matching key at a keyhole
*Severity: subtle · Confidence: likely · Fix: small · Audit: INV-12*

- **Retail:** opens the ring on index 0 (Inv_RingInit with the current item
  reset).
- **Ours:** `openlara/inventory.h:551-556` jumps to the matching slot.

### 151. Ring motion details
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: INV-14*

- **Retail:**
  - The current item stops spinning while L or R is held.
  - An item that was previously current unwinds at 0x200/0x100 per vblank, even
    after the ring stops.
  - Deselecting an item plays SFX 112.
  - The passport and the "Exit to Title" page close forward, to the last frame.
- **Ours:**
  - The item always spins.
  - The unwind finishes within the 12-tick spin.
  - No deselect sound (`openlara/inventory.h:1194`).
  - Items always close by reversing to frame 0.

### 152. The passport and the save/load model are the GBA ones, not the Saturn's
*Severity: gameplay · Confidence: confirmed · Fix: large · Audit: INV-15,
INV-32*

- **Retail:** Option_Passport (`06026a90`), the load list (`06016a58`),
  CreateSaveGameInfo (`0602c35c`), the prompt and write (`0603c1bc`,
  `0602cb54`), BUP write (`0604605c`).
  - **Pages:**

    | Page | In game | At the title |
    |---|---|---|
    | 0 | Load Game | Load Game |
    | 1 | Restart Level | Start Game |
    | 2 | Exit to Title | (can't be reached) |

  - **There is no Save page.** Page 0 is skipped (forced to page 1) when saving
    is disabled or no save file exists.
  - **Load Game:** pressing A opens a "Select Level" requester (5 lines, width
    272). It lists every level reached so far, from the start-info "available"
    bits, plus "Current Position" after its level. Otherwise it shows "No save
    games!".
  - **Choosing a level** restarts it from its level-start inventory and plays
    its FMV. StartGame (`06016214`) plays FMVs for the Gym, Caves, Folly,
    Khamoon, Mines and Atlantis. Restart Level does the same for the current
    level. "Current Position" plays no FMV.
  - Page titles sit at y −24, with a background box and outline. There are no
    arrow glyphs.
- **Ours:** `openlara/inventory.h:831-905`, `1236-1261`.
  - Pages are Load / Save / Exit, and the in-game passport opens on Save
    (`inventory.h:437`).
  - Load lists only "Caves", "City of Vilcabamba" and "Current Position"
    (`inventory.h:1251-1258`), so anyone can jump straight to level 2.
  - The title passport can't flip; there is no Start Game and no Restart Level.
  - Arrow glyphs are drawn beside the title (`inventory.h:1512-1523`).
- **Related:** saving moves to crystals and the end-of-level prompt (113, 153).

### 153. Saving: retail asks and confirms; ours saves silently and shows GBA error text
*Severity: gameplay · Confidence: confirmed · Fix: medium · Audit: INV-16*

- **Retail:**
  - At a crystal (`0602b940`): "Save Game?" YES/NO, default YES. Success shows
    "Save Complete!" for 60 vblanks. Failure shows "The file could not be saved!
    Try Again? YES/NO". The crystal is used up only on a successful save.
  - After every level's stats screen there is also a "Save Game?" prompt
    (`0602cb54`).
- **Ours:**
  - The passport's Save page writes at once, with no prompt and no confirmation
    (`openlara/inventory.h:879-896`).
  - On failure it shows STR_GBA_SAVE_WARNING_1-3: "No SRAM or FRAM / Saves will
    be lost / after GBA reboot!".

### 154. You can save while dead
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: INV-17*

- **Retail:** there is no save page anywhere.
- **Ours:** the death page allows paging to SAVE_GAME
  (`openlara/inventory.h:843-866`), so `gameSave()` writes a dead Lara over the
  only save.

### 155. Backup RAM handling differs
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: INV-18*

- **Retail:** backup init at boot (`0603b76c`), and the write (`0604605c`).
  - It checks System and Cartridge memory. If both exist, it asks: "MEMORY BACKUP
    / Which type of memory do you want to use? SYSTEM / CARTRIDGE".
  - It shows "not ready" messages, and "insufficient memory … An additional 55
    blocks … Do you still wish to start the game? YES/NO". YES disables saving.
  - It keeps one file: name "TOMBRAIDER_", comment "SAVE_FILE", 0xD7C bytes,
    with a checksum (`0602cce8`) and a verify.
  - Gamma and the control method are not saved.
- **Ours:** `saturn/backup.cpp:53-66`, `saturn/os.cpp:30-77`.
  - Internal memory only, with no boot checks and no messages.
  - Two files: TOMBR_SAVE (its comment is the level's file name) and TOMBR_CFG.
  - `osSaveSettings` rewrites backup RAM on every option toggle.

### 156. The bonus game after finishing is missing
*Severity: gameplay · Confidence: confirmed (decoded) · Fix: medium · Audit:
INV-19*

- **Retail:**
  - The level stats routine (`06016558`) sets the bonus flag (`DAT_06092491`)
    after the Great Pyramid's stats.
  - ModifyStartInfo (`0602c0ec`) then gives all the guns with 1234 ammo.
  - Ammo isn't used up, enemy hit points double (decomp.c:19227), ammo counts
    are hidden, and "BONUS!" is printed.
- **Ours:** none.

### 157. No automatic GAME OVER, the wrong options, and it can be backed out of
*Severity: gameplay · Confidence: confirmed · Fix: small to medium · Audit:
INV-20*

- **Retail:** ControlPhase (`0600f66c`; TR1X `game.c`) and Display_Inventory(4)
  (`06017420`).
  - GAME OVER opens after Lara.death_count passes 300 frames (10 s), or 60
    frames (2 s) plus any button.
  - Its ring is titled "GAME OVER" and holds Game and Gamma. The passport opens
    automatically on Load Game / Restart Level / Exit to Title.
  - B is ignored, so it can't be left.
- **Ours:**
  - Only START opens it (`openlara/lara.h:3937`), at any moment after death,
    with no timer.
  - The ring holds Passport, Controls, Detail and Sound.
  - B returns to the dead game (`inventory.h:1000-1004`, `1161`).
  - There is no Restart Level.

### 158. Health and air bars: position, size and colours
*Severity: visible · Confidence: confirmed (the gouraud decode is likely) · Fix:
small · Audit: INV-21*

- **Retail:** DrawHealthBar (`06015e50`), DrawAirBar (`06015ed8`), bar drawing
  (`0603b524`, `0603b610`), on its 352×224 screen.
  - Top left: the health frame is 104×9 at (32, 28), with the air bar at
    (32, 16) always above it.
  - Fill rows start at x = 34, with width pct − 1 (0..99).
  - A black box with a (5, 5, 5) outline.
  - The fill is base RGB(15, 15, 15) plus gouraud tables:
    - effective health rows (14,10,3) (19,15,6) (14,10,3) (11,7,1) (8,4,0);
    - air rows (6,13,14) (11,17,18) (6,13,14) (2,10,12) (0,8,10).
  - No low-health flashing.
- **Ours:** `openlara/draw.h:979-995`, `saturn/render.cpp:2544-2568`.
  - Top right, at (W − 108, 4). Health drops to y = 14 when the air bar shows.
  - 100 + 2 by 5 + 2.
  - The health rows are about 4 levels darker per channel; the air bar uses a
    different, bluer ramp.

### 159. No HUD ammo counter
*Severity: visible · Confidence: confirmed · Fix: small (after issue 52) ·
Audit: INV-22*

- **Retail:** DrawAmmoInfo (`06015f8c`) and MakeAmmoString (`06015f34`). With a
  non-pistol gun drawn, it prints "%5d A/B/C" in small digits at (256, 64).
- **Ours:** none.

### 160. No pickup notification
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: INV-23*

- **Retail:** AddDisplayPickup (`060161d0`) and DrawPickups (`060160f8`).
  - The picked-up item's sprite appears bottom right, at (W − W/10, H − W/10),
    at sprite scale 0x3000, for 75 game frames.
  - Up to 3 at once, each stepping left by (W/10)·4/3.
  - It's a 2D sprite, not a spinning model.
- **Ours:** nothing (`openlara/object.h:579-664` only adds to the counts).

### 161. The FPS counter is on by default
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-24*

- **Ours:** `gSettings.video_fps = 1` (`openlara/game.h:113`), shown by
  `saturn/debug.cpp:1063`. There is also a Show FPS toggle in the ring.
- **Retail:** none.
- **Decision:** keep it during development?

### 162. No "Press START" title screen, no attract demos, no return to it
*Severity: visible · Confidence: confirmed · Fix: medium (screen), large (demos)
· Audit: INV-25*

- **Retail:** main (`06032d94`), Press START (`060334d0`), StartDemo
  (`06011d48`), ProcessDemoInput (`06011f48`).
  - After boot: USATIT with a static "Press START button" (bottom, y −22) and CD
    track 2.
  - After 600 idle vblanks (10 s) it plays an attract demo, alternating City of
    Vilcabamba and Lost Valley (DEMODATA LEVEL2 and LEVEL3A).
  - A demo uses fixed RNG seeds, Lara with pistols only, a flashing "Press START
    button" and "DEMO PLAY" top left. START or the end of the data quits it.
  - START goes to the title ring on USAOPT. B on the ring, and Exit to Title,
    come back to this screen.
- **Ours:** `saturn/main.cpp:70-78`, `os.cpp:106-111`. Straight from the CAFE
  FMV to the ring. No Press START, no demos, and B does nothing.
- **Needs:** demos need deterministic logic (issue 202).

### 163. No level statistics screen, and the level timer never counts
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: INV-26,
MATH-16, LVL-32*

- **Retail:** the stats screen (`06016558`) and the level-complete sequencer
  (`060163d8`).
  - After every level except the Gym: fade to black, then the USAOPT background.
  - The lines are: the level name; "KILLS n"; "PICKUPS n"; "SECRETS n of m"
    (totals at `0x06060d24`); "TIME TAKEN m:ss" or "h:mm:ss" (timer/30).
  - It waits 4 flips, then for A or C, then shows the Save Game? prompt.
  - For Qualopec, Tihocan, Natla's Mines and Atlantis the stats come after the
    following cutscene.
  - The game timer (`DAT_06092484`) counts every tick in ControlPhase.
- **Ours:** none. `STR_LEVEL_STATS` is unused, `gSaveGame.time` is never
  incremented, and `nextLevel` (`openlara/game.h:15-31`) jumps straight on.

### 164. The PRISON FMV and CUT4 play in the wrong order
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-27*

- **Retail:** the level-complete sequencer (`060163d8`, case 14). After Atlantis:
  PRISON, then CUT4, then the Atlantis stats, then the Great Pyramid.
- **Ours:** `saturn/os.cpp:115-150` and the order in `common.cpp`. Atlantis, then
  CUT4, then PRISON, then the Great Pyramid.

### 165. The ending sequence differs
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-28*

- **Retail:** `060163d8` case 15, and the ending (`06016bac`).
  1. The Great Pyramid's stats, and the save prompt.
  2. END, which can't be skipped.
  3. CRED224 for 900 vblanks (15 s).
  4. CORELOGO, then CAFE, then Press START.

  ENDPG224 is never shown.
- **Ours:** `saturn/os.cpp:132-139`. END, then CRED224 for 8 s, then ENDPG224
  for 8 s, then the ring.

### 166. Every FMV can be skipped, and there are no fades
*Severity: subtle · Confidence: confirmed · Fix: small to medium · Audit:
INV-29*

- **Retail:**
  - The FMV player's third argument (`06043068`) marks CORELOGO and END as
    unskippable; the others skip on START.
  - EIDOS224 shows for 90 vblanks.
  - Pictures fade in over 30 vblanks. Level, FMV and stats transitions fade to
    black over 30 (`0603a19c`, `0603a480`).
- **Ours:** everything can be skipped (`saturn/fmv.cpp:265-270`), EIDOS shows
  for 180 vblanks, and `saturn/screen.cpp` cuts hard everywhere.

### 167. Cutscenes can't be skipped
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-30*

- **Retail:** DoCinematic (`0600d744`) ends on START.
- **Ours:** `openlara/camera.h:43-77` ignores input.

### 168. No A+B+C+START soft reset
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-31*

- **Retail:** the vblank handler (`0603938c`) sets `DAT_06060c00`. Gameplay,
  menus and FMVs then exit to the title.
- **Ours:** none (`saturn/input.cpp`).

### 169. No pause on pad disconnect; only a pad in port 1
*Severity: subtle · Confidence: likely · Fix: small · Audit: INV-32*

- **Retail:**
  - The pad scan (`06043720`) looks for a digital pad (type 0x02 or 0x15),
    through a multitap if one is present.
  - ControlPhase (`0600f66c`) opens the inventory when no pad is present.
- **Ours:** `saturn/input.cpp` reads a digital pad in port 1 only, and does
  nothing when it's missing.

### 170. Retail's level-skip code is missing
*Severity: subtle · Confidence: likely (decoded) · Fix: small · Audit: INV-33*

- **Retail:** the inventory input routine (`06018bf8`) watches the inventory's
  last 8 inputs.
  - The sequence is STEPR, LOOK, STEPR, LOOK, STEPL, STEPL, STEPL, OPTION. With
    Method 1 that is Z Y Z Y X X X START.
  - It then plays SFX 116.
  - Choosing the passport's "Exit to Title" then completes the level instead
    (StartGame `06016214` checks `DAT_060627bc`).
  - The flag clears whenever the inventory opens, so the code must be entered
    with the passport open.
- **Ours:** none.

### 171. The medipack sound plays at the wrong moment and place
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: INV-34,
SND-B5*

- **Retail:** Option_DoInventory (`06026880`) plays SFX 116 when the medipack is
  selected in the ring, even at full health. It is positionless, always-on and
  halved in volume (issue 177).
- **Ours:** plays it only on an actual heal, at Lara's position
  (`openlara/lara.h:3906`).

### 172. Selecting Gamma in game exits the inventory
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: INV-35*

- **Retail:** Display_Inventory (`06017420`, case 8).
  - In game, selecting Gamma exits the inventory.
  - At the title, Controls and Gamma return to the ring.
- **Ours:** there's no Gamma item; see issue 145.

### 173. The ring camera's distance and perspective differ
*Severity: subtle · Confidence: confirmed · Fix: small (with issue 8) · Audit:
INV-36*

- **Retail:** the ring camera is 598 from the ring, at FOV 80° (focal about
  210 px on 352).
- **Ours:** 768 at focal 256.
- **Effect:** our ring is about 5% smaller, with flatter perspective.

---

## Audio

### 177. Retail's per-sample volume halving isn't reproduced
*Severity: subtle to audible · Confidence: confirmed · Fix: small · Audit:
SND-3*

- **Retail:**
  - `DAT_06063218` halves the volume (vol >> 1 before mapping, about −11 dB) for
    SFX 0 variants 0–3, 2, 22, 37, 108, 110, 116 and 147.
  - The halving is applied only at key-on. The per-frame update (`0603c720`)
    re-applies the full volume to positioned sounds.
  - So the halving lasts the whole sound only for sounds with no position: ring
    rotate 108, controls select 110, the medipack 116, and the earthquake rumble
    147.
- **Ours:** not in `sfx.py` or `sound.cpp`.
- **Effect:** the inventory rotate, controls select and medipack sounds are about
  11 dB louder than retail.
- **Fix:** a table in `sfx.py`, and an attenuation bit in the offsets.

### 178. The underwater/surface sound gating doesn't exist
*Severity: audible · Confidence: confirmed · Fix: medium · Audit: SND-4*

- **Retail:** Sound_Effect (`060236e0`; TR1X `sound.c`) returns early when
  `flags != 2 && (flags & 1) != (room[camera.pos.room].flags & 1)`. The callers
  pass:

  | Caller | Flags |
  |---|---|
  | Lara's anim sounds (AnimateLara `06020540`) | 2, always |
  | Other items' anim sounds (AnimateItem `0600f894`) | the item's room flags |
  | Bubbles (`060154c8`) | 1, underwater only |
  | The camera woosh, inventory, passport, medipack | 2 |
  | Everything else, including sound sources and flip effects | 0, only when the camera is dry |

- **There is no muffle filter;** this gating is the whole underwater audio
  effect.
- **Ours:** `soundPlay(id, pos)` has no flags (`openlara/item.h:29`).
- **Effect:**
  - With the camera underwater, retail silences land creatures, guns, traps,
    waterfalls and lava.
  - With the camera dry, retail silences underwater creatures and bubbles.
  - Ours plays all of them all the time.
- **Fix:** add a flags argument plus the camera-room test. The anim command at
  `item.h:361` passes `type == ITEM_LARA ? 2 : room->info->flags`; tag each other
  call site per the table.

### 179. No stereo pan
*Severity: audible · Confidence: confirmed (code; which side is left needs a
listen) · Fix: small to medium · Audit: SND-5*

- **Retail:** Sound_Effect (`060236e0`), UpdateSlotParams (`06023da0`), TL + pan
  (`06046b28`).
  - pan = phd_atan(src.z − lara.z, src.x − lara.x) − (lara.y_rot + torso_y_rot
    + head_y_rot).
  - Pan is forced to 0 when the distance is 0, the position is NULL, or the
    sound info has flag 0x1000.
  - The driver writes DIPAN = (pan >> 11) & 0x1F. `06045a0e` is >> 8, then
    `&0xF8 << 5` in `06046b28`.
  - So 0 to +180° goes 0x00 → 0x0F (centre to full side). Just past 0 on the
    negative side jumps to 0x1F (full other side), returning to 0x10 (centre) at
    −180°. This is a retail quirk.
  - Pan is updated every frame for positioned sounds.
- **Ours:** `sndPlaySample` (`saturn/sound.cpp`) always posts pan 0 (centre),
  which `saturn/sound.68k` writes with send level 7.
- **Effect:** everything is centred in ours; retail pans relative to Lara's
  facing.
- **Fix:** pass the angle to `sndPlaySample`, write DIPAN, and update it per frame
  (needs issue 182).
- **Decision:** reproduce the wrap quirk?

### 180. The volume-to-attenuation curve differs
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-6*

- **Retail:** level to TL (`06046d60`): level = 0xC0 + (vol >> 9), then
  TL = (0x10000 − level²) >> 9.
  - That is TL 0..56, a range of 0 to −21 dB. Half volume gives −11 dB.
  - Volumes 1..511 still play, at −21 dB.
- **Ours:**
  - `openlara/item.h:73-78` drops anything with volume below 512.
  - `gVolumeTL` (`saturn/sound.cpp:52`) is amplitude-linear: half volume is
    −6 dB, and the minimum is −30 to −36 dB.
- **Effect:** retail is quieter at mid distance and louder at the far edge. In
  ours, far sounds fade away completely.
- **Fix:** retail's formula, keep sounds with volume ≥ 1, then the sample
  attenuation (issue 177).

### 181. Random pitch wobble is ±10% in ours and effectively none in retail
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-7*

- **Retail:** Sound_Effect (`060236e0`) and S_SoundPlaySample (`0603c478`). The
  pitch is 90..109, but the driver adds (pitch − 100) >> 1 raw FNS units to the
  base word. That's about −0.25% to +0.4%: inaudible.
- **Ours:** `openlara/item.h:80-84` gives 115..140 / 128. `pitchOctFns` turns
  that into ±10%, applied to every sound flagged P: footsteps, gunshots and most
  creatures.
- **Effect:** repeated sounds vary in pitch in ours; retail's repeats are
  identical.
- **Fix:** drop it, or reproduce the FNS offset.

### 182. The sound-slot model doesn't match retail
*Severity: subtle to audible · Confidence: confirmed · Fix: medium to large ·
Audit: SND-8*

- **Retail:** Sound_GetSlot (`06023af4`), the slot loop (`06023c7c`),
  UpdateSlotParams (`06023da0`).
  - 24 slots of 20 bytes (`DAT_06078cc8`). The first 8 are reserved, one per
    looped (mode 2) sound (`DAT_060723b8`).
  - A slot is keyed by (sound id, position pointer).
  - Modes:
    - WAIT (0): if the same id and position is still playing, do nothing.
    - RESTART (1): restart it.
    - Mode 3: never plays.
  - Every frame, volume and pan are recomputed from the stored position pointer
    for positioned sounds still playing, and finished slots are freed.
  - Up to 32 hardware voices.
- **Ours:** `sndPlaySample` (`saturn/sound.cpp`), the sound CPU's voices
  (`saturn/sound.68k`).
  - Keyed by sample index only. Two emitters of the same sound share one voice:
    for WAIT and LOOP the second call only rewrites the volume, and for REPLAY it
    cuts the first.
  - Two variants of one WAIT sound can overlap.
  - Volume is never updated after key-on (only when the sound is played again),
    and there is no pan.
  - 8 voices. A new sound is dropped when they're all busy, or when decoding it
    would take the sound CPU past its budget (`DECODE_BUDGET`) and every voice
    decoding is as loud or louder; otherwise the quietest decoding voice gives
    way. Retail's 24 slots never decode anything.
- **Effect:** two wolves, darts or gunmen collapse into one sound. Moving sources
  (boulders, running creatures) don't fade or pan as they move.
- **Fix:** port the slot table into `soundPlay` and `sound.cpp`.

### 183. Looped sounds never stop
*Severity: audible · Confidence: confirmed · Fix: medium · Audit: SND-9*

- **Retail:**
  - The looped (mode 2) sounds are 79, 80, 81, 118, 148, 150, 154 and 155.
  - Every frame, SoundEffects (`06014fbc`) resets the ambient loudness. Sources
    and flip effects then call Sound_Effect, and the nearest emitter sets the
    volume and pan.
  - A slot nobody played that frame is stopped (`06023c7c`).
  - FlipMap (`060112a8`) and the inventory call StopAmbientSounds (`06023c00`).
  - The flood (`06015b44`) is issue 127.
  - The flame (`06030670`/`0603071c`) calls StopEffect(150) when it goes out.
- **Ours:**
  - `openlara/level.h:353` plays FX_FLOOD as one positionless LOOP call that is
    never stopped. In Lost Valley, the Cistern and Tihocan the full-volume roar
    continues until the level ends.
  - `object.h:1936` loops 150 for the Flame, and FlameEmitter
    (`object.h:1960`) removes the flame with no `soundStop`, so the loop runs
    on.
  - The last emitter called sets the volume, not the nearest. Going out of range
    freezes a loop at its last volume.
- **Fix:** ambient-slot logic, plus a per-frame "not played means stop" rule.
- **Retail quirks to decide on (likely):**
  - Item-driven loops (fire) are reset in the same frame they're played, because
    items run before SoundEffects. So retail restarts them every frame, which
    stutters.
  - Looped voices are keyed with 0x1830 (`06046c5c`): LPCTL 1 plus **PCM8B**, on
    data loaded as 16-bit (LEA = bytes/2 − 2). So retail's loops play about an
    octave lower, over the first half of the data.

### 184. Room sound sources are never played
*Severity: audible · Confidence: confirmed · Fix: medium (needs 183) · Audit:
SND-10*

- **Retail:** SoundEffects (`06014fbc`) plays every room sound source each frame
  (`DAT_06093a20`, count `DAT_06074c64`), with SPM 0. It uses flag 0x80 when the
  room is unflipped and 0x40 when flipped. The data:

  | Level | Sources |
  |---|---|
  | Lost Valley (LEVEL3A) | 10 × SFX 80 |
  | Qualopec (LEVEL3B) | 1 × SFX 79 |
  | Tihocan (LEVEL7B) | 1 × SFX 80 |
  | Natla's Mines (LEVEL10A) | 12 × SFX 79 |
  | Atlantis (LEVEL10B) | 38 × SFX 148 (flags 0xC0) |
  | Great Pyramid (LEVEL10C) | 8 × SFX 148 |

- **Ours:** the sources are packed (`tools/pack_level.py:694`) and loaded
  (`openlara/fmt/phd.h:560`), but never used.
- **Effect:** the waterfalls in Lost Valley, Qualopec, Tihocan and the Mines, and
  the lava in Atlantis and the Pyramid, are silent.
- **Fix:** a per-frame loop after the camera update.

### 185. Missing sound calls
*Severity: audible · Confidence: confirmed · Fix: small each · Audit: SND-12*

| Sound | Retail | Ours |
|---|---|---|
| Splash on water entry (33) | Splash `060156e8` | `fxSplash` has no sound (`openlara/item.h:695`) |
| Block landing thud (70), with floor shake | MovableBlockControl `0602409c` | TODO at `object.h:1228`; see issue 4 |
| Lara hit by an enemy bullet (50) | GunHit `06028778` | `enemy.h:2257` `bite()` is silent; see issue 65 |
| Natla's shot (123) | `0602500c` | `SND_NATLA_SHOT` never used; see issue 85 |
| Missile impacts (10 / 104 / 50 / 31) | `060316a0` | missing, because shots are hitscan; see issue 84 |
| Scion destruction (104 every 10 frames for 3 s) | `06026640` | missing; see issue 2 |
| Body-part explosions (104) | `0603202c` / `0603255c` | missing; see issue 213 |
| Lava fountain (149, every frame) | TR1X `lava.c` | missing; see issue 119 |
| Lara burning (looped 150 on her joints) | `0603071c` | only damage. `room.h:911` plays 149 once on lava death instead; see issue 120 |
| Raising block (117) | `06015bbc` | missing; see issue 127 |
| Drop sand (161, then a moving 118 for 4 s) | `06015c70` | missing; see issue 127 |
| Chain block (173 at frame 0, then 33 at frame 55) | `06015be4` | missing; see issue 127 |
| Inventory deselect (112) | Display_Inventory `06017420`, case 10 | silent (`inventory.h:1194`) |
| Underwater bubbles (37) | `060154c8` | none (`item.h:403-406`) |

### 186. Exploding deaths play a sound in ours; retail is silent
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-13*

- **Retail:** the Torso, the centaur and the mutant (`06024b34`, `06030cf8`,
  `06030ffc`) call SFX **172**, not TR1X's 171.
  - 172 isn't in LEVEL8C, 10B or 10C, so retail is silent there.
  - In LEVEL7B, 172 is the placeholder click.
- **Ours:** `openlara/enemy.h:2868`, `3043` and `3321` play 171.
- **Decision:** keep retail's silence, or keep 171?

### 187. The Caves-area levels play track 5 at load and loop it forever
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-14*

- **Retail:** no level-start track. The only CD plays are:
  - floor triggers (TriggerCDTrack `06011488`/`060115dc`);
  - the secret jingle;
  - Natla and the skater;
  - cutscenes;
  - the title track, looped.

  Track 5, a 2-minute cue, plays only from its floor triggers (LEVEL1 0:3:1 and
  others), once.
- **Ours:**
  - `openlara/common.cpp:46-50` sets `TRACK_TR1_CAVES` (5) for LEVEL1, 2, 3A
    and 3B, and `game.h:258` plays it at load.
  - `game.h:294-297` restarts it whenever the CD is idle, including after every
    triggered track and during the inventory.
- **Fix:** use `TRACK_NONE`, and auto-restart only at the title.

### 188. Opening the inventory doesn't stop sound effects or pause the music
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-15,
INV-41*

- **Retail:** Display_Inventory (`06017420`) calls StopAmbientSounds and
  StopAllSamples (`0603c6d4`), then S_CDPause (`0603c7f4`) unless it's in title
  mode. On exit, ControlPhase calls the CD resume (`0603c80c`), which resumes at
  level 0xA0.
- **Ours:** `openlara/inventory.h:520` does neither.
- **Fix:** add a CDC pause and resume to `saturn/cd.cpp`.

### 189. Music keeps playing after Lara dies
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-16*

- **Retail:** LaraControl (`06020144`) calls S_CDStop (`0603c77c`) when Lara's hit
  points first drop below 1 (TR1X `laramisc.c:161`).
- **Ours:** no stop anywhere in `openlara/lara.h`.

### 190. Loading a save replays the saved track from the start
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-17*

- **Retail:** plays no CD on load; it's silent until the next trigger.
- **Ours:** `openlara/game.h:37` saves `gCurTrack`, even track 13 or 5, and
  `game.h:92` replays it from the start.

### 191. Cutscene audio isn't synced, and CUT4 restarts its track
*Severity: audible · Confidence: confirmed (CUT4), likely (desync) · Fix: small
· Audit: SND-18*

- **Retail:** the cutscene start (`0600d574`).
  - StartSyncedAudio (`0603c794`) starts the track and waits until the drive
    reports PLAY.
  - It then sets `DAT_06062ad0 = 0`, muting all sound effects. That's inaudible
    in practice: the Saturn CUT levels have no usable samples.
  - The CD is never restarted.
- **Ours:**
  - Starts the track immediately, without waiting (`openlara/game.h:258`), so
    the audio lags by the seek time. See also issue 201.
  - CUT4 runs 66.7 s against the 63.5 s of track 22, so `game.h:294` restarts
    the track for the last 3 s or so.

### 192. Natla's track 54 and the skater's track 56 are missing
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-19*

- **Retail:**
  - Natla (`0602500c`) plays track 54 when her semi-death timer reaches 480
    frames.
  - The skater (`06029ac0`) plays track 56 when his hit points drop below 120 and
    the current track isn't 56, and sets the current track.
- **Ours:** `openlara/enemy.h:3204` and the skater have no calls.

### 193. Non-one-shot music triggers replay when Lara re-enters them
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: SND-20*

- **Retail:**
  - The current track (`DAT_06060bf0`) is reset only at level init (`0602ce48`),
    not when a track stops or ends. So the same trigger doesn't play its track
    again until a different track has played.
  - The secret jingle, S_CDPlay(13), doesn't set the current track.
- **Ours:**
  - `gCurTrack` resets to −1 when the CD ends or stops (`openlara/game.h:295`,
    `saturn/sound.cpp:234`).
  - So non-oneshot triggers replay on re-entry: LEVEL1 track 9, LEVEL4 track 7,
    LEVEL8B tracks 3, 4 and 5, track 8 in LEVEL10B and 10C, and LEVEL3B track 17
    after its antipad stop.
  - Separately, `sndPlayTrack(13)` is ignored while 13 is still playing.

### 194. Retail CD music is mono (left channel only); ours is stereo
*Severity: audible · Confidence: confirmed (code) · Fix: small, if wanted ·
Audit: SND-C9*

- **Retail:** the CD send level (`06046d2c`).
  - Slot 16's send register is written as 0xB0 (EFSDL 5, centred).
  - The right-channel write goes to `0x25B00226`, slot 17's LEA register, by
    mistake. SCSP init (`06046192`) zeroes slot 17, so the right channel is never
    mixed.
  - Also, retail's voice search uses slots 0–31, so a 17th sound effect landing
    on slot 16 zeroes its send level and mutes the music until the next CD play.
- **Ours:** `saturn/sound.cpp:101-102` mixes left and right separately.
- **Decision:** this is a retail driver bug. The recommendation is to keep our
  stereo.

---

## Numeric primitives and timing

### 196. The projection divide table: vertex jitter, and it saves nothing on the CPUs (reported)
*Severity: visible · Confidence: confirmed (error measured; cycle counts from
the asm) · Fix: medium · Audit: MATH-2*

- **Retail:** three hardware divides per vertex (`0604127c` meshes, `06041a80`
  rooms).
  1. zv/persp is started, then overlapped with the x dot product (about 14
     instructions). Reading the quotient stalls about 23 cycles.
  2. x/zp overlaps the y dot product, stalling about 22 cycles.
  3. y/zp is read 2 instructions after it starts, stalling about 36 cycles.

  That is about 80 stall cycles out of about 170 per vertex. The results are
  effectively exact: zp keeps 14 fraction bits, and division truncates toward
  zero.
- **Ours:** `d = divTable[z >> 4]`, i.e. 65536/i truncated to 16 bits
  (DIV_TABLE_SIZE 1281, `openlara/common.h:170`, `573-581`), then
  `(x·d) >> 12`. The lookup costs about 6 cycles. Used in:
  - `projectView`, and `saturn/render.cpp:320`, `1523`
  - `xform.s`, `meshxform.s`
  - `project.dsp`; `rooms.cpp:531` builds its table
- **Error** against an exact integer reference, at the same focal length, for a
  vertex 176 px off centre:

  | z (world units) | Max error | Mean error |
  |---|---|---|
  | 80–128 | 34–35 px | about 14 px |
  | 128–256 | 21 px | 7.5–8.3 px |
  | 256–512 | 11 px | 3.6–4.5 px |
  | 512–1024 | 6 px | 1.8–2.7 px |
  | 1024–2048 | 3–4 px | 0.9–1.8 px |
  | 2048–8192 | 2–3 px | 0.4–1.3 px |
  | 8192–20480 | 3–4 px | 0.6–1.4 px |

  - Near the camera the error comes from z >> 4 quantisation. Far away it comes
    from the truncated d, which is only 51–128 at those depths (steps of up to
    2%).
  - Left of and above centre get an extra 1 px bias (floor against truncate).
  - Vertices jump by these steps as z crosses multiples of 16, so walls and
    floors right next to the camera wobble by 10–35 px.
- **Cost:**
  - Against retail's 3-divide scheme, the table saves about 75 cycles per vertex
    (about 2.6 µs at 28.6 MHz).
  - Against a **single** exact reciprocal per vertex, it saves nothing. Start
    DVSR/DVDNT right after z, as retail does, and read it after the roughly 30
    instructions of x/y work. That costs about 3–10 cycles per vertex, roughly
    0.2–0.6 ms per frame at 2000 vertices. It needs a 32-bit multiply
    (`dmuls.l`) for z below about 256.
  - The DSP has no divider, so `project.dsp` needs a table regardless.
- **Fix:**
  - CPU: one overlapped reciprocal divide per vertex.
  - DSP: a finer table indexed by z >> 2, with 32-bit 2^28/i entries. It is
    fetched by DMA either way, so the cost per vertex is the same. At 20 KB it is
    too big for the CPU cache, which is why the CPUs should use the divider.
  - The crunch lab measured "exact divide" at corner p99 1.93 → 1.31 px and −3%
    flicker at the ledge.

### 197. Other approximate divides where retail divides exactly
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: MATH-3*

- **Retail:**
  - GetBoundsAccurate (`06014e84`) computes a + (b − a)·frac/rate with the
    hardware divide (`060457f6`).
  - InterpolateMatrix (`06014a60`) divides exactly.
  - Move3DPosTo3DPos (`0600f504`) computes x·16/dist.
- **Ours:**

  | Code | Approximation | Error |
  |---|---|---|
  | interpolated collision bounds (`openlara/item.h:199`) | `(b − a)·(frac·(65536/rate)) >> 16` | ±1 unit in 25–50% of cases, always ≤ retail |
  | root offset lerp for drawing (`draw.h:343`, `559`) | same | same |
  | `matrixLerp`, for rates other than 2 or 4 (`common.cpp:1234`) | an 8-bit weight | about 0.4% of the keyframe difference |
  | alignment step (`item.h:536-544`) | 16·d / (sqrt >> 1) via FixedInvU; snaps at dist² ≤ 256, where retail snaps at isqrt ≤ 16 | steps up to 6–12% longer, so alignments to switches and pickups finish ±1 tick from retail |

  `FixedInvS` is unused.
- **Fix:** exact divides; it's a handful of calls per item per tick.

### 198. Animation interpolation lerps relative matrices, not two full stacks
*Severity: subtle (drawing only) · Confidence: confirmed · Fix: medium · Audit:
MATH-4*

- **Retail:** keeps two matrix stacks, one for keyframe A and one for B, built
  with phd_TranslateRel_I (`060148e0`), phd_RotYXZpack_I (`060149dc`) and
  phd_PushMatrix_I/PopMatrix_I (`060147dc`/`060147bc`). Each node's absolute
  matrix is lerped exactly: averaged for rate 2, (b − a)·frac/rate otherwise
  (InterpolateMatrix `06014a60`).
- **Ours:** `matrixFrameLerp` (`openlara/common.cpp:1428`) rotates A and B on the
  already-lerped parent, then lerps. The result is lerp(P·RA, P·RB) instead of
  lerp(PA·RA, PB·RB).
- **Error:** second order, t(1 − t)(PA − PB)(RA − RB). That is roughly 1% of bone
  length when both the parent and the child move about 11° per keyframe: about 3
  units on a 300-unit limb. Retail's shrinking of limb ends on fast mid-frames
  isn't reproduced either.
- **Fix:** add a second stack. It costs roughly the same, since we already rotate
  twice per node.

### 199. Our sin/cos lookup rounds the angle the wrong way in mirrored quadrants
*Severity: subtle · Confidence: confirmed (exhaustive over all 65536 angles) ·
Fix: small · Audit: MATH-5*

- **Retail:** phd_sin (`0603e0b0`) and phd_cos (`0603e0a8`), with helpers
  `0603e104`/`0603e134` and the table at `0x060659C0`, identical to TR1X's. The
  angle is folded into 0–90° before >> 4, which amounts to using ceil(a/16) in
  the mirrored quadrants.
- **Ours:** `gSinCosTable[a >> 4]` (`openlara/common.h:2771-2777`) always rounds
  down. Values at multiples of 16 match exactly.
- **Error:**
  - For 92.6% of angles in every quadrant, one of the two values is one table
    step off: sin in quadrants 1 and 3, cos in quadrants 0 and 2. That is at most
    26/16384, about 0.082°.
  - In per-tick movement the integer step differs on 2.7% of headings at speed
    30, 4.6% at 50 and 9.2% at 100.
  - So positions drift slowly and deterministically (Lara, enemies,
    projectiles), and matrices differ slightly.
- **Fix:** read sin and cos as two 16-bit lookups, with the quadrant-dependent
  index ((a + 15) >> 4 in the mirrored quadrant). About 1–2 extra cycles.

### 200. The catch-up cap is 10 ticks; retail's is 10 vblanks (5 ticks)
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: MATH-8*

- **Retail:** ControlPhase (`0600f66c`).
  - nframes is counted in vblanks and capped at 10. Each call adds
    nframes·0x8000 to frame_count (`DAT_06060c10`), and one tick runs per
    0x10000. So at most 5 ticks run per displayed frame, and below 6 fps the game
    goes into slow motion.
  - Input is read per tick (`060374b4`).
  - Each tick runs items, effects, LaraControl (`06020144`), CalculateCamera
    (`0600d128`) and Sound_UpdateEffects (`06014fbc`), increments the timer
    (`DAT_06092484`), and counts the health bar down (`DAT_0606280c`).
  - nframes comes from S_DumpScreen (`06037a8c`). After waiting for the flip
    (`06005e1e`), it returns the 60 Hz count (`DAT_06063888`), which the
    interrupt handler (`0603938c`) increments.
- **Ours:** `delta = min(ticks, MAX_UPDATE_FRAMES)`, which is 10 ticks
  (`saturn/main.cpp:119`, `openlara/common.h:469`, `game.h:290`).
- **Effect:** none at 10 fps or above; at 20 fps both alternate 1 and 2 ticks.
  Between 3 and 6 fps ours runs up to twice as many ticks per frame as retail.
- **Fix:** cap at 5.

### 201. After every level load, save load or cutscene start, 10 ticks run at once
*Severity: visible, and gameplay at load points · Confidence: confirmed (the
jump), likely (the audio lead) · Fix: small · Audit: MATH-9*

- **Retail:** GameLoop (`0601636c`) starts with nframes = 1. The level-start
  routine (`0603ab38`) zeroes the vblank counter (`DAT_06063888`) after loading.
- **Ours:**
  - `startLevel` and `gameLoad` run inside `gameUpdate`
    (`openlara/game.h:323-333`). `lastTick` was set before the multi-second CD
    load, so the next frame gets delta = 10 (`saturn/main.cpp:119`).
  - So 0.33 s of game time passes in one displayed frame: enemies move, Lara
    starts moving, and a cutscene skips 10 camera frames.
  - `sndPlayTrack` starts the cutscene's audio at the end of the load, so the
    picture probably leads the audio by about 8 ticks (0.27 s) for the whole
    cutscene.
- **Fix:** re-sync `lastTick = gVblankCount >> 1` after any load.

### 202. RNG seeds: every level start has the same seed, and saves restore it
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: MATH-10*

- **Retail:**
  - Both streams use the same LCG: seed·0x41C64E6D + 0x3039, output
    (seed >> 10) & 0x7FFF. Random_GetControl is `060168f8` (seed
    `DAT_06060d34`) and Random_GetDraw is `06016940` (seed `DAT_06060d38`).
  - Both seeds are 0xD371F947 at power-on, and are reseeded only by StartDemo
    (`06011d48`).
  - The RNG state carries across levels and isn't restored by loading a save.
- **Ours:**
  - Both seeds are set from `osGetSystemTimeMS()·3` and `·7` at every
    `startLevel` (`openlara/game.h:248-249`). That function returns 0
    (`saturn/os.cpp:23-26`), so both seeds are 0 at every level start.
  - The seeds are saved and restored with saves (`game.h:38-39`, `95-96`).
- **Effect:** every level start plays out with the same random decisions, and
  reloading a save reproduces the same outcomes; retail's vary.
- **Stream deviations found:**
  - The enemy-miss ricochet uses the logic stream (`enemy.h:2261-2263`); retail
    uses the draw stream.
  - Camera shake uses no RNG at all (issue 17).
  - `RAND_LOGIC` uses >> 15 where retail uses /0x7FFF at a few sites, which is
    negligible.
- **Fix:** seed 0xD371F947 once at boot, don't reseed per level, and don't save
  the seeds. Demos (issue 162) need this.

### 203. anglesFromVector lacks retail's overflow guard
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: MATH-11*

- **Retail:** phd_GetVectorAngles (`0603c9ec`) shifts x, y and z right by 2
  until each fits int16, then sign-corrects the pitch.
- **Ours:** `phd_atan(phd_sqrt(x·x + z·z), −y)` (`openlara/common.cpp:1068-1072`).
- **Error:**
  - The pitch is 1 angle unit lower in 74% of random cases (0.0055°,
    negligible).
  - For horizontal lengths over 65535 the square wraps and the pitch is wrong.
    For example, (50000, 1000, 50000) gives −393 instead of −143. That isn't
    reachable in normal play.

### 204. getFrames has an extra no-interpolation branch
*Severity: subtle · Confidence: likely · Fix: small · Audit: MATH-12*

- **Retail:** GetFrames (`06014dc4`) always interpolates toward the next keyframe,
  and only adjusts the rate.
- **Ours:** also sets `indexB = indexA` when `indexB·rate >= frameEnd`
  (`openlara/item.h:157-160`), which turns interpolation off.
- **Effect:** only animations whose frameBegin is at most the rate (an object's
  first anims) lose interpolation on their last partial segment.

---

## Scene look: lighting and effects

### 206. Item and effect sprites fade in the fog; retail's don't
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GFX-2*

- **Retail:**
  - S_DrawSprite (`0x06038aac`) builds the sprite's gouraud as grey
    31 − (shade >> 8), from the passed shade only, with no depth term.
  - DrawSpriteItem (`0x060130ac`) passes item->shade; DrawEffect
    (`0x06012f94`) passes 0x1000.
  - Room sprites do fog, but only through their room vertex. TR1X's
    Output_DrawSprite fogs; the Saturn wins.
- **Ours:** `saturn/render.cpp:2445` adds z − FOG_MIN to every sprite.
- **Effect:** pickups (medipacks, ammo, keys), blood, splashes, ricochets, bubbles
  and flames beyond 12 sectors darken toward black by 20. Retail keeps them at
  full shade. It shows in big rooms: Lost Valley, the Colosseum, Atlantis.
- **Fix:** skip the fog for non-room sprites.

### 207. Room sprites stay drawn right up to the camera; retail drops them within 640 units
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GFX-3*

- **Retail:** the room sprite routine (`0x0603feda`) returns when z ≤ near << 3,
  i.e. 8 × 80 = 640 units (asm `0x0603fee8`–`0x0603fef4`). It also drops a sprite
  whose anchor vertex is flagged far off-screen (clip & 0x0C), or near, far or
  behind (& 0x7F in the face dispatch at `0x0603fb48`).
- **Ours:** `openlara/draw.h:830` goes through `renderSprite`, which culls only
  below 64 units (`saturn/render.cpp:2417`).
- **Effect:** plant and foliage sprites in Caves (43 of them), Vilcabamba (81)
  and Lost Valley (30) vanish on retail when the camera comes within about 0.6
  sectors. Ours keep drawing them, huge, in front of the camera.
- **Fix:** add z ≤ 640 to the room-sprite path.

### 208. Shadow sizes, and which objects cast them, differ from retail
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GFX-4*

- **Retail:** shadow_size is OBJECT_INFO +0x2e (object table at `0x06089be0`,
  stride 0x34), set around decomp.c:33618–34028:

  | Size | Objects |
  |---|---|
  | 160 | Lara, evil Lara |
  | 128 | wolf, bear, bat, rat, water rat, lion, lioness, puma, gorilla, raptor, Larson, Pierre, skater, cowboy, Kold, Natla, pendulum, darts |
  | 85 | crocodile, alligator, winged mutant, centaur, the torso (Adam) |
  | 64 | T-rex |
  | 256 | Damocles sword |
  | none | mummy, skateboard, cutscene actors O_PLAYER_1–4 |

- **Ours:** `openlara/draw.h:737` uses `drawShadow(item, 160)` for every item
  flagged with a shadow (`enemy.h:26`, `object.h:382`, `935`, `976`,
  `lara.h:2741`).
  - Every enemy gets 160, and so do cutscene actors (`object.h:382`) and
    mummies.
  - TrapSword (the Damocles sword) has no shadow.
  - The doppelganger has her shadow removed (`enemy.h:882`).
  - The skateboard is drawn through `Gunman::draw` (`enemy.h:2577-2596`), so the
    skater casts a second shadow.
- **Effect:**
  - Enemy shadows are 25% too big; crocodiles, centaurs and mutants nearly 2×;
    the T-rex 2.5×.
  - Shadows appear in cutscenes and under mummies.
  - The Folly's 19 swords show no 256-size shadow. In retail that shadow tells
    the player where a sword will fall.
- **Fix:** a per-type shadow-size table that also drives the flag.

### 209. Shadows disappear when their owner is off-screen
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: GFX-5*

- **Retail:** DrawAnimatingItem draws the shadow (the call near decomp.c:14064)
  before S_GetObjectBounds; DrawLara does the same (near 14247).
- **Ours:** `drawModel` returns at `sphereIsVisible` (`openlara/draw.h:704`)
  before reaching the shadow at line 737.
- **Effect:** a shadow vanishes while its owner (Lara above the frame, an enemy
  just off-screen) is out of view, even though the floor under it is in view.

### 210. Hatched creatures and Thor's hammer head are lit flat
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GFX-6*

- **Retail:** pod and statue creatures get shade −1 (+0x2a = 0xFFFF, near
  decomp.c:37401 and 37493). The hammer head copies the handle's shade (near
  decomp.c:26116); in LEVEL4 the handle's shade is −1. TR1X: `pod.c:68`,
  `statue.c:41`, `thors_hammer.c:36`.
- **Ours:** `spawnHidden` and `HammerHandle` use `ItemObj::add`
  (`openlara/object.h:1980`, `1775`), which sets intensity = 128
  (`item.h:577`). That gives static 4096: flat, with no directional shading.
- **Effect:** mutants and Adam from the Atlantis pods, and centaurs from the
  Tihocan statues and pods, are an even mid-grey that ignores the room's lights.
  So is the hammer head in the Folly's Thor room.
- **Fix:** set intensity = 0 for these spawns.

### 211. Distant objects fade fully to black; retail keeps the lit side faintly visible
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: GFX-7*

- **Retail:** Output_CalculateLight (`060388ec`) clamps the light adder to 0x1FFF
  after adding fog (asm `0x06038a12`–`0x06038a22`). The per-vertex shade is then
  clamped to 0..0x1FFF, so at full fog the lit side stays at 0x1FFF − range.
- **Ours:** `openlara/draw.h:133` caps at 8191 + gLightRange, with the comment
  "the fog darkens the side facing the light to black too". That describes a
  deviation from retail.
- **Effect:** enemies and Lara 15–20 sectors away go fully black in ours. On
  retail their lit side stays a dim grey.
- **Fix:** clamp to 8191.

### 212. The Atlantis doppelganger is drawn as a plain animated item
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: GFX-8*

- **Retail:** DrawEvilLara (`0x060210b8`; TR1X `evil_lara.c`) swaps the 15 Lara
  mesh pointers (`0x06094490`) for O_EVIL_LARA's and calls DrawLara
  (`0x06013438`). So she copies Lara's:
  - aiming arms and gun pose;
  - muzzle flashes;
  - head and torso turn;
  - hit flinch;
  - 160 shadow.
- **Ours:** `Doppelganger` (`openlara/enemy.h:875-950`) goes through `drawModel`
  and `drawNodesLerp`. Her arms just follow the body animation, and she has no
  shadow.
- **Effect:** when Lara draws and fires, her double keeps her arms down: no aim,
  no flash, no shadow.
- **Fix:** draw her with the `drawLaraNodes` path, using her own meshes.

### 213. Exploding deaths are missing; bodies just vanish
*Severity: visible · Confidence: confirmed · Fix: large · Audit: GFX-9*

- **Retail:** ExplodingDeath (`06031c64`; TR1X `effects/body_part.c`) creates
  O_BODY_PART effects (0xA8, near decomp.c:37194 and 37266).
  - The model's meshes break off as flying, bouncing parts. Parts that carry
    damage become explosion sprites when they hit the floor or Lara.
  - It is used for mutants (`mutant.c:76`), centaurs (`centaur.c:138`) and the
    torso (`abortion.c:219`).
  - Centaur statues burst entirely (`statue.c:67`).
  - Pods throw shell meshes 9–23 and keep 0x1FF (`pod.c:107`).
- **Ours:**
  - Mutants turn invisible with one blood sprite
    (`openlara/enemy.h:2869-2872`).
  - The centaur and Adam turn invisible at the end of the animation
    (`enemy.h:3043-3045`, `3319-3325`).
  - The statue turns invisible instantly (`object.h:2017-2023`).
  - The pod keeps all 24 meshes drawn (issue 116).
- **Effect:** across Atlantis, Tihocan, Sanctuary and the Great Pyramid, bodies
  blink out instead of bursting.
- **Used by:** 86, 87, 88, 115, 116.

### 214. Midas twinkles are missing
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GFX-10,
LARA-23*

- **Retail:** ItemSparkle (`06015380`), called from AsUseMidas and AsDieMidas
  (`0601be54`, `0601be78`; TR1X `lara.c:730`, `826`). It spawns O_TWINKLE sprites
  on Lara's hands while she uses the Midas hand, and over each gilded mesh during
  the gold death. Twinkle effects (0xA5) are created near decomp.c:15374/15462.
- **Ours:** the STATE_USE_MIDAS and STATE_DEATH_MIDAS handlers
  (`openlara/lara.h:1248-1281`) do the mesh swap but spawn no twinkles. SPARKLES
  is set up but never spawned.
- **Effect:** Palace Midas's lead-bar conversion and the gold death have no
  sparkle.

### 215. No fade from black after loading a level, and no fade at level end
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GFX-11*

- **Retail:**
  - The level render init (`0x060373fc`) fades colour offset A to the brightness
    setting over 30 frames, via `0x060391a0` with a per-vblank step at
    `0x06039264`.
  - The level-end stats screen (`0x06016558`) first fades to −255 over 30
    frames.
- **Ours:** `gBrightness` is never set to anything but 0
  (`openlara/level.h:238-262`), and VDP2_CLOFEN is cleared
  (`saturn/system.cpp:264`).
- **Effect:** every level cuts straight in at full brightness, and cuts out at
  the end.
- **Fix:** colour offset is already in VDP2.

### 216. The flickering-lights effect is off by 1–3 frames
*Severity: subtle · Confidence: confirmed against TR1X (Saturn not checked) ·
Fix: small · Audit: GFX-12*

- **Retail:** TR1X `effects/flicker.c` flips the room at timer values 90, 92, 105
  and 107, and again once the timer passes 125.
- **Ours:** `openlara/level.h:380` uses {90, 93, 105, 108, 123}.
- **Effect:** the second and fourth darks last 3 frames instead of 2, and the
  lights come back on 3 frames early.

### 217. Item sprites are culled to the room's portal window and depth-biased
*Severity: subtle · Confidence: likely · Fix: small · Audit: GFX-13*

- **Retail:** S_DrawSprite tests a sprite only against the full 352×224 screen
  (`DAT_06063c4c` and 50), not the room bounds, and sorts it at its own z.
  Verified for the culling; the sort units weren't decoded.
- **Ours:** `saturn/render.cpp:2439` culls against `viewport`, which is the
  room's clip, and line 2462 sorts at z − 128.
- **Effect:** rarely, an effect whose room is stale gets culled, or blood shows in
  front of the face it should sit behind.

### 218. The object cull sphere is smaller than the bounding box's corners
*Severity: subtle · Confidence: suspected · Fix: small · Audit: GFX-14*

- **Retail:** S_GetObjectBounds (`0x06037d04`) projects all 8 corners of the
  bounding box.
- **Ours:** `drawModel`'s cull sphere is 0.75 × the largest extent
  (`openlara/draw.h:678`). For a cube-like box that is less than the true corner
  distance (0.87 × the side).
- **Effect:** an item's corners may pop out early at the screen and portal edges.

---

## Rendering: the new renderer's open work

Not differences from retail: open work on the renderer being rebuilt in the
render lab (`lab/`, `make lab`, `tools/lab.py`), measured or known and left
for later. Times are Mednafen at the ledge with the master alone, unless the
issue says otherwise.

**Each issue has these fields:**
- **Kind**: *speed*, *look*, *correctness* (the new way not yet doing what
  the game does), or *coverage* (not yet measured or checked somewhere).
- **Size**: what it is worth or costs, and whether that was measured or
  estimated.
- **Fix**: small / medium / large.

### 219. Pose: each rotation waits on the multiplier
*Kind: speed · Size: about 4% of the new pose, ~0.05 ms (estimated) · Fix:
small*

- `lab/pose.s` (ROWROT) reads two of each row's four products straight after
  their MULS.W: about 4 wait cycles a row, 12 in a rotation of ~91.
- **Fix:** interleave the rows so the next row's loads and multiplies fill the
  waits. That needs two more registers: park the pending sine offsets on the
  stack once per `.Lrot` call.

### 220. Pose: each node's matrix is built at the stack's top, then copied out
*Kind: speed · Size: the copy-out is 12% of the new pose (measured); about 5%
to win (estimated) · Fix: medium*

- Each drawn node's 48 bytes are copied to its draw's slot, and a node that
  pushes copies 48 bytes onto the stack.
- Worth more on the Saturn: the new pose runs ×1.38 its Mednafen time there
  (1.65 ms), the new light, which writes little, ×1.17.
- **Fix:** at the whole-frame step, write the matrices straight into the mesh
  jobs the feed builds for the DSP (`lab/meshfeed.s`), which removes the copy.
  Failing that, build each node from its parent in its own slot, with a stack
  of pointers.

### 221. Rooms decide turned lines and mip levels every other frame
*Kind: speed · Size: ~0.6 ms (~0.7 on the Saturn), measured · Fix: small*

- Deciding every 4th frame would save that much of the room pass. A quad's
  choice could then be up to 3 frames late (now 1).
- Parked by the owner (2026-10-29) as a saving, to be judged on a moving
  capture.

### 222. Rooms aren't culled before the transform
*Kind: speed · Size: ~0.8 ms net, measured, not built · Fix: large*

- Culling face by face would leave 820 of the ledge's 1,414 room vertices
  needed. Spatial clusters with exact box tests save only 7-19%: TR's faces
  are big next to its portals, so a cluster's box nearly always straddles a
  portal edge.
- A DSP outcode pass plus a face pre-pass nets about 0.8 ms.
- Skipped vertices would take made-up clip flags, and faces would keep the
  game's order (an exact list needs it).
- Modelled whole-room and face culling 2026-11-02 (`tools/occmodel`: 8000 random views a
  level, the game's walk, a depth buffer as the oracle; its walk matches the game's list at
  two real views). Views of 2000+ room vertices, Cistern then all levels, Mednafen ms of
  room work and walk together, both CPUs (about half reaches the frame); VDP1 modelled:
  - Perfect knowledge, rooms: -3.7, -5.1. Faces too: -5.0, -5.8, VDP1 -25%.
  - True portal shapes: +9.7, +3.3 (-0.7, -1.2 before the walk's own cost).
  - Baked big faces in a coarse mask: +5.9, +3.5 (-0.4 before its cost). Dead.
  - Rooms baked by camera sector: -0.7, -1.8, 5-37 KB a level, rare misses (worst 10276 px).
    By heading too, trained hard, the heavy places alone: -0.8 at the Cistern, 63 KB or more.
    A room hidden from one camera isn't hidden half a sector along.
  - Nearest room first, the frame's own big faces as the mask: +1.0, +0.6, VDP1 -8%, nothing
    lost, needs rooms' order reversed.
  - A window under 128 px not entered, filled: -0.87, -0.61, mean 10 and 3 px lost, worst 268.
    Under 256: -1.62, -1.07, mean 112 and 26 px, worst 836. Free, no data. Not tried in game.

### 223. Mesh back faces are found after the transform
*Kind: speed · Size: at most ~1.1 ms, about what the test costs; measured, not
built · Fix: medium*

- 790 of the ledge's 1,339 mesh faces face away, but the front faces still use
  1,117 of its 1,634 mesh vertices.
- A test per face in mesh space costs about what it saves, unless each face's
  normal is stored, which costs RAM.

### 224. Room faces run on the slave alone
*Kind: speed · Size: unmeasured · Fix: medium*

- With stages 2 and 3 together, the rooms' faces stay on the slave: they share
  the room texture cache and the per-texture material records, and keep the
  room list's order.
- Splitting them across both CPUs needs a material record set per CPU and the
  order kept.

### 225. The DSP has the longest lane in stage 2
*Kind: speed · Size: ~1 ms (packing's +31%), measured · Fix: medium*

- The DSP turns the rooms and then the meshes: 2.29 + 0.06 + ~4.7 ms, longer
  than either CPU's work. Meshes packed as the level keeps them cost +31% DSP
  time against the bench's v3 layout, which would have grown RAM.
- On the Saturn (19bcbaf) the CPUs' traffic slows the DSP more than Mednafen
  shows: alone rooms 2.32 + meshes 4.47 ms; with both CPUs at work 2.78 +
  0.07 + 5.45, 8.30 of stage 2's 8.50 (meshes +22%, Mednafen ~+10%). In the
  whole frame the CPUs are the longer lane (~19 ms).
- **Levers:** load the mesh program without the CPU's hand-off; start the DSP
  earlier, overlapping pose or the previous frame's end.

### 230. Textures crunch and shimmer: the crunch lab's favoured look isn't in the game
*Kind: look · Fix: large*

- `tools/crunch` measured each idea's flicker on the ledge. The owner's
  favourite (2026-10-29): ripmaps, the textures' own palette, softened thin
  lines, an exact divide (issue 229) and contrast by room light. Baked relief
  is the most promising texture treatment.
- Ripmaps matter because the mip level is chosen along lines only, so grazing
  faces shimmer.
- None of it is in the game or the new renderer yet.

### 231. Objects have no mip levels
*Kind: speed (VDP1) · Size: 1.5-5% of VDP1 time, measured · Fix: medium*

- Shrunk object faces are 13% of a frame's VDP1 time at one spot and 31% among
  plants, but mips would save only 1.5-5%: most of their cost is per line, on
  small faces. They would also take VRAM from the room texture cache.

### 232. Light: the divider's waits and the square root
*Kind: speed · Size: about 0.1-0.15 ms of the new light's 0.64 (estimated) ·
Fix: small*

- `lab/pose.s` (poseLightAsm) waits on the SH-2's divider for each light, and
  three times for the light's direction: about 15% of its time. phd_sqrt's 16
  bit-pair steps take about another 15%.
- **Fix:** overlap each divide with the next light's arithmetic, and the
  direction's divides with the view's products; a shorter exact square root.

### 233. The draws' meshes run on the master alone
*Kind: speed · Size: the pool's frame is 3.98 ms with the slave idle 2.4 ms
(measured) · Fix: medium*

- In the frame (stages 1-3 together) the master projects every draw's mesh
  and adds its faces; the slave takes rooms and, when the master is the
  heavier, some items' pose. A scene with few rooms and many meshes (the pool:
  1 room) leaves the slave idle.
- **Fix:** let the slave take draws too: mesh jobs claimed from the end, as
  the room chunks are, their faces into its own list of buckets, kept in the
  game's order by the join.

### 234. On the Saturn the slave idles 1.32 ms of the frame
*Kind: speed · Size: up to ~0.7 ms at the ledge on the Saturn (measured
19bcbaf; Mednafen shows 0.19) · Fix: small once found*

- The frame (stages 1-3 together) on the Saturn: master 20.83 ms work + 0.27
  waits, slave 19.35 + 1.32. Mednafen, same build: master 16.11 + 0.35, slave
  16.00 + 0.19.
- Where the slave waits on the hardware isn't known yet: for the DSP's first
  room (the DSP runs slower on the Saturn with both CPUs busy, issue 225) or
  at the end. The frame's plan prices pose, light and the DSP in Mednafen's
  cycles, and the new pose costs ×1.38 on the Saturn.
- **Fix:** find the wait (the frame trace on the Saturn), then price the plan
  from the Saturn's figures.
- Since the frame makes its own draw list (Mednafen, ledge): the slave walks
  the rooms and starts the DSP while the master makes the draw list, the
  meshes' jobs and the plan (1.5 ms together), then waits for the plan:
  slave 16.62 ms work + 1.05 waits, master 17.56 + 0.23. Using that wait
  means giving the slave part of the master's work before the plan: the
  pose claimed item by item (as the rooms' chunks are) instead of planned,
  or the rooms' first chunks taken before the plan.

### 238. The draw list is mostly cold memory
*Kind: speed · Size: ledge 0.96 ms new against the game's 1.29 (measured,
163aca5); maybe 0.3 ms to win (estimated) · Fix: medium*

- `lab/draws.s`'s profile is flat: the sphere tests ~8%, writing the draws
  ~10%, each mesh's vertex count looked up ~4%, the calls' dispatch, the
  pose's items. With the cache emptied before each run (as a frame finds the
  game's items), most of its 27.6k cycles are lines filled.
- **Levers:** a leaner draw (8 bytes: type, mesh, flags, first vertex) where
  the stages after need no more; a byte table of each mesh's vertex count
  instead of its header's; the items' fields read in one pass of lines.
- In the frame (Mednafen, ledge, the master's path before the pose): the
  draw list 29.3k cycles (1.02 ms), the meshes' jobs from it 8.2k (0.29 ms;
  `verticesDrawsSet` reads every draw back from memory to find the meshes),
  the plan 5.3k (0.18 ms), the share and the claims 0.8k. The jobs' pass is
  a lever of its own: `drawsAsm` could write each mesh's job (out, parts,
  transform) as it writes the draw, with the draw's lines still in hand.
- The draw order hints (a0a4c24) cost the draw list ~0.08 ms at the ledge
  (each model's and static's call tests its thing's bit), and a hinted
  mesh's faces are found and held in C after `facesMeshAsm` (~0.15 ms with
  LEVEL1's stamped icicle and planks on screen; render.cpp's own hints cost
  ~0.2 ms there). Both could be asm: the bit test inside the model and
  static handlers, the holding beside `facesmesh.s`.

### 244. Some rooms' faces stay in low work RAM at four levels
*Kind: speed · Size: LEVEL6 17 rooms of 79, LEVEL7A 44 of 139, LEVEL7B 10 of
114, LEVEL10B 18 of 101 (tools/ramaudit.py) · Fix: medium*

- The level's arena takes the rooms' faces last, room by room as they fit
  (`os.cpp osPlaceRoomFaces`); the rest stay where the level file has them,
  in low work RAM, which the slave reads more slowly every frame it draws
  those rooms.
- **Measured 2026-11-03 (Mednafen, room 103, Near Ours, Texels 1):** 41 cycles a face read. The same
  frame's `facesRoomAsm` is 17.30 ms with every room's faces low and 16.84 ms as it ships (326 of 1357
  faces read from high), so about 15.4 ms with all of them high: 1.5 ms to win at that view.
- **It has grown:** LEVEL7A's faces got 37,296 bytes of high work RAM at 33102ae (48 rooms) and get
  6,832 now (11 rooms), on an HW_LOG build at both: the program grew 30,448 bytes between them, which
  is all of the loss. That view's rooms need 22 KB. Any more growth takes the rest.
- **What grew:** 17.9 KB of code (near faces 7.7, retail's camera 3.8, debug 2.5, FMV 1.0, the rest
  2.9) and 12.5 KB of buffers and tables (3,328 room vertices 7.7, the stamps page 1.3, sort keys 1.1).
- **Against retail:** the code isn't the problem: retail's is 380 KB and ours 212 KB, and our port of
  its camera 3.8 KB against its 5.4 KB. Buffers are: about 270 KB of data in retail, 570 KB of bss in
  ours. Retail's one room vertex buffer is 48,000 bytes (1,500 of 32 bytes at `06094564`), overwritten
  room by room; ours hold the frame's rooms, 88 KB. Its near faces are 4.9 KB against our 9.0, of which
  2.6 is the old path behind the menu's Near Ours, which a build without the menu can't reach.
- **Test discs read worse than the game:** a build without HW_LOG ends 19,632 bytes lower, which would
  leave LEVEL7A's faces about 26.5 KB (by subtraction, not run).
- **After 2026-11-03** (`gSaveBuffer` gone, the old near path gone, no vertices kept for split corners:
  15 KB; room 103, Near 4096, Texels 1): a test disc gives LEVEL7A's faces 21.8 KB (23 rooms), 16 of the
  view's 26 rooms high, `facesRoomAsm` 16.82 -> 15.89 ms; a build without HW_LOG 41.4 KB (53 rooms), all
  26 high, 15.41 ms. HW_LOG costs a test disc 19.6 KB of this and 0.48 ms at that view.
- **Candidates:** issues 264, 265 and 266.
- **Levers:** 242's reserve (face sets in low work RAM instead, 8-byte room
  faces, the meshes' parts folded into their headers, the draws' limit
  measured in play, the rooms projected in place over the DSP's rotated
  vertices), and the program's biggest buffers (ramaudit lists them:
  OpenLara's `enemiesExtra` 64.5 KB, the frame's commands 56, the vertices
  40, the rooms' rotated vertices 38).

### 245. Levels load about a second slower than with the old renderer
*Kind: speed · Size: Mednafen, from gNextLevel to the first frame drawn:
LEVEL1 10.8 s (old 10.2), LEVEL2 12.7 (11.8), LEVEL4 13.3 (11.8), LEVEL10B
12.7 (11.3); LEVEL7A 12.3 (12.7) · Fix: small to medium*

- The load reads one file more (the meshes' store, .MSH), copies every
  room's vertices and as many rooms' faces as fit to high work RAM, and
  makes the renderer's tables; the title's first frame comes 2 s later.
  Which of these takes the time is unmeasured, and so is the Saturn's
  (Mednafen's CD timing is not the drive's).
- **Levers:** the meshes' store read with the level file (one seek less),
  the rooms' copies made by DMA.

### 251. Seam slivers show the old frame: the hall of mirrors that's left
*Kind: look · Size: Mednafen, every move on land, frames 1800-2880, holes a list
3.6 px Cistern look, 4.3 Cistern diagonal, 16.7 pool, 56.7 tunnel (worst 492) ·
Fix: medium, parked*

- Parked 2026-11-02: the owner saw no slivers on the Saturn in play, the water
  tunnel included. Holes this size count on Mednafen's magenta, not on the TV.
- What's left after 261c654 and 3a2242a: long wedges 1-4 px wide along seams,
  100+ px long at worst. A vertex lying partway along another face's edge (a
  T-junction; 6-12% of a room's vertices in eight levels checked, 5-20 a room,
  85 at most, about 200 more across portals) lands off that edge's straight
  line, and what's behind the wedge isn't drawn.
- The ripple on rooms across the water surface from the camera moves each
  vertex up to 2 px each way (`kWibble`, as retail), so water scenes show the
  most. Retail has the same cracks, but its clear every frame makes them black;
  ours have shown the old frame since fd12447 dropped the clear.
- Repair: after a room is projected, put each T vertex back on its edge: a list
  per room (vertex, edge's two ends), about 5 KB a level, about 70 cycles a vertex
  by the divider, about 0.5 ms a frame at 200 visible (all estimates). It would
  run after the ripple and close clamped corners' wedges as well. Crude test,
  command lists moved offline, tunnel: holes 74 -> 19 px a list. Water rooms first
  would cost less.
- Not free elsewhere: a full clear is 3.5 ms; VDP1's vblank erase reaches 85-170
  of 224 lines on the Saturn, varying (2d1b9da); erasing in the field before a
  change needs the next list known finished, which heavy frames don't have.
- Unexplained: a 565 px wedge at a wall in the pool spot, wider than the ripple
  alone gives (`disc pool`, list 170: quads 0x80 and 0xC0 of the list).

### 252. Near quads warp, and strips could cut them with no new texture memory
*Kind: look · Size: Cistern look, Mednafen geometry, faces with 1+ px of warp
(about 19 a list, 64 at most): worst point's error median 2.4 px, 1 face in 10
over 14.6, at most 188 · Fix: medium, parked*

- Parked 2026-11-02: with the bugs gone the owner reads the warp as the Saturn norm
  they know. The strips stay here as the owner's idea worked through, unbuilt.
- VDP1 maps a texture bilinearly over the screen quad, so a quad running away
  from the camera has its texels in the wrong place. Splits with projected corners
  fix that but pop when they turn on: 6f5f284 turned on at a depth ratio of 2, far
  past where it could go unseen.
- Turn a split on by the warp itself, the distance between the corners' mean and
  the projected centre: at 2 px the picture moves 2 px when it starts. About 12
  quads a list qualify at that spot, 32 at most.
- VDP1 has no stride: a row of a W wide texture starts W texels after the last.
  So any band of rows of a texture already in the cache is a texture, by address
  alone (16 wide and up), with no copy and no new layout, and the turned copy gives
  the other axis. Columns can't be had from the same bytes as the whole: stored as
  two columns, each 32 x 64 reads on past its quadrants, but no 64 wide command
  could draw it.
- Worst error by way of cutting, same faces (commands a face; median, 90th
  percentile, share under 2 px): whole (1; 2.4, 14.6, 39%), 2x2 (4; 0.6, 4.3, 79%),
  4 strips (4; 0.7, 2.7, 84%), 8 strips (8; 0.7, 2.4, 85%), 4x4 (16; 0.2, 1.1, 99%).
  Strips stop gaining at 4: what's left runs the other way.
- Not built. Strips don't shorten lines, so far-reaching quads keep roomQuadCut.

### 255. The Cistern's long views put the CPUs and VDP1 over budget together
*Kind: speed · Size: Saturn photos below, 20-27 fps · Fix: unknown, not traced*

- Found in play on the Saturn (2026-11-02). Lara's room, position and heading;
  what the bench showed:
  - Room 84, 40578 -5120 49157, 0x139C, guns out: rooms 31, room vertices 2821,
    mesh vertices 1229, commands 1184, meshes 98; VDP1 93, master 123, slave 107,
    24 fps.
  - Room 102, 34112 -3328 55667, 0x3962: rooms 32, 2790, 828, 1059, 53; VDP1 82,
    master 110, slave 93, 27 fps.
  - Room 103, 47729 0 50939, 0xE4BF, looking up: rooms 30, 2547, 1405, 905, 115;
    VDP1 77, master 118, slave 108, 25 fps.
- Holding look changes the load by the view alone. Room 84, 40542 -5120 49808,
  0xFA5E: not looking, rooms 28, 2583, 1169, 1129, 86, VDP1 102, slave 115, 20 fps;
  looking up, rooms 24, 2070, 835, 796, 61, VDP1 71, slave 94, 30 fps. The camera
  drops behind her head and the balconies leave the frame.
- These numbers come from before issue 254's fix: a room was missing from some of
  them, so the full views cost more than this.

### 256. Lighter rats and crocodiles, as the wolf got
*Kind: speed · Size: 4.1 ms a rat, 3.5 ms a crocodile, Mednafen · Fix: the diet, choices the user's*

- The Cistern places 17 land rats (26 joints, 235 vertices), and water crocodiles
  (21 joints, 164 vertices). Atlantis, Palace Midas, Khamoon, Colosseum and Lost
  Valley have many-jointed animals of their own (`tools/diet.py`'s survey).
- Master mesh work at the room 90 spot (`tools/hwtest.sh cistern-steps`, Mednafen, DRAWS +
  DRAW_LIST + POSE + FEED a frame): 21.09 ms; rat hidden 16.95; both crocodiles
  hidden 14.16; all three 9.64.
- Fix: merge and hold joints with `tools/diet.py`, the user choosing the merges in
  `tools/model_view.py`, as for the wolf. The animals stay; only their meshes change, and
  every state pays less, awake or asleep.
- Built (81b84a3), the user's choices: every animal in the viewer joined into one
  shell, one vertex a corner; the rat's limbs, tail and ears as cards. Master mesh work a kind (draws, pose,
  feed, draw list, upload), three standing still in room 32 (`tools/hwtest.sh zoo-rats` and so on), Mednafen,
  less the room without them, ms an animal: rat 3.20 to 2.60, crocodile 2.26 to 2.14, gorilla 2.18 to 2.08.
  VDP1 a list, ms an animal: rat 1.08 to 0.47, crocodile 0.91 to 0.78, gorilla 0.67 to 0.66. Wolves and the bear:
  within 0.3 ms. Awake, three rats, a gorilla and two crocodiles at Lara's feet (`zoo`): 28.05 to 28.95 fps, VDP1
  31.50 to 27.47 ms. What's left of a rat: draws 1.53, feed 0.44, pose 0.40, draw list 0.20: feed, pose and the
  draw list go by joints, and joints are what's left.
- The diet's originals are out of the level. `tools/pack_level.py` kept every mesh whole in the
  PKD beside the renderer's store, though the game's code reads only its collision sphere, and loaded the level's
  object textures as one block, read or not. Now the PKD has the spheres, the store the geometry, and the blob what
  the store's faces read (`TextureTable.drop_unread`). Low work RAM, all meshes: 48-88 KB a level (LEVEL7B 1.0 KB
  free to 87 KB). VDP1 VRAM, for the room texture cache: LEVEL1 12.4 KB, LEVEL7A 9.4 KB, none where nothing was
  unread. Every mesh of every level compared with before, face by face: same bytes read, same spheres. The program
  is byte-identical; frame costs within 0.03 ms (Mednafen, `zoo`, `zoo-wolves`).
- On the Saturn, 2026-11-04, `zoo` and `zoo-wolves` before merging: the user found they look right. No Saturn
  trace yet. Merged with VDP2's planes. Against main before the merge, Mednafen, 30 fps at all of these: `zoo`
  VDP1 27.06 to 24.97 ms, master draws 13.51 to 11.60, slave jobs 7.22 to 5.99; `zoo-wolves` VDP1 25.53 to 25.19,
  draws 9.86 to 9.57; `cistern-steps` draws 5.40 to 4.87, VDP1 17.52 to 17.72; `ledge` and `valley-plants` within
  0.12 ms. The rise at `cistern-steps` is in room 90, not the animals: the same commands and pixels, but four big
  faces there went from a shaded copy of their colour table to real gouraud (+0.15 ms). The copies' cache picks
  its slot by the table's address (`renderer/faces.cpp` `shadedCopy`), and the tables moved.
- The raptor and T-rex (branch `shell`), the user's choices: body, neck and tail one shell, legs in their pieces (joined,
  a leg crushes at its bends, and fixed to a bend it loses its feel). Raptor: claws fused and, with the arms, cards.
  T-rex: full arms, hands flat plates fused to the forearms. Arm cards on it saved 0.10 ms of CPU and 0.18 of VDP1 against
  full arms, not worth the code to keep their dark band in place. Standing still, Mednafen, less the room (`zoo-rex`,
  `zoo-raptors`, `zoo-dinos-none`), ms a frame, CPU then VDP1: T-rex 3.49 to 3.12, 5.03 to 4.40; a raptor of three 4.31 to
  3.03, 0.93 to 0.72. Mesh store: LEVEL3A 3.0 KB less, LEVEL3B 2.2 KB. No new code, packer only. On the Saturn (`zoo-dinos`, 2026-11-04) the user found they look right; no Saturn trace yet.

### 257. Creatures that paint nothing cost as much as those that do
*Kind: speed · Size: 6.9 ms at one spot, Mednafen · Fix: open*

- Room 90 spot: both crocodiles of room 0, 12,165 and 16,922 units away, paint no
  pixel (8 paired pictures against hiding them, within 4 degrees of the user's camera).
  Room 0 itself shows 8,380 pixels, so nothing by the room catches them.
- They pass `roomsItemSeen` (`renderer/rooms.cpp`), which already tests a creature against
  each chain of portals its room was seen by: nearer rooms' own walls hide them, not
  portals.
- Ideas: hand stamps that list items as well as rooms (`saturn/debug.cpp`), or fewer
  joints past a distance.

### 258. The draw distance: what's past where anything can be seen
*Kind: speed · Size: see below · Fix: built, the default the user's call on the Saturn*

- A room wholly past the far plane was never drawn: a portal with all four corners past it
  isn't entered (`renderer/rooms.s` .Lcheck). What the far plane leaves is rooms with a corner
  inside it, whose every vertex is then turned.
- Far 16 on the Saturn, room 103 area, poses differing slightly: 25 fps to 30, VDP1 77 to 66,
  master 118 to 101, slave 108 to 85, no change seen in the picture. In Lara's Home, bright, 16
  pops in a little and 18 is mostly fine (the user, 2026-11-02). A cap that suits a dark
  level doesn't suit a bright one.
- The user again, 2026-11-03: Far 16 once the Tiny and Black rules were in showed no real
  improvement, those rules already doing this work. Far stays 20.
- The black test (menu row Black): a room seen by no portal with a corner nearer than where its
  brightest vertex reaches the fog's floor is dropped, with what stands in it, its window
  filled, the walk going on through it (`RoomLevel.black`); a face wholly at the floor is
  filled, not drawn (`CLIP_BLACK`). Creatures in a dropped room go on being simulated.
- It costs a compare a portal, and on a vertex at the floor two instructions. Modelled, views of
  2000+ room vertices, Cistern then all levels: rooms -0.65, -0.82 ms; faces -1.22, -0.92 ms,
  with fills not counted, so read low; lit pixels lost 0.
- Measured, Mednafen, one held view each. Room 103 spot's own camera: one room and a 26-joint
  creature gone, DRAWS 10.07 to 6.53 ms, 73 meshes to 46. Room 90 camera: 3 rooms, windows of
  3 to 256 pixels; the frame there is still over on meshes (256, 257).
- The tiny window test (menu row Tiny, 128 px unless changed): a portal, none of it behind the
  camera, whose window comes to under so many pixels isn't entered; its window filled
  transparent, or under 16 pixels left as it is. Modelled, net ms, Cistern then all levels: 64 px
  -0.55, -0.32; 128 px -0.87, -0.61; 256 px -1.62, -1.07. Mednafen near the stamped camera: rooms 3, 9
  and 10 gone, 2853 room vertices to 2423, 5 pixels of fill left showing at 256 px.
- Menu row Holes paints fills and whatever nothing draws magenta, at about 3.5 ms of VDP1.
- Decided, the user, on the Saturn (2026-11-02): Tiny at 256, "totally fine to see black there", to
  keep an eye on; the room 103 spot 28-30 fps depending on the camera, called done.
- Open: Far 16 against the black test on the Saturn, in Lara's Home and at both spots; the stamps'
  hall of mirrors (the user, turning back to a stamped view): hidden rooms' windows are now
  filled too, to be looked at again.
- 2026-11-05: the faces' half was wrong, shade 31 isn't black on the Saturn, and is gone; the rooms'
  half now spares any room with a face that shows at the floor (301).

### 271. Other levels could have the vblank erase; each must pay for its earlier deadline
*Kind: speed · Size: below · Fix: small per level, the user's call, each read on the Saturn first*

- Lost Valley alone has it (267). `renderLevelInit` chooses by level; rooms flagged instead would have to choose a
  list ahead, so walking into an open room could show old pixels for a frame.
- The rule at the 30 fps line: it gains the black fills F (ms of VDP1), costs the strip 0.59 and the 2.47 ms deadline, so
  lists right at the line gain F - 3.06. Past the line it helps: a list that misses 30 fps by less than F - 0.59
  goes from 20 fps to 2 and 3 fields in turn, 24. The slave gains about 5 us a fill.
- Mednafen (`../trs_clear`, lists read long there), list ms and fps, no clear against the erase: Cistern room 103
  35.49, 20.00 / 35.20, 23.64 (half the lists late); Cistern water 39.70, 20 / 37.60, 20; Cistern room 99
  30.17, 30 / 27.68, 30; Cistern steps 31.14, 30 / 32.37, 30 (every list late, a polygon every list); the ledge
  31.72, 30 / 32.45, 24.24; wolves 28.18 fps / 26.97; Vilcabamba 26.55, 30 / 27.10, 30; the pool 19.72, 30 /
  20.32, 30. F is about 2.3-3.1 at the Cistern, 0.7 at the ledge, 0 at Vilcabamba and the pool.
- Open places on a first scan (`tools/levelgaps.py`): LEVEL5 346 sectors, LEVEL8C 289, LEVEL6 76, LEVEL7B 67.
  The Colosseum and the Sanctuary may need it for their holes, as Lost Valley did.
- In a closed room one box round the fills (no erase) has more room at the line unless the box costs 3.06 ms or
  more, near the whole frame: Cistern room 103 with near-black faces skipped, 32.57 ms against a limit of 33.36 with the box, 30.87 against
  30.89 with the erase. So the erase is for open scenes, for their holes.
- Not measured: cracks. Where seams open, closed rooms show the last frame now, nearly the same picture, and
  would show black. Stills of Cistern room 103 and the water differ by about 30 pixels that way.
- If every level had it, the fills' code could go: the walk's, `.Lvoid` in two files, `faceVoid`, about 150 lines.

### 272. A skybox from TRX on VDP2 behind the open scenes
*Kind: look, an addition retail hasn't got · Size: Lost Valley, the Colosseum, the Sanctuary · Fix: medium, waits for the VDP2 work*

- The user's idea, 2026-11-03: TRX (`external_resources/TRX`) has skyboxes; VDP2 could show one behind VDP1's frame, where
  colour 0 is transparent. The VDP2 support is coming along on branch `vdp2-lab` (`docs/vdp2-lab.md`).
- The catch: under the erase (267) a face at the fog's floor draws nothing, so sky would show through far rock that should
  stand black against it. Those faces would need real black (0x8000) above the horizon, at some of what the erase saved.

### 280. The flooded Cistern from above the water holds 22 fps where the dry one holds 30
*Kind: speed · Size: Mednafen, VDP1 +8.7 ms at one view · Fix: to be looked at*

- Room 99 along the hall, the frames page: dry (`tools/hwtest.sh cistern-straight`) 24 rooms, 769 commands, VDP1 24.4 ms,
  CPU 25.7 ms, 30 fps. Flooded (`cistern-flood-above`, branch `vdp2-planes` for now) 24 rooms, 936 commands, VDP1 33.0 ms,
  CPU 25-30 ms, 22-23 fps. Not read on the Saturn.
- What the flood adds is the water's surface: 293 quads at y -3072 and 14 at -3328, one each way, transparent, four frames of
  animated texture. VDP1 walks each one whole for the flecks it holds.
- Under water the same hall is the lightest it gets (`cistern-flood`): 5 to 7 rooms, VDP1 16.7-23.4 ms, CPU 12-14 ms, 30 fps.
- With floors on VDP2 (269), the wet floor gone from under the surface, the same view: VDP1 24.0 ms, 30 fps. See also 253, the flecks.

### 281. Solid boxes: tried on three pillars, none in the game yet, and what's left of the idea
*Kind: speed - Size: Mednafen, VDP1 -4.41 ms at room 103, little elsewhere - Fix: built for faces and portals, objects to come*

- `saturn/occluders.txt` names shapes nothing can be seen through, a box or a flat rectangle with the side it's seen from.
  Each frame each gets a rectangle inside its outline (`rooms.s roomsBoxesAsm`). A portal or a room face wholly in the rectangle
  and past the sides that face the camera is left out; so the rule works for a long wall or a floor, not only a pillar. HW_LOG
  menu row `Boxes`. `python -I tools/occluders.py check LEVEL` tests a level's boxes against its rooms.
- Commented out of `saturn/occluders.txt` at the user's word (589979b): a tool in the belt, the Saturn saw about 3.3 ms of VDP1 saved at room 103.
- Three Cistern pillars, on against off, Mednafen, list ms: room 103 33.83 -> 29.42 (20 -> 30 fps), water 38.62 -> 36.90 (the
  texture cache has room, a strip that was flat colour keeps its texture), room 99 28.50 -> 27.32. Steps, room 2: nothing. The
  CPUs: the slave's faces -0.23 ms at room 99, the finding +0.17 to +0.27 ms wherever a box is in view. Levels with no boxes:
  under 0.1 ms. 8000 random views in tools/occmodel: 0 pixels lost.
- Not done: objects behind a box are still drawn. A box with a corner within 128 of the camera or past the far plane is off.
- Finding more: tools/occmodel `boxsearch` tried 493 boxes of rock and 651 rectangles of faces over 300 heavy Cistern views:
  the best 16 save 0.08 ms of room work and 0.19 ms of VDP1 on average. So a spot tool, placed by hand where a view is heavy; no
  automatic list. The rock between rooms is thin, and what looks solid is mostly sectors of air closed by faces.
- For VDP2 planes (269): a flat rectangle at the plane's height, seen from above, hides what's below it wholly inside its rectangle.
  It can't help a face that pokes out over the rim, which is what the kept faces are for.

### 283. VDP1's faces show through a plane where they lie past it
*Kind: look · Size: Cistern room 103 from the start, a green patch of pool wall on the floor · Fix: rooms under the floor built, Saturn read to come; dips and meshes left*

- VDP1 is always in front of VDP2. A floor face used to hide what lay past it; on the plane (269) it can't, so a pit's far wall paints over the near floor when it reaches past the rim on screen.
- The user's plan, 2026-11-04: nearly all of them just want trimming, which solid boxes (281) can do with a flat rectangle at the floor's height. A face flush against a ledge, straddling a view above and below the plane, can't be trimmed that way and wants a mask: a colour 0 polygon in its place in the list, as Sonic World does at its river (docs/vdp2-lab.md). Pinned till solid boxes are better understood.
- A census by `tools/occmodel` `planes` mode: Cistern hall 241 px a view, valley 99, 8% of random views past 400 px. It's been judged by eye only at the spots played so far.
- Two spots from the user, 2026-11-04: `tools/hwtest.sh cistern-stamp` (room 0's far wall over room 105's floor) and `valley-under` (room 38's wall over room 31's ground, seen through a hole 2 sectors by 1).
- Census again, 4000 views, cameras in the plane's rooms, off rooms' edge sectors (`tools/occmodel`, `OCC_PLANE_CLIP`, `OCC_PLANE_RIM`), px painted over a view / views past 400 px / real pixels lost a view: hall today 219 / 301 / 0; four rim planes 2 sectors wide, any overlap 44 / 62 / 64; 4 wide, any overlap 3.3 / 8 / 64 (174 views lose 400+ px of pool wall, worst 5144); 4 wide, wholly inside 57 / 177 / 0; faces trimmed to their room's window 160 / 178 / 0.
- Rooms wholly under the floor kept to the openings at the floor's height the walk went through: a face none of whose outline falls in an opening is left out, others are drawn clipped to a rectangle round what falls in one (VDP1 user clip, one more command for those that need it). Hall 0.1 / 0 / 0, worst 28 px, 2.5 faces clipped and 1.2 left out a view, of 14.9. Valley 77 -> 25 / 259 -> 63 / 2 (4 views past 400 px lost, not looked at). Replayed on both real lists with Mednafen's VDP1 code, the patch and the wall gone, the pool's true strip kept: room 103 27.21 -> 26.92 ms, 9 clipped; valley 25.25 -> 24.78 ms, 3 clipped. Not in the game.
- What's left in the valley is rooms 56 and 51 painting over themselves, a dip 256 deep with no opening to go by. Their worst 10 floor faces back on VDP1 (`tools/floor_paint.py`) take 96% of it for 2.6% of the plane's pixels.
- The dip, `tools/hwtest.sh valley-dip`: 2 sectors, 6 faces, at sectors x 36, z 27-28 across rooms 56 and 51; retail and the floor on polygons show floor to the rock. Kept to its rim like a room under the floor (tried): it clears most of it but VDP1's clip is a rectangle and the rim a diagonal on the frame, so a wedge stays. Built: the plane's tiles round a dip stay in their rooms as quads too, drawn as polygons of colour 0 in a floor face's own place in the draw order, which clears what the dip painted and shows the plane. Picture at the spot the same as the floor on polygons, VDP2's pixels kept. `pack_level.py take_masks` finds them with no hand data: any face wholly at or under the floor in a room with part of its floor on the plane. The Cistern has none.
- Neither way covers meshes or sprites: items, creatures and room sprites under the floor still paint over it. The user's report, 2026-11-04, `cistern-stamp` walked to room 105: the pool's plants (sprite 142 on room 0's bottom, 3 px by 10 at that view) showing through the plane left of Lara. The plan: tag what's wholly under the floor, by its room and its top, and send its commands through the same column check. A thing poking above the floor stays out of it, or the check would cut off the part above: Lara treading water, a crocodile surfacing.
- Built on that rule with a table of the frame's columns in place of a rectangle cut per face (docs/vdp2-production.md, `Under floor` in the menu): hall 0.7 / 0 / 0, worst 260 px. Master 0.5 ms at room 103, 1.2 ms with the pool in front of Lara, in C.

### 284. On a plane level the trace holds about 8 frames
*Kind: test discs alone · Size: 8 frames against 120 · Fix: small, if it gets in the way*

- CPU writes to bank B would disturb the plane, so the trace goes to the bitmap's last 32 lines, 32 KB (`saturn/os.cpp`), and the profile's blocks take a third of that. Without them about 15 frames.

### 286. Coming back from the inventory, room textures show their lower detail for a frame or two
*Kind: look · Size: about 2,500 pixels a frame of the top of the frame, two frames, at the valley's room 51 · Fix: to be costed*

- Reported in play as trees flickering on unpausing, at room 51, 41188, 3584, 36270. Mednafen, the same pause with and without floors on VDP2, pixels changed from frame to frame in the top of the frame: with 569, 2,789, 2,493, then 22; without 774, 3,349, 2,575, then 761: VDP2 isn't the cause.
- The inventory takes the texture cache's first slots for the item textures (`detailSuspend`, `saturn/detail.cpp`). Back in the game the faces whose textures those were draw lower detail till theirs are uploaded again. To read on the Saturn, where it may differ: a disc with no plane at the same spot.

### 287. The T-rex's music isn't heard on the Saturn
*Kind: audible - Size: the track that plays as he wakes - Fix: root cause not found yet, one Saturn run to settle it*

- Reported in play. The trigger is the row of sectors room 56 x 4-9, z 3 and room 57 x 1-3, z 3: `SOUNDTRACK(16)`, and it wakes item 55. The bench page reads track 16 on the Saturn, so the game asks and the CD block didn't refuse: a refusal or an end of play would put the level's track 5 back within a frame (`game.h gameUpdate`).
- Mednafen plays it (`tools/hwtest.sh valley-music`, level started on the trigger, sound recorded): silence to loud from the trigger, loudness about 11,000 RMS, then track 5 again 38.8 s later, the track's length. The track's file on the disc is loud and whole. So game logic and data are right, and the fault is the Saturn's alone.
- Differences to test: since pipelining (33102ae) the trigger's CD command comes from the slave, mid-frame, where the level's start track comes from the master. And the original asks for index 0 up to index 1 of the track (0603c75c, 06043c8c), ours index 1 up to index 99.
- Test build on branch `water` (../trs_water, disc `build/disc_w_valley-music`): the bench page's bottom row reads the CD block's own status, track and position (Mednafen, playing: `cd 03 trk 10 idx 01 fad 026459`, track in hex), and the menu row `CD play` plays 16 or 5 from the master, as ours, as the original or stopping first.

### 292. City of Khamoon, room 14: VDP1 63 ms, two near pieces 20 ms of it
*Kind: speed - Size: Saturn 15.0 fps at the spot, CPUs fine (master 23.2 ms of work), the master waiting 37 ms on VDP1 - Fix: to be looked at*

- `HW_TRACE_AT=600 AT_LEVEL=LEVEL8A AT_ID=LVL_TR1_8A AT_START=14@27947:-5340:36945 AT_YAW=0x466D sh tools/hwtest.sh at`, a view tools/occmodel's `heaviest` found; Lara drops to Y -512 there and the camera sits against sloped rock, so play may seldom hold it. Lara herself isn't in the picture.
- `tools/stepwise.py`, Mednafen: 63.7 ms, 773 commands, most pixels written 3 to 6 times. Two tiles of a room 12 face near the camera, 16 x 16 texels, take 10.83 and 9.41 ms: 524 lines, 93,884 pixels walked for 11,368 written, corners held at -128 (retail's rule, 263), a long fan across the frame. The next costliest command takes 1.5 ms.
- Splitting a held tile once more in 3D would drop most of that walk and the warp with it, but a tile's rows are its own width apart in the cache, so 8 x 8 pieces want their own copy of the texture. Not built; not retail's way, the user's call. It wouldn't reach 30 fps here alone (43 ms).

- Next to it on the same table: Natla's Mines room 67 (`at`, 67@52645:-10926:98834, 0xAF46), Saturn 19.9 fps, VDP1 35.0 ms, CPUs fine, 2 ms over the line. `tools/stepwise.py`: no single piece; room 67, room 80 and room 68's commands leave no pixel, 10.7 ms of Mednafen's 45.7. A place for solid boxes (281) if play stands there.

### 293. Cistern, room 91, on the ledge over the pool: both CPUs over, 25 fps
*Kind: speed - Size: Saturn 25 fps, master 39-42 ms, slave 36 ms, VDP1 23 ms - Fix: to be looked at*

- Reported in play on the Saturn, 2026-11-05, on the build with the night's CPU work. The scene page's numbers, overlay on: room 91, Lara 40380, -5632, 65974, heading 8371; camera in room 91, 40108, -6780, 66300, pitch eb68, heading 6c6e. 30 rooms, 2,356 room vertices, 167 meshes with 1,709 vertices, 1,099 commands. vdp1 68-70, cpu 118-125, slave 108 (percent of a 30 fps frame), list 23.27 ms, er 112, late 14, tl 0.
- `AT_LEVEL=LEVEL7A AT_ID=LVL_TR1_7A AT_START=91@40380:-5632:65974 AT_YAW=0x8371 tools/hwtest.sh at` starts Lara there; the camera's look wants finding by hand. `cistern-mirror` is the same room, 40465, -5632, 65637.
- VDP1 has room, so it's the CPUs: to be read as the other spots were (`tools/hwtrace.py`, `tools/frameprof.py`). 167 meshes is the most at any spot read so far.

### 295. Water in Lost Valley and Qualopec's tomb stays polygons, not shimmers
*Kind: look, speed - Size: 309 faces in Lost Valley, 107 in Qualopec - Fix: small to try, the user's call*

- Asked by the user in play, 2026-11-05. `pack_level.take_water` goes by the texture and the shape, not by whether it's water: an animated texture at least 80% empty (`SPARSE_EMPTY`), on a flat face exactly one sector square. Every level with this kind of water passes (86-96% empty) but these two: their texture, 25..28 in LEVEL3A and 24..27 in LEVEL3B, is 55-68% empty.
- Their rushing water is another texture, solid and mostly sloped (LEVEL3A 18..21 and 41..44), and should stay polygons.
- To try: let 55-68% through for these levels. The art clipped from a fuller texture would be bigger pieces, to be judged by eye.

### 296. Shimmers shrink when Lara comes up for air
*Kind: look - Size: seen in play, 2026-11-05 - Fix: to be looked at*

- The user: coming up from below, the water's shimmers shrink in size, which doesn't look good. Diving into the water doesn't do it. `tools/hwtest.sh pool` with `tools/traces/pool_up.txt` and `pool_dive.txt` are the two moves.
- Not reproduced at Vilcabamba's pool (`tools/hwtest.sh pool`, Lara swum up through the surface, Mednafen, the list read every 6 frames across the crossing): every shimmer is the same 8 x 1 or 8 x 2 sprite on both sides, no lines. Art is drawn 1:1 and doesn't scale; only past about 3,360 units from the camera do shimmers become lines, whose length falls with distance. So a pool that small can't show it. To ask the user where they saw it; a view opening onto far water on surfacing is the first thing to try.
- The user's reading, 2026-11-05: an illusion. Shimmers were tuned for Lara at a pool's edge. With the water plane close to the camera, as when she swims or surfaces, they stay at their largest size while the water around them grows in perspective, and near water doesn't gain density either, so they read as smaller and sparser. Not more density; perhaps scale the sprites up near the camera.
- To read: `shimmersBorn` and `facesShimmersFrame` in renderer/faces.cpp, renderer/shimmers.s, the sheets seen from below (`up`) and what `(height < 0) != (up != 0)` and `s.owed = absOf(height) * owed` do as the camera crosses the surface.

### 297. Spots the user picked in play where VDP1 is over
*Kind: speed - Size: on the Saturn only the Caves' vines are over: 20 fps, VDP1 37.8 ms - Fix: the vines to be looked at; the rest left alone at the user's word*

- The user, walking the review discs in Mednafen, 2026-11-05 (HW_NO_ENEMIES). Lara / camera / pitch and heading of the camera, as the scene page shows them:
  - City of Khamoon, the open view, the one that matters most: room 12, Lara 28699, -1536, 32914, heading 4000; camera 27152, -2103, 32925, 0047 4047. 12 rooms, 778 commands, 46 meshes, vdp1 101, cpu 53.
  - City of Khamoon, staring at a corner: room 12, Lara 27947, -512, 36945, heading 4034; camera 26891, -1104, 37628, ffa8 5762. 17 rooms, 748 commands, 48 meshes, vdp1 100, cpu 61. Next to 292's model spot. Nothing heavy in sight, so it's overdraw behind the wall.
  - Natla's Mines, between two walls, a place players reach easily: room 68, Lara 51178, -9984, 93119, heading c219; camera 52791, -10400, 93038, 0b51 c216. 11 rooms, 623 commands, 39 meshes, vdp1 115, cpu 51.
  - Caves, the vines: room 26, Lara 9590, 5888, 83354, heading 18ab; camera 8282, 5054, 82552, 0014 299a. 3 rooms, 1041 commands, 52 meshes, vdp1 138, cpu 54. The user remembers it dropping frames on the Saturn too.
- The camera's heading is where Lara is seen from, so `AT_YAW` at the camera's heading gives the same view from `tools/hwtest.sh at`.
- The same frames, VDP1 list, Mednafen (`tools/stepwise.py`) -> Saturn (`tools/hwtable.py`, tag u1, u1c): Khamoon corner 33.3 -> 27.4 ms; Khamoon open 33.6 -> 26.5; Natla 35.6 -> 27.2; vines 47.7 -> 37.8. The Saturn is 0.76-0.82 of Mednafen at all four. Master's work 13-15 ms at the first three, all at 29.9 fps with no wait on VDP1. Vines 19.9 fps, the master waiting 29.8 ms on VDP1, 20.1 ms of work.
- The user played the sphinx on the Saturn: "performs perfectly fine". So no solid boxes at Khamoon or the houses for now, though the stepper shows the corner's list is 60% commands that leave no pixel: 511 of 737, 20.1 of 33.3 ms in Mednafen, nearly all room 12 hiding itself and what's behind.
- Vines, in Mednafen: static 8 alone is 291 commands, 16.4 ms; room 26 19.1 ms, of which 9.9 ms ends up under the vines and 8.5 ms under its own faces; room 27 7.4 ms, of which 4.4 ms is its floor under room 26's. 7 ms has to go on the Saturn.
- Natla's two houses can both be entered, so their doors and inner walls stay. The left: door 69 opened by the switch at room 71, sector 2,2; inside, Uzi ammo, a big medikit, a puzzle item. The right: door 70 worked by pads at room 67's sectors 3,5, 3,6 and 4,5; a corridor from room 74 with switch 66 in it.

### 298. Walls stand on portals, so the walk looks through more than is open
*Kind: speed - Size: 5-31% of all portal area by level (Tihocan 31%, Cistern 17%), not yet priced in ms - Fix: small, at pack time, not asked for yet*

- Found at Natla's Mines, room 68's portal to 67: 6144 wide, but three of its six sectors are the right house's front wall, faces of room 68 standing right on the portal's plane. The walk takes the whole slot as open, so all of room 67 behind the wall is in the frame's list: 3.56 ms at the alley in Mednafen, leaving 1% of its pixels.
- `tools/portalwalls.py [LEVEL ...]` counts it: of each portal, what faces flat on its plane and seen from the side it's looked through cover. Some are 100% covered, so never seen through from that side. Every level has such portals: LEVEL1 6.8%, 2 13.6%, 3A 6.3%, 3B 12.9%, 4 9.4%, 5 5.6%, 6 16.1%, 7A 17.0%, 7B 31.3%, 8A 10.1%, 8B 12.7%, 8C 15.1%, 10A 11.5%, 10B 17.4%, 10C 4.5%.
- The fix would be in `tools/pack_level.py`: shrink a portal to what's open, or split it into its open parts, or drop it from that side. It costs nothing at run time, works in every state of the level and cuts both the walk and VDP1. To mind: doors. A door's hole isn't covered by a face, so it stays open; a face that moves (a flip map's alternate room) must be tested in each state.

### 299. A door seen from behind its slab flickers against its frame
*Kind: look - Size: a band along the door's near edge in motion - Fix: not now, the user's word: there are many of this class in the game*

- Natla's Mines, the east end of the alley: door 64 (DOOR_2, room 66, rotation 4000), seen from room 68. Lara 53710, -10003, 93106, heading 0b0c; camera 53312, -10577, 91621, pitch feb4, heading 0aa7. Reported by the user 2026-11-05, with a photo, on the disc with `FLUSH_DOORS`.
- Not tested, by the geometry: this door faces away from the alley. `FLUSH_DOORS` keeps the half of the slab on the door's front side, so from the alley its back is flush with the wall and its edge lies behind the wall's plane. When the depth sort draws that edge after the wall, it shows on top. The two doors along the alley face it, so they don't. A door seen from both sides can't have its edge clear of the wall from both sides unless it has no thickness at all.

### 305. A turned texture that keeps turning, frame after frame
*Kind: look - Size: found 2026-11-09: the turned copies are right, the turn flips back and forth - Fix: the user's call: a margin on the turn, and Fast (306)*

- Seen by the user on the Saturn, 2026-11-09, while recording the video's mips walk: a face whose texture turns every frame, like a rotation, as if the turned path weren't getting the turned copy (or the copy weren't there) and turned the unturned texture instead.
- Seen on a test disc, not on main: `trs_video/builds/capn3_4e60502_cavesstart_plain`, 4e60502 with the room art left out (test only: `pack_level.py`'s `room_art()` call skipped, so the rooms keep the original texels and colour tables, and the mips are reduced from those). Caves, from the level's own start, a slow walk through the first big cave. Check unmodified 4e60502 at the same place first: leaving the room art out could be the cause.
- Recording: `build/video/saturn/mips_n3_ours_plain_walk.mp4` (27 s, 60 fps). Time and place in it not noted yet. The tinted walks at the same place (`mips_n1_ours_walk.mp4`, also without room art, and `mips_m1_tinted_walk_take2.mp4`, with it) may show it too.
- Found 2026-11-09, in Mednafen on the same disc. The copies are right: a probe walked the Caves from the level's start and along the back wall (974 lists) and compared every cached slot's texels in VDP1 VRAM with the texture set, transposed for a turned slot: 15,583 checks, none wrong.
- What turns is the texture's grain. A quad decides its turn (lines along the texture's rows or its columns) by which way makes fewer lines, with no margin, so a face about square on the screen flips every other frame as the camera moves: on the back-wall stretch 53 faces of 300 px or more did, some 30 times. Fast (306) reads every other texel along a line, so a texel shows as a 2-pixel streak along the lines; when the lines turn, the streaks turn with them. A texture that isn't square also changes size each flip (full and half size by turns, its texels per line differing each way).
- The same list redrawn by the fork's VDP1 code with its turned faces drawn unturned: with Fast the back wall's streaks go from upright to flat; without Fast only which texels and rows are skipped changes. Cures: a margin on the turn (keep the last one unless the other way saves, say, an eighth of the lines), which costs next to nothing, and Fast off, or Texels 1, so the streaks aren't there to turn (306).

### 306. Room textures at full size show half their texels along each line (Fast)
*Kind: look - Size: 85% of the full-size room area along the Caves back wall - Fix: the user's call, it costs VDP1 time*

- Seen in the video's mips walk, 2026-11-09: at about 19 s of `build/video/saturn/mips_sbs_B_retail_vs_ours.mp4` our back wall stretched and clumpy beside retail's, both at full size by the tint (no room art on our disc).
- Room faces are drawn with PMOD_HSS, Fast (76e4250, 2026-10-25, before the mips: the bridge view 12 -> 15 fps then). On a line shorter than the texels it crosses, VDP1 then reads only the even ones (Mednafen `vdp1.cpp` SetupDrawLine). Texels 1.5 (`gTexels` 6, 2026-11-02) keeps full size until 1.5 texels a pixel along a face's longest line, so a full-size face between 1 and 1.5 always has Fast at work: 32 of 64 texels along each line, each over 1.3 to 2 pixels. Retail never sets HSS.
- Probe walks in Mednafen: along the back wall, full size is 52% of the rooms' textured area on screen and Fast is at work on 85% of it; running from the level's start, 53% and 62%.
- So 1.5 shows no more texels along lines than 1.0: in that band it has the half size's count, every other texel instead of filtered. It adds rows across the lines only.
- Mednafen, VDP1 ms a list (the calibrated model), each way set by poke in one running game:

| Spot | Today (1.5, Fast) | 1.5, rooms without Fast | 1.0, Fast | 1.0, rooms without Fast |
|---|---|---|---|---|
| Ledge | 32.22 | +2.76 | -0.35 | +0.78 |
| Caves back wall, the walk's 19 s view | 20.98 | +1.57 | -0.63 | +0.18 |
| Caves back wall, close (z 32391) | 16.32 | +1.74 | -0.13 | +1.47 |
| Cistern | 22.87 | +1.58 | 0 | +0.87 |

- Mednafen prices lines shrunk without Fast low (its own note: 100% more on the hardware). On the Saturn, Fast off on every face (rooms and meshes, a 2026-11-05 build) took the ledge's list from 28.1 to 34.5 ms, 30 to 20 fps. A Saturn run at the ledge would price the rooms-only ways.
- Ways: Texels 1 (free; a full-size face's longest line is never shrunk, the band takes the filtered half size); rooms without Fast at 1.0, or at 1.5 for every texel as retail shows them; or ripmaps (half wide, full tall) for the band, today's VDP1 time with filtered texels. Pictures: scratchpad mips/capwall (the same list four ways) and mips/ways_out.

## Production code: clean-up

Not differences from retail, and not open work on the picture: what a shipping build still
carries from how it was built, debug reads and lab paths and spare memory. From an audit of
`saturn/` and `renderer/` on 2026-11-04 (code at 7cac8a9), to be taken on later at the user's
word. Of the same kind and already open: 264, 265, 266.

### 273. Timer reads a shipping build doesn't use
*Partly closed 2026-11-05: 2358a37. No read now round a wait that doesn't wait: master's timerTicks at the flooded hall .43 -> .2 ms. The pairs round waits that do wait stay for now; `s.late` steers the split.*

*Kind: speed · Size: perhaps 0.3 to 0.5 ms across both CPUs at a heavy view, not measured · Fix: small*

- `ticks()` (`saturn/system.cpp:620`, about 110 cycles by the audit's reading) is taken twice round every
  wait on the frame's path: `roomTurned` (`renderer/vertices.cpp:322`), each chunk of a room, up to 119
  a frame; each draw (`vertices.cpp:757-775`, `806-822`); the stages' waits (`renderer/frame.cpp:387-513`,
  `579-738`). The sums go to `gStats` for the debug page (`saturn/render.cpp:1057`) and no further.
- **Some stay:** `s.late` steers the master and slave's split each frame (`saturn/render.cpp:958`, by
  `drawsEnd`, `slaveEnd`, `gNextAfter`), so those reads aren't debug. The fix is the wait pairs behind `HW_LOG`.

### 275. Two tables bigger than any level has asked
*Kind: memory · Size: about 8 KB of high work RAM · Fix: small*

- `gSeen` (`renderer/rooms.cpp:46`): `ROOMS_LISTED` 128, 8.7 KB, where the game has been seen to reach 38.
  `gList` and `gListCopy` go by the same number. The lab wants more, so the game's limit goes behind `RENDER_LAB`.
- `made` in `facesMeshHinted` (`renderer/faces.cpp:329`, `MAX_MADE_FACES` 512), 8 KB, where the largest mesh has
  163 faces.
- Both need a limit known to hold in every level: the packer can check the meshes' and the rooms' worst case
  is the walk's own, as in issue 254. What it frees goes to 244.

### 276. Options only the lab or the debug menu changes, read on every vertex, face or room
*Kind: speed · Size: a cycle or two each, not measured · Fix: small*

- `farScaled` (`renderer/roomproject.s`, `renderer/meshproject.s`, set by `vertices.cpp:366`): only the lab turns
  it off. `lod` (`renderer/faces.cpp:1133`): the menu's Texels, which waits on the user's read on the Saturn
  (`texel-lod-door`). `black` (`gBlack`, `saturn/render.cpp:436`): the menu's. `hidden` (`renderer/rooms.cpp:90`):
  the stamps' menu. `posed` (`renderer/frame.cpp:544`): false only for the lab's `frameDraws` (277).
- These become constants in a shipping build, as `RENDER_LAB` already does in the assembly.

### 277. The lab's paths compiled into the game, and dead code
*Kind: code and memory · Size: not measured · Fix: small*

- Lab only, in the game's build: `frameDraws` (`renderer/frame.cpp:876`, its one caller `lab/lab.cpp:715`) and the
  `given` branches it needs in `stagesTogether` (`frame.cpp:542-691`).
- No caller anywhere: `detailMips` (`saturn/detail.cpp:79`), `slaveDone` (`saturn/slave.cpp:134`).

### 278. C on the frame's path with assembly beside it
*Kind: speed · Size: below, Mednafen · Fix: medium each, only if the frame still needs the CPU*

- `verticesDrawsSet` (`renderer/vertices.cpp:207`) 0.29 ms and the frame's plan (`framePlan`, `verticesPlan`) 0.18 ms
  at the ledge; `facesMeshHinted` (`renderer/faces.cpp:319`) about 0.15 ms at LEVEL1.
- The near faces' split: about 1.2 to 1.5 ms at the ledge and Cistern room 103, measured before its assembly half
  existed, so to be measured again first.

### 279. The DMA wait doesn't count a level 0 transfer that was interrupted
*Kind: correctness · Size: not seen to fail · Fix: one line, then a run on the Saturn*

- `DSTA_D0_BUSY` (`saturn/hw.h:62`) is 0x00000030, waiting or moving. libyaul's level 0 mask (`scu/dma.h`) also has bit
  16, interrupted, so 0x00010030. `dmaWait` (`saturn/system.cpp:615`) can return while a transfer is held up by a higher
  level, and the next `dmaCopy` writes its registers over it. The lab found it (`docs/vdp2-production.md`, branch `vdp2-lab`).

---

## Decisions needed before fixing

| Issue | Decision |
|---|---|
| **88** | Retail's torso charges without turning (a retail bug). Reproduce it? |
| **99** | Retail's gorilla sidestep turns wrongly (a retail bug). Reproduce it? |
| **11** | Retail's CombatCamera uses a stale target_square. Reproduce it? |
| **139** | Retail's gym counter starts at 150 and is never reset. Reproduce it? |
| **179** | Retail's stereo pan wraps oddly just left of centre. Reproduce it? |
| **183** | Retail restarts ambient loops every frame (a stutter), and plays loops an octave down (8-bit keyed on 16-bit data). Reproduce them? |
| **186** | Retail's exploding deaths are silent (sound 172 is missing). Copy that, or keep 171? |
| **194** | Retail's CD music is mono because of a driver bug. Recommendation: keep stereo. |
| **145** | Retail's sound sliders do nothing. Ours has on/off switches. Which to keep? |
| **161** | Keep the FPS counter on by default during development? |
| **162** | Attract demos need deterministic logic (issue 202) and the DEMODATA format. Build them? |
| **253** | Water's flecks are left out near the camera, where retail draws them. Keep it? |

## Closed

### 304. The plane showed far from where it belongs, through gaps
*Closed 2026-11-05: 8579cf9, maps placed where their tiles are; the long strip by 153b7d9 (301). Mednafen at the pit: the same as a build with no planes.*

- Reported in play, 2026-11-05: Caves room 14, the pit under the bridges (Lara 26625, 6656, 50869, heading ca85), on the plane trial: the leafy ceiling of rooms 26 and 27 showing behind the bridge, 25 sectors from them.
- A plane's map holds 32 x 32 sectors and repeated across the level, so wherever VDP1 left the frame clear, some tile of the plane showed. The long strip was faces the black test left out (301); a small square through another gap stayed.
- Now a set whose tiles fit one map has it placed there, on the fewest pages of names, and VDP2 shows nothing past its edges (screen-over transparent). The Caves' and both Cistern sets: names 15 -> 11 and 26 -> 20 pages. Saturn photos: the vine rooms and the Cistern's flooded hall draw as before.
- Lost Valley's floor runs 38 sectors north to south and still repeats: 7 tiles of room 31 (x 37-40, z 7-9) share map squares with room 30's, 32 sectors south, and stay polygons. The user, having played the level: fine so far, to watch in playtests.

### 303. A recess above the Caves' ceiling plane painted over it
*Closed 2026-11-05: 8579cf9, masks for ceilings (28 tiles in room 26).*

- Reported on the Saturn, 2026-11-05, on the plane trial: room 26's ceiling cut into by the faces of a recess above it (y 3840, over the plane's 4352).
- VDP1 is always in front of VDP2, so faces beyond a plane paint over it. Floors had masks for their dips (the plane's tiles kept as colour 0 polygons, drawn where the dip's faces were); ceilings have them now, and the ring of masks widens with the recess's depth (the tiles that hid it here were 2 sectors away).

### 302. Vine curtains and other statics drop out for a frame
*Closed 2026-11-05: acb5ecb. The user's recording replays clean in Mednafen; no Saturn run of its own.*

- Reported in play on the Saturn, 2026-11-05: the Caves' vine rooms, a curtain gone for a frame now and then (the user's video, frames 395-396 the first). Recorded in Mednafen by the user and reproduced from the recording.
- Pipelining (33102ae): the slave fills its waits with the next frame's update, which moves the camera, while the master places the rooms' static meshes from the camera as it makes the draw list. A frame could take some from one camera and some from the next. The slave's spare work now waits until the draw list is made.

### 301. The black test hid bright faces at the fog's floor
*Closed 2026-11-05: 153b7d9. The faces' half of the black test removed; the rooms' half kept only for rooms whose every texture is black there. Saturn, 4 spots: 29.9 fps before and after.*

- Reported in play, 2026-11-05: Caves, room 21 (Lara 12715, 3840, 56617, heading 4be9). Floor quads where the snow meets the dark lose their corners: a jagged edge, where retail and the Oct 30 build (dad9b03) show dim snow and rock. Menu row Black set to Off made it go.
- The black test (258) took shade 31, the fog's floor, for black. On the Saturn shade 31 takes 16 off each channel (of 31): a snow texel of 28 comes out 12, dim but plain to see. Retail and the PlayStation show bright texels to the far cut. 258's model counted "lit pixels lost 0" on the same wrong premise.
- Faces too dark to see were already drawn as plain black polygons (c987059), going by each texture's brightest colour. That covers the dark faces the black test left out, and still hides what's behind them.
- The rooms' half drops a room wholly past its fog distance. It now keeps any room with a face whose texture shows at the floor, in any frame of its animation. At all 8 Mednafen spots it then measured the same as off, so it may go too (the user plans lighting changes, which move what counts as dark).
- It also took most of the Caves pit leak (room 14, the plane test): the long green strip was the ceiling plane showing where the black test left faces out. A small square of it is left.

| Spot | Mednafen VDP1 ms | Saturn rooms ms (second CPU) | Saturn fps |
|---|---|---|---|
| Caves room 21 (the report) | 21.25 -> 22.53 | 10.7 -> 12.8 | 29.9 -> 29.9 |
| Caves bridge ledge | 30.98 -> 32.25 | 15.0 -> 15.6 | 29.9 -> 29.9 |
| Cistern room 99 | 20.54 -> 22.86 | 15.9 -> 16.2 | 29.9 -> 29.9 |
| Cistern room 90 | 17.66 -> 19.43 | 10.1 -> 10.6 | 29.9 -> 29.9 |
| Cistern room 103 | 9.71 -> 9.71 | | |
| Lost Valley room 30 | 22.99 -> 23.18 | | |
| Lost Valley sky | 16.42 -> 17.15 | | |
| Vilcabamba | 26.28 -> 26.28 | | |

Mednafen: hwtest discs held at their start, medians of 300 frames from frame 3000. Saturn: hwtable rows, build/night/black_before_table.txt and black_after_table.txt in ../trs_dev.

### 300. A block under a ceiling open to the room above can't be climbed
*Closed 2026-11-05: 83d938b, one `alterFloor` that does what retail's AlterFloorHeight (`060249a0`) does. Mednafen, played from `tools/hwtest.sh khamoon-block` and `khamoon-stack`: three pulls, the push, the block, the pillar, along the crevice and out into room 5. Before the fix she stood at the block and nothing happened. Saturn not run: no timing or video in it.*

- Reported in play on the Saturn, 2026-11-05: City of Khamoon, room 3, the second block pushed beside the tall pillar (Lara 25700, 2304, 17767). The way Stella's walkthrough solves the level: the block is the only way up out of that room, so the level couldn't be finished.
- The block's top meets room 3's ceiling at y 1280, and that ceiling is only the opening into room 2. Ours compared the new floor with the sector's own ceiling, took them as met and made the sector solid. Retail reads the ceiling a block above the new floor, through the rooms above, so the floor stays and Lara stands on it.
- Any block in any level could hit it, wherever it stopped under such a ceiling. The Cistern's item 69 (room 60) started that way, solid; it now has 1536 of head room. It's the only block whose start changes.
- Thor's hammer and the moving blocks shared the same shortcut in a second copy, and now share the fix.

### 294. Caves, room 24: a gap in the level shows the old frame
*Closed 2026-11-05: GAPS in tools/pack_level.py, a triangle added to the level: x 44032 to 44800 at z 66560, y 1792 to 2304, texture 92 as the wall it carries on from. The same view drawn over magenta: 53 pixels -> 0.*

*Kind: look - Size: 53 pixels at the user's view - Fix: to be chosen*

- Reported in play on the Saturn, 2026-11-05: Lara 43909, 7168, 76056, heading c3fb; camera 44317, 6643, 77515, pitch 085b, heading 8860. `AT_LEVEL=LEVEL1 AT_ID=LVL_TR1_1 AT_START=24@43909:7168:76056 AT_YAW=0x8860 tools/hwtest.sh at` gives nearly the same view, and the list drawn offline over magenta (scratchpad hole_scan) shows 53 px at x 125-136, y 19-26. The tiny-window rule isn't it: the same with it off.
- No face of any room in the level covers that point from that view, so the level has a gap there. Retail clears the frame and shows black; the Caves don't clear, so the old frame shows.
- The same kind of gap as 267. The choices there apply: the vblank erase for the level (by 271's rule it wouldn't pay at the bridge, where VDP1 is close to the line and the fills small), or a hand-placed quad behind the gap.

### 289. A frame settled a field late stays a field late
*Closed 2026-11-05: 465410f, c5330da. Saturn: valley ledge VDP1 wait 4.2 -> 0 ms, bridge ledge 8.1 -> 0 ms, both still 29.9 fps.*

*Kind: speed - Size: 3.5 to 8 ms of the master's frame where VDP1 has time to spare - Fix: small*

- Lists can settle out of step with the ticks, or with one more queued, and stay so, the master waiting on VDP1 at each frame's start while VDP1 idles after each list. After 8 such frames, with VDP1 under 30.5 ms and a frame ending within 3 ms of the next, at most once in 60 frames, the ticks start a vertical blank later.
- 465410f toggled a phase, so every other slip came a field early and put the queue back: at light scenes a swing with a 50 ms frame about every 4 seconds (St Francis' Folly room 62, Tihocan room 102, 29.8 fps). Corrected in c5330da: always a delay, and a light scene loses nothing by waiting, so it stays put.

### 290. Meshes wholly past the far plane were drawn
*Closed 2026-11-05: d176b92. Saturn, Lost Valley room 32: 26.2 -> 29.9 fps, 6 ms to spare. Mednafen: 144 meshes -> 96, master 26.64 -> 16.64 ms, same 547 commands, same picture. City of Khamoon room 15: 51 -> 48 meshes, 50 fills gone, same picture.*

*Kind: speed - Size: up to 10 ms of the master's frame in open levels - Fix: small*

- The sphere test had no far test: posed, rotated on the DSP, projected the slow way with divides, faces walked, for a transparent fill or nothing.

### 291. Room vertices past the far plane took two divides each
*Closed 2026-11-05: 2358a37. A table, 576 bytes: 144 -> about 45 cycles a vertex. Mednafen, room projection, Lost Valley room 32: 16.12 -> 12.76 ms.*

*Kind: speed - Size: 800 such vertices a list in open levels - Fix: small*

- Only fills want where they land, and a vertex lands alike for every face sharing it.

### 285. Lost Valley: room 56, high over the valley, the CPUs over on the Saturn (reported in play)
*Closed 2026-11-05: Saturn 29.9 fps, master 25.7 ms of work, VDP1 wait 4.2 -> 0 ms. Room projection cbf3339, the frame's split 443915d, the tick's slip 465410f, meshes past the far plane d176b92.*

*Kind: speed · Size: 27 fps, master 37 ms, slave 34 ms · Fix: to be looked at*

- `tools/hwtest.sh valley-ledge`, Lara at 37787, 230, 20847, heading 3ffc, camera 37048, -933, 22086, pitch f74c, heading 6787. Saturn, floor on VDP2 (269): 18 rooms, 2,647 room vertices, 122 meshes with 1,514 vertices, 446 commands; VDP1 21.7 ms, master 37.0 ms, slave 34.4 ms, 26.7 fps over 5 seconds, 24 of 134 lists late.
- VDP1 has plenty of room here and both CPUs are over. The user expects what's left of the VDP2 work to reach it.

### 282. The room walk does half its corner sums twice
*Closed 2026-11-05: f5c51e2, by another road. Facing tested in the room's loop, rectangles' corners by sums, one DMULS.L a coordinate: Mednafen, flooded hall, 115K -> 72K cycles of walk a list; Saturn, room 103 looking up, 7.4 -> 5.2 ms. Exact in the lab. The frame cache below isn't built: about -0.4 ms in Mednafen for 6.4 KB of RAM, a choice if the walk comes back.*

*Kind: speed - Size: Saturn walk 7.4 ms at room 103 looking up; estimated -0.8 ms mean, -1.9 ms worst, heavy Cistern views - Fix: to be built*

- tools/occmodel `walkstat`, heavy views (2000+ room vertices), portals checked / facing the camera / different ones of those:
  Cistern 235 / 108 / 57, worst 395 / 187 / 69; Lost Valley 112 / 54 / 35, worst 216 / 98 / 50; level 5 135 / 71 / 45; 8A 69 / 39 / 32.
- A room is walked again when its window grows, and every portal of it is rotated and projected again though nothing about it has
  changed in the frame. Keeping each portal's projected bounds for the frame leaves only the clip against the window to redo.

### 274. 16 KB at the bottom of gVertices that nothing seems to use
*Closed 2026-11-05: 443915d. Nothing used them, as read; `MAX_VERTICES` 3,328, `ROOMS_VERTICES_BOTTOM` 0 in the game. What it freed went to the rooms' faces.*

*Kind: memory · Size: 16 KB of high work RAM · Fix: small, to confirm first*

- `saturn/render.cpp:417`: 5,376 entries of 8 bytes. The walk places rooms from entry 2,048 up
  (`renderer/rooms.s:958`, `MASTER_VERTICES`), and the game's meshes go to `gMeshOut`
  (`renderer/vertices.cpp:152`). The bottom 2,048 look left over from when the meshes shared the buffer.
- From reading the code, not from a run. A Mednafen write watch over those 16 KB through a level settles it.
- What it frees goes to the rooms' faces (244).

### 253. Water's flecks are left out near the camera, where retail draws them
*Closed 2026-11-05: water's sparkle replaced by shimmers, `renderer/shimmers.s`. The user: "very close to the concept art". Saturn, flooded hall from Lara's start, percent of a frame, VDP1 / slave / master / fps: no water 62 / 91 / 106 / 28-29; the water's polygons 79 / 102 / 120 / 25-26; shimmers (one lit per 150 px, about 330 drawn) 66 / 101 / 118 / 25.*

- The sparkle's flat sector quads and the vertices only they used are out of the rooms (`pack_level.py take_water`), 838 quads and 615 vertices in the Cistern. Each sheet of water keeps its sectors sorted by the frame in 8 they start on.
- A shimmer is a piece clipped from the level's own water texture (`water_art`: dashes and chunks, one size), or a short line far off, at a place in its sector that holds for its 8 frames, each with its own start, dimmer and half-toned coming and going.
- It's worked out once, as it's born, and kept in 6 bytes; only its place on the frame is new each frame. Assembly redoing everything every frame took 8.0 ms in Mednafen at the hall and 113% on the Saturn's slave; kept, 3.7 ms. Tried and dropped: starting from the frame's cells, which looked stuck to the screen.
- What's left in the code from the old way: the sparse paths and the water textures in TEX, unread now.

### 253 (as it stood). Water's flecks are left out near the camera, where retail draws them
*Kind: look · Decision: keep it? · Built 0c367e4*

- Textures 80% or more empty over 64 lines or crossing the near plane. Saves a
  face of up to 14.7 ms (Cistern look) and more at the tunnel. Tile by tile, near
  flecks go as the camera comes within about 1400 units. A fade before the limit
  (gouraud) could hide the edge, untried. Not yet judged on the Saturn.
- 2026-11-04: the user wants them back near the camera. Why they cost so: VDP1 walks every pixel of a face whether its texel is empty or not, so a sparkle tile costs what a solid one does, and cost follows the water's area on screen, not the tiles' count. Flooded, room 99 looking over the water (`cistern-flood-down`, Z + UP 14 frames, Mednafen, `tools/stepwise.py` capture flood-down2): list 36.33 ms, without sparkles 26.83; 169 faces walk 65,630 px to write about 7,000.
- Offline with Mednafen's VDP1 code, list ms there: end codes after each row's last fleck 34.88; half the faces a frame 31.39; every run of a texture's rows a flat polygon 45.36 (+7,897 commands); the 24 longest a tile 29.18 (+801); the 12 longest 28.10 (+351); thin lines, 24 a tile 28.50 (+801), one a 128 px of face 27.42 (+167). The textures are one bit, a single colour. Far cut, test branch `water`, in the hall from Lara's start: today 24.8; near on 27.6, cut at 12 26.75, 10 25.87, 8 25.04, 6 23.68, 4 21.75.

### 288. Boxes under the flooded Cistern's statues pop between textured and flat
*Closed 2026-11-04: 6368808. Mednafen at the reported view, flat room faces 7-9 a frame to 0, VDP1 33.70 to 33.87 ms.*

*Kind: look � Size: a handful of faces in rooms full of textures � Fix: small*

- The user's report, 2026-11-04, on the shimmers build, reproduced on main's code with the water texture: not shimmers, and the rats and going in and out of the room aren't needed.
- `tools/hwtest.sh cistern-flood-statues`. At the view the half-size pool holds 64 of 64, all drawn that frame, while the full-res pool holds 30-35 of 128. A texture with no smaller mip than half size then had nowhere to go but its mean colour, and which textures lost out changed with the view.
- Now such a face takes its full-res copy while that pool has room in the frame, and goes flat only after that. Same order in C and in `facesroom.s`.

### 269. Flat floors and ceilings on a VDP2 plane
*Closed 2026-11-04: branch vdp2-planes, docs/vdp2-production.md. The Cistern (floor, ceiling) and Lost Valley (floor) are on it. Saturn, room 103 about 22 degrees up: VDP1 20.5, master 27.4, slave 24.7 ms, 30.0 fps.*

*Kind: speed, look � Size: see below � Fix: large, only if it pays*

- The user's idea (2026-11-03): one big flat floor, Lost Valley's or the Cistern's, drawn by a VDP2 rotation
  plane under VDP1's frame, with correct perspective and no VDP1 warp.
- What the biggest plane holds (`occmodel one` with OCC_PLANES, priced at each view's VDP1 time a pixel;
  Mednafen): among the plants 134 faces, 33,435 px, about 8.0 of 42.7 ms; Cistern room 103 floor 107 faces,
  26,011 px, about 5.3 of 35.5 ms, its ceiling at -7168 95 faces, about 1.5 ms. At about 570 cycles a kept
  face that is also about 2.7 and 4.0 ms of slave work. Floors elsewhere at other heights stay on VDP1.
- **What it lacks that VDP1 floors have:** each vertex's light and the moving caustics, so light is baked or
  per line; a floor hides what VDP1 drew behind it, and a plane under the frame can't, so rooms below the
  floor would show until those pixels are written colour 0, which gives back part of the saving; VDP2
  VRAM and cycles for the plane's cells, map and coefficients.
- **Now:** proven on the Saturn in the lab, 2026-11-04, and on its way into the game: the design is
  `docs/vdp2-production.md`, the lab's notes `docs/vdp2-lab.md`, branch `vdp2-lab`; the build on branch `vdp2-planes`.
- **First test on the Saturn, not in Mednafen:** no display read from VDP2 bank B has ever worked on this
  Saturn (mednafen-vdp2 notes), and new layouts have looked right in Mednafen and broken on the console.

### 205. Room light falloff is doubled, and in 11 rooms the square overflows
*Severity: visible · Confidence: confirmed (data plus arithmetic) · Fix: small
· Audit: GFX-1*

- **Retail:** Output_CalculateLight (`060388ec`; TR1X `output.c`) computes
  falloff² >> 12 and dist² >> 12, then intensity·F/(d + F).
  - It uses the raw falloff from offset +0x10 of each 0x14-byte light record
    (asm `0x06038980`).
  - The LIGHTSIZ chunk is read in bulk at `0x06039152` and never scaled.
  - The Saturn's raw falloff values are identical to the PS1's (checked in
    LEVEL1 and LEVEL2).
- **Ours:** `tools/trsat.py:236` stores `radius = radius * 2`, copied from
  OpenLara's SAT reader. `tools/pack_level.py:499` then packs it, and
  `openlara/draw.h:84`, `89` use it (`att = X_SQR(radius) >> 12` on an int32).
- **Effect:**
  - Everywhere, lights reach about twice as far, so Lara and enemies look
    brighter and more strongly side-lit than on retail.
  - Where the raw falloff is above about 23170, the int32 square overflows and
    goes negative. The rooms are:
    - LEVEL2: 71, 82
    - LEVEL3A: 1, 47, 86
    - LEVEL3B: 1
    - LEVEL5: 42
    - LEVEL7A: 6, 32
    - LEVEL7B: 28
    - LEVEL10A: 37
  - In the rooms with radius 65280, the light term grows without bound as Lara
    nears about 5.8k units from the light, then turns negative beyond that. Her
    shading blows out, then the light drops out.
  - In the rooms with 46848, the falloff is inverted: farther from the light
    reads brighter.
- **Fix:** drop the ×2, and pack radius >> 7 (with << 7 in the engine) so the
  square stays under 2³¹.

Issues 8 to 15, 23 and 26 below: closed by retail's camera, the default since 2026-11-02 (`openlara/camera.h`
updateRetail, within 2 units of retail in Mednafen at rest and looking; combat, fixed cameras and walls
in motion not compared).

### 8. The field of view is 69° where retail's is 80° (reported in play)
*Severity: visible · Confidence: confirmed · Fix: small to medium (but see the
decision) · Audit: CAM-1, MATH-1, GFX near plane*

- **Retail:**
  - phd_InitWindow (`0603cf1c`, called from `060373fc`) sets 352×224, near 80,
    far 20480, FOV 80°.
  - AlterFOV (`06039b34`) sets `phd_persp` (`DAT_06063c70`) to
    176·cos 40° / sin 40° ≈ 209.75.
  - The vertex transforms (`0604127c` meshes, `06041a80` rooms) compute
    `zp = zv/persp`, then `xs = xv/zp + 176`.
  - LaraNormal and the inventory re-apply FOV 80; cutscenes change it per
    frame (issue 19).
- **Ours:** `sx = (x · divTable[z >> 4]) >> 12`, i.e. 256·x/z, a fixed 256 px
  focal length. Sites:
  - `openlara/common.h:2276-2286` (PERSPECTIVE)
  - `saturn/render.cpp:319-323`, and `2431` (sprites)
  - `saturn/render.h:166-175`
  - `saturn/xform.s:167-185`, `saturn/meshxform.s:167-185`
  - `saturn/project.dsp`

  `matrixSetView` (`common.cpp:1460`) applies no scale. The near plane is
  `VIEW_MIN_F` 64 (`common.h:502`), against retail's 80.
- **Effect:** always.
  - The view is 69.0° × 47.3°, against retail's 80.2° × 56.4°.
  - Everything is drawn 22.5% larger, so the camera feels zoomed in and closer.
  - The inventory ring is affected too (issue 173).
- **Fix:**
  - A projection-only reciprocal table, 53504/i (65536·209.75/256), or scale
    view-matrix rows 0 and 1 by 209.75/256.
  - Keep `divTable` at 65536/i, because `GET_FRAME_T`, `moveTo` and `matrixLerp`
    use it.
  - Make the focal length a variable, for cutscene FOV (19).
  - Near plane 80.
- **Decision:** 80° puts about 1.5× as much of the world in the frame, so more
  rooms, faces and vertices. That has a CPU cost. All perf comparisons with the
  original so far measured our narrower view.

### 9. The chase camera catches up 1/8 per tick; retail's catches up 1/12
*Severity: visible · Confidence: confirmed · Fix: small · Audit: CAM-2*

- **Retail:** ChaseCamera (`0600ca08`) passes CHASE_SPEED 12 to MoveCamera
  (`0600c054`). That moves (target - pos) / speed with C division (the
  hardware-divide helper `060457f6`).
- **Ours:**
  - `openlara/camera.h:387` does `move(best, lastFixed ? speed : 8)`.
  - `camera.h:283-288` then uses `>> 3`, which rounds toward minus infinity, so
    negative errors never settle to zero.
- **Effect:** always. Ours is about 50% stiffer: it trails less, and swings
  wider when Lara turns, runs or falls.
- **Fix:** speed 12, and `/ speed`.

### 10. The chase camera looks 10° down by default
*Severity: visible · Confidence: confirmed · Fix: small · Audit: CAM-3*

- **Retail:**
  - The target elevation is reset to 0 at the end of every CalculateCamera
    (`DAT_06074db4 = 0`).
  - ChaseCamera adds only Lara's x_rot.
  - Only specific states set an elevation: hang -60°, slide -45°, surface
    -22°, and so on.
- **Ours:** `openlara/camera.h:378-380` sets `CAM_ANGLE_FOLLOW` (-10°,
  `camera.h:12`) whenever the target pitch is 0. This also applies underwater.
- **Effect:** always. Our camera sits about 267 units higher (a quarter block)
  and looks down. Retail's is level with Lara's chest height.
- **Fix:** delete the default.

### 11. Walls are handled by OpenLara's own scheme, not TR1's box-based slide
*Severity: visible · Confidence: confirmed (mechanism) · Fix: medium · Audit:
CAM-4, CAM-18*

- **Retail, chase and combat cameras:**
  - SmartShift (`0600c50c`) runs LOS (`06010ca8`), then works on the
    pathfinding boxes of the target and the camera.
  - It extends into neighbour boxes where the next block isn't a BadPosition
    (`0600c4a4`), and insets by 256.
  - ShiftCamera (`0600c394`) slides the camera along the box edge while keeping
    distance² = target_square.
- **Retail, look camera:** ClipCamera (`0600c2e8`).
- **Retail, fixed cameras:** LOS first, then ShiftClamp(256) (`0600cb20`) only
  if LOS fails (FixedCamera `0600d090`).
- **Ours:**
  - `getBestLocation` (`openlara/camera.h:232-273`) traces to the ideal spot.
  - If the trace is blocked within 768 (measured with `fastLength`), it tries
    the four world-axis directions (0/90/180/270°, not relative to Lara). It
    takes whichever is nearest the current view.
  - `clip()` (`camera.h:209-230`) then pushes 255 off walls on each axis per
    sector, and clamps y 255 from floor and ceiling.
  - `clip()` also runs on every fixed camera (`camera.h:277`). That moves 2
    retail cameras within 255 of a wall: one in Palace Midas, one in Natla's
    Mines.
- **Effect:** in corridors, and with Lara's back to a wall. Retail slides around
  Lara along the wall at full distance. Ours comes in close or jumps to a
  world-aligned side.
- **Fix:**
  - Port SmartShift, ShiftCamera, ClipCamera, BadPosition and ShiftClamp; our
    levels have the boxes.
  - Retail quirk to keep or not: CombatCamera uses a stale target_square.
  - The LOS details are issue 27.

### 12. The look camera is at half distance and has no pivot shift
*Severity: visible · Confidence: confirmed · Fix: medium · Audit: CAM-5*

- **Retail:** LookCamera (`0600ce74`; TR1X `camera.c`).
  - Distance 1536 (0x600).
  - The pivot moves along Lara's facing by -512·sin(elevation).
  - The target is ShiftClamped 306 (0x132) from walls, floor and ceiling.
  - The target's x/z glides at 1/speed (4).
  - ClipCamera handles walls.
- **Ours:** `openlara/camera.h:421-432` sets
  `targetDist = lookAtItem ? 1536 : CAM_DIST_LOOK` (768, `camera.h:9`). There is
  no pivot shift, no ShiftClamp and no target glide.
- **Effect:** every time Look is held while standing or treading water. Ours is
  twice as close. When looking up or down, retail swings to show the area ahead
  of Lara; ours orbits Lara herself.
- **Fix:** port LookCamera.

### 13. The combat camera is too close and pitched 10° down
*Severity: visible · Confidence: confirmed · Fix: small · Audit: CAM-7*

- **Retail:** CombatCamera (`0600cd34`) uses COMBAT_DISTANCE 2560 (0xA00). Its
  elevation is x_rot plus either the target angles or head + torso, with no
  extra offset.
- **Ours:** `openlara/camera.h:394` adds `CAM_ANGLE_COMBAT` (-10°), and
  `camera.h:414` sets the distance to 2048.
- **Effect:** whenever guns are drawn.
- **Fix:** 2560 and no offset.

### 14. Fixed cameras ignore the trigger's glide speed and always cut
*Severity: visible · Confidence: confirmed · Fix: small · Audit: CAM-8*

- **Retail:**
  - FixedCamera calls MoveCamera(ideal, camera.speed).
  - TestTriggers (`06010350`) decodes the speed as (field << 3) + 1.
  - A camera with a target item is forced to speed 1 on its first frame.
- **Ours:** `openlara/camera.h:443` always calls `move(best, 1)`.
- **Effect:** in retail's data, 19 of the 101 triggered cameras glide (speeds
  17, 33, 65 and 153, none with a target):
  - Gym, Caves, Vilcabamba
  - Lost Valley ×3, Qualopec ×6
  - Colosseum, Cistern, Tihocan ×2, Sanctuary ×2, Natla's Mines

  We cut instantly at all of them.
- **Fix:** `move(best, speed)`.

### 15. Look or combat after a targeted fixed camera aims at world y = 0
*Severity: visible (glitch) · Confidence: confirmed · Fix: small · Audit:
CAM-9*

- **Retail:** CalculateCamera's look/combat branch sets target.y = y - 256
  (`DAT_06074d84 = iVar9 - 0x100`) and speed 1.
- **Ours:** `openlara/camera.h:532-534` sets `target.pos.y = 0; speed = 1;`.
  Upstream OpenLara has the same bug.
- **Effect:** drawing guns or pressing Look while a camera with a target item is
  showing. The camera aims at y = 0 for a frame, then recovers at 1/4 per tick.
- **Fix:** one line.

### 23. FOLLOW_CENTRE also adds the bounding box's X centre
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: CAM-17*

- **Retail:** adds only the box's Z centre, rotated by yaw (bounds +8/+10).
- **Ours:** `openlara/camera.h:542-552` rotates both X and Z.
- **Effect:** push/pull, climbing out of water, and special kill animations.

### 26. Camera smoothing rounds with shifts instead of dividing
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: CAM-21*

- **Retail:** target.y smoothing in chase divides by 4; MoveCamera divides by
  speed.
- **Ours:** `openlara/camera.h:560` uses `>> 2`, and `camera.h:285` uses `>> 3`.
- **Effect:** residual offsets of a few units that never settle.

### 259. A hall of mirrors where the rooms want more vertices than there are
*Kind: visible · Fixed: smallest windows give way, filled · Found by the user, 2026-11-02*

- Room 91, 40465 -5632 65637, 0x73D4 (`tools/hwtest.sh cistern-mirror`), and 40340 -5632 65637,
  0x6EEC: 38 rooms listed want 3408 vertices of 3328. The first listed made way (issue 254's
  fix): there, room 105, 6,289 pixels of the picture, its window unfilled, so it showed old
  frames.
- Now the rooms showing least make way (`renderer/rooms.cpp` roomsMakeWay), their windows filled and
  what stands in them dropped: there, a room of 133 vertices seen through 3 pixels. Holes in
  Mednafen, lists drawn offline over magenta: 6,289 to 5 or fewer.

### 267. Open skies and gaps in the rock show the last frame: Lost Valley's hall of mirrors
*Closed 2026-11-04: bba39c3*

- Reported on the Saturn, 2026-11-03: room 56 looking up (`tools/hwtest.sh valley-sky`), and a smaller
  one at room 27 (`valley-cave`). Retail clears its frame, so what no face covers is black there. We
  had no clear (fd12447), which is fine in closed rooms.
- Pixels no command writes, Mednafen, `tools/stepwise.py`: looking up 33,887 (43% of the frame), among the
  plants 3,100, walking in 2,013, T-rex 1,846, the cave 265 in one triangle.
- Bake-off, branch `clear-bakeoff` (`../trs_clear`). Saturn, room 51 (41347 3584 32913 8892), list ms, all
  20 fps: no clear 37.46, quads and a fill 38.37, quads with the black fills in one box 35.67, a polygon over
  the frame 37.40, vblank erase 34.60, manual erase 34.01 (flickers). The black fills are 3.45 ms there, the strip 0.59,
  a whole frame 3.39. Ceiling quads (`tools/level_view.py`) were never reviewed: they lost.
- A first fill round each open room's box (23559a1) was reverted the same day: close to a whole clear looking up.
- What the erase gives up: a list must end 2.47 ms sooner to be erased. Ending in that gap, it still changes on
  time and the next list pays 2.80 ms for a whole-frame polygon. Cost and gain elsewhere: issue 271.
- Asked at vblank-in, the erase smeared on the Saturn with lists ending just before it, and not in Mednafen:
  the upload that starts as a list ends is suspected of holding the write to VDP1 past the sweep's start, the
  bench having asked first and started its DMA after. Asked as the list ends, the user saw no smearing:
  103 erased, 5 late, 0 too late in 5 seconds.
- The final build on the Saturn (a0f11be, from the valley mouth), 2026-11-03: the user looked it over, it looks good.

### 260. The game crashed when Pierre ran off
*Kind: crash · Fixed · Found by the user, 2026-11-02*

- Slave address error in `Enemy::updateLocation` (vector 9). Pierre gave back his creature data in
  `updateState`, then the same update went on to read it. Every time he left out of the camera's
  sight. Reproduced with `tools/hwtest.sh cistern-pierre`.
- Now as retail: he leaves after he's animated.

### 91. Pierre: no one-at-a-time rule, and he survives water
*Severity: gameplay · Confidence: confirmed · Fix: small · Audit: AI-19*

- **Retail:** PierreControl (`06028ef0`) tracks g_PierreItemNum
  (`DAT_06078c80`).
  - When a second Pierre activates, one is removed: the non-one-shot one, or the
    old one if the new one is one-shot.
  - A Pierre in water is removed.
- **Ours:** `openlara/enemy.h:2421-2480` has neither rule.
- **Effect:** two Pierres can be active together in the Folly, Colosseum,
  Cistern and Tihocan.

### 254. A big view lost the camera's own room: floors and pillars gone
*Closed 2026-11-02: e73d0d6*

- Found in play on the Saturn (2026-11-02): at about 2800 room vertices on the bench,
  the pillar beside Lara, then the floor under her, then the whole platform she
  stood on went, for as long as the view lasted. Meshes stayed: a plant in its urn
  floating on nothing.
- The jobs placed 3072 room vertices (each room's rounded up to 16) and a room past
  that had no job. The walk lists the camera's room last, so it went first.
  Reproduced at `tools/hwtest.sh cistern-pillar` looking up: 34 rooms wanting 3088,
  room 106, the camera's, out for that list.
- Now 3328, with 128 more kept for the corners of quads the near plane splits (7.7
  KB of high work RAM, taken from what the rooms' faces get). Past that the first
  listed, the deepest, make way, and the camera's room can't go. Forced with the top
  poked to 2800 of the same view: the 4 deepest rooms of 34 leave, 4161 pixels
  undrawn in the distance, the game carries on. Lab exact on ledge and pool.
- Drunk walk over the Cistern, 1176 lists: 2928 at most. Not seen past 3088 yet.

### 229. Vertices near the camera jitter from the divide table
*Closed 2026-11-02: 3a2242a, for rooms' vertices nearer than 2048; the rest and meshes keep the table*

- The projection uses `divTable[z >> 4]` (65536 / i in 16 bits), from
  OpenLara's GBA build. The error grows near the camera: about 5 px at z 512,
  176 px off centre. Retail divided each vertex with the SH-2's divider.
- The lab's stages reproduce the game's arithmetic exactly. An exact divide
  changes the output, so the checks would compare against a new reference.

### 247. The Cistern's water room 108 holds 20 fps on VDP1
*Closed 2026-11-02: b5d651b. Saturn, this spot: 30 fps (the user, in play)*

- Found in play on the Saturn (2026-11-01, Cistern test disc with the
  pipelining probe, 90ceaff): LEVEL7A room 108 at 44105 -256 62852, yaw
  0x91E7, looking across the green water. Scene page: rooms 19, room
  vertices 1939, mesh vertices 1210, commands 950, meshes 101.
- VDP1 needs about 31.7 ms for the list, past the 30.9 ms from a frame's
  display start to the second field's change (issue 241), so every list
  waits a third field.
- Which commands cost VDP1 the time (Mednafen, which reads 39.7 ms there): no
  one face, the dearest 1.4 ms; room faces 92% of the list, 3.2 screens of
  pixels.
- The list now fits in two fields if the Saturn's 31.7 ms holds, as lists may
  end inside the vertical blank (issue 241, b5d651b). `tools/hwtest.sh
  cistern-water` is this spot.

### 241. VDP1 idles between lists: the change waits for a vblank, then the upload
*Closed 2026-11-02: b5d651b*

- `stageRun` (lab.cpp): once VDP1 has drawn a list the master waits for the
  vblank-in, asks for the frame buffers' change, waits for the display to
  start (a plot late in the vertical blank locks VDP1 up), uploads the next
  list (0.84 ms, 43 KB at the ledge) and only then plots it. For 30 fps a
  list has to be drawn by the second field's vblank-in: about 30.9 ms after
  the display starts, less the upload.
- **Measured on the Saturn (2026-11-02, `make vdp1bench`):** the change comes
  on line 0, and the request is taken up to the vertical blank's last line,
  with lists ending as late as line 261 and the next plot right after: 0 hung.
- **Done (b5d651b):** a list ending inside the vertical blank asks for the
  change itself (`system.cpp spriteEndHandler`), so the limit is about 33.3 ms,
  not 30.9. Mednafen, Cistern diagonal, far plane moved to size the list, fps
  before -> after: VDP1 31.7 ms 19.98 -> 29.82, 32.6 ms 19.98 -> 28.96, 33.3 ms
  19.98 -> 22.39. Past the upload's slack a late plot carries over to the next
  frame, which gives 2- and 3-field frames mixed.
- **Not worth it, costed 2026-11-02:** two command areas in VDP1 VRAM, the next
  list uploaded while VDP1 draws the last, would keep the upload (0.84 ms) out of
  the limit. A second area of 1792 commands is 70 KB: by `detail.cpp`'s rules 14
  of 16 levels lose their mip pools and 6 can't hold the inventory's item
  textures. Capped at 1152 (20 KB) 10 levels still lose them; 1024 is under the
  1131 commands seen in a list. It would help 19 of 1200 Mednafen lists, and
  those already mix 2 and 3 fields near 29 fps.

### 250. Guns out with a creature about: Look can't tilt towards it, and her head and torso keep snapping back (reported in play)
*Closed 2026-11-01: 030af15*

- **Seen:** in Mednafen (2026-11-01, main 53dd062), on the Cistern's first pool's
  rim with the guns out: holding Look and forward she "can't look down" and her
  head glitches. `tools/hwtest.sh cistern-look` starts there: LEVEL7A room 108 at
  43468 -256 62565, yaw 0x905F, with the pool's two crocodiles awake. Draw with
  C, then hold Z and up.
- **Measured at that spot, Look and up held for 90 game ticks:**

  | | Head and torso pitch |
  |---|---|
  | Crocodiles asleep | smooth to -42° and stays |
  | Crocodiles awake | reaches -10° to -14°, snaps to 0°, about every 7 ticks |
  | Crocodiles awake, test build (below) | smooth to -42° and stays |

  Look and back, away from the crocodile, reaches +22° smoothly in all three. The
  user's crocodiles being alive at the report is my reconstruction: their later
  save has both dead.
- **Ours:**
  1. `weaponFindTargets` (`openlara/lara.h:3786`) measures a creature against
     Lara's body plus torso. The crocodile on the pool's floor is about 70° below
     her and the pistols' limit is 60°, so tilting the torso 10° takes it as the
     target.
  2. `weaponTrackTargets` measures from the body alone, so no arm aims.
  3. `updateLook` (`lara.h:2864`) stops at any target, aimed or not, and writes
     head = torso = half the arms' angle, which is 0°.
  4. With the torso back at 0° the crocodile is out of range, the target goes,
     and Look starts again from 0°.
- **Retail:** `LaraGetNewTarget` (`larafire.c:432`) measures against the torso
  too, but only a locked arm moves the head and torso: `PistolHandler`
  (`lara2gun.c:172`) and the rifle's handler (`lara1gun.c:134`) leave them alone
  otherwise.
- **Test:** `if (R.aim || L.aim)` in place of the target test in `updateLook` (a
  test build, not committed) gives the third row above.
- **Fix:** that line. Issues 30 and 31 are about where and how Look runs, and 56
  about the shotgun's torso.
- **Done:** `updateLook` no longer looks at targets. Head and torso move only
  for a locked arm, in `weaponUpdatePistols` and `weaponUpdateShotgun`, as
  PistolHandler and RifleHandler. At `tools/hwtest.sh cistern-look`, with the
  crocodiles awake, Look reaches -42� smoothly. Auto-aim, on the wolves in
  Mednafen: both wolves are found, locked and killed with Action held, with the
  pistols and with the shotgun, and no wolf in plain reach went untargeted but
  for the tick after readying and the tick after a kill.

### 69. Torso and head while aiming: one-frame lag and different formulas
*Closed 2026-11-01: 030af15*

- **Retail:**
  - PistolHandler (`0601f084`) sets the head and torso after AimWeapon, in the
    same frame.
  - With one arm locked it uses that arm's angle / 2. With none locked it leaves
    them alone, and LaraAboveWater's /8 decay applies.
  - UndrawPistols sets them to (R+L)/4. UndrawShotgun halves the torso every
    frame.
- **Ours:**
  - `updateLook` (`openlara/lara.h:2852-2918`) runs before updateWeapon
    (`lara.h:3940` against `3998`), so it uses last frame's arm angles.
  - It uses (R+L) >> 1 when one or no arm is locked.
  - It uses the generic /8 decay during holstering.
- **Done:** ported. Head and torso are set after the arms aim, in the same tick:
  arm / 2 with one locked, (R+L) / 4 with both, untouched with none. Holstering
  and readying set them as UndrawPistols, UndrawShotgun and ReadyPistols. On the
  wolves in Mednafen they matched the formula on all 161 locked ticks.

### 59. Pistol arms follow the torso when not aiming
*Closed 2026-11-01: 030af15*

- **Retail:** DrawLara (`06013438`), in every DRAW, UNDRAW and READY frame,
  draws the pistol arms with the item's matrix copied from two levels back, plus
  the arm rotation and the gun frame.
- **Ours:** `openlara/lara.h:3560` sets `useBasis` only for AIM with frame > 0,
  or for FIRE. The holster keeps whatever value was left.
- **Effect:** when running or jumping with guns drawn but not aiming, and during
  draw and holster, our arms bob with the torso's body animation by about 5°, or
  more while running. Retail's arms stay fixed to Lara's heading.
- **Fix:** always use the basis for pistol-type guns.
- **Done:** `updateWeapon` sets the basis for pistol-type guns in every DRAW,
  HOLSTER and READY frame, so the arms no longer tilt with the torso on Look.

### 56. Shotgun aiming twists the torso twice as far as retail
*Closed 2026-11-01: 030af15*

- **Retail:** RifleHandler (`0601e9a0`) sets torso = left_arm/2 and head 0, and
  only when the arm is locked.
- **Ours:** `openlara/lara.h:2879` sets `T = R.angle`, the full angle, whenever a
  target exists, locked or not.
- **Effect:** with a target 60° to the side, retail's upper body turns 30° and
  ours turns 60°.
- **Done:** torso = arm / 2 and head 0, only while locked (`weaponUpdateShotgun`).

### 70. Targeting origin and range
*Closed 2026-11-01: 030af15*

- **Retail:** LaraTargetInfo and LaraGetNewTarget use a fixed origin at y - 650
  (`-0x28A` in `0601f838` and `0601f9f4`). They measure range from that point,
  per axis first, then squared.
- **Ours:**
  - `openlara/lara.h:3714` and `3756` use the gun height (500 for the shotgun).
  - `lara.h:3770` measures range from Lara's feet, with no per-axis check, so
    the X_SQR sums overflow int32 beyond about 45 blocks and wrap.
- **Effect:** shotgun lock and line of sight differ slightly. In principle, very
  distant enemies could pass the range check.
- **Done:** targets are found and tracked from y - 650 for every gun (shots keep
  the gun's height), with range tested per axis, then squared, and the nearest
  heading chosen as retail does. Ours keeps one check retail doesn't have: the
  item's status must be active.

### 249. Cistern: an invisible wall at a corner of the first pool's rim (reported in play)
*Closed 2026-11-01: 2994412*

- **Seen:** on the Saturn (2026-11-01, main 33102ae): running around the rim of
  the pool and cutting the corner, Lara appears to run into an invisible wall.
  `tools/hwtest.sh cistern-rim` starts there: LEVEL7A room 111 at 39375 -256
  61648, yaw 0x1732; hold forward and she stops at about 39858 62363. Both
  frame orders do it (update on the master and on the slave), along the same
  path.
- **Ours:** at the hit her front probe (39936, 62543) gets WALL. Four rooms meet
  at that corner: 111, 110, 109 and 108. The probe's sector, world (39, 61), is
  open floor at -256 in room 108, but it is the corner sector of room 111, where
  it is a wall with no portal. `Room::getSector` (`openlara/room.h:193`) reads
  it as it stands.
- **Retail:** GetFloor (`0600fcc8`; TR1X `control_util.c:32`) never reads a
  room's corner sector: on the first or last row of z it clamps x to 1 ..
  xSectors - 2. Here that reads world (38, 61), whose portal leads to room 110,
  then room 108, so the floor is found.
- **Fix:** the same clamp in `Room::getSector`. This is the room-edge half of
  issue 137.
- **Done:** `Room::getSector` clamps as GetFloor. From `tools/hwtest.sh
  cistern-rim`, hold forward and she runs on through rooms 110 and 108 to the
  stone block at world (39, 62).

### 248. Cistern: the first pool's tunnel blocks Lara, who keeps splashing "out" while underwater (reported in play)
*Closed 2026-11-01: d3b5617*

- **Seen:** on the Saturn (2026-11-01, main 33102ae): swimming into the tunnel
  in the Cistern's first pool, Lara is stopped, and she keeps playing the
  splash of leaving the water though she is still underwater.
  `tools/hwtest.sh cistern-tunnel` starts there: LEVEL7A room 0, under water at
  43309 1183 53339, yaw 0x7A6A; hold B. The user's save state at that spot takes
  the same path.
- **Ours:** this is issue 34's wading. The tunnel's first sector, world (42, 51)
  in room 51, has its floor at 1280 and ceiling at 768, so `getWaterDepth`
  returns 512, and `c_checkWallUW` (`openlara/lara.h:1420`) switches at 512 or
  less. The loop, about 39 game ticks long:
  1. She enters the tunnel and goes to WADE with anim 192, at y 1080.
  2. Next tick that is under `LARA_WADE_MIN_DEPTH` from the tunnel's ceiling, so
     she goes to ABOVE.
  3. Collision puts her back in room 0, about 100 units back. Room 0's water
     level is 0, so she is deep: `updateWaterState` puts her UNDER, 100 lower, at
     pitch -45°, with `fxSplash`. That is the splash.
  4. Her yaw is 5° less each time round.
- **Retail:** TR1X's `LaraSwimCollision` (`laraswim.c:262`) has no depth check.
  Room 51 is flagged as water, so she would stay under.
- **Test:** with only that branch off (a test build, not committed) she swims
  from room 0 into room 51 and on to the wall at z 51501, UNDER all the way.
- **Not run:** builds before 33102ae. The branch has been there since the
  vendored OpenLara, 9c4257c, and it doesn't depend on which CPU runs the update.
- **Fix:** issue 34. Only this one branch has been tried off so far.
- **Done:** issue 34's fix. From `tools/hwtest.sh cistern-tunnel`, hold B and she
  swims into room 51 and on to the wall at z 51501, under water all the way.

### 34. TR2-style wading is live in TR1, with an animation from another model
*Closed 2026-11-01: d3b5617*

- **Retail:** TR1 has no wading state (LaraControl `06020144`). Shallow water
  rooms are swum through normally.
- **Ours:**
  - `c_checkWallUW` (`openlara/lara.h:1415-1431`) switches to WADE when the
    water is 512 deep or less, and calls `animSet(ANIM_SWIM_STAND = 192)`.
  - Lara's anims are 0–159 (model 1 starts at 160), so anim 192 belongs to
    another model.
  - `updateWaterState` (`lara.h:2934-3058`) has more WADE branches.
- **Where:** water 512 deep or less is in Tihocan rooms 80, 99, 103 and 105
  (depths 0, 256 and 512), in Atlantis room 21 (512), and in the Cistern's
  tunnel, room 51 (512; issue 248).
- **Effect:** swimming there flips Lara into WADE and out of the water, then
  loops back to underwater with a splash. Issue 248 measures it.
- **Fix:** remove every WADE path for TR1.
- **Done:** every WADE path is gone. `updateWaterState` follows LaraControl: the
  room's water flag alone decides, and leaving water also resets the gun status,
  head and torso. In Mednafen: a jump into the Cistern's pool, up to the surface
  and climbing out all work, and she swims through Tihocan's room 103. The
  remaining water states keep their numbers, as saved games hold them.

### 195. A zero-length sample plays several seconds of garbage
*Closed 2026-10-31: 53ae8fa*

- **Ours:** a zero-length sample sets LEA to 0xFFFF (`saturn/sound.cpp:173`), and
  plays about 6–12 s of garbage. Such samples exist in CUT2's 108–115, though
  they're unreachable today.
- **Fix:** guard against it.
- **Done:** `sndPlaySample` plays nothing for a zero-length sample.

### 176. Retail's sample alias table isn't reproduced, so doors and others play a click
*Closed 2026-10-31: 53ae8fa*

- **Retail:** `DAT_060631d8` redirects a sample to another sound's sample when
  both sounds are in the level. It is applied in S_SoundPlaySample (`0603c478`)
  before the rate and halving lookups.
  - The redirects are 14v0 → 14v1, 117 → 153, 170 → 70, 99 → 70, 136 → 70,
    172 → 82 and 64 → 63.
  - Each redirected source in the level data is a 16-byte placeholder: the bytes
    "SAMPLE  " plus an index and a size.
- **Live aliases in the data:**
  - 64 → 63 (door → block scrape): LEVEL2 through LEVEL10C. Doors (models 57–63)
    use SFX 64 in their animations.
  - 14v0 → v1 (bear snarl): LEVEL1 and 2.
  - 99 → 70: LEVEL4, 6 and 10C.
  - 117 → 153: LEVEL6.
  - 136 → 70: LEVEL10C.
- **Ours:** `tools/sfx.py:255` skips the placeholders but keeps their 16 bytes,
  so `sound.cpp` plays 8 loud 16-bit words: a click.
- **Effect:**
  - Every door open and close clicks instead of scraping.
  - Half of the bear snarls click.
  - Earthquake/explosion 99, the Torso landing (136) and the Midas raising block
    (117) click.
- **Fix:** in `sfx.py`, point the source's offset entry at the target sample, but
  only when both sounds exist in that level.
- **Done:** `sfx.py` gives each alias's placeholder a copy of the sample
  it plays (the offsets table can't share one).

### 175. About 56 sound effects play about 10.7 semitones too high and end early
*Closed 2026-10-31: 53ae8fa*

- **Retail:**
  - The sample loader (LoadSoundFX `06036ec0`) fills a per-sample table at
    `DAT_060a0e7c` (8 bytes each: bits, rate, halve, alias) from lists built into
    0MAIN.
  - S_SoundPlaySample (`0603c478`) picks a base pitch word from the words at
    `0x06063b10`:

    | Word | Octave / FNS | Rate | Used for |
    |---|---|---|---|
    | 0x1400 | -3 / 0 | 5512.5 Hz | the default |
    | 0x1050 | -4 / 0x50 | 2971.6 Hz | variant 0 of every sound in the rate list at `0x0606324a` |
    | 0x1800 | -2 / 0 | 11025 Hz | SFX 58, the bat (list at `0x06063246`) |

  - The rate list is 3, 11–16, 18, 60, 63, 65–76, 82, 83, 85–93, 100–102,
    104–107, 129, 130, 132, 133, 135, 144, 145, 148–150, 154, 163–166 and 168.
  - Only variant 0 gets the special rate; variants 1 and up stay at 5512.5.
- **Evidence:** in LEVEL1, LEVEL2, LEVEL5 and LEVEL10B, each rate-list sample's
  length divided by 2971.6 Hz matches its PS1 duration. For example, SFX 11 gives
  0.72 s against the PS1's 1.66 s cut, and SFX 66 gives 0.85 s against 1.02 s.
  At 5512.5 Hz they come out about half the PS1 length. Sounds not on the list
  (0, 4, 8) match at 5512.5.
- **Ours:**
  - `tools/sfx.py:4` assumes every Saturn sample is 5512.5 Hz.
  - Correlating against the PS1 samples at 2:1 can't match these, so they keep
    the Saturn data.
  - `saturn/sound.cpp:69` (`pitchOctFns`) then plays them at octave -3.
  - The bat (58) plays an octave below retail.
- **Effect:** chipmunked and short:
  - creatures: the bear, T-rex, raptor, lion, gorilla, crocodile, mummy,
    Atlanteans and the skater;
  - the block push/pull scrape (63), chain doors, rock falls, the pendulum;
  - lava, fire, the altar, and the underwater woosh (60).
- **Fix:** either carry a rate in the offsets table (`sound.h`), play at 2971.6
  or 11025 Hz, and compute `voice.end` from the real rate; or have `sfx.py` match
  these against the PS1 samples resampled by 11025/2971.6, so they become 8-bit
  11025 Hz PS1 samples like the rest.
- **Done:** the offsets table carries every sample's rate; `sfx.py` finds
  the slow samples (made at 2500 Hz: the original plays them 1.19 times as
  fast as recorded) in the PlayStation's band-limited to 2500 Hz and packs
  the PlayStation's as 8-bit at 13103 Hz while sound RAM lasts. The bat
  keeps its own 16-bit sample, now at 11025 Hz.
- **A deliberate difference since bb07d5f** (the user's choice, 2026-10-31):
  every sample is the PlayStation's, whole, which the sound CPU decodes
  (`saturn/sound.68k`), at its own 11025 Hz: PC/PlayStation pitch, so the
  rate-list sounds play about 3 semitones below retail Saturn. Only samples
  the PlayStation has none of (the cutscenes) keep the original's, the
  rate-list ones at the 2500 Hz they were made at.

### 174. The underwater "woosh" (SFX 60) is never played (reported in play)
*Closed 2026-10-31: 53ae8fa*

- **Retail:** at the top of CalculateCamera (`0600d128`), every frame, before
  the cinematic branch:
  - if the camera's room (`DAT_06074d7c`) is underwater, it calls
    Sound_Effect(60, NULL, 2), where flags 2 means "always";
  - otherwise it calls Sound_StopEffect(60, NULL).
- **How retail repeats it:** SFX 60 is mode 0 (WAIT), volume 0x7FFF, with no
  wibble, and it is on the 2971.6 Hz rate list. It isn't a hardware loop. Each
  call is ignored while it plays, and it keys on again once it ends, so it
  repeats with up to a one-frame gap.
- **Ours:** `SND_UNDERWATER` is defined (`openlara/common.h:2030`) but never
  used.
- **Fix:**
  - At the start of `Camera::update()` (`openlara/camera.h:488`), before the
    cutscene and free-camera returns:
    `if (ROOM_FLAG_WATER(view.room->info->flags)) soundPlay(SND_UNDERWATER, NULL);
    else soundStop(SND_UNDERWATER);`
  - The existing engine handles a call every frame correctly. `sndPlaySample`
    finds the voice with the same index, rewrites TL and returns. `sndFrame`
    frees it at `voice.end`, and the next call keys it on again. That is retail's
    retrigger.
  - Two conditions:
    1. The sample must play at 2971.6 Hz (issue 175), and `voice.end` must use
       that rate. Otherwise it's chipmunked and keyed off at 54%.
    2. Opening the inventory should stop it (issue 188).
- **Done:** `Camera::update()` plays it while the camera's room is water,
  stops it out of it; with 175 it plays at the original's rate (the
  PlayStation's sample, 8-bit, at 13103 Hz).
- **Since bb07d5f** (the user's choice, 2026-10-31): the whole PlayStation
  sample at its own rate (11025 Hz, PC/PlayStation pitch), as with every sound
  (see 175).

### 150. No fades when the inventory opens and closes
*Closed 2026-10-31: 61534f6*

- **Retail:** `0603a9dc`, `0603aaf0`, `0603ab38`.
  - On open, it copies the last VDP1 frame into a VDP2 bitmap, dims it with
    colour offset −64 over 15 vblanks, and fades the ring layer in over 5.
  - On close, the ring dims out and the scene brightens over 30 vblanks.
- **Ours:** `saturn/render.cpp:1515`, `2570` redraw the paused scene dimmed by
  gouraud −8/31 instantly. The amount of dimming matches; there is no fade.
- **Done:** as retail: the scene drawn once and held in VDP1's draw buffer,
  copied into NBG0 by SCU DMA, the fades on VDP2's colour offsets A (the
  picture) and B (the ring); the inventory's frames draw only the ring
  (Mednafen ledge: 30 fps, render 10.6 ms, VDP1 6.7 ms).

### 140. Inventory items are lit flat; retail uses directional gouraud (reported in play)
*Closed 2026-10-31: 61534f6*

- **Retail** (TR1X `inventry.c` Display_Inventory sets g_LsAdder; `invfunc.c`
  Inv_RingLight sets g_LsDivider and the light). On Saturn: Display_Inventory
  `06017420`, Inv_RingLight `0601a1e8`, and the vertex-light routine near
  decomp.c:44310.
  - g_LsDivider (`DAT_06063c28`) = 0x6000.
  - The light vector is phd_GetVectorAngles(-1536, 256, 1024), then
    phd_RotateLight (`0603ce34`), computed every frame after the view is built.
  - Each vertex's shade = LsAdder + ((n · (Mᵀ·L)) / 0x6000 >> 16), clamped to
    0..0x1FFF. That swings ±2731 across a mesh.
  - g_LsAdder (`DAT_06063c24`) is set per item before it draws:
    - 0x1400 for the current item while the ring is still (idle, selecting,
      selected, deselecting);
    - 0x1800 for every other item, and for the current item while the ring
      turns.

    These are 0x400 darker than PC's 0x1000/0x1400.
- **Ours:** `openlara/inventory.h:1395` calls `calcLightingStatic(255 << 5)`.
  That gives `gLightAmbient` 4064 and `gLightRange` 0, so every vertex gets one
  flat shade (about 15 of 31). No directional shading, and no highlight on the
  current item.
- **Effect:** retail items have a lit side and a dark side, the other items sit
  noticeably darker, and the current item stands out. Ours are uniformly bright.
- **Fix:**
  - In `drawPage()`, after `matrixSetView`, set
    `gLightView = gViewRotation × (13504, -2251, -9002)`. That is
    −unit(−1536, 256, 1024) << 14, negated because our meshxform shade is
    `ambient − (n·l)·range >> 14`. Use the same maths as calcLightingDynamic.
  - In `drawSlot()`, replace calcLightingStatic with
    `gLightAmbient = (current && state != INV_STATE_SPIN) ? 0x1400 : 0x1800;
    gLightRange = 2731;`.
  - Inventory meshes are packed with normals (`pack_level.py:556`), so the
    existing normal path applies.
- **Done:** as the fix above, the light handed to the pose with each
  matrix the inventory draws under (`render.cpp` renderModelUnder,
  `pose.cpp` poseMatrices).

### 246. A switch can't be pulled after the last level left its slot a timer
*Closed 2026-10-31: 6739a04*

- **Retail:** InitialiseItem (TR1X `items.c`) sets every item's timer to 0
  as the level loads.
- **Ours:** the `ItemObj` constructor (`openlara/item.h`) left it, so a slot
  kept the timer of the last level's item there. LEVEL1's item 28 is a timed
  door (a pad opens it for 2 s) whose timer ends at -1; LEVEL2's item 28 is
  the switch in room 21. Its first update after a pull saw -1, sent the
  lever back to where it stood and left the switch busy: Lara pulled, the
  lever didn't move, the trigger never fired, the door stayed shut, and the
  switch refused every pull after. Every time on reaching LEVEL2 from
  LEVEL1; never on a level loaded on its own (seen on the Saturn and in
  Mednafen).
- **Fix:** the constructor sets the timer to 0. Mednafen, slot 28's timer
  -1 before a load: stuck before, the door opens after.

### 242. Step 8 doesn't fit high work RAM without moving three of TRS's buffers
*Closed 2026-10-30: done in step 8 (stage B, 3861082 on; stage C,
b94b382); every level fits, four with some rooms' faces left in low work RAM
(244)*

- **Moves:** to low work RAM, the three of TRS's buffers no frame reads:
  `gTextures` (25 KB: the level's file in low work RAM holds the entries
  already, so read them there; the new renderer reads them at load and on
  the careful split path only), `gPalettes` (45 KB: swapped into VRAM at
  load and at the player's pick) and `gItemData` (72 KB: copied to VRAM
  once as the inventory opens, then by the CPU, since SCU DMA can't read
  low work RAM).
- **Room for them:** TRS loads each level's meshes and rooms into low work
  RAM and copies them to `gMeshData` and `gRoomData`, which leaves up to
  213 KB of dead copies (LEVEL7B, which has 1.9 KB free today). The packer
  writes the meshes (in the new formats) and the rooms' placed vertices and
  faces as their own block, loaded straight to high work RAM.
- **The renderer at the game's limits** (in the audit's sums already): room
  materials for 288 textures (GYM has 279; the lab 512), a mesh's held
  faces 256 (the largest mesh has 163; the lab 512), the rooms' vertices
  from 0 (the lab keeps the master's 2048 below them for its scenes'
  copies).
- **In reserve**, the renderer's own levers if the budget tightens: face
  sets in low work RAM (60 KB at LEVEL7B; the cost unmeasured: TRS's meshes
  ran a quarter slower from there), face sets at 8 bytes a face (20 KB;
  corners scaled from bytes, roughly 0.3-0.6 ms on the master), the meshes'
  parts folded into their headers (about 10 KB), the draws' limit measured
  in play (208 bytes a draw at 256: about 20 KB at 160), the rooms
  projected in place over the DSP's rotated vertices (25 KB; the chunks'
  claims make it a design job).
- **Code:** the old renderer's objects (40 KB) and the game's drawing the
  new replaces (pose, light, walk, draw list in main.cpp: 6 KB) go; the new
  renderer's code is 43 KB, so 0MAIN.BIN shrinks 3 KB (189 to 186), 7 with
  the polygon hunt (4 KB), which reads the old renderer's faces. The debug
  menu's rendering options go with the old renderer: their code is inside
  it, and their menu rows are about 2 KB of what's left. Some of
  render.cpp (HUD, text and sprite calls) stays as calls into the new, and
  the switch adds glue for the game's calls: give or take a few KB.
- HW_LOG builds (the trace, the bench, the profiler) have 13 KB less free
  than the game as it ships; the audit measures the shipping build.
- **As done:** the colour tables' other versions and the inventory's item
  textures load into low work RAM, and the texture entries are read where
  the level file holds them. The level's high work RAM is an arena from the
  program's bss to the master's stack (282 KB as it ships, after the item
  records' 14.6 KB went in 7097ee9), taken in order at load: the mesh
  store, every room's vertices, the renderer's tables, then the rooms'
  faces as they fit (`os.cpp`).
- `tools/ramaudit.py` now measures the game as it loads each level (558510a):
  the arena fills at LEVEL6 (62 of 79 rooms' faces placed), LEVEL7A (95 of
  139), LEVEL7B (104 of 114) and LEVEL10B (83 of 101); every other level
  places all of them, with 2.6 KB (LEVEL10A) to 271 KB left. Low work RAM is
  tightest at LEVEL7B: 1.2 KB left.

### 243. Animated textures: the new renderer's materials are made once
*Closed 2026-10-30: 893462f*

- TRS shifts the texture entries along each animated range every few
  frames (openlara level.h `animTexturesShift`). The new renderer makes its
  room materials (`RoomMaterial`, with its per-frame memo of the cache's
  character) and its mesh materials (keyed by texture and parallelogram)
  once, at load, so they don't follow.
- No scene covers it: each is one captured frame. **Fix:** shift the
  materials with the entries (a mesh texture can own two materials), and a
  scene of an animated range over several frames.
- **As fixed:** room textures are the only ones animated ranges hold (the
  packer puts them first, with fixed indices; meshes use object textures),
  so the mesh materials don't move. The room materials shift with the
  entries (`frameTexturesShifted`, from level.h `animTexturesShift` through
  `renderTexturesShifted`). A material is made from its texture's entry
  alone, so shifting it is making it again, and its memo of the cache goes
  with the texture it is of; no lab scene was needed for that.
- Seen in the Gym, Lara above the pool: before, the surface's textures
  stood at the step they loaded at (the same picture each time the range
  came round, different in between); after, they follow the old renderer's.

### 239. The new renderer's RAM in TRS isn't budgeted yet
*Closed 2026-10-30: measured (tools/ramaudit.py); what it found is 242 and
243*

- `tools/ramaudit.py` reads TRS's map, the lab's buffers and code (sorted
  into the renderer's and the lab's own) and every level's packed file, and
  prints high and low work RAM after the switch, level by level.
- As the lab stands, the switch leaves high work RAM 96 KB short at
  LEVEL7B. TRS as it ships has 42 KB free; the old renderer frees 349 KB
  (buffers 203, code 46, its polygon hunt 4, `gMeshData` 96); the new takes
  488 KB (buffers 225 at the game's limits, made at load 57, code 43,
  LEVEL7B's meshes in the new formats with their materials and parts 153,
  the walk's tables 9).
- The new formats cost the meshes: LEVEL7B's 88 KB become 121 (face sets
  take 12 bytes a face against TRS's 6; meshrot.dsp's groups pad to four
  vertices), plus materials 18 and the meshes' parts 15.
- Every room's vertices must sit in high work RAM: roomrot.dsp reads them
  by DMA, which can't reach low work RAM. 47 KB at most (LEVEL7A), so
  `gRoomData` takes every room's vertices first, then faces as they fit.

### 240. The new way's two lists split where the capture's did
*Closed 2026-10-30: each frame's split from its own faces*

- Each frame splits the command slots from its own draw list and walk
  (`newSplit`, once the slave has walked): the draws can make a command a
  face at most (their meshes' and shadows' faces, their copies, the fills),
  the rooms about one a face. When both fit, the draws' list is as long as
  the draws' commands at most and the rooms' list takes the rest; when they
  don't, each list gets its share of the faces.
- The first try, the draws' bound and the rooms the rest, ran the rooms out
  at the ledge (the wolves' and Lara's back faces count in the bound): the
  share fixed it. All eight scenes exact.
- The frame starts its lists on those slots without emptying their buckets
  again (`facesListStart`: the join before left them empty).

### 237. No captured scene has a fill past the draw distance
*Closed 2026-10-30: scene fill*

- A search of LEVEL1's side portals with the lab's walk found views that
  reach past the draw distance; Lara started in room 15 (18432,4352,46592,
  yaw 0) gives the capture `fill` (one fill, 873 commands). The frame makes
  its fill from the walk, exact against the game's list.
- It found two of the lab's own faults, fixed: stages 2+3 run on the scene's
  draws dropped the capture's copies of its fills, and the frame's pose
  check read the scene's transforms a fill early on a draw list.

### 226. The new face stage doesn't apply the draw order hints
*Closed 2026-10-30: lab/hints.cpp, a0a4c24*

- Both ways apply `saturn/depth_hints.txt` as render.cpp does: the game's
  way holds and places faces with render.cpp's own logic, now checked in
  the buckets the capture left them in; the new way's draw list records the
  calls whose thing may be hinted, holds those meshes' named faces after
  `facesMeshAsm` and places them once the room they go behind is projected.
- Scene `hints` (the depth cases' feet spot) moves 2 icicle faces; exact on
  six scenes, mutations caught, the chains' surgery checked at random
  (`facesChainCheck`).

### 227. Sprites and portal fills are still C in the new face stage
*Closed 2026-10-30: lab/facesother.s*

- Sprites, rectangles and lines (renderRect), the walk's fills and voids go
  straight to their commands in assembly (`facesOtherAsm`); `facesOther`
  stays as its C model. Exact on four scenes; `facesOtherCheck` compares
  both on made-up faces of every kind with paths counted (five mutations
  caught: a clamp, the skip test, the flip mask, a bucket's first, a
  rectangle's corner).

### 235. The frame's rooms walk holds up the DSP's start
*Closed 2026-10-30: the slave walks, while the master makes the draw list*

- The frame's slave walks the rooms, makes their jobs and starts the DSP on
  them in walk order while the master makes the draw list; the master's path
  no longer has the walk on it. Mednafen, ledge: the master waits 0.004 ms
  for the walk.

### 236. The frame's plan runs between frames, untimed
*Closed 2026-10-30: the frame plans after its walk, timed*

- `stagesTogether` (lab.cpp) makes the draw list, the meshes' jobs and the
  plan (the slave's items, the master's rooms' chunks) inside the measured
  frame, after the slave's walk. The DSP's room order stays the walk's (the
  slave has started it before the plan). Mednafen, ledge: the plan 0.18 ms,
  its predictions now kept by the jobs and rooms as they are set (each
  mesh's price once, at load).

### 228. Stage 2, stages 2+3 together and pose have no Saturn numbers yet
*Closed 2026-10-30: measured on the Saturn, 19bcbaf on the card (ledge)*

- Every check exact on the hardware: pose, light, statics and shadows (made-up
  passes too), stage 2's vertices, the new list, stages 2+3 together, the
  frame's transforms. Both lists draw the same frame on the TV.
- The game's way, one CPU: pose 2.56 ms, light 1.25, statics and shadows
  0.44, rooms seen 0.66; stage 2 21.64; stage 3 31.11; stages 1-3 57.00
  (Mednafen 50.31).
- The new way: pose 1.65, light 0.75, statics and shadows 0.23; stage 2
  together 8.50 (Mednafen 7.21); stages 2+3 together 19.28 (15.62); the frame
  so far 21.10 (16.46).
- Saturn ÷ Mednafen: the game's code ×1.09-1.27, the new code ×1.17-1.5
  (writes cost more on the hardware), the frame ×1.28. Findings filed under
  225, 220 and 234.

