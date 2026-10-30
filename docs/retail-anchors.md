# Retail Saturn 0MAIN: function and data map

Names for the unnamed `FUN_`/`DAT_` addresses in `build/ghidra/decomp.c`
(retail T-7910H, loaded at 0x06004000), identified during the 2026-10-29 retail
audit ([retail-audit.md](retail-audit.md)). Names follow TR1X 2.0
(`external_resources/TR1X_2.0`), the readable map of the same Core source:
same constants, same call structure. Found by distinctive constants (SFX,
anim, state and object ids) and by call graph.

## Game loop, system, input

| Address | Name |
|---|---|
| 06032d94 | main / boot |
| 0601636c | GameLoop (starts with nframes 1) |
| 0600f66c | ControlPhase (nframes in vblanks, capped at 10; 0x8000 per vblank into frame_count, one tick per 0x10000) |
| 060127b0 | DrawPhaseGame |
| 06037a8c | S_DumpScreen (returns the 60 Hz count) |
| 0603938c | vblank interrupt (vblank count, soft-reset check) |
| 06005e1e | flip wait |
| 06039c00 | wait N frames |
| 060374b4 | S_UpdateInput (pad to input bits via the control-method table at 0x060637fc) |
| 06043720 | pad scan (digital pad 0x02/0x15, multitap) |
| 060168f8 / 06016930 | Random_GetControl / SeedControl |
| 06016940 / 06016978 | Random_GetDraw / SeedDraw |
| 060457f6 | signed divide (__sdivsi3, hardware divider) |
| 06045a02 + 2n | shift chain, `>> (14 - n)` |

## Maths and 3D

| Address | Name |
|---|---|
| 0603e034 | phd_atan |
| 0603e0b0 / 0603e0a8 | phd_sin / phd_cos (helpers 0603e104, 0603e134) |
| 0603e164 | phd_sqrt |
| 0603c828 | phd_GenerateW2V |
| 0603c97c | phd_LookAt |
| 0603c9ec | phd_GetVectorAngles |
| 0603cb44 | phd_RotYXZ |
| 0603db10 | phd_RotYXZpack |
| 0603ddfc | phd_PushMatrix |
| 0603de4c / 0603dee0 | phd_TranslateRel / phd_TranslateAbs |
| 0603ce34 | phd_RotateLight |
| 0603cf1c | phd_InitWindow (352x224, near 80, far 20480, FOV 80) |
| 06039b34 | phd_AlterFOV |
| 0604127c | mesh vertex transform (3 overlapped hardware divides a vertex) |
| 06041a80 | room vertex transform |
| 06041678 | mesh transform with wibble |
| 060413c2 / 060417e2 | mesh vertex lighting / underwater variant |

## Drawing

| Address | Name |
|---|---|
| 06012d4c | PrintRooms |
| 06037ae8 | Output_DrawRoom |
| 0603feda | room sprite draw (drops z <= 640) |
| 0603fb48 | face dispatch |
| 060130fc | DrawAnimatingItem |
| 060130ac | DrawSpriteItem / pickup item |
| 060130f0 | DrawDummyItem (invisible) |
| 06024ab4 | DrawUnclippedItem |
| 06012f94 | DrawEffect |
| 06013438 | DrawLara |
| 06013ee8 | DrawLaraInt |
| 06013d5c | DrawGunFlash |
| 06013df8 | CalculateObjectLighting |
| 060210b8 | DrawEvilLara |
| 06022358 | DrawLightning |
| 060388ec | Output_CalculateLight |
| 06038a68 | Output_CalculateStaticLight |
| 06038798 | Output_DrawShadow (mesh at 0x0606388c) |
| 06038aac | Output_DrawSprite |
| 06038d08 | Output_DrawSpriteRel |
| 06037d04 | S_GetObjectBounds |
| 060397cc | Output_AnimateTextures (every 10 vblanks) |
| 060373fc | level render init (fade in) |
| 060391a0 / 06039264 | colour-offset fade / per-vblank step |

## Animation

