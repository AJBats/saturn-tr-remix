// The draw order hints (hints.h): render.cpp depthHintsBegin, its hints'
// matching, depthHintsPlace and rooms.cpp roomsFaceDepth.

#include "hints.h"

namespace render {

#define HINT_ANYWHERE   0xFF
#define MAX_ACTIVE      32

static const DepthHint* gHints;     // the level's
static uint32_t gHintCount;
static int32_t gLara[3];            // her room and block
static const DepthHint* gActive[MAX_ACTIVE];
static int32_t gActiveCount;

void hintsInit(const DepthHint* hints, uint32_t count)
{
    gHints = hints;
    gHintCount = count;
}

void hintsLara(int32_t room, int32_t bx, int32_t bz)
{
    gLara[0] = room;
    gLara[1] = bx;
    gLara[2] = bz;
}

// A hint of Lara's block, or of anywhere.
static bool hintHere(const DepthHint &h)
{
    return h.room == HINT_ANYWHERE || (h.room == gLara[0] && h.bx == gLara[1] && h.bz == gLara[2]);
}

void hintsMasks(uint32_t masks[2])
{
    masks[0] = masks[1] = 0;
    for (uint32_t i = 0; i < gHintCount; i++)
    {
        const DepthHint &h = gHints[i];
        if (hintHere(h)) {
            masks[h.kind] |= 1u << (h.index & 31);
        }
    }
}

void hintsBegin(const uint16_t* rooms, uint32_t count)
{
    gActiveCount = 0;
    for (uint32_t i = 0; i < gHintCount && gActiveCount < MAX_ACTIVE; i++)
    {
        const DepthHint &h = gHints[i];
        if (!hintHere(h))
            continue;
        bool drawn = h.buckets != 0;
        for (uint32_t k = 0; k < count && !drawn; k++) {
            drawn = rooms[k] == h.behindRoom;
        }
        if (drawn) {
            gActive[gActiveCount++] = &h;
        }
    }
}

static bool hintThing(const DepthHint &h, int32_t kind, int32_t owner, int32_t index)
{
    return h.kind == kind && h.index == index && (kind == 0 || h.owner == owner);
}

// Hints a and b draw the same faces before room faces.
static bool sameFaces(const DepthHint &a, const DepthHint &b)
{
    return !b.buckets && hintThing(b, a.kind, a.owner, a.index) && b.node == a.node &&
           (a.node == HINT_WHOLE || b.polygon == a.polygon);
}

bool hintsOf(const DrawThing &thing, HintFaces &out)
{
    out.buckets = 0;
    out.count = 0;
    if (thing.kind == NO_THING)
        return false;
    bool hinted = false;
    for (int32_t i = 0; i < gActiveCount; i++)
    {
        const DepthHint &h = *gActive[i];
        if (!hintThing(h, thing.kind, thing.owner, thing.index))
            continue;
        hinted = true;
        if (h.buckets) {
            out.buckets = h.buckets;
        }
    }
    if (!hinted || out.buckets)
        return hinted;

    // the faces held, in the hints' order: every face, or one
    for (int32_t i = 0; i < gActiveCount && out.count < HINT_FACES; i++)
    {
        const DepthHint &h = *gActive[i];
        if (h.buckets || !hintThing(h, thing.kind, thing.owner, thing.index) ||
            (h.node != HINT_WHOLE && h.node != thing.node))
            continue;
        out.polygons[out.count] = h.node == HINT_WHOLE ? HINT_EVERY : h.polygon;
        out.hints[out.count++] = &h;
    }
    return out.count > 0;
}

int32_t hintsDraws(const HintedCall* calls, uint32_t count, HintedDraw* out, int32_t room)
{
    int32_t n = 0;
    HintFaces faces;
    for (uint32_t k = 0; k < count; k++)
    {
        const HintedCall &c = calls[k];
        for (uint32_t i = 0; i < c.count && n < room; i++)
        {
            DrawThing thing = { c.kind, c.owner, uint8_t(i), 0, c.index, 0 };
            if (hintsOf(thing, faces)) {
                out[n++] = { c.first + i, thing };
            }
        }
    }
    return n;
}

// The bucket of a room face's farthest corner, if it faces the camera.
static bool roomFaceDepth(uint32_t room, uint32_t index, bool triangle, HintRooms rooms, int32_t &depth)
{
    HintRoom r;
    if (!rooms(room, r))
        return false;
    if (index >= (triangle ? r.trianglesCount : r.quadsCount))
        return false;
    const uint16_t* corners = triangle ? r.triangles[index].indices : r.quads[index].indices;
    int32_t n = triangle ? 3 : 4;

    // facing the camera: the camera on the side the corners wind clockwise
    // from, with y down (room space: vertex units of 256 but y from the top)
    const RoomVertex* a = r.points + corners[0] / sizeof(Vertex);
    const RoomVertex* b = r.points + corners[1] / sizeof(Vertex);
    const RoomVertex* c = r.points + corners[2] / sizeof(Vertex);
    int32_t e1x = b->x - a->x, e1y = b->y - a->y, e1z = b->z - a->z;
    int32_t e2x = c->x - a->x, e2y = c->y - a->y, e2z = c->z - a->z;
    // the camera's offset in units of 32: the products, under 2^17 * 2^12,
    // sum within 32 bits
    int32_t d[3];
    const int32_t corner[3] = { a->x << 8, a->y << 8, a->z << 8 };
    for (int32_t k = 0; k < 3; k++)
    {
        int32_t v = r.camera[k] - corner[k];
        v = v < -(1 << 17) ? -(1 << 17) : v > (1 << 17) ? 1 << 17 : v;
        d[k] = int16_t(v >> 5);
    }
    int32_t side = (e1y * e2z - e1z * e2y) * d[0] + (e1z * e2x - e1x * e2z) * d[1] + (e1x * e2y - e1y * e2x) * d[2];
    if (side >= 0)
        return false;

    int32_t z = 0;
    for (int32_t i = 0; i < n; i++)
    {
        int32_t cz = ((const Vertex*)((const uint8_t*)r.vertices + corners[i]))->z;
        z = cz > z ? cz : z;
    }
    depth = z >> OT_SHIFT;
    return true;
}

int32_t hintsPlace(const DepthHint* hint, int32_t depth, HintRooms rooms)
{
    for (int32_t j = 0; j < gActiveCount; j++)
    {
        const DepthHint &h = *gActive[j];
        int32_t behind;
        if (sameFaces(*hint, h) &&
            roomFaceDepth(h.behindRoom, h.behind & ~HINT_TRIANGLE, (h.behind & HINT_TRIANGLE) != 0, rooms, behind)) {
            depth = depth > behind + 1 ? depth : behind + 1;
        }
    }
    return hintsBucket(depth);
}

}
