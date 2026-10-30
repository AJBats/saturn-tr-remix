# Retail audit: our build vs retail Saturn Tomb Raider (2026-10-29)

> The live, numbered list with full details is [OPEN_ISSUES.md](../OPEN_ISSUES.md).
> Each issue there carries the audit ID used here. This file is the audit's
> snapshot.

Every place found where our build (at a98d9fa) plays, looks or sounds
different from retail Saturn TR1 (T-7910H). The audit was prompted by four
things noticed in play: the underwater woosh, unshaded inventory items, the
camera and the save crystals. The divide table was a fifth.

## Method

- **Retail is the truth**: the Saturn 0MAIN decomp (`build/ghidra/decomp.c`).
  Function addresses are in [retail-anchors.md](retail-anchors.md).
- **Readable map**: TR1X 2.0 (`external_resources/TR1X_2.0`, git tag 2.0, not
  committed). It is a decomp of PC TR1 whose files mirror Core's originals.
  - Only the original code paths count; its `g_Config` fixes are ignored.
  - Where PC and Saturn differ, Saturn wins.
  - The modern TRX checkout (`external_resources/TRX`) is not faithful. Some of
    its TR2 behaviour leaked into `openlara/object.h` (LVL-20).
- **How it was found**: nine parallel read-only audits, one per area. Nothing was
  run in an emulator.
  - "Saturn ✓" means the retail side was read in the Saturn decomp.
  - "PC" means TR1X only.
  - Line references are at a98d9fa.
- **Checked by hand afterwards**:
  - the 256 px focal length (`common.h:2276`, `render.cpp:319`)
  - triggers leaving hidden items hidden (`room.h:1033`, `item.h:1058`)
  - mirrored trapdoor sectors (`item.h:801` against TR1X `OnTrapDoor`)
  - the doubled light radius (`trsat.py:236`)
  - inverted block gravity (`object.h:1219`)
  - the timer/radius union (`common.h:1736` with `room.h:1010`)
  - the local ammo copy, and the revive cheat (`lara.h:3930`)
  - retail's sample pitch words and rate list in 0MAIN's data
- **Codes**:
  - Severity: **G** gameplay, **V** visible or audible, **S** subtle.
  - Fix size: s / m / l.
  - IDs (CAM-1, AI-3, ...) are for referring to findings later.

## The five that prompted the audit

1. **Underwater woosh (SND-11).** At the top of every camera update, retail
   CalculateCamera (`0600d128`) calls `Sound_Effect(60, NULL, always)` when the
   camera's room is underwater, and `Sound_StopEffect(60)` when it isn't. Ours
   never plays sound 60.
   - Sound 60 is a WAIT sound, not a loop: calls while it plays are ignored,
     and it keys on again when it ends.
   - Fix: one call in `Camera::update` (`camera.h:488`), placed before the
     cutscene return. Our voice code already handles a call every frame.
   - It needs SND-1 too. Retail plays sound 60 at 2971.6 Hz and we play it at
     5512.5, so ours would come out chipmunked and cut off at 54%.
2. **Inventory gouraud (INV-1).** Retail lights ring items with a directional
   light from (-1536, 256, 1024), rotated by the view each frame. The divider
   is 0x6000, giving a ±2731 swing across a mesh.
   - The adder is 0x1400 for the current item while the ring is still, and
     0x1800 (darker) for every other item.
   - Ours: one flat shade, `calcLightingStatic(255 << 5)` at `inventory.h:1395`.
   - Fix (s): set `gLightView` to the view-rotated (13504, -2251, -9002) in
     `drawPage`, then per item `gLightAmbient = 0x1400` or `0x1800` and
     `gLightRange = 2731`. Inventory meshes already carry normals.
   - Several items are also posed wrong when selected (INV-2), and the compass
     needle never moves (INV-3).
3. **The camera (CAM-1 to CAM-5).** Five differences each change the feel:
   - **The field of view is about 69° wide where retail's is 80°.** Our
     projection is a fixed 256 px focal length; retail's is 209.75. Everything
     is drawn 22% bigger, so it feels zoomed in.
   - The chase camera catches up 1/8 of the way per tick; retail's catches up
     1/12.
   - Our chase camera always looks 10° down; retail's pitch is 0 plus Lara's.
   - Walls are handled by OpenLara's own scheme instead of TR1's slide along
     box edges.
   - Our look camera sits at half retail's distance.

   19 of the 101 retail fixed cameras glide into place; ours all cut (CAM-8).
4. **Save crystals (LVL-30, INV-15/16).** Disabled at `object.h:404`. The full
   retail behaviour is under "Save crystal" below. The bigger change is the save
   model itself: the retail Saturn passport has **no Save page**. You save at a
   crystal, or at the "Save Game?" prompt after each level's stats screen. Ours
   is the GBA model: save anywhere from the passport, with no prompt.
5. **The divide table (MATH-2).** Retail projects with the SH-2 hardware
   divider and is effectively exact.
   - Our `divTable[z >> 4]` errors, for a vertex 176 px off centre:

     | Depth z | Error |
     |---|---|
     | 80–128 | up to 34 px (mean 14) |
     | 128–256 | 21 px |
     | 256–512 | 11 px |
     | 512–1024 | 6 px |
     | beyond 2048 | 2–4 px |

     Near walls and floors wobble by these steps as z crosses multiples of 16.
   - On cost, "it doesn't save us anything" holds for the CPUs. Retail's own
     scheme (three divides a vertex) is about 75 cycles a vertex slower than our
     lookup. But one reciprocal divide started after z and read about 30
     instructions later costs only +3 to +10 cycles a vertex. That is roughly
     0.2–0.6 ms a frame at 2000 vertices.
   - The DSP has no divider, so `project.dsp` still needs a table: a finer one
     indexed by z >> 2, with 32-bit entries.

## Progression blockers (fix before anyone plays past Caves)

- **LVL-1 Items hidden until triggered never appear.** This applies to anything
  that isn't a creature.
  - The trigger only sets the item active when its status is 0
    (`room.h:1033-1037`). Items that start hidden (`item.h:1058`) stay
    invisible, so they are neither drawn nor touched. Retail sets them active
    unconditionally (`06010350` case 0).
  - Natla's Mines: fuse 42 never appears, so the fuse puzzle can't be finished.
    The pistols (82) and the boat (2) never appear either.
  - Atlantis: the Uzis (37).
  - Qualopec: the ceiling slabs fall invisible and harmless.
  - Folly: blocks 19 and 20.
  - Fix: s.
- **LVL-2 Great Pyramid: the Scion can't be destroyed, so the only exit never
  opens.**
  - The SCION_TARGET item (145) is mapped to `ViewTarget`, which is invisible
    and can't be targeted (`item.h:1642`).
  - Retail: 5 HP. On death it runs the heavy trigger under itself, which opens
    door 116 (the only exit), starts the earthquake and track 16, and fires the
    lightning. It also spawns an explosion every 10 frames for 3 s, with sound
    104 and camera bounce -200 (`06026640`).
  - The explosion effect isn't implemented either.
  - Fix: m.
- **LVL-3 + LVL-4 + LVL-1: Folly's Thor room can't be finished**, and each of
  the three is enough on its own.
  - The hammer tests its heavy trigger at the head's position instead of the
    handle's (`object.h:1824`).
  - The pushable blocks' gravity test is inverted, `pos.y > floor`
    (`object.h:1219`), so blocks pushed off a ledge hover. Upstream has the same
    bug.
  - Blocks 19 and 20 start hidden (LVL-1).
  - Fix: s each.
