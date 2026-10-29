// Stage 3 the new way (faces.h): what facesMeshAsm does, in C to check it
// against; the faces few enough for C; the join.

#include "faces.h"
#include "hw.h"

#include <string.h>

#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define VDP1_COORD_MIN  -1024
#define VDP1_COORD_MAX  1023
#define CLIP_FRAME      0x01
#define CLIP_DISCARD    0x3E
#define CLIP_PLANE      0x20
#define CMD_FLIP_H      0x0010
#define CMD_JUMP        0x1000  // JP 001: the next command is at CMDLINK
#define TEX_ATTR_DIR    0x0030
#define NO_GOURAUD      0xFFF80000  // w1 without its colour calculation and colour

#define FACE_TYPE_SHIFT 14
#define FACE_TYPE_MASK  15
#define FACE_TEXTURE    0x3FFF
enum { FACE_TYPE_SPRITE = 6, FACE_TYPE_FILL_S, FACE_TYPE_LINE_H, FACE_TYPE_LINE_V, FACE_TYPE_VOID = 11 };

void facesListInit(CommandList &list, Command* cmds, uint32_t (*grd)[2], int32_t first, int32_t end,
                   uint16_t* heads, uint16_t* tails, const Material* materials, const uint16_t* shadeGouraud,
                   ShadeCache* shade)
{
    list.cmd = cmds[first].w;
    list.end = cmds[end].w;
    list.grd = grd[first];
    list.link = facesLink(first);
    list.grdLink = ((VRAM_GOURAUD_BASE >> 3) + first) << 16;
    list.heads = heads;
    list.tails = tails;
    list.materials = materials;
    list.shadeGouraud = shadeGouraud;
    list.shade = shade;
    memset(heads, 0, (OT_SIZE + 1) * sizeof(heads[0]));
}

static inline const Vertex* corner(const Vertex* vertices, uint32_t offset)
{
    return (const Vertex*)((const uint8_t*)vertices + offset);
}

static inline bool backface(const Vertex* a, const Vertex* b, const Vertex* c)
{
    return (b->x - a->x) * (c->y - a->y) <= (c->x - a->x) * (b->y - a->y);
}

// Wholly beyond a frame edge, or a corner behind the near plane.
static inline bool culled(uint32_t clipAnd, uint32_t clipOr)
{
    return (clipAnd & CLIP_DISCARD) || (clipOr & CLIP_PLANE);
}

static inline int32_t offFrame(const Vertex* v)
{
    int32_t x = v->x, y = v->y;
    int32_t off = 0;
    if (x < 0) {
        off -= x;
    } else if (x > FRAME_WIDTH) {
        off += x - FRAME_WIDTH;
    }
    if (y < 0) {
        off -= y;
    } else if (y > FRAME_HEIGHT) {
        off += y - FRAME_HEIGHT;
    }
    return off;
}

static inline int32_t coord(int32_t v)
{
    return v < VDP1_COORD_MIN ? VDP1_COORD_MIN : v > VDP1_COORD_MAX ? VDP1_COORD_MAX : v;
}

// The command at list.cmd into bucket depth (w0 its control word, jumping to
// CMDLINK: the bucket's last), and the list on to the next.
static inline void linked(CommandList &list, uint32_t w0, int32_t depth)
{
    uint32_t head = list.heads[depth];
    list.cmd[0] = w0 | head;
    list.cmd[7] = list.grdLink;
    list.heads[depth] = list.link;
    if (!head) {
        list.tails[depth] = list.link;
    }
    list.cmd += 8;
    list.grd += 2;
    list.link += sizeof(Command) >> 3;
    list.grdLink += 1 << 16;
}

// A shaded copy of the colour table of w1 for gouraud value g, in the
// list's cache (flush.s .LshadeTable): its address / 8, or 0.
static uint32_t shadedCopy(ShadeCache* cache, uint32_t w1, uint32_t g)
{
    uint32_t table = w1 & 0xFFFF;
    uint32_t key = (table << 15) | g;
    uint32_t slot = ((g << 3) ^ table) & 0xFF;
    if (cache->keys[slot] != key)
    {
        slot ^= 1;
        if (cache->keys[slot] != key)
            return cache->miss(cache, key, slot);
    }
    if (cache->frames[slot] < cache->validFrom)
        return cache->miss(cache, key, slot);
    cache->frames[slot] = cache->frame;
    return (cache->vram + slot * SHADE_TABLE_SIZE) >> 3;
}

