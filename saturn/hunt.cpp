// Polygon hunt (hunt.h). Finding the faces painted over nearer ones needs no
// depth buffer: for each pair of faces whose boxes meet on the frame, a few
// points where both lie are tested, and where the face drawn later is farther
// (1/z, affine on the frame, fitted to its corners) it covers what should
// show. Slow, but the frame stands still meanwhile. The records live in the
// low work RAM the level leaves.

#include "hunt.h"
#include "crash.h"
#include "hw.h"
#include "input.h"
#include "overlay.h"
#include "system.h"
#include "trace.h"
#include "vram.h"

#include <string.h>

#define HUNT_FACES      VRAM_MAX_CMDS
#define HUNT_MESHES     512
#define HUNT_LIT        256     // faces lit up, at most
#define MIN_AREA        16      // pixels a face must cover wrongly to be lit
#define SAMPLES         6       // points across a pair's overlap, each way

// A face as drawn, in the frame's order.
struct HuntFace
{
    const Face* face;
    uint16 slot;            // its command's
    uint8 room;             // the slave's list
    uint8 n;                // corners tested: 3, 4, or 0 (not a surface)
    int16 x[4], y[4];
    int16 x0, y0, x1, y1;   // its box on the frame
    int32 a, b, c;          // 1/z (2^26 / z) at x, y: ((a x + b y) >> 16) + c
    int32 area;             // pixels it is painted over nearer faces (estimated)
    int16 under;            // the one it covers most
    int16 underArea;
};

// A mesh's faces from first on (the master's pool), its vertices from base.
struct HuntMesh
{
    const Face* first;
    const Vertex* base;
    const Mesh* mesh;
    uint8 kind;             // DEPTH_HINT_ITEM 0, DEPTH_HINT_STATIC 1
    uint8 owner;            // a static's room
    uint8 node;             // the object's mesh, from its first
    uint16 index;           // the item's, or the static's in its room
};

enum HuntState { HUNT_IDLE, HUNT_ARMED, HUNT_RECORDED, HUNT_READY };

struct HuntBuffers
{
    HuntFace faces[HUNT_FACES];
    HuntMesh meshes[HUNT_MESHES];
    uint16 lit[HUNT_LIT];               // faces painted over nearer ones, most first
    uint16 byX[HUNT_FACES];             // the surfaces by their boxes' left edges
    uint16 column[FRAME_WIDTH + 1];     // counting them
    uint32 saved[HUNT_LIT][8];          // their commands as drawn
};

extern Lara* players[MAX_PLAYERS];

bool gHuntRecording;

static HuntBuffers* gHunt;
static int32 gState = HUNT_IDLE;
static int32 gFaceCount, gMeshCount, gLitCount;
static int32 gCursor;
static bool gReadout, gTint;
static bool gLeaving;           // B is down: the hunt ends as it comes up (the game mustn't see it)
static uint16 gPad;
static const uint32* gCmds;
static uint8 gKind, gOwner;
static uint16 gIndex;
static int32 gNode;
static vec3i gLaraPos;
static int32 gLaraRoom, gLaraYaw;

bool huntArm()
{
    // the trace's rings lie in the same memory: they stop for the hunt
    traceStop();
    uint32 size;
    uint8* ram = osLowRamFree(size);
    if (size < sizeof(HuntBuffers))
    {
        traceResume();
        return false;
    }
    gHunt = (HuntBuffers*)(uint32(ram + 15) & ~15);
    gFaceCount = gMeshCount = gLitCount = 0;
    gKind = gOwner = 0;
    gIndex = 0xFFFF;
    gNode = 0;
    gState = HUNT_ARMED;
    gHuntRecording = true;
    return true;
}

bool huntFrozen()
{
    return gState >= HUNT_RECORDED;
}

void huntObject(int32 kind, int32 owner, int32 index)
{
    gKind = kind;
    gOwner = owner;
    gIndex = index;
    gNode = 0;
}

void huntMesh(const Mesh* mesh, const Face* first, const Vertex* base)
{
    if (gMeshCount == HUNT_MESHES)
        return;
    HuntMesh &m = gHunt->meshes[gMeshCount++];
    m.first = first;
    m.base = base;
    m.mesh = mesh;
    m.kind = gKind;
    m.owner = gOwner;
    m.index = gIndex;
    m.node = gNode++;
}