| Address | Name |
|---|---|
| 0600f894 | AnimateItem |
| 06020540 | AnimateLara |
| 06014dc4 | GetFrames |
| 06014e84 | GetBoundsAccurate |
| 06014efc | GetBestFrame |
| 06014a60 | InterpolateMatrix |
| 060148e0 / 060149dc | phd_TranslateRel_I / phd_RotYXZpack_I |
| 060147dc / 060147bc | phd_PushMatrix_I / phd_PopMatrix_I |
| 06014a1c | phd_PutPolygons_I |

## Camera and cutscenes

| Address | Name |
|---|---|
| 0600bfe0 | InitialiseCamera |
| 0600c054 | MoveCamera (bounce) |
| 0600c2e8 | ClipCamera |
| 0600c394 | ShiftCamera |
| 0600c4a4 | BadPosition |
| 0600c50c | SmartShift |
| 0600ca08 | ChaseCamera (speed 12) |
| 0600cb20 | ShiftClamp |
| 0600cd34 | CombatCamera (2560) |
| 0600ce74 | LookCamera (1536) |
| 0600d090 | FixedCamera |
| 0600d128 | CalculateCamera (underwater SFX 60 at its top) |
| 0600d574 | cutscene start (per-CUT position, angle, flip) |
| 0600d744 | DoCinematic (START ends it) |
| 0600d850 | CalculateCinematicCamera |
| 0600daa4 | InGameCinematicCamera |
| 0600d96c | ControlCinematicPlayer |
| 0600d9b8 / 0600da6c | InitialisePlayer1 / InitialiseGenPlayer |
| 06010270 | RefreshCamera |

Camera struct at 06074d70:

| Offset | Field |
|---|---|
| +0x00 | pos (room +0x0c) |
| +0x10 | target (room +0x1c) |
| +0x20 | type (2 look, 3 combat, 4 cinematic) |
| +0x24 | shift |
| +0x28 | flags |
| +0x2c | fixed_camera |
| +0x34 | bounce |
| +0x38 | target_distance |
| +0x3c | target_square |
| +0x40 | target_angle |
| +0x42 | actual_angle |
| +0x44 | target_elevation |
| +0x48 | number |
| +0x4a | last |
| +0x4c | timer |
| +0x4e | speed |
| +0x50 | item |
| +0x54 | last_item |
| +0x58 | fixed[] |

## Lara

| Address | Name |
|---|---|
| 06020144 | LaraControl (water status, air, CD stop on death) |
| 0601af90 | LaraAboveWater |
| 0602111c | LaraSurface |
| 06021ab8 | LaraUnderWater |
| 060209a4 | InitialiseLara |
| 06020d34 | LaraInitialiseMeshes |
| 06020b20 | InitialiseLaraInventory |
| 06020820 | UseItem |
| 06015a7c | LaraNormal (chase camera, FOV 80) |
| 06015380 | ItemSparkle |
| 06062904 / 060629e4 | state control / collision routine tables (56 entries each) |

State handlers:

| Address | Name | Address | Name |
|---|---|---|---|
| 0601b140 | AsWalk | 0601b1d0 | AsRun |
| 0601b2e4 | AsStop (reads Look) | 0601b4ac | AsForwardJump |
| 0601b584 | AsFastBack | 0601b5e8 / 0601b66c | AsTurnR / L |
| 0601b700 | AsFastFall | 0601b754 | AsHang |
| 0601b7a8 | AsReach | 0601b7ec | AsCompress |
| 0601b8f4 | AsBack | 0601b994 | AsFastTurn |
| 0601b9e0 / 0601ba68 | AsStepRight / Left | 0601baf0 | AsSlide |
| 0601bb24 | AsBackJump | 0601bbb0 | AsFallBack |
| 0601bbf4 / 0601bc38 | AsHangLeft / Right | 0601bcf4 | AsPPReady |
| 0601be54 / 0601be78 | AsUseMidas / AsDieMidas | 0601c13c / 0601c168 | AsSwanDive / AsFastDive |
| 06021c9c | AsSwim | 06021d58 | AsGlide |
| 06021e20 | AsTread | 06021ed8 | AsDive |
| 06021f00 | AsUWDeath | 0602130c / 06021398 | AsSurfSwim / Back |
| 06021414 / 06021494 | AsSurfLeft / Right | 06021514 | AsSurfTread (reads Look) |

