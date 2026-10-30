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
| 29-51 | Lara: movement, collision, water, controls |
| 52-73 | Weapons and combat |
| 74-112 | Creatures and AI |
| 113-139 | Level logic, traps and objects |
| 140-173 | Inventory, HUD, saves and game flow |
| 174-195 | Audio |
| 196-204 | Numeric primitives and timing |
| 205-218 | Scene look: lighting and effects |
| 219-239 | Rendering: the new renderer's open work |

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

### 23. FOLLOW_CENTRE also adds the bounding box's X centre
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: CAM-17*

- **Retail:** adds only the box's Z centre, rotated by yaw (bounds +8/+10).
- **Ours:** `openlara/camera.h:542-552` rotates both X and Z.
- **Effect:** push/pull, climbing out of water, and special kill animations.

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

### 26. Camera smoothing rounds with shifts instead of dividing
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: CAM-21*

- **Retail:** target.y smoothing in chase divides by 4; MoveCamera divides by
  speed.
- **Ours:** `openlara/camera.h:560` uses `>> 2`, and `camera.h:285` uses `>> 3`.
- **Effect:** residual offsets of a few units that never settle.

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

### 34. TR2-style wading is live in TR1, with an animation from another model
*Severity: gameplay / visible · Confidence: likely (code path plus data; not
seen in game) · Fix: small · Audit: LARA-6*

- **Retail:** TR1 has no wading state (LaraControl `06020144`). Shallow water
  rooms are swum through normally.
- **Ours:**
  - `c_checkWallUW` (`openlara/lara.h:1415-1431`) switches to WADE when the
    water is 512 deep or less, and calls `animSet(ANIM_SWIM_STAND = 192)`.
  - Lara's anims are 0–159 (model 1 starts at 160), so anim 192 belongs to
    another model.
  - `updateWaterState` (`lara.h:2934-3058`) has more WADE branches.
- **Where:** water 512 deep or less is in Tihocan rooms 80, 99, 103 and 105
  (depths 0, 256 and 512), and in Atlantis room 21 (512).
- **Effect:** diving or swimming there should flip Lara into WADE with a garbled
  pose, then loop back to underwater.
- **Fix:** remove every WADE path for TR1.

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
- Leaving water (underwater or the surface, to air) doesn't reset the gun
  status or head/torso (`lara.h:2994-3024`); retail does (`06020144`).
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

### 56. Shotgun aiming twists the torso twice as far as retail
*Severity: visible · Confidence: confirmed · Fix: small · Audit: GUN-5*

- **Retail:** RifleHandler (`0601e9a0`) sets torso = left_arm/2 and head 0, and
  only when the arm is locked.
- **Ours:** `openlara/lara.h:2879` sets `T = R.angle`, the full angle, whenever a
  target exists, locked or not.
- **Effect:** with a target 60° to the side, retail's upper body turns 30° and
  ours turns 60°.

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

### 59. Pistol arms follow the torso when not aiming
*Severity: visible (subtle) · Confidence: confirmed · Fix: small · Audit:
GUN-8*

- **Retail:** DrawLara (`06013438`), in every DRAW, UNDRAW and READY frame,
  draws the pistol arms with the item's matrix copied from two levels back, plus
  the arm rotation and the gun frame.
- **Ours:** `openlara/lara.h:3560` sets `useBasis` only for AIM with frame > 0,
  or for FIRE. The holster keeps whatever value was left.
- **Effect:** when running or jumping with guns drawn but not aiming, and during
  draw and holster, our arms bob with the torso's body animation by about 5°, or
  more while running. Retail's arms stay fixed to Lara's heading.
- **Fix:** always use the basis for pistol-type guns.

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

### 69. Torso and head while aiming: one-frame lag and different formulas
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: GUN-19*

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

### 70. Targeting origin and range
*Severity: subtle · Confidence: confirmed (the overflow effect is suspected and
rare) · Fix: small · Audit: GUN-20*

- **Retail:** LaraTargetInfo and LaraGetNewTarget use a fixed origin at y - 650
  (`-0x28A` in `0601f838` and `0601f9f4`). They measure range from that point,
  per axis first, then squared.
- **Ours:**
  - `openlara/lara.h:3714` and `3756` use the gun height (500 for the shotgun).
  - `lara.h:3770` measures range from Lara's feet, with no per-axis check, so
    the X_SQR sums overflow int32 beyond about 45 blocks and wrap.
- **Effect:** shotgun lock and line of sight differ slightly. In principle, very
  distant enemies could pass the range check.

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

### 137. Slope rounding, room-edge sectors and static collision radius
*Severity: subtle · Confidence: likely (TR1X GetHeight/GetCeiling; Saturn not
checked) · Fix: small · Audit: LVL-27*

- **Slope rounding:** on one sign of each slant axis (positive for floors,
  negative X and positive Z for ceilings), ours rounds the other way from
  retail. Heights can differ by 1 unit per axis (`openlara/room.h:46-50`,
  `82-83`).
- **Room edges:** `getSector` clamps positions outside the room differently from
  retail's GetFloor, which avoids the corner sectors (`room.h:196`).
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

### 140. Inventory items are lit flat; retail uses directional gouraud (reported in play)
*Severity: visible · Confidence: confirmed · Fix: small · Audit: INV-1*

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

