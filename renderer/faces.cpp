// Faces (faces.h): the commands' list, the hinted meshes' faces, the room
// faces facesroom.s leaves to C (cut by a plane, or near the camera), the
// room materials, the shadows' faces.

#include "faces.h"
#include "platform.h"
#include "hints.h"
#include "hw.h"

#include <stddef.h>
#include <string.h>

namespace render {

#define FRAME_WIDTH     352
#define FRAME_HEIGHT    224
#define VDP1_COORD_MIN  -1024
#define VDP1_COORD_MAX  1023
#define CLIP_FRAME      0x01
#define CLIP_LEFT       0x02
#define CLIP_RIGHT      0x04
#define CLIP_TOP        0x08
#define CLIP_BOTTOM     0x10
#define CLIP_DISCARD    0x3E
#define CLIP_EDGES      0x1E    // beyond a frame edge
#define CLIP_PLANE      0x20
#define VIEW_NEAR       64      // a corner's z behind the near plane
#define FAR_BUCKET      1280    // a corner at the far plane: VIEW_MAX_F >> FIXED_SHIFT >> OT_SHIFT
#define CMD_FLIP_H      0x0010
#define CMD_JUMP        0x1000  // JP 001: the next command is at CMDLINK
#define X_CLAMP_5(v)    ((v) < 0 ? 0 : (v) > 31 ? 31 : (v))
#define TEX_ATTR_DIR    0x0030
#define NO_GOURAUD      0xFFF80000  // w1 without its colour calculation and colour

#define FACE_TYPE_SHIFT 14
#define FACE_TYPE_MASK  15
#define FACE_TEXTURE    0x3FFF
enum { FACE_TYPE_SPRITE = 6, FACE_TYPE_FILL_S, FACE_TYPE_LINE_H, FACE_TYPE_LINE_V, FACE_TYPE_VOID = 11 };

void facesListInit(CommandList &list, Command* cmds, uint32_t (*grd)[2], int32_t first, int32_t end,
                   uint16_t* heads, uint16_t* tails, const Material* materials, const uint16_t* shadeGouraud,
                   const uint8_t* shadeLevel, ShadeCache* shade, const Sprite* sprites)
{
    facesListStart(list, cmds, grd, first, end);
    list.heads = heads;
    list.tails = tails;
    list.materials = materials;
    list.shadeGouraud = shadeGouraud;
    list.shadeLevel = shadeLevel;
    list.shade = shade;
    list.sprites = sprites;
    memset(heads, 0, (OT_SIZE + 1) * sizeof(heads[0]));
}

void facesListStart(CommandList &list, Command* cmds, uint32_t (*grd)[2], int32_t first, int32_t end)
{
    list.cmd = cmds[first].w;
    list.end = cmds[end].w;
    list.grd = grd[first];
    list.link = facesLink(first);
    list.grdLink = ((VRAM_GOURAUD_BASE >> 3) + first) << 16;
    list.voidX0 = list.voidY0 = 0x7FFF;
    list.voidX1 = list.voidY1 = -1;
    list.voidArea = 0;
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

// A mesh face's fate (facesmesh.s CULL, .Lvoid) unless its mesh lies inside:
// left wholly beyond a frame edge or with a corner behind the near plane;
// filled behind everything where a plane cuts it otherwise (the far plane);
// else drawn.
enum MeshFace { MESH_FACE_LEFT, MESH_FACE_DRAWN, MESH_FACE_VOID };

static inline MeshFace meshFace(const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    if (a->clip & b->clip & c->clip & d->clip & CLIP_EDGES)
        return MESH_FACE_LEFT;
    if (!((a->clip | b->clip | c->clip | d->clip) & CLIP_PLANE))
        return MESH_FACE_DRAWN;
    bool near = a->z <= VIEW_NEAR || b->z <= VIEW_NEAR || c->z <= VIEW_NEAR || d->z <= VIEW_NEAR;
    return near ? MESH_FACE_LEFT : MESH_FACE_VOID;
}

// A parallelogram triangle's fourth corner, x + y - v0 (its z y's), for its
// fill (facesmesh.s .LtriVoid).
static inline Vertex fourthCorner(const Vertex* v0, const Vertex* x, const Vertex* y)
{
    Vertex v = *y;
    v.x = int16_t(x->x + y->x - v0->x);
    v.y = int16_t(x->y + y->y - v0->y);
    return v;
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

static inline uint32_t pair(int32_t hi, int32_t lo)
{
    return (uint32_t(hi) << 16) | (lo & 0xFFFF);
}

static inline int32_t max(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static inline int32_t min(int32_t a, int32_t b)
{
    return a < b ? a : b;
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

// Under water red loses 2 steps for every 1024 units beyond the first 1024,
// so distant things fade to teal (render.cpp waterGouraud).
static inline uint32_t waterColour(const CommandList &list, const Vertex* v)
{
    int32_t c = list.shadeLevel[v->g];
    int32_t r = c;
    if (v->z > 1024)
    {
        int32_t fade = ((v->z - 1024) >> 10) << 1;
        r -= fade < 31 ? fade : 31;
        if (r < 0) {
            r = 0;
        }
    }
    return (c << 10) | (c << 5) | r;
}

// How a face's corners shade it (flush.s .Lgouraud on): its gouraud table, or
// a shaded copy of its colour table (w1, when shadeable) where the corners
// share a shade; a far face (one drawn with a mip level) takes their mean
// that way when they lie within 2 levels. Under water, the fading gouraud
// alone.
enum Shading { SHADE_NEAR, SHADE_FAR, SHADE_WATER };

static void shade(CommandList &list, uint32_t w1, bool shadeable, Shading shading,
                  const Vertex* va, const Vertex* vb, const Vertex* vc, const Vertex* vd)
{
    uint32_t a, b, c, d;
    if (shading == SHADE_WATER)
    {
        a = waterColour(list, va);
        b = waterColour(list, vb);
        c = waterColour(list, vc);
        d = waterColour(list, vd);
    }
    else
    {
        const uint16_t* colour = list.shadeGouraud;
        a = colour[va->g];
        b = colour[vb->g];
        c = colour[vc->g];
        d = colour[vd->g];
        bool one = a == b && a == c && a == d;
        if (shading == SHADE_FAR && list.shade)
        {
            // the shades are greys: a level each
            int32_t la = a & 31, lb = b & 31, lc = c & 31, ld = d & 31;
            if (max(max(la, lb), max(lc, ld)) - min(min(la, lb), min(lc, ld)) <= 2)
            {
                uint32_t mean = (la + lb + lc + ld + 2) >> 2;
                a = b = c = d = (mean << 10) | (mean << 5) | mean;
                one = true;
            }
        }
        if (one && shadeable && list.shade)
        {
            uint32_t copy = shadedCopy(list.shade, w1, a);
            if (copy)
            {
                list.cmd[1] = (w1 & NO_GOURAUD) | copy;
                return;
            }
        }
    }
    list.grd[0] = (a << 16) | b;
    list.grd[1] = (c << 16) | d;
}

static inline void shade(CommandList &list, const Material &m, Shading shading,
                         const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    shade(list, m.w1, m.flags & MAT_SHADEABLE, shading, a, b, c, d);
}

// VDP1 draws a quad as lines from edge AD to edge BC, each ending at its
// first pixel past the frame once it has been on it: a quad further off the
// frame on its AD side is drawn as B A D C, mirrored. True when it is.
static inline bool mirrored(const Vertex* &a, const Vertex* &b, const Vertex* &c, const Vertex* &d)
{
    if (!((a->clip | b->clip | c->clip | d->clip) & CLIP_FRAME) ||
        offFrame(a) + offFrame(d) <= offFrame(b) + offFrame(c))
        return false;
    const Vertex* t = a; a = b; b = t;
    t = c; c = d; d = t;
    return true;
}

// The list's command at link (its VDP1 place / 8): its words.
static inline uint32_t* commandAt(const CommandList &list, uint32_t link)
{
    return list.cmd - (list.link - link) * 2;
}

// A command into bucket depth, its first word w0 (unlinked): jumping to the
// bucket's last.
static void linkCommand(CommandList &list, uint32_t* cmd, uint32_t w0, uint32_t link, int32_t depth)
{
    uint32_t head = list.heads[depth];
    cmd[0] = w0 | head;
    list.heads[depth] = link;
    if (!head) {
        list.tails[depth] = link;
    }
}

// A command back out of its bucket's chain (render.cpp faceUnlink): its
// newer neighbour, or the head, jumping past it; its newer neighbour the
// first when it was.
static void unlinkCommand(CommandList &list, uint32_t link, int32_t depth)
{
    uint32_t older = commandAt(list, link)[0] & 0xFFFF;
    if (list.heads[depth] == link)
    {
        list.heads[depth] = older;
        return;
    }
    for (uint32_t p = list.heads[depth]; p;)
    {
        uint32_t* c = commandAt(list, p);
        uint32_t next = c[0] & 0xFFFF;
        if (next == link)
        {
            c[0] = (c[0] & 0xFFFF0000) | older;
            if (list.tails[depth] == link) {
                list.tails[depth] = p;
            }
            return;
        }
        p = next;
    }
}

// render.cpp renderMeshHinted: the mesh's faces as facesMeshAsm makes them,
// then moved by the buckets, or the faces the hints name, in their order,
// taken back out of their buckets and held. facesMeshAsm makes a command for
// each face its tests keep, in order: each command's face, its polygon and
// bucket, from those tests here.
#define MAX_MADE_FACES  512
void facesMeshHinted(const FaceSet* set, const Vertex* vertices, uint32_t mode, CommandList &list,
                     const HintFaces &hints, HeldCommand* held, int32_t &count, int32_t room)
{
    struct Made
    {
        uint32_t link;
        int32_t depth;
        uint32_t polygon;
        bool taken;     // held, or a fill (which stays behind everything)
    };
    static Made made[MAX_MADE_FACES];
    const uint32_t step = sizeof(Command) >> 3;
    uint32_t first = list.link;
    facesMeshAsm(set, vertices, mode, &list);
    uint32_t madeCount = (list.link - first) / step;

    bool inside = mode & MESH_INSIDE;
    uint32_t n = 0;
    const FaceQuad* q = faceQuads(set);
    for (int32_t i = 0; i < set->quads && n < madeCount && n < MAX_MADE_FACES; i++, q++)
    {
        const Vertex* a = corner(vertices, q->corners[0]);
        const Vertex* b = corner(vertices, q->corners[1]);
        const Vertex* c = corner(vertices, q->corners[2]);
        const Vertex* d = corner(vertices, q->corners[3]);
        MeshFace fate = inside ? MESH_FACE_DRAWN : meshFace(a, b, c, d);
        if (backface(a, b, c) || fate == MESH_FACE_LEFT)
            continue;
        bool fill = fate == MESH_FACE_VOID;
        made[n] = { first + n * step, fill ? FAR_BUCKET : int32_t(uint32_t(a->z + b->z + c->z + d->z) >> (2 + OT_SHIFT)),
                    uint32_t(i), fill };
        n++;
    }
    const FaceTriangle* t = faceTriangles(set);
    for (int32_t i = 0; i < set->triangles && n < madeCount && n < MAX_MADE_FACES; i++, t++)
    {
        const Vertex* a = corner(vertices, t->corners[0]);
        const Vertex* b = corner(vertices, t->corners[1]);
        const Vertex* c = corner(vertices, t->corners[2]);
        MeshFace fate = inside ? MESH_FACE_DRAWN : meshFace(a, b, c, c);
        if (backface(a, b, c) || fate == MESH_FACE_LEFT)
            continue;
        bool fill = fate == MESH_FACE_VOID;
        made[n] = { first + n * step, fill ? FAR_BUCKET : int32_t(uint32_t(a->z + b->z + c->z + c->z) >> (2 + OT_SHIFT)),
                    uint32_t(i) | HINT_TRIANGLE, fill };
        n++;
    }

    if (hints.buckets)
    {
        for (uint32_t k = 0; k < n; k++)
        {
            if (made[k].taken)
                continue;
            uint32_t* cmd = commandAt(list, made[k].link);
            unlinkCommand(list, made[k].link, made[k].depth);
            linkCommand(list, cmd, cmd[0] & 0xFFFF0000, made[k].link, hintsBucket(made[k].depth + hints.buckets));
        }
        return;
    }
    for (int32_t e = 0; e < hints.count; e++)
    {
        uint32_t polygon = hints.polygons[e];
        for (uint32_t k = 0; k < n && count < room; k++)
        {
            Made &f = made[k];
            if (f.taken || (polygon != HINT_EVERY && f.polygon != polygon))
                continue;
            f.taken = true;
            uint32_t* cmd = commandAt(list, f.link);
            unlinkCommand(list, f.link, f.depth);
            held[count++] = { cmd, cmd[0] & 0xFFFF0000, f.link, f.depth, hints.hints[e] };
        }
    }
}

void facesLinkHeld(CommandList &list, const HeldCommand &held, int32_t depth)
{
    linkCommand(list, held.cmd, held.w0, held.link, depth);
}

// --- rooms ------------------------------------------------------------------

// A room face's flags (render.h): its texture or colour, kind, how its
// texture is cached
#define FACE_TRIANGLE       (1u << 31)
#define FACE_WATER          (1u << 30)
#define FACE_DETAIL         (1u << 29)
#define FACE_DETAIL_WHOLE   (1u << 27)
#define FACE_DETAIL_MIP2    (1u << 26)
#define FACE_DETAIL_SLOT_SHIFT  20
#define FACE_DETAIL_TURNED  (1u << 19)
#define FACE_DETAIL_MIP     (1u << 18)
#define FACE_DETAIL_QUAD_SHIFT  18
#define FACE_TYPE_F         1
#define ROOM_QUAD_FLAGS     0x00FFFFFF  // but the kept decisions
#define TEX_ATTR_AKILL      0x0001
#ifdef RENDER_LAB
#define TEX_ATTR_SPARSE     0       // the lab's lists are the original's
#else
#define TEX_ATTR_SPARSE     0x0002  // mostly see-through: left out when large
#endif

#define VIEW_FAR        20480   // VIEW_MAX_F >> FIXED_SHIFT
#define NEAR_SPLIT_DIST 2048    // quads whose corner A is nearer go the careful way
#define DETAIL_DIST     4096    // quads crossing the near plane nearer than this split 2x2

static int32_t gViewFar = VIEW_FAR; // facesFar's
extern "C" { volatile int32_t gFacesVoidWay; }

void facesFar(int32_t far)
{
    gViewFar = far;
}

// The command words a room face's flags make (render.cpp faceMaterial); a
// texture with see-through texels that the cache missed is left out (a skip).
static void roomMaterial(const RoomTextures &t, uint32_t flags, uint32_t* w)
{
    uint32_t index = flags & FACE_TEXTURE;
    if (((flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK) == FACE_TYPE_F)
    {
        w[0] = (CMD_POLYGON | CMD_JUMP) << 16;
        w[1] = pair(PMOD_NO_ECD_SPD | PMOD_GOURAUD, t.palette[index]);
        return;
    }
    const Texture &tex = t.textures[index];
    if (tex.srca || (flags & FACE_DETAIL))
    {
        uint32_t pmod = t.pmod | PMOD_ECD_OFF | (tex.attr & TEX_ATTR_AKILL ? 0 : PMOD_SPD_OFF);
        uint32_t srca = tex.srca;
        uint32_t colr = tex.colr;
        uint32_t size = tex.size;
        if (flags & FACE_DETAIL)
        {
            uint32_t slot = (flags >> FACE_DETAIL_SLOT_SHIFT) & 0x7F;
            if (flags & FACE_DETAIL_WHOLE)
            {
                uint32_t level = flags & FACE_DETAIL_MIP ? (flags & FACE_DETAIL_MIP2 ? 2 : 1) : 0;
                colr += level * 4;  // the level's colour table, after the texture's
                srca = level ? (t.pools[level] + (slot & 0x3F) * (DETAIL_SLOT_SIZE >> 2 * level)) >> 3
                             : (t.pools[0] + slot * DETAIL_SLOT_SIZE) >> 3;
                uint32_t width = (size >> 8) * 16 >> level;
                uint32_t height = (size & 0xFF) * 2 >> level;
                size = flags & FACE_DETAIL_TURNED ? ((height / 8) << 8) | width : ((width / 8) << 8) | height;
            }
            else
            {
                uint32_t quad = (flags >> FACE_DETAIL_QUAD_SHIFT) & 3;
                srca = (t.pools[0] + slot * DETAIL_SLOT_SIZE + quad * (size >> 8) * 4 * (size & 0xFF)) >> 3;
            }
        }
        w[0] = (CMD_DISTORTED_SPRITE | CMD_JUMP | (tex.attr & TEX_ATTR_DIR)) << 16;
        w[1] = pair(pmod, colr);
        w[2] = pair(srca, size);
        return;
    }
    if (tex.attr & TEX_ATTR_AKILL)
    {
        w[0] = (CMD_SKIP | CMD_JUMP) << 16;
        return;
    }
    w[0] = (CMD_POLYGON | CMD_JUMP) << 16;
    w[1] = pair(PMOD_NO_ECD_SPD | PMOD_GOURAUD, tex.color);
}

// A room face's command from its material in the list's next command, and its
// flags and corners (flush.s, render.cpp faceCommand): turned as its texture
// is, mirrored off the frame, shaded.
static void roomCorners(CommandList &list, uint32_t flags, const Vertex* a, const Vertex* b, const Vertex* c,
                        const Vertex* d, int32_t depth)
{
    uint32_t* w = list.cmd;
    if (w[0] & (CMD_SKIP << 16))
    {
        linked(list, w[0], depth);
        return;
    }
    const uint32_t turned = FACE_DETAIL | FACE_DETAIL_WHOLE | FACE_DETAIL_TURNED;
    if ((flags & turned) == turned)
    {
        const Vertex* s = b; b = d; d = s;      // A D C B, along the transposed texture
    }
    if (mirrored(a, b, c, d)) {
        w[0] ^= CMD_FLIP_H << 16;
    }
    w[3] = a->xy;
    w[4] = b->xy;
    w[5] = c->xy;
    w[6] = d->xy;
    const uint32_t far = FACE_DETAIL | FACE_DETAIL_WHOLE | FACE_DETAIL_MIP;
    Shading shading = flags & FACE_WATER ? SHADE_WATER : (flags & far) == far ? SHADE_FAR : SHADE_NEAR;
    shade(list, w[1], ((w[1] >> 16) & 0x3F) == (PMOD_LUT16 | PMOD_GOURAUD), shading, a, b, c, d);
    linked(list, w[0], depth);
}

static void roomCommand(CommandList &list, const RoomTextures &t, uint32_t flags, const Vertex* a,
                        const Vertex* b, const Vertex* c, const Vertex* d, int32_t depth)
{
    if (list.cmd == list.end)
        return;
    roomMaterial(t, flags, list.cmd);
    roomCorners(list, flags, a, b, c, d, depth);
}

// A transparent fill behind everything over the corners' box on the frame,
// where a plane cuts a room's or a mesh's face away.
static void faceVoid(CommandList &list, const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    const int32_t way = *uncached(&gFacesVoidWay);
    if (list.cmd == list.end || way == FACE_VOIDS_NONE)
        return;
    int32_t x0 = max(min(min(a->x, b->x), min(c->x, d->x)), 0);
    int32_t y0 = max(min(min(a->y, b->y), min(c->y, d->y)), 0);
    int32_t x1 = min(max(max(a->x, b->x), max(c->x, d->x)), FRAME_WIDTH - 1);
    int32_t y1 = min(max(max(a->y, b->y), max(c->y, d->y)), FRAME_HEIGHT - 1);
    if (x0 <= x1 && y0 <= y1)
    {
        list.voidX0 = min(list.voidX0, x0);
        list.voidY0 = min(list.voidY0, y0);
        list.voidX1 = max(list.voidX1, x1);
        list.voidY1 = max(list.voidY1, y1);
        list.voidArea += (x1 - x0 + 1) * (y1 - y0 + 1);
    }
    if (way == FACE_VOIDS_BOXED)
        return;
    uint32_t* w = list.cmd;
    w[1] = pair(PMOD_NO_ECD_SPD, 0);
    w[3] = pair(x0, y0);
    w[4] = pair(x1, y0);
    w[5] = pair(x1, y1);
    w[6] = pair(x0, y1);
    linked(list, (CMD_POLYGON | CMD_JUMP | (x0 > x1 || y0 > y1 ? CMD_SKIP : 0)) << 16, FAR_BUCKET);
}

static inline int32_t zMax(const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    return max(max(a->z, b->z), max(c->z, d->z));
}

static inline int32_t span(const Vertex* a, const Vertex* b)
{
    int32_t dx = a->x - b->x, dy = a->y - b->y;
    return max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
}

static inline uint32_t detailLevel(uint32_t variant)
{
    return variant < DETAIL_MIP1 || variant > DETAIL_MIP2_TURNED ? 0 : (variant - DETAIL_MIP1) / 2 + 1;
}

static inline uint32_t detailPoolStart(uint32_t level)
{
    return level ? DETAIL_FULL_SLOTS + (level - 1) * DETAIL_MIP_SLOTS : 0;
}

// The texture's slot in its level's pool for the variant, marked drawn this
// frame (taken into the cache if it hasn't it); -1 when not cached.
static int32_t cachedSlot(RoomTextures &t, const Texture &tex, uint32_t variant)
{
    uint32_t key = tex.reserved - 1;
    if (key >= DETAIL_MAX_KEYS)
        return -1;
    int32_t slot = int32_t(t.keys[variant][key]) - 1;
    if (slot < 0)
    {
        slot = t.miss(key, tex.size, variant);
        if (slot < 0)
            return -1;
    }
    t.slots[detailPoolStart(detailLevel(variant)) + slot].frame = t.frame;
    return slot;
}

// faces.s WHOLE: the face's texture whole from the cache, at mip level
// 'level' turned or not (variant: level * 2 + turned), in its flags; with no
// room at that level the next one down, and drawn in its mean colour when
// the cache misses it at all of them.
static uint32_t wholeTexture(RoomTextures &t, uint32_t flags, uint32_t variant)
{
    static const uint8_t kVariants[6] = { DETAIL_WHOLE, DETAIL_TURNED, DETAIL_MIP1, DETAIL_MIP1_TURNED,
                                          DETAIL_MIP2, DETAIL_MIP2_TURNED };
    const Texture &tex = t.textures[flags & FACE_TEXTURE];
    int32_t slot = -1;
    if (tex.reserved)
    {
        const uint32_t mips = tex.size & 7 ? 0 : tex.size & 0x010F ? 1 : 2;
        slot = cachedSlot(t, tex, kVariants[variant]);
        const uint32_t turned = variant & 1;
        const bool small = variant >= 2;
        while (slot < 0 && variant / 2 < mips)
        {
            variant += 2;
            slot = cachedSlot(t, tex, kVariants[variant]);
        }
        if (slot < 0 && small)
        {
            variant = turned;
            slot = cachedSlot(t, tex, kVariants[variant]);
        }
    }
    if (slot < 0)
        return flags;
    flags |= FACE_DETAIL | FACE_DETAIL_WHOLE | (uint32_t(slot) << FACE_DETAIL_SLOT_SHIFT);
    if (variant & 1) {
        flags |= FACE_DETAIL_TURNED;
    }
    if (variant >= 4) {
        flags |= FACE_DETAIL_MIP | FACE_DETAIL_MIP2;
    } else if (variant >= 2) {
        flags |= FACE_DETAIL_MIP;
    }
    return flags;
}

// A texture the cache can take.
static inline bool cacheable(const RoomTextures &t, uint32_t flags)
{
    const Texture &tex = t.textures[flags & FACE_TEXTURE];
    return tex.reserved && tex.reserved - 1 < DETAIL_MAX_KEYS;
}

static const int8_t kWibble[32] = {
    0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0,
    0, 0, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, 0
};

static inline int32_t dot3(const int16_t* m, int32_t x, int32_t y, int32_t z, int32_t t)
{
    return t + m[0] * x + m[1] * y + m[2] * z;
}

template <int N>
static inline int32_t asr16(int32_t v)
{
    return int16_t(uint32_t(v) >> N);
}

#ifdef RENDER_LAB

// A point in room space (vertex units times 4) projected as the room's own
// vertices are (rooms.cpp projectRoomPoint), but for the shade.
static void projectRoomPoint(const FaceRoom &room, const RoomSplit &split, int32_t vx, int32_t vy, int32_t vz,
                             Vertex* res)
{
    const int32_t zMin = 64 << 8, zMax = gViewFar << 8;
    int32_t z = dot3(room.m + 6, vx, vy, vz, room.t[2]);
    uint32_t clip = 0;
    if (z <= zMin) {
        clip = CLIP_PLANE;
        z = zMin;
    }
    if (z >= zMax) {
        clip = CLIP_PLANE;
        z = zMax;
    }
    const int32_t limit16 = 0x7FFF << 8;
    int32_t x = dot3(room.m, vx, vy, vz, room.t[0]);
    int32_t y = dot3(room.m + 3, vx, vy, vz, room.t[1]);
    x = asr16<8>(x < -limit16 ? -limit16 : x > limit16 ? limit16 : x);
    y = asr16<8>(y < -limit16 ? -limit16 : y > limit16 ? limit16 : y);
    z = uint32_t(z) >> 8;

    // render.h projectView
    uint32_t dz = uint32_t(z) >> 4;
    if (dz >= 1281) {
        dz = 1280;
    }
    int16_t dd = split.divTable[dz];
    const int32_t limit = 2047 << 12;
    int32_t px = x * dd, py = y * dd;
    int32_t sx = asr16<12>(px < -limit ? -limit : px > limit ? limit : px) + (FRAME_WIDTH >> 1);
    int32_t sy = asr16<12>(py < -limit ? -limit : py > limit ? limit : py) + (FRAME_HEIGHT >> 1);
    if (uint32_t(sx) > FRAME_WIDTH || uint32_t(sy) > FRAME_HEIGHT)
    {
        clip |= CLIP_FRAME;
        sx = coord(sx);
        sy = coord(sy);
    }
    if (sx < room.clip[0]) clip |= 0x02;
    if (sx > room.clip[2]) clip |= 0x04;
    if (sy < room.clip[1]) clip |= 0x08;
    if (sy > room.clip[3]) clip |= 0x10;
    res->clip = clip;
    res->x = sx;
    res->y = sy;
    res->z = z;

    if (room.ripple && !(clip & CLIP_FRAME))
    {
        res->x = sx + kWibble[(room.phase + sx) & 31];
        res->y = sy + kWibble[(room.phase + sy) & 31];
    }
}

// A quarter of a split quad, culled or filled where a plane cuts it.
static void roomQuarter(CommandList &list, const RoomTextures &t, uint32_t flags, const Vertex* a, const Vertex* b,
                        const Vertex* c, const Vertex* d)
{
    uint32_t clipAnd = a->clip & b->clip & c->clip & d->clip;
    if (culled(clipAnd, a->clip | b->clip | c->clip | d->clip))
    {
        if (!(clipAnd & CLIP_DISCARD)) {
            faceVoid(list, a, b, c, d);
        }
        return;
    }
    roomCommand(list, t, flags, a, b, c, d, zMax(a, b, c, d) >> OT_SHIFT);
}

// A quad crossing the near plane split 2x2 (rooms.cpp faceAddRoomQuadDetail),
// each quarter drawn with a quadrant of its texture, the new corners
// projected from room space: false when there's no room or no cached texture.
static bool roomSplit(const FaceRoom &room, RoomTextures &t, RoomSplit &split, CommandList &list,
                      const RoomQuad &q, const Vertex* v0, const Vertex* v1, const Vertex* v2, const Vertex* v3)
{
    if (split.top - 5 < split.bottom || list.cmd + 4 * 8 > list.end)
        return false;
    const Texture &tex = t.textures[q.flags & FACE_TEXTURE];
    int32_t slot = cachedSlot(t, tex, DETAIL_QUADS);
    if (slot < 0)
        return false;

    const RoomVertex* r[4];
    for (int32_t i = 0; i < 4; i++) {
        r[i] = room.points + q.indices[i] / sizeof(Vertex);
    }
    split.top -= 5;
    Vertex* m01 = split.vertices + split.top;
    Vertex* m12 = m01 + 1;
    Vertex* m23 = m01 + 2;
    Vertex* m30 = m01 + 3;
    Vertex* mc = m01 + 4;
    projectRoomPoint(room, split, (r[0]->x + r[1]->x) << 1, (r[0]->y + r[1]->y) << 1, (r[0]->z + r[1]->z) << 1, m01);
    projectRoomPoint(room, split, (r[1]->x + r[2]->x) << 1, (r[1]->y + r[2]->y) << 1, (r[1]->z + r[2]->z) << 1, m12);
    projectRoomPoint(room, split, (r[2]->x + r[3]->x) << 1, (r[2]->y + r[3]->y) << 1, (r[2]->z + r[3]->z) << 1, m23);
    projectRoomPoint(room, split, (r[3]->x + r[0]->x) << 1, (r[3]->y + r[0]->y) << 1, (r[3]->z + r[0]->z) << 1, m30);
    projectRoomPoint(room, split, r[0]->x + r[1]->x + r[2]->x + r[3]->x, r[0]->y + r[1]->y + r[2]->y + r[3]->y,
                     r[0]->z + r[1]->z + r[2]->z + r[3]->z, mc);
    m01->g = (v0->g + v1->g) >> 1;
    m12->g = (v1->g + v2->g) >> 1;
    m23->g = (v2->g + v3->g) >> 1;
    m30->g = (v3->g + v0->g) >> 1;
    mc->g = (v0->g + v1->g + v2->g + v3->g) >> 2;

    uint32_t flags = (q.flags & ROOM_QUAD_FLAGS) | room.flags | FACE_DETAIL | (uint32_t(slot) << FACE_DETAIL_SLOT_SHIFT);
    roomQuarter(list, t, flags | (0 << FACE_DETAIL_QUAD_SHIFT), v0, m01, mc, m30);
    roomQuarter(list, t, flags | (1 << FACE_DETAIL_QUAD_SHIFT), m01, v1, m12, mc);
    roomQuarter(list, t, flags | (2 << FACE_DETAIL_QUAD_SHIFT), mc, m12, v2, m23);
    roomQuarter(list, t, flags | (3 << FACE_DETAIL_QUAD_SHIFT), m30, mc, m23, v3);
    return true;
}

#else

// The camera on a room quad's front side, worked out in room space: on the
// frame, corners clamped to what VDP1 can address or held at the near plane can
// wind a quad facing the camera the wrong way round, and it would be left out.
static bool roomQuadFacing(const FaceRoom &room, const RoomQuad &q)
{
    const RoomVertex* a = room.points + q.indices[0] / sizeof(Vertex);
    const RoomVertex* b = room.points + q.indices[1] / sizeof(Vertex);
    const RoomVertex* c = room.points + q.indices[2] / sizeof(Vertex);
    int32_t e1x = b->x - a->x, e1y = b->y - a->y, e1z = b->z - a->z;
    int32_t e2x = c->x - a->x, e2y = c->y - a->y, e2z = c->z - a->z;
    // the camera's offset in units of 32: the products, under 2^17 * 2^12,
    // sum within 32 bits (hints.cpp)
    const int32_t corner[3] = { a->x << 8, a->y << 8, a->z << 8 };
    int32_t d[3];
    for (int32_t k = 0; k < 3; k++)
    {
        int32_t v = room.camera[k] - corner[k];
        v = v < -(1 << 17) ? -(1 << 17) : v > (1 << 17) ? 1 << 17 : v;
        d[k] = int16_t(v >> 5);
    }
    return (e1y * e2z - e1z * e2y) * d[0] + (e1z * e2x - e1x * e2z) * d[1] + (e1x * e2y - e1y * e2x) * d[2] < 0;
}

#endif

// A sparse texture (water's flecks, 80% or more see-through) over this many lines
// is left out: VDP1 walks the empty texels as it does the rest, so one such quad
// near the camera can take more than the rest of the frame. As facesroom.s's.
#define SPARSE_LINES    64

#ifdef RENDER_LAB

// The old path the lab's captures were taken with: a quad a plane cuts, or near
// the camera (rooms.cpp faceAddRoomQuads), split or filled where the near plane
// cuts it, else whole, turned when that makes fewer lines, at full res.
static void roomQuadCareful(const FaceRoom &room, RoomTextures &t, RoomSplit &split, CommandList &list,
                            const RoomQuad &q)
{
    const Vertex* a = corner(room.vertices, q.indices[0]);
    const Vertex* b = corner(room.vertices, q.indices[1]);
    const Vertex* c = corner(room.vertices, q.indices[2]);
    const Vertex* d = corner(room.vertices, q.indices[3]);
    uint32_t clipAnd = a->clip & b->clip & c->clip & d->clip;
    uint32_t clipOr = a->clip | b->clip | c->clip | d->clip;
    if ((clipAnd & (CLIP_DISCARD & ~CLIP_PLANE)) || ((clipAnd & CLIP_PLANE) && a->z < gViewFar))
        return;     // wholly beyond a frame edge or behind the near plane
    if (backface(a, b, c))
        return;
    uint32_t flags = (q.flags & ROOM_QUAD_FLAGS) | room.flags;
    const Texture &tex = t.textures[flags & FACE_TEXTURE];
    if (clipOr & CLIP_PLANE)
    {
        bool near = min(min(a->z, b->z), min(c->z, d->z)) < DETAIL_DIST;
        if (near && (tex.attr & TEX_ATTR_SPARSE))
            return;
        if (!(near && tex.reserved && roomSplit(room, t, split, list, q, a, b, c, d))) {
            faceVoid(list, a, b, c, d);
        }
        return;
    }
    int32_t along = max(span(a, d), span(b, c));
    int32_t across = max(span(a, b), span(d, c));
    if ((tex.attr & TEX_ATTR_SPARSE) && min(along, across) >= SPARSE_LINES)
        return;
    uint32_t variant = across < along;
    roomCommand(list, t, wholeTexture(t, flags, variant), a, b, c, d, zMax(a, b, c, d) >> OT_SHIFT);
}

#else

// --- near the camera, as the original ------------------------------------

// The original has no polygon clipping (0603fcec, 06040038, issue 263). VDP1
// lays a texture evenly across a quad, so a quad is given more corners, each
// where it truly lies: within NEAR_SPLIT, or near the camera or far off the
// frame, four quadrants of its texture; a quadrant near the camera or far off
// the frame, four tiles of it. A corner nearer than the plane is projected
// held at the plane, and only a tile with a corner behind the camera, or that
// comes out facing away, is left out. Each piece sorts by its own farthest
// corner. Past NEAR_SPLIT a quad stays whole, the warp too small to see go.
//
// facesroom.s does this itself for a quad with every corner in front of the
// plane and in reach of the frame's coordinates, from the corners' screen places
// and depths. This takes the rest, whose corners only room space can place.

#define NEAR_PLANE      80      // phd_InitWindow's
#define NEAR_SPLIT      4096    // facesroom.s's too
#define NEAR_CODE       __attribute__((section(".text.facesnear")))   // out of the hot code's order (saturn.ld)
#define OFF_FRAME_MAX   128     // new corners are held this close to the frame

// A piece's corner's clip flags: the frame's and edges' as a vertex's, and
#define CLIP_NEAR       0x20    // in front of the camera, nearer than the plane
#define CLIP_BEHIND     0x40    // behind it
#define CLIP_HELD       0x80    // over OFF_FRAME_MAX off the frame

enum NearTest { NEAR_OFF, NEAR_CLEAN, NEAR_SPLIT_AGAIN, NEAR_FLIPPED };

// A piece's corner: where it lies from the camera, << 8, and as drawn.
struct NearPoint
{
    int32_t x, y, z;
    Vertex v;
};

// x, y held near the frame; clip gains what they show.
static inline void nearHold(const FaceRoom &room, int32_t &x, int32_t &y, uint32_t &clip)
{
    if (uint32_t(x) > FRAME_WIDTH || uint32_t(y) > FRAME_HEIGHT) {
        clip |= CLIP_FRAME;
    }
    if (x < -OFF_FRAME_MAX)
    {
        x = -OFF_FRAME_MAX;
        clip |= CLIP_HELD;
    }
    else if (x > FRAME_WIDTH + OFF_FRAME_MAX)
    {
        x = FRAME_WIDTH + OFF_FRAME_MAX;
        clip |= CLIP_HELD;
    }
    if (y < -OFF_FRAME_MAX)
    {
        y = -OFF_FRAME_MAX;
        clip |= CLIP_HELD;
    }
    else if (y > FRAME_HEIGHT + OFF_FRAME_MAX)
    {
        y = FRAME_HEIGHT + OFF_FRAME_MAX;
        clip |= CLIP_HELD;
    }
    if (x < room.clip[0]) clip |= CLIP_LEFT;
    if (x > room.clip[2]) clip |= CLIP_RIGHT;
    if (y < room.clip[1]) clip |= CLIP_TOP;
    if (y > room.clip[3]) clip |= CLIP_BOTTOM;
}

// A corner on to the frame from where it lies (06040d58).
NEAR_CODE static void nearProject(const FaceRoom &room, const RoomSplit &split, NearPoint &p)
{
    int32_t z = p.z;
    uint32_t clip = 0;
    if (z < NEAR_PLANE << 8)
    {
        clip = z <= 0 ? CLIP_BEHIND : CLIP_NEAR;
        z = NEAR_PLANE << 8;
    }
    const int32_t limit16 = 0x7FFF << 8;
    int32_t x = asr16<8>(p.x < -limit16 ? -limit16 : p.x > limit16 ? limit16 : p.x);
    int32_t y = asr16<8>(p.y < -limit16 ? -limit16 : p.y > limit16 ? limit16 : p.y);
    z = uint32_t(z) >> 8;
    uint32_t dz = uint32_t(z) >> 4;
    int32_t dd;
    if (z < VIEW_EXACT)
    {
        volatile int32_t* const divider = (volatile int32_t*)0xFFFFFF00;
        divider[0] = z;         // DVSR
        divider[1] = 1 << 20;   // DVDNT: divides
        dd = divider[1];
    }
    else
    {
        dd = split.divTable[dz >= 1281 ? 1280 : dz];
    }
    const int32_t limit = 2047 << 12;
    int32_t px = x * dd, py = y * dd;
    int32_t sx = asr16<12>(px < -limit ? -limit : px > limit ? limit : px) + (FRAME_WIDTH >> 1);
    int32_t sy = asr16<12>(py < -limit ? -limit : py > limit ? limit : py) + (FRAME_HEIGHT >> 1);
    nearHold(room, sx, sy, clip);
    if (room.ripple && !(clip & CLIP_FRAME))
    {
        sx += kWibble[(room.phase + sx) & 31];
        sy += kWibble[(room.phase + sy) & 31];
    }
    p.v.x = sx;
    p.v.y = sy;
    p.v.z = z;
    p.v.clip = clip;
}

// A corner of the quad itself: as the rooms' projection left it unless nearer
// than the plane.
NEAR_CODE static void nearCorner(const FaceRoom &room, const RoomSplit &split, uint32_t index, NearPoint &p)
{
    const RoomVertex* r = room.points + index / sizeof(Vertex);
    const Vertex* v = corner(room.vertices, index);
    int32_t vx = r->x * 4, vy = r->y * 4, vz = r->z * 4;
    p.x = dot3(room.m, vx, vy, vz, room.t[0]);
    p.y = dot3(room.m + 3, vx, vy, vz, room.t[1]);
    p.z = dot3(room.m + 6, vx, vy, vz, room.t[2]);
    p.v = *v;
    if (p.z < NEAR_PLANE << 8)
    {
        nearProject(room, split, p);
        return;
    }
    int32_t x = v->x, y = v->y;
    uint32_t clip = 0;
    nearHold(room, x, y, clip);
    p.v.x = x;
    p.v.y = y;
    p.v.clip = clip;
}

static inline void nearMid(const FaceRoom &room, const RoomSplit &split, const NearPoint &a, const NearPoint &b,
                           NearPoint &m)
{
    m.x = (a.x + b.x) >> 1;
    m.y = (a.y + b.y) >> 1;
    m.z = (a.z + b.z) >> 1;
    m.v.g = (a.v.g + b.v.g) >> 1;
    nearProject(room, split, m);
}

// The four edges' midpoints and the centre (06040b08): m[0 .. 4] AB, BC, CD,
// DA, centre.
NEAR_CODE static void nearMids(const FaceRoom &room, const RoomSplit &split, const NearPoint &a, const NearPoint &b,
                     const NearPoint &c, const NearPoint &d, NearPoint* m)
{
    nearMid(room, split, a, b, m[0]);
    nearMid(room, split, b, c, m[1]);
    nearMid(room, split, c, d, m[2]);
    nearMid(room, split, d, a, m[3]);
    m[4].x = (a.x + b.x + c.x + d.x) >> 2;
    m[4].y = (a.y + b.y + c.y + d.y) >> 2;
    m[4].z = (a.z + b.z + c.z + d.z) >> 2;
    m[4].v.g = (a.v.g + b.v.g + c.v.g + d.v.g) >> 2;
    nearProject(room, split, m[4]);
}

// What's to be done with a piece (060408a4).
NEAR_CODE static NearTest nearTest(const Vertex &a, const Vertex &b, const Vertex &c, const Vertex &d)
{
    uint32_t clipAnd = a.clip & b.clip & c.clip & d.clip;
    uint32_t clipOr = a.clip | b.clip | c.clip | d.clip;
    if (clipAnd & (CLIP_EDGES | CLIP_BEHIND))
        return NEAR_OFF;
    if ((clipOr & CLIP_BEHIND) || backface(&a, &b, &c))
        return NEAR_FLIPPED;
    return clipOr & (CLIP_NEAR | CLIP_HELD) ? NEAR_SPLIT_AGAIN : NEAR_CLEAN;
}

// A piece's command (06040a60, 060409a6): its material w, its texture at srca, this size.
NEAR_CODE static void nearCommand(CommandList &list, const uint32_t* w, uint32_t flags, uint32_t srca, uint32_t size,
                        const NearPoint &a, const NearPoint &b, const NearPoint &c, const NearPoint &d)
{
    if (list.cmd == list.end)
        return;
    list.cmd[0] = w[0];
    list.cmd[1] = w[1];
    list.cmd[2] = pair(srca >> 3, size);
    roomCorners(list, flags, &a.v, &b.v, &c.v, &d.v, zMax(&a.v, &b.v, &c.v, &d.v) >> OT_SHIFT);
}

// Quadrant 'quarter' in tiles (0604011c); false when there's no having them.
NEAR_CODE static bool nearTiles(const FaceRoom &room, RoomTextures &t, const RoomSplit &split, CommandList &list,
                      const Texture &tex, const uint32_t* w, uint32_t flags, int32_t quarter, const NearPoint &a,
                      const NearPoint &b, const NearPoint &c, const NearPoint &d)
{
    if (tex.size & 0x0101)
        return false;   // VDP1 takes widths by 8; a tile's texels must start on a byte
    int32_t slot = cachedSlot(t, tex, DETAIL_TILES);
    if (slot < 0)
        return false;
    NearPoint m[5];
    nearMids(room, split, a, b, c, d, m);
    const uint32_t size = ((tex.size >> 9) << 8) | ((tex.size & 0xFF) >> 1);
    const uint32_t bytes = (size >> 8) * 4 * (size & 0xFF);
    uint32_t srca = t.pools[0] + slot * DETAIL_SLOT_SIZE + quarter * 4 * bytes;
    const NearPoint* tile[4][4] = { { &a, &m[0], &m[4], &m[3] }, { &m[0], &b, &m[1], &m[4] },
                                    { &m[4], &m[1], &c, &m[2] }, { &m[3], &m[4], &m[2], &d } };
    for (int32_t i = 0; i < 4; i++, srca += bytes)
    {
        const NearPoint* const* p = tile[i];
        NearTest test = nearTest(p[0]->v, p[1]->v, p[2]->v, p[3]->v);
        // the original leaves out a tile with a corner behind the camera, over a
        // cleared frame; held at the plane it still covers what shows of it
        if (test == NEAR_FLIPPED && backface(&p[0]->v, &p[1]->v, &p[2]->v)) {
            faceVoid(list, &p[0]->v, &p[1]->v, &p[2]->v, &p[3]->v);     // no old pixels where it was
        } else if (test != NEAR_OFF) {
            nearCommand(list, w, flags, srca, size, *p[0], *p[1], *p[2], *p[3]);
        }
    }
    return true;
}

// The quad in quadrants (06040038); false when its texture can't have them.
NEAR_CODE static bool nearSplit(const FaceRoom &room, RoomTextures &t, const RoomSplit &split, CommandList &list,
                      const RoomQuad &q, bool triangle)
{
    const Texture &tex = t.textures[q.flags & FACE_TEXTURE];
    int32_t slot = cachedSlot(t, tex, DETAIL_QUADS);
    if (slot < 0)
        return false;
    NearPoint a, b, c, d, m[5];
    nearCorner(room, split, q.indices[0], a);
    nearCorner(room, split, q.indices[1], b);
    nearCorner(room, split, q.indices[2], c);
    if (triangle) {
        d = c;
    } else {
        nearCorner(room, split, q.indices[3], d);
    }
    nearMids(room, split, a, b, c, d, m);

    const uint32_t flags = (q.flags & ROOM_QUAD_FLAGS) | room.flags | FACE_DETAIL;
    uint32_t w[3];
    roomMaterial(t, flags, w);
    const uint32_t bytes = (tex.size >> 8) * 4 * (tex.size & 0xFF);
    uint32_t srca = t.pools[0] + slot * DETAIL_SLOT_SIZE;
    const NearPoint* quarter[4][4] = { { &a, &m[0], &m[4], &m[3] }, { &m[0], &b, &m[1], &m[4] },
                                       { &m[4], &m[1], &c, &m[2] }, { &m[3], &m[4], &m[2], &d } };
    for (int32_t i = 0; i < 4; i++, srca += bytes)
    {
        const NearPoint* const* p = quarter[i];
        NearTest test = nearTest(p[0]->v, p[1]->v, p[2]->v, p[3]->v);
        if (test == NEAR_OFF)
            continue;
        if (test == NEAR_CLEAN || !nearTiles(room, t, split, list, tex, w, flags, i, *p[0], *p[1], *p[2], *p[3])) {
            nearCommand(list, w, flags, srca, tex.size, *p[0], *p[1], *p[2], *p[3]);
        }
    }
    return true;
}

// A room face near the camera: split, filled where the far plane cuts it, or
// whole as facesroom.s DECIDE would have drawn it.
NEAR_CODE static void roomQuadNear(const FaceRoom &room, RoomTextures &t, const RoomSplit &split, CommandList &list,
                         const RoomQuad &q, bool triangle)
{
    const Vertex* a = corner(room.vertices, q.indices[0]);
    const Vertex* b = corner(room.vertices, q.indices[1]);
    const Vertex* c = corner(room.vertices, q.indices[2]);
    const Vertex* d = triangle ? c : corner(room.vertices, q.indices[3]);
    if (a->clip & b->clip & c->clip & d->clip & CLIP_EDGES)
        return;
    const uint32_t cut = (a->clip | b->clip | c->clip | d->clip) & CLIP_PLANE;
    if (cut && !roomQuadFacing(room, q))
        return;     // facesroom.s has tested the others
    const int32_t farthest = zMax(a, b, c, d);
    if (cut && farthest >= gViewFar)
    {
        faceVoid(list, a, b, c, d);
        return;
    }
    const uint32_t flags = (q.flags & ROOM_QUAD_FLAGS) | room.flags;
    const uint32_t index = flags & FACE_TEXTURE;
    const Texture &tex = t.textures[index];
    const bool near = min(min(a->z, b->z), min(c->z, d->z)) < NEAR_PLANE;
    const bool sparse = tex.attr & TEX_ATTR_SPARSE;
    if (near && sparse)
        return;
    const bool coloured = ((flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK) == FACE_TYPE_F;
    const int32_t off = max(max(offFrame(a), offFrame(b)), max(offFrame(c), offFrame(d)));
    if (!sparse && !coloured && tex.reserved && (near || farthest <= NEAR_SPLIT || off > OFF_FRAME_MAX) &&
        nearSplit(room, t, split, list, q, triangle))
        return;
    if (cut)
    {
        faceVoid(list, a, b, c, d);
        return;
    }
    const int32_t along = max(span(a, d), span(b, c));
    const int32_t across = max(span(a, b), span(d, c));
    const uint32_t turned = along > across;
    const RoomMaterial &m = t.materials[index];
    const uint32_t want = t.lod * (turned ? along : across);
    uint32_t variant = turned;
    if (t.lod && m.lines[turned] > want && m.mips)
    {
        variant += m.lines[turned] > 2 * want && m.mips > 1 ? 4 : 2;
    }
    if (sparse && variant < 2 && min(along, across) >= SPARSE_LINES)
        return;
    roomCommand(list, t, coloured ? flags : wholeTexture(t, flags, variant), a, b, c, d, farthest >> OT_SHIFT);
}

#endif

// --- under a floor on VDP2 ------------------------------------------------

#ifndef RENDER_LAB

#define UNDER_MAX       8
#define OPENING_NEAR    (80 << 8)
#define OPENING_LIMIT   32000
#define COLUMN_SHIFT    4
#define COLUMNS         ((FRAME_WIDTH >> COLUMN_SHIFT) + 1)

static int32_t gFloorY = FACES_NO_FLOOR;
static bool gFloorKeep = true;
static struct { int16_t first, end; } gUnder[UNDER_MAX];    // the commands of the rooms under the floor
static int32_t gUnderCount;
#define MASKS_MAX       8
static struct { int16_t first, end; } gMasks[MASKS_MAX];    // the commands of the masks round dips
static int32_t gMaskCount;
static int32_t gDipBox[4];      // x0 y0 x1 y1, round the dips' faces drawn this frame
static int16_t gColumnTop[COLUMNS], gColumnBottom[COLUMNS]; // the rows the openings cover, column by column

void facesFloorSet(int32_t y)
{
    gFloorY = y;
}

void facesFloorKeep(bool on)
{
    gFloorKeep = on;
}

void facesFloorBegin()
{
    gUnderCount = 0;
    gMaskCount = 0;
    gDipBox[0] = gDipBox[1] = 0x7FFF;
    gDipBox[2] = gDipBox[3] = -0x7FFF;
}

// A command's box on the frame. A polygon or distorted sprite's four corners, as the list makes them.
static inline void commandBox(const uint32_t* w, int32_t &x0, int32_t &y0, int32_t &x1, int32_t &y1)
{
    const int32_t ax = int16_t(w[3] >> 16), ay = int16_t(w[3]), bx = int16_t(w[4] >> 16), by = int16_t(w[4]);
    const int32_t cx = int16_t(w[5] >> 16), cy = int16_t(w[5]), dx = int16_t(w[6] >> 16), dy = int16_t(w[6]);
    x0 = min(min(ax, bx), min(cx, dx));
    y0 = min(min(ay, by), min(cy, dy));
    x1 = max(max(ax, bx), max(cx, dx));
    y1 = max(max(ay, by), max(cy, dy));
}

void facesFloorDip(const Command* cmds, int32_t first, int32_t end)
{
    for (int32_t s = first; s < end; s++)
    {
        int32_t x0, y0, x1, y1;
        commandBox(cmds[s].w, x0, y0, x1, y1);
        gDipBox[0] = min(gDipBox[0], x0);
        gDipBox[1] = min(gDipBox[1], y0);
        gDipBox[2] = max(gDipBox[2], x1);
        gDipBox[3] = max(gDipBox[3], y1);
    }
}

bool facesFloorDipDrawn()
{
    return gDipBox[0] <= gDipBox[2];
}

void facesFloorMasks(int32_t first, int32_t end)
{
    if (first == end || gMaskCount == MASKS_MAX)
        return;
    gMasks[gMaskCount].first = int16_t(first);
    gMasks[gMaskCount].end = int16_t(end);
    gMaskCount++;
}

// Each mask's slot becomes the clip, jumping to its polygon at the list's end, which goes on where it went;
// so the buckets are joined by now.
static void masksDraw(CommandList &list, Command* cmds)
{
    const int32_t bx0 = max(gDipBox[0], 0), by0 = max(gDipBox[1], 0);
    const int32_t bx1 = min(gDipBox[2], FRAME_WIDTH - 1), by1 = min(gDipBox[3], FRAME_HEIGHT - 1);
    for (int32_t i = 0; i < gMaskCount; i++)
    {
        for (int32_t s = gMasks[i].first; s < gMasks[i].end; s++)
        {
            uint32_t* w = cmds[s].w;
            int32_t x0, y0, x1, y1;
            commandBox(w, x0, y0, x1, y1);
            if (x0 > bx1 || x1 < bx0 || y0 > by1 || y1 < by0 || bx0 > bx1 || by0 > by1 || list.cmd == list.end)
            {
                w[0] = (w[0] & 0xFFFF) | (CMD_SKIP_JUMP << 16);
                continue;
            }
            uint32_t* face = list.cmd;
            face[0] = (w[0] & 0xFFFF) | ((CMD_POLYGON | CMD_JUMP) << 16);
            face[1] = pair(PMOD_NO_ECD_SPD | PMOD_USER_CLIP, 0);
            for (int32_t k = 3; k < 8; k++) {
                face[k] = w[k];
            }
            w[0] = ((CMD_USER_CLIP | CMD_JUMP) << 16) | list.link;
            w[3] = pair(bx0, by0);
            w[5] = pair(bx1, by1);
            list.cmd += 8;
            list.grd += 2;
            list.link += sizeof(Command) >> 3;
            list.grdLink += 1 << 16;
        }
    }
}

void facesFloorRoom(const FaceRoom &room, const RoomLevel* level, int32_t first, int32_t end)
{
    if (first == end || level[room.room].yTop < gFloorY - 2 || gFloorY == FACES_NO_FLOOR || !gFloorKeep ||
        gUnderCount == UNDER_MAX)
        return;
    gUnder[gUnderCount].first = int16_t(first);
    gUnder[gUnderCount].end = int16_t(end);
    gUnderCount++;
}

// a * b / c by the divider, its 64 bit dividend
static inline int32_t mulDiv(int32_t a, int32_t b, int32_t c)
{
    const int64_t n = int64_t(a) * b;
    volatile int32_t* const divider = (volatile int32_t*)0xFFFFFF00;
    divider[0] = c;                 // DVSR
    divider[4] = int32_t(n >> 32);  // DVDNTH
    divider[5] = int32_t(n);        // DVDNTL: divides
    return divider[5];
}

// a * b >> 16
static inline int32_t mulQ16(int32_t a, int32_t b)
{
    int32_t high, low;
    asm volatile("dmuls.l %2, %3\n\tsts mach, %0\n\tsts macl, %1\n\txtrct %0, %1"
                 : "=&r"(high), "=&r"(low) : "r"(a), "r"(b) : "macl", "mach");
    return low;
}

// An edge of an opening's outline into the columns it crosses.
static void columnsEdge(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x0 > x1)
    {
        int32_t t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    if (x1 < 0 || x0 >= FRAME_WIDTH)
        return;
    const int32_t slope = x1 > x0 ? mulDiv(y1 - y0, 1 << 16, x1 - x0) : 0;
    const int32_t first = max(x0, 0) >> COLUMN_SHIFT, last = min(x1, FRAME_WIDTH - 1) >> COLUMN_SHIFT;
    for (int32_t c = first; c <= last; c++)
    {
        const int32_t xa = max(x0, c << COLUMN_SHIFT), xb = min(x1, ((c + 1) << COLUMN_SHIFT) - 1);
        int32_t ya = y0 + mulQ16(slope, xa - x0), yb = x1 > x0 ? y0 + mulQ16(slope, xb - x0) : y1;
        if (ya > yb)
        {
            const int32_t t = ya; ya = yb; yb = t;
        }
        // an edge's end rounds: a row over, whichever way, is a row more kept
        if (ya - 1 < gColumnTop[c]) {
            gColumnTop[c] = int16_t(max(ya - 1, -OPENING_LIMIT));
        }
        if (yb + 1 > gColumnBottom[c]) {
            gColumnBottom[c] = int16_t(min(yb + 1, OPENING_LIMIT));
        }
    }
}

// An opening's corners, in the room's coordinates, onto the frame and into the columns.
static void columnsOpening(const FaceRoom &room, const int16_t* corners, int32_t yTop)
{
    int32_t v[4][3];
    for (int32_t i = 0; i < 4; i++)
    {
        const int32_t px = corners[3 * i] >> 6, py = (corners[3 * i + 1] - yTop) >> 6, pz = corners[3 * i + 2] >> 6;
        v[i][0] = dot3(room.m, px, py, pz, room.t[0]);
        v[i][1] = dot3(room.m + 3, px, py, pz, room.t[1]);
        v[i][2] = dot3(room.m + 6, px, py, pz, room.t[2]);
    }
    int32_t x[8], y[8];
    int32_t n = 0;
    for (int32_t i = 0; i < 4; i++)
    {
        const int32_t* a = v[i];
        const int32_t* b = v[(i + 1) & 3];
        int32_t cut[3];
        const int32_t* out[2];
        int32_t m = 0;
        if (a[2] >= OPENING_NEAR) {
            out[m++] = a;
        }
        if ((a[2] >= OPENING_NEAR) != (b[2] >= OPENING_NEAR))
        {
            const int32_t along = OPENING_NEAR - a[2], whole = b[2] - a[2];
            cut[0] = a[0] + mulDiv(b[0] - a[0], along, whole);
            cut[1] = a[1] + mulDiv(b[1] - a[1], along, whole);
            cut[2] = OPENING_NEAR;
            out[m++] = cut;
        }
        for (int32_t k = 0; k < m; k++)
        {
            volatile int32_t* const divider = (volatile int32_t*)0xFFFFFF00;
            divider[0] = out[k][2] >> 8;    // DVSR
            divider[1] = 1 << 20;           // DVDNT: divides
            const int32_t dd = divider[1];
            int32_t vx = out[k][0] >> 8, vy = out[k][1] >> 8;
            vx = vx < -32767 ? -32767 : vx > 32767 ? 32767 : vx;
            vy = vy < -32767 ? -32767 : vy > 32767 ? 32767 : vy;
            const int32_t sx = ((vx * dd) >> 12) + (FRAME_WIDTH >> 1);
            const int32_t sy = ((vy * dd) >> 12) + (FRAME_HEIGHT >> 1);
            x[n] = sx < -OPENING_LIMIT ? -OPENING_LIMIT : sx > OPENING_LIMIT ? OPENING_LIMIT : sx;
            y[n] = sy < -OPENING_LIMIT ? -OPENING_LIMIT : sy > OPENING_LIMIT ? OPENING_LIMIT : sy;
            n++;
        }
    }
    for (int32_t i = 0; n >= 3 && i < n; i++)
    {
        const int32_t j = i + 1 == n ? 0 : i + 1;
        columnsEdge(x[i], y[i], x[j], y[j]);
    }
}

// The rows the openings at the floor's height cover; false if the camera isn't above the floor.
static bool columnsMake(const FaceRoom* rooms, uint32_t count, const RoomLevel* level, const void* portals)
{
    if (rooms[0].camera[1] + level[rooms[0].room].yTop >= gFloorY)
        return false;
    for (int32_t c = 0; c < COLUMNS; c++)
    {
        gColumnTop[c] = 0x7FFF;
        gColumnBottom[c] = -0x7FFF;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const FaceRoom &room = rooms[i];
        const RoomLevel &r = level[room.room];
        if (r.yTop >= gFloorY - 2)
            continue;
        const int16_t* p = (const int16_t*)portals + r.first * 16;  // Portal: to, normal, corners
        for (uint32_t k = 0; k < r.portals; k++, p += 16)
        {
            const int32_t y = p[5];
            if (p[2] && y >= gFloorY - 2 && y <= gFloorY + 2 && level[uint16_t(p[0])].yTop >= gFloorY - 2) {
                columnsOpening(room, p + 4, r.yTop);
            }
        }
    }
    return true;
}

// A room's commands kept to the openings. Its slot becomes the clip, jumping to the face at the
// list's end, which goes on where it went; so the buckets are joined by now.
static void underRoom(CommandList &list, Command* cmds, int32_t first, int32_t end)
{
    for (int32_t s = first; s < end; s++)
    {
        uint32_t* w = cmds[s].w;
        if (w[0] & (CMD_SKIP << 16))
            continue;
        const int32_t ax = int16_t(w[3] >> 16), ay = int16_t(w[3]), bx = int16_t(w[4] >> 16), by = int16_t(w[4]);
        const int32_t cx = int16_t(w[5] >> 16), cy = int16_t(w[5]), dx = int16_t(w[6] >> 16), dy = int16_t(w[6]);
        const int32_t fx0 = max(min(min(ax, bx), min(cx, dx)), 0), fx1 = min(max(max(ax, bx), max(cx, dx)), FRAME_WIDTH - 1);
        const int32_t fy0 = min(min(ay, by), min(cy, dy)), fy1 = max(max(ay, by), max(cy, dy));
        if (fx0 > fx1)
            continue;
        int32_t top = 0x7FFF, bottom = -0x7FFF;
        for (int32_t c = fx0 >> COLUMN_SHIFT; c <= fx1 >> COLUMN_SHIFT; c++)
        {
            top = min(top, gColumnTop[c]);
            bottom = max(bottom, gColumnBottom[c]);
        }
        if (top <= fy0 && bottom >= fy1)
            continue;       // all of it can show
        top = max(top, fy0);
        bottom = min(bottom, fy1);
        if (top > bottom)
        {
            w[0] = (w[0] & 0xFFFF) | (CMD_SKIP_JUMP << 16);     // none of it can
            continue;
        }
        if (list.cmd == list.end)
            continue;
        uint32_t* face = list.cmd;
        for (int32_t i = 0; i < 8; i++) {
            face[i] = w[i];
        }
        face[1] |= PMOD_USER_CLIP << 16;
        w[0] = ((CMD_USER_CLIP | CMD_JUMP) << 16) | list.link;
        w[3] = pair(fx0, max(top, 0));
        w[5] = pair(fx1, min(bottom, FRAME_HEIGHT - 1));
        list.cmd += 8;
        list.grd += 2;
        list.link += sizeof(Command) >> 3;
        list.grdLink += 1 << 16;
    }
}

void facesUnderFloor(const FaceRoom* rooms, uint32_t count, const RoomLevel* level, const void* portals, CommandList &list,
                     Command* cmds)
{
    masksDraw(list, cmds);
    if (!gUnderCount || !columnsMake(rooms, count, level, portals))
        return;
    for (int32_t i = 0; i < gUnderCount; i++) {
        underRoom(list, cmds, gUnder[i].first, gUnder[i].end);
    }
}

#endif

// --- water's shimmers -------------------------------------------------------

#ifndef RENDER_LAB

#define SHIMMER_FAR         (20 * 1024)
#define SHIMMER_ROOM_LEFT   32      // the list's commands they leave for what's kept to a floor's openings
#define PIECES_MAX          16
#define SHIMMER_TABLES      12
#define SHIMMER_BAG         64
#define SHIMMER_BORN_MAX    128     // the most born in a frame
#define SHIMMER_HEIGHTS     16      // of water, in a level
#define SHIMMER_RUNS        32      // of a room and a height, in a generation
#define SHIMMER_LIMIT       (1280 - 5 - 45)     // shimmers.s S_LIMIT: leaves BUCKET_ON within the buckets

struct ShimmerPiece
{
    uint32_t art;                   // srca / 8 << 16 | CMDSIZE
    uint8_t halfW, halfH;           // half its width and height in pixels
    uint8_t w, kind, count, pad;    // its width; which kind; how often the texture has it
    uint32_t pad2;
};
static_assert(sizeof(ShimmerPiece) == 16, "shimmers.s");

// A shimmer, from its birth (shimmers.s).
struct ShimmerLife
{
    int16_t x, z;           // in the world, 8 to a unit
    uint8_t level;          // how dim
    uint8_t piece;          // in gShimmerPieces, in bytes
};
static_assert(sizeof(ShimmerLife) == 6, "shimmers.s");

// A run of a generation's shimmers: they lie in one room, at one height.
struct ShimmerRun
{
    uint8_t count, room;
    uint16_t view;          // in gShimmerViews, in bytes
};
static_assert(sizeof(ShimmerRun) == 4, "shimmers.s");

// The view's rows of world x and z, twice over, and for each the sum for a height of water.
struct ShimmerView
{
    int16_t z0, z1;
    int32_t zSum;
    int16_t x0, x1;
    int32_t xSum;
    int16_t y0, y1;
    int32_t ySum;
};
static_assert(sizeof(ShimmerView) == 24, "shimmers.s");

// By its age, coming and going: half tone, and how many tones dimmer.
static const uint8_t kShimmerAges[8] = { 0x80 | 4, 2, 1, 0, 0, 1, 2, 0x80 | 4 };

static int32_t gShimmerOneIn = 150;
static ShimmerPiece gShimmerPieces[PIECES_MAX] __attribute__((aligned(16)));
static uint8_t gShimmerBag[SHIMMER_BAG];            // pieces by chance, as offsets into gShimmerPieces
static uint32_t gShimmerW1[8][2 * SHIMMER_TABLES];  // by age, then level: art's first word pair, then a line's
static int32_t gShimmerSpriteDd;                    // from this divTable value, art; below it, a line
static int32_t gShimmerDepth[81];   // z in steps of 256 to sectors owed, for a height of 1, 4 bits of fraction
static ShimmerLife gShimmerLives[8][SHIMMER_BORN_MAX] __attribute__((aligned(16)));    // by the frame they were born, of 8
static ShimmerRun gShimmerRuns[8][SHIMMER_RUNS];
static int32_t gShimmerRunCount[8];
static uint32_t gShimmerLastFrame;
static uint8_t gShimmerSeen[256];                   // by room: the frame's low byte if it's in this frame's walk
static ShimmerView gShimmerViews[SHIMMER_HEIGHTS] __attribute__((aligned(16)));
static int16_t gShimmerHeightY[SHIMMER_HEIGHTS];
static int32_t gShimmerHeightCount;
static int32_t gShimmerSlotZ[SHIMMER_HEIGHTS][8];   // the z past which a sector doesn't owe so many
static int32_t gShimmerSlotOwed[SHIMMER_HEIGHTS];   // ... for what it owes at that height's distance

void facesShimmersDensity(int32_t oneIn)
{
    gShimmerOneIn = oneIn;
}

// An equal share of the bag to dashes and to chunks, each piece as often as the texture has it.
static void shimmersBag(int32_t pieces)
{
    int32_t totals[2] = { 0, 0 };
    for (int32_t p = 0; p < pieces; p++) {
        totals[gShimmerPieces[p].kind >> 1] += gShimmerPieces[p].count;
    }
    const int32_t shares = (totals[0] != 0) + (totals[1] != 0);
    int32_t slot = 0, done = 0;
    for (int32_t k = 0; k < 2; k++)
    {
        if (!totals[k])
            continue;
        done++;
        int32_t sum = 0;
        const int32_t first = slot, room = done * SHIMMER_BAG / shares - first;
        for (int32_t p = 0; p < pieces; p++)
        {
            if (gShimmerPieces[p].kind >> 1 != k)
                continue;
            sum += gShimmerPieces[p].count;
            for (const int32_t upTo = first + sum * room / totals[k]; slot < upTo; slot++) {
                gShimmerBag[slot] = uint8_t(p * sizeof(ShimmerPiece));
            }
        }
    }
    while (slot < SHIMMER_BAG) {
        gShimmerBag[slot++] = 0;
    }
}

void facesShimmersInit(uint32_t vram, const uint8_t* art)
{
    // the frame's area of a square sector at depth z from a height h is 1024^2 F^2 h / z^3, F^2 about
    // 210 x 256; sectors owe one lit shimmer for so many pixels, and more so as they go off into the distance
    for (int32_t i = 0; i <= 80; i++)
    {
        const int64_t z = (i + (i ? 0 : 1)) * 256 + 128;
        gShimmerDepth[i] = int32_t(((((int64_t(1048576) * 53760 / (z * z)) << 20) / z) * (8192 + z) / 8192) >> 12);
    }
    for (int32_t i = 0; i < 8; i++) {
        gShimmerRunCount[i] = 0;
    }
    gShimmerHeightCount = 0;
    for (int32_t i = 0; i < SHIMMER_HEIGHTS; i++) {
        gShimmerSlotOwed[i] = -1;
    }
    // no room for art: lines alone, of this piece's width
    gShimmerPieces[0].w = 5;
    gShimmerPieces[0].kind = 2;
    gShimmerPieces[0].count = 1;
    gShimmerSpriteDd = 0x7FFFFFFF;
    int32_t made = 0;
    uint32_t tables = 0;
    uint16_t colours[SHIMMER_TABLES] = {};
    if (vram && art)
    {
        uint32_t at = (vram + 7) & ~7u;
        const uint32_t end = vram + SHIMMER_VRAM_BYTES;
        volatile uint16_t* const base = (volatile uint16_t*)VDP1_VRAM;
        const int32_t own = (art[0] << 8) | art[1];
        const int32_t ownR = own & 31, ownG = (own >> 5) & 31, ownB = (own >> 10) & 31;
        tables = at >> 3;
        for (int32_t t = 0; t < SHIMMER_TABLES; t++)
        {
            const int32_t add = 8 - 2 * t;
            const int32_t r = X_CLAMP_5(ownR + add), g = X_CLAMP_5(ownG + add), b = X_CLAMP_5(ownB + add);
            colours[t] = uint16_t(0x8000 | (b << 10) | (g << 5) | r);
            for (int32_t i = 0; i < 16; i++) {
                base[(at + t * 32 + i * 2) >> 1] = i ? colours[t] : 0;
            }
        }
        at += SHIMMER_TABLES * 32;
        const int32_t pieces = min(int32_t(art[2]), PIECES_MAX);
        const uint8_t* source = art + 4;
        for (int32_t p = 0; p < pieces; p++)
        {
            const int32_t w = source[0], h = source[1];
            const int32_t wide = (w + 7) & ~7;
            if (at + wide / 2 * h > end)
                break;
            ShimmerPiece &piece = gShimmerPieces[made];
            piece.w = uint8_t(w);
            piece.kind = source[2];
            piece.count = source[3];
            source += 4;
            for (int32_t y = 0; y < h; y++)
            {
                const int32_t row = (source[2 * y] << 8) | source[2 * y + 1];
                for (int32_t x = 0; x < wide; x += 4)
                {
                    uint32_t word = 0;
                    for (int32_t k = 0; k < 4; k++)
                    {
                        const int32_t xx = x + k;
                        const bool on = xx < w && (row & (0x8000 >> xx));
                        word = (word << 4) | (on ? 1 : 0);
                    }
                    base[(at + y * (wide / 2) + x / 2) >> 1] = uint16_t(word);
                }
            }
            piece.art = ((at >> 3) << 16) | ((wide >> 3) << 8) | h;
            piece.halfW = uint8_t(w >> 1);
            piece.halfH = uint8_t(h >> 1);
            at = (at + wide / 2 * h + 7) & ~7u;
            made++;
            source += 2 * h;
        }
        if (made) {
            gShimmerSpriteDd = 256;     // a pixel of the texture is a quarter of the frame's pixels and more
        }
    }
    shimmersBag(max(made, 1));
    for (int32_t age = 0; age < 8; age++)
    {
        const int32_t dimmer = kShimmerAges[age] & 0x7F;
        const uint32_t mesh = kShimmerAges[age] & 0x80 ? PMOD_MESH : 0;
        for (int32_t level = 0; level < SHIMMER_TABLES; level++)
        {
            const int32_t tone = min(level + dimmer, SHIMMER_TABLES - 1);
            gShimmerW1[age][level] = pair(PMOD_ECD_OFF | PMOD_LUT16 | mesh, tables + tone * 4);
            gShimmerW1[age][SHIMMER_TABLES + level] = pair(PMOD_NO_ECD_SPD, colours[tone]);
        }
    }
}

static inline int32_t absOf(int32_t v)
{
    return v < 0 ? -v : v;
}

static_assert(offsetof(ShimmerSheet, frame) == 32 && offsetof(ShimmerSheet, runs) == 44 &&
              offsetof(ShimmerSheet, wx) == 72 && offsetof(ShimmerSheet, oz) == 96 &&
              offsetof(ShimmerSheet, lineShift) == 124 && offsetof(ShimmerSheet, top) == 176, "shimmers.s");

// One side of the room's clip, for facesShimmersBornAsm: a value linear in a sector's x and z.
static void shimmerSide(ShimmerSide &p, int32_t side, int32_t sign, int32_t o, int32_t e, int32_t f, int32_t oz, int32_t ez, int32_t fz)
{
    p.o = sign * (o - side * (oz >> 8));
    p.e = sign * (e - ((side * ez) >> 8));
    p.f = sign * (f - ((side * fz) >> 8));
    p.past = ((absOf(p.e) + absOf(p.f)) >> 1) + 512;    // 512: what the sums round off
}

// a * b >> 16
static inline int32_t shimmerQ16(int32_t a, int32_t b)
{
    int32_t high, low;
    asm volatile("dmuls.l %2, %3\n\tsts mach, %0\n\tsts macl, %1\n\txtrct %0, %1"
                 : "=&r"(high), "=&r"(low) : "r"(a), "r"(b) : "macl", "mach");
    return low;
}

static inline int16_t shimmerRow(int32_t m)
{
    return int16_t(m >= 16384 ? 32767 : m <= -16384 ? -32767 : m * 2);
}

// A height of water's view: in order of first seen in the level, so shimmers kept can go by it.
static int32_t shimmerHeightAt(int32_t y)
{
    for (int32_t i = 0; i < gShimmerHeightCount; i++)
    {
        if (gShimmerHeightY[i] == y)
            return i * int32_t(sizeof(ShimmerView));
    }
    if (gShimmerHeightCount == SHIMMER_HEIGHTS)
        return -1;
    gShimmerHeightY[gShimmerHeightCount] = int16_t(y);
    return gShimmerHeightCount++ * int32_t(sizeof(ShimmerView));
}

// Who has which of the frame's shimmers: the first to come has the new ones and this frame's
// generation, each older generation whoever takes it; and that the older ones may be begun on.
static uint8_t gShimmerClaims[8] __attribute__((aligned(16)));
static volatile uint32_t gShimmerReady __attribute__((aligned(16)));

void facesShimmersReset()
{
    for (int32_t k = 0; k < 8; k++) {
        *uncached(gShimmerClaims + k) = 0;
    }
    *uncached(&gShimmerReady) = 0;
}

// This frame's new shimmers, the views all generations are drawn by first.
static void shimmersBorn(ShimmerSheet &s, const FaceRoom* rooms, uint32_t count, const RoomLevel* level,
                         const RoomGeometry* geometry, uint32_t frame)
{
    const int16_t* const m = rooms[0].m;
    for (uint32_t f = gShimmerLastFrame + 1, k = 0; f <= frame && k < 8; f++, k++) {
        gShimmerRunCount[f & 7] = 0;
    }
    gShimmerLastFrame = frame;

    s.depth = gShimmerDepth;
    s.bag = gShimmerBag;

    // the camera in the world
    const RoomLevel &first = level[rooms[0].room];
    const int32_t camX = rooms[0].camera[0] + (first.x << 8);
    const int32_t camY = rooms[0].camera[1] + first.yTop;
    const int32_t camZ = rooms[0].camera[2] + (first.z << 8);
    const int16_t x0 = shimmerRow(m[0]), x1 = shimmerRow(m[2]);
    const int16_t y0 = shimmerRow(m[3]), y1 = shimmerRow(m[5]);
    const int16_t z0 = shimmerRow(m[6]), z1 = shimmerRow(m[8]);
    const int32_t xBase = -int32_t((int64_t(x0) * camX + int64_t(x1) * camZ) >> 3);
    const int32_t yBase = -int32_t((int64_t(y0) * camX + int64_t(y1) * camZ) >> 3);
    const int32_t zBase = -int32_t((int64_t(z0) * camX + int64_t(z1) * camZ) >> 3);

    // each height's view; a height first seen below gets its own as it's seen
    const auto makeView = [&](int32_t h)
    {
        const int32_t dy = gShimmerHeightY[h] - camY;
        ShimmerView &v = gShimmerViews[h];
        v.z0 = z0, v.z1 = z1, v.x0 = x0, v.x1 = x1, v.y0 = y0, v.y1 = y1;
        v.zSum = zBase + ((m[7] * dy) >> 2);
        v.xSum = xBase + ((m[1] * dy) >> 2);
        v.ySum = yBase + ((m[4] * dy) >> 2);
    };
    for (int32_t h = 0; h < gShimmerHeightCount; h++) {
        makeView(h);
    }

    const uint8_t seen = uint8_t(frame);
    for (uint32_t i = 0; i < count; i++) {
        gShimmerSeen[uint8_t(rooms[i].room)] = seen;
    }
    *uncached(&gShimmerReady) = 1;

    // the new ones
    ShimmerRun* const runs = gShimmerRuns[frame & 7];
    int32_t runCount = 0;
    ShimmerLife* const lives = gShimmerLives[frame & 7];
    s.lives = lives;
    s.livesEnd = lives + SHIMMER_BORN_MAX;
    const int32_t owed = (16 * 256) / gShimmerOneIn;
    bool roomLeft = true;
    for (uint32_t i = 0; i < count; i++)
    {
        const FaceRoom &room = rooms[i];
        const uint16_t* after = (const uint16_t*)geometry[room.room].water;
        if (!after || !after[3] || !roomLeft)
            continue;
        const uint8_t* water = (const uint8_t*)(after + 3);
        const RoomLevel &r = level[room.room];
        const int16_t* const rm = room.m;
        const int32_t ex = rm[0] * 16, ey = rm[3] * 16, ez = rm[6] * 16;    // a sector along x, on the view
        const int32_t fx = rm[2] * 16, fy = rm[5] * 16, fz = rm[8] * 16;    // ... along z
        s.turn = room.room * 97;
        s.wx = (r.x << 5) + 64;
        s.wz = (r.z << 5) + 64;
        s.ez = ez;
        s.fz = fz;
        s.x0 = room.clip[0];
        s.y0 = room.clip[1];
        s.x1 = room.clip[2];
        s.y1 = room.clip[3];
        const int32_t reach = (absOf(ez) + absOf(fz)) >> 1;
        s.reachZ = reach;
        s.nearZ = (NEAR_PLANE << 8) - reach;
        const int32_t farZ = (SHIMMER_FAR << 8) + reach;
        const int32_t cx0 = room.clip[0] - (FRAME_WIDTH >> 1), cy0 = room.clip[1] - (FRAME_HEIGHT >> 1);
        const int32_t cx1 = room.clip[2] - (FRAME_WIDTH >> 1), cy1 = room.clip[3] - (FRAME_HEIGHT >> 1);

        int32_t sheets = (water[0] << 8) | water[1];
        const uint8_t* p = water + 2;
        for (; sheets > 0 && roomLeft; sheets--)
        {
            const int32_t y = int16_t((p[0] << 8) | p[1]);
            const int32_t up = p[2];
            const int32_t bx0 = p[4], bz0 = p[5], bx1 = p[6], bz1 = p[7];
            const uint8_t* const counts = p + 8;
            p += 16;
            int32_t sectors = 0;
            for (int32_t k = 0; k < 8; k++) {
                sectors += counts[k];
            }
            const uint8_t* const list = p;
            p += sectors * 3 + (sectors & 1);
            const int32_t relY = y - r.yTop;
            const int32_t height = room.camera[1] - relY;
            const int32_t known = gShimmerHeightCount;
            const int32_t viewAt = shimmerHeightAt(y);
            if (gShimmerHeightCount != known) {
                makeView(known);
            }
            if ((height < 0) != (up != 0) || viewAt < 0 || runCount == SHIMMER_RUNS)
                continue;
            s.view = (const uint8_t*)gShimmerViews + viewAt;
            s.owed = absOf(height) * owed;
            // for the first 8 slots, the z past which a sector doesn't owe that many: once a frame a height
            const int32_t heightIndex = viewAt / int32_t(sizeof(ShimmerView));
            int32_t* const slotZ = gShimmerSlotZ[heightIndex];
            if (gShimmerSlotOwed[heightIndex] != s.owed)
            {
                gShimmerSlotOwed[heightIndex] = s.owed;
                int32_t step = 80;
                slotZ[0] = SHIMMER_FAR << 8;
                for (int32_t k = 1; k < 8; k++)
                {
                    while (step > 0 && shimmerQ16(gShimmerDepth[step], s.owed) < k << 4) {
                        step--;
                    }
                    slotZ[k] = (step + 1) << 16;
                }
            }
            const int32_t vy = relY >> 6;
            // the sheet's first sector's middle
            const int32_t ox = rm[1] * vy + room.t[0] + ((ex + fx) >> 1);
            const int32_t oy = rm[4] * vy + room.t[1] + ((ey + fy) >> 1);
            s.oz = rm[7] * vy + room.t[2] + ((ez + fz) >> 1);
            shimmerSide(s.right, cx1, 1, ox, ex, fx, s.oz, ez, fz);
            shimmerSide(s.left, cx0, -1, ox, ex, fx, s.oz, ez, fz);
            shimmerSide(s.bottom, cy1, 1, oy, ey, fy, s.oz, ez, fz);
            shimmerSide(s.top, cy0, -1, oy, ey, fy, s.oz, ez, fz);
            const int32_t nearest = s.oz + (ez > 0 ? ez * bx0 : ez * bx1) + (fz > 0 ? fz * bz0 : fz * bz1) - reach;
            ShimmerLife* const from = (ShimmerLife*)s.lives;
            const uint8_t* q = list;
            for (uint32_t k = 0; k < 8 && roomLeft; k++)
            {
                // those starting on this frame in 8: this frame's slot is this one of the first 8
                const int32_t n = counts[k];
                const uint32_t slot = (frame - k) & 7;
                s.oneZ = min(slotZ[slot] + reach, farZ);
                if (n && nearest <= s.oneZ && !facesShimmersBornAsm(&s, q, n, slot)) {
                    roomLeft = false;
                }
                q += n * 3;
            }
            const int32_t born = int32_t((ShimmerLife*)s.lives - from);
            if (born)
            {
                runs[runCount].count = uint8_t(born);
                runs[runCount].room = uint8_t(room.room);
                runs[runCount].view = uint16_t(viewAt);
                runCount++;
            }
        }
    }
    gShimmerRunCount[frame & 7] = runCount;
}

int32_t facesShimmersFrame(const FaceRoom* rooms, uint32_t count, const RoomLevel* level, const RoomGeometry* geometry,
                           CommandList &list, uint32_t frame)
{
    if (!gShimmerOneIn || !count)
        return 0;
    ShimmerSheet s;
    s.list = &list;
    s.stop = list.end - SHIMMER_ROOM_LEFT * 8;
    s.divTable = divTable;
    s.pieces = gShimmerPieces;
    s.views = gShimmerViews;
    s.seen = gShimmerSeen;
    s.frame = frame;
    s.spriteDd = gShimmerSpriteDd;
    s.limit = SHIMMER_LIMIT;

    uint32_t* const began = list.cmd;
    const bool first = tasClaim(uncached(gShimmerClaims));
    if (first)
    {
        shimmersBorn(s, rooms, count, level, geometry, frame);
    }
    else
    {
        while (!*uncached(&gShimmerReady)) {
            busPause();
        }
    }
    for (uint32_t age = first ? 0 : 1; age < 8; age++)
    {
        if (age && !tasClaim(uncached(gShimmerClaims + age)))
            continue;
        const uint32_t born = (frame - age) & 7;
        s.runs = gShimmerRunCount[born];
        if (!s.runs)
            continue;
        s.lineShift = (kShimmerAges[age] & 0x80) != 0;
        if (!facesShimmersDrawAsm(&s, gShimmerLives[born], (const uint8_t*)gShimmerRuns[born], gShimmerW1[age]))
            break;
    }
    return (list.cmd - began) / (sizeof(Command) / sizeof(uint32_t));
}

#endif

// --- rooms through facesroom.s ------------------------------------------

static_assert(sizeof(RoomMaterial) == 64 && offsetof(RoomMaterial, key) == 8 &&
              offsetof(RoomMaterial, mips) == 12 && offsetof(RoomMaterial, lines) == 14 &&
              offsetof(RoomMaterial, sizes) == 18 && offsetof(RoomMaterial, frame) == 32 &&
              offsetof(RoomMaterial, w2) == 36 && sizeof(RoomVariant) == 16 &&
              offsetof(RoomVariant, shading) == 14 && offsetof(RoomRun, parity) == 16 &&
              offsetof(RoomRun, list) == 44 && offsetof(RoomRun, plain) == 56 &&
              offsetof(RoomRun, variants) == 60 && offsetof(RoomRun, room) == 64 &&
              offsetof(RoomRun, farBucket) == 268 && offsetof(RoomRun, carefulTri) == 272 &&
              offsetof(RoomRun, quads) == 276 && offsetof(RoomRun, pieceW0) == 292 &&
              offsetof(RoomRun, pieceMaterial) == 300 && offsetof(RoomRun, wibble) == 304 &&
              offsetof(RoomRun, phase) == 308 && offsetof(RoomRun, window) == 312 &&
              offsetof(RoomRun, tiles) == 328 && offsetof(RoomRun, near) == 332 &&
              offsetof(RoomRun, voidWay) == 336 &&
              offsetof(RoomRun, boxNear) == 340 && offsetof(RoomRun, boxCount) == 344 &&
              offsetof(RoomRun, boxes) == 348 && offsetof(RoomRun, points) == 352 &&
              offsetof(RoomRun, fullOut) == 356 &&
              sizeof(RoomBox) == 20 && offsetof(RoomBox, lo) == 8 && offsetof(RoomBox, hi) == 14 &&
              offsetof(CommandList, voidX0) == 48 &&
              offsetof(CommandList, voidArea) == 64 &&
              offsetof(RoomMaterial, quadrant) == 30 && offsetof(RoomMaterial, quads) == 60 &&
              offsetof(FaceRoom, points) == 8 && offsetof(FaceRoom, quadsCount) == 16 &&
              offsetof(FaceRoom, facing) == 20 && offsetof(FaceRoom, camera) == 32 &&
              offsetof(DetailSlot, frame) == 4 && sizeof(DetailSlot) == 8, "facesroom.s's layouts");

void facesRoomMaterials(const RoomTextures &t, RoomMaterial* materials, int32_t count)
{
    for (int32_t i = 0; i < count; i++)
    {
        const Texture &tex = t.textures[i];
        RoomMaterial &m = materials[i];
        uint32_t w[3];
        const uint32_t cached = FACE_DETAIL | FACE_DETAIL_WHOLE;
        roomMaterial(t, i | cached, w);
        m.w0 = w[0];
        m.w1 = w[1];
        m.key = cacheable(t, i) ? tex.reserved - 1 : -1;
        m.colour = tex.color;
        m.mips = tex.size & 7 ? 0 : tex.size & 0x010F ? 1 : 2;
        m.skip = ((tex.attr & TEX_ATTR_AKILL) != 0) | (tex.attr & TEX_ATTR_SPARSE) | (63 << 2);
        m.lines[0] = (tex.size >> 8) << 6;
        m.lines[1] = (tex.size & 0xFF) << 3;
        for (uint32_t v = 0; v < 6; v++)
        {
            uint32_t level = v >> 1;
            uint32_t flags = i | cached | (v & 1 ? FACE_DETAIL_TURNED : 0) |
                             (level ? FACE_DETAIL_MIP : 0) | (level == 2 ? FACE_DETAIL_MIP2 : 0);
            roomMaterial(t, flags, w);
            m.sizes[v] = w[2] & 0xFFFF;
            m.w2[v] = 0;
        }
        m.quadrant = (tex.size >> 8) * (tex.size & 0xFF) / 2;
        m.frame = ~t.frame;
        m.quads = 0;
    }
}

#ifdef RENDER_LAB
static void roomCareful(RoomRun* run, const RoomQuad* q)
{
    roomQuadCareful(*run->room, *run->textures, *run->split, *run->list, *q);
}
#else
NEAR_CODE static void roomCareful(RoomRun* run, const RoomQuad* q)
{
    roomQuadNear(*run->room, *run->textures, *run->split, *run->list, *q, false);
}

NEAR_CODE static void roomCarefulTri(RoomRun* run, const RoomQuad* q)
{
    roomQuadNear(*run->room, *run->textures, *run->split, *run->list, *q, true);
}

NEAR_CODE static int32_t roomTiles(RoomRun* run, uint32_t key, uint32_t size)
{
    RoomTextures &t = *run->textures;
    int32_t slot = int32_t(t.keys[DETAIL_TILES][key]) - 1;
    if (slot < 0)
    {
        slot = t.miss(key, size, DETAIL_TILES);
        if (slot < 0)
            return 0;
    }
    t.slots[slot].frame = t.frame;
    return (t.pools[0] + slot * DETAIL_SLOT_SIZE) >> 3;
}
#endif

void facesRoomsBegin(RoomRun &run, RoomTextures &t, RoomSplit &split, CommandList &list)
{
    static const uint8_t kVariants[6] = { DETAIL_WHOLE, DETAIL_TURNED, DETAIL_MIP1, DETAIL_MIP1_TURNED,
                                          DETAIL_MIP2, DETAIL_MIP2_TURNED };
    run.materials = t.materials;
    run.frame = t.frame;
    run.lod = t.lod;
    run.fullOut = ~t.frame;
    run.end = list.end;
    run.heads = list.heads;
    run.tails = list.tails;
    run.shadeGouraud = list.shadeGouraud;
    run.shade = list.shade;
    run.shadeLevel = list.shadeLevel;
    run.list = &list;
    run.miss = t.miss;
    run.careful = roomCareful;
#ifdef RENDER_LAB
    run.carefulTri = nullptr;
    run.near = nullptr;
#else
    run.carefulTri = roomCarefulTri;
    run.near = facesRoomNear;
    run.quads.keys = t.keys[DETAIL_QUADS];
    run.quads.frames = &t.slots[0].frame;
    run.quads.pool = t.pools[0] >> 3;
    run.quads.step = DETAIL_SLOT_SIZE >> 3;
    run.quads.variant = DETAIL_QUADS;
    run.tiles = roomTiles;
#endif
    for (uint32_t v = 0; v < 6; v++)
    {
        RoomVariant &rv = run.dry[v];
        uint32_t level = v >> 1;
        rv.keys = t.keys[kVariants[v]];
        rv.frames = &t.slots[detailPoolStart(level)].frame;
        rv.pool = t.pools[level] >> 3;
        rv.step = (DETAIL_SLOT_SIZE >> 2 * level) >> 3;
        rv.size = offsetof(RoomMaterial, sizes) + 2 * v;
        rv.turned = v & 1;
        rv.shading = level ? SHADING_FAR : SHADING_NEAR;
        rv.variant = kVariants[v];
        run.wet[v] = rv;
        run.wet[v].shading = SHADING_WATER;
    }
    run.textures = &t;
    run.split = &split;
    run.farBucket = gViewFar >> OT_SHIFT;
    run.voidWay = *uncached(&gFacesVoidWay);
    run.boxes = run.roomBoxes;
    run.seen = nullptr;
    run.seenCount = 0;
}

#ifdef STEPWISE
StepMark gStepMarks[2][STEP_MARKS];
uint32_t gStepHead[2], gStepUploaded[2];
#endif

// A room point's coordinate is its byte << 8, so past a side at v is a byte over
// roomByteUnder(v) or under roomByteOver(v), held to what's beyond any byte.
#define ROOM_BYTES  256

static inline int32_t roomByteUnder(int32_t v)
{
    v >>= 8;
    return v < -1 ? -1 : v > ROOM_BYTES ? ROOM_BYTES : v;
}

static inline int32_t roomByteOver(int32_t v)
{
    v = -(-v >> 8);
    return v < -1 ? -1 : v > ROOM_BYTES ? ROOM_BYTES : v;
}

// The frame's boxes over the room's window that some of it can lie behind.
static void facesRoomBoxes(const FaceRoom &room, RoomRun &run)
{
    int32_t n = 0, nearest = 0x7FFF;
    const int32_t* w = room.clip;
    for (int32_t i = 0; i < run.seenCount; i++)
    {
        const BoxSeen &s = run.seen[i];
        if (s.x1 < w[0] || s.x0 > w[2] || s.y1 < w[1] || s.y0 > w[3])
            continue;
        RoomBox &b = run.roomBoxes[n];
        bool some = true;
        for (int32_t k = 0; k < 3; k++)
        {
            int32_t lo = roomByteUnder(s.lo[k] + room.camera[k]);
            int32_t hi = roomByteOver(s.hi[k] + room.camera[k]);
            some = some && lo < ROOM_BYTES - 1 && hi > 0;
            b.lo[k] = int16_t(lo);
            b.hi[k] = int16_t(hi);
        }
        if (!some)
            continue;
        b.x0 = s.x0;
        b.y0 = s.y0;
        b.x1 = s.x1;
        b.y1 = s.y1;
        nearest = s.near < nearest ? s.near : nearest;
        n++;
    }
    run.boxCount = n;
    run.boxNear = nearest;
}

void facesRoom(const FaceRoom &room, RoomRun &run)
{
#ifdef STEPWISE
    stepMark(0, *run.list, STEP_ROOM | room.room);
#endif
    bool water = room.flags & FACE_WATER;
    run.vertices = room.vertices;
    run.plain = water ? SHADING_WATER : SHADING_PLAIN;
    run.variants = water ? run.wet : run.dry;
    run.room = &room;
    run.wibble = room.ripple ? kWibble : nullptr;
    run.phase = room.phase;
    for (int32_t i = 0; i < 4; i++) {
        run.window[i] = room.clip[i];
    }
    run.points = room.points;
    facesRoomBoxes(room, run);
    facesRoomAsm(&room, &run);
}

const FaceSet* facesShadow()
{
    struct Shadow
    {
        FaceSet set;
        FaceQuad quads[3];
    };
    static const Shadow shadow = {
        { 3, 0, 0, 0 },
        { { { 0 * 8, 1 * 8, 2 * 8, 7 * 8 }, MAT_SHADOW, 0 },
          { { 7 * 8, 2 * 8, 3 * 8, 6 * 8 }, MAT_SHADOW, 0 },
          { { 6 * 8, 3 * 8, 4 * 8, 5 * 8 }, MAT_SHADOW, 0 } }
    };
    return &shadow.set;
}

// facesother.s's layouts
static_assert(offsetof(CommandList, sprites) == 44 && offsetof(Face, indices) == 8 && sizeof(Sprite) == 16 &&
              offsetof(Sprite, attr) == 6, "facesother.s's layouts");

}

#ifdef RENDER_LAB
#include "labfaces.inc"
#endif