- **AI-3 Tihocan (likely):** Pierre drops only the scion (`enemy.h:2445`).
  Retail also drops the magnums and Key 1 (`06028ef0`). The level has two Key 1
  keyholes and only one placed key, so the second keyhole likely can't be
  opened. Fix: s.
- **AI-4 Atlantis (likely):** the doppelganger mirrors about the centre of
  her start room instead of the fixed point (36864, 61440). She lands about 15
  sectors off.
  - Her fall never runs floor triggers, and the heavy trigger under her landing
    spot is the only thing that opens door 31 (`06020e84`;
    `enemy.h:897-917`).
  - Fix: s–m.
- **LVL-5 Trapdoors support the wrong second sector (mirrored).**
  - `item.h:801-805` against retail `OnTrapDoor` (`0602fadc`): Lara falls
    through the far half of a closed trapdoor and walks on air beside it.
  - Places: Vilcabamba, Khamoon (9 trapdoors), Atlantis, Great Pyramid.
  - Fix: s.

## Camera (CAM)

- **CAM-1 V: the field of view is 69°×47° against retail's 80°×56°.**
  - Ours: focal 256 px (`common.h:2276-2286`, `render.cpp:319-323` and `2431`,
    `render.h:166`, `xform.s:167`, `meshxform.s:167`, `project.dsp`). Near
    plane 64.
  - Retail: phd_InitWindow `0603cf1c` (352×224, near 80, far 20480, FOV 80°),
    AlterFOV `06039b34`, focal `phd_persp` 209.75.
  - Fix (s–m): a projection-only reciprocal, 53504/i. Keep `divTable` at
    65536/i, because `GET_FRAME_T`, `moveTo` and `matrixLerp` use it.
  - **This is a decision; see "Your calls" below.**
- **CAM-2 V: the chase camera moves `>> 3` per tick (1/8, rounding toward
  minus infinity) against `/12`.**
  - Ours: `camera.h:283-288`, `387`. Retail: ChaseCamera `0600ca08` and
    MoveCamera `0600c054`.
  - Ours is about 50% stiffer: it trails less and swings wider. Fix: s.
- **CAM-3 V: default chase pitch.** `CAM_ANGLE_FOLLOW` -10° is applied when the
  target pitch is 0 (`camera.h:378-380`). Retail resets elevation to 0 each
  frame and adds Lara's x_rot; only specific states set one. Ours sits about 267
  units higher, looking down. Fix: s.
- **CAM-4 V: walls.**
  - Ours (`getBestLocation`, `camera.h:209-273`): traces to the ideal spot, then
    falls back to world-axis directions measured with `fastLength`, then clips
    255 from walls per axis.
  - Retail: SmartShift `0600c50c`, ShiftCamera `0600c394`, ClipCamera
    `0600c2e8`, BadPosition `0600c4a4` and ShiftClamp `0600cb20`. They work on
    the pathfinding boxes and slide the camera around Lara along box edges at
    full distance.
  - Fix: m (port them).
- **CAM-5 V: the look camera.**
  - Ours: 768 instead of 1536, with none of the following.
  - Retail: the pivot moves -512·sin(elevation) along Lara's facing, the target
    is ShiftClamped 306 from walls, and the target glides at 1/4 per tick
    (`0600ce74`).
  - Ours, `camera.h:421-432`. Fix: m.
- **CAM-6 G**: Look works in every state and freezes steering. See LARA-2.
- **CAM-7 V: the combat camera** is at 2048 with -10° added; retail uses 2560
  with no offset (`0600cd34`; ours `camera.h:394`, `414`). Fix: s.
- **CAM-8 V: fixed cameras ignore the trigger's glide speed.** Ours always calls
  `move(best, 1)` (`camera.h:443`); retail uses speed (field << 3) + 1. The 19
  gliding cameras are in the Gym, Caves, Vilcabamba, Lost Valley ×3, Qualopec
  ×6, Colosseum, Cistern, Tihocan ×2, Sanctuary ×2 and Mines. Fix: s.
- **CAM-9 V**: after a targeted fixed camera, Look or combat sets `target.y = 0`
  (`camera.h:532`, an upstream bug); retail uses y - 256. Fix: s.
- **CAM-10 V: trigger look-ats.**
  - Ours: ±44° yaw, -42..+22° pitch, 2° a tick, and no decay
    (`camera.h:461-480`, `lara.h:2913`).
  - Retail: ±50°, ±85°, 4° a tick, with a 1/8 decay.
  - Targets above Lara are never looked at. Fix: s.
- **CAM-11 V: shake.**
  - Ours: a vertical sine bob (`camera.h:593-598`).
  - Retail: a bounce. Positive is a one-tick jolt; negative is random x/y/z
    jitter that decays 5 a tick.
  - Retail sources:
    - Earthquake -250
    - Explosion -75
    - Earthquake object -150 or +50
    - Torso landing +500
    - Scion3 -200 every 10 ticks
    - DinoStomp up to +100 within 16 sectors
  - Fix: m.
- **CAM-12 V**: death by the lava wedge has no camera (CHASE_OBJECT, -180°,
  3072; `06030bc8`; ours `object.h:1703-1741`). Fix: s.
- **CAM-13 V**: per-frame cutscene FOV is ignored (`camera.h:43-130`). CUT1
  runs 35–80°, CUT2 47–55°, CUT4 47.5–70°. Needs CAM-1's lever. Fix: s.
- **CAM-14 S–V: in-game cinematic timing.**
  - Ours shows frame 0 and runs a tick behind.
  - It ends when the frames run out instead of on FX_LARA_NORMAL.
  - FX_LARA_NORMAL doesn't restore the chase camera or FOV 80.
  - The listener target isn't updated.
  - `camera.h:95-106`, `item.h:396`; retail `0600daa4`, `06015a7c`. Fix: s.
- **CAM-15 V (likely)**: CUT3's flip map is commented out (`camera.h:32`); retail
  flips in `0600d574`. Fix: s.
- **CAM-16 V**: loading a save restores the saved camera
  (`lara.h:4101-4140`). Retail re-runs InitialiseCamera, which starts above
  Lara's head and pulls back. Fix: s.
- **CAM-17 S**: FOLLOW_CENTRE adds the box's X centre as well
  (`camera.h:542-552`). Fix: s.
- **CAM-18 S**: `clip()` moves 2 fixed cameras that sit near walls (Midas,
  Mines; `camera.h:277`). Fixed by CAM-4.
- **CAM-19 S, latent**: the looked-at mark reuses `ITEM_FLAG_ANIMATED`
  (`camera.h:484`, `room.h:895`), the same bit that selects violent mood
  (AI-2). Separate them when fixing AI-2.
- **CAM-20 S**: the inventory's background is drawn without the camera shift
  (`render.cpp:2589`).
- **CAM-21 S**: rounding: `>> 2` / `>> 3` where retail divides by 4 / speed.
- **CAM-22 S**: LOS edge cases: xLOS/zLOS return values and ClipTarget
  (`room.h:808-838`).
- **CAM-23 S**: shake doesn't re-resolve the camera room (`camera.h:593`).
- **CAM-24 S**: RefreshCamera returns at the first non-matching TO_CAMERA
  (`room.h:854-866`).

## Lara: movement, collision, water, controls (LARA)

- **LARA-1 G: buttons and control model.**
  - Retail Method 1: A Jump, B Action, C Draw, X Step Left, Y Look, Z Step
    Right, L Walk, R Roll, START Inventory. Methods 2 and 3 can be picked in the
    options (table `0x060637fc`, `060374b4`).
  - Retail sidestep and roll have their own input bits. Walk+Left/Right is a slow
    turn, and Lara can turn while stepping.
  - Ours (`lara.h:2765-2773`): A Action, B Jump, C Weapon, X Walk, Z Look,
    and Y sets UP|DOWN for roll. L and R are unused.
  - Ours: sidestep is Walk+direction. Our roll acts as "forward" while hanging,
    crouching or swimming.
  - Fix: m.
