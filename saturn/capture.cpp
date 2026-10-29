// Frame capture (capture.h).

#include "capture.h"
#include "detail.h"
#include "hunt.h"
#include "slave.h"
#include "trace.h"

volatile uint32 gCaptureRequest;
CaptureHeader* gCapture;
bool gCaptureRecording;

enum CaptureState { CAPTURE_IDLE, CAPTURE_RECORDING, CAPTURE_HELD };

static int32 gState = CAPTURE_IDLE;
static uint32 gCapacity;        // records that fit
static CaptureRecord* gRecords;
static uint16 gObject;
static uint8 gNode;

void captureTick()
{
    if (*uncached(&gCaptureRequest) != CAPTURE_ARM || gState != CAPTURE_IDLE)
        return;
    *uncached(&gCaptureRequest) = CAPTURE_NONE;
    if (gHuntRecording || huntFrozen())
        return;     // the hunt has the memory
    // the trace's rings lie in the same memory: they stop for the capture
    traceStop();
    uint32 size;
    uint8* ram = osLowRamFree(size);
    if (size < sizeof(CaptureHeader) + sizeof(CaptureOrder) + 64 * sizeof(CaptureRecord))
    {
        traceResume();
        return;
    }
    gCapture = (CaptureHeader*)(uint32(ram + 15) & ~15);
    gRecords = (CaptureRecord*)((CaptureOrder*)(gCapture + 1) + 1);
    gCapacity = (size - (uint32((uint8*)gRecords - ram))) / sizeof(CaptureRecord);
    gCapture->magic = CAPTURE_MAGIC;
    gCapture->ready = 0;
    gCapture->count = 0;
    gCapture->overflow = 0;
    gObject = 0;
    gNode = 0;
    gState = CAPTURE_RECORDING;
    gCaptureRecording = true;
}

bool captureFrozen()
{
    return gState == CAPTURE_HELD;
}

void captureHold()
{
    if (*uncached(&gCaptureRequest) != CAPTURE_RELEASE)
        return;
    *uncached(&gCaptureRequest) = CAPTURE_NONE;
    gCapture = NULL;
    gState = CAPTURE_IDLE;
    traceResume();
}

// The next record, or NULL when there is no room left; its matrix and light
// are the drawing CPU's now.
static CaptureRecord* record(CaptureType type, const void* ref)
{
    if (gCapture->count == gCapacity)
    {
        gCapture->overflow = 1;
        return NULL;
    }
    CaptureRecord &r = gRecords[gCapture->count++];
    r.type = type;
    r.node = 0;
    r.index = gObject;
    r.ref = ref;
    r.matrix = matrixGet();
    r.ambient = gLightAmbient;
    r.range = gLightRange;
    r.light = gLightView;
    r.flags = cpuState()->meshFaceFlags;
    r.base = NULL;
    r.first = NULL;
    for (int32 i = 0; i < 4; i++) {
        r.extra[i] = 0;
    }
    return &r;
}

void captureObject(int32 kind, int32 owner, int32 index)
{
    gObject = index;
    gNode = 0;
    CaptureRecord* r = record(CAP_OBJECT, NULL);
    if (r)
    {
        r->extra[0] = kind;
        r->extra[1] = owner;
    }
}

void captureRoom(const Room* room)
{
    CaptureRecord* r = record(CAP_ROOM, room);
    if (r)
    {
        r->index = room - rooms;
        r->extra[0] = viewport.x0;
        r->extra[1] = viewport.y0;
        r->extra[2] = viewport.x1;
        r->extra[3] = viewport.y1;
    }
}

void captureDraw(CaptureType type, const void* ref, const Vertex* base, const Face* first,
                 int32 e0, int32 e1, int32 e2, int32 e3)
{
    CaptureRecord* r = record(type, ref);
    if (!r)
        return;
    if (type == CAP_MESH || type == CAP_STATIC) {
        r->node = gNode++;
    }
    r->base = base;
    r->first = first;
    r->extra[0] = e0;
    r->extra[1] = e1;
    r->extra[2] = e2;
    r->extra[3] = e3;
}

void captureOrdered(Face* const* roomOT, const uint16* roomGroups, Face* const* meshOT, const uint16* meshGroups,
                    int32 farShare, uint32 shadeFrame)
{
    CaptureOrder &o = *(CaptureOrder*)(gCapture + 1);
    for (int32 i = 0; i < OT_SIZE; i++)
    {
        o.roomOT[i] = roomOT ? roomOT[i] : NULL;
        o.meshOT[i] = meshOT[i];
    }
    for (int32 i = 0; i < OT_GROUPS; i++)
    {
        o.roomGroups[i] = roomOT ? roomGroups[i] : 0;
        o.meshGroups[i] = meshGroups[i];
    }
    o.farShare = farShare;
    o.shadeFrame = shadeFrame;
    o.detailFrame = gDetailFrame;
}

void captureFlushed(int32 faces)
{
    gCapture->faces = faces;
    gCapture->camera = gCameraViewPos;
    for (int32 i = 0; i < 9; i++) {
        gCapture->rotation[i] = gViewRotation[i];
    }
    gCapture->cameraWater = gCameraWater;
    gCapture->meshesCount = level.meshesCount;
    gCapture->meshes = level.meshes;
    gCapture->sprites = level.sprites;
    gCapture->causticsFrame = gCausticsFrame;
    gCaptureRecording = false;
    gState = CAPTURE_HELD;
    *uncached(&gCapture->ready) = 1;
}
