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
#   cistern-feet LEVEL7A, room 97: the floor gone from under Lara's feet
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
    cistern-pillar) level=LEVEL7A id=LVL_TR1_7A start=106@47461:0:64619 yaw=0xA15E ;;
    cistern-platform) level=LEVEL7A id=LVL_TR1_7A start=102@34112:-3328:55667 yaw=0x3962 ;;
    cistern-balcony) level=LEVEL7A id=LVL_TR1_7A start=84@40578:-5120:49157 yaw=0x139C ;;
    cistern-steps) level=LEVEL7A id=LVL_TR1_7A start=90@38752:-5632:65724 yaw=0x4B0B ;;
    cistern-stamp) level=LEVEL7A id=LVL_TR1_7A start=103@47529:0:52129 yaw=0xDB9F ;;
    cistern-mirror) level=LEVEL7A id=LVL_TR1_7A start=91@40465:-5632:65637 yaw=0x73D4 ;;
    cistern-pierre) level=LEVEL7A id=LVL_TR1_7A start=91@40465:-5632:65637 yaw=0x73D4 trigger=29 ;;
    cistern-flat) level=LEVEL7A id=LVL_TR1_7A start=105@47569:0:59202 yaw=0x9D98 ;;
    cistern-feet) level=LEVEL7A id=LVL_TR1_7A start=97@50480:-3328:60035 yaw=0x31D0 ;;
    *)          echo "usage: tools/hwtest.sh [ledge|vilcabamba|pool|wolves|cistern|cistern-straight|cistern-rim|cistern-pillar|cistern-platform|cistern-balcony|cistern-steps|cistern-stamp|cistern-mirror|cistern-pierre|cistern-flat|cistern-feet|cistern-tunnel|cistern-look|cistern-water]" >&2; exit 1 ;;
esac
art=${OBJECT_ART:-0}

make levels OBJECT_ART=$art     # every level in that art, before the start's
LARA_START=$start START_TRIGGER=${trigger:-} OBJECT_ART=$art python -I tools/pack_level.py \
    external_resources/disc/files/DATA $level build/data/$level.PKD \
    external_resources/ps1/files/files/PSXDATA
sleep 1     # make compares seconds
touch build/data/$level.PKD
make disc START_LEVEL=$id START_YAW=$yaw HW_LOG=1 OBJECT_ART=$art

# the level packed with the test's start dated older than what it is made
# from, so that no later build takes it for the game's
touch -d 2000-01-01 build/data/$level.PKD