- **LARA-2 G: Look.**
  - Retail reads it only in Stop (`0601b2e4`) and Surface-tread (`06021514`).
  - Ours turns the head in every state and strips the d-pad
    (`lara.h:2852-2918`, `744`). Holding Look stops a run and freezes steering
    in jumps, swims and hangs.
  - Fix: s.
- **LARA-3 V**: surface-tread look uses the land constants. Retail: 3° a tick,
  ±50° yaw, ±42° pitch, torso = head/2. Fix: s.
- **LARA-4 G**: underwater turning ramps up to 4° a tick; retail turns 6° a tick
  instantly (`06021c9c`; ours `lara.h:585-600`, `3159`). Land turn speed also
  carries over into the water. Fix: s.
- **LARA-5 V**: underwater pitch is clamped to ±85°; retail allows ±100° and
  flips facing past 90° (`lara.h:3162`, `2057`). Fix: s.
- **LARA-6 G (likely)**: TR2-style wading is live.
  - Water 512 or shallower switches to WADE with anim 192, which isn't one of
    Lara's anims (`lara.h:1415-1431`, `2934-3058`).
  - The data has such water in Tihocan rooms 80, 99, 103, 105 and Atlantis room
    21.
  - TR1 has no wading. Fix: s (remove).
- **LARA-7 G: surface swimming.**
  - Ours: an 800-tall box, step-up 384 or 128, and a mid-floor stop only on
    slopes.
  - Ours: climb-out onto ledges up to 316 below the surface.
  - Retail: 700 tall, step-up -100, stop whenever the floor is less than 700
    below, climb-out from -512 to +100 (`0602111c`, `060218e8`).
  - Ours, `lara.h:2069`, `2540`, `3085`, `1438`, `1713`. Fix: s–m.
- **LARA-8 S**: climb-out isn't tested while treading water (`lara.h:2544`).
- **LARA-9 G**: currents don't pull at the surface, and there's no collision
  after a current move (`lara.h:3958`, `3109-3139`). Fix: s.
- **LARA-10 G**: lava doesn't stop a walking Lara; the lava-is-pit flag is a
  TODO (`common.cpp:1509`). Natla's Mines, Atlantis and the Pyramid have
  hundreds of lava sectors. Fix: s.
- **LARA-11 G**: lava floors kill while Lara is hanging or swimming above them.
  Retail kills only when `pos.y == floor` (`room.h:907-913`). Burning is LVL-12.
  Fix: s.
- **LARA-12 V**: Walk+Up while hanging does a handstand; the Saturn never
  selects it (`0601ca00`; `lara.h:2352`). Fix: s.
- **LARA-13 S**: pull-up accepts any ledge closer than 850 either way; retail
  requires 650–850 (`lara.h:2346`).
- **LARA-14 G: sliding.**
  - (a) The angle isn't int16-wrapped. On south-running slopes, headings between
    90° and 180° slide backwards; that's 8–152 slope entries a level.
  - (b) The z tilt never wins over x: 56 slopes in Lost Valley, 25 in
    Qualopec, 18 in Sanctuary.
  - (c) Hitting a wall mid-slide makes Lara stand up.
  - `lara.h:1450-1487`; retail `0601e404`. Fix: s.
- **LARA-15 S**: stepping down while walking back plays anim 62; retail always
  plays 61 (`lara.h:2428`).
- **LARA-16 S**: landing a swan dive takes fall damage; retail's doesn't
  (`lara.h:1505-1531`).
- **LARA-17 S**: the reach grab measures with the new anim's box
  (`lara.h:1669`).
- **LARA-18 G (likely)**: vaulting from standing needs exactly Up+Action and
  nudges Lara 4 units (`lara.h:2242-2252`). Retail vaults only from walk/run.
- **LARA-19 S, small items**:
  - Stop has no ceiling check.
  - Walking into a wall leaves the state as Walk.
  - A sideways wall brush takes 2° off the lean.
  - The underwater clamp restores the old position.
  - With guns drawn, Walk blocks the fast turn.
  - Leaving water doesn't reset the guns or the head.
  - Air refills 10 a frame instead of instantly.
- **LARA-20 G: pushes.** ItemPushLara re-collides the pusher instead of Lara
  (an upstream bug). It always grows the box by Lara's radius, so creatures
  hold her 100 units off (`item.h:1231-1300`; retail `0600edf8`). Fix: s.
- **LARA-21 G**: Jump+Draw while dead revives Lara at full health
  (`lara.h:3930`, `restore()` at `417`). This is an OpenLara cheat. Fix: s
  (remove).
- **LARA-22 S**: Lara's save block omits moveAngle, turnSpeed and swimTimer.

## Weapons and combat (GUN)

- **GUN-1 G**: every weapon has unlimited ammo.
  - `weaponFire` decrements a local copy (`lara.h:3177`), and all ammo is set to
    -1 (`2722-2725`).
  - So there's no empty click and no switch back to pistols. Retail: FireWeapon
    `0601fd2c`; the shotgun uses 6 a shot.
  - Fix: m.
