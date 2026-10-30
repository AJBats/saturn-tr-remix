// The lab's side of the renderer's modules (renderer/): each fed from the
// scene, and held to the game's way.
#ifndef LAB_RENDER_H
#define LAB_RENDER_H

#include <stdint.h>

#include "hints.h"
#include "scene.h"
#include "textures.h"

// The room texture cache from the scene (labtextures.cpp): its set (RTEX,
// RTXI, INFO's pools) and the cache as the game's frame left it (DKEY, DSLT);
// texturesReset puts back the cache as the frame found it (DKY0, DSL0).
void texturesLabInit();
void texturesReset();

// What the checks hold it to: the uploads the game queued (DUPL); entries
// of two queues that differ (a count apart counting as all); the cache now
// against the game's after its frame (DKEY, DSLT as captured): keys and
// slots that differ; the queue's textures as staged against VDP1 VRAM now
// (before the lab uploads any, the scene's image: the game's own uploads):
// longwords that differ.
const TextureQueue& texturesCaptured();
int32_t texturesQueueOff(const TextureQueue &a, const TextureQueue &b);
int32_t texturesStateOff();
int32_t texturesVramOff(const TextureQueue &queue);

// The draw order hints from the scene (labhints.cpp): its level's (HINT) and
// Lara's room and block (LARA); the draws whose faces the frame's hints move
// from the scene's draws' things (DTHG), as hintsDraws from a draw list's
// hinted calls.
void hintsLabInit();
int32_t hintsSceneDraws(const DrawThing* things, uint32_t count, HintedDraw* out, int32_t room);

#endif
