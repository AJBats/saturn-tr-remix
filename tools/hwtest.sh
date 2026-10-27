#!/bin/sh
# The hardware test disc: LEVEL1 starting in the bridge view the measurements
# are taken in (Lara exactly where the user stood her for the first Saturn
# benchmark), with the boot log, stats and measurement table on screen
# (HW_LOG). Mednafen and the Saturn then measure the very same view.
#
# usage: tools/hwtest.sh    (the disc lands in build/disc; restore a normal
#                            LEVEL1 with make disc after touching
#                            tools/pack_level.py)
set -e
cd "$(dirname "$0")/.."

LARA_START=13@28773:4352:49888 python -I tools/pack_level.py \
    external_resources/disc/files/DATA LEVEL1 build/data/LEVEL1.PKD \
    external_resources/ps1/files/files/PSXDATA
sleep 1     # make compares seconds
touch build/data/LEVEL1.PKD saturn/main.cpp saturn/crash.cpp saturn/debug.cpp
make disc START_LEVEL=LVL_TR1_1 START_YAW=0xD08E HW_LOG=1
