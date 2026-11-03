// Faces (faces.h): the commands' list, the hinted meshes' faces, the room
// faces facesroom.s leaves to C (cut by a plane, or near the camera), the
// room materials, the shadows' faces.

#include "faces.h"
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
    if (list.cmd == list.end)
        return;
    int32_t x0 = max(min(min(a->x, b->x), min(c->x, d->x)), 0);
    int32_t y0 = max(min(min(a->y, b->y), min(c->y, d->y)), 0);
    int32_t x1 = min(max(max(a->x, b->x), max(c->x, d->x)), FRAME_WIDTH - 1);
    int32_t y1 = min(max(max(a->y, b->y), max(c->y, d->y)), FRAME_HEIGHT - 1);
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
        while (slot < 0 && variant / 2 < mips)
        {
            variant += 2;
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
#ifdef RENDER_LAB
    int16_t dd = split.divTable[dz];
#else
    // as rooms' near vertices are (vertices.h RoomProject.zExact)
    int16_t dd = z < VIEW_EXACT ? (1 << 20) / z : split.divTable[dz];
#endif
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

// A sparse texture (water's flecks, 80% or more see-through) over this many lines
// is left out: VDP1 walks the empty texels as it does the rest, so one such quad
// near the camera can take more than the rest of the frame. As facesroom.s's.
#define SPARSE_LINES    64

// A corner this far off the frame and its quad is cut on the screen (roomQuadCut).
#define CUT_OFF_FRAME   256

// The point halfway between two of a quad's corners on the screen, or the mean
// of two such points.
static void cutPoint(const FaceRoom &room, const Vertex* a, const Vertex* b, Vertex* res)
{
    int32_t x = (a->x + b->x) >> 1, y = (a->y + b->y) >> 1;
    uint32_t clip = 0;
    if (uint32_t(x) > FRAME_WIDTH || uint32_t(y) > FRAME_HEIGHT) clip |= CLIP_FRAME;
    if (x < room.clip[0]) clip |= CLIP_LEFT;
    if (x > room.clip[2]) clip |= CLIP_RIGHT;
    if (y < room.clip[1]) clip |= CLIP_TOP;
    if (y > room.clip[3]) clip |= CLIP_BOTTOM;
    res->x = x;
    res->y = y;
    res->z = (a->z + b->z) >> 1;
    res->g = (a->g + b->g) >> 1;
    res->clip = clip;
}

static inline void cutQuarter(CommandList &list, const RoomTextures &t, uint32_t flags, const Vertex* a,
                              const Vertex* b, const Vertex* c, const Vertex* d, int32_t depth)
{
    if (!(a->clip & b->clip & c->clip & d->clip & CLIP_DISCARD)) {
        roomCommand(list, t, flags, a, b, c, d, depth);
    }
}

// A quad reaching far off the frame cut 2x2 on the screen, each quarter drawn
// with a quadrant of its texture. VDP1 draws a quad as lines between two of its
// edges, which lays the texture bilinearly over the screen quad, so quarters cut
// at the corners' means draw the same picture and the cut can't be seen coming
// or going. Quarters off the frame are left out, and the rest walk less of what
// lies off it (VDP1 steps through every pixel before a line reaches the frame).
// In the whole quad's bucket, as it would have been. False when there's no room
// or no cached texture.
static bool roomQuadCut(const FaceRoom &room, RoomTextures &t, CommandList &list, const RoomQuad &q,
                        const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
{
    if (list.cmd + 4 * 8 > list.end)
        return false;
    int32_t slot = cachedSlot(t, t.textures[q.flags & FACE_TEXTURE], DETAIL_QUADS);
    if (slot < 0)
        return false;

    Vertex ab, bc, cd, da, mid;
    cutPoint(room, a, b, &ab);
    cutPoint(room, b, c, &bc);
    cutPoint(room, c, d, &cd);
    cutPoint(room, d, a, &da);
    cutPoint(room, &ab, &cd, &mid);
    uint32_t flags = (q.flags & ROOM_QUAD_FLAGS) | room.flags | FACE_DETAIL | (uint32_t(slot) << FACE_DETAIL_SLOT_SHIFT);
    int32_t depth = zMax(a, b, c, d) >> OT_SHIFT;
    cutQuarter(list, t, flags | (0 << FACE_DETAIL_QUAD_SHIFT), a, &ab, &mid, &da, depth);
    cutQuarter(list, t, flags | (1 << FACE_DETAIL_QUAD_SHIFT), &ab, b, &bc, &mid, depth);
    cutQuarter(list, t, flags | (2 << FACE_DETAIL_QUAD_SHIFT), &mid, &bc, c, &cd, depth);
    cutQuarter(list, t, flags | (3 << FACE_DETAIL_QUAD_SHIFT), &da, &mid, &cd, d, depth);
    return true;
}

// A quad a plane cuts, or near the camera (rooms.cpp faceAddRoomQuads): split
// or filled where the near plane cuts it, cut on the screen if it reaches far off
// the frame, else whole, turned when that makes fewer lines, at full res.
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
#ifdef RENDER_LAB
    if (backface(a, b, c))
        return;
#else
    if (!roomQuadFacing(room, q))
        return;
#endif
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
#ifndef RENDER_LAB
    if ((clipOr & CLIP_FRAME) && tex.reserved &&
        max(max(offFrame(a), offFrame(b)), max(offFrame(c), offFrame(d))) >= CUT_OFF_FRAME &&
        roomQuadCut(room, t, list, q, a, b, c, d))
        return;
#endif
    uint32_t variant = across < along;
    roomCommand(list, t, wholeTexture(t, flags, variant), a, b, c, d, zMax(a, b, c, d) >> OT_SHIFT);
}

// --- near the camera, as the original ------------------------------------

#ifndef RENDER_LAB

// The original has no polygon clipping (0603fcec, 06040038, issue 263). VDP1
// lays a texture evenly across a quad, so a quad is given more corners, each
// where it truly lies: within NEAR_SPLIT, or near the camera or far off the
// frame, four quadrants of its texture; a quadrant near the camera or far off
// the frame, four tiles of it. A corner nearer than the plane is projected
// held at the plane, and only a tile with a corner behind the camera, or that
// comes out facing away, is left out. Each piece sorts by its own farthest
// corner. Past NEAR_SPLIT a quad stays whole, the warp too small to see go.

#define NEAR_PLANE      80      // phd_InitWindow's
#define NEAR_SPLIT      4096
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

static bool gNearRetail = true;

void facesNear(bool retail)
{
    gNearRetail = retail;
}

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
static void nearProject(const FaceRoom &room, const RoomSplit &split, NearPoint &p)
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
static void nearCorner(const FaceRoom &room, const RoomSplit &split, uint32_t index, NearPoint &p)
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
static void nearMids(const FaceRoom &room, const RoomSplit &split, const NearPoint &a, const NearPoint &b,
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
static NearTest nearTest(const Vertex &a, const Vertex &b, const Vertex &c, const Vertex &d)
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
static void nearCommand(CommandList &list, const uint32_t* w, uint32_t flags, uint32_t srca, uint32_t size,
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
static bool nearTiles(const FaceRoom &room, RoomTextures &t, const RoomSplit &split, CommandList &list,
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
        if (test != NEAR_OFF && test != NEAR_FLIPPED) {
            nearCommand(list, w, flags, srca, size, *p[0], *p[1], *p[2], *p[3]);
        }
    }
    return true;
}

// The quad in quadrants (06040038); false when its texture can't have them.
static bool nearSplit(const FaceRoom &room, RoomTextures &t, const RoomSplit &split, CommandList &list,
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
static void roomQuadNear(const FaceRoom &room, RoomTextures &t, const RoomSplit &split, CommandList &list,
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

// --- rooms through facesroom.s ------------------------------------------

static_assert(sizeof(RoomMaterial) == 64 && offsetof(RoomMaterial, key) == 8 &&
              offsetof(RoomMaterial, mips) == 12 && offsetof(RoomMaterial, lines) == 14 &&
              offsetof(RoomMaterial, sizes) == 18 && offsetof(RoomMaterial, frame) == 32 &&
              offsetof(RoomMaterial, w2) == 36 && sizeof(RoomVariant) == 16 &&
              offsetof(RoomVariant, shading) == 14 && offsetof(RoomRun, parity) == 16 &&
              offsetof(RoomRun, list) == 44 && offsetof(RoomRun, plain) == 56 &&
              offsetof(RoomRun, variants) == 60 && offsetof(RoomRun, room) == 64 &&
              offsetof(RoomRun, farBucket) == 268 && offsetof(RoomRun, carefulTri) == 272 &&
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
        m.skip = ((tex.attr & TEX_ATTR_AKILL) != 0) | (tex.attr & TEX_ATTR_SPARSE);
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
        m.spare = 0;
        m.frame = ~t.frame;
        m.spare2 = 0;
    }
}

static void roomCareful(RoomRun* run, const RoomQuad* q)
{
#ifndef RENDER_LAB
    if (gNearRetail)
    {
        roomQuadNear(*run->room, *run->textures, *run->split, *run->list, *q, false);
        return;
    }
#endif
    roomQuadCareful(*run->room, *run->textures, *run->split, *run->list, *q);
}

#ifndef RENDER_LAB
static void roomCarefulTri(RoomRun* run, const RoomQuad* q)
{
    roomQuadNear(*run->room, *run->textures, *run->split, *run->list, *q, true);
}
#endif

void facesRoomsBegin(RoomRun &run, RoomTextures &t, RoomSplit &split, CommandList &list)
{
    static const uint8_t kVariants[6] = { DETAIL_WHOLE, DETAIL_TURNED, DETAIL_MIP1, DETAIL_MIP1_TURNED,
                                          DETAIL_MIP2, DETAIL_MIP2_TURNED };
    run.materials = t.materials;
    run.frame = t.frame;
    run.lod = t.lod;
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
#else
    run.carefulTri = gNearRetail ? roomCarefulTri : nullptr;
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
}

void facesRoom(const FaceRoom &room, RoomRun &run)
{
    bool water = room.flags & FACE_WATER;
    run.vertices = room.vertices;
    run.plain = water ? SHADING_WATER : SHADING_PLAIN;
    run.variants = water ? run.wet : run.dry;
    run.room = &room;
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
