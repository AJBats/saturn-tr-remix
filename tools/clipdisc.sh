#!/bin/sh
# A disc for a clip (tools/clip.py): the game as it ships, no on-screen log,
# started in a level at a spot, as tools/hwtest.sh's "at" makes for the
# Saturn tests.
#   CLIP_LEVEL=LEVEL8C CLIP_ID=LVL_TR1_8C CLIP_START=room@x:y:z CLIP_YAW=0xC000 \
#   [CLIP_TRIGGER=item,...] [CLIP_PLACE=item@room:x:y:z,...] sh tools/clipdisc.sh
# In tools/pack_level.py's terms: LARA_START, START_TRIGGER, ITEMS_AT.
set -e
level=$CLIP_LEVEL id=$CLIP_ID
make levels
LARA_START=$CLIP_START START_TRIGGER=${CLIP_TRIGGER:-} ITEMS_AT=${CLIP_PLACE:-} python -I tools/pack_level.py \
    external_resources/disc/files/DATA $level build/data/$level.PKD \
    external_resources/ps1/files/files/PSXDATA
sleep 1     # make compares seconds
touch build/data/$level.PKD
make disc START_LEVEL=$id START_YAW=$CLIP_YAW

# the level packed with the clip's start dated older than what it is made
# from, so that no later build takes it for the game's
touch -d 2000-01-01 build/data/$level.PKD
