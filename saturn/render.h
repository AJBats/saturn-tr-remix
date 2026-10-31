// The game's side of the renderer (renderer/): what draw.h, the inventory and
// the HUD call (common.h's render* and set* functions) becomes the frame's
// call records, which flush hands to the renderer with the view; the level's
// data become its tables at load.
#ifndef RENDER_H
#define RENDER_H

#include "common.h"

// The rooms the camera sees, walked by the renderer (render::roomsSeen,
// exactly room.h getVisibleRooms): NULL-ended, the camera's room last, each
// with its window in clip. The rooms' faces go to the frame with them.
Room** renderWalk(Room* cameraRoom);

// An item's model, posed and lit as drawModel would (the renderer tests its
// sphere); its shadow, on the floor given (drawn if its model is); a room's
// static mesh (its room, its index there), with its room's origin from the
// camera.
void renderModel(const ItemObj* item);
void renderShadowCall(const ItemObj* item, int32 floor);
void renderStatic(const RoomMesh* mesh, int32 rx, int32 ry, int32 rz, int32 room, int32 index);

// The frame's list waits in work RAM while VDP1 draws the last one: the
// master goes on with the next frame and uploads it, with the textures its
// faces queued, and asks for its plot as soon as VDP1 is done. The master
// calls renderPoll between pieces of work for that.
extern bool gListPending;
void renderUploadReady();
inline void renderPoll()
{
    if (gListPending) {
        renderUploadReady();
    }
}

// Memory to borrow while nothing is drawn (videos between levels): the
// frame's command buffer, at least size bytes. The list pending in it is
// dropped.
void* renderScratch(uint32 size);

// The colour tables have swapped between the original and remastered sets
// (palettes.cpp): textures take the other set's mean colours.
void renderPalettesSwapped();

// After flipMap: the rooms swapped with their alternates.
void renderRoomsFlipped();

#endif