Collision handlers and helpers:

| Address | Name | Address | Name |
|---|---|---|---|
| 0601c1cc | ColWalk | 0601c374 | ColRun |
| 0601c4fc | ColStop | 0601c5cc | ColForwardJump |
| 0601c6a0 | ColFastBack | 0601c77c | ColTurnR |
| 0601c8e4 | ColDeath | 0601c950 | ColFastFall |
| 0601ca00 | ColHang | 0601ca78 | ColReach |
| 0601cb98 | ColCompress | 0601cc38 | ColBack |
| 0601cd54 / 0601ce0c | ColStepRight / Left | 0601cf6c | ColUpJump |
| 0601d034 | ColFallBack | 0601d234 / 0601d304 | ColRoll / Roll2 |
| 0601d418 / 0601d494 | ColSwanDive / FastDive | 0601d554 | ColDefault |
| 0601d594 | ColJumper | 0601d61c | GetLaraCollisionInfo |
| 0601d658 | LaraSlideSlope | 0601d77c | LaraHitCeiling |
| 0601d7ec | LaraHangTest | 0601da6c | LaraDeflectEdge |
| 0601daf0 | LaraDeflectEdgeJump | 0601dc8c | LaraSlideEdgeJump |
| 0601ddd0 | TestLaraVault | 0601dfe8 | LaraTestHangJump |
| 0601e1a0 | TestHangSwingIn | 0601e270 | LaraTestHangJumpUp |
| 0601e404 | TestLaraSlide | 0601e588 | LaraFloorFront |
| 0601e62c | LaraLandedBad | 060217cc | LaraSurfaceCollision |
| 060218e8 | LaraTestWaterClimbOut | 0602201c | LaraSwimCollision |
| 06021fac | ColUWDeath | 06022150 | LaraWaterCurrent |

## Weapons

| Address | Name |
|---|---|
| 0601f3f4 | LaraGun |
| 0601f730 | InitialiseNewWeapon |
| 0601f838 | LaraTargetInfo |
| 0601f9f4 | LaraGetNewTarget |
| 0601fbc4 | find_target_point |
| 0601fc98 | AimWeapon |
| 0601fd2c | FireWeapon |
| 0601fff4 | HitTarget |
| 0601f084 | PistolHandler |
| 0601f200 | AnimatePistols |
| 0601ec7c / 0601ecf4 | DrawPistols / UndrawPistols |
| 0601eedc | ReadyPistols |
| 0601ef74 | DrawPistolMeshes |
| 0601efdc / 0601f030 | UndrawPistolMeshLeft / Right |
| 0601e6f4 / 0601e76c | DrawShotgun / UndrawShotgun |
| 0601e880 / 0601e8c4 | DrawShotgunMeshes / UndrawShotgunMeshes |
| 0601e908 | ReadyShotgun |
| 0601e9a0 | RifleHandler |
| 0601ea54 | AnimateShotgun |
| 0601eb8c | FireShotgun |
| 060286e4 | GunShot (enemy muzzle flash) |
| 06028778 | GunHit |
| 06028890 | GunMiss |
| 060289b8 | ShotLara |

The weapons table (g_Weapons) is at 0x06062814, 0x30 bytes a weapon:

| Offset | Field |
|---|---|
| +0x18 | aim speed |
| +0x1a | accuracy |
| +0x1c | gun height |
| +0x20 | damage |
| +0x24 | target distance |
| +0x28 | recoil frame |
| +0x2a | flash time |
| +0x2c | sample |

## Collision, floor, triggers