- **GUN-2 G**: every game starts with all four guns, 10 small and 5 large
  medipacks (the GBA demo's inventory) and the shotgun on Lara's back
  (`inventory.h:246-254`, `lara.h:2712`, `2731`). See INV-6.
- **GUN-3 G**: the target is dropped the instant it dies and Lara retargets
  while Action is held (`lara.h:3699`, `3212`). Retail keeps shooting the corpse
  until Action is released (`0601f838`, `0601f9f4`). Fix: s.
- **GUN-4 G: the shotgun runs OpenLara's own animation scheme.**
  - A locked fire cycle is 26 frames; retail's is 33, so ours fires 27% faster.
  - There are no unaim/undraw anims.
  - Holstering plays sound 7 instead of 6 at frame 101.
  - The draw starts at frame 14 instead of 13.
  - `lara.h:3564-3641`, `213`, `3455`; retail `0601ea54`, `0601e76c`. Fix: m.
- **GUN-5 V**: the shotgun twists the torso by the full aim angle; retail uses
  half, and only when locked (`lara.h:2879`). Fix: s.
- **GUN-6 V**: the shotgun's own anim drives Lara's torso (`draw.h:413-421`,
  `538-546`); retail ignores it. Fix: s.
- **GUN-7 G**: the shot's sphere test uses `fastLength` (`lara.h:3230`), so
  targets are 85% of their true area. Fix: s.
- **GUN-8 V**: when not aiming, the pistol arms follow the torso instead of
  Lara's body (`lara.h:3560`). Fix: s.
- **GUN-9 G (likely)**: choosing the current, holstered weapon in the ring draws
  it; retail doesn't (`lara.h:3871`; `06020820`).
- **GUN-10 V**: the stagger lasts 5 frames past contact (`item.h:1279`,
  `lara.h:386-404`); retail staggers only on contact frames.
- **GUN-11 V**: creatures under 256 tall never stagger Lara (rats, bats); see
  `item.h:1251`.
- **GUN-12 S**: dying creatures still stagger Lara (`enemy.h:765`).
- **GUN-13 V**: blood from Lara's hits doesn't move with the target, and the
  mummy doesn't bleed (`enemy.h:745-747`).
- **GUN-14 S**: in a killing shotgun blast, the leftover pellets ricochet
  instead of hitting the body. Fixed by GUN-3.
- **GUN-15 V: enemy gunmen** (Larson, Pierre, cowboy, Kold, skater).
  - Ours: blood at the shooter's own gun, no hit sound 50, no muzzle flash, and
    misses that spark 512–1535 above Lara's feet (`enemy.h:2249-2267`).
  - Retail (`06028778`, `06028890`, `060286e4`): GunHit puts blood on a random
    Lara joint, and GunMiss puts the spark at her floor height ±256.
  - Fix: m.
- **GUN-16 S**: bite blood has speed 0 (`enemy.h:449`).
- **GUN-17 S**: the muzzle flash's roll is fixed per shot; retail re-rolls it
  every frame (`lara.h:3531`; `06013d5c`).
- **GUN-18 S**: pistol draw/holster frames are one off (`lara.h:3310-3395`).
- **GUN-19 S**: head and torso while aiming lag a frame and use other
  formulas (`lara.h:2852-2918`).
- **GUN-20 S**: targeting starts at gun height instead of y-650. Range is
  measured from the feet, and `X_SQR` overflows past 45 blocks
  (`lara.h:3714`, `3756`, `3770`).
- **GUN-21 S**: water rats chirp when shot (`enemy.h:1877`).
- **GUN-22 S**: random numbers are drawn in a different order per pellet.
- **GUN-23 S**: dying leaves the weapon READY (`lara.h:3830`).

## Creatures and AI (AI)

- **AI-1 G: every triggered creature's radius becomes 0.**
  - `radius` shares a union with `timer` (`common.h:1736`), and the trigger
    writes `timer` (`room.h:1010`). It's also stored halved (`enemy.h:35`).
  - Creatures sink half into walls, and pairs of wolves or rats merge.
  - Fix: s.
- **AI-2 G**: violent mood is keyed on `ITEM_FLAG_ANIMATED`, which only the bear
  sets (`enemy.h:195`, `1170`).
  - Retail is also violent for: crocodile, water crocodile, lions and puma,
    raptor, T-rex, water rat, Kold, centaur, mutants, Natla and the torso.
  - Ours stalk instead of charging, and flee at random.
  - Fix: s.
- **AI-3 G**: Pierre's drops (blocker, above).
- **AI-4 G**: the doppelganger (blocker, above). She also has no death anim.
- **AI-5 G: wolf pounce.**
  - Ours: pounce only within 768 and only when Lara is NOT facing the wolf
    (`enemy.h:1040-1049`).
  - Retail (`06032664`): pounce within 1086, or whenever Lara faces it.
  - So our wolves rarely pounce (50 damage) and bite more (100). Fix: s.
- **AI-6 G**: the "Lara is facing me" flag isn't int16-wrapped
  (`enemy.h:177`). It's wrong for 25% of angle pairs, which affects the wolf
  and T-rex choices. Fix: s.
- **AI-7 G**: the water crocodile's vSpeed is 0, so it can't change depth
  (`enemy.h:1560-1569`). That's 7 levels. Fix: s.
- **AI-8 G**: water rats crawl along the pool floor (`enemy.h:702-710`); retail
  swims them toward the surface. Fix: s.
- **AI-9 G**: crocodiles and rats don't switch between land and water forms, and
  a dead water crocodile doesn't float up (`06011698`, `0602ba74`). Fix: m.
- **AI-10 G**: fleeing creatures don't turn back at their flee point
  (`enemy.h:186-193`; `0600a0fc`). Fix: s.
- **AI-11 G (likely)**: the T-rex, centaur and winged mutant ignore BLOCKABLE
  boxes (`nav.h:26`). Lost Valley has 19 of them. Fix: s.
- **AI-12 G**: Natla's aggression is 0, because the field is reused as her stage
  flag (`enemy.h:3140`). She never re-aims her path at Lara. Fix: s.
- **AI-13 G: Natla is missing most of her fight.**
  - Missing: flight, rockets, the 3-shot spread and sound 123.
  - Her shots are instant hitscan at 100.
  - She can be targeted while down.
  - Track 54 doesn't play (`enemy.h:3116-3263`; `0602500c`).
  - Fix: l.
- **AI-14 G: mutants.**
  - The winged one doesn't fly.
  - Shots are instant. Retail's shard (30 damage, speed 250) and rocket (100
    plus splash, speed 220) travel and can be dodged.
  - No exploding death.
  - The shooter's smartness is 0x7FFF instead of 0x2000.
  - `enemy.h:2822-3000`; `06030ffc`. Fix: l.
- **AI-15 G**: centaurs fire an instant rocket and don't burst
  (`enemy.h:3085`, `3039`). Fix: l (shared with AI-14).
- **AI-16 G: the torso.**
  - Missing: the grab kill (special anim, hands busy, camera 2048) and the
    exploding death.
  - It steers at Lara instead of along its path.
  - Its FORWARD state turns, where retail's doesn't (a retail bug).
  - `enemy.h:3378`, `3416`; `06024b34`. Fix: m.
- **AI-17 G**: the T-rex walks continuously; retail stops and goes
  (`enemy.h:2023`). Fix: s.
- **AI-18 G**: Kold halves the linear distance instead of the squared one. He
  hits 57% at 6 sectors where retail hits 38% (`enemy.h:2803`). Fix: s.
- **AI-19 G**: there's no one-Pierre-at-a-time rule, and Pierre survives water
  (`06028ef0`). Fix: s.
- **AI-20 G**: the puma has 40 HP; retail's has 45 (`enemy.h:1581`).
- **AI-21 V**: the bear never eats Lara's body (`enemy.h:155-159`, `1244`).
- **AI-22 V**: the bear's upright walk falls straight back to rearing
  (`enemy.h:1270`).
- **AI-23 V**: the T-rex's head and neck turn about 10° and 5°; retail's turn
  45° each (`enemy.h:1997`).
- **AI-24 V**: the dead T-rex isn't solid (`enemy.h:809-817`).
- **AI-25 V**: the T-rex kill doesn't set hands busy, air or the camera
  (`enemy.h:2048-2072`; `060125f4`).
- **AI-26 V**: bored flyers aim at the floor; the flyer/walker target heights
  are swapped (`enemy.h:335-337`).
- **AI-27 V**: the centaur statue and mutant eggs don't shatter, and the statue's
  centaur gets a random yaw (`object.h:1997-2081`, `enemy.h:29`).
- **AI-28 V**: the gorilla has no vault onto blocks (ApeVault `06029570`), and
  its sidestep differs; retail has a bug there (`enemy.h:1701`, `1759-1825`).
- **AI-29 V: mummies** get a random yaw and take an AI slot, so they can be
  evicted and vanish. Their head turns only conditionally, and they don't bleed
  (`enemy.h:2196-2218`; `06031ba8`).
- **AI-30 V**: bored creatures never turn their heads to Lara
  (`enemy.h:179`).
- **AI-31 S**: blocking is unimplemented (`@TODO blocking`, `enemy.h:150`,
  `184`, `389`). Lara on a block and boxes behind doors are affected. Fix: m.
- **AI-32 S**: when hit while Lara is out of their zone, ours flee 1 time in 16;
  retail always flees (`enemy.h:367`).
- **AI-33 S**: flyers in transit don't lift their target 1024 above boxes
  (`nav.h`).
- **AI-34 S**: there's no random target point when no path exists
  (`nav.h:307`).
- **AI-35 S**: fly checks: no BadFloor fly test, and the ceiling test uses the
  origin instead of the top bound (`enemy.h:457-472`, `676-688`).
- **AI-36 S**: the turn is applied after the move, a one-frame lag of up to 6°
  (`enemy.h:807`).
- **AI-37 S**: wolf/raptor details:
  - they tilt while walking
  - the raptor's roar and the wolf's bite skip the "ahead" check
  - the wolf's bite sets RUN as its goal