### 150. No fades when the inventory opens and closes
*Severity: subtle · Confidence: likely (which colour-offset layer does what is
inferred) · Fix: small · Audit: INV-13*

- **Retail:** `0603a9dc`, `0603aaf0`, `0603ab38`.
  - On open, it copies the last VDP1 frame into a VDP2 bitmap, dims it with
    colour offset −64 over 15 vblanks, and fades the ring layer in over 5.
  - On close, the ring dims out and the scene brightens over 30 vblanks.
- **Ours:** `saturn/render.cpp:1515`, `2570` redraw the paused scene dimmed by
  gouraud −8/31 instantly. The amount of dimming matches; there is no fade.

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

### 174. The underwater "woosh" (SFX 60) is never played (reported in play)
*Severity: audible · Confidence: confirmed · Fix: small (needs 175) · Audit:
SND-11*

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

### 175. About 56 sound effects play about 10.7 semitones too high and end early
*Severity: audible · Confidence: confirmed (data read from 0MAIN) · Fix: medium
· Audit: SND-1*

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

### 176. Retail's sample alias table isn't reproduced, so doors and others play a click
*Severity: audible · Confidence: confirmed · Fix: small · Audit: SND-2*

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
- **Ours:** `saturn/sound.cpp:187` always writes `7 << 13` (centre).
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
- **Ours:** `saturn/sound.cpp:134-160`.
  - Keyed by sample index only. Two emitters of the same sound share one voice:
    for WAIT and LOOP the second call only rewrites the volume, and for REPLAY it
    cuts the first.
  - Two variants of one WAIT sound can overlap.
  - Volume is never updated after key-on (only when the sound is played again),
    and there is no pan.
  - 16 voices; new sounds are dropped when they're all busy.
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

### 195. A zero-length sample plays several seconds of garbage
*Severity: subtle · Confidence: confirmed · Fix: small · Audit: SND-22*

- **Ours:** a zero-length sample sets LEA to 0xFFFF (`saturn/sound.cpp:173`), and
  plays about 6–12 s of garbage. Such samples exist in CUT2's 108–115, though
  they're unreachable today.
- **Fix:** guard against it.

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

### 226. The new face stage doesn't apply the draw order hints
*Kind: correctness · Fix: medium*

- `saturn/depth_hints.txt` (06107f4) moves some of the master's faces to other
  buckets. The game's way in the lab applies the moves (the scene's MOVE
  chunk); the new way (`lab/facesmesh.s`) doesn't.
- No captured scene has a hinted face yet (ledge, pool, wolves, wolves_aim: 0
  moved), so the exactness checks can't see it.
- **Fix:** capture a scene where hints move faces, then add them to the new way.

### 227. Sprites and portal fills are still C in the new face stage
*Kind: speed (the production standard) · Size: small in the scenes so far ·
Fix: small*

- `lab/faces.cpp` facesOther. The rest of the stage is assembly.

### 229. Vertices near the camera jitter from the divide table
*Kind: look · Size: corner error p99 1.93 → 1.31 px with an exact divide
(crunch lab, ledge) · Fix: medium, after a decision*

- The projection uses `divTable[z >> 4]` (65536 / i in 16 bits), from
  OpenLara's GBA build. The error grows near the camera: about 5 px at z 512,
  176 px off centre. Retail divided each vertex with the SH-2's divider.
- The lab's stages reproduce the game's arithmetic exactly. An exact divide
  changes the output, so the checks would compare against a new reference.

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

### 237. No captured scene has a fill past the draw distance
*Kind: coverage · Fix: small*

- The walk's fills (renderVoid) are exact against the game's walk on
  made-up views (33 fills at the ledge's), and the frame turns them into
  faces before its draws. But none of the four scenes has one, so a fill's
  command in a whole list (the farthest bucket, before the draws' faces) has
  never been checked against the game's.
- About 1 in 30 cameras placed in random rooms of Caves sees one.
- **Fix:** find such a view with the lab's walk, start Lara there
  (LARA_START) and capture it.

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

### 239. The new renderer's RAM in TRS isn't budgeted yet
*Kind: coverage · Size: TRS's high work RAM has ~31 KB free at the ledge
(measured, 2026-10-30) · Fix: medium, before step 8*

- The old renderer's working buffers in TRS: ~222 KB (command list and
  gouraud 70, vertices 40, face pools 60, ordering tables 15, room texture
  cache 14, DSP and jobs 10). The new renderer's in the lab: ~250 KB, sized
  for the lab's maximums (room materials 32 for 512 textures, the walk's
  lists 17 for 128 rooms, mesh and DSP jobs 35 for 256 draws).
- Its level data in its own formats (the DSP's packed meshes, the face sets,
  the room materials) is packed per scene only: never measured for whole
  levels against TRS's gMeshData (96 KB) and gRoomData (128 KB).
- **Fix:** the audit the owner asked for before step 8: every buffer sized
  to the game's real limits, every level's data in the new formats measured,
  against TRS's map, so the switch is known to fit.

---

## Decisions needed before fixing

| Issue | Decision |
|---|---|
| **8** | Retail's 80° field of view puts about 1.5× as much world on screen; that costs CPU. Past perf comparisons with the original measured our narrower view. |
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
| **229** | An exact divide changes the projection's output, so the lab's checks would compare against a new reference. Take it? |

## Closed

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