| Address | Name |
|---|---|
| 0600dc00 | GetCollisionInfo |
| 0600e2dc | FindGridShift |
| 0600e318 | CollideStaticObjects |
| 0600e844 | ShiftItem |
| 0600e878 | UpdateLaraRoom |
| 0600e8f0 | GetTiltType |
| 0600e9ac | LaraBaddieCollision |
| 0600eb28 | EffectSpaz |
| 0600ebbc | CreatureCollision |
| 0600ec4c | ObjectCollision |
| 0600ecc0 | DoorCollision |
| 0600ed5c | TrapCollision |
| 0600edf8 | ItemPushLara |
| 0600f1bc | TestLaraPosition |
| 0600f324 | AlignLaraPosition |
| 0600f408 | MoveLaraPosition |
| 0600f504 | Move3DPosTo3DPos |
| 0600fb70 | GetChange |
| 0600fc4c | TranslateItem |
| 0600fcc8 | GetFloor |
| 0600fe84 | GetWaterHeight |
| 0600fff0 | GetHeight |
| 060109d4 | GetCeiling |
| 06010350 | TestTriggers |
| 06010958 | TriggerActive |
| 06010ca8 | LOS |
| 060112a8 | FlipMap |
| 06011488 / 060115dc | TriggerCDTrack / apply track flags |
| 0602b848 / 0602b8c4 / 0602b908 | SwitchTrigger / KeyTrigger / PickupTrigger |
| 060249a0 | AlterFloorHeight |
| 06014f30 | ItemNearLara |

## Items and effects

| Address | Name |
|---|---|
| 0601a83c / 0601a870 | CreateItem / InitialiseItem |
| 0601ab6c / 0601aa3c | AddActiveItem / RemoveActiveItem |
| 0601a764 | KillItem |
| 0601aadc | RemoveDrawnItem |
| 0601ac0c | ItemNewRoom |
| 0601aca0 | SpawnItem |
| 0601adf4 / 0601ae50 / 0601aefc | CreateEffect / KillEffect / EffectNewRoom |
| 0602cf08 | object setup (object table) |
| 06015060 | DoBloodSplat |
| 060151f4 | Ricochet |
| 060156e8 | Splash |
| 060154c8 | CreateBubble / LaraBubbles |
| 06031c64 | ExplodingDeath |
| 0602e50c | GetSpheres |
| 060316a0 | ControlMissile |

Flip and anim effect table at 0x06060c64 (16 entries):

| Address | Effect |
|---|---|
| 060159a4 | Turn180 |
| 060159b8 | DinoStomp |
| 06015a7c | LaraNormal |
| 060154c8 | LaraBubbles |
| 06015990 | FinishLevel |
| 06015ac0 | EarthQuake |
| 06015b44 | Flood |
| 06015bbc | RaisingBlock |
| 06015c34 | Stairs2Slope |
| 06015c70 | DropSand |
| 06015ce4 | PowerUp |
| 06015d44 | Explosion |
| 06015d78 | LaraHandsFree |
| 06015d8c | FlipMap |
| 06015da4 | LaraDrawRightGun |
| 06015be4 | ChainBlock |

## Objects and traps

| Address | Name |
|---|---|
| 0602a9fc | PickUpCollision |
| 0602ac10 | PickUpScionCollision |
| 0602ad7c | PickUpScion4Collision |
| 06026618 | ScionControl |
| 06026640 | Scion3Control |
| 0602ae90 | MidasCollision |
| 0602b0d0 / 0602b1fc | SwitchCollision / SwitchCollision2 (underwater) |
| 0602b7ec | SwitchControl |
| 0602b30c / 0602b53c | KeyHoleCollision / PuzzleHoleCollision |
| 0602ba1c / 0602ba34 / 0602ba5c / 0602b940 | save crystal init / control / draw / collision |
| 06025960 / 06025e94 | InitialiseDoor / DoorControl |
| 06026474 | CogControl (also drawbridge and moving bar) |
| 0602f9e8 / 0602fa48 / 0602fa90 / 0602fadc | trapdoor control / floor / ceiling / OnTrapDoor |
| 06024060 / 0602409c / 060241e8 | movable block init / control / collision |
| 06024894 / 060248c0 | rolling block |
| 0602f2f4 / 0602f33c / 0602f590 | rolling ball |
| 0602f868 | spikes |
| 0602fb88 | pendulum |
| 0602fcf8 | falling block |
| 0602fe90 | teeth |
| 0602ff58 | falling ceiling |
| 0603001c / 0603007c / 060301c4 | Damocles sword |
| 060302e4 / 060304b4 | dart emitter / darts |
| 06030670 / 0603071c | flame emitter / flame |
| 060309b8 / 06030aa0 | lava emitter / lava |
| 060308b0 | LavaBurn |
| 06030bc8 | lava wedge |
| 0602281c / 06022888 | lightning |
| 06022bb0 / 06022c3c | Thor's hammer |
| 060264f8 | cabin |
| 060265a8 | boat |
| 06026800 | earthquake object |
| 0601586c | waterfall |
| 06032454 / 0603255c | centaur statue |
| 060321e8 / 0603231c | mutant pod |