- **AI-38 S**: the bear's hurt flag is sticky (`enemy.h:1207`).
- **AI-39 S (likely)**: dead flyers drift toward a stale global waypoint
  (`enemy.h:657`).
- **AI-40 S**: the water crocodile's smartness is 0x2000; retail's is 0x400
  (`enemy.h:1450`).
- **AI-41 G: AI distances use `fastLength`** (up to 11.8% over;
  `enemy.h:174`, `313`). Every range triggers up to 10% closer on diagonals.
  - Enemy gunfire's hit chance also uses it, plus a truncated R/0x7FFF
    (`enemy.h:2237`, `2252-2255`). At 5 sectors on a diagonal, hit chance is
    0.123 where retail's is 0.240.
  - Fix: s.

## Level logic, traps, objects (LVL)

LVL-1 to LVL-5 are the blockers above.

- **LVL-6 G**: boulders never reset on antipad (Mines 33 and 35, Atlantis 127;
  `object.h:1553-1563`; `0602f33c`). Fix: s.
- **LVL-7 G**: centaur statues wake on proximity instead of on their trigger,
  aren't solid and don't shatter (`object.h:2006`; `06032454`). Fix: s.
- **LVL-8 V**: mutant eggs throw no shell shards and aren't solid
  (`object.h:2028-2079`). Fix: m (needs the body-part effect).
- **LVL-9 V: lightning** (`object.h:1903-1916`; `0602281c`).
  - Nothing is drawn: neither the emitter mesh nor the bolt.
  - The Pyramid's strike radius should be 1024, not 2560.
  - Thunder plays only on the strike; retail also plays it on strike-off.
  - Lara doesn't flinch.
  - Fix: m.
- **LVL-10 V**: waterfall mist is missing (Lost Valley 12, Mines 1;
  `object.h:231-236`; `0601586c`). Fix: s.
- **LVL-11 G: lava emitters emit nothing**: no blobs (10 damage each), no sound
  149, and they aren't solid.
  - Mines 6, Atlantis 7, Pyramid 13; `object.h:239-244`; `060309b8`.
  - Fix: m.
- **LVL-12 G: Lara never catches fire.**
  - Retail flames attach to her within 300: 5 HP a frame and looped sound 150,
    until she reaches water (`0603071c`). LavaBurn puts 10 flames on her
    (`060308b0`).
  - Ours only deals proximity damage (`object.h:1928-1945`, `room.h:905-913`).
  - Fix: m.
- **LVL-13 G: doors.**
  - No collision push; it's a TODO (`object.h:279`).
  - Closed doors don't block in flipped rooms, also a TODO (`object.h:292`).
    Many doors are affected: Cistern, Khamoon, Atlantis and others.
  - They don't block AI through the portal sector (`object.h:303`); 68 of 93
    doors sampled.
  - Fix: s–m.
- **LVL-14 V: scions.**
  - Qualopec and Sanctuary: the pickup cinematic is rotated 90°
    (`object.h:602`).
  - Atlantis holder: Lara stands at -612 instead of -407. Our scion is removed
    and counted as a pickup; retail's stays in the holder (`object.h:574-594`).
  - The holder (147) has no class, so it isn't solid.
  - Fix: s.
- **LVL-15 V: Midas.**
  - Using the lead bar starts the 500-frame death cinematic; retail has none
    (`object.h:843`).
  - The death cinematic uses yaw 0 (`object.h:808`).
  - A wrong item plays "no" (`object.h:847`).
  - The hand's placeholder mesh is drawn; retail draws nothing there.
  - Fix: s.
- **LVL-16 V: flip effects** (`level.h:322-418`; table `0x06060c64`).

  | Effect | Retail | Ours |
  |---|---|---|
  | Raising block | sound 117 | missing |
  | Drop sand | sound 161, then moving 118 for 4 s | missing |
  | Chain block | sound 173, then 33 at frame 55 | missing |
  | Flood | moving, positioned 81 for 4 s | one positionless loop that never stops (SND-9) |
  | Stairs to slope | sound at frame 5 | at once |
  | Explosion | bounce -75 | 30-frame shake |
  | Earthquake | sounds at 0, 3, 20, 35, 50, 70 | 0, 3, 18, 33, 48, 69 |

  Fix: s.
- **LVL-17 S**: a flip with no effect cancels the running one
  (`room.h:1198`).
- **LVL-18 V**: anim effect 14 (draw right gun) is missing (`item.h:416`), so
  Lara has no gun in CUT4.
- **LVL-19 S**: DinoStomp is a 15-frame shake; retail's is a one-frame bounce
  (`level.h:330-350`).
- **LVL-20 S**: the Pyramid earthquake object has the wrong sounds and odds, and
  TRX's TR2 behaviour (waking flame emitters and falling ceilings) leaked in
  (`object.h:2164-2189`; `06026800`).
- **LVL-21 S**: the underwater switch reaches z +1024 instead of +512, and can be
  pulled again mid-animation (`object.h:32-35`, `493-520`).
- **LVL-22 S: keyholes.** A used hole gives no "no", the "no" repeats while
  Action is held, and our check needs the plain stand anim
  (`object.h:715-745`).
- **LVL-23 G**: an item chosen in open space stays armed and auto-inserts at the
  next hole. See INV-7.
- **LVL-24 S (PC)**: block push and pull ignore static meshes
  (`object.h:1092-1133`).
- **LVL-25 S**: the rolling block has extras retail lacks, and the flip doesn't
  move rolling-block floors.
- **LVL-26 S**: the cabin isn't solid and sets its flip once
  (`object.h:2111`).
- **LVL-27 S (PC)**: slope rounding differs by 1 unit, `getSector` clamps
  differently, and the static collision radius lacks retail's +50
  (`room.h:49-83`, `196`, `282`).
- **LVL-28 S: small trap differences.**
  - The blade doesn't push while inactive.
  - Teeth make 2 blood splats; retail makes 6.
  - The dart ricochet lasts 4 frames; retail's lasts 6.
  - Damocles recomputes its floor every frame.
  - Thor's kill check has an extra height test.
  - Flame emitter teardown doesn't stop sound 150.
- **LVL-29 S**: the Gym ending waits 90 trigger hits; retail waits 150
  (`lara.h:4192`).
- **LVL-30 G: save crystal (item 83)**, hidden at `object.h:399-411`. The retail
  behaviour (`0602ba1c`, `0602ba34`, `0602ba5c`, `0602b940`):
  - **Init**: added to the active list at level start. Every level except the
    Gym has 1–7.
  - **Control**: one looping anim, 60 frames at rate 8. It spins about Y and
    bobs between about -268 and -404 above the item.
  - **Draw**: the plain animated-item routine. The mesh is a 5-vertex,
    6-triangle bipyramid lit by its normals; there's no glow.
  - **Collision**, every frame:
    1. Solid push.
    2. Then require Action pressed, guns holstered, on the ground, and Lara
       standing still.
    3. Turn the crystal to Lara's yaw, and test her position with the pickup
       bounds (x ±256, y ±100, z -256..+100, pitch ±10°).
    4. If she passes, hide the crystal, then build the save, so the save
       already has it used.
    5. Run the "Save Game?" YES/NO prompt and the backup write (`0603c1bc`).
       Success shows "Save Complete!" for 60 vblanks.
    6. On success it's removed for good. On NO or a failed write it becomes
       active again.
  - No Lara animation and no sound.

## Inventory, HUD, saves, game flow (INV)

