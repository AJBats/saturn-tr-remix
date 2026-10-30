// The faces module's lab half (renderer/faces.cpp includes it when
// RENDER_LAB): what facesMeshAsm, facesRoomAsm and facesOtherAsm do, in C to
// check them against, and the checks; the join in C. It sees the module's
// own helpers.
#include "labrender.h"
#include "scene.h"

using namespace render;

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

#define CHAIN_COMMANDS  64
#define CHAIN_BUCKETS   6
#define CHAIN_PASSES    64

int32_t facesChainCheck()
{
    static Command* cmds;
    static uint16_t* heads;
    static uint16_t* tails;
    if (!cmds)
    {
        cmds = (Command*)sceneAllocLow(CHAIN_COMMANDS * sizeof(Command));
        heads = (uint16_t*)sceneAllocLow((OT_SIZE + 1) * sizeof(uint16_t));
        tails = (uint16_t*)sceneAllocLow(OT_SIZE * sizeof(uint16_t));
        if (!tails)
            return 1;
    }
    uint32_t seed = 0x1F123BB5;
    auto next = [&seed] { seed = seed * 1664525 + 1013904223; return seed >> 8; };
    enum { HEAD_ALONE, HEAD, MIDDLE, END, INTO_EMPTY, INTO_CHAIN, COVER_COUNT };
    int32_t bad = 0, cover[COVER_COUNT] = {};
    for (int32_t pass = 0; pass < CHAIN_PASSES; pass++)
    {
        static uint32_t (*grd)[2] = (uint32_t (*)[2])sceneAllocLow(CHAIN_COMMANDS * 8);
        CommandList list;
        facesListInit(list, cmds, grd, 0, CHAIN_COMMANDS, heads, tails, nullptr, nullptr, nullptr, nullptr, nullptr);
        // the model: each bucket's commands, newest first; a command's bucket
        int8_t chain[CHAIN_BUCKETS][CHAIN_COMMANDS];
        int32_t length[CHAIN_BUCKETS] = {};
        int32_t bucketOf[CHAIN_COMMANDS];
        int32_t made = 0;
        auto bucket = [](int32_t b) { return b * 211; };
        for (int32_t op = 0; op < 3 * CHAIN_COMMANDS; op++)
        {
            uint32_t kind = next() % 4;
            if ((kind == 0 || made < 4) && made < CHAIN_COMMANDS)
            {
                // a command made and linked, as linked() links
                int32_t b = next() % CHAIN_BUCKETS;
                uint32_t* w = list.cmd;
                uint32_t link = list.link;
                list.cmd += 8;
                list.link += sizeof(Command) >> 3;
                linkCommand(list, w, (0x1000 | next() % 16) << 16, link, bucket(b));
                for (int32_t k = length[b]; k > 0; k--) {
                    chain[b][k] = chain[b][k - 1];
                }
                chain[b][0] = made;
                length[b]++;
                bucketOf[made++] = b;
                continue;
            }
            // a command in a chain taken out, and (or not) linked into another
            int32_t c = next() % made;
            int32_t b = bucketOf[c];
            if (b < 0)
                continue;
            int32_t at = 0;
            while (chain[b][at] != c) {
                at++;
            }
            cover[length[b] == 1 ? HEAD_ALONE : at == 0 ? HEAD : at == length[b] - 1 ? END : MIDDLE]++;
            uint32_t link = facesLink(c);
            unlinkCommand(list, link, bucket(b));
            for (int32_t k = at; k < length[b] - 1; k++) {
                chain[b][k] = chain[b][k + 1];
            }
            length[b]--;
            bucketOf[c] = -1;
            if (kind == 1)
                continue;
            int32_t to = next() % CHAIN_BUCKETS;
            cover[length[to] ? INTO_CHAIN : INTO_EMPTY]++;
            uint32_t* w = cmds[c].w;
            linkCommand(list, w, w[0] & 0xFFFF0000, link, bucket(to));
            for (int32_t k = length[to]; k > 0; k--) {
                chain[to][k] = chain[to][k - 1];
            }
            chain[to][0] = c;
            length[to]++;
            bucketOf[c] = to;
        }

        // each chain, from its head through the commands' links, and its first
        for (int32_t b = 0; b < CHAIN_BUCKETS; b++)
        {
            uint32_t p = heads[bucket(b)];
            int32_t k = 0;
            bool same = true;
            for (; p && k < length[b]; k++)
            {
                same = same && p == facesLink(chain[b][k]);
                p = cmds[facesSlot(p)].w[0] & 0xFFFF;
            }
            same = same && !p && k == length[b] && (!length[b] || tails[bucket(b)] == facesLink(chain[b][k - 1]));
            bad += !same;
        }
    }
    for (int32_t k = 0; k < COVER_COUNT; k++) {
        bad += cover[k] ? 0 : CHAIN_BUCKETS;
    }
    return bad;
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

void facesOther(const Face &face, const Vertex* half, int32_t depth, CommandList &list)
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
            const Sprite &sprite = list.sprites[flags & FACE_TEXTURE];
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

// The made-up faces' check: two lists of OTHER_COMMANDS each, a pass's faces
// more than that, into a few buckets (the first, the last, between).
#define OTHER_COMMANDS  48
#define OTHER_FACES     56
#define OTHER_VERTICES  16
#define OTHER_SPRITES   16
#define OTHER_PASSES    64

enum OtherCover
{
    COVER_SPRITE, COVER_FLIPPED, COVER_FILL, COVER_LINE_H, COVER_LINE_V, COVER_VOID, COVER_OTHER_TYPE,
    COVER_SKIP_X, COVER_SKIP_Y, COVER_DRAWN, COVER_LEFT, COVER_TOP, COVER_RIGHT, COVER_BOTTOM,
    COVER_INSIDE, COVER_FULL, COVER_FIRST, COVER_SHARED, COVER_COUNT
};

int32_t facesOtherCheck(const uint16_t* shadeGouraud)
{
    static const int32_t kBuckets[6] = { 0, 1, 37, 640, OT_SIZE - 2, OT_SIZE - 1 };
    struct Side
    {
        CommandList list;
        Command* cmds;
        uint32_t (*grd)[2];
        uint16_t* heads;
        uint16_t* tails;
    };
    static Side sides[2];
    static Vertex* vertices;
    static Sprite* sprites;
    if (!sprites)
    {
        for (Side &s : sides)
        {
            s.cmds = (Command*)sceneAllocLow((OTHER_COMMANDS + 1) * sizeof(Command));
            s.grd = (uint32_t (*)[2])sceneAllocLow((OTHER_COMMANDS + 1) * 8);
            s.heads = (uint16_t*)sceneAllocLow((OT_SIZE + 1) * sizeof(uint16_t));
            s.tails = (uint16_t*)sceneAllocLow(OT_SIZE * sizeof(uint16_t));
        }
        vertices = (Vertex*)sceneAllocLow(OTHER_VERTICES * sizeof(Vertex));
        sprites = (Sprite*)sceneAllocLow(OTHER_SPRITES * sizeof(Sprite));
        if (!sprites || !sides[1].tails)
            return 1;
    }
    uint32_t seed = 0x2B7E1516;
    auto next = [&seed] { seed = seed * 1664525 + 1013904223; return seed >> 8; };
    int32_t bad = 0, cover[COVER_COUNT] = {};
    Face faces[OTHER_FACES];
    int32_t depths[OTHER_FACES];
    for (int32_t pass = 0; pass < OTHER_PASSES; pass++)
    {
        // the corners: from well off the frame to inside it, every other
        // one small (a rectangle's size, any sign); shades within the table;
        // the sprites' fields at random
        int32_t reach = pass & 1 ? 1500 : 400;
        for (int32_t k = 0; k < OTHER_VERTICES; k++)
        {
            Vertex &v = vertices[k];
            if (k & 1)
            {
                v.x = int16_t(int32_t(next() % 400) - 40);
                v.y = int16_t(int32_t(next() % 300) - 30);
            }
            else
            {
                v.x = int16_t(int32_t(next() % (2 * reach)) - reach + FRAME_WIDTH / 2);
                v.y = int16_t(int32_t(next() % (2 * reach)) - reach + FRAME_HEIGHT / 2);
            }
            v.z = int16_t(next());
            v.g = next() % 32;
            v.clip = next();
        }
        for (int32_t k = 0; k < OTHER_SPRITES; k++)
        {
            Sprite &sp = sprites[k];
            sp.srca = next();
            sp.colr = next();
            sp.size = next();
            sp.attr = k & 1 ? next() : next() & ~TEX_ATTR_DIR;
            sp.l = sp.t = sp.r = sp.b = int16_t(next());
        }

        // the faces: a sprite's, a fill's, a line's, a void's, or any type;
        // the flags' bits past the type at random; corners anywhere
        bool used[6] = {};
        for (int32_t f = 0; f < OTHER_FACES; f++)
        {
            Face &face = faces[f];
            uint32_t kind = next() % 8;
            uint32_t type = kind < 3 ? 6 : kind < 6 ? 7 + kind - 3 : kind == 6 ? 11 : next() % 16;
            uint32_t low = type == 6 ? next() % OTHER_SPRITES : next() & FACE_TEXTURE;
            face.flags = (next() << 18) | (type << FACE_TYPE_SHIFT) | low;
            face.next = nullptr;
            for (int32_t i = 0; i < 4; i++) {
                face.indices[i] = int16_t(8 * (next() % (OTHER_VERTICES / 2) * 2));
            }
            uint32_t b = next() % 6;
            depths[f] = kBuckets[b];
            if (f < OTHER_COMMANDS)
            {
                cover[used[b] ? COVER_SHARED : COVER_FIRST]++;
                used[b] = true;
            }
        }

        // each side's list over the same words, then the faces into it, in C
        // and in assembly
        for (int32_t s = 0; s < 2; s++)
        {
            Side &side = sides[s];
            uint32_t fill = 0x9E3779B9 * uint32_t(pass + 1);
            for (int32_t c = 0; c <= OTHER_COMMANDS; c++)
            {
                for (int32_t w = 0; w < 8; w++) {
                    side.cmds[c].w[w] = fill + 77 * uint32_t(c * 8 + w);
                }
                side.grd[c][0] = side.grd[c][1] = ~fill + uint32_t(c);
            }
            facesListInit(side.list, side.cmds, side.grd, 0, OTHER_COMMANDS, side.heads, side.tails, nullptr,
                          shadeGouraud, nullptr, nullptr, sprites);
            for (int32_t f = 0; f < OTHER_FACES; f++)
            {
                if (s) {
                    facesOtherAsm(faces + f, vertices, depths[f], &side.list);
                } else {
                    facesOther(faces[f], vertices, depths[f], side.list);
                }
            }
        }
        const Side &c = sides[0], &a = sides[1];
        for (int32_t k = 0; k <= OTHER_COMMANDS; k++) {
            bad += memcmp(c.cmds[k].w, a.cmds[k].w, sizeof(Command)) != 0 || memcmp(c.grd[k], a.grd[k], 8) != 0;
        }
        bad += memcmp(c.heads, a.heads, (OT_SIZE + 1) * sizeof(uint16_t)) != 0;
        for (int32_t b = 0; b < 6; b++) {
            bad += c.heads[kBuckets[b]] && c.tails[kBuckets[b]] != a.tails[kBuckets[b]];
        }
        bad += c.list.cmd - c.cmds[0].w != a.list.cmd - a.cmds[0].w || c.list.grd - c.grd[0] != a.list.grd - a.grd[0] ||
               c.list.link != a.list.link || c.list.grdLink != a.list.grdLink;

        // the paths these faces took
        for (int32_t f = 0; f < OTHER_FACES; f++)
        {
            const Face &face = faces[f];
            uint32_t type = (face.flags >> FACE_TYPE_SHIFT) & FACE_TYPE_MASK;
            if (f >= OTHER_COMMANDS)
            {
                cover[COVER_FULL]++;
                continue;
            }
            switch (type)
            {
                case 6:
                    cover[COVER_SPRITE]++;
                    cover[COVER_FLIPPED] += (sprites[face.flags & FACE_TEXTURE].attr & TEX_ATTR_DIR) != 0;
                    break;
                case 7: cover[COVER_FILL]++; break;
                case 8: cover[COVER_LINE_H]++; break;
                case 9: cover[COVER_LINE_V]++; break;
                default:
                {
                    cover[type == 11 ? COVER_VOID : COVER_OTHER_TYPE]++;
                    const Vertex* v0 = corner(vertices, face.indices[0]);
                    int32_t x0 = v0->x, y0 = v0->y, x1 = x0, y1 = y0;
                    for (int32_t i = 1; i < 4; i++)
                    {
                        const Vertex* v = corner(vertices, face.indices[i]);
                        x0 = min(x0, v->x);
                        y0 = min(y0, v->y);
                        x1 = max(x1, v->x);
                        y1 = max(y1, v->y);
                    }
                    cover[COVER_LEFT] += x0 < 0;
                    cover[COVER_TOP] += y0 < 0;
                    cover[COVER_RIGHT] += x1 > FRAME_WIDTH - 1;
                    cover[COVER_BOTTOM] += y1 > FRAME_HEIGHT - 1;
                    cover[COVER_INSIDE] += x0 >= 0 && y0 >= 0 && x1 < FRAME_WIDTH && y1 < FRAME_HEIGHT;
                    x0 = max(x0, 0);
                    y0 = max(y0, 0);
                    x1 = min(x1, FRAME_WIDTH - 1);
                    y1 = min(y1, FRAME_HEIGHT - 1);
                    cover[COVER_SKIP_X] += x0 > x1;
                    cover[COVER_SKIP_Y] += x0 <= x1 && y0 > y1;
                    cover[COVER_DRAWN] += x0 <= x1 && y0 <= y1;
                }
            }
        }
    }

    // a path never taken counts as every face off
    for (int32_t k = 0; k < COVER_COUNT; k++) {
        bad += cover[k] ? 0 : OTHER_FACES;
    }
    return bad;
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