The object table (OBJECT_INFO, TR1X layout) is at 0x06089be0 + id·0x34:

| Offset | Field |
|---|---|
| +0x00 | nmeshes |
| +0x02 | mesh_index |
| +0x1c | draw routine |
| +0x26 | hit points |
| +0x28 | pivot |
| +0x2a | radius |
| +0x2c | smartness |
| +0x2e | shadow_size |

## Creatures and AI

| Address | Name |
|---|---|
| 06009900 | InitialiseCreature |
| 06009950 | CreatureAIInfo |
| 06009ee4 | StalkBox |
| 0600a04c | ValidBox |
| 0600a0fc | CreatureMood |
| 0600b000 | CreatureAnimation |
| 0600be48 | CreatureTurn |
| 0600bf08 | CreatureTilt |
| 0600bf40 | CreatureHead |
| 0600bf8c | CreatureEffect |
| 06023068 / 060230c0 | DisableBaddieAI / EnableBaddieAI |
| 06023278 | InitialiseSlot |

| Address | Control | Address | Control |
|---|---|---|---|
| 06009328 | Bat | 0600947c | Bear |
| 06011698 | Alligator (water croc) | 060119e0 | Croc |
| 06011f94 | Raptor | 060122fc | Dino (T-rex) |
| 060125f4 | LaraDinoDeath | 06020e84 | Evil Lara |
| 06024b34 | Abortion (torso) | 0602500c | Natla |
| 06028a58 | Larson | 06028ef0 | Pierre |
| 06029570 / 060296b8 | ApeVault / Ape | 06029ac0 | SkateKid |
| 06029fa8 | Cowboy | 0602a558 | Baldy (Kold) |
| 0602ba74 | Rat | 0602bd64 | Vole (water rat) |
| 06030cf8 | Centaur | 06030ffc | Flyer (mutants) |
| 06031b68 / 06031ba8 | InitialiseMummy / Mummy | 06032634 / 06032664 | InitialiseWolf / Wolf |
| 06032a78 | Lion | | |

## Inventory, HUD, game flow, saves

| Address | Name |
|---|---|
| 06017420 | Display_Inventory (modes 0 game, 1 title, 2 keys, 4 death) |
| 06018518 | Construct_Inventory |
| 060186e0 | AnimateInventoryItem |
| 0601880c | DrawInventoryItem |
| 06018bf8 | inventory input / cheat recorder |
| 06018d2c / 06018ef4 | RingIsOpen / RingIsNotOpen |
| 06018f8c / 06019698 | RingNotActive / RingActive |
| 060196d8 / 06019aa0 / 06019c28 | Inv_AddItem / Inv_InsertItem / Inv_RequestItem |
| 06019ca8 / 06019cd4 / 06019e40 | Inv_RemoveAllItems / Inv_RemoveItem / Inv_GetItemOption |
| 0601a028 / 0601a180 / 0601a1e8 | Inv_RingInit / Inv_RingGetView / Inv_RingLight |
| 0601a230 / 0601a278 | Inv_RingCalcAdders / Inv_RingDoMotions |
| 0601a428 / 0601a46c | Inv_RingRotateLeft / Right |
| 0601a4b0 .. 0601a68c | Inv_RingMotion* family |
| 06026880 | Option_DoInventory |
| 06026a90 | Option_Passport |
| 06027014 | Option_Gamma |
| 06027114 / 060271c4 | Option_Detail / Option_Sound (unused) |
| 060272ec | Option_Compass |
| 0602733c / 060274b8 | Option_Control |
| 060276fc | control labels screen |
| 06015e04 | Overlay_DrawGameInfo |
| 06015e50 / 06015ed8 | DrawHealthBar / DrawAirBar |
| 06015f34 / 06015f8c | MakeAmmoString / DrawAmmoInfo |
| 060160f8 / 060161d0 | DrawPickups / AddDisplayPickup |
| 0603b524 / 0603b610 | bar drawing |
| 06016558 | level stats screen |
| 060163d8 | level-complete sequencer |
| 06016214 | StartGame (FMVs) |
| 06016c64 | gameflow |
| 06016a58 | load list |
| 06016bac | ending |
| 060334d0 | Press START |
| 060335ec | title |
| 06011d48 / 06011f48 | StartDemo / ProcessDemoInput |
| 0602cd44 | InitialiseLevel |
| 0602ce48 | level init (current track reset) |
| 0602ced8 | stats reset |
| 0602c088 / 0602c0ec / 0602c20c | InitialiseStartInfo / ModifyStartInfo / CreateStartInfo |
| 0602c35c / 0602c5ac | CreateSaveGameInfo / ExtractSaveGameInfo |
| 0602cb54 | save prompt |
| 0603c1bc | save with prompt |
| 0602c040 | read save |
| 0602cce8 | save checksum |
| 0603b76c | backup RAM init |
| 0604605c | BUP write |
| 0603a0dc / 06043068 | play FMV / FMV player (third argument: skippable) |
| 0603a19c / 0603a480 | show picture / fade to black |
| 0603a4fc | loading picture |
| 0603a9dc / 0603aaf0 / 0603ab38 | inventory fade in / out / finish (zeroes the vblank count) |