- **INV-1 V**: lighting (see the top).
- **INV-2 V**: wrong selected poses (`inventory.h:110-137`). Retail's values
  are in its item structs at `0x06061b40-0x06062280` (same as TR1X invvars.c):

  | Item | Retail (pre, x, y, dist) | Ours |
  |---|---|---|
  | Compass | 4352, 8192, 0, 456 | 3641, 12743, 0, 512 |
  | Shotgun | 3200, 0, 8192, 296 | 3094, 3641, 0, 296 |
  | Lead bar | 3616, 8160, 4096, 352 | Y 67°, 256 |
  | Keys, puzzles, scion | 7200, 4352, 0, 256 | 3641, 2731, 0, 296 |
  | Controls | 5504, -1536, 0, 352 | X 67° |

- **INV-3 V: the compass.**
  - The needle never moves. Retail: `speed = speed*19/20 + (int16)(-lara.y_rot -
    item.y_rot - needle)/50` (`0601880c`).
  - Our A closes the compass and the ring. Retail ignores A; B plays the anim
    forward to the end (`060272ec`).
  - Fix: s–m.
- **INV-4 V: medipacks.** Retail always shows the HUD health bar while a medipack
  is current, and the count only when above 1. Ours shows both only above 1,
  with the bar in the wrong place (`inventory.h:1398-1418`).
- **INV-5 V**: there are no ammo counts ("%5d A/B/C") and no ammo-box items in
  the ring. Needs GUN-1.
- **INV-6 G: the starting inventory is the GBA demo's and is never reset.**
  - Ours sets it once (`inventory.h:231-258`, `game.h:117`). Keys and puzzles
    carry into later levels and survive a new game.
  - Retail rebuilds weapons, ammo and medipacks from per-level start info every
    level. A new game has pistols only (`06019ca8`, `0602c0ec`, `0602c20c`).
  - Fix: m.
- **INV-7 G**: an item chosen away from a hole stays armed and auto-inserts
  later (`inventory.h:1010-1028`, `object.h:721`). Retail clears its chosen
  item every frame. Fix: s.
- **INV-8 V: the options ring.**
  - Retail in game: Game (passport), Gamma. At the title: Game, Gamma,
    Controls, Gym. There's no Sound or Detail.
  - Gamma is a VDP2 colour-offset slider in steps of ±8. Controls cycles
    Control Method 1/2/3 over a pad diagram.
  - Ours: Passport, Controls, Detail, Sound, plus Home at the title. Gamma,
    VSync, Rumble and Swap are dead stubs (`inventory.h:452-465`,
    `1262-1278`, `os.cpp:95-101`).
  - Fix: m.
- **INV-9 V: labels.** Retail says "OPTION" where ours says "OPTIONS", "Gym"
  for Lara's Home, "GAME OVER" as the death ring's title, and spells it
  "Saphire Key".
- **INV-10 V**: Obelisk puzzle 3 is "Seal of Anubis", not "Eye of Horus"
  (`inventory.h:633`, an upstream bug).
- **INV-11 S: buttons.** Retail: A and C select, B goes back, only START closes
  the ring, and B at the title returns to Press START.
- **INV-12 S (likely)**: the key ring pre-selects the matching key
  (`inventory.h:551`).
- **INV-13 S**: retail fades the background: dim over 15 vblanks with the ring
  fading in over 5, brighten over 30 on close. Ours is instant.
- **INV-14 S: ring motion.** The current item should stop spinning while L/R is
  held, and old items should unwind. There should be a deselect sound 112, and
  the passport should close forward.
- **INV-15 G: passport and saves.**
  - Retail pages: Load Game / Restart Level (Start Game at the title) / Exit to
    Title. There is no Save page.
  - Load Game opens "Select Level": every level reached, plus Current Position.
    Choosing a level restarts it from its start inventory, with its FMV
    (`06026a90`, `06016a58`).
  - Ours: Load/Save/Exit, opening on Save. Load lists Caves, Vilcabamba and
    Current Position. The title passport can't flip (`inventory.h:831-905`,
    `1236-1261`).
  - Fix: l.
- **INV-16 G**: saving happens at crystals (LVL-30) and at the post-stats
  "Save Game?" prompt. A failure says "The file could not be saved! Try
  Again?". Ours saves silently, and on failure shows the GBA SRAM text
  (`inventory.h:879-896`). Fix: m.
- **INV-17 G**: ours lets you save while dead (`inventory.h:843-866`). Fix: s.
- **INV-18 V: backup RAM.**
  - Retail asks "System or Cartridge?" and has not-ready and insufficient-memory
    (55 blocks) messages (`0603b76c`).
  - Retail keeps one file, TOMBRAIDER_ (0xD7C bytes with a checksum).
  - Ours: internal memory only, two files, and settings are rewritten on every
    toggle (`backup.cpp:53-66`, `os.cpp:30-77`).
  - Fix: m.
- **INV-19 G**: the bonus game after finishing is missing: all guns with 1234
  ammo, ammo not used up, enemy HP doubled, "BONUS!" (`06016558`). Fix: m.
- **INV-20 G: GAME OVER.**
  - Retail opens it on its own, 300 frames after death (or 60 plus a button).
    The ring holds Game and Gamma, with the passport open on
    Load/Restart/Exit, and it can't be backed out of (`0600f66c`).
  - Ours opens only on START, holds the normal options, and B goes back to the
    dead game (`lara.h:3937`).
  - Fix: s–m.
- **INV-21 V: health and air bars.**
  - Retail: top-left, health at (32, 28) and air at (32, 16), 104×9, with its
    own colour rows (`06015e50`, `0603b524`).
  - Ours: top-right, 102×7, darker and bluer (`draw.h:979-995`,
    `render.cpp:2544-2568`).
  - Fix: s.
- **INV-22 V**: there's no HUD ammo counter ("%5d A/B/C" at (256, 64),
  `06015f8c`). Needs GUN-1.
- **INV-23 V**: there's no pickup notification. Retail shows the item's sprite
  bottom-right for 75 frames, up to 3 at once (`060161d0`, `060160f8`). Fix: m.
- **INV-24 V**: the FPS counter is on by default (`game.h:113`).
- **INV-25 V: no Press START screen or demo mode.**
  - Retail: USATIT with "Press START button". After 600 idle vblanks it plays
    attract demos, alternating Vilcabamba and Lost Valley from DEMODATA, with
    "DEMO PLAY" on screen (`060334d0`, `06011d48`).
  - Ours goes straight from the FMVs to the ring (`main.cpp:70-78`).
  - Fix: m for the screen, l for demos, which need deterministic logic
    (MATH-10).
- **INV-26 V: no level stats screen.**
  - Retail shows KILLS, PICKUPS, SECRETS n of m and TIME TAKEN after every level
    but the Gym (`06016558`). For Qualopec, Tihocan, Mines and Atlantis it comes
    after the cutscene.
  - Our timer never counts (`gSaveGame.time`).
  - Fix: m.
- **INV-27 V**: the PRISON FMV and CUT4 play in the wrong order. Retail: PRISON,
  CUT4, stats (`060163d8`; ours `os.cpp:115-150`).
- **INV-28 V: the ending.** Retail: END (unskippable), CRED224 for 15 s,
  CORELOGO, CAFE, Press START; ENDPG224 is never shown. Ours: END, CRED 8 s,
  ENDPG 8 s, ring (`os.cpp:132-139`).
- **INV-29 S: FMVs.** Retail can't skip CORELOGO or END, shows EIDOS for 90
  vblanks (ours 180), and fades pictures over 30 vblanks (`fmv.cpp:265`,
  `screen.cpp`).
- **INV-30 V**: START doesn't skip cutscenes in ours (`0600d744`;
  `camera.h:43-77`).
