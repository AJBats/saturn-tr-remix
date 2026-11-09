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

// facesMeshCardsAsm in C. A card's half width at each end in sixteenths of a
// pixel (half * divTable[z >> 4] >> 8); the line between its ends' length L
// from its sides' longer and shorter, (123 longer + 51 shorter) / 128 (an
// octagon's; a line longer than divTable reaches halved until it isn't, its
// ends' x and y with it); each width times 65536 / 8L (in eighths of a pixel
// under 112 pixels long, else in pixels: at least the longer side, and 1),
// times the line's dy and -dx, >> 16: the corner's offset from its end in
// half pixels, then whole pixels rounded.
static int32_t cardRound(int32_t r)
{
    return (int32_t(int16_t(uint32_t(r) >> 16)) + 1) >> 1;
}

static void cardCorner(Vertex &v, int32_t x, int32_t y, bool flagged)
{
    if (flagged)
    {
        uint32_t clip = 0;
        if (uint32_t(x) > FRAME_WIDTH || uint32_t(y) > FRAME_HEIGHT)
        {
            clip = CLIP_FRAME;
            x = coord(x);
            y = coord(y);
            clip |= (x < 0 ? CLIP_LEFT : 0) | (x > FRAME_WIDTH ? CLIP_RIGHT : 0) | (y < 0 ? CLIP_TOP : 0) |
                    (y > FRAME_HEIGHT ? CLIP_BOTTOM : 0);
        }
        v.clip = uint8_t(clip);
    }
    v.x = int16_t(x);
    v.y = int16_t(y);
}

// A facing card's first end placed from its second, which stays: its length
// at the second's depth, in sixteenths of a pixel (s), the way the line from
// the second points on the screen, lifted up the screen by about 0.3 s so that
// it never comes to nothing; that line's length as a card's is worked out.
static void cardFacing(Vertex &a, const Vertex &b, uint32_t length, const uint16_t* divTable)
{
    uint32_t s = (2 * length * divTable[uint16_t(b.z) >> 4]) >> 8;
    int32_t dx = a.x - b.x, dy = a.y - b.y - int32_t((s * 5) >> 8);
    uint32_t hi = uint32_t(dx < 0 ? -dx : dx), lo = uint32_t(dy < 0 ? -dy : dy);
    if (hi < lo)
    {
        uint32_t t = hi;
        hi = lo;
        lo = t;
    }
    while (hi > 936)
    {
        dx >>= 1;
        dy >>= 1;
        hi >>= 1;
        lo >>= 1;
    }
    uint32_t l = ((123 * hi + 51 * lo + 64) << 1) >> 8;
    if (l < hi) {
        l = hi;
    }
    if (!l)
    {
        l = 1;
        dy = -1;
    }
    uint32_t m = (s * divTable[l]) >> 4;
    int16_t ox = int16_t((uint32_t(dx) * m) >> 16), oy = int16_t((uint32_t(dy) * m) >> 16);
    a.x = int16_t(b.x + ox);
    a.y = int16_t(b.y + oy);
}

static uint8_t cardShade(int32_t g)
{
    return uint8_t(g < 0 ? 0 : g > 31 ? 31 : g);
}

