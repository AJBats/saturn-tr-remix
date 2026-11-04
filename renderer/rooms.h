// The rooms seen (rooms.s): the portal walk and each room's queued window
// and matrix, exactly the game's (room.h getVisibleRooms, draw.h queueRoom),
// then the jobs of the rooms with vertices, placed, as the vertices and the
// faces take them; the fills past the draw distance.
#ifndef RENDER_ROOMS_H
#define RENDER_ROOMS_H

#include <stdint.h>

#include "types.h"

namespace render {

// The rooms a walk may list: the game's MAX_ROOM_LIST is 32; room for more,
// so views that see further (the lab's made-up ones) can't overrun it.
#define ROOMS_LISTED    128
#define ROOM_JOBS       64      // rooms.cpp's MAX_ROOM_JOBS: those queued with vertices
#define ROOM_FILLS      64      // the fills past the draw distance kept (all counted)
#define ROOM_ENTRIES    128     // the portals rooms were seen by kept (all counted)

// A room the walk saw through a portal: the room, the room it was seen from,
// the portal (from's).
struct RoomEntry
{
    uint8_t room, from;
    uint16_t pad;
    const uint8_t* portal;
};

struct RoomsContext
{
    const RoomLevel* level;     // +0  the level's rooms
    const void* portals;        // +4  their portals
    const uint16_t* divTable;   // +8
    int32_t camera[3];          // +12
    const int32_t* view;        // +24 the view's matrix (12 int32, no translation)
    int32_t (*clip)[4];         // +28 each room's window, x0 y0 x1 y1 (empty between frames)
    uint8_t* visible;           // +32 each room's listing (0 between frames)
    uint16_t* list;             // +36 the rooms listed, in order
    int32_t listed;             // +40
    RoomSeen* seen;             // +44 their windows and matrices, as queued
    int32_t viewport[4];        // +48
    uint32_t cameraRoom;        // +64
    int32_t voids;              // +68 the fills past the draw distance
    uint16_t* listEnd;          // +72 rooms.s's own
    int32_t k18, k20;           // +76 2^18, 2^20
    int32_t viewMax;            // +84 VIEW_MAX_F
    const RoomGeometry* geometry;   // +88 each room's
    uint8_t* rgeo;              // +92 where it lies
    Vertex* vertices;           // +96 where the jobs place their rooms' vertices
    FaceRoom* jobs;             // +100 the rooms listed with vertices, placed (roomsJobsAsm)
    int32_t jobCount;           // +104
    const RoomCaustics* caustics;   // +108 the caustics' wave
    int16_t* table;             // +112 the caustics' table, a vertex's shade offset by its index (32)
    int32_t (*fills)[4];        // +116 the fills' rectangles, x0 y0 x1 y1 (the first ROOM_FILLS)
    int32_t farScaled;          // +120 roomsFarScaled's
    int16_t rotation[10];       // +124 rooms.s's own: the view's rotation, its rows as 16-bit x y z
    RoomEntry* entries;         // +144 each portal a room was seen by (the first ROOM_ENTRIES)
    int32_t entryCount;         // +148
    int32_t verticesTop;        // +152 where the rooms' vertices must stop
    const uint8_t* hidden;      // +156 by room: listed, but dropped (roomsHideWith)
    uint8_t* seenNear;          // +160 by room: seen nearer than RoomLevel.black (0 between frames)
    int32_t tiny;               // +164 roomsTiny's
    const BoxSeen* boxes;       // +168 the solid boxes on the frame: a portal wholly behind one
    int32_t boxCount;           // +172 ... isn't entered
    const SolidBox* solid;      // +176 the level's solid boxes, for roomsBoxesAsm
    int32_t solidCount;         // +180
};

extern "C" void roomsSeenAsm(RoomsContext* ctx);
extern "C" void roomsBoxesAsm(RoomsContext* ctx);
extern "C" void roomsJobsAsm(RoomsContext* ctx);

// The rooms want over more vertices than there are: those showing least, by
// their windows' size, dropped until they fit (roomsJobsAsm's); never the camera's.
extern "C" void roomsMakeWay(RoomsContext* ctx, int32_t over);

// The level's rooms as the walk reads them (RoomLevel each, their portals in
// the game's layout), their geometry (RoomGeometry: offsets in rgeo), the
// caustics' wave (the caller moves its phase each frame), the vertices the
// jobs place their rooms' in, and the game's sines (gSinCosTable: sin << 16
// | cos, for the items' test).
#define MAX_LEVEL_ROOMS 139     // openlara MAX_ROOMS: LEVEL7A
struct RoomsLevel
{
    const RoomLevel* level;
    uint32_t count;
    const void* portals;
    const RoomGeometry* geometry;
    uint8_t* rgeo;
    const RoomCaustics* caustics;
    Vertex* vertices;
    const uint32_t* sines;
};
void roomsInit(const RoomsLevel &level);

// The frame's view: the camera, the view's matrix (12 int32, no translation),
// the viewport (x0 y0 x1 y1), the camera's room.
void roomsView(const int32_t camera[3], const int32_t view[12], const int32_t viewport[4], uint32_t cameraRoom);

// The walk from the view, and the jobs: the rooms listed with vertices. The
// jobs, the fills and the caustics' table are kept two deep: a walk fills the
// set the frame isn't drawing from, so that the next frame's walk may run
// while this frame is drawn (frame.h frameNextWork). The frame draws from
// the last walk's (take, or roomsTake later), or from none (roomsNone).
void roomsSeen(bool take = true);
void roomsTake();
void roomsNone();
FaceRoom* roomsJobs(uint32_t &count);       // the frame's
FaceRoom* roomsJobsAhead(uint32_t &count);  // the last walk's, not yet taken

// Rooms to leave without jobs from a view (the debug page's stamps): called before
// each walk with the camera, the view's matrix, the camera's room and a byte for
// each room, to set. They stay listed: the walk goes through them, what stands in
// them still draws. And the last walk's list while there is such a call.
typedef void (*RoomsHide)(const int32_t camera[3], const int32_t view[12], uint32_t cameraRoom, uint8_t* hidden);
void roomsHideWith(RoomsHide hide);
int32_t roomsListCopy(uint16_t* out, int32_t max);

// The fills past the draw distance the frame's walk found (renderVoid's: x0
// y0 x1 y1), the first ROOM_FILLS: their count.
const int32_t (*roomsFills(int32_t &count))[4];

// Whether an item drawn (its record, its frames A and B, its model's call:
// a shadow's may follow) could show through the portals the walk saw its
// room by, from the camera's room: the box holding both frames' boxes,
// turned to its heading, or that box flat on the floor below for its
// shadow, against the planes through the camera and each portal's edges,
// room by room back. drawsAsm asks for a model of more than one node, not
// Lara's, that passed the original's test; roomsItemsTest(false) answers
// yes to all (the walk not done before the draws: the lab's).
extern "C" int32_t roomsItemSeen(const ItemRecord* item, const int16_t* frameA, const int16_t* frameB,
                                 const CallRecord* call);
void roomsItemsTest(bool on);

// A portal's corner past the far plane takes its depth there, its x and y
// scaled by the plane's depth over its own so that it projects where it lies
// (vertices.h verticesFarScaled); off, it projects at the plane, as the
// original game's did (the lab's checks against its captures).
void roomsFarScaled(bool on);

// The far plane's distance, in world units (frame.h frameFar).
void roomsFar(int32_t far);

// A portal, none of it behind the camera, whose window comes to under this many
// pixels isn't entered: its window filled transparent, or under 16 pixels left
// as it is. 0: every window entered.
void roomsTiny(int32_t pixels);

// The level's solid boxes, seen afresh for each walk. What lies wholly behind one on the
// frame isn't drawn: a portal isn't entered, room faces aren't made (faces.h facesRoom).
void roomsSolidBoxes(const SolidBox* boxes, uint32_t count);
void roomsBoxesOn(bool on);

// The boxes of a walk, by its jobs (roomsJobs, roomsJobsAhead).
const BoxSeen* roomsBoxes(const FaceRoom* jobs, int32_t &count);

// The walk's context, for checks.
RoomsContext& roomsContext();

}

#endif
