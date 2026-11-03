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

// Work for the slave in this frame's render, begun as render/frame.h
// frameNextWork has it (main.cpp's next frame), or nullptr. In it, the next
// frame's walk from the camera as it stands, which renderWalk then takes if
// that frame's view is the same.
void renderNextWork(void (*work)());
void renderWalkAhead(Room* cameraRoom);

// An item's model, posed and lit as drawModel would (the renderer tests its
// sphere), or as the inventory's drawNodesLerp under the matrix on top of the
// stack, lit by its intensity; its shadow, on the floor given (drawn if its
// model is); a room's static mesh (its room, its index there), with its
// room's origin from the camera.
void renderModel(const ItemObj* item);
void renderModelUnder(const ItemObj* item);
void renderShadowCall(const ItemObj* item, int32 floor);
void renderStatic(const RoomMesh* mesh, int32 rx, int32 ry, int32 rz, int32 room, int32 index);

// Memory to borrow while nothing is drawn (videos between levels): the
// frame's command buffer, at least size bytes. The list pending in it is
// dropped.
void* renderScratch(uint32 size);

// After flipMap, at levelCommit: the rooms swapped with their alternates.
void renderRoomsFlipped();

// In animTexturesShift, at levelCommit: the animated ranges' textures shifted
// one along.
void renderTexturesShifted();

// The draw distance for the rooms and the items, in world units (the game's
// 20 sectors unless the HW_LOG menu's Far moves it in), kept across levels.
void renderFar(int32 far);

// What the fog has at its floor, past which nothing shows but the texels the
// floor's -16 leaves: 0 all drawn, 1 rooms wholly there dropped (RoomLevel.black)
// with what stands in them, 2 faces wholly there filled too.
void renderBlack(int32 mode);

// A portal's window under so many pixels: not entered (renderer/rooms.h roomsTiny).
void renderTiny(int32 pixels);

// 4 x the texels a pixel a room face may take before its next mip level.
void renderTexels(int32 lod);

#ifdef HW_LOG
// The debug page's stamps (renderer/rooms.h roomsHideWith, roomsListCopy, frame.h
// framePaintRoom).
typedef void (*RenderHide)(const int32_t camera[3], const int32_t view[12], uint32_t cameraRoom, uint8_t* hidden);
void renderHideWith(RenderHide hide);
int32 renderListCopy(uint16_t* out, int32 max);
void renderPaintRoom(int32 room);
// What nothing draws in the scene shows green, not the frames before: VDP1 pays for a
// polygon over the frame.
void renderHolesShown(bool shown);
#endif

#endif