void facesMeshCardsC(const FaceSet* set, Vertex* vertices, uint32_t mode, const uint16_t* divTable, int32_t shade)
{
    const FaceQuad* q = faceQuads(set) + set->quads - set->cards;
    for (int32_t k = 0; k < set->cards; k++, q++)
    {
        Vertex &a = *(Vertex*)((uint8_t*)vertices + q->corners[0]);
        Vertex &b = *(Vertex*)((uint8_t*)vertices + q->corners[1]);
        Vertex &c = *(Vertex*)((uint8_t*)vertices + q->corners[2]);
        Vertex &d = *(Vertex*)((uint8_t*)vertices + q->corners[3]);
        bool inside = mode & MESH_INSIDE;
        if (!inside && ((a.clip | b.clip) & CLIP_PLANE))
            continue;
        if (shade)
        {
            a.g = d.g = cardShade(a.g + shade);
            b.g = c.g = cardShade(b.g + shade);
        }
        if (CARD_LENGTH(q->half)) {
            cardFacing(a, b, CARD_LENGTH(q->half), divTable);
        }
        uint32_t half = CARD_HALF(q->half);
        uint32_t wa = (half * divTable[uint16_t(a.z) >> 4]) >> 8;
        uint32_t wb = (half * divTable[uint16_t(b.z) >> 4]) >> 8;

        // on the frame: the mesh inside it, or both ends M or more inside each
        // edge (M = the wider half width in pixels + 1: no corner farther)
        bool onFrame = inside;
        if (!inside && !((a.clip | b.clip) & CLIP_FRAME))
        {
            int32_t m = int32_t((wa > wb ? wa : wb) >> 4) + 1;
            int32_t w = FRAME_WIDTH - 2 * m, h = FRAME_HEIGHT - 2 * m;
            onFrame = h >= 0 && uint32_t(a.x - m) <= uint32_t(w) && uint32_t(b.x - m) <= uint32_t(w) &&
                      uint32_t(a.y - m) <= uint32_t(h) && uint32_t(b.y - m) <= uint32_t(h);
        }

        int32_t dx = b.x - a.x, dy = b.y - a.y;
        uint32_t hi = uint32_t(dx < 0 ? -dx : dx), lo = uint32_t(dy < 0 ? -dy : dy);
        if (hi < lo)
        {
            uint32_t t = hi;
            hi = lo;
            lo = t;
        }
        while (hi > 936)
        {
            dx >>= 1;
            dy >>= 1;
            hi >>= 1;
            lo >>= 1;
        }
        uint32_t length = 123 * hi + 51 * lo;   // 128 x
        uint32_t sa, sb;
        if (hi < 112)
        {
            uint32_t l = (length + 8) >> 4;     // 8 x, rounded
            if (l < 8 * hi) {
                l = 8 * hi;
            }
            if (!l) {
                l = 1;
            }
            sa = wa * divTable[l];
            sb = wb * divTable[l];
        }
        else
        {
            uint32_t l = ((length + 64) << 1) >> 8;
            if (l < hi) {
                l = hi;
            }
            sa = (wa * divTable[l]) >> 3;
            sb = (wb * divTable[l]) >> 3;
        }
        int32_t ax = cardRound(int32_t(sa * uint32_t(dy))), ay = cardRound(int32_t(sa * uint32_t(-dx)));
        int32_t bx = cardRound(int32_t(sb * uint32_t(dy))), by = cardRound(int32_t(sb * uint32_t(-dx)));
        int32_t x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
        bool flagged = !onFrame;
        cardCorner(a, x0 + ax, y0 + ay, flagged);
        cardCorner(d, x0 - ax, y0 - ay, flagged);
        cardCorner(b, x1 + bx, y1 + by, flagged);
        cardCorner(c, x1 - bx, y1 - by, flagged);
    }
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
        MeshFace fate = inside ? MESH_FACE_DRAWN : meshFace(a, b, c, d);
        if (fate == MESH_FACE_LEFT)
            continue;
        if (fate == MESH_FACE_VOID)
        {
            faceVoid(list, a, b, c, d);
            continue;
        }
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
        MeshFace fate = inside ? MESH_FACE_DRAWN : meshFace(a, b, c, c);
        if (fate == MESH_FACE_LEFT)
            continue;
        if (fate == MESH_FACE_VOID)
        {
            Vertex d = t->flags & TRI_PARALLELOGRAM ? fourthCorner(a, b, c) : *c;
            faceVoid(list, a, b, c, &d);
            continue;
        }
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
// about a texel a pixel along them; kept, the face's last decision (its
// flags' top byte): the other way must then save an eighth of the lines.
static uint32_t roomVariant(const RoomTextures &t, uint32_t flags, const Vertex* a, const Vertex* b,
                            const Vertex* c, const Vertex* d, uint32_t kept)
{
    int32_t turned = max(span(a, d), span(b, c));   // the lines' length turned
    int32_t across = max(span(a, b), span(d, c));   // ... and as it is
    const Texture &tex = t.textures[flags & FACE_TEXTURE];
    uint32_t size = tex.size;
    uint32_t variant = turnedFace(kept, turned, across);
    if (variant) {
        across = turned;
    }
    if (!t.lod)
        return variant;
    // 4 x the texels along a line: the whole texture's width, or turned its
    // height
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
                faceVoid(list, a, b, c, d);                 // wholly past the far plane
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
            variant = roomVariant(t, flags, a, b, c, d, kept);
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
            faceVoid(list, a, b, c, c);
            continue;
        }
        if (backface(a, b, c))
            continue;
        uint32_t flags = (q->flags & ROOM_QUAD_FLAGS) | room.flags | FACE_TRIANGLE;
        uint32_t variant = roomVariant(t, flags, a, b, c, c, q->flags >> 24);
        if (cacheable(t, flags)) {
            const_cast<RoomQuad*>(q)->flags = (q->flags & ROOM_QUAD_FLAGS) | ((variant | 8) << 24);
        }
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

// Made-up cards: ends anywhere from far off the frame to its middle, at any
// depth between the planes (some past one), any half width, some facing, any
// shade to add; each card's corners placed and its faces made in C
// (facesMeshCardsC, facesMeshC) and in assembly, the mesh inside the frame or
// not: the vertices and commands each way, against each other. A degenerate card (its ends on one pixel)
// faces away and is left out both ways. Made-up triangles over the same
// corners, of every kind, follow them (a plane cutting some: their fills).
#define CARD_CARDS      48
#define CARD_TRIANGLES  32
#define CARD_COMMANDS   (CARD_CARDS + CARD_TRIANGLES)
#define CARD_PASSES     24

int32_t facesCardsCheck(const Material* materials, uint32_t materialCount, const uint16_t* divTable,
                        const uint16_t* shadeGouraud)
{
    struct Side
    {
        CommandList list;
        Command* cmds;
        uint32_t (*grd)[2];
        uint16_t* heads;
        uint16_t* tails;
        Vertex* vertices;
    };
    static Side sides[2];
    static FaceSet* set;
    static Vertex* made;
    if (!set)
    {
        for (Side &s : sides)
        {
            s.cmds = (Command*)sceneAllocLow((CARD_COMMANDS + 1) * sizeof(Command));
            s.grd = (uint32_t (*)[2])sceneAllocLow((CARD_COMMANDS + 1) * 8);
            s.heads = (uint16_t*)sceneAllocLow((OT_SIZE + 1) * sizeof(uint16_t));
            s.tails = (uint16_t*)sceneAllocLow(OT_SIZE * sizeof(uint16_t));
            s.vertices = (Vertex*)sceneAllocLow(4 * CARD_CARDS * sizeof(Vertex));
        }
        set = (FaceSet*)sceneAllocLow(sizeof(FaceSet) + CARD_CARDS * sizeof(FaceQuad) +
                                      CARD_TRIANGLES * sizeof(FaceTriangle));
        made = (Vertex*)sceneAllocLow(4 * CARD_CARDS * sizeof(Vertex));
        if (!made || !sides[1].vertices || !materialCount)
            return 1;
    }
    uint32_t seed = 0x3C6EF372;
    auto next = [&seed] { seed = seed * 1664525 + 1013904223; return seed >> 8; };
    int32_t bad = 0;
    for (int32_t pass = 0; pass < CARD_PASSES; pass++)
    {
        bool inside = pass % 3 == 2;
        int32_t reach = inside ? 0 : pass & 1 ? 1500 : 300;
        set->quads = set->cards = CARD_CARDS;
        set->triangles = CARD_TRIANGLES;
        set->back = 0;
        int32_t shade = pass % 4 == 0 ? 0 : int32_t(next() % 13) - 6;
        FaceQuad* q = (FaceQuad*)(set + 1);
        for (int32_t k = 0; k < CARD_CARDS; k++, q++)
        {
            for (int32_t i = 0; i < 4; i++) {
                q->corners[i] = uint16_t((4 * k + i) * sizeof(Vertex));
            }
            q->material = uint16_t(next() % materialCount);
            q->half = uint16_t(1 + next() % 255);
            for (int32_t e = 0; e < 2; e++)
            {
                // an end, as the projection leaves it; inside the frame for a
                // mesh inside it, near the middle so that its corners are too
                Vertex &v = made[4 * k + e];
                int32_t x, y;
                if (inside)
                {
                    x = FRAME_WIDTH / 2 - 40 + int32_t(next() % 80);
                    y = FRAME_HEIGHT / 2 - 30 + int32_t(next() % 60);
                    q->half = uint16_t(1 + next() % 32);
                }
                else if (k & 1)
                {
                    x = int32_t(next() % (2 * reach + FRAME_WIDTH)) - reach;
                    y = int32_t(next() % (2 * reach + FRAME_HEIGHT)) - reach;
                }
                else
                {
                    x = int32_t(next() % FRAME_WIDTH);
                    y = int32_t(next() % FRAME_HEIGHT);
                }
                v.z = int16_t(64 + next() % 20416);
                v.g = uint8_t(next() % 32);
                v.clip = 0;
                if (inside) {
                    v.z = int16_t(2000 + next() % 8000);
                }
                else if (next() % 16 == 0) {
                    v.clip = CLIP_PLANE;
                }
                cardCorner(v, x, y, !inside);
                if (!inside && next() % 16 == 0) {
                    v.clip |= CLIP_PLANE;
                }
                if (k % 7 == 0 && e) {
                    v.x = made[4 * k].x;
                    v.y = made[4 * k].y;
                }
            }
            if (k % 3 == 0) {
                q->half |= uint16_t((1 + next() % 255) << 8);
            }
            made[4 * k + 2] = made[4 * k + 1];
            made[4 * k + 3] = made[4 * k];
        }
        static const uint16_t kKinds[6] = { 0, TRI_PARALLELOGRAM, TRI_PARALLELOGRAM | TRI_SWAP,
                                            TRI_PARALLELOGRAM | TRI_V0_AT_D,
                                            TRI_PARALLELOGRAM | TRI_SWAP | TRI_V0_AT_D, 0 };
        FaceTriangle* t = (FaceTriangle*)q;
        for (int32_t k = 0; k < CARD_TRIANGLES; k++, t++)
        {
            for (int32_t i = 0; i < 3; i++) {
                t->corners[i] = uint16_t((next() % (4 * CARD_CARDS)) * sizeof(Vertex));
            }
            t->flags = kKinds[next() % 6];
            t->material = uint16_t(next() % materialCount);
            t->spare = 0;
        }
        uint32_t mode = inside ? MESH_INSIDE : 0;
        for (int32_t s = 0; s < 2; s++)
        {
            Side &side = sides[s];
            memcpy(side.vertices, made, 4 * CARD_CARDS * sizeof(Vertex));
            uint32_t fill = 0x9E3779B9 * uint32_t(pass + 1);
            for (int32_t c = 0; c <= CARD_COMMANDS; c++)
            {
                for (int32_t w = 0; w < 8; w++) {
                    side.cmds[c].w[w] = fill + 77 * uint32_t(c * 8 + w);
                }
                side.grd[c][0] = side.grd[c][1] = ~fill + uint32_t(c);
            }
            facesListInit(side.list, side.cmds, side.grd, 0, CARD_COMMANDS, side.heads, side.tails, materials,
                          shadeGouraud, nullptr, nullptr, nullptr);
            if (s)
            {
                facesMeshCardsAsm(set, side.vertices, mode, divTable, shade);
                facesMeshAsm(set, side.vertices, mode, &side.list);
            }
            else
            {
                facesMeshCardsC(set, side.vertices, mode, divTable, shade);
                facesMeshC(set, side.vertices, mode, side.list);
            }
        }
        const Side &c = sides[0], &a = sides[1];
        bad += memcmp(c.vertices, a.vertices, 4 * CARD_CARDS * sizeof(Vertex)) != 0;
        for (int32_t k = 0; k <= CARD_COMMANDS; k++) {
            bad += memcmp(c.cmds[k].w, a.cmds[k].w, sizeof(Command)) != 0 || memcmp(c.grd[k], a.grd[k], 8) != 0;
        }
        bad += memcmp(c.heads, a.heads, (OT_SIZE + 1) * sizeof(uint16_t)) != 0;
    }
    return bad;
}

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

