// Stage 3 the new way (faces.h): what facesMeshAsm does, in C to check it
// against; the faces few enough for C; the join.

#include "faces.h"
#include "hw.h"

#include <stddef.h>
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
                   const uint8_t* shadeLevel, ShadeCache* shade)
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
    list.shadeLevel = shadeLevel;
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

static void quadCommand(CommandList &list, const Material &m, Shading shading, const Vertex* a, const Vertex* b,
                        const Vertex* c, const Vertex* d, int32_t depth)
{
    uint32_t w0 = m.w0;
    if (mirrored(a, b, c, d)) {
        w0 ^= CMD_FLIP_H << 16;
    }
    uint32_t* w = list.cmd;
    w[1] = m.w1;
    w[2] = m.w2;
    w[3] = a->xy;
    w[4] = b->xy;
    w[5] = c->xy;
    w[6] = d->xy;
    shade(list, m, shading, a, b, c, d);
    linked(list, w0, depth);
}

// v0, x, y: the fourth corner x + y - v0.
static void parallelogramCommand(CommandList &list, const Material &m, Shading shading, const Vertex* v0,
                                 const Vertex* x, const Vertex* y, bool atD, int32_t depth)
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
        shade(list, m, shading, x, x, y, v0);
    }
    else
    {
        w[3] = v0->xy;
        w[4] = x->xy;
        w[5] = p;
        w[6] = y->xy;
        shade(list, m, shading, v0, x, y, y);
    }
    linked(list, m.w0, depth);
}

void facesMeshC(const FaceSet* set, const Vertex* vertices, uint32_t mode, CommandList &list)
{
    bool inside = mode & MESH_INSIDE;
    Shading shading = mode & MESH_WATER ? SHADE_WATER : SHADE_NEAR;
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
        quadCommand(list, list.materials[q->material], shading, a, b, c, d, depth);
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
            quadCommand(list, m, shading, a, b, c, c, uint32_t(a->z + b->z + c->z + c->z) >> (2 + OT_SHIFT));
            continue;
        }
        if (t->flags & TRI_SWAP)
        {
            const Vertex* s = b; b = c; c = s;
        }
        parallelogramCommand(list, m, shading, a, b, c, t->flags & TRI_V0_AT_D,
                             uint32_t(a->z + b->z + c->z + c->z) >> (2 + OT_SHIFT));
    }
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

