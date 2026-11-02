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
    *)          echo "usage: tools/hwtest.sh [ledge|vilcabamba|pool|wolves|cistern|cistern-straight|cistern-rim|cistern-tunnel|cistern-look|cistern-water]" >&2; exit 1 ;;
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
