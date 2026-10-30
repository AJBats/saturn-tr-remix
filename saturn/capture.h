// Frame capture, for the clean-room renderer's lab: a debugger asks for it
// (gCaptureRequest), the next frame's render inputs are recorded in the low
// work RAM the level leaves, and the game holds that frame (its vertices and
// commands as drawn) until the debugger lets it go. tools/capture.py drives
// it in Mednafen.
//
// What is recorded is what the renderer is given: each room queued with its
// matrix and window, each object begun, each mesh, static mesh, shadow and
// sprite drawn with the matrix and light it was drawn with; then the ordering
// tables flush is given, and the camera. Beside them the renderer's calls as
// the game makes them, before the renderer's own tests: each item's model
// (drawModel), room static (drawRoomStatic), shadow (drawShadow, with the
// floor the sector data gave) and rectangle (renderRect: the walk's fills,
// then the HUD's), and each viewport set.
// While recording, every object is the master's (as for the polygon hunt), so
// the records come in the order the master draws them.
#ifndef CAPTURE_H
#define CAPTURE_H

#include "render.h"

enum CaptureRequest { CAPTURE_NONE, CAPTURE_ARM, CAPTURE_RELEASE };
enum CaptureType { CAP_ROOM = 1, CAP_OBJECT, CAP_MESH, CAP_STATIC, CAP_SHADOW, CAP_SPRITE,
                   CAP_VIEWPORT, CAP_MODEL, CAP_STATIC_CALL, CAP_SHADOW_CALL, CAP_RECT };

// A thing drawn.
struct CaptureRecord
{
    uint8 type;             // CaptureType
    uint8 node;             // a mesh's, from its object's first
    uint16 index;           // the room's; else its object's (item, or static in its room)
    const void* ref;        // the room, the mesh, the sprite
    Matrix matrix;          // as drawn with
    int32 ambient, range;   // the light, as drawn with
    vec3i light;
    uint32 flags;           // the faces' (gMeshFaceFlags)
    const Vertex* base;     // its vertices, as drawn
    const Face* first;      // and its first face
    int32 extra[4];         // a room's window x0 y0 x1 y1; an object's kind and owner;
                            // a shadow's x z sx sz; a sprite's vx vy vz vg; a viewport's
                            // x0 y0 x1 y1; a shadow call's floor; a rectangle's x y width height
};
// A call's record: a model's item (index) with the animation and frame it
// was drawn with (ambient, range: an item's draw may swap them) and its type
// (light.x); a static's RoomMesh (ref) and place from the camera (light); a
// rectangle's type, colour (ambient, range) and bucket (light.x).

// The frame, in front of its records.
struct CaptureHeader
{
    uint32 magic;           // CAPTURE_MAGIC
    uint32 ready;           // set once the frame is recorded
    uint32 count;           // records
    uint32 faces;           // commands flush built
    vec3i camera;           // gCameraViewPos
    int16 rotation[9];      // gViewRotation
    uint8 cameraWater;
    uint8 overflow;         // records left out for want of room
    uint16 meshesCount;
    const Mesh* const* meshes;  // level.meshes: the records' meshes are among them
    const Sprite* sprites;      // level.sprites
    int32 causticsFrame;
};

#define CAPTURE_MAGIC 0x43415031    // "CAP1"

// The ordering tables as flush took them (it empties them as it goes), the
// faces the draw order hints held back placed: the lab's flush starts from
// these. It follows the header; the records follow it.
struct CaptureOrder
{
    Face* roomOT[OT_SIZE];          // the slave's
    Face* meshOT[OT_SIZE];          // the master's
    uint16 roomGroups[OT_GROUPS];   // their faces by groups of buckets
    uint16 meshGroups[OT_GROUPS];
    int32 farShare;                 // the slave's share of the commands, percent
    uint32 shadeFrame;              // the frame's, for the shaded colour tables
    uint32 detailFrame;             // ... for the room texture cache
};

static_assert(sizeof(CaptureRecord) == 104 && sizeof(CaptureHeader) == 64 && sizeof(CaptureOrder) == 10424,
              "layouts read by tools/capture.py");

extern volatile uint32 gCaptureRequest;     // a debugger's CaptureRequest
extern CaptureHeader* gCapture;             // where the records are, while there are some
extern bool gCaptureRecording;

// The main loop's, before rendering: arms a capture a debugger asked for.
void captureTick();

// The frame is recorded: the game holds it until released.
bool captureFrozen();
void captureHold();

// The renderer's (render.cpp), while recording: what is drawn, and where
// its vertices and faces went.
void captureObject(int32 kind, int32 owner, int32 index);
void captureRoom(const Room* room);
void captureDraw(CaptureType type, const void* ref, const Vertex* base, const Face* first,
                 int32 e0, int32 e1, int32 e2, int32 e3);
void captureOrdered(Face* const* roomOT, const uint16* roomGroups, Face* const* meshOT, const uint16* meshGroups,
                    int32 farShare, uint32 shadeFrame);
void captureViewport(const RectMinMax &vp);
void captureRect(int32 x, int32 y, int32 width, int32 height, uint32 type, uint16 color, int32 z);
void captureFlushed(int32 faces);

#endif
