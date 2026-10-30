// The draw order hints (saturn/depth_hints.txt, as render.cpp keeps them):
// while Lara stands in a block of a room, or anywhere, a thing's faces sort
// some buckets farther or nearer, or some of its faces (or all) are drawn
// before faces of a room: held out of the ordering table, then placed one
// bucket farther than the farthest corner of those room faces that face the
// camera (or where they were). Both ways take the hints and where to place a
// held face from here; each holds and places its own faces.
#ifndef RENDER_HINTS_H
#define RENDER_HINTS_H

#include <stdint.h>

#include "types.h"

namespace render {

#define HINT_WHOLE      0xFF    // DepthHint.node: every face of the thing
#define HINT_TRIANGLE   0x8000  // DepthHint.polygon, behind: a triangle's index, else a quad's

// render.cpp's.
struct DepthHint
{
    uint8_t level;
    uint8_t room;               // Lara's, or 0xFF: anywhere
    uint8_t bx, bz;             // her block: x >> 10, z >> 10
    uint8_t kind;               // 0 an item, 1 a static
    uint8_t owner;              // a static's room
    uint16_t index;             // the item's, or the static's in its room
    int16_t buckets;            // the thing's faces moved by; 0 for faces drawn before a room's:
    uint8_t node;               // ... of the thing's mesh, from its first drawn, or HINT_WHOLE
    uint8_t behindRoom;         // ... drawn before a face of this room
    uint16_t polygon;           // ... the mesh's quad, or triangle | HINT_TRIANGLE
    uint16_t behind;            // ... and the room's
};

// A room as a hint reads it: its faces' corners, its vertices in room space
// and as drawn, the camera from its origin. rooms(level index) gives each
// room of the frame's, or none.
struct HintRoom
{
    const RoomQuad* quads;
    const RoomQuad* triangles;
    uint32_t quadsCount, trianglesCount;
    const RoomVertex* points;
    const Vertex* vertices;
    const int32_t* camera;
};
typedef bool (*HintRooms)(uint32_t room, HintRoom &out);

// The level's hints; Lara's room and block this frame (x >> 10, z >> 10).
void hintsInit(const DepthHint* hints, uint32_t count);
void hintsLara(int32_t room, int32_t bx, int32_t bz);

// The things a frame's hints may name, as their index's bit (& 31) by kind
// (render.cpp gHintMask): those of Lara's block or of anywhere, before the
// rooms drawn are known.
void hintsMasks(uint32_t masks[2]);

// The frame's hints: those of Lara's block or of anywhere, and (for faces
// drawn before a room's) of a room drawn (rooms: their level indices).
void hintsBegin(const uint16_t* rooms, uint32_t count);

// The faces of a thing's mesh its hints move: by buckets (0 none); else held
// for hintsPlace, in the hints' order each hint's: every face (HINT_EVERY)
// or one polygon (a face held already stays as held first). Whether any are.
#define HINT_FACES  16
#define HINT_EVERY  0xFFFF
struct HintFaces
{
    int32_t buckets;
    int32_t count;
    uint16_t polygons[HINT_FACES];      // or HINT_EVERY
    const DepthHint* hints[HINT_FACES];
};
bool hintsOf(const DrawThing &thing, HintFaces &out);

// The draws whose faces the frame's hints move, in draw order, each with its
// thing, from a draw list's hinted calls; how many (room at most).
struct HintedDraw
{
    uint32_t draw;
    DrawThing thing;
};
int32_t hintsDraws(const HintedCall* calls, uint32_t count, HintedDraw* out, int32_t room);

// A held face's bucket: depth (its corners'), or one farther than the
// farthest corner of each room face facing the camera that the frame's hints
// for the same faces as hint name.
int32_t hintsPlace(const DepthHint* hint, int32_t depth, HintRooms rooms);

// What render.cpp faceLink clamps a face's bucket to.
inline int32_t hintsBucket(int32_t depth)
{
    return depth < 0 ? 0 : depth > OT_SIZE - 2 ? OT_SIZE - 2 : depth;
}

}

#endif