void huntOrder(Face* const* roomOT, Face* const* meshOT, int32 buckets)
{
    // as flush draws them: far to near, in each bucket the slave's before the master's
    for (int32 i = buckets - 1; i >= 0; i--)
    {
        for (int32 list = 0; list < 2; list++)
        {
            const Face* f = list ? meshOT[i] : roomOT ? roomOT[i] : NULL;
            for (; f && gFaceCount < HUNT_FACES; f = f->next)
            {
                HuntFace &h = gHunt->faces[gFaceCount++];
                h.face = f;
                h.room = !list;
            }
        }
    }

    const ItemObj* lara = (const ItemObj*)(const void*)players[0];
    gLaraPos = lara ? lara->pos : _vec3i(0, 0, 0);
    gLaraRoom = lara ? lara->room - rooms : -1;
    gLaraYaw = lara ? uint16(lara->angle.y) : 0;
}

void huntFlushed(const uint32* cmds)
{
    // the commands' slots in drawing order: after the clear, on through the
    // jump from the slave's commands to the master's
    gCmds = cmds;
    int32 slot = 1;
    for (int32 k = 0; k < gFaceCount; )
    {
        uint32 w = cmds[slot * 8];
        uint32 ctrl = w >> 16;
        if (ctrl & CMD_END)
            break;
        if ((ctrl & 0x7000) == CMD_SKIP_JUMP)     // only the link: a face's is never a jump
        {
            slot = ((w & 0xFFFF) * 8 - VRAM_FRAME_CMDS) / 32;
            continue;
        }
        gHunt->faces[k++].slot = slot++;
    }
    gHuntRecording = false;
    gState = HUNT_RECORDED;
}

// --- finding the faces painted over nearer ones ---------------------------