// The face's gouraud table from its corners' shades, or a shaded copy of
// its colour table where they share one.
static inline void shade(CommandList &list, const Material &m, uint32_t ga, uint32_t gb, uint32_t gc, uint32_t gd)
{
    const uint16_t* colour = list.shadeGouraud;
    uint32_t a = colour[ga], b = colour[gb], c = colour[gc], d = colour[gd];
    if (a == b && a == c && a == d && (m.flags & MAT_SHADEABLE) && list.shade)
    {
        uint32_t copy = shadedCopy(list.shade, m.w1, a);
        if (copy)
        {
            list.cmd[1] = (m.w1 & NO_GOURAUD) | copy;
            return;
        }
    }
    list.grd[0] = (a << 16) | b;
    list.grd[1] = (c << 16) | d;
}

static void quadCommand(CommandList &list, const Material &m, const Vertex* a, const Vertex* b, const Vertex* c,
                        const Vertex* d, int32_t depth)
{
    uint32_t w0 = m.w0;
    // VDP1 draws a quad as lines from edge AD to edge BC, each ending at its
    // first pixel past the frame once it has been on it: a quad further off
    // the frame on its AD side is drawn as B A D C, mirrored
    if (((a->clip | b->clip | c->clip | d->clip) & CLIP_FRAME) &&
        offFrame(a) + offFrame(d) > offFrame(b) + offFrame(c))
    {
        const Vertex* t = a; a = b; b = t;
        t = c; c = d; d = t;
        w0 ^= CMD_FLIP_H << 16;
    }
    uint32_t* w = list.cmd;
    w[1] = m.w1;
    w[2] = m.w2;
    w[3] = a->xy;
    w[4] = b->xy;
    w[5] = c->xy;
    w[6] = d->xy;
    shade(list, m, a->g, b->g, c->g, d->g);
    linked(list, w0, depth);
}

// v0, x, y: the fourth corner x + y - v0.
static void parallelogramCommand(CommandList &list, const Material &m, const Vertex* v0, const Vertex* x,
                                 const Vertex* y, bool atD, int32_t depth)
{
    uint32_t p = (uint32_t(coord(x->x + y->x - v0->x)) << 16) | (coord(x->y + y->y - v0->y) & 0xFFFF);
    uint32_t* w = list.cmd;
    w[1] = m.w1;
    w[2] = m.w2;
    if (atD)
    {
        w[3] = x->xy;
        w[4] = p;
        w[5] = y->xy;
        w[6] = v0->xy;
        shade(list, m, x->g, x->g, y->g, v0->g);
    }
    else
    {
        w[3] = v0->xy;
        w[4] = x->xy;
        w[5] = p;
        w[6] = y->xy;
        shade(list, m, v0->g, x->g, y->g, y->g);
    }
    linked(list, m.w0, depth);
}

void facesMeshC(const FaceSet* set, const Vertex* vertices, int32_t inside, CommandList &list)
{
    const FaceQuad* q = faceQuads(set);
    for (int32_t i = 0; i < set->quads; i++, q++)
    {
        if (list.cmd == list.end)
            return;
        const Vertex* a = corner(vertices, q->corners[0]);
        const Vertex* b = corner(vertices, q->corners[1]);
        const Vertex* c = corner(vertices, q->corners[2]);
        const Vertex* d = corner(vertices, q->corners[3]);
        if (backface(a, b, c))
            continue;
        if (!inside && culled(a->clip & b->clip & c->clip & d->clip, a->clip | b->clip | c->clip | d->clip))
            continue;
        int32_t depth = uint32_t(a->z + b->z + c->z + d->z) >> (2 + OT_SHIFT);
        quadCommand(list, list.materials[q->material], a, b, c, d, depth);
    }

    const FaceTriangle* t = faceTriangles(set);
    for (int32_t i = 0; i < set->triangles; i++, t++)
    {
        if (list.cmd == list.end)
            return;
        const Vertex* a = corner(vertices, t->corners[0]);
        const Vertex* b = corner(vertices, t->corners[1]);
        const Vertex* c = corner(vertices, t->corners[2]);
        if (backface(a, b, c))
            continue;
        if (!inside && culled(a->clip & b->clip & c->clip, a->clip | b->clip | c->clip))
            continue;
        const Material &m = list.materials[t->material];
        if (!(t->flags & TRI_PARALLELOGRAM))
        {
            quadCommand(list, m, a, b, c, c, uint32_t(a->z + b->z + c->z + c->z) >> (2 + OT_SHIFT));
            continue;
        }
        if (t->flags & TRI_SWAP)
        {
            const Vertex* s = b; b = c; c = s;
        }
        parallelogramCommand(list, m, a, b, c, t->flags & TRI_V0_AT_D,
                             uint32_t(a->z + b->z + c->z + c->z) >> (2 + OT_SHIFT));
    }
}

const FaceSet* facesShadow()
{
    struct Shadow
    {
        FaceSet set;
        FaceQuad quads[3];
    };
    static const Shadow shadow = {
        { 3, 0 },
        { { { 0 * 8, 1 * 8, 2 * 8, 7 * 8 }, MAT_SHADOW, 0 },
          { { 7 * 8, 2 * 8, 3 * 8, 6 * 8 }, MAT_SHADOW, 0 },
          { { 6 * 8, 3 * 8, 4 * 8, 5 * 8 }, MAT_SHADOW, 0 } }
    };
    return &shadow.set;
}

