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
    if (size < sizeof(CaptureHeader) + sizeof(CaptureOrder) + sizeof(CaptureCache) + 64 * sizeof(CaptureRecord))
    {
        traceResume();
        return;
    }
    gCapture = (CaptureHeader*)(uint32(ram + 15) & ~15);
    CaptureCache* cache = (CaptureCache*)((CaptureOrder*)(gCapture + 1) + 1);
    gRecords = (CaptureRecord*)(cache + 1);

    // the room texture cache before the frame's faces take from it
    memcpy(cache->keys, gDetailKeys, sizeof(cache->keys));
    memcpy(cache->slots, gDetailSlots, sizeof(cache->slots));
    cache->frame = gDetailFrame;
    cache->uploads = 0;
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

// The next record, or NULL when there is no room left (or on the slave: the
// records are the master's); its matrix and light are the drawing CPU's now.
static CaptureRecord* record(CaptureType type, const void* ref)
{
    if (cpuState()->slave)
        return NULL;
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

void captureViewport(const RectMinMax &vp)
{
    CaptureRecord* r = record(CAP_VIEWPORT, NULL);
    if (r)
    {
        r->extra[0] = vp.x0;
        r->extra[1] = vp.y0;
        r->extra[2] = vp.x1;
        r->extra[3] = vp.y1;
    }
}

void captureModelCall(const ItemObj* item)
{
    CaptureRecord* r = record(CAP_MODEL, item);
    if (r)
    {
        r->index = item - items;
        r->ambient = item->animIndex;
        r->range = item->frameIndex;
        r->light.x = item->type;
    }
}

void captureStaticCall(const RoomMesh* mesh, int32 rx, int32 ry, int32 rz)
{
    CaptureRecord* r = record(CAP_STATIC_CALL, mesh);
    if (r)
    {
        r->light.x = rx;
        r->light.y = ry;
        r->light.z = rz;
    }
}

void captureShadowCall(const ItemObj* item, int32 floor)
{
    CaptureRecord* r = record(CAP_SHADOW_CALL, item);
    if (r)
    {
        r->index = item - items;
        r->extra[0] = floor;
    }
}

void captureRect(int32 x, int32 y, int32 width, int32 height, uint32 type, uint16 color, int32 z)
{
    CaptureRecord* r = record(CAP_RECT, NULL);
    if (r)
    {
        r->extra[0] = x;
        r->extra[1] = y;
        r->extra[2] = width;
        r->extra[3] = height;
        r->ambient = type;
        r->range = color;
        r->light.x = z;
    }
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
    CaptureCache &cache = *(CaptureCache*)((CaptureOrder*)(gCapture + 1) + 1);
    int32_t queued;
    const void* uploads = detailQueued(queued);
    cache.uploads = queued;
    memcpy(cache.upload, uploads, queued * sizeof(cache.upload[0]));
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