#define FAR_BUCKET      1280    // a corner at the far plane: VIEW_MAX_F >> FIXED_SHIFT >> OT_SHIFT
#define VIEW_FAR        20480   // ... VIEW_MAX_F >> FIXED_SHIFT
#define NEAR_SPLIT_DIST 2048    // quads whose corner A is nearer go the careful way
#define DETAIL_DIST     4096    // quads crossing the near plane nearer than this split 2x2

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
        uint32_t size = tex.size;
        if (flags & FACE_DETAIL)
        {
            uint32_t slot = (flags >> FACE_DETAIL_SLOT_SHIFT) & 0x7F;
            if (flags & FACE_DETAIL_WHOLE)
            {
                uint32_t level = flags & FACE_DETAIL_MIP ? (flags & FACE_DETAIL_MIP2 ? 2 : 1) : 0;
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
        w[1] = pair(pmod, tex.colr);
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

// A room face's command from its flags and corners (flush.s, render.cpp
// faceCommand): turned as its texture is, mirrored off the frame, shaded.
static void roomCommand(CommandList &list, const RoomTextures &t, uint32_t flags, const Vertex* a,
                        const Vertex* b, const Vertex* c, const Vertex* d, int32_t depth)
{
    if (list.cmd == list.end)
        return;
    uint32_t* w = list.cmd;
    roomMaterial(t, flags, w);
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

// A transparent fill behind everything over the corners' box on the frame,
// where a plane cuts a room face away (render.cpp faceCommand's void).
static void roomVoid(CommandList &list, const Vertex* a, const Vertex* b, const Vertex* c, const Vertex* d)
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
    return variant < DETAIL_MIP1 ? 0 : (variant - DETAIL_MIP1) / 2 + 1;
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
// 'level' turned or not (variant: level * 2 + turned), in its flags; drawn
// in its mean colour when the cache misses it.
static uint32_t wholeTexture(RoomTextures &t, uint32_t flags, uint32_t variant)
{
    static const uint8_t kVariants[6] = { DETAIL_WHOLE, DETAIL_TURNED, DETAIL_MIP1, DETAIL_MIP1_TURNED,
                                          DETAIL_MIP2, DETAIL_MIP2_TURNED };
    const Texture &tex = t.textures[flags & FACE_TEXTURE];
    int32_t slot = tex.reserved ? cachedSlot(t, tex, kVariants[variant]) : -1;
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

// faces.s TURN and the mip level: the variant (level * 2 + turned) a face
// takes, its lines turned when that makes fewer, at the first level with
// about a texel a pixel along them.
static uint32_t roomVariant(const RoomTextures &t, uint32_t flags, const Vertex* a, const Vertex* b,
                            const Vertex* c, const Vertex* d)
{
    int32_t turned = max(span(a, d), span(b, c));   // the lines' length turned
    int32_t across = max(span(a, b), span(d, c));   // ... and as it is
    uint32_t variant = 0;
    if (turned > across)
    {
        variant = 1;
        across = turned;
    }
    const Texture &tex = t.textures[flags & FACE_TEXTURE];
    if (!t.lod)
        return variant;
    // 4 x the texels along a line: the whole texture's width, or turned its
    // height
    uint32_t size = tex.size;
    uint32_t texels = variant ? (size & 0xFF) << 3 : (size >> 8) << 6;
    uint32_t pixels = uint32_t(t.lod) * uint32_t(across & 0xFFFF);
    if (texels <= pixels || (size & 7))
        return variant;
    variant += 2;
    if (texels <= pixels * 2 || (size & 0x010F))
        return variant;
    return variant + 2;
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
    const int32_t zMin = 64 << 8, zMax = VIEW_FAR << 8;
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
            roomVoid(list, a, b, c, d);
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

// A quad a plane cuts, or near the camera (rooms.cpp faceAddRoomQuads): split
// or filled where the near plane cuts it, else whole, turned when that makes
// fewer lines, at full res.
static void roomQuadCareful(const FaceRoom &room, RoomTextures &t, RoomSplit &split, CommandList &list,
                            const RoomQuad &q)
{
    const Vertex* a = corner(room.vertices, q.indices[0]);
    const Vertex* b = corner(room.vertices, q.indices[1]);
    const Vertex* c = corner(room.vertices, q.indices[2]);
    const Vertex* d = corner(room.vertices, q.indices[3]);
    uint32_t clipAnd = a->clip & b->clip & c->clip & d->clip;
    uint32_t clipOr = a->clip | b->clip | c->clip | d->clip;
    if ((clipAnd & (CLIP_DISCARD & ~CLIP_PLANE)) || ((clipAnd & CLIP_PLANE) && a->z < VIEW_FAR))
        return;     // wholly beyond a frame edge or behind the near plane
    if (backface(a, b, c))
        return;
    uint32_t flags = (q.flags & ROOM_QUAD_FLAGS) | room.flags;
    const Texture &tex = t.textures[flags & FACE_TEXTURE];
    if (clipOr & CLIP_PLANE)
    {
        if (!(min(min(a->z, b->z), min(c->z, d->z)) < DETAIL_DIST && tex.reserved &&
              roomSplit(room, t, split, list, q, a, b, c, d))) {
            roomVoid(list, a, b, c, d);
        }
        return;
    }
    uint32_t variant = max(span(a, b), span(d, c)) < max(span(a, d), span(b, c));
    roomCommand(list, t, wholeTexture(t, flags, variant), a, b, c, d, zMax(a, b, c, d) >> OT_SHIFT);
}

// faces.s's room quads: count of them from q, facing the camera (no back
// face test) or not; parity: the frame's, a quad taking its turn and mip
// level afresh on frames of its own and keeping them in between (-1: every
// frame).
static void roomQuads(const FaceRoom &room, RoomTextures &t, RoomSplit &split, CommandList &list, RoomQuad* q,
                      int32_t count, bool facing, int32_t parity)
{
    for (int32_t left = count; left > 0; left--, q++)
    {
        if (list.cmd == list.end)
            return;
        const Vertex* a = corner(room.vertices, q->indices[0]);
        const Vertex* b = corner(room.vertices, q->indices[1]);
        const Vertex* c = corner(room.vertices, q->indices[2]);
        if (!facing && backface(a, b, c))
            continue;
        const Vertex* d = corner(room.vertices, q->indices[3]);
        uint32_t clipAnd = a->clip & b->clip & c->clip & d->clip;
        uint32_t clipOr = a->clip | b->clip | c->clip | d->clip;
        if (culled(clipAnd, clipOr))
        {
            if (clipAnd & (CLIP_DISCARD & ~CLIP_PLANE))
                continue;                                   // wholly beyond a frame edge
            if (!(clipAnd & CLIP_PLANE)) {
                roomQuadCareful(room, t, split, list, *q);  // cut by a plane
            } else if ((a->z >> OT_SHIFT) == FAR_BUCKET) {
                roomVoid(list, a, b, c, d);                 // wholly past the far plane
            }
            continue;
        }
        if (a->z < NEAR_SPLIT_DIST)
        {
            roomQuadCareful(room, t, split, list, *q);
            continue;
        }

        // its turn and mip level: kept from its last deciding frame, or decided
        // now (and kept, for a texture the cache takes)
        uint32_t flags = (q->flags & ROOM_QUAD_FLAGS) | room.flags;
        uint32_t kept = q->flags >> 24;
        uint32_t variant;
        if (parity >= 0 && ((parity ^ left) & 1) && (kept & 8)) {
            variant = kept & 7;
        }
        else
        {
            variant = roomVariant(t, flags, a, b, c, d);
            if (parity >= 0 && cacheable(t, flags)) {
                q->flags = (q->flags & ROOM_QUAD_FLAGS) | ((variant | 8) << 24);
            }
        }
        roomCommand(list, t, wholeTexture(t, flags, variant), a, b, c, d, zMax(a, b, c, d) >> OT_SHIFT);
    }
}

// Of a run of quads square to an axis, the ones facing the camera, from
// *first (rooms.cpp facingQuads).
static int32_t facingQuads(const FaceRoom &room, const RoomQuad* quads, int32_t count, int32_t run, int32_t &first)
{
    int32_t axis = run >> 1;
    int32_t c = room.camera[axis] + ((run & 1) ? 0 : 1);
    int32_t lo = 0, hi = count;
    while (lo < hi)
    {
        int32_t mid = (lo + hi) >> 1;
        const uint8_t* v = (const uint8_t*)(room.points + quads[mid].indices[0] / sizeof(Vertex));
        if ((int32_t(v[axis]) << 8) < c) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (run & 1)
    {
        first = 0;
        return lo;
    }
    first = lo;
    return count - lo;
}

void facesRoomC(const FaceRoom &room, RoomTextures &t, RoomSplit &split, CommandList &list)
{
    int32_t offset = 0;
    for (int32_t run = 0; run <= 6; run++)
    {
        int32_t count = run < 6 ? room.facing[run] : room.quadsCount - offset;
        int32_t first = 0;
        int32_t n = run < 6 ? facingQuads(room, room.quads + offset, count, run, first) : count;
        if (n > 0)
        {
            int32_t parity = int32_t(t.frame & 1) ^ ((room.quadsCount ^ (offset + first + n)) & 1);
            roomQuads(room, t, split, list, room.quads + offset + first, n, run < 6, parity);
        }
        offset += count;
    }

    const RoomQuad* q = room.triangles;
    for (int32_t i = 0; i < room.trianglesCount; i++, q++)
    {
        if (list.cmd == list.end)
            return;
        const Vertex* a = corner(room.vertices, q->indices[0]);
        const Vertex* b = corner(room.vertices, q->indices[1]);
        const Vertex* c = corner(room.vertices, q->indices[2]);
        uint32_t clipAnd = a->clip & b->clip & c->clip;
        uint32_t clipOr = a->clip | b->clip | c->clip;
        if (culled(clipAnd, clipOr))
        {
            // cut by a plane, or wholly past the far one: filled
            if ((clipAnd & (CLIP_DISCARD & ~CLIP_PLANE)) ||
                ((clipAnd & CLIP_PLANE) && (a->z >> OT_SHIFT) != FAR_BUCKET) || backface(a, b, c))
                continue;
            roomVoid(list, a, b, c, c);
            continue;
        }
        if (backface(a, b, c))
            continue;
        uint32_t variant = roomVariant(t, q->flags, a, b, c, c);
        uint32_t flags = q->flags | room.flags | FACE_TRIANGLE;
        roomCommand(list, t, wholeTexture(t, flags, variant), a, b, c, c, zMax(a, b, c, c) >> OT_SHIFT);
    }
}

// --- rooms through facesroom.s ------------------------------------------

static_assert(sizeof(RoomMaterial) == 32 && offsetof(RoomMaterial, key) == 8 &&
              offsetof(RoomMaterial, mips) == 12 && offsetof(RoomMaterial, lines) == 14 &&
              offsetof(RoomMaterial, sizes) == 18 && sizeof(RoomVariant) == 16 &&
              offsetof(RoomVariant, shading) == 14 && offsetof(RoomRun, parity) == 16 &&
              offsetof(RoomRun, list) == 44 && offsetof(RoomRun, facing) == 56 &&
              offsetof(RoomRun, variants) == 64 && offsetof(RoomRun, room) == 160 &&
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
        m.skip = (tex.attr & TEX_ATTR_AKILL) != 0;
        m.lines[0] = (tex.size >> 8) << 6;
        m.lines[1] = (tex.size & 0xFF) << 3;
        for (uint32_t v = 0; v < 6; v++)
        {
            uint32_t level = v >> 1;
            uint32_t flags = i | cached | (v & 1 ? FACE_DETAIL_TURNED : 0) |
                             (level ? FACE_DETAIL_MIP : 0) | (level == 2 ? FACE_DETAIL_MIP2 : 0);
            roomMaterial(t, flags, w);
            m.sizes[v] = w[2] & 0xFFFF;
        }
        m.spare = 0;
    }
}

static void roomCareful(RoomRun* run, const RoomQuad* q)
{
    roomQuadCareful(*run->room, *run->textures, *run->split, *run->list, *q);
}

void facesRoom(const FaceRoom &room, RoomTextures &t, RoomSplit &split, CommandList &list)
{
    static const uint8_t kVariants[6] = { DETAIL_WHOLE, DETAIL_TURNED, DETAIL_MIP1, DETAIL_MIP1_TURNED,
                                          DETAIL_MIP2, DETAIL_MIP2_TURNED };
    bool water = room.flags & FACE_WATER;
    RoomRun run;
    run.vertices = room.vertices;
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
    run.plain = water ? SHADING_WATER : SHADING_PLAIN;
    for (uint32_t v = 0; v < 6; v++)
    {
        RoomVariant &rv = run.variants[v];
        uint32_t level = v >> 1;
        rv.keys = t.keys[kVariants[v]];
        rv.frames = &t.slots[detailPoolStart(level)].frame;
        rv.pool = t.pools[level] >> 3;
        rv.step = (DETAIL_SLOT_SIZE >> 2 * level) >> 3;
        rv.size = offsetof(RoomMaterial, sizes) + 2 * v;
        rv.turned = v & 1;
        rv.shading = water ? SHADING_WATER : level ? SHADING_FAR : SHADING_NEAR;
        rv.variant = kVariants[v];
    }
    run.room = &room;
    run.textures = &t;
    run.split = &split;

    int32_t offset = 0;
    for (int32_t r = 0; r <= 6; r++)
    {
        int32_t count = r < 6 ? room.facing[r] : room.quadsCount - offset;
        int32_t first = 0;
        int32_t n = r < 6 ? facingQuads(room, room.quads + offset, count, r, first) : count;
        if (n > 0)
        {
            run.facing = r < 6;
            run.parity = int32_t(t.frame & 1) ^ ((room.quadsCount ^ (offset + first + n)) & 1);
            facesRoomQuadsAsm(room.quads + offset + first, n, &run);
        }
        offset += count;
    }
    if (room.trianglesCount) {
        facesRoomTrianglesAsm(room.triangles, room.trianglesCount, &run);
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