- **INV-31 V**: there's no A+B+C+START soft reset (`0603938c`).
- **INV-32 S (likely)**: no pause on pad disconnect and no multitap pad scan
  (`06043720`).
- **INV-33 S (likely)**: retail's level-skip code is missing. In the inventory,
  enter Z Y Z Y X X X START; Exit to Title then completes the level
  (`06018bf8`).
- **INV-34 S**: retail plays medipack sound 116 on select, positionless and
  halved (`06026880`).
- **INV-35 S**: selecting Gamma in game exits the inventory.
- **INV-36 S**: the ring camera is 598 from the ring at 80° (ours 768 at focal
  256).

## Audio (SND)

- **SND-1 V: about 56 sound effects play about 10.7 semitones too high and end
  early.**
  - Retail picks a base pitch per sample from tables built into 0MAIN
    (`0603c478`, `06036ec0`): words `0x1400` / `0x1050` / `0x1800` at
    `0x06063b10`, i.e. 5512.5, 2971.6 and 11025 Hz.
  - The rate list at `0x0606324a` plays these sounds' variant 0 at 2971.6 Hz:
    3, 11-16, 18, 60, 63, 65-76, 82, 83, 85-93, 100-102, 104-107, 129, 130,
    132, 133, 135, 144, 145, 148-150, 154, 163-166, 168.
  - The bat (58) plays at 11025 Hz.
  - Ours plays them all at 5512.5 (`tools/sfx.py:4`, `sound.cpp:69`), so these
    don't match the PS1 samples and keep the Saturn data.
  - Affected: bear, T-rex, raptor, lion, gorilla, crocodile, mummy, Atlantean,
    skater, block scrape, doors, lava, fire and the underwater woosh.
  - Fix: m. Either carry a per-sample rate, or match these against PS1
    resampled by 11025/2971.6.
- **SND-2 V: retail's sample alias table** (`0x060631d8`) isn't reproduced, so
  its 16-byte placeholder samples play as clicks.
  - Doors 64 should use 63, the block scrape (LEVEL2-10C).
  - Bear snarl 14 variant 0 should use variant 1.
  - Also 99→70, 117→153, 136→70, 170→70 and 172→82.
  - `tools/sfx.py:255`. Fix: s.
- **SND-3 V**: retail's per-sample volume-halving table (`0x06063218`) is
  missing. Ring rotate 108, controls 110, medipack 116 and the rumble 147 are
  about 11 dB louder than retail. Fix: s.
- **SND-4 V: underwater/surface gating** is missing (`0600f894`,
  `06020540`).
  - Retail: with the camera underwater, land sounds are silent; with it dry,
    underwater sounds and bubbles are silent.
  - Lara's anim sounds, the woosh and the menus always play.
  - This gating is the whole "underwater" audio effect; there's no muffle.
  - Ours: `soundPlay` has no flags (`item.h:29`, `361`). Fix: m.
- **SND-5 V: no stereo pan.**
  - Retail: DIPAN from the angle to the source relative to Lara's yaw plus her
    head and torso, updated every frame (`060236e0`, `06046b28`). It has a
    wrap quirk at -0.
  - Ours is always centred (`sound.cpp:187`).
  - Fix: s–m.
- **SND-6 V: volume curve.**
  - Retail: TL = (0x10000 - (0xC0 + vol >> 9)²) >> 9, a 0 to -21 dB range
    (`06046d60`).
  - Ours is amplitude-linear down to -30 dB or below, and drops volumes below
    512 (`item.h:73-78`, `sound.cpp:52`). So far sounds fade out completely.
  - Fix: s.
- **SND-7 V**: ours wobbles pitch ±10%. Retail's random pitch is effectively
  inaudible (±0.4%), so repeated sounds are identical (`item.h:80-84`).
  Fix: s.
- **SND-8 V: the slot model.**
  - Retail: 24 slots keyed by (sound id, position pointer), 8 reserved for
    loops. Volume and pan are updated every frame from the position, with 32
    voices (`06023af4`, `06023c7c`).
  - Ours is keyed by sample index with 16 voices and no updates. Two wolves
    share a voice, and moving sources don't fade or pan.
  - Fix: m–l.
- **SND-9 V**: looped sounds never stop.
  - The flood loops at full volume until level end (`level.h:353`; Lost
    Valley, Cistern, Tihocan).
  - A removed flame's sound 150 keeps playing (`object.h:1960`).
  - The last emitter sets the volume, not the nearest.
  - Retail stops any loop nobody played that frame. Fix: m.
- **SND-10 V: room sound sources are never played.** They're packed and loaded
  (`phd.h:560`) but never used. Retail plays them every frame (`06014fbc`):
  waterfalls in Lost Valley, Qualopec, Tihocan and the Mines, and lava in
  Atlantis (38 sources) and the Pyramid (8). Fix: m.
- **SND-11 V**: the underwater woosh (see the top).
- **SND-12 V: missing sound calls.**
  - Splash 33 on water entry (`item.h:695`).
  - Block landing 70 (`object.h:1228`).
  - Lara hit by a bullet, 50.
  - Natla's shot 123.
  - Missile impacts.
  - Scion destruction 104.
  - Body-part explosions 104.
  - Lava fountain 149.
  - Lara burning 150.
  - Raising block 117.
  - Drop sand 161/118.
  - Chain block 173/33.
  - Inventory deselect 112.
  - Bubbles 37 (`item.h:403`).
- **SND-13 S**: exploding deaths play 171. Retail calls 172, which is absent or
  a click in those levels, so retail is silent (`enemy.h:2868`, `3043`,
  `3321`).
- **SND-14 V: track 5 in the Caves-area levels.**
  - Ours plays track 5 at load in LEVEL1, 2, 3A and 3B, and restarts it whenever
    the CD is idle, including after triggered tracks and in the inventory
    (`common.cpp:46-50`, `game.h:258`, `294-297`).
  - Retail plays it only from its floor triggers, once.
  - Fix: s.
- **SND-15 V**: opening the inventory doesn't stop the sound effects or pause the
  CD. Retail resumes the CD at 0xA0 (`06017420`; `inventory.h:520`). Fix: s.
- **SND-16 V**: music keeps playing after Lara dies; retail stops the CD.
  Fix: s.
- **SND-17 V**: loading a save replays the saved track from the start
  (`game.h:37`, `92`). Retail stays silent until the next trigger. Fix: s.
- **SND-18 V: cutscene audio.** Retail waits for the CD to report PLAY before
  starting (`0603c794`). CUT4's track restarts for its last 3 s
  (`game.h:294`). Fix: s.
- **SND-19 V**: Natla's track 54 and the skater's track 56 are missing
  (`0602500c`, `06029ac0`). Fix: s.
- **SND-20 S**: retail never resets the current track when a track ends, so the
  same trigger doesn't replay. Ours re-triggers non-oneshot tracks on re-entry:
  LEVEL1 9, LEVEL4 7, LEVEL8B 3/4/5, LEVEL10B/10C 8 (`game.h:295`,
  `sound.cpp:234`).
- **SND-21 S**: medipack sound 116 is positionless, and the water rat's chirp is
  wrong (GUN-21).
- **SND-22 S**: a zero-length sample gives LEA 0xFFFF and plays garbage; add a
  guard (`sound.cpp:173`).

## Numeric primitives and timing (MATH)

- **MATH-1 V**: the field of view. See CAM-1.
- **MATH-2 V**: the projection divide table. See the top.
- **MATH-3 S: other approximate divides.**
  - Interpolated bounds (`item.h:199`), root lerp (`draw.h:343`, `559`) and
    `matrixLerp`'s 8-bit weight (`common.cpp:1234`) are off by 1 unit in 25–50%
    of cases.
  - The alignment step uses `FixedInvU` (`item.h:536-544`), so alignments can
    end a tick off.
  - Retail divides exactly (`06014e84`, `0600f504`). Fix: s.
