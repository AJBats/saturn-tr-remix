#!/bin/sh
# The hardware test disc: a level starting at one of the spots the user stood
# Lara in to look at, with the boot log, stats and measurement table on
# screen (HW_LOG). Mednafen and the Saturn then show the very same view.
#
#   ledge       LEVEL1's bridge view the measurements are taken in (where
#               the user stood her for the first Saturn benchmark)
#   vilcabamba  LEVEL2, room 15, among the stone houses
#
# usage: tools/hwtest.sh [SPOT]    (ledge if none; the disc lands in
#                                   build/disc; restore a normal level with
#                                   make disc after touching
#                                   tools/pack_level.py)
set -e
cd "$(dirname "$0")/.."

case "${1:-ledge}" in
    ledge)      level=LEVEL1 id=LVL_TR1_1 start=13@28773:4352:49888 yaw=0xD08E ;;
    vilcabamba) level=LEVEL2 id=LVL_TR1_2 start=15@66372:0:26074 yaw=0x1ADC ;;
    *)          echo "usage: tools/hwtest.sh [ledge|vilcabamba]" >&2; exit 1 ;;
esac

LARA_START=$start python -I tools/pack_level.py \
    external_resources/disc/files/DATA $level build/data/$level.PKD \
    external_resources/ps1/files/files/PSXDATA
sleep 1     # make compares seconds
touch build/data/$level.PKD
make disc START_LEVEL=$id START_YAW=$yaw HW_LOG=1