## Audio

| Address | Name |
|---|---|
| 060236e0 | Sound_Effect(sfx, pos, flags) (flags 2 = always) |
| 06023ec8 | Sound_StopEffect |
| 060235b0 | Sound_ResetEffects |
| 06023af4 | Sound_GetSlot |
| 06023bb4 / 06023c00 | ResetAmbientLoudness / StopAmbientSounds |
| 06023c7c | Sound_UpdateEffects (slot loop) |
| 06023da0 | UpdateSlotParams |
| 06024000 / 0602402c | ClearSlot / ClearSlotHandles |
| 06014fbc | SoundEffects (room sources, every tick) |
| 06036ec0 | LoadSoundFX |
| 0603c478 / 0603c588 | S_SoundPlaySample (one-shot / looped) |
| 0603c698 | IsPlaying |
| 0603c6d4 | StopAll |
| 0603c704 | StopSample |
| 0603c720 | SetPanAndVolume |
| 0603c75c / 0603c77c | S_CDPlay / S_CDStop |
| 0603c794 | StartSyncedAudio (waits for PLAY) |
| 0603c7cc | CD is playing |
| 0603c7f4 / 0603c80c | CD pause / resume |
| 06043c8c | CDC play (loop flag) |
| 06046ba4 / 06046c5c | SCSP key-on |
| 06046b28 | TL + pan |
| 06046d60 | level to TL |
| 06046d2c | CD send level |
| 06046a58 | sample upload |
| 06046192 | SCSP init |

## Data

The weapons and object tables are in their own sections above.

**Rooms, items and effects**

| Address | Data |
|---|---|
| 060a00fc | rooms, 0x44 each (flags +0x42, bit 0 underwater) |
| 060a00e4 | Lara's ITEM pointer |
| 06072bdc | g_Items, 0x48 each (+0x22 hit points, +0x2c creature data, +0x44 status byte: 8 hit, 0x10 gravity, 4 collidable) |
| 06089308 | g_Effects, 0x28 each |
| 06072bd8 | boxes, 0x14 each |
| 06072b4c | anims, 0x20 each |
| 0608c2b4 | baddie slots |
| 06062ac4 | slots in use |
| 06078c80 | Pierre's item number |
| 06093a24 | g_TriggerIndex |
| 06089bb8 | g_FlipMapTable |
| 060794bc | g_FlipStatus |
| 06060c08 / 06060c0c | flip effect / flip timer |
| 06074dc8 | fixed cameras |

**Lara (g_Lara)**