- **MATH-4 S: animation interpolation.** Ours lerps relative matrices,
  lerp(P·RA, P·RB). Retail keeps two full stacks, lerp(PA·RA, PB·RB)
  (`06014a60` and neighbours; `common.cpp:1428`). That's about 1% of a bone's
  length on fast motion. Fix: m.
- **MATH-5 S: sin/cos rounding.** Retail folds the angle into 0–90° before
  `>> 4`. Ours always rounds down (`common.h:2771-2777`).
  - For 92.6% of angles, sin or cos is one step off.
  - Movement steps differ on 2.7–9% of headings, so positions drift.
  - Fix: s.
- **MATH-6 G: `fastLength`** (max + min/2, up to 11.8% over) where retail
  squares exactly: AI ranges (AI-41), Lara's shot test (GUN-7), enemy hit chance,
  and the camera (CAM-4).
- **MATH-7 G: angle differences not wrapped to int16.**
  - TestLaraPosition (`object.h:102-104`): Lara can use switches, keyholes,
    puzzle holes and blocks with her back to them, for 90–150° of headings.
  - Also AI-6 and LARA-14.
  - Fix: s each.
- **MATH-8 S**: the catch-up cap is 10 ticks; retail's is 10 vblanks, i.e. 5
  ticks (`0600f66c`; `main.cpp:119`, `common.h:469`). This only matters below
  6 fps. Fix: s.
- **MATH-9 V/G: 10 ticks run at once after every load, save load and cutscene
  start.**
  - `lastTick` predates the CD load, so 0.33 s of game time passes in one frame
    (`game.h:323-333`, `main.cpp:119`).
  - Cutscene pictures likely lead their audio by about 0.27 s.
  - Retail starts with 1 frame and zeroes the vblank count (`0601636c`,
    `0603ab38`).
  - Fix: s.
- **MATH-10 S: the RNG.**
  - Ours seeds both streams from `osGetSystemTimeMS()`, which returns 0
    (`os.cpp:23-26`), at every level start. Both seeds are saved in and
    restored from saves (`game.h:248-249`, `38-39`, `95-96`).
  - Retail seeds 0xD371F947 once at power-on, keeps the state across levels,
    and doesn't restore it on load (`060168f8`, `06016940`).
  - Also: the enemy-miss ricochet uses the logic stream, and our shake uses no
    random numbers at all.
  - Fix: s.
- **MATH-11 S**: `anglesFromVector` lacks retail's shift-down guard, and its
  pitch is 1 unit low (`common.cpp:1068`).
- **MATH-12 S (likely)**: `getFrames` has an extra no-interpolation branch
  (`item.h:157-160`).

## Scene look: lighting and effects (GFX)

- **GFX-1 V: light falloff is doubled.**
  - `tools/trsat.py:236` stores `radius * 2`, copied from OpenLara's SAT reader.
    Retail uses the raw value (`060388ec`), which is identical to the PS1's.
  - So lights reach twice as far: Lara and enemies look brighter and more
    side-lit everywhere.
  - The int32 square overflows in 11 rooms, giving blow-outs or inverted
    falloff: LEVEL2 71, 82; LEVEL3A 1, 47, 86; LEVEL3B 1; LEVEL5 42; LEVEL7A 6,
    32; LEVEL7B 28; LEVEL10A 37.
  - Fix: s. Drop the ×2 and pack `radius >> 7`.
- **GFX-2 V**: item and effect sprites darken with depth fog; retail's only fog
  through a room vertex (`render.cpp:2445`; `06038aac`). Pickups, blood and
  splashes go black at range. Fix: s.
- **GFX-3 V**: room sprites aren't dropped within 640 units (`draw.h:830`;
  `0603feda`). Foliage draws huge in front of the camera: Caves 43,
  Vilcabamba 81, Lost Valley 30. Fix: s.
- **GFX-4 V: shadows.**
  - Retail sizes by type: Lara 160; most creatures 128; crocodiles, centaurs and
    mutants 85; the T-rex 64; the Damocles sword 256. Mummies and cutscene actors
    cast none.
  - Ours uses 160 for all of them, including cutscene actors and mummies
    (`draw.h:737`).
  - The swords have no shadow, though in Folly it's the hint where one will
    fall.
  - The doppelganger has none, and the skater casts two.
  - Fix: s.
- **GFX-5 S**: a shadow vanishes when its owner is off-screen; it's drawn after
  the cull (`draw.h:704`).
- **GFX-6 V**: hatched creatures and Thor's hammer head are lit flat
  (intensity 128 instead of -1; `object.h:1980`, `1775`, `item.h:577`).
  Fix: s.
- **GFX-7 S**: the fog clamp darkens the lit side to black; retail keeps a dim
  grey (`draw.h:133`; `060388ec`). Fix: s.
- **GFX-8 V**: the doppelganger is drawn as a plain item: no aiming arms,
  flashes, head/torso turn or shadow. Retail draws her with DrawLara
  (`060210b8`; `enemy.h:875-950`). Fix: m.
- **GFX-9 V**: exploding deaths are missing: bodies blink out, and hatched pods
  keep their shells (`visibleMask 0xFFFFFF` where retail uses 0x1FF;
  `06031c64`). Fix: l.
- **GFX-10 V**: Midas twinkles are missing, both on Lara's hands while she uses
  it and over the gilded meshes during the gold death (`lara.h:1248-1281`;
  `06015380`). Fix: s.
- **GFX-11 V**: no fade from black after a level load, and no fade at level end
  (`0603a480`; `level.h:238-262`, `system.cpp:264`). Fix: s.
- **GFX-12 S (PC)**: the flicker effect's timings are {90, 93, 105, 108, 123};
  retail's are {90, 92, 105, 107, 125} (`level.h:380`).
- **GFX-13 S (likely)**: item sprites are culled to the room's portal window
  and depth-biased by -128 (`render.cpp:2439`, `2462`).
- **GFX-14 S (suspected)**: the object cull sphere, 0.75 × the largest extent,
  is smaller than a box corner's distance (`draw.h:678`).
- Elsewhere in this list: gunmen's muzzle flash (GUN-15), projectiles (AI-13
  to AI-15), the Scion (LVL-2), lightning (LVL-9), lava (LVL-11), mist (LVL-10),
  burning (LVL-12), the flash roll (GUN-17), the near plane (CAM-1).

## Your calls

- **The field of view.** At 80° the frame shows about 1.5× the world area of
  today's 69° view, so more rooms, faces and vertices on the CPU side. It also
  means **every perf comparison with the original so far** has measured a
  narrower view than retail's. Retail-accurate FOV and an honest A/B both point
  the same way, but the bill is real.
- **Retail bugs** (gameplay ones are part of TR1's feel; audio-driver ones are
  not):
  - Gameplay:
    - the gorilla's sidestep turn
    - the torso's FORWARD state that never turns
    - CombatCamera's stale target_square
    - the gym trigger counter that never resets
  - Audio driver:
    - CD music in mono (the right channel is never mixed)
    - looped sounds keyed as 8-bit on 16-bit data, so an octave down
    - the pan wrap at -0
    - ambient loops restarted every frame
    - exploding deaths silent (sound 172 missing)
    - retail's sound sliders do nothing
- **Debug leftovers**: the FPS counter on by default, the Detail/Sound/VSync
  options in the ring, the revive cheat.
- **Features retail has that we never built**: Press START plus attract demos,
  level stats, GAME OVER, the bonus game, the level-skip code, soft reset, and
  Gamma and control-method options.