static inline uint32_t pair(int32_t hi, int32_t lo)
{
    return (uint32_t(hi) << 16) | (lo & 0xFFFF);
}

void facesOther(const Face &face, const Vertex* half, const Sprite* sprites, int32_t depth, CommandList &list)
{
    if (list.cmd == list.end)
        return;
    uint32_t flags = face.flags;
    uint32_t type = (flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
    const Vertex* v0 = corner(half, face.indices[0]);
    uint32_t* w = list.cmd;
    uint32_t w0;
    switch (type)
    {
        case FACE_TYPE_SPRITE:
        {
            // a scaled sprite between the two corners, one shade
            const Sprite &sprite = sprites[flags & FACE_TEXTURE];
            uint32_t g = list.shadeGouraud[v0->g];
            w0 = (CMD_SCALED_SPRITE | CMD_JUMP | (sprite.attr & TEX_ATTR_DIR)) << 16;
            w[1] = pair(PMOD_ECD_OFF | PMOD_LUT16 | PMOD_GOURAUD, sprite.colr);
            w[2] = pair(sprite.srca, sprite.size);
            w[3] = v0[0].xy;
            w[5] = v0[1].xy;
            list.grd[0] = list.grd[1] = (g << 16) | g;
            break;
        }

        case FACE_TYPE_FILL_S:
        case FACE_TYPE_LINE_H:
        case FACE_TYPE_LINE_V:
        {
            // a rectangle: position, then size; a line's colour in z
            int32_t x0 = v0[0].x, y0 = v0[0].y;
            int32_t x1 = x0 + v0[1].x - 1, y1 = y0 + v0[1].y - 1;
            w0 = (CMD_POLYGON | CMD_JUMP) << 16;
            w[1] = type == FACE_TYPE_FILL_S ? pair(PMOD_NO_ECD_SPD | PMOD_SHADOW, RGB(0, 0, 0))
                                            : pair(PMOD_NO_ECD_SPD, uint16_t(v0->z));
            w[3] = pair(x0, y0);
            w[4] = pair(x1, y0);
            w[5] = pair(x1, y1);
            w[6] = pair(x0, y1);
            break;
        }

        default:
        {
            // FACE_TYPE_VOID: the corners' box on the frame, transparent
            int32_t x0 = v0->x, y0 = v0->y, x1 = x0, y1 = y0;
            for (int32_t i = 1; i < 4; i++)
            {
                const Vertex* v = corner(half, face.indices[i]);
                x0 = v->x < x0 ? v->x : x0;
                y0 = v->y < y0 ? v->y : y0;
                x1 = v->x > x1 ? v->x : x1;
                y1 = v->y > y1 ? v->y : y1;
            }
            x0 = x0 < 0 ? 0 : x0;
            y0 = y0 < 0 ? 0 : y0;
            x1 = x1 > FRAME_WIDTH - 1 ? FRAME_WIDTH - 1 : x1;
            y1 = y1 > FRAME_HEIGHT - 1 ? FRAME_HEIGHT - 1 : y1;
            w0 = (CMD_POLYGON | CMD_JUMP | (x0 > x1 || y0 > y1 ? CMD_SKIP : 0)) << 16;
            w[1] = pair(PMOD_NO_ECD_SPD, 0);
            w[3] = pair(x0, y0);
            w[4] = pair(x1, y0);
            w[5] = pair(x1, y1);
            w[6] = pair(x0, y1);
        }
    }
    linked(list, w0, depth);
}

void facesCopy(CommandList &list, const uint32_t* w, const uint32_t* grd, int32_t depth)
{
    if (list.cmd == list.end)
        return;
    for (int32_t i = 1; i < 7; i++) {
        list.cmd[i] = w[i];
    }
    list.grd[0] = grd[0];
    list.grd[1] = grd[1];
    linked(list, (w[0] & 0xFFFF0000) | (CMD_JUMP << 16), depth);
}

void facesJoin(CommandList* const* lists, int32_t count, Command* cmds, int32_t first, int32_t end)
{
    // the link: the low half of a command's first word
    uint16_t* link = (uint16_t*)cmds[first].w + 1;
    for (int32_t b = OT_SIZE - 1; b >= 0; b--)
    {
        for (int32_t k = 0; k < count; k++)
        {
            CommandList &list = *lists[k];
            uint32_t head = list.heads[b];
            if (!head)
                continue;
            *link = head;
            list.heads[b] = 0;
            link = (uint16_t*)cmds[facesSlot(list.tails[b])].w + 1;
        }
    }
    *link = facesLink(end);
}