static bool surface(HuntFace &h)
{
    const Face* f = h.face;
    uint32 type = (f->flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
    uint32 ctrl = gCmds[h.slot * 8] >> 16;
    if (type == FACE_TYPE_SHADOW || type == FACE_TYPE_SPRITE || type == FACE_TYPE_FILL_S ||
        type == FACE_TYPE_LINE_H || type == FACE_TYPE_LINE_V || type == FACE_TYPE_VOID || (ctrl & CMD_SKIP))
        return false;

    const Vertex* half = h.room ? gSlaveVertices : gVertices;
    h.n = (f->flags & FACE_TRIANGLE) || (!h.room && (f->flags & FACE_PARALLELOGRAM)) ? 3 : 4;
    int32 w[4];
    h.x0 = h.y0 = 0x7FFF;
    h.x1 = h.y1 = -0x7FFF;
    for (int32 i = 0; i < h.n; i++)
    {
        const Vertex* v = cornerVertex(f->indices[i], half);
        if (v->z <= 0 || (v->clip & CLIP_PLANE))
            return false;
        h.x[i] = v->x;
        h.y[i] = v->y;
        w[i] = (1 << 26) / v->z;
        h.x0 = X_MIN(h.x0, v->x);
        h.y0 = X_MIN(h.y0, v->y);
        h.x1 = X_MAX(h.x1, v->x);
        h.y1 = X_MAX(h.y1, v->y);
    }
    h.x0 = X_MAX(h.x0, 0);
    h.y0 = X_MAX(h.y0, 0);
    h.x1 = X_MIN(h.x1, FRAME_WIDTH - 1);
    h.y1 = X_MIN(h.y1, FRAME_HEIGHT - 1);
    if (h.x0 > h.x1 || h.y0 > h.y1)
        return false;

    int32 dx1 = h.x[1] - h.x[0], dy1 = h.y[1] - h.y[0];
    int32 dx2 = h.x[2] - h.x[0], dy2 = h.y[2] - h.y[0];
    int32 det = dx1 * dy2 - dx2 * dy1;
    if (det == 0)
        return false;
    int64_t dw1 = w[1] - w[0], dw2 = w[2] - w[0];
    h.a = int32(((dw1 * dy2 - dw2 * dy1) << 16) / det);
    h.b = int32(((dx1 * dw2 - dx2 * dw1) << 16) / det);
    h.c = w[0] - int32((int64_t(h.a) * h.x[0] + int64_t(h.b) * h.y[0]) >> 16);
    return true;
}

static int32 depth(const HuntFace &h, int32 x, int32 y)
{
    return int32((int64_t(h.a) * x + int64_t(h.b) * y) >> 16) + h.c;
}

// x, y on the face (either winding; its edges count)
static bool inside(const HuntFace &h, int32 x, int32 y)
{
    int32 sign = 0;
    for (int32 i = 0; i < h.n; i++)
    {
        int32 j = i + 1 == h.n ? 0 : i + 1;
        int32 c = (h.x[j] - h.x[i]) * (y - h.y[i]) - (h.y[j] - h.y[i]) * (x - h.x[i]);
        if (c == 0)
            continue;
        if (!sign) {
            sign = c > 0 ? 1 : -1;
        } else if ((c > 0) != (sign > 0)) {
            return false;
        }
    }
    return true;
}

// Pixels later (drawn after earlier) covers wrongly, estimated over a grid of
// points where their boxes meet.
static int32 wrongArea(const HuntFace &earlier, const HuntFace &later)
{
    int32 x0 = X_MAX(earlier.x0, later.x0), x1 = X_MIN(earlier.x1, later.x1);
    int32 y0 = X_MAX(earlier.y0, later.y0), y1 = X_MIN(earlier.y1, later.y1);
    if (x0 > x1 || y0 > y1)
        return 0;
    int32 sx = X_MAX((x1 - x0 + SAMPLES) / SAMPLES, 1);
    int32 sy = X_MAX((y1 - y0 + SAMPLES) / SAMPLES, 1);
    int32 wrong = 0;
    for (int32 y = y0 + sy / 2; y <= y1; y += sy)
    {
        for (int32 x = x0 + sx / 2; x <= x1; x += sx)
        {
            if (!inside(earlier, x, y) || !inside(later, x, y))
                continue;
            int32 near = depth(earlier, x, y);
            int32 far = depth(later, x, y);
            if (near > far + (far >> 6)) {
                wrong++;
            }
        }
    }
    return wrong * sx * sy;
}

static void analyse()
{
    HuntFace* faces = gHunt->faces;
    for (int32 k = 0; k < gFaceCount; k++)
    {
        HuntFace &h = faces[k];
        h.area = 0;
        h.under = -1;
        h.underArea = 0;
        if (!surface(h)) {
            h.n = 0;
        }
    }

    // the pairs whose boxes meet: by left edge, each face against those
    // starting before its right edge
    uint16* column = gHunt->column;
    uint16* byX = gHunt->byX;
    memset(column, 0, sizeof(gHunt->column));
    int32 count = 0;
    for (int32 k = 0; k < gFaceCount; k++)
    {
        if (faces[k].n)
        {
            column[faces[k].x0 + 1]++;
            count++;
        }
    }
    for (int32 x = 1; x <= FRAME_WIDTH; x++) {
        column[x] += column[x - 1];
    }
    for (int32 k = 0; k < gFaceCount; k++) {
        if (faces[k].n) {
            byX[column[faces[k].x0]++] = k;
        }
    }

    for (int32 a = 0; a < count; a++)
    {
        const HuntFace &A = faces[byX[a]];
        for (int32 b = a + 1; b < count; b++)
        {
            const HuntFace &B = faces[byX[b]];
            if (B.x0 > A.x1)
                break;
            if (B.y1 < A.y0 || B.y0 > A.y1)
                continue;
            int32 i = X_MIN(byX[a], byX[b]);
            int32 j = X_MAX(byX[a], byX[b]);
            int32 area = wrongArea(faces[i], faces[j]);
            if (area < MIN_AREA)
                continue;
            HuntFace &later = faces[j];
            later.area += area;
            if (area > later.underArea)
            {
                later.underArea = X_MIN(area, 0x7FFF);
                later.under = i;
            }
        }
    }

    // the faces lit, most pixels first
    gLitCount = 0;
    for (int32 k = 0; k < gFaceCount; k++)
    {
        if (faces[k].area < MIN_AREA)
            continue;
        int32 i = X_MIN(gLitCount, HUNT_LIT - 1);
        if (gLitCount < HUNT_LIT) {
            gLitCount++;
        } else if (faces[k].area <= faces[gHunt->lit[i]].area) {
            continue;
        }
        while (i > 0 && faces[gHunt->lit[i - 1]].area < faces[k].area)
        {
            gHunt->lit[i] = gHunt->lit[i - 1];
            i--;
        }
        gHunt->lit[i] = k;
    }
}

// --- showing them ----------------------------------------------------------

static volatile uint32* command(int32 slot)
{
    return (volatile uint32*)(VDP1_VRAM + VRAM_FRAME_CMDS + slot * 32);
}

static void light()
{
    vdp1WaitDrawn();
    for (int32 i = 0; i < gLitCount; i++)
    {
        volatile uint32* w = command(gHunt->faces[gHunt->lit[i]].slot);
        const uint32* saved = gHunt->saved[i];
        if (!gTint)
        {
            w[0] = saved[0];
            w[1] = saved[1];
            continue;
        }
        uint16 colour = i == gCursor ? RGB(31, 0, 0) : RGB(0, 31, 0);
        w[0] = (saved[0] & 0x70000000) | (CMD_POLYGON << 16) | (saved[0] & 0xFFFF);
        w[1] = (PMOD_NO_ECD_SPD << 16) | colour;
    }
    vdp1Plot();
}

static char* append(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

static char* number(char* p, int32 v)
{
    char digits[12];
    int32 n = 0;
    if (v < 0)
    {
        *p++ = '-';
        v = -v;
    }
    do {
        digits[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (n) {
        *p++ = digits[--n];
    }
    *p = 0;
    return p;
}

static char* hex4(char* p, uint32 v)
{
    for (int32 i = 12; i >= 0; i -= 4) {
        *p++ = "0123456789ABCDEF"[(v >> i) & 15];
    }
    *p = 0;
    return p;
}

// A face's name: "room R qN" (tN a triangle, "split" a quarter of a split
// quad), "item I mM qN" (its object's mesh M), "static R:I mM qN", or "?".
static char* name(char* p, const HuntFace &h)
{
    if (h.room)
    {
        int32 room, index;
        bool triangle;
        if (!roomsFaceSource(h.face, room, index, triangle))
            return append(p, "slave ?");
        p = number(append(p, "room "), room);
        if (index < 0)
            return append(p, " split");
        return number(append(p, triangle ? " t" : " q"), index);
    }

    const HuntMesh* m = NULL;
    for (int32 i = 0; i < gMeshCount && gHunt->meshes[i].first <= h.face; i++) {
        m = gHunt->meshes + i;
    }
    if (!m || m->index == 0xFFFF)
        return append(p, "?");
    if (m->kind == 0) {
        p = number(append(p, "item "), m->index);
    } else {
        p = number(append(number(append(p, "static "), m->owner), ":"), m->index);
    }
    p = number(append(p, " m"), m->node);
    bool triangle;
    int32 index = renderMeshPolygon(m->mesh, m->base, h.face, triangle);
    if (index < 0)
        return append(p, " ?");
    return number(append(p, triangle ? " t" : " q"), index);
}

static void readout()
{
    char text[64];
    if (!gReadout)
    {
        overlayFrame();
        return;
    }

    char* p = append(text, "HUNT ");
    if (gLitCount)
    {
        p = number(p, gCursor + 1);
        p = number(append(p, "/"), gLitCount);
        const HuntFace &h = gHunt->faces[gHunt->lit[gCursor]];
        p = number(append(p, "  "), h.area);
        append(p, " px");
    }
    else
    {
        append(p, "none found");
    }
    overlayRow(2, text);

    p = number(append(text, "Lara room "), gLaraRoom);
    p = number(append(p, "  "), gLaraPos.x);
    p = number(append(p, " "), gLaraPos.y);
    number(append(p, " "), gLaraPos.z);
    overlayRow(3, text);

    p = number(append(text, "block "), gLaraPos.x >> 10);
    p = number(append(p, " "), gLaraPos.z >> 10);
    hex4(append(p, "  facing "), gLaraYaw);
    overlayRow(4, text);

    if (gLitCount)
    {
        const HuntFace &h = gHunt->faces[gHunt->lit[gCursor]];
        name(append(text, "over:  "), h);
        overlayRow(5, text);
        if (h.under >= 0)
        {
            name(append(text, "under: "), gHunt->faces[h.under]);
            overlayRow(6, text);
        }
    }
    overlayRow(8, "dpad next  A tint  C readout  B exit");
    overlayFrame();
}

void huntUpdate(uint16 pad)
{
    if (gState == HUNT_RECORDED)
    {
        crashHold(true);
        gReadout = false;
        gLeaving = false;
        for (int32 row = 0; row < OVERLAY_ROWS; row++) {
            overlayUnpin(row);
        }
        overlayFrame();
        vdp1WaitDrawn();
        analyse();
        for (int32 i = 0; i < gLitCount; i++)
        {
            volatile uint32* w = command(gHunt->faces[gHunt->lit[i]].slot);
            for (int32 k = 0; k < 8; k++) {
                gHunt->saved[i][k] = w[k];
            }
        }
        gCursor = 0;
        gTint = true;
        gPad = pad;
        gState = HUNT_READY;
        light();
        readout();
        return;
    }

    uint16 pressed = pad & ~gPad;
    gPad = pad;

    if (pressed & PAD_B) {
        gLeaving = true;
    }
    if (gLeaving && !(pad & PAD_B))
    {
        gTint = false;
        light();
        gReadout = false;
        overlayFrame();
        crashHold(false);
        gState = HUNT_IDLE;
        traceResume();
        return;
    }

    bool changed = false;
    if (gLitCount && (pressed & (PAD_RIGHT | PAD_DOWN)))
    {
        gCursor = (gCursor + 1) % gLitCount;
        changed = true;
    }
    if (gLitCount && (pressed & (PAD_LEFT | PAD_UP)))
    {
        gCursor = (gCursor + gLitCount - 1) % gLitCount;
        changed = true;
    }
    if (pressed & PAD_A)
    {
        gTint = !gTint;
        changed = true;
    }
    if (pressed & PAD_C) {
        gReadout = !gReadout;
    }
    if (changed) {
        light();
    }
    readout();
}
