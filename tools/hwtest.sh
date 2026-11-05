#!/bin/sh
# The hardware test disc: a level starting at one of the spots the user stood
# Lara in to look at, with the boot log, stats and measurement table on
# screen (HW_LOG). Mednafen and the Saturn then show the very same view.
#
#   ledge       LEVEL1's bridge view the measurements are taken in (where
#               the user stood her for the first Saturn benchmark)
#   vilcabamba  LEVEL2, room 15, among the stone houses
#   pool        LEVEL2, room 30, in the pool by the stone houses (water)
#   wolves      LEVEL1's snow pit below the ledge, the ledge's two wolves
#               awake from the start and coming at her (no input needed)
#   cistern     LEVEL7A, room 99, a diagonal view across the Cistern's
#               rooms near the start: its measurements are taken here (the
#               user's pick, the CPU's worst they had found)
#   cistern-straight  the same spot, Lara turned to look along the hall
#   cistern-rim LEVEL7A, room 111, on the first pool's rim: running forward
#               from here Lara passes the corner of four rooms (issue 249)
#   cistern-tunnel LEVEL7A, room 0, under water at the mouth of the 512 tall
#               tunnel to room 51 (issue 248)
#   cistern-look LEVEL7A, room 108, on the rim, the pool's two crocodiles awake:
#               looking about with the guns out, one aimed at (issue 250)
#   cistern-water LEVEL7A, room 108, looking across the green water: VDP1 just
#               over its budget, the CPUs with room (issue 247)
#   cistern-flood LEVEL7A flooded, room 103's stamp spot: under water in the hall
#   cistern-flood-above LEVEL7A flooded, room 99 along the hall, standing over
#               the water
#   cistern-flood-down the same spot, Lara turned to the water: looking down (Z + UP) near sparkles
#               fill the frame
#   cistern-flood-statues LEVEL7A flooded, room 101: the boxes under the statues in the water, drawn flat when the half-size textures' pool ran out
#   cistern-pillar LEVEL7A, room 106, a pillar to Lara's right: looking up
#               (Z + DOWN) the walk lists more vertices than the jobs had room for
#   cistern-platform LEVEL7A, room 102, the walk over that room standing still
#   cistern-balcony LEVEL7A, room 84, up on the balcony: the CPUs and VDP1 over
#               together (issue 255)
#   cistern-steps LEVEL7A, room 90, above the pool: the master over on its meshes
#               however many rooms are hidden
#   cistern-stamp LEVEL7A, room 103: 25 fps at Far 20, 30 at Far 16 or with the
#               rooms that can't be seen hidden by hand
#   cistern-mirror LEVEL7A, room 91, over the pool: holes in the picture at
#               a view of 36 rooms
#   cistern-pierre LEVEL7A, the mirror spot with Pierre (item 29) awake, out
#               of the camera's sight, for his running off
#   cistern-flat LEVEL7A, room 105: a wall drawn flat at 1.5 texels a pixel, the
#               room texture cache full
#   caves-planks LEVEL1, room 14, on the rope bridge's ledge: planks drawn over
#               Lara's feet
#   caves-mirror LEVEL1, room 24: hall of mirrors at the top of the frame, with
#               the camera looking down on her
#   cistern-warp LEVEL7A, room 31: where retail's near faces beat ours (issue 263)
#   cistern-ledge31 LEVEL7A, room 31, on the platform: what's below clipping up through it
#   cistern-corner LEVEL7A, room 31 again, turned to the wall, close: the near
#               faces at their hardest
#   cistern-feet LEVEL7A, room 97: the floor gone from under Lara's feet
#   cistern-feet-door the same with the door beside her (item 25) opened, as it
#               was when the floor went
#   cistern-bright LEVEL7A, room 32: where a light's falloff overflowed and
#               Lara and the crocodiles blew out white (issue 205)
#   cistern-dark LEVEL7A, room 2: a dark wall (room 111 q89) with bright stone behind it,
#               at the top left of the frame
#   valley      LEVEL3A, room 30, at the mouth of the valley with nothing awake:
#               a raptor wakes at the first steps in, the T-rex further north
#   valley-trex LEVEL3A, room 60, across the floor from the T-rex, him awake and
#               coming at her
#   valley-sky  LEVEL3A, room 56: looking up (Z + DOWN), the sky a hall of mirrors
#   valley-cave LEVEL3A, room 27: a hall of mirrors in the rock by the way in
#   valley-plants LEVEL3A, room 51, among the plants: VDP1 at 115 on the Saturn
#   valley-ledge LEVEL3A, room 56, high over the valley: the CPUs over on the Saturn (issue 285)
#   valley-dark LEVEL3A, room 63: dark rock with a bright cave behind it, in the middle of
#               the frame
#   zoo         LEVEL7A, room 32, bright: three rats and a gorilla moved in beside the
#               two crocodiles there, all still until Lara steps a block forward
#   zoo-wolves  LEVEL1, room 17: the bear and two wolves moved in beside it, the same
#   zoo-none zoo-rats zoo-crocs zoo-gorillas  room 32 with none of them, or three of
#               a kind standing in view, still: what each kind costs
#   bear        LEVEL1, room 17: the bear awake, about three blocks off
#
#   valley-under LEVEL3A, room 31: a polygon from under the ground showing at the lower right
#   valley-music LEVEL3A, room 56, on the trigger that wakes the T-rex and asks for his music
#   valley-end  LEVEL3A, room 51, the level ending at once: from a level with a plane to one without
#   vilcabamba-end LEVEL2's stone houses, the level ending at once: on into LEVEL3A's plane
#
# PLANES=0 in the environment packs the level without its floor and ceiling on VDP2.
#
# OBJECT_ART=1 in the environment reworks the meshes' textures too
# (tools/pack_level.py), in every level.
#
# usage: tools/hwtest.sh [SPOT]    (ledge if none; the disc lands in
#                                   build/disc; the next make disc packs the
#                                   level as the game has it again)
set -e
cd "$(dirname "$0")/.."