| Address | Field |
|---|---|
| 0609446e | gun_status |
| 06094470 | gun_type |
| 06094472 | request_gun_type |
| 06094474 | calc_fall_speed |
| 06094476 | water_status |
| 0609447a | hit_frame |
| 0609447c | hit_direction |
| 0609447e | air |
| 06094480 | dive_count |
| 06094482 | death_count |
| 06094484 | current_active |
| 06094486 | spaz_effect_count |
| 06094488 | spaz_effect |
| 0609448c | mesh_effects |
| 06094490 | mesh_ptrs[15] |
| 060944cc | target |
| 060944d0 | target_angles |
| 060944d4 | turn_rate |
| 060944d6 | move_angle |
| 060944d8 / 060944da | head y / x |
| 060944de / 060944e0 | torso y / x |
| 060944e4 / 060944f4 | left_arm / right_arm (frame_base, frame +4, lock +6, y +8, x +a, z +c, flash +e) |
| 06094504 / 06094510 / 0609451c / 06094528 | pistols / magnums / uzis / shotgun ammo (ammo, hit, miss each) |

**Input, cutscenes, projection, lighting**

| Address | Data |
|---|---|
| 060637e4 | g_Input bits: 1 fwd, 2 back, 4 left, 8 right, 0x10 jump, 0x20 draw, 0x40 action, 0x80 walk, 0x100 option, 0x200 look, 0x400 step left, 0x800 step right, 0x1000 roll |
| 06062770 | g_InputDB |
| 060637f0 | control method |
| 060637fc | control-method table |
| 06093188 | g_CinePosition (y_rot +0x0e) |
| 06072884 | g_CineFrame |
| 060737fc | NumCineFrames |
| 06094468 | Cine |
| 06063c70 | phd_persp |
| 06063c4c | window width |
| 06063c5c / 06063c60 | znear / zfar |
| 06060bf4 | ChunkyFlag |
| 06063c24 / 06063c28 | g_LsAdder / g_LsDivider |
| 0606388c | shadow mesh |

**Inventory, saves, timers**

| Address | Data |
|---|---|
| 0606279c | g_InvMode |
| 0606277a | g_InvChosen |
| 060623e0 / 060623e4 | options ring count / list |
| 06061b40 .. 06062280 | inventory item structs |
| 06060fd0 | per-level key/puzzle names |
| 060627a0 | gamma colour offset |
| 060627bc | level-skip cheat flag |
| 06060ca4 | g_OverlayFlag |
| 0606280c | g_HealthBarTimer |
| 06062c2c | pickup bounds |
| 06062c8c | Atlantis scion holder position |
| 06062cec | underwater switch bounds |
| 06062810 | TestLaraSlide's old angle |
| 06092324 | g_SaveGame (0xD7C bytes) |
| 06092484 | save-game timer (ticks) |
| 06092488 / 0609248c / 06092490 | kills / secrets / pickups |
| 06092491 | bonus game flag |
| 06060d24 | secrets per level |
| 06060cb4 | g_CurrentLevel |
| 06060cb8 | g_LevelComplete |
| 06060cbc / 06060cc0 | saving enabled / save exists |
| 06060c00 | soft-reset flag |
| 06060c04 | 0x8000 per vblank |
| 06060c10 | ControlPhase frame_count |
| 06063888 | vblank count |
| 06060d34 / 06060d38 | control / draw RNG seeds (0xD371F947 at power-on) |

**Tables**

| Address | Data |
|---|---|
| 060659c0 | sin table |
| 060649bc / 0606499c | atan table / atan octant base |

**Audio**

| Address | Data |
|---|---|
| 06078cc8 | sound slots (24 x 20 bytes) |
| 060723b8 | ambient lookup |
| 06073804 | SampleLUT |
| 0608c2e0 | SampleInfos |
| 060a0e7c | per-sample table (bits, rate, halve, alias; 8 bytes) |
| 060631d8 | sample alias table |
| 06063218 | volume-halve table |
| 06063246 | 11025 Hz list (sfx 58) |
| 0606324a | 2971.6 Hz list |
| 06063b10 | base pitch words 0x1400, 0x1050, 0x1800 |
| 06092294 | music track flags |
| 06060bf0 | current track |
| 06060c14 | gym counter (150) |
| 06062ad0 | sound active |
| 06093a20 / 06074c64 | sound sources / count |