case "${1:-ledge}" in
    ledge)      level=LEVEL1 id=LVL_TR1_1 start=13@28773:4352:49888 yaw=0xD08E ;;
    vilcabamba) level=LEVEL2 id=LVL_TR1_2 start=15@66372:0:26074 yaw=0x1ADC ;;
    pool)       level=LEVEL2 id=LVL_TR1_2 start=30@69945:1262:31077 yaw=0xC06D ;;
    wolves)     level=LEVEL1 id=LVL_TR1_1 start=14@28262:6656:50925 yaw=0xD257 trigger=26,27 ;;
    cistern)    level=LEVEL7A id=LVL_TR1_7A start=99@45157:-3328:63405 yaw=0x9965 ;;
    cistern-straight) level=LEVEL7A id=LVL_TR1_7A start=99@45157:-3328:63405 yaw=0x81B6 ;;
    cistern-rim) level=LEVEL7A id=LVL_TR1_7A start=111@39375:-256:61648 yaw=0x1732 ;;
    cistern-tunnel) level=LEVEL7A id=LVL_TR1_7A start=0@43309:1183:53339 yaw=0x7A6A ;;
    cistern-look) level=LEVEL7A id=LVL_TR1_7A start=108@43468:-256:62565 yaw=0x905F trigger=0,1 ;;
    cistern-water) level=LEVEL7A id=LVL_TR1_7A start=108@44105:-256:62852 yaw=0x91E7 ;;
    cistern-flood) level=LEVEL7A id=LVL_TR1_7A start=103@47529:0:52129 yaw=0xDB9F trigger=flip0 ;;
    cistern-flood-above) level=LEVEL7A id=LVL_TR1_7A start=99@45157:-3328:63405 yaw=0x81B6 trigger=flip0 ;;
    cistern-flood-down) level=LEVEL7A id=LVL_TR1_7A start=99@45157:-3328:63405 yaw=0x9ACB trigger=flip0 ;;
    cistern-flood-statues) level=LEVEL7A id=LVL_TR1_7A start=101@45437:-3328:59770 yaw=0xBDF2 trigger=flip0 ;;
    cistern-pillar) level=LEVEL7A id=LVL_TR1_7A start=106@47461:0:64619 yaw=0xA15E ;;
    cistern-platform) level=LEVEL7A id=LVL_TR1_7A start=102@34112:-3328:55667 yaw=0x3962 ;;
    cistern-balcony) level=LEVEL7A id=LVL_TR1_7A start=84@40578:-5120:49157 yaw=0x139C ;;
    cistern-steps) level=LEVEL7A id=LVL_TR1_7A start=90@38752:-5632:65724 yaw=0x4B0B ;;
    cistern-stamp) level=LEVEL7A id=LVL_TR1_7A start=103@47529:0:52129 yaw=0xDB9F ;;
    cistern-mirror) level=LEVEL7A id=LVL_TR1_7A start=91@40465:-5632:65637 yaw=0x73D4 ;;
    cistern-pierre) level=LEVEL7A id=LVL_TR1_7A start=91@40465:-5632:65637 yaw=0x73D4 trigger=29 ;;
    cistern-flat) level=LEVEL7A id=LVL_TR1_7A start=105@47569:0:59202 yaw=0x9D98 ;;
    caves-planks) level=LEVEL1 id=LVL_TR1_1 start=14@25024:5120:52823 yaw=0xCD2D ;;
    caves-mirror) level=LEVEL1 id=LVL_TR1_1 start=24@44659:7168:76311 yaw=0xC631 ;;
    cistern-warp) level=LEVEL7A id=LVL_TR1_7A start=31@58826:-3584:53511 yaw=0xC207 ;;
    cistern-ledge31) level=LEVEL7A id=LVL_TR1_7A start=31@58986:-3584:52668 yaw=0xE765 ;;
    cistern-corner) level=LEVEL7A id=LVL_TR1_7A start=31@58826:-3584:53511 yaw=0x4207 ;;
    cistern-feet) level=LEVEL7A id=LVL_TR1_7A start=97@50480:-3328:60035 yaw=0x31D0 ;;
    cistern-feet-door) level=LEVEL7A id=LVL_TR1_7A start=97@50480:-3328:60035 yaw=0x31D0 trigger=25 ;;
    cistern-bright) level=LEVEL7A id=LVL_TR1_7A start=32@63367:-1280:50740 yaw=0xF1C1 ;;
    cistern-dark) level=LEVEL7A id=LVL_TR1_7A start=2@38785:0:52052 yaw=0x0368 ;;
    valley)     level=LEVEL3A id=LVL_TR1_3A start=30@39424:3584:42496 yaw=0x6000 ;;
    valley-trex) level=LEVEL3A id=LVL_TR1_3A start=60@45568:3584:15872 yaw=0x4000 trigger=55 ;;
    valley-sky) level=LEVEL3A id=LVL_TR1_3A start=56@41167:3584:27288 yaw=0xA136 ;;
    valley-cave) level=LEVEL3A id=LVL_TR1_3A start=27@36389:113:46532 yaw=0x3D3B ;;
    valley-plants) level=LEVEL3A id=LVL_TR1_3A start=51@41453:3584:35241 yaw=0x8143 ;;
    valley-ledge) level=LEVEL3A id=LVL_TR1_3A start=56@37787:230:20847 yaw=0x3FFC ;;
    valley-under) level=LEVEL3A id=LVL_TR1_3A start=31@38567:3584:8468 yaw=0x5E78 ;;
    valley-music) level=LEVEL3A id=LVL_TR1_3A start=56:5:3 yaw=0x4000 ;;
    valley-end) level=LEVEL3A id=LVL_TR1_3A start=51@41453:3584:35241 yaw=0x8143 trigger=end ;;
    vilcabamba-end) level=LEVEL2 id=LVL_TR1_2 start=15@66372:0:26074 yaw=0x1ADC trigger=end ;;
    valley-dark) level=LEVEL3A id=LVL_TR1_3A start=63@60614:3584:15342 yaw=0x0000 ;;
    zoo)        level=LEVEL7A id=LVL_TR1_7A start=32@63367:-1280:50740 yaw=0xF1C1 trigger=41,30,31,21,36,37 wake=32:62976:51712 place=41@32:62976:-1280:52736,30@32:60928:-1280:52736,31@32:64000:-1280:51712,21@32:59904:-1280:54784 ;;
    zoo-wolves) level=LEVEL1 id=LVL_TR1_1 start=17@17920:6656:59904 yaw=0xC000 trigger=30,45,46 wake=17:16896:59904 place=45@17:13824:6656:59904,46@17:14848:6656:59904 ;;
    zoo-none)   level=LEVEL7A id=LVL_TR1_7A start=32@63367:-1280:50740 yaw=0xF1C1 place=36@5:45568:-5632:76288,37@5:45568:-5632:76288,39@5:45568:-5632:76288 ;;
    zoo-rats)   level=LEVEL7A id=LVL_TR1_7A start=32@63367:-1280:50740 yaw=0xF1C1 place=36@5:45568:-5632:76288,37@5:45568:-5632:76288,39@5:45568:-5632:76288,41@32:62976:-1280:52736,30@32:60928:-1280:52736,31@32:64000:-1280:51712 ;;
    zoo-crocs)  level=LEVEL7A id=LVL_TR1_7A start=32@63367:-1280:50740 yaw=0xF1C1 place=36@32:62976:-1280:52736,37@32:60928:-1280:52736,39@32:59904:-1280:54784 ;;
    zoo-gorillas) level=LEVEL7A id=LVL_TR1_7A start=32@63367:-1280:50740 yaw=0xF1C1 place=36@5:45568:-5632:76288,37@5:45568:-5632:76288,39@5:45568:-5632:76288,21@32:62976:-1280:52736,27@32:60928:-1280:52736,28@32:59904:-1280:54784 ;;
    bear)       level=LEVEL1 id=LVL_TR1_1 start=17@16896:6656:60928 yaw=0xA000 trigger=30 ;;
    *)          echo "usage: tools/hwtest.sh [ledge|vilcabamba|pool|wolves|cistern|cistern-straight|cistern-rim|cistern-pillar|cistern-platform|cistern-balcony|cistern-steps|cistern-stamp|cistern-mirror|cistern-pierre|cistern-flat|caves-planks|caves-mirror|cistern-warp|cistern-ledge31|cistern-corner|cistern-feet|cistern-feet-door|cistern-bright|cistern-dark|cistern-tunnel|cistern-look|cistern-water|cistern-flood|cistern-flood-above|cistern-flood-down|cistern-flood-statues|valley|valley-trex|valley-sky|valley-cave|valley-plants|valley-dark|valley-ledge|valley-under|valley-music|valley-end|vilcabamba-end|zoo|zoo-wolves|zoo-none|zoo-rats|zoo-crocs|zoo-gorillas|bear]" >&2; exit 1 ;;
esac
art=${OBJECT_ART:-0}

make levels OBJECT_ART=$art     # every level in that art, before the start's
LARA_START=$start START_TRIGGER=${trigger:-} ITEMS_AT=${place:-} TRIGGER_AT=${wake:-} OBJECT_ART=$art python -I tools/pack_level.py \
    external_resources/disc/files/DATA $level build/data/$level.PKD \
    external_resources/ps1/files/files/PSXDATA
sleep 1     # make compares seconds
touch build/data/$level.PKD
make disc START_LEVEL=$id START_YAW=$yaw HW_LOG=1 OBJECT_ART=$art

# the level packed with the test's start dated older than what it is made
# from, so that no later build takes it for the game's
touch -d 2000-01-01 build/data/$level.PKD
