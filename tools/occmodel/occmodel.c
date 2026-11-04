// What room culling could save, before any of it is built: random views about a level, the
// game's portal walk (renderer/rooms.s) as the baseline, a true depth buffer as the oracle of
// which listed rooms show a pixel, and the tests against both:
//   poly   windows as the portals' true shapes, clipped portal by portal, not rectangles
//   mask   big solid faces baked per room (export.py) drawn into a coarse screen mask as the
//          walk enters their rooms; a portal wholly behind what's there isn't entered
//   pvs    the rooms seen from a camera's cell over training views, baked; held-out views test it
//   tiny   a portal showing fewer than so many pixels of window isn't entered, its window filled
//   hot    pvs for only the camera cells and headings whose views are heavy, trained hard
//   faces31 a face all of whose vertices are at the fog's floor isn't drawn
//   both31 that and dark31
//   dark   each room its own draw distance: past where its brightest vertex fogs to near black
//          it isn't drawn, its window filled; the walk goes on through it
//   far    the draw distance moved in: portals and faces past it gone; pixels lost, and how
//          many of them weren't near black already (shade + fog, 0 lit .. 31 dark)
//   near   rooms taken nearest first: a room's window or a face wholly behind the mask is
//          skipped, each big opaque face made goes in it: nothing baked, the faces themselves
// A test that leaves out a room the oracle saw shows as lost pixels. Costs are Mednafen's cycles
// (cistern-study): 125 a vertex, 80 a face dropped, 300 a face kept, 600 a portal check.
//
// One view of the game's (tools/hwtest.sh spots, a camera read off a frame) is walked with "one"
// and its list checked against the game's: exact at the pillar spot, one 1 px window out at the
// balcony. Which faces and rooms show is at half resolution, statics and items not drawn.
//
// gcc -O2 -o occmodel occmodel.c -lm; occmodel LEVEL.bin VIEWS SEED [PVS_CELL PVS_VIEWS]
//         occmodel LEVEL.bin VIEWS SEED 0 0 HOT_GRID HOT_YAWS HOT_VERTS HOT_VIEWS
//         occmodel LEVEL.bin one x y z room r00..r22 focal
//         occmodel LEVEL.bin region x y z room r00..r22 focal RADIUS ARC_DEGREES PITCH_DEGREES VIEWS
//         occmodel LEVEL.bin cameras VIEWS SEED
//         occmodel LEVEL.bin holes VIEWS SEED
//         occmodel LEVEL.bin boxes FILE VIEWS SEED   solid boxes (x0 x1 y0 y1 z0 z1 a line) as the game has them
//         occmodel LEVEL.bin boxsearch OUT COUNT VIEWS SEED [HAND]   big boxes in the rock between rooms (no room's air in
//                                 them, every room and alternate), picked one at a time over heavy views; HAND: boxes to start from
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 352
#define H 224
#define NEAR 64.0f
#define VIEW_MAX 20480.0f
#define FOG_MIN 12288.0f
#define DARK_SHADE 26       // past it, near black: gouraud adds 16 - shade to 5-bit texels
#define MAXROOMS 256
#define CELLW 44
#define CELLH 28
#define CELL 8
#define ZW (W / 2)
#define ZH (H / 2)

static float gFar = VIEW_MAX;
static int gBlackFaces;     // a face wholly at the fog's floor culled
static int gFrom[MAXROOMS];         // one's: the room each was first entered from
static float gNearest[MAXROOMS];    // ... the nearest corner of a portal it was seen by
static double gListedRooms, gWholePast;     // the walk's rooms, and those wholly past the far plane
static float F = 256.0f;      // the game's: x 256 / z

typedef struct { float x, y, z; } V3;
typedef struct { int target; float n[3]; V3 c[4]; } Portal;
typedef struct { V3 c[4]; float n[3]; } Occ;
typedef struct { int a, b, c, d, transp; } Quad;
typedef struct {
    int x, z, ytop, ybot, xs, zs, alt, nv, nq, nt, np, ns, nocc, nstatic;
    V3* v;
    uint8_t* shade;
    Quad* q;
    int (*t)[3];
    Portal* p;
    int (*s)[2];
    Occ* o;
    V3* vv;         // in view space, this view's
    float* sx, *sy;
    int stamp;
    int faceBase;   // its first face's id over the level: quads, then triangles
} Room;

static Room rooms[MAXROOMS];
static int nrooms, nfaces;
static V3 cam;
static float R[3][3];
static int viewStamp;

typedef struct { float x0, y0, x1, y1; } Rect;

static void loadLevel(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fread(&nrooms, 4, 1, f);
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        fread(&r->x, 4, 14, f);
        r->v = malloc(sizeof(V3) * (r->nv + 1));
        r->shade = malloc(r->nv + 1);
        r->vv = malloc(sizeof(V3) * (r->nv + 1));
        r->sx = malloc(4 * (r->nv + 1));
        r->sy = malloc(4 * (r->nv + 1));
        for (int k = 0; k < r->nv; k++) {
            int c[4];
            fread(c, 4, 4, f);
            r->v[k] = (V3){ c[0], c[1], c[2] };
            r->shade[k] = c[3];
        }
        r->q = malloc(sizeof(Quad) * (r->nq + 1));
        fread(r->q, sizeof(Quad), r->nq, f);
        r->t = malloc(12 * (r->nt + 1));
        fread(r->t, 12, r->nt, f);
        r->p = malloc(sizeof(Portal) * (r->np + 1));
        for (int k = 0; k < r->np; k++) {
            int c[16];
            fread(c, 4, 16, f);
            r->p[k].target = c[0];
            r->p[k].n[0] = c[1]; r->p[k].n[1] = c[2]; r->p[k].n[2] = c[3];
            for (int j = 0; j < 4; j++)
                r->p[k].c[j] = (V3){ c[4 + j * 3], c[5 + j * 3], c[6 + j * 3] };
        }
        r->s = malloc(8 * (r->ns + 1));
        fread(r->s, 8, r->ns, f);
        r->o = malloc(sizeof(Occ) * (r->nocc + 1));
        for (int k = 0; k < r->nocc; k++) {
            int c[15];
            fread(c, 4, 15, f);
            for (int j = 0; j < 4; j++)
                r->o[k].c[j] = (V3){ c[j * 3], c[j * 3 + 1], c[j * 3 + 2] };
            r->o[k].n[0] = c[12]; r->o[k].n[1] = c[13]; r->o[k].n[2] = c[14];
        }
        r->faceBase = nfaces;
        nfaces += r->nq + r->nt;
    }
    fclose(f);
}

static inline V3 toView(V3 p)
{
    float dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z;
    return (V3){ R[0][0] * dx + R[0][1] * dy + R[0][2] * dz,
                 R[1][0] * dx + R[1][1] * dy + R[1][2] * dz,
                 R[2][0] * dx + R[2][1] * dy + R[2][2] * dz };
}

static inline float screenX(V3 v) { return v.x / v.z * F + W / 2; }
static inline float screenY(V3 v) { return v.y / v.z * F + H / 2; }

static void roomProject(Room* r)
{
    if (r->stamp == viewStamp)
        return;
    r->stamp = viewStamp;
    for (int k = 0; k < r->nv; k++)
    {
        V3 v = toView(r->v[k]);
        r->vv[k] = v;
        float z = v.z < 16 ? 16 : v.z;
        r->sx[k] = v.x / z * F + W / 2;
        r->sy[k] = v.y / z * F + H / 2;
    }
}

// --- the convex window ------------------------------------------------------------------
typedef struct { int n; float x[32], y[32]; } Poly;

static float polyArea(const Poly* p)
{
    float a = 0;
    for (int i = 0; i < p->n; i++) {
        int j = (i + 1) % p->n;
        a += p->x[i] * p->y[j] - p->x[j] * p->y[i];
    }
    return a * 0.5f;
}

// p cut by the half plane a x + b y + c >= 0
static void polyCut(Poly* p, float a, float b, float c)
{
    Poly o;
    o.n = 0;
    for (int i = 0; i < p->n && o.n < 30; i++)
    {
        int j = (i + 1) % p->n;
        float di = a * p->x[i] + b * p->y[i] + c, dj = a * p->x[j] + b * p->y[j] + c;
        if (di >= 0) {
            o.x[o.n] = p->x[i]; o.y[o.n++] = p->y[i];
        }
        if ((di >= 0) != (dj >= 0)) {
            float t = di / (di - dj);
            o.x[o.n] = p->x[i] + (p->x[j] - p->x[i]) * t;
            o.y[o.n++] = p->y[i] + (p->y[j] - p->y[i]) * t;
        }
    }
    *p = o;
}

// a view-space polygon past the near plane, on the screen
static int clipProject(const V3* in, int n, Poly* out, float znear)
{
    V3 t[16];
    int m = 0;
    for (int i = 0; i < n; i++)
    {
        V3 a = in[i], b = in[(i + 1) % n];
        if (a.z >= znear) t[m++] = a;
        if ((a.z >= znear) != (b.z >= znear)) {
            float k = (znear - a.z) / (b.z - a.z);
            t[m++] = (V3){ a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, znear };
        }
    }
    out->n = m;
    for (int i = 0; i < m; i++) {
        out->x[i] = screenX(t[i]);
        out->y[i] = screenY(t[i]);
    }
    return m;
}

static void polyFrame(Poly* p)
{
    polyCut(p, 1, 0, 0);
    if (p->n) polyCut(p, -1, 0, W);
    if (p->n) polyCut(p, 0, 1, 0);
    if (p->n) polyCut(p, 0, -1, H);
}

// --- the mask of big solid faces ---------------------------------------------------------
static float maskZ[CELLH][CELLW];
typedef struct { long drawn, tried, rows, cells, tests; } MaskCost;
static MaskCost maskCost;

static void maskClear(void)
{
    for (int j = 0; j < CELLH; j++)
        for (int i = 0; i < CELLW; i++)
            maskZ[j][i] = 1e30f;
}

static void spanAt(const Poly* p, float y, float* lo, float* hi)
{
    *lo = 1e30f; *hi = -1e30f;
    for (int i = 0; i < p->n; i++)
    {
        int j = (i + 1) % p->n;
        float y0 = p->y[i], y1 = p->y[j];
        if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y))
        {
            float x;
            if (y0 == y1) {
                x = p->x[i];
                if (p->x[j] < *lo) *lo = p->x[j];
                if (p->x[j] > *hi) *hi = p->x[j];
            } else {
                x = p->x[i] + (p->x[j] - p->x[i]) * (y - y0) / (y1 - y0);
            }
            if (x < *lo) *lo = x;
            if (x > *hi) *hi = x;
        }
    }
}

static void maskDraw(const Occ* o)
{
    maskCost.tried++;
    float d = o->n[0] * (cam.x - o->c[0].x) + o->n[1] * (cam.y - o->c[0].y) + o->n[2] * (cam.z - o->c[0].z);
    if (d >= 0)
        return;     // its back: not drawn, so it hides nothing
    V3 v[4];
    float zmax = -1e30f, zmin = 1e30f;
    for (int i = 0; i < 4; i++) {
        v[i] = toView(o->c[i]);
        if (v[i].z > zmax) zmax = v[i].z;
        if (v[i].z < zmin) zmin = v[i].z;
    }
    if (zmax < NEAR)
        return;
    Poly p;
    clipProject(v, 4, &p, NEAR);
    if (p.n < 3)
        return;
    maskCost.drawn++;
    float ylo = 1e30f, yhi = -1e30f;
    for (int i = 0; i < p.n; i++) {
        if (p.y[i] < ylo) ylo = p.y[i];
        if (p.y[i] > yhi) yhi = p.y[i];
    }
    int r0 = (int)floorf(ylo / CELL), r1 = (int)floorf(yhi / CELL);
    if (r0 < 0) r0 = 0;
    if (r1 >= CELLH) r1 = CELLH - 1;
    for (int j = r0; j <= r1; j++)
    {
        float a0, b0, a1, b1;
        spanAt(&p, j * CELL, &a0, &b0);
        spanAt(&p, (j + 1) * CELL, &a1, &b1);
        maskCost.rows++;
        float xl = a0 > a1 ? a0 : a1, xr = b0 < b1 ? b0 : b1;
        if (xl >= xr || a0 > 1e29f || a1 > 1e29f)
            continue;
        int c0 = (int)ceilf(xl / CELL), c1 = (int)floorf(xr / CELL) - 1;
        if (c0 < 0) c0 = 0;
        if (c1 >= CELLW) c1 = CELLW - 1;
        for (int i = c0; i <= c1; i++) {
            maskCost.cells++;
            if (zmax < maskZ[j][i])
                maskZ[j][i] = zmax;
        }
    }
}

// the window wholly behind what's in the mask
static int maskHides(Rect w, float zmin)
{
    int c0 = (int)floorf(w.x0 / CELL), c1 = (int)floorf((w.x1 - 0.001f) / CELL);
    int r0 = (int)floorf(w.y0 / CELL), r1 = (int)floorf((w.y1 - 0.001f) / CELL);
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= CELLW) c1 = CELLW - 1;
    if (r1 >= CELLH) r1 = CELLH - 1;
    for (int j = r0; j <= r1; j++)
        for (int i = c0; i <= c1; i++) {
            maskCost.tests++;
            if (!(maskZ[j][i] < zmin))
                return 0;
        }
    return 1;
}

// --- solid boxes -------------------------------------------------------------------------
// As the game's (renderer/rooms.cpp boxSee): a rectangle wholly inside a box's outline, from two
// opposite sides of it, and where a point lies if it's behind the box: past each side of it that
// faces the camera.
#define MAXBOXES 4096
#define BOXES_SEEN 6
#define BOX_NEAR_Z 128.0f
#define BOX_LEAST 16.0f
typedef struct { float lo[3], hi[3]; int side; } SolidBox;     // side: flat, seen only from k + 1 above (+) or below (-) its plane on axis k
typedef struct { float x0, y0, x1, y1, lo[3], hi[3], area; } BoxSeen;
static SolidBox gSolid[MAXBOXES];
static int gSolidN;
static BoxSeen gBoxSeen[BOXES_SEEN];
static int gBoxSeenN;
static int gBoxFaces;       // room faces behind them left out

static void boxRectTry(BoxSeen* b, float x0, float y0, float x1, float y1)
{
    float w = fminf(x1, W) - fmaxf(x0, 0), h = fminf(y1, H) - fmaxf(y0, 0);
    if (w < BOX_LEAST || h < BOX_LEAST || w * h <= b->area)
        return;
    b->x0 = x0 + 1; b->y0 = y0 + 1; b->x1 = x1 - 1; b->y1 = y1 - 1;
    b->area = w * h;
}

static int boxSee(const SolidBox* whole, BoxSeen* o)
{
    float camera[3] = { cam.x, cam.y, cam.z };
    int inside = 1;
    for (int k = 0; k < 3; k++)
        inside = inside && camera[k] >= whole->lo[k] && camera[k] <= whole->hi[k];
    if (inside)
        return 0;
    int flat = -1;
    if (whole->side)
    {
        flat = abs(whole->side) - 1;
        float at = camera[flat] - whole->lo[flat];
        if (whole->side > 0 ? at <= 0 : at >= 0)
            return 0;
    }
    // what part of it lies between the near limit and the far plane: a part of a solid box is one
    SolidBox part = *whole;
    SolidBox* b = &part;
    for (int pass = 0; pass < 2; pass++)
    {
        float zlo = 0, zhi = 0;
        float at[3][2];
        for (int k = 0; k < 3; k++)
        {
            at[k][0] = R[2][k] * (b->lo[k] - camera[k]);
            at[k][1] = R[2][k] * (b->hi[k] - camera[k]);
            zlo += fminf(at[k][0], at[k][1]);
            zhi += fmaxf(at[k][0], at[k][1]);
        }
        // the longest way along the view first
        for (int n = 0; n < 3 && (zlo < BOX_NEAR_Z || zhi >= gFar - 16); n++)
        {
            int k = -1;
            float reach = 0;
            for (int j = 0; j < 3; j++)
                if (fabsf(at[j][1] - at[j][0]) > reach) {
                    reach = fabsf(at[j][1] - at[j][0]);
                    k = j;
                }
            if (k < 0)
                return 0;
            float rk = R[2][k];
            if (zlo < BOX_NEAR_Z)
            {
                float more = BOX_NEAR_Z - zlo;
                if (more >= reach)
                    return 0;
                if (rk > 0) b->lo[k] += more / rk; else b->hi[k] += more / rk;
                zlo += more;
                reach -= more;
            }
            if (zhi >= gFar - 16)
            {
                float less = zhi - (gFar - 16);
                if (less >= reach)
                    return 0;
                if (rk > 0) b->hi[k] -= less / rk; else b->lo[k] -= less / rk;
                zhi -= less;
            }
            at[k][0] = at[k][1] = 0;
        }
        if (zlo >= BOX_NEAR_Z && zhi < gFar - 16)
            break;
    }
    float sx[8], sy[8];
    for (int c = 0; c < 8; c++)
    {
        V3 p = { c & 1 ? b->hi[0] : b->lo[0], c & 2 ? b->hi[1] : b->lo[1], c & 4 ? b->hi[2] : b->lo[2] };
        V3 v = toView(p);
        if (v.z < BOX_NEAR_Z || v.z >= gFar)
            return 0;
        sx[c] = v.x / v.z * F + W / 2;
        sy[c] = v.y / v.z * F + H / 2;
    }
    o->area = 0;
    for (int bit = 1; bit < 8; bit <<= 1)
    {
        float x0[2] = { 1e30f, 1e30f }, x1[2] = { -1e30f, -1e30f }, y0[2] = { 1e30f, 1e30f }, y1[2] = { -1e30f, -1e30f };
        for (int c = 0; c < 8; c++)
        {
            int side = (c & bit) != 0;
            x0[side] = fminf(x0[side], sx[c]);
            x1[side] = fmaxf(x1[side], sx[c]);
            y0[side] = fminf(y0[side], sy[c]);
            y1[side] = fmaxf(y1[side], sy[c]);
        }
        for (int a = 0; a < 2; a++)
        {
            int d = a ^ 1;
            if (y1[a] < y0[d])
                boxRectTry(o, fmaxf(x0[a], x0[d]), y1[a], fminf(x1[a], x1[d]), y0[d]);
            if (x1[a] < x0[d])
                boxRectTry(o, x1[a], fmaxf(y0[a], y0[d]), x0[d], fminf(y1[a], y1[d]));
        }
    }
    if (o->area <= 0)
        return 0;
    for (int k = 0; k < 3; k++)
    {
        // a flat one's past its plane alone: a ray through the rectangle has crossed the plane on it
        int counts = flat < 0 || flat == k;
        o->lo[k] = counts && camera[k] < b->lo[k] ? b->lo[k] : -1e30f;
        o->hi[k] = counts && camera[k] > b->hi[k] ? b->hi[k] : 1e30f;
    }
    return 1;
}

static void boxesSee(const SolidBox* list, int count)
{
    gBoxSeenN = 0;
    for (int i = 0; i < count; i++)
    {
        BoxSeen s;
        if (!boxSee(&list[i], &s))
            continue;
        int at = gBoxSeenN;
        if (gBoxSeenN == BOXES_SEEN)
        {
            at = 0;
            for (int j = 1; j < gBoxSeenN; j++)
                if (gBoxSeen[j].area < gBoxSeen[at].area)
                    at = j;
            if (gBoxSeen[at].area >= s.area)
                continue;
        }
        else
            gBoxSeenN++;
        gBoxSeen[at] = s;
    }
}

static inline int pointPast(const BoxSeen* b, V3 p)
{
    return p.x > b->lo[0] && p.x < b->hi[0] && p.y > b->lo[1] && p.y < b->hi[1] && p.z > b->lo[2] && p.z < b->hi[2];
}

// --- the walks ---------------------------------------------------------------------------
typedef struct {
    int useBoxes;
    int useMask, occK, usePoly;
    float minArea;      // a window smaller is filled, its room not entered
    int darkShade;      // a room whose portal lies past where its brightest vertex reaches this isn't drawn
    uint8_t unlisted[MAXROOMS];
    double fills, fillPx;
    Rect clip[MAXROOMS];
    uint8_t has[MAXROOMS], listed[MAXROOMS], inStack[MAXROOMS], occDrawn[MAXROOMS];
    int list[MAXROOMS], nlist;
    long checks, visits;
    int overflow;
} Walk;

static inline void roomOccluders(Walk* w, int room)
{
    if (!w->useMask || w->occDrawn[room])
        return;
    w->occDrawn[room] = 1;
    Room* r = &rooms[room];
    int n = r->nocc < w->occK ? r->nocc : w->occK;
    for (int k = 0; k < n; k++)
        maskDraw(&r->o[k]);
}

static int portalFacing(const Room* r, const Portal* p)
{
    (void)r;
    return p->n[0] * (p->c[0].x - cam.x) + p->n[1] * (p->c[0].y - cam.y) + p->n[2] * (p->c[0].z - cam.z) < 0;
}

// the rectangle walk (rooms.s): true when the room is to be entered
static long gWalkChecks, gWalkFacing, gWalkUnique;
static const Portal* gWalkSeen[4096];
static int gWalkSeenN;

static int checkRect(Walk* w, int room, const Portal* p, Rect* out)
{
    w->checks++;
    gWalkChecks++;
    if (!portalFacing(&rooms[room], p))
        return 0;
    gWalkFacing++;
    {
        int k = 0;
        while (k < gWalkSeenN && gWalkSeen[k] != p)
            k++;
        if (k == gWalkSeenN && gWalkSeenN < 4096) {
            gWalkSeen[gWalkSeenN++] = p;
            gWalkUnique++;
        }
    }
    V3 pv[4];
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, zmin = 1e30f;
    int near = 0, far = 0;
    for (int i = 0; i < 4; i++)
    {
        V3 v = toView(p->c[i]);
        pv[i] = v;
        if (v.z <= 0) { near++; continue; }
        if (v.z > gFar) far++;
        if (v.z < zmin) zmin = v.z;
        float sx, sy;
        if (v.z < 16) {
            sx = v.x < 0 ? 0 : W;
            sy = v.y < 0 ? 0 : H;
        } else {
            sx = v.x / v.z * F + W / 2;
            sy = v.y / v.z * F + H / 2;
        }
        if (sx < x0) x0 = sx;
        if (sx > x1) x1 = sx;
        if (sy < y0) y0 = sy;
        if (sy > y1) y1 = sy;
    }
    if (near == 4 || far == 4)
        return 0;
    if (near)
    {
        static const int ea[4] = { 0, 1, 2, 3 }, eb[4] = { 3, 0, 1, 2 };
        for (int e = 0; e < 4; e++)
        {
            V3 a = pv[ea[e]], b = pv[eb[e]];
            if ((a.z > 0) == (b.z > 0))
                continue;
            if (a.x < 0 && b.x < 0) x0 = 0;
            else if (a.x > 0 && b.x > 0) x1 = W;
            else { x0 = 0; x1 = W; }
            if (a.y < 0 && b.y < 0) y0 = 0;
            else if (a.y > 0 && b.y > 0) y1 = H;
            else { y0 = 0; y1 = H; }
        }
    }
    Rect c = w->clip[room];
    if (c.x0 > x0) x0 = c.x0;
    if (c.y0 > y0) y0 = c.y0;
    if (c.x1 < x1) x1 = c.x1;
    if (c.y1 < y1) y1 = c.y1;
    if (!(x0 < x1 && y0 < y1))
        return 0;
    Rect win = { x0, y0, x1, y1 };
    if (w->useBoxes && !near)
    {
        for (int i = 0; i < gBoxSeenN; i++)
        {
            const BoxSeen* b = &gBoxSeen[i];
            if (x0 >= b->x0 && y0 >= b->y0 && x1 <= b->x1 && y1 <= b->y1 && pointPast(b, p->c[0]) &&
                pointPast(b, p->c[1]) && pointPast(b, p->c[2]) && pointPast(b, p->c[3]))
                return 0;
        }
    }
    if (w->useMask && !near && maskHides(win, zmin))
        return 0;
    if (w->minArea > 0 && !near && (x1 - x0) * (y1 - y0) < w->minArea) {
        w->fills += 1;
        w->fillPx += (x1 - x0) * (y1 - y0);
        return 0;
    }
    int t = p->target;
    if (zmin < gNearest[t] || near) {
        gNearest[t] = near ? 0 : zmin;
    }
    if (!w->has[t]) {
        gFrom[t] = room;
    }
    if (w->darkShade)
    {
        int bright = 31;
        Room* tr = &rooms[t];
        for (int k = 0; k < tr->nv; k++)
            if (tr->shade[k] < bright) bright = tr->shade[k];
        float zBlack = FOG_MIN + (w->darkShade - bright) * 256.0f;
        if (zBlack < FOG_MIN) zBlack = FOG_MIN;
        if (!near && zmin > zBlack)
        {
            if (!w->unlisted[t]) {
                w->unlisted[t] = 1;
                w->fills += 1;
                w->fillPx += (x1 - x0) * (y1 - y0);
            }
        }
        else
            w->unlisted[t] = 2;     // seen nearer by another way: drawn
    }
    if (!w->has[t]) {
        w->clip[t] = win;
        w->has[t] = 1;
        *out = win;
        return 1;
    }
    Rect o = w->clip[t];
    int grown = x0 < o.x0 || y0 < o.y0 || x1 > o.x1 || y1 > o.y1;
    if (x0 < o.x0) o.x0 = x0;
    if (y0 < o.y0) o.y0 = y0;
    if (x1 > o.x1) o.x1 = x1;
    if (y1 > o.y1) o.y1 = y1;
    w->clip[t] = o;
    *out = win;
    return grown;
}

static void visitRect(Walk* w, int room, int depth)
{
    if (depth > 200) { w->overflow = 1; return; }
    w->visits++;
    roomOccluders(w, room);
    Room* r = &rooms[room];
    for (int k = 0; k < r->np; k++)
    {
        Rect win;
        if (checkRect(w, room, &r->p[k], &win))
        {
            int t = r->p[k].target;
            visitRect(w, t, depth + 1);
            if (!w->listed[t]) {
                w->listed[t] = 1;
                w->list[w->nlist++] = t;
            }
        }
    }
}

// the true shape: each path's window its portals' polygons clipped in turn
static void visitPoly(Walk* w, int room, const Poly* win, int depth)
{
    if (w->checks > 20000 || depth > 60) { w->overflow = 1; return; }
    w->visits++;
    w->inStack[room] = 1;
    roomOccluders(w, room);
    Room* r = &rooms[room];
    for (int k = 0; k < r->np; k++)
    {
        const Portal* p = &r->p[k];
        w->checks++;
        if (w->inStack[p->target] || !portalFacing(r, p))
            continue;
        V3 pv[4];
        int near = 0, far = 0;
        float zmin = 1e30f;
        for (int i = 0; i < 4; i++) {
            pv[i] = toView(p->c[i]);
            if (pv[i].z <= 1) near++;
            if (pv[i].z > VIEW_MAX) far++;
            if (pv[i].z < zmin) zmin = pv[i].z;
        }
        if (near == 4 || far == 4)
            continue;
        Poly q;
        clipProject(pv, 4, &q, 1.0f);
        if (q.n < 3)
            continue;
        if (polyArea(&q) < 0)
        {
            for (int i = 0; i < q.n / 2; i++) {
                float tx = q.x[i], ty = q.y[i];
                q.x[i] = q.x[q.n - 1 - i]; q.y[i] = q.y[q.n - 1 - i];
                q.x[q.n - 1 - i] = tx; q.y[q.n - 1 - i] = ty;
            }
        }
        for (int e = 0; e < win->n && q.n >= 3; e++)
        {
            int f = (e + 1) % win->n;
            float dx = win->x[f] - win->x[e], dy = win->y[f] - win->y[e];
            polyCut(&q, -dy, dx, dy * win->x[e] - dx * win->y[e]);
        }
        if (q.n < 3 || polyArea(&q) < 0.5f)
            continue;
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (int i = 0; i < q.n; i++) {
            if (q.x[i] < x0) x0 = q.x[i];
            if (q.x[i] > x1) x1 = q.x[i];
            if (q.y[i] < y0) y0 = q.y[i];
            if (q.y[i] > y1) y1 = q.y[i];
        }
        Rect b = { x0, y0, x1, y1 };
        if (w->useMask && !near && maskHides(b, zmin))
            continue;
        int t = p->target;
        if (!w->has[t]) {
            w->clip[t] = b;
            w->has[t] = 1;
        } else {
            Rect o = w->clip[t];
            if (x0 < o.x0) o.x0 = x0;
            if (y0 < o.y0) o.y0 = y0;
            if (x1 > o.x1) o.x1 = x1;
            if (y1 > o.y1) o.y1 = y1;
            w->clip[t] = o;
        }
        visitPoly(w, t, &q, depth + 1);
        if (!w->listed[t]) {
            w->listed[t] = 1;
            w->list[w->nlist++] = t;
        }
    }
    w->inStack[room] = 0;
}

static void walkRun(Walk* w, int cameraRoom, int prepass)
{
    w->nlist = 0;
    w->checks = w->visits = 0;
    w->overflow = 0;
    w->fills = w->fillPx = 0;
    memset(w->has, 0, sizeof(w->has));
    memset(w->listed, 0, sizeof(w->listed));
    memset(w->inStack, 0, sizeof(w->inStack));
    memset(w->occDrawn, 0, sizeof(w->occDrawn));
    memset(w->unlisted, 0, sizeof(w->unlisted));
    if (w->useMask) {
        maskClear();
        memset(&maskCost, 0, sizeof(maskCost));
        // the rooms next to the camera's first, whatever order the walk would take them in
        if (prepass) {
            roomOccluders(w, cameraRoom);
            Room* r = &rooms[cameraRoom];
            for (int k = 0; k < r->np; k++)
                if (portalFacing(r, &r->p[k])) {
                    roomOccluders(w, r->p[k].target);
                    if (prepass > 1) {
                        Room* s = &rooms[r->p[k].target];
                        for (int m = 0; m < s->np; m++)
                            if (portalFacing(s, &s->p[m]))
                                roomOccluders(w, s->p[m].target);
                    }
                }
        }
    }
    w->clip[cameraRoom] = (Rect){ 0, 0, W, H };
    w->has[cameraRoom] = 1;
    if (w->usePoly) {
        Poly f;
        f.n = 4;
        f.x[0] = 0; f.y[0] = 0; f.x[1] = W; f.y[1] = 0; f.x[2] = W; f.y[2] = H; f.x[3] = 0; f.y[3] = H;
        visitPoly(w, cameraRoom, &f, 0);
    } else {
        visitRect(w, cameraRoom, 0);
    }
    w->listed[cameraRoom] = 1;
    w->list[w->nlist++] = cameraRoom;
}

// --- the work a list means ---------------------------------------------------------------
typedef struct { double rooms, verts, statics, kept, dropped, px, cycles, vdp1; } Work;

static double faceArea(const Room* r, const int* idx, int n)
{
    V3 v[4];
    for (int i = 0; i < n; i++)
        v[i] = r->vv[idx[i]];
    Poly p;
    clipProject(v, n, &p, NEAR);
    if (p.n < 3)
        return 0;
    polyFrame(&p);
    if (p.n < 3)
        return 0;
    return fabs(polyArea(&p));
}

static inline int faceFacing(const Room* r, const int* idx)
{
    V3 a = r->v[idx[0]], b = r->v[idx[1]], c = r->v[idx[2]];
    float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z, e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
    float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
    return nx * (cam.x - a.x) + ny * (cam.y - a.y) + nz * (cam.z - a.z) < 0;
}

// wholly past one side of the window, behind the near plane or past the far one
static inline int faceCulled(const Room* r, const int* idx, int n, Rect w)
{
    int l = 1, rr = 1, t = 1, b = 1, nz = 1, fz = 1, bk = gBlackFaces;
    for (int i = 0; i < n; i++)
    {
        float x = r->sx[idx[i]], y = r->sy[idx[i]], z = r->vv[idx[i]].z;
        if (bk && r->shade[idx[i]] + (z > FOG_MIN ? (int)((z - FOG_MIN) / 256) : 0) < 31) bk = 0;
        if (x >= w.x0) l = 0;
        if (x <= w.x1) rr = 0;
        if (y >= w.y0) t = 0;
        if (y <= w.y1) b = 0;
        if (z >= NEAR) nz = 0;
        if (z <= gFar) fz = 0;
    }
    return l || rr || t || b || nz || fz || bk;
}

// A face wholly behind a solid box: every corner in its rectangle and past its sides that face the camera.
static int faceBehind(const Room* r, const int* idx, int n)
{
    for (int i = 0; i < gBoxSeenN; i++)
    {
        const BoxSeen* b = &gBoxSeen[i];
        int all = 1;
        for (int k = 0; k < n && all; k++)
        {
            float x = r->sx[idx[k]], y = r->sy[idx[k]];
            all = r->vv[idx[k]].z >= NEAR && x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1 && pointPast(b, r->v[idx[k]]);
        }
        if (all)
            return 1;
    }
    return 0;
}

static const uint8_t* gFaceSet;     // the faces drawn, if not all of them

static inline int faceOut(int id)
{
    return gFaceSet && !(gFaceSet[id >> 3] & (1 << (id & 7)));
}

static void roomWork(int room, Rect w, Work* out)
{
    Room* r = &rooms[room];
    roomProject(r);
    double kept = 0, dropped = 0, px = 0;
    for (int i = 0; i < r->nq; i++)
    {
        int idx[4] = { r->q[i].a, r->q[i].b, r->q[i].c, r->q[i].d };
        if (!faceFacing(r, idx))
            continue;
        if (faceCulled(r, idx, 4, w) || (!r->q[i].transp && faceOut(r->faceBase + i))) { dropped++; continue; }
        if (gBoxFaces && faceBehind(r, idx, 4)) { dropped++; continue; }
        kept++;
        px += faceArea(r, idx, 4);
    }
    for (int i = 0; i < r->nt; i++)
    {
        int* idx = r->t[i];
        if (!faceFacing(r, idx))
            continue;
        if (faceCulled(r, idx, 3, w) || faceOut(r->faceBase + r->nq + i)) { dropped++; continue; }
        if (gBoxFaces && faceBehind(r, idx, 3)) { dropped++; continue; }
        kept++;
        px += faceArea(r, idx, 3);
    }
    out->rooms += 1;
    out->verts += r->nv;
    out->statics += r->nstatic;
    out->kept += kept;
    out->dropped += dropped;
    out->px += px;
    out->cycles += 125.0 * r->nv + 80.0 * dropped + 300.0 * kept + 2000.0 * r->nstatic;
    out->vdp1 += 80.0 * kept + 3.5 * px;
}

static Work listWork(const Walk* w)
{
    Work o;
    memset(&o, 0, sizeof(o));
    for (int i = 0; i < w->nlist; i++)
        roomWork(w->list[i], w->clip[w->list[i]], &o);
    return o;
}

// --- the oracle: a depth buffer at half resolution ---------------------------------------
static float zbuf[ZH][ZW];
static int32_t owner[ZH][ZW];       // a face's id
static long ownerPx[MAXROOMS];
static uint16_t* facePx;
static int faceRoom(int id)
{
    int lo = 0, hi = nrooms - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (rooms[mid].faceBase <= id) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static void fillTri(float x0, float y0, float iz0, float x1, float y1, float iz1, float x2, float y2, float iz2, int room)
{
    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (fabsf(area) < 1e-3f)
        return;
    float inv = 1.0f / area;
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= ZW) maxx = ZW - 1;
    if (maxy >= ZH) maxy = ZH - 1;
    for (int y = miny; y <= maxy; y++)
    {
        float py = y + 0.5f;
        for (int x = minx; x <= maxx; x++)
        {
            float px = x + 0.5f;
            float w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) * inv;
            float w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            float iz = w0 * iz0 + w1 * iz1 + w2 * iz2;
            if (iz > zbuf[y][x]) {
                zbuf[y][x] = iz;
                owner[y][x] = room;
            }
        }
    }
}

static void fillFace(const Room* r, const int* idx, int n, int id)
{
    V3 t[8];
    int m = 0;
    for (int i = 0; i < n; i++)
    {
        V3 a = r->vv[idx[i]], b = r->vv[idx[(i + 1) % n]];
        if (a.z >= NEAR) t[m++] = a;
        if ((a.z >= NEAR) != (b.z >= NEAR)) {
            float k = (NEAR - a.z) / (b.z - a.z);
            t[m++] = (V3){ a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, NEAR };
        }
    }
    if (m < 3)
        return;
    float sx[8], sy[8], iz[8];
    for (int i = 0; i < m; i++) {
        sx[i] = (t[i].x / t[i].z * F + W / 2) * 0.5f;
        sy[i] = (t[i].y / t[i].z * F + H / 2) * 0.5f;
        iz[i] = 1.0f / t[i].z;
    }
    for (int i = 1; i + 1 < m; i++)
        fillTri(sx[0], sy[0], iz[0], sx[i], sy[i], iz[i], sx[i + 1], sy[i + 1], iz[i + 1], id);
}

// which of a walk's rooms show: ownerPx by room
static void oracle(const Walk* w)
{
    for (int y = 0; y < ZH; y++)
        for (int x = 0; x < ZW; x++) {
            zbuf[y][x] = 0;
            owner[y][x] = -1;
        }
    memset(ownerPx, 0, sizeof(ownerPx));
    for (int l = 0; l < w->nlist; l++)
    {
        int room = w->list[l];
        Room* r = &rooms[room];
        Rect c = w->clip[room];
        roomProject(r);
        for (int i = 0; i < r->nq; i++)
        {
            if (r->q[i].transp)
                continue;
            int idx[4] = { r->q[i].a, r->q[i].b, r->q[i].c, r->q[i].d };
            if (faceFacing(r, idx) && !faceCulled(r, idx, 4, c) && !(gBoxFaces && faceBehind(r, idx, 4)))
                fillFace(r, idx, 4, r->faceBase + i);
        }
        for (int i = 0; i < r->nt; i++)
        {
            int* idx = r->t[i];
            if (faceFacing(r, idx) && !faceCulled(r, idx, 3, c) && !(gBoxFaces && faceBehind(r, idx, 3)))
                fillFace(r, idx, 3, r->faceBase + r->nq + i);
        }
    }
    memset(facePx, 0, nfaces * sizeof(uint16_t));
    for (int y = 0; y < ZH; y++)
        for (int x = 0; x < ZW; x++)
            if (owner[y][x] >= 0) {
                ownerPx[faceRoom(owner[y][x])] += 4;
                if (facePx[owner[y][x]] < 60000)
                    facePx[owner[y][x]] += 4;
            }
}

// --- views -------------------------------------------------------------------------------
static uint32_t rngState;
static inline uint32_t rng(void)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}
static inline float urand(float a, float b) { return a + (b - a) * (rng() >> 8) * (1.0f / 16777216.0f); }

static void viewSet(V3 pos, float yaw, float pitch)
{
    cam = pos;
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float f[3] = { sy * cp, sp, cy * cp };
    float r[3] = { cy, 0, -sy };
    float d[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
    if (d[1] < 0) { d[0] = -d[0]; d[1] = -d[1]; d[2] = -d[2]; }
    for (int i = 0; i < 3; i++) { R[0][i] = r[i]; R[1][i] = d[i]; R[2][i] = f[i]; }
    viewStamp++;
}

static int gPlaceInner;

// a place in a room's air
static int placeIn(int room, int sector, V3* pos)
{
    Room* r = &rooms[room];
    int floor = r->s[sector][0], ceil = r->s[sector][1];
    if (floor <= ceil + 200 || floor == -127 * 256 || ceil == -127 * 256)
        return 0;
    int sx = sector / r->zs, sz = sector % r->zs;
    // a room's edge sectors are its portals': a camera there is really in the room next door
    if (gPlaceInner && (sx == 0 || sz == 0 || sx == r->xs - 1 || sz == r->zs - 1))
        return 0;
    pos->x = r->x + (sx + urand(0.02f, 0.98f)) * 1024;
    pos->z = r->z + (sz + urand(0.02f, 0.98f)) * 1024;
    pos->y = urand(ceil + 80, floor - 80);
    return 1;
}

static int randomPlace(V3* pos)
{
    for (;;)
    {
        int room = rng() % nrooms;
        Room* r = &rooms[room];
        if (r->alt || r->ns == 0 || !r->np)
            continue;
        if (placeIn(room, rng() % r->ns, pos))
            return room;
    }
}

// --- the tests ---------------------------------------------------------------------------
enum { T_BASE, T_ORACLE, T_ORACLE_F, T_POLY, T_M8, T_M32, T_M32P1, T_W16, T_W32, T_W64, T_W128, T_W256, T_W512, T_W1024, T_PVS, T_PVS8, T_PVSF, T_PVSF8, T_HOT, T_NEAR64, T_NEAR256, T_NEAR1024, T_NEARR, T_F18, T_F16, T_F14, T_F12, T_F10, T_F8, T_D24, T_D26, T_D28, T_D30, T_D31, T_B31, T_DB31, T_COUNT };
static const char* const kNames[T_COUNT] = { "base", "oracle", "oracle-faces", "poly", "mask8", "mask32", "mask32+1", "tiny16", "tiny32", "tiny64", "tiny128", "tiny256", "tiny512", "tiny1024", "pvs", "pvs8", "pvs-faces", "pvs8-faces", "hot", "near64", "near256", "near1024", "near-rooms", "far18", "far16", "far14", "far12", "far10", "far8", "dark24", "dark26", "dark28", "dark30", "dark31", "faces31", "both31" };

typedef struct { double rooms, verts, statics, kept, px, cycles, vdp1, test, lost, hit, over, lit; } Sum;
typedef struct { Sum s[T_COUNT]; double key, verts; } ViewRow;

static void addRow(Sum* s, const Work* w, double test, double lost, int hit, int over)
{
    s->rooms = w->rooms; s->verts = w->verts; s->statics = w->statics; s->kept = w->kept; s->px = w->px;
    s->cycles = w->cycles; s->vdp1 = w->vdp1; s->test = test; s->lost = lost; s->hit = hit; s->over = over;
}

// --- pvs ---------------------------------------------------------------------------------
static int pvsCell, pvsViews;
static int cellBase[MAXROOMS], cellCount;
static uint8_t (*pvsAll)[MAXROOMS / 8];     // by cell
static uint8_t (*pvsOct)[MAXROOMS / 8];     // by cell * 8 + octant
static uint8_t* cellUsed;
static uint8_t* faceAll, *faceOct;          // the faces shown, by cell and by cell * 8 + octant
static int faceBytes;

static inline int cellOf(int room, float x, float z)
{
    Room* r = &rooms[room];
    int sx = (int)((x - r->x) / 1024), sz = (int)((z - r->z) / 1024);
    if (sx < 0) sx = 0;
    if (sz < 0) sz = 0;
    if (sx >= r->xs) sx = r->xs - 1;
    if (sz >= r->zs) sz = r->zs - 1;
    int cw = (r->zs + pvsCell - 1) / pvsCell;
    return cellBase[room] + (sx / pvsCell) * cw + sz / pvsCell;
}

static inline int octantOf(float yaw)
{
    float t = yaw / (2 * (float)M_PI);
    t -= floorf(t);
    return (int)(t * 8) & 7;
}

static void pvsTrain(void)
{
    static Walk w;
    cellCount = 0;
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        cellBase[i] = cellCount;
        cellCount += ((r->xs + pvsCell - 1) / pvsCell) * ((r->zs + pvsCell - 1) / pvsCell);
    }
    pvsAll = calloc(cellCount, MAXROOMS / 8);
    pvsOct = calloc(cellCount * 8, MAXROOMS / 8);
    cellUsed = calloc(cellCount, 1);
    faceBytes = (nfaces + 7) / 8;
    faceAll = calloc(cellCount, faceBytes);
    faceOct = calloc((size_t)cellCount * 8, faceBytes);
    long done = 0;
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        if (r->alt || !r->ns || !r->np)
            continue;
        for (int s = 0; s < r->ns; s++)
        {
            for (int v = 0; v < pvsViews; v++)
            {
                V3 pos;
                if (!placeIn(i, s, &pos))
                    break;
                int oct = v & 7;
                float yaw = (oct + urand(0, 1)) * (2 * (float)M_PI) / 8;
                viewSet(pos, yaw, urand(-0.6f, 0.6f));
                w.useMask = 0; w.usePoly = 0;
                walkRun(&w, i, 0);
                oracle(&w);
                int cell = cellOf(i, pos.x, pos.z);
                cellUsed[cell] = 1;
                for (int k = 0; k < w.nlist; k++)
                    if (ownerPx[w.list[k]]) {
                        int t = w.list[k];
                        pvsAll[cell][t >> 3] |= 1 << (t & 7);
                        pvsOct[cell * 8 + oct][t >> 3] |= 1 << (t & 7);
                    }
                uint8_t* fa = faceAll + (size_t)cell * faceBytes;
                uint8_t* fo = faceOct + ((size_t)cell * 8 + oct) * faceBytes;
                for (int y = 0; y < ZH; y++)
                    for (int x = 0; x < ZW; x++)
                        if (owner[y][x] >= 0) {
                            int id = owner[y][x];
                            fa[id >> 3] |= 1 << (id & 7);
                            fo[id >> 3] |= 1 << (id & 7);
                        }
                // the camera's room is always kept
                pvsAll[cell][i >> 3] |= 1 << (i & 7);
                pvsOct[cell * 8 + oct][i >> 3] |= 1 << (i & 7);
                done++;
            }
        }
    }
    fprintf(stderr, "pvs trained on %ld views, %d cells\n", done, cellCount);
}

static int cmpSets(const void* a, const void* b) { return memcmp(a, b, MAXROOMS / 8); }

static long distinct(uint8_t (*sets)[MAXROOMS / 8], int n)
{
    uint8_t (*c)[MAXROOMS / 8] = malloc((size_t)n * (MAXROOMS / 8));
    memcpy(c, sets, (size_t)n * (MAXROOMS / 8));
    qsort(c, n, MAXROOMS / 8, cmpSets);
    long d = 0;
    for (int i = 0; i < n; i++)
        if (i == 0 || memcmp(c[i], c[i - 1], MAXROOMS / 8))
            d++;
    free(c);
    return d;
}

static Walk gBase, gTry;

// --- near: the frame's own faces as the occluders ------------------------------------------
static void maskPoly(const Poly* p, float zmax)
{
    maskCost.drawn++;
    float ylo = 1e30f, yhi = -1e30f;
    for (int i = 0; i < p->n; i++) {
        if (p->y[i] < ylo) ylo = p->y[i];
        if (p->y[i] > yhi) yhi = p->y[i];
    }
    int r0 = (int)floorf(ylo / CELL), r1 = (int)floorf(yhi / CELL);
    if (r0 < 0) r0 = 0;
    if (r1 >= CELLH) r1 = CELLH - 1;
    for (int j = r0; j <= r1; j++)
    {
        float a0, b0, a1, b1;
        spanAt(p, j * CELL, &a0, &b0);
        spanAt(p, (j + 1) * CELL, &a1, &b1);
        maskCost.rows++;
        float xl = a0 > a1 ? a0 : a1, xr = b0 < b1 ? b0 : b1;
        if (xl >= xr || a0 > 1e29f || a1 > 1e29f)
            continue;
        int c0 = (int)ceilf(xl / CELL), c1 = (int)floorf(xr / CELL) - 1;
        if (c0 < 0) c0 = 0;
        if (c1 >= CELLW) c1 = CELLW - 1;
        for (int i = c0; i <= c1; i++) {
            maskCost.cells++;
            if (zmax < maskZ[j][i])
                maskZ[j][i] = zmax;
        }
    }
}

// the polygon's cells, row by row, all behind the mask
static int maskHidesPoly(const Poly* in, float zmin)
{
    float ylo = 1e30f, yhi = -1e30f;
    for (int i = 0; i < in->n; i++) {
        if (in->y[i] < ylo) ylo = in->y[i];
        if (in->y[i] > yhi) yhi = in->y[i];
    }
    int r0 = (int)floorf(ylo / CELL), r1 = (int)floorf((yhi - 0.001f) / CELL);
    if (r0 < 0) r0 = 0;
    if (r1 >= CELLH) r1 = CELLH - 1;
    for (int j = r0; j <= r1; j++)
    {
        Poly q = *in;
        polyCut(&q, 0, 1, -(float)(j * CELL));
        if (q.n) polyCut(&q, 0, -1, (float)((j + 1) * CELL));
        if (q.n < 2)
            continue;
        float lo = 1e30f, hi = -1e30f;
        for (int i = 0; i < q.n; i++) {
            if (q.x[i] < lo) lo = q.x[i];
            if (q.x[i] > hi) hi = q.x[i];
        }
        int c0 = (int)floorf(lo / CELL), c1 = (int)floorf((hi - 0.001f) / CELL);
        if (c0 < 0) c0 = 0;
        if (c1 >= CELLW) c1 = CELLW - 1;
        maskCost.rows++;
        for (int i = c0; i <= c1; i++) {
            maskCost.tests++;
            if (!(maskZ[j][i] < zmin))
                return 0;
        }
    }
    return 1;
}

// one face of a room taken nearest first: 1 kept. test: it may be left out if hidden
static int nearFace(const Room* r, const int* idx, int n, Rect w, int id, int transp, float minOcc, int testFaces, double* lost, double* px, double* dropped)
{
    if (!faceFacing(r, idx))
        return 0;
    if (faceCulled(r, idx, n, w)) { (*dropped)++; return 0; }
    V3 v[4];
    float zmin = 1e30f, zmax = -1e30f;
    for (int i = 0; i < n; i++) {
        v[i] = r->vv[idx[i]];
        if (v[i].z < zmin) zmin = v[i].z;
        if (v[i].z > zmax) zmax = v[i].z;
    }
    Poly p;
    clipProject(v, n, &p, NEAR);
    if (p.n < 3) { (*dropped)++; return 0; }
    if (testFaces && zmin >= NEAR)
    {
        maskCost.tried++;
        if (maskHidesPoly(&p, zmin))
        {
            (*dropped)++;
            if (!transp)
                *lost += facePx[id];
            return 0;
        }
    }
    Poly f = p;
    polyFrame(&f);
    double area = f.n >= 3 ? fabs(polyArea(&f)) : 0;
    *px += area;
    if (!transp && minOcc > 0 && area >= minOcc && zmax < VIEW_MAX)
        maskPoly(&p, zmax);
    return 1;
}

static void nearRun(const Walk* base, float minOcc, int testFaces, Work* o, double* test, double* lost)
{
    memset(o, 0, sizeof(*o));
    maskClear();
    memset(&maskCost, 0, sizeof(maskCost));
    *lost = 0;
    for (int l = base->nlist - 1; l >= 0; l--)
    {
        int room = base->list[l];
        Room* r = &rooms[room];
        Rect w = base->clip[room];
        roomProject(r);
        // its window against the mask at the nearest depth of its box, not yet its vertices
        float zmin = 1e30f;
        V3 c[8];
        int m = 0;
        float x0 = 1e30f, y0 = 1e30f, z0 = 1e30f, x1 = -1e30f, y1 = -1e30f, z1 = -1e30f;
        for (int k = 0; k < r->nv; k++) {
            V3 a = r->v[k];
            if (a.x < x0) x0 = a.x;
            if (a.y < y0) y0 = a.y;
            if (a.z < z0) z0 = a.z;
            if (a.x > x1) x1 = a.x;
            if (a.y > y1) y1 = a.y;
            if (a.z > z1) z1 = a.z;
        }
        for (int k = 0; k < 8; k++)
            c[m++] = toView((V3){ k & 1 ? x1 : x0, k & 2 ? y1 : y0, k & 4 ? z1 : z0 });
        for (int k = 0; k < 8; k++)
            if (c[k].z < zmin) zmin = c[k].z;
        maskCost.tried++;
        if (l != base->nlist - 1 && zmin >= NEAR && maskHides(w, zmin))
        {
            *lost += ownerPx[room];
            continue;
        }
        double kept = 0, dropped = 0, px = 0;
        for (int i = 0; i < r->nq; i++)
        {
            int idx[4] = { r->q[i].a, r->q[i].b, r->q[i].c, r->q[i].d };
            kept += nearFace(r, idx, 4, w, r->faceBase + i, r->q[i].transp, minOcc, testFaces, lost, &px, &dropped);
        }
        for (int i = 0; i < r->nt; i++)
            kept += nearFace(r, r->t[i], 3, w, r->faceBase + r->nq + i, 0, 0, testFaces, lost, &px, &dropped);
        o->rooms += 1;
        o->verts += r->nv;
        o->statics += r->nstatic;
        o->kept += kept;
        o->dropped += dropped;
        o->px += px;
        o->cycles += 125.0 * r->nv + 80.0 * dropped + 300.0 * kept + 2000.0 * r->nstatic;
        o->vdp1 += 80.0 * kept + 3.5 * px;
    }
    *test = 600.0 * base->checks + 30.0 * maskCost.tried + 60.0 * maskCost.drawn + 40.0 * maskCost.rows + 4.0 * maskCost.cells + 3.0 * maskCost.tests;
}

// --- hot: the places that hurt -------------------------------------------------------------
static int hotG, hotY, hotT, hotD;
static int hotBase[MAXROOMS], hotCells;
static uint8_t* hotBin;                     // by cell * yaw bin: its views get heavy
static uint8_t (*hotSet)[MAXROOMS / 8];
static long hotCount, hotViews;

static inline int hotCell(int room, float x, float z)
{
    Room* r = &rooms[room];
    int w = r->xs * 1024 / hotG, h = r->zs * 1024 / hotG;
    int gx = (int)((x - r->x) / hotG), gz = (int)((z - r->z) / hotG);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx >= w) gx = w - 1;
    if (gz >= h) gz = h - 1;
    return hotBase[room] + gx * h + gz;
}

static inline int yawBin(float yaw)
{
    float t = yaw / (2 * (float)M_PI);
    t -= floorf(t);
    int b = (int)(t * hotY);
    return b >= hotY ? hotY - 1 : b;
}

static void hotTrain(void)
{
    static Walk w;
    hotCells = 0;
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        hotBase[i] = hotCells;
        hotCells += (r->xs * 1024 / hotG) * (r->zs * 1024 / hotG);
    }
    hotBin = calloc((size_t)hotCells * hotY, 1);
    hotSet = calloc((size_t)hotCells * hotY, MAXROOMS / 8);
    w.useMask = 0; w.usePoly = 0; w.minArea = 0;
    // which bins get heavy, over a coarse look
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        if (r->alt || !r->ns || !r->np)
            continue;
        for (int sc = 0; sc < r->ns; sc++)
            for (int v = 0; v < hotY * 16; v++)
            {
                V3 pos;
                if (!placeIn(i, sc, &pos))
                    break;
                float yaw = urand(0, 2 * (float)M_PI);
                viewSet(pos, yaw, urand(-0.5f, 0.5f));
                walkRun(&w, i, 0);
                long verts = 0;
                for (int k = 0; k < w.nlist; k++)
                    verts += rooms[w.list[k]].nv;
                if (verts >= hotT)
                    hotBin[(size_t)hotCell(i, pos.x, pos.z) * hotY + yawBin(yaw)] = 1;
            }
    }
    // those bins, hard
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        if (r->alt || !r->ns || !r->np)
            continue;
        for (int sc = 0; sc < r->ns; sc++)
        {
            V3 probe;
            if (!placeIn(i, sc, &probe))
                continue;
            int perSector = (1024 / hotG) * (1024 / hotG);
            for (int bin = 0; bin < hotY; bin++)
            {
                int any = 0;
                for (int q = 0; q < perSector * 4 && !any; q++) {
                    V3 t;
                    if (placeIn(i, sc, &t) && hotBin[(size_t)hotCell(i, t.x, t.z) * hotY + bin])
                        any = 1;
                }
                if (!any)
                    continue;
                for (int v = 0; v < hotD * perSector; v++)
                {
                    V3 pos;
                    if (!placeIn(i, sc, &pos))
                        break;
                    size_t b = (size_t)hotCell(i, pos.x, pos.z) * hotY + bin;
                    if (!hotBin[b])
                        continue;
                    viewSet(pos, (bin + urand(0, 1)) * (2 * (float)M_PI) / hotY, urand(-0.6f, 0.6f));
                    walkRun(&w, i, 0);
                    oracle(&w);
                    hotViews++;
                    for (int k = 0; k < w.nlist; k++)
                        if (ownerPx[w.list[k]]) {
                            int t = w.list[k];
                            hotSet[b][t >> 3] |= 1 << (t & 7);
                        }
                    hotSet[b][i >> 3] |= 1 << (i & 7);
                }
            }
        }
    }
    for (size_t b = 0; b < (size_t)hotCells * hotY; b++)
        hotCount += hotBin[b];
}

static void filtered(const Walk* base, const uint8_t* set, Walk* out)
{
    *out = *base;
    out->nlist = 0;
    for (int i = 0; i < base->nlist; i++)
    {
        int t = base->list[i];
        if (set[t >> 3] & (1 << (t & 7)))
            out->list[out->nlist++] = t;
    }
}

static int lostOf(const Walk* base, const Walk* w, double* lost)
{
    uint8_t keep[MAXROOMS];
    memset(keep, 0, sizeof(keep));
    for (int i = 0; i < w->nlist; i++)
        keep[w->list[i]] = 1;
    double l = 0;
    int removed = 0;
    for (int i = 0; i < base->nlist; i++)
        if (!keep[base->list[i]]) {
            removed++;
            l += ownerPx[base->list[i]];
        }
    *lost = l;
    return removed;
}

static double lostFaces(const Walk* base, const uint8_t* set)
{
    double l = 0;
    for (int i = 0; i < base->nlist; i++)
    {
        Room* r = &rooms[base->list[i]];
        int n = r->nq + r->nt;
        for (int k = 0; k < n; k++)
        {
            int id = r->faceBase + k;
            if (facePx[id] && !(set[id >> 3] & (1 << (id & 7))))
                l += facePx[id];
        }
    }
    return l;
}

static void run(const char* name, int views)
{
    ViewRow* rows = calloc(views, sizeof(ViewRow));
    int nocc = 0;
    for (int i = 0; i < nrooms; i++) nocc += rooms[i].nocc;
    for (int v = 0; v < views; v++)
    {
        V3 pos;
        int room = randomPlace(&pos);
        float yaw = urand(0, 2 * (float)M_PI);
        viewSet(pos, yaw, urand(-0.5f, 0.5f));
        gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 0;
        walkRun(&gBase, room, 0);
        oracle(&gBase);
        Work wb = listWork(&gBase);
        ViewRow* row = &rows[v];
        row->key = wb.cycles;
        row->verts = wb.verts;
        addRow(&row->s[T_BASE], &wb, 600.0 * gBase.checks, 0, 0, 0);

        // the oracle's list
        gTry = gBase;
        gTry.nlist = 0;
        for (int i = 0; i < gBase.nlist; i++)
            if (ownerPx[gBase.list[i]] || gBase.list[i] == room)
                gTry.list[gTry.nlist++] = gBase.list[i];
        Work wo = listWork(&gTry);
        addRow(&row->s[T_ORACLE], &wo, 600.0 * gBase.checks, 0, gTry.nlist < gBase.nlist, 0);

        // ... and the faces that show, alone
        {
            static uint8_t* set;
            if (!set) set = malloc((nfaces + 7) / 8);
            memset(set, 0, (nfaces + 7) / 8);
            for (int i = 0; i < nfaces; i++)
                if (facePx[i])
                    set[i >> 3] |= 1 << (i & 7);
            gFaceSet = set;
            Work wf = listWork(&gTry);
            gFaceSet = NULL;
            addRow(&row->s[T_ORACLE_F], &wf, 600.0 * gBase.checks, 0, 1, 0);
        }

        static const struct { int t, poly, k, pre; } tests[] = {
            { T_POLY, 1, 0, 0 }, { T_M8, 0, 8, 0 }, { T_M32, 0, 32, 0 }, { T_M32P1, 0, 32, 1 },
        };
        for (unsigned n = 0; n < sizeof(tests) / sizeof(tests[0]); n++)
        {
            gTry.useMask = tests[n].k > 0;
            gTry.occK = tests[n].k;
            gTry.usePoly = tests[n].poly;
            gTry.minArea = 0;
            walkRun(&gTry, room, tests[n].pre);
            double test = gTry.usePoly ? 1600.0 * gTry.checks : 600.0 * gTry.checks;
            if (gTry.useMask)
                test += 20.0 * maskCost.tried + 480.0 * maskCost.drawn + 40.0 * maskCost.rows + 4.0 * maskCost.cells + 3.0 * maskCost.tests;
            if (gTry.overflow) {
                addRow(&row->s[tests[n].t], &wb, test, 0, 0, 1);
                continue;
            }
            // only rooms the baseline lists count as work: a tighter walk lists no others
            double lost;
            int removed = lostOf(&gBase, &gTry, &lost);
            Work wk = listWork(&gTry);
            addRow(&row->s[tests[n].t], &wk, test, lost, removed > 0, 0);
        }
        {
            static const struct { int t; float far; } farWays[] = {
                { T_F18, 18432 }, { T_F16, 16384 }, { T_F14, 14336 }, { T_F12, 12288 }, { T_F10, 10240 }, { T_F8, 8192 },
            };
            for (unsigned n = 0; n < sizeof(farWays) / sizeof(farWays[0]); n++)
            {
                gFar = farWays[n].far;
                gTry.useMask = 0; gTry.usePoly = 0; gTry.minArea = 0;
                walkRun(&gTry, room, 0);
                Work wk = listWork(&gTry);
                uint8_t keep[MAXROOMS];
                memset(keep, 0, sizeof(keep));
                for (int i = 0; i < gTry.nlist; i++)
                    keep[gTry.list[i]] = 1;
                // the pixels of faces wholly past it or in rooms not entered, and those not near black
                double lost = 0, lit = 0;
                for (int i = 0; i < gBase.nlist; i++)
                {
                    int t = gBase.list[i];
                    Room* r = &rooms[t];
                    roomProject(r);
                    for (int f = 0; f < r->nq + r->nt; f++)
                    {
                        int id = r->faceBase + f;
                        if (!facePx[id])
                            continue;
                        int idx[4], cn;
                        if (f < r->nq) { idx[0] = r->q[f].a; idx[1] = r->q[f].b; idx[2] = r->q[f].c; idx[3] = r->q[f].d; cn = 4; }
                        else { idx[0] = r->t[f - r->nq][0]; idx[1] = r->t[f - r->nq][1]; idx[2] = r->t[f - r->nq][2]; cn = 3; }
                        int past = 1, dark = 1;
                        for (int c = 0; c < cn; c++) {
                            float z = r->vv[idx[c]].z;
                            if (z <= gFar) past = 0;
                            int sh = r->shade[idx[c]] + (z > FOG_MIN ? (int)((z - FOG_MIN) / 256) : 0);
                            if (sh < DARK_SHADE) dark = 0;
                        }
                        if (!keep[t] || past) {
                            lost += facePx[id];
                            if (!dark) lit += facePx[id];
                        }
                    }
                }
                addRow(&row->s[farWays[n].t], &wk, 600.0 * gTry.checks, lost, gTry.nlist < gBase.nlist, 0);
                row->s[farWays[n].t].lit = lit;
                gFar = VIEW_MAX;
            }
        }
        {
            for (int i = 0; i < gBase.nlist; i++)
            {
                Room* r = &rooms[gBase.list[i]];
                roomProject(r);
                int inside = 0;
                for (int k = 0; k < r->nv && !inside; k++)
                    if (r->vv[k].z <= VIEW_MAX) inside = 1;
                gListedRooms += 1;
                if (!inside) gWholePast += 1;
            }
            static const struct { int t, dark; } darkWays[] = { { T_D24, 24 }, { T_D26, 26 }, { T_D28, 28 }, { T_D30, 30 }, { T_D31, 31 }, { T_DB31, 31 } };
            for (unsigned n = 0; n < sizeof(darkWays) / sizeof(darkWays[0]); n++)
            {
                gTry.useMask = 0; gTry.usePoly = 0; gTry.minArea = 0;
                gTry.darkShade = darkWays[n].dark;
                walkRun(&gTry, room, 0);
                gTry.darkShade = 0;
                int kept = 0;
                double lost = 0, lit = 0;
                for (int i = 0; i < gTry.nlist; i++)
                {
                    int t = gTry.list[i];
                    if (gTry.unlisted[t] == 1)
                    {
                        Room* r = &rooms[t];
                        roomProject(r);
                        for (int f = 0; f < r->nq + r->nt; f++)
                        {
                            int id = r->faceBase + f;
                            if (!facePx[id])
                                continue;
                            int idx[4], cn;
                            if (f < r->nq) { idx[0] = r->q[f].a; idx[1] = r->q[f].b; idx[2] = r->q[f].c; idx[3] = r->q[f].d; cn = 4; }
                            else { idx[0] = r->t[f - r->nq][0]; idx[1] = r->t[f - r->nq][1]; idx[2] = r->t[f - r->nq][2]; cn = 3; }
                            int dark = 1;
                            for (int c = 0; c < cn; c++) {
                                float z = r->vv[idx[c]].z;
                                int sh = r->shade[idx[c]] + (z > FOG_MIN ? (int)((z - FOG_MIN) / 256) : 0);
                                if (sh < DARK_SHADE) dark = 0;
                            }
                            lost += facePx[id];
                            if (!dark) lit += facePx[id];
                        }
                    }
                    else
                        gTry.list[kept++] = t;
                }
                gTry.nlist = kept;
                gBlackFaces = darkWays[n].t == T_DB31;
                Work wk = listWork(&gTry);
                gBlackFaces = 0;
                wk.cycles += 300.0 * gTry.fills;
                wk.vdp1 += 80.0 * gTry.fills + 2.0 * gTry.fillPx;
                addRow(&row->s[darkWays[n].t], &wk, 600.0 * gTry.checks, lost, kept < gBase.nlist, 0);
                row->s[darkWays[n].t].lit = lit;
            }
            gBlackFaces = 1;
            Work wk = listWork(&gBase);
            gBlackFaces = 0;
            addRow(&row->s[T_B31], &wk, 600.0 * gBase.checks, 0, 0, 0);
        }
        static const struct { int t; float area; } tiny[] = {
            { T_W16, 16 }, { T_W32, 32 }, { T_W64, 64 }, { T_W128, 128 }, { T_W256, 256 }, { T_W512, 512 }, { T_W1024, 1024 },
        };
        for (unsigned n = 0; n < sizeof(tiny) / sizeof(tiny[0]); n++)
        {
            gTry.useMask = 0; gTry.usePoly = 0;
            gTry.minArea = tiny[n].area;
            walkRun(&gTry, room, 0);
            double lost;
            int removed = lostOf(&gBase, &gTry, &lost);
            Work wk = listWork(&gTry);
            // each window filled: a command, its pixels; a little C
            wk.cycles += 300.0 * gTry.fills;
            wk.vdp1 += 80.0 * gTry.fills + 2.0 * gTry.fillPx;
            addRow(&row->s[tiny[n].t], &wk, 600.0 * gTry.checks, lost, removed > 0, 0);
        }
        {
            static const struct { int t; float occ; int faces; } nearWays[] = {
                { T_NEAR64, 64, 1 }, { T_NEAR256, 256, 1 }, { T_NEAR1024, 1024, 1 }, { T_NEARR, 256, 0 },
            };
            for (unsigned n = 0; n < sizeof(nearWays) / sizeof(nearWays[0]); n++)
            {
                Work wk;
                double test, lost;
                nearRun(&gBase, nearWays[n].occ, nearWays[n].faces, &wk, &test, &lost);
                addRow(&row->s[nearWays[n].t], &wk, test, lost, wk.rooms < gBase.nlist, 0);
            }
        }
        if (hotG)
        {
            size_t b = (size_t)hotCell(room, pos.x, pos.z) * hotY + yawBin(yaw);
            int have = 0;
            for (int k = 0; k < MAXROOMS / 8; k++)
                have |= hotSet[b][k];
            if (hotBin[b] && have)
            {
                double lost;
                filtered(&gBase, hotSet[b], &gTry);
                int removed = lostOf(&gBase, &gTry, &lost);
                Work wk = listWork(&gTry);
                addRow(&row->s[T_HOT], &wk, 600.0 * gBase.checks, lost, removed > 0, 0);
            }
            else
                addRow(&row->s[T_HOT], &wb, 600.0 * gBase.checks, 0, 0, 1);
        }
        if (pvsCell)
        {
            int cell = cellOf(room, pos.x, pos.z);
            int oct = octantOf(yaw);
            double lost;
            if (cellUsed[cell])
            {
                filtered(&gBase, pvsAll[cell], &gTry);
                int removed = lostOf(&gBase, &gTry, &lost);
                Work wk = listWork(&gTry);
                addRow(&row->s[T_PVS], &wk, 600.0 * gBase.checks, lost, removed > 0, 0);
                filtered(&gBase, pvsOct[cell * 8 + oct], &gTry);
                removed = lostOf(&gBase, &gTry, &lost);
                wk = listWork(&gTry);
                addRow(&row->s[T_PVS8], &wk, 600.0 * gBase.checks, lost, removed > 0, 0);

                // by face, rooms with none left out whole
                const uint8_t* fa = faceAll + (size_t)cell * faceBytes;
                const uint8_t* fo = faceOct + ((size_t)cell * 8 + oct) * faceBytes;
                filtered(&gBase, pvsAll[cell], &gTry);
                gFaceSet = fa;
                wk = listWork(&gTry);
                gFaceSet = NULL;
                addRow(&row->s[T_PVSF], &wk, 600.0 * gBase.checks, lostFaces(&gBase, fa), 1, 0);
                filtered(&gBase, pvsOct[cell * 8 + oct], &gTry);
                gFaceSet = fo;
                wk = listWork(&gTry);
                gFaceSet = NULL;
                addRow(&row->s[T_PVSF8], &wk, 600.0 * gBase.checks, lostFaces(&gBase, fo), 1, 0);
            }
            else
            {
                addRow(&row->s[T_PVS], &wb, 600.0 * gBase.checks, 0, 0, 1);
                addRow(&row->s[T_PVS8], &wb, 600.0 * gBase.checks, 0, 0, 1);
                addRow(&row->s[T_PVSF], &wb, 600.0 * gBase.checks, 0, 0, 1);
                addRow(&row->s[T_PVSF8], &wb, 600.0 * gBase.checks, 0, 0, 1);
            }
        }
    }
    // the heaviest fifth by the baseline's room work
    double* keys = malloc(sizeof(double) * views);
    for (int v = 0; v < views; v++) keys[v] = rows[v].key;
    for (int i = 1; i < views; i++) {
        double k = keys[i];
        int j = i - 1;
        while (j >= 0 && keys[j] > k) { keys[j + 1] = keys[j]; j--; }
        keys[j + 1] = k;
    }
    double cut = keys[(int)(views * 0.8)];
    static const char* const kBands[3] = { "all", "heavy", "big" };
    for (int band = 0; band < 3; band++)
    {
        for (int t = 0; t < T_COUNT; t++)
        {
            if ((t == T_PVS || t == T_PVS8 || t == T_PVSF || t == T_PVSF8) && !pvsCell)
                continue;
            if (t == T_HOT && !hotG)
                continue;
            Sum a;
            memset(&a, 0, sizeof(a));
            double n = 0, lostViews = 0, lost16 = 0, worst = 0, lit = 0;
            for (int v = 0; v < views; v++)
            {
                if (band == 1 && rows[v].key < cut)
                    continue;
                if (band == 2 && rows[v].verts < 2000)
                    continue;
                const Sum* s = &rows[v].s[t];
                a.rooms += s->rooms; a.verts += s->verts; a.statics += s->statics; a.kept += s->kept; a.px += s->px;
                a.cycles += s->cycles; a.vdp1 += s->vdp1; a.test += s->test; a.lost += s->lost; a.hit += s->hit; a.over += s->over;
                lostViews += s->lost > 0;
                lit += s->lit;
                lost16 += s->lost >= 64;
                if (s->lost > worst) worst = s->lost;
                n++;
            }
            if (n == 0)
                continue;
            printf("%s %s %s views %.0f rooms %.2f verts %.1f statics %.1f kept %.1f px %.0f cycles %.0f vdp1 %.0f test %.0f lost %.2f lostviews %.0f lost64 %.0f worst %.0f hit %.0f over %.0f lit %.2f\n",
                   name, kBands[band], kNames[t], n, a.rooms / n, a.verts / n, a.statics / n, a.kept / n, a.px / n,
                   a.cycles / n, a.vdp1 / n, a.test / n, a.lost / n, lostViews, lost16, worst, a.hit, a.over, lit / n);
        }
    }
    if (pvsCell)
    {
        long used = 0;
        for (int c = 0; c < cellCount; c++) used += cellUsed[c];
        long d1 = distinct(pvsAll, cellCount), d8 = distinct(pvsOct, cellCount * 8);
        int setBytes = (nrooms + 7) / 8;
        printf("%s pvsdata cells %d used %ld setbytes %d distinct %ld bytes %ld octdistinct %ld octbytes %ld\n", name, cellCount, used, setBytes,
               d1, d1 * setBytes + cellCount * 2L, d8, d8 * setBytes + cellCount * 16L);
    }
    if (hotG)
    {
        // the bins' sets, distinct; each bin a key and an index
        uint8_t (*c)[MAXROOMS / 8] = malloc((size_t)hotCount * (MAXROOMS / 8) + 1);
        long n = 0;
        for (size_t b = 0; b < (size_t)hotCells * hotY; b++)
            if (hotBin[b])
                memcpy(c[n++], hotSet[b], MAXROOMS / 8);
        long d = n ? distinct(c, n) : 0;
        int setBytes = (nrooms + 7) / 8;
        printf("%s hotdata bins %ld distinct %ld setbytes %d bytes %ld trained %ld\n", name, n, d, setBytes, d * setBytes + n * 5, hotViews);
    }
    printf("%s occluders %d\n", name, nocc);
    printf("%s whole listed %.0f past %.0f\n", name, gListedRooms, gWholePast);
}

// a stamp's question: poses round one camera, within its room's air
static void region(int room, float radius, float arc, float pitchRange, int views)
{
    V3 centre = cam;
    float yaw0 = atan2f(R[2][0], R[2][2]), pitch0 = asinf(R[2][1]);
    static double sum[MAXROOMS], worst[MAXROOMS], seen[MAXROOMS], cost[MAXROOMS], vcost[MAXROOMS];
    Room* r = &rooms[room];
    int done = 0;
    double base = 0, basev = 0, baseVerts = 0, baseRooms = 0;
    uint32_t tries = 0;
    rngState = 12345;
    static Work* one;
    static struct { float x, y, z, yaw, pitch; } *pose;
    if (!pose) pose = malloc(sizeof(*pose) * views);
    (void)one;
    while (done < views && tries < (uint32_t)views * 200)
    {
        tries++;
        V3 pos = { centre.x + urand(-radius, radius), centre.y + urand(-radius / 2, radius / 2), centre.z + urand(-radius, radius) };
        int sx = (int)floorf((pos.x - r->x) / 1024), sz = (int)floorf((pos.z - r->z) / 1024);
        if (sx < 0 || sz < 0 || sx >= r->xs || sz >= r->zs)
            continue;
        int* sector = r->s[sx * r->zs + sz];
        if (sector[0] <= sector[1] + 200 || pos.y > sector[0] - 64 || pos.y < sector[1] + 64)
            continue;
        float yaw = yaw0 + urand(-arc, arc), pitch = pitch0 + urand(-pitchRange, pitchRange);
        viewSet(pos, yaw, pitch);
        gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 0;
        walkRun(&gBase, room, 0);
        oracle(&gBase);
        Work all;
        memset(&all, 0, sizeof(all));
        for (int i = 0; i < gBase.nlist; i++)
        {
            int t = gBase.list[i];
            Work w;
            memset(&w, 0, sizeof(w));
            roomWork(t, gBase.clip[t], &w);
            all.cycles += w.cycles; all.vdp1 += w.vdp1; all.verts += w.verts; all.rooms += 1;
            cost[t] += w.cycles;
            vcost[t] += w.vdp1;
            seen[t] += 1;
            sum[t] += ownerPx[t];
            if (ownerPx[t] > worst[t]) worst[t] = ownerPx[t];
        }
        base += all.cycles; basev += all.vdp1; baseVerts += all.verts; baseRooms += all.rooms;
        done++;
    }
    if (!done) { printf("no pose\n"); return; }
    if (getenv("OCC_ROOMS"))
    {
        // these rooms, one by one: the most and the mean pixels, how often listed, their cost
        char list[512];
        strncpy(list, getenv("OCC_ROOMS"), sizeof(list) - 1);
        for (char* t = strtok(list, ","); t; t = strtok(NULL, ","))
        {
            int n = atoi(t);
            printf("  room %3d: %3d v, most %5.0f px, mean %6.1f px, listed %3.0f%%, CPU %.2f ms, VDP1 %.2f ms\n", n, rooms[n].nv, worst[n], sum[n] / done, 100 * seen[n] / done, cost[n] / done / 28636.36, vcost[n] / done / 28636.36);
        }
    }
    const double MS = 1 / 28636.36;
    printf("poses %d (radius %.0f, heading +-%.0f, pitch +-%.0f): rooms %.1f verts %.0f CPU %.2f ms VDP1 %.2f ms\n", done, radius, arc * 57.3, pitchRange * 57.3, baseRooms / done, baseVerts / done, base / done * MS, basev / done * MS);
    static const double tols[] = { 0, 16, 64, 256, 1024 };
    for (int k = 0; k < 5; k++)
    {
        double saved = 0, savedv = 0, verts = 0, lost = 0;
        int count = 0;
        for (int t = 0; t < nrooms; t++)
        {
            if (!seen[t] || t == room || worst[t] > tols[k])
                continue;
            count++;
            saved += cost[t] / done;
            savedv += vcost[t] / done;
            verts += rooms[t].nv * seen[t] / done;
            lost += sum[t] / done;
        }
        printf("  rooms showing at most %5.0f px at any pose: %2d of them, %5.0f verts, CPU -%.2f ms, VDP1 -%.2f ms, %.1f px lost on average\n", tols[k], count, verts, saved * MS, savedv * MS, lost);
    }
}

// The chase cameras at the same places and headings, a camera standing over a room's floor: OpenLara's
// (849 up, looking 10 degrees down, focal 256) and retail's (581 up, level, focal 209.75, the game's own
// figures at room 103), and the two halves of the change between them. Tiny 256 as the game has it.
static void cameras(const char* name, int views)
{
    static const struct { float up, pitch, focal; const char* name; } ways[] = {
        { 849, 10.1f / 57.3f, 256.0f, "old" },
        { 849, 10.1f / 57.3f, 209.75f, "old-wide" },
        { 581, 0, 256.0f, "level" },
        { 581, 0, 209.75f, "retail" },
    };
    enum { WAYS = 4 };
    Work* rows = malloc(sizeof(Work) * WAYS * views);
    double (*order)[2] = malloc(sizeof(double) * 2 * views);
    for (int v = 0; v < views; v++)
    {
        int room, sector, floor, ceil;
        for (;;)
        {
            room = rng() % nrooms;
            Room* r = &rooms[room];
            if (r->alt || r->ns == 0 || !r->np)
                continue;
            sector = rng() % r->ns;
            floor = r->s[sector][0];
            ceil = r->s[sector][1];
            if (floor != -127 * 256 && ceil != -127 * 256 && floor - 849 - 40 > ceil)
                break;
        }
        Room* r = &rooms[room];
        V3 pos;
        pos.x = r->x + (sector / r->zs + urand(0.02f, 0.98f)) * 1024;
        pos.z = r->z + (sector % r->zs + urand(0.02f, 0.98f)) * 1024;
        float yaw = urand(0, 6.2832f);
        for (int k = 0; k < WAYS; k++)
        {
            pos.y = floor - ways[k].up;
            F = ways[k].focal;
            viewSet(pos, yaw, ways[k].pitch);
            gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 256;
            walkRun(&gBase, room, 0);
            rows[v * WAYS + k] = listWork(&gBase);
        }
        order[v][0] = rows[v * WAYS + 3].verts;
        order[v][1] = v;
    }
    // heaviest tenth, by retail's vertices
    for (int i = 1; i < views; i++)
    {
        double a = order[i][0], b = order[i][1];
        int j = i - 1;
        while (j >= 0 && order[j][0] < a) {
            order[j + 1][0] = order[j][0];
            order[j + 1][1] = order[j][1];
            j--;
        }
        order[j + 1][0] = a;
        order[j + 1][1] = b;
    }
    for (int part = 0; part < 2; part++)
    {
        int n = part ? views / 10 : views;
        for (int k = 0; k < WAYS; k++)
        {
            Work t;
            memset(&t, 0, sizeof(t));
            int over = 0;
            for (int i = 0; i < n; i++)
            {
                const Work* w = &rows[(int)order[i][1] * WAYS + k];
                t.rooms += w->rooms; t.verts += w->verts; t.statics += w->statics; t.kept += w->kept;
                t.dropped += w->dropped; t.px += w->px; t.cycles += w->cycles; t.vdp1 += w->vdp1;
                over += w->verts >= 2000;
            }
            printf("%s %s %s rooms %.1f verts %.0f statics %.1f kept %.0f dropped %.0f px %.0f ms %.2f vdp1ms %.2f over2000 %.1f\n",
                   name, part ? "heavy" : "all", ways[k].name, t.rooms / n, t.verts / n, t.statics / n, t.kept / n, t.dropped / n,
                   t.px / n, t.cycles / n / 28636.36, t.vdp1 / n / 28636.36, 100.0 * over / n);
        }
    }
}

// Pixels no room's face covers, from retail's chase camera at random places, headings and looks, nothing cut by
// distance or by the tiny-window rule: holes in the level itself. By the smallest listed window each hole's pixel
// falls in: for each such room the views it was listed in, those with holes, and their pixels a view.
static void holes(const char* name, int views)
{
    static long listedIn[MAXROOMS], holedIn[MAXROOMS];
    static double holePx[MAXROOMS];
    long withHoles = 0, big = 0;
    double total = 0;
    gFar = 1e9f;
    F = 209.75f;
    for (int v = 0; v < views; v++)
    {
        int room, sector, floor, ceil;
        for (;;)
        {
            room = rng() % nrooms;
            Room* r = &rooms[room];
            if (r->alt || r->ns == 0 || !r->np)
                continue;
            sector = rng() % r->ns;
            floor = r->s[sector][0];
            ceil = r->s[sector][1];
            if (floor != -127 * 256 && ceil != -127 * 256 && floor - 581 - 40 > ceil)
                break;
        }
        Room* r = &rooms[room];
        V3 pos;
        pos.x = r->x + (sector / r->zs + urand(0.02f, 0.98f)) * 1024;
        pos.z = r->z + (sector % r->zs + urand(0.02f, 0.98f)) * 1024;
        pos.y = floor - 581;
        viewSet(pos, urand(0, 6.2832f), urand(-0.5f, 0.3f));
        gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 0;
        walkRun(&gBase, room, 0);
        oracle(&gBase);
        static uint8_t seen[MAXROOMS];
        memset(seen, 0, sizeof(seen));
        long count = 0;
        for (int i = 0; i < gBase.nlist; i++)
            listedIn[gBase.list[i]]++;
        for (int y = 0; y < ZH; y++)
            for (int x = 0; x < ZW; x++)
            {
                if (owner[y][x] >= 0)
                    continue;
                count += 4;
                float px = x * 2 + 1, py = y * 2 + 1, best = 1e30f;
                int at = -1;
                for (int i = 0; i < gBase.nlist; i++)
                {
                    Rect c = gBase.clip[gBase.list[i]];
                    if (px < c.x0 || px > c.x1 || py < c.y0 || py > c.y1)
                        continue;
                    float area = (c.x1 - c.x0) * (c.y1 - c.y0);
                    if (area < best) {
                        best = area;
                        at = gBase.list[i];
                    }
                }
                if (at >= 0) {
                    holePx[at] += 4;
                    if (!seen[at]) {
                        seen[at] = 1;
                        holedIn[at]++;
                    }
                }
            }
        total += count;
        withHoles += count > 0;
        big += count >= 400;
    }
    printf("%s views %d with_holes %.1f%% over400px %.1f%% mean_px %.0f\n", name, views, 100.0 * withHoles / views, 100.0 * big / views, total / views);
    for (int k = 0; k < 8; k++)
    {
        int b = -1;
        for (int i = 0; i < nrooms; i++)
            if (holePx[i] > 0 && (b < 0 || holePx[i] > holePx[b]))
                b = i;
        if (b < 0)
            break;
        printf("  room %d listed %ld holed %ld px_when_holed %.0f\n", b, listedIn[b], holedIn[b], holePx[b] / (holedIn[b] ? holedIn[b] : 1));
        holePx[b] = 0;
    }
}

// --- solid boxes over random views ---------------------------------------------------------
typedef struct { double views, rooms, verts, kept, cycles, vdp1, rooms0, verts0, kept0, cycles0, vdp10, test, lost, lostViews, lost64, worst, bigLost; } BoxSum;

static int32_t gOwnerBase[ZH][ZW];

static void boxesLoad(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    float v[6];
    int side = 0;
    gSolidN = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && gSolidN < MAXBOXES)
    {
        side = 0;
        if (sscanf(line, "%f %f %f %f %f %f %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &side) < 6)
            continue;
        SolidBox* b = &gSolid[gSolidN++];
        b->lo[0] = v[0]; b->hi[0] = v[1];
        b->lo[1] = v[2]; b->hi[1] = v[3];
        b->lo[2] = v[4]; b->hi[2] = v[5];
        b->side = side;
    }
    fclose(f);
}

// cycles a box on the frame takes to find, and a face in a room it lies over to test (renderer: to be measured)
#define BOX_SEE_CYCLES  1200.0
#define BOX_FACE_CYCLES 15.0

static void boxesRow(const char* name, const char* band, const BoxSum* b)
{
    double n = b->views > 0 ? b->views : 1;
    printf("%s %s views %.0f rooms %.1f -> %.1f verts %.0f -> %.0f kept %.0f -> %.0f cycles %.0f -> %.0f test %.0f vdp1 %.0f -> %.0f lost %.1f lostviews %.0f lost64 %.0f worst %.0f\n",
           name, band, b->views, b->rooms0 / n, b->rooms / n, b->verts0 / n, b->verts / n, b->kept0 / n, b->kept / n,
           b->cycles0 / n, b->cycles / n, b->test / n, b->vdp10 / n, b->vdp1 / n, b->lost / n, b->lostViews, b->lost64, b->worst);
}

// How much of a walk is done twice: portals checked, those facing the camera, the different ones of those.
static void walkStat(const char* name, int views)
{
    double checks = 0, facing = 0, unique = 0, visits = 0, rooms_ = 0, verts = 0;
    int n = 0, worstChecks = 0, worstFacing = 0, worstUnique = 0;
    for (int v = 0; v < views; v++)
    {
        V3 pos;
        int room = randomPlace(&pos);
        viewSet(pos, urand(0, 2 * (float)M_PI), urand(-0.5f, 0.5f));
        gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 256; gBase.useBoxes = 0;
        gWalkChecks = gWalkFacing = gWalkUnique = 0;
        gWalkSeenN = 0;
        walkRun(&gBase, room, 0);
        Work w = listWork(&gBase);
        if (w.verts < 2000)
            continue;
        n++;
        checks += gWalkChecks; facing += gWalkFacing; unique += gWalkUnique; visits += gBase.visits; rooms_ += w.rooms; verts += w.verts;
        if (gWalkFacing > worstFacing) {
            worstFacing = gWalkFacing; worstChecks = gWalkChecks; worstUnique = gWalkUnique;
        }
    }
    printf("%s heavy views %d: rooms %.1f verts %.0f room visits %.1f portals checked %.1f facing %.1f different %.1f; worst facing %d (checked %d, different %d)\n", name, n, rooms_ / n, verts / n, visits / n, checks / n, facing / n, unique / n, worstFacing, worstChecks, worstUnique);
}

static void boxes(const char* name, int views)
{
    BoxSum all, heavy;
    memset(&all, 0, sizeof(all));
    memset(&heavy, 0, sizeof(heavy));
    for (int v = 0; v < views; v++)
    {
        V3 pos;
        int room = randomPlace(&pos);
        viewSet(pos, urand(0, 2 * (float)M_PI), urand(-0.5f, 0.5f));
        gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 256; gBase.useBoxes = 0;
        gBoxFaces = 0;
        walkRun(&gBase, room, 0);
        Work wb = listWork(&gBase);
        oracle(&gBase);
        memcpy(gOwnerBase, owner, sizeof(owner));
        boxesSee(gSolid, gSolidN);
        gTry = gBase;
        gTry.useBoxes = 1;
        walkRun(&gTry, room, 0);
        gBoxFaces = 1;
        Work wt = listWork(&gTry);
        oracle(&gTry);
        gBoxFaces = 0;
        long lost = 0;
        for (int y = 0; y < ZH; y++)
            for (int x = 0; x < ZW; x++)
                if (owner[y][x] != gOwnerBase[y][x])
                    lost += 4;
        if (lost && getenv("OCC_BOXLOG"))
        {
            printf("lost %ld room %d cam %.0f %.0f %.0f forward %.3f %.3f %.3f\n", lost, room, cam.x, cam.y, cam.z, R[2][0], R[2][1], R[2][2]);
            for (int i = 0; i < gBoxSeenN; i++)
                printf("  box %.0f %.0f .. %.0f %.0f past x %.0f %.0f y %.0f %.0f z %.0f %.0f\n", gBoxSeen[i].x0, gBoxSeen[i].y0, gBoxSeen[i].x1, gBoxSeen[i].y1,
                       gBoxSeen[i].lo[0], gBoxSeen[i].hi[0], gBoxSeen[i].lo[1], gBoxSeen[i].hi[1], gBoxSeen[i].lo[2], gBoxSeen[i].hi[2]);
            // the faces that lost pixels, and what shows there now
            static int ids[64], counts[64], now[64];
            int n = 0;
            float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
            for (int y = 0; y < ZH; y++)
                for (int x = 0; x < ZW; x++)
                    if (owner[y][x] != gOwnerBase[y][x])
                    {
                        bx0 = fminf(bx0, x * 2); by0 = fminf(by0, y * 2); bx1 = fmaxf(bx1, x * 2); by1 = fmaxf(by1, y * 2);
                        int k = 0;
                        while (k < n && !(ids[k] == gOwnerBase[y][x] && now[k] == owner[y][x]))
                            k++;
                        if (k == n && n < 64) {
                            ids[n] = gOwnerBase[y][x]; now[n] = owner[y][x]; counts[n] = 0; n++;
                        }
                        if (k < 64)
                            counts[k]++;
                    }
            printf("  changed px within %.0f %.0f .. %.0f %.0f; listed", bx0, by0, bx1, by1);
            for (int i = 0; i < gBase.nlist; i++)
                if (!gTry.listed[gBase.list[i]])
                    printf(" -%d", gBase.list[i]);
            printf("\n");
            for (int k = 0; k < n && k < 12; k++)
            {
                int ra = ids[k] >= 0 ? faceRoom(ids[k]) : -1, rb = now[k] >= 0 ? faceRoom(now[k]) : -1;
                printf("    %d px: room %d face %d -> room %d face %d\n", counts[k] * 4, ra, ra >= 0 ? ids[k] - rooms[ra].faceBase : -1, rb, rb >= 0 ? now[k] - rooms[rb].faceBase : -1);
            }
        }
        double test = BOX_SEE_CYCLES * gBoxSeenN + (gBoxSeenN ? BOX_FACE_CYCLES * (wt.kept + wt.dropped) : 0);
        BoxSum* bands[2] = { &all, wb.verts >= 2000 ? &heavy : NULL };
        for (int k = 0; k < 2; k++)
        {
            BoxSum* b = bands[k];
            if (!b)
                continue;
            b->views++;
            b->rooms0 += wb.rooms; b->verts0 += wb.verts; b->kept0 += wb.kept; b->cycles0 += wb.cycles; b->vdp10 += wb.vdp1;
            b->rooms += wt.rooms; b->verts += wt.verts; b->kept += wt.kept; b->cycles += wt.cycles; b->vdp1 += wt.vdp1;
            b->test += test;
            b->lost += lost;
            b->lostViews += lost > 0;
            b->lost64 += lost >= 64;
            if (lost > b->worst)
                b->worst = lost;
        }
    }
    boxesRow(name, "all", &all);
    boxesRow(name, "heavy", &heavy);
}

// --- boxes found in the rock ----------------------------------------------------------------
// Air: each sector's column between its ceiling and floor, a click or two over for slopes, in any room,
// alternates too. What's left is rock, or nothing at all; a box wholly in it hides what's behind as long
// as the rooms round it are closed, which the search's lost pixels check.
#define AIR_Y 256
#define AIR_MARGIN 512
#define SEARCH_VERTS 1500
#define SEARCH_KEEP 400

static uint8_t* gAir;
static int gAx, gAy, gAz, gAx0, gAy0, gAz0;

static inline uint8_t* airAt(int x, int y, int z)
{
    return &gAir[(y * gAz + z) * gAx + x];
}

static void airMap(void)
{
    int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30), y0 = 1 << 30, y1 = -(1 << 30);
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        x0 = r->x < x0 ? r->x : x0;
        z0 = r->z < z0 ? r->z : z0;
        x1 = r->x + r->xs * 1024 > x1 ? r->x + r->xs * 1024 : x1;
        z1 = r->z + r->zs * 1024 > z1 ? r->z + r->zs * 1024 : z1;
        y0 = r->ytop < y0 ? r->ytop : y0;
        y1 = r->ybot > y1 ? r->ybot : y1;
    }
    gAx0 = x0; gAz0 = z0;
    gAy0 = (y0 - 2048) / AIR_Y * AIR_Y;
    gAx = (x1 - x0) / 1024;
    gAz = (z1 - z0) / 1024;
    gAy = (y1 + 2048 - gAy0) / AIR_Y + 1;
    gAir = calloc(gAx * gAy * gAz, 1);
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        for (int sx = 0; sx < r->xs; sx++)
            for (int sz = 0; sz < r->zs; sz++)
            {
                int k = sx * r->zs + sz;
                if (k >= r->ns)
                    continue;
                int floor = r->s[k][0], ceil = r->s[k][1];
                if (floor == -127 * 256 || ceil == -127 * 256)
                    continue;
                int a = (ceil - AIR_MARGIN - gAy0) / AIR_Y, b = (floor + AIR_MARGIN - gAy0) / AIR_Y;
                a = a < 0 ? 0 : a;
                b = b >= gAy ? gAy - 1 : b;
                int x = (r->x - gAx0) / 1024 + sx, z = (r->z - gAz0) / 1024 + sz;
                for (int y = a; y <= b; y++)
                    *airAt(x, y, z) = 1;
            }
    }
}

static SolidBox* gCands;
static int gCandN;

static void candAdd(int x0, int x1, int y0, int y1, int z0, int z1)
{
    if (gCandN == 200000)
        return;
    SolidBox* b = &gCands[gCandN++];
    b->lo[0] = gAx0 + x0 * 1024.0f; b->hi[0] = gAx0 + x1 * 1024.0f;
    b->lo[1] = gAy0 + y0 * (float)AIR_Y; b->hi[1] = gAy0 + y1 * (float)AIR_Y;
    b->lo[2] = gAz0 + z0 * 1024.0f; b->hi[2] = gAz0 + z1 * 1024.0f;
    b->side = 0;
}

// Every box of rock that can't grow: in x and z by the rectangles of each range of heights
// in steps of 1024, a rectangle the next step up or down also has left to the taller range.
static int rockRow(int x0, int x1, int y0, int y1, int z0, int z1)
{
    if (y0 < 0 || y1 > gAy)
        return 0;
    for (int y = y0; y < y1; y++)
        for (int z = z0; z < z1; z++)
            for (int x = x0; x < x1; x++)
                if (*airAt(x, y, z))
                    return 0;
    return 1;
}

static void candsFind(void)
{
    gCands = malloc(sizeof(SolidBox) * 200000);
    int* height = malloc(sizeof(int) * (gAz + 1));
    int* stStart = malloc(sizeof(int) * (gAz + 2));
    int* stH = malloc(sizeof(int) * (gAz + 2));
    uint8_t* mask = malloc(gAx * gAz);
    const int step = 1024 / AIR_Y;
    for (int y0 = 0; y0 + step <= gAy; y0 += step)
    {
        memset(mask, 1, gAx * gAz);
        for (int y1 = y0 + step; y1 <= gAy; y1 += step)
        {
            int any = 0;
            for (int z = 0; z < gAz; z++)
                for (int x = 0; x < gAx; x++)
                {
                    uint8_t* m = &mask[z * gAx + x];
                    for (int y = y1 - step; *m && y < y1; y++)
                        if (*airAt(x, y, z))
                            *m = 0;
                    any |= *m;
                }
            if (!any)
                break;
            memset(height, 0, sizeof(int) * (gAz + 1));
            for (int x = 0; x < gAx; x++)
            {
                for (int z = 0; z < gAz; z++)
                    height[z] = mask[z * gAx + x] ? height[z] + 1 : 0;
                height[gAz] = 0;
                int top = 0;
                for (int z = 0; z <= gAz; z++)
                {
                    int start = z;
                    while (top && stH[top - 1] > height[z])
                    {
                        int h = stH[top - 1];
                        start = stStart[top - 1];
                        top--;
                        int grows = x + 1 < gAx;
                        for (int k = start; grows && k < z; k++)
                            if (!mask[k * gAx + x + 1])
                                grows = 0;
                        if (grows)
                            continue;
                        int xa = x + 1 - h, xb = x + 1;
                        long tall = (long)(y1 - y0) * AIR_Y / 1024;
                        long sideX = h * tall, sideZ = (z - start) * tall, flat = (long)h * (z - start);
                        long big = sideX > sideZ ? sideX : sideZ;
                        big = flat > big ? flat : big;
                        if (big < 6)
                            continue;
                        if (rockRow(xa, xb, y0 - step, y0, start, z) || rockRow(xa, xb, y1, y1 + step, start, z))
                            continue;
                        candAdd(xa, xb, y0, y1, start, z);
                    }
                    if (height[z] && (!top || stH[top - 1] < height[z]))
                    {
                        stStart[top] = start;
                        stH[top++] = height[z];
                    }
                }
            }
        }
    }
    free(height);
    free(stStart);
    free(stH);
    free(mask);
    fprintf(stderr, "%d boxes of rock\n", gCandN);
}

// Runs of a level's own opaque faces on one plane, all facing one way: a flat rectangle seen from its front
// that hides what's past its plane. On each plane a grid of cells a face wholly covers (1024 along a
// sector's side, 256 up a wall), and the rectangles of it that can't grow. A dry level's rooms only.
#define WALL_CELLS 512
typedef struct { int axis, value, front; } WallPlane;
typedef struct { WallPlane plane; int a0, a1, b0, b1; } WallFace;

static WallFace* gWalls;
static int gWallN;

static int wallCmp(const void* pa, const void* pb)
{
    const WallFace* a = pa, *b = pb;
    if (a->plane.axis != b->plane.axis) return a->plane.axis - b->plane.axis;
    if (a->plane.value != b->plane.value) return a->plane.value - b->plane.value;
    return a->plane.front - b->plane.front;
}

static void wallsCollect(void)
{
    gWalls = malloc(sizeof(WallFace) * (nfaces + 1));
    gWallN = 0;
    for (int i = 0; i < nrooms; i++)
    {
        Room* r = &rooms[i];
        if (r->alt)
            continue;
        for (int q = 0; q < r->nq; q++)
        {
            if (r->q[q].transp)
                continue;
            int idx[4] = { r->q[q].a, r->q[q].b, r->q[q].c, r->q[q].d };
            V3 c[4];
            for (int k = 0; k < 4; k++)
                c[k] = r->v[idx[k]];
            float at[4][3];
            for (int k = 0; k < 4; k++) {
                at[k][0] = c[k].x; at[k][1] = c[k].y; at[k][2] = c[k].z;
            }
            for (int k = 0; k < 3; k++)
            {
                if (at[0][k] != at[1][k] || at[0][k] != at[2][k] || at[0][k] != at[3][k])
                    continue;
                int ua = k == 0 ? 2 : 0, ub = k == 1 ? 2 : 1;   // a: 1024 steps along the ground, b: up a wall, or the ground's other way
                float a0 = 1e30f, a1 = -1e30f;
                for (int n = 0; n < 4; n++) {
                    a0 = fminf(a0, at[n][ua]);
                    a1 = fmaxf(a1, at[n][ua]);
                }
                // the part of b every a covers: the highest of each end's lows, the lowest of its highs
                float lo0 = 1e30f, hi0 = -1e30f, lo1 = 1e30f, hi1 = -1e30f;
                for (int n = 0; n < 4; n++)
                {
                    if (at[n][ua] == a0) { lo0 = fminf(lo0, at[n][ub]); hi0 = fmaxf(hi0, at[n][ub]); }
                    else { lo1 = fminf(lo1, at[n][ub]); hi1 = fmaxf(hi1, at[n][ub]); }
                }
                float b0 = fmaxf(lo0, lo1), b1 = fminf(hi0, hi1);
                if (a1 <= a0 || b1 <= b0)
                    continue;
                // its normal, from the winding: the side it's seen from (faceFacing)
                float e1[3] = { at[1][0] - at[0][0], at[1][1] - at[0][1], at[1][2] - at[0][2] };
                float e2[3] = { at[2][0] - at[0][0], at[2][1] - at[0][1], at[2][2] - at[0][2] };
                float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
                WallFace* w = &gWalls[gWallN++];
                w->plane.axis = k;
                w->plane.value = (int)at[0][k];
                w->plane.front = n[k] < 0 ? 1 : -1;
                w->a0 = (int)a0; w->a1 = (int)a1; w->b0 = (int)b0; w->b1 = (int)b1;
                break;
            }
        }
    }
    qsort(gWalls, gWallN, sizeof(WallFace), wallCmp);
}

static void wallCand(const WallPlane* p, int a0, int a1, int b0, int b1)
{
    if (gCandN == 200000)
        return;
    SolidBox* b = &gCands[gCandN++];
    int ua = p->axis == 0 ? 2 : 0, ub = p->axis == 1 ? 2 : 1;
    b->lo[p->axis] = b->hi[p->axis] = p->value;
    b->lo[ua] = a0; b->hi[ua] = a1;
    b->lo[ub] = b0; b->hi[ub] = b1;
    b->side = p->front > 0 ? p->axis + 1 : -(p->axis + 1);
}

static void wallsFind(void)
{
    wallsCollect();
    int before = gCandN;
    static uint8_t grid[WALL_CELLS][WALL_CELLS];
    static int height[WALL_CELLS + 1], stStart[WALL_CELLS + 2], stH[WALL_CELLS + 2];
    for (int i = 0; i < gWallN; )
    {
        int j = i;
        while (j < gWallN && !wallCmp(&gWalls[i], &gWalls[j]))
            j++;
        const WallPlane* p = &gWalls[i].plane;
        const int stepB = p->axis == 1 ? 1024 : 256;
        int a0 = 1 << 30, a1 = -(1 << 30), b0 = 1 << 30, b1 = -(1 << 30);
        for (int k = i; k < j; k++)
        {
            a0 = gWalls[k].a0 < a0 ? gWalls[k].a0 : a0;
            a1 = gWalls[k].a1 > a1 ? gWalls[k].a1 : a1;
            b0 = gWalls[k].b0 < b0 ? gWalls[k].b0 : b0;
            b1 = gWalls[k].b1 > b1 ? gWalls[k].b1 : b1;
        }
        int na = (a1 - a0) / 1024, nb = (b1 - b0) / stepB;
        if (na > WALL_CELLS || nb > WALL_CELLS || na <= 0 || nb <= 0) { i = j; continue; }
        for (int a = 0; a < na; a++)
            memset(grid[a], 0, nb);
        for (int k = i; k < j; k++)
            for (int a = (gWalls[k].a0 - a0) / 1024; a < (gWalls[k].a1 - a0) / 1024; a++)
                for (int b = (gWalls[k].b0 - b0 + stepB - 1) / stepB; b < (gWalls[k].b1 - b0) / stepB; b++)
                    grid[a][b] = 1;
        memset(height, 0, sizeof(int) * (nb + 1));
        for (int a = 0; a < na; a++)
        {
            for (int b = 0; b < nb; b++)
                height[b] = grid[a][b] ? height[b] + 1 : 0;
            height[nb] = 0;
            int top = 0;
            for (int b = 0; b <= nb; b++)
            {
                int start = b;
                while (top && stH[top - 1] > height[b])
                {
                    int h = stH[top - 1];
                    start = stStart[top - 1];
                    top--;
                    int grows = a + 1 < na;
                    for (int k = start; grows && k < b; k++)
                        if (!grid[a + 1][k])
                            grows = 0;
                    if (grows)
                        continue;
                    long area = (long)h * 1024 * (b - start) * stepB / (1024 * 1024);
                    if (area >= 4)
                        wallCand(p, a0 + (a + 1 - h) * 1024, a0 + (a + 1) * 1024, b0 + start * stepB, b0 + b * stepB);
                }
                if (height[b] && (!top || stH[top - 1] < height[b]))
                {
                    stStart[top] = start;
                    stH[top++] = height[b];
                }
            }
        }
        i = j;
    }
    fprintf(stderr, "%d rectangles of faces\n", gCandN - before);
}

typedef struct { V3 pos; float R[3][3]; int room; float cycles, vdp1; } SearchView;
static SearchView* gViews;
static int gViewN;

static void viewsFind(int want)
{
    gViews = malloc(sizeof(SearchView) * want);
    gViewN = 0;
    long tries = 0;
    while (gViewN < want && tries < 4000000)
    {
        tries++;
        V3 pos;
        int room = randomPlace(&pos);
        viewSet(pos, urand(0, 2 * (float)M_PI), urand(-0.5f, 0.5f));
        gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 256; gBase.useBoxes = 0;
        gBoxFaces = 0;
        walkRun(&gBase, room, 0);
        Work w = listWork(&gBase);
        if (w.verts < SEARCH_VERTS)
            continue;
        SearchView* v = &gViews[gViewN++];
        v->pos = pos;
        memcpy(v->R, R, sizeof(R));
        v->room = room;
        v->cycles = w.cycles;
        v->vdp1 = w.vdp1;
    }
}

static inline void viewUse(const SearchView* v)
{
    cam = v->pos;
    memcpy(R, v->R, sizeof(R));
    viewStamp++;
}

static SolidBox gPicked[MAXBOXES];
static int gPickedN;

// What a view saves with these boxes on, in cycles of room work and of VDP1.
static void viewSaves(const SearchView* v, const SolidBox* list, int count, double* cpu, double* vdp1)
{
    viewUse(v);
    boxesSee(list, count);
    gTry.useMask = 0; gTry.usePoly = 0; gTry.minArea = 256; gTry.useBoxes = 1;
    walkRun(&gTry, v->room, 0);
    gBoxFaces = 1;
    Work w = listWork(&gTry);
    gBoxFaces = 0;
    *cpu = v->cycles - w.cycles;
    *vdp1 = v->vdp1 - w.vdp1;
}

// Pixels a view shows differently with these boxes on.
static long viewLost(const SearchView* v, const SolidBox* list, int count)
{
    viewUse(v);
    gBase.useMask = 0; gBase.usePoly = 0; gBase.minArea = 256; gBase.useBoxes = 0;
    gBoxFaces = 0;
    walkRun(&gBase, v->room, 0);
    oracle(&gBase);
    memcpy(gOwnerBase, owner, sizeof(owner));
    boxesSee(list, count);
    gTry.useMask = 0; gTry.usePoly = 0; gTry.minArea = 256; gTry.useBoxes = 1;
    walkRun(&gTry, v->room, 0);
    gBoxFaces = 1;
    oracle(&gTry);
    gBoxFaces = 0;
    long lost = 0;
    for (int y = 0; y < ZH; y++)
        for (int x = 0; x < ZW; x++)
            if (owner[y][x] != gOwnerBase[y][x])
                lost += 4;
    return lost;
}

static void boxSearch(const char* name, const char* out, int count, int views, const char* hand)
{
    const double MS = 1 / 28636.36;
    airMap();
    candsFind();
    wallsFind();
    viewsFind(views);
    fprintf(stderr, "%d heavy views\n", gViewN);
    gPickedN = 0;
    if (hand)
    {
        boxesLoad(hand);
        for (int i = 0; i < gSolidN; i++)
            gPicked[gPickedN++] = gSolid[i];
    }
    // first pass: each alone, what it saves over the views where it's seen at all
    double* score = calloc(gCandN, sizeof(double));
    SolidBox one[1];
    for (int c = 0; c < gCandN; c++)
    {
        one[0] = gCands[c];
        for (int v = 0; v < gViewN; v++)
        {
            viewUse(&gViews[v]);
            BoxSeen seen;
            if (!boxSee(&one[0], &seen))
                continue;
            double cpu, vdp1;
            viewSaves(&gViews[v], one, 1, &cpu, &vdp1);
            score[c] += cpu + vdp1;
        }
        if (c % 1000 == 0)
            fprintf(stderr, "%d of %d\n", c, gCandN);
    }
    // the best SEARCH_KEEP go on
    int* order = malloc(sizeof(int) * gCandN);
    int n = 0;
    for (int c = 0; c < gCandN; c++)
        if (score[c] > 0)
            order[n++] = c;
    for (int i = 0; i < n && i < SEARCH_KEEP; i++)
    {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (score[order[j]] > score[order[best]])
                best = j;
        int t = order[i]; order[i] = order[best]; order[best] = t;
    }
    n = n < SEARCH_KEEP ? n : SEARCH_KEEP;
    fprintf(stderr, "%d candidates kept\n", n);
    uint8_t* banned = calloc(n, 1);
    double total[2] = { 0, 0 };
    for (int v = 0; v < gViewN; v++)
    {
        total[0] += gViews[v].cycles;
        total[1] += gViews[v].vdp1;
    }
    double base[2] = { 0, 0 };
    for (int v = 0; v < gViewN && gPickedN; v++)
    {
        double cpu, vdp1;
        viewSaves(&gViews[v], gPicked, gPickedN, &cpu, &vdp1);
        base[0] += cpu;
        base[1] += vdp1;
    }
    printf("%s %d heavy views, room work %.2f ms, VDP1 %.2f ms a view (Mednafen cycles, modelled); from the start saved %.2f, %.2f\n", name, gViewN,
           total[0] / gViewN * MS, total[1] / gViewN * MS, base[0] / gViewN * MS, base[1] / gViewN * MS);
    FILE* f = fopen(out, "w");
    for (int i = 0; i < gPickedN; i++)
        fprintf(f, "%.0f %.0f %.0f %.0f %.0f %.0f %d\n", gPicked[i].lo[0], gPicked[i].hi[0], gPicked[i].lo[1], gPicked[i].hi[1], gPicked[i].lo[2], gPicked[i].hi[2], gPicked[i].side);
    for (int round = 0; round < count; round++)
    {
        int best = -1;
        double bestGain = 0, bestCpu = 0, bestVdp1 = 0;
        for (int i = 0; i < n; i++)
        {
            if (banned[i])
                continue;
            gPicked[gPickedN] = gCands[order[i]];
            double a = 0, b = 0;
            for (int v = 0; v < gViewN; v++)
            {
                viewUse(&gViews[v]);
                BoxSeen seen;
                if (!boxSee(&gPicked[gPickedN], &seen))
                {
                    continue;
                }
                double cpu, vdp1, cpu0, vdp10;
                viewSaves(&gViews[v], gPicked, gPickedN + 1, &cpu, &vdp1);
                viewSaves(&gViews[v], gPicked, gPickedN, &cpu0, &vdp10);
                a += cpu - cpu0;
                b += vdp1 - vdp10;
            }
            if (a + b > bestGain)
            {
                bestGain = a + b;
                best = i;
                bestCpu = a;
                bestVdp1 = b;
            }
        }
        if (best < 0)
            break;
        gPicked[gPickedN] = gCands[order[best]];
        long worst = 0, lost = 0;
        int lostViews = 0;
        for (int v = 0; v < gViewN; v++)
        {
            long l = viewLost(&gViews[v], gPicked, gPickedN + 1);
            lost += l;
            lostViews += l > 0;
            worst = l > worst ? l : worst;
        }
        const SolidBox* b = &gPicked[gPickedN];
        if (lost > 64 * gViewN / 100)
        {
            banned[best] = 1;
            printf("  no: x %.0f %.0f y %.0f %.0f z %.0f %.0f loses %ld px over %d views, worst %ld\n", b->lo[0], b->hi[0], b->lo[1], b->hi[1], b->lo[2], b->hi[2], lost, lostViews, worst);
            fflush(stdout);
            round--;
            continue;
        }
        base[0] += bestCpu;
        base[1] += bestVdp1;
        gPickedN++;
        printf("  box %d: x %.0f %.0f y %.0f %.0f z %.0f %.0f side %d: +%.2f ms room work, +%.2f ms VDP1; in all %.2f, %.2f; lost %ld px over %d views, worst %ld\n", round + 1,
               b->lo[0], b->hi[0], b->lo[1], b->hi[1], b->lo[2], b->hi[2], b->side, bestCpu / gViewN * MS, bestVdp1 / gViewN * MS, base[0] / gViewN * MS, base[1] / gViewN * MS, lost, lostViews, worst);
        fflush(stdout);
        fprintf(f, "%.0f %.0f %.0f %.0f %.0f %.0f %d\n", b->lo[0], b->hi[0], b->lo[1], b->hi[1], b->lo[2], b->hi[2], b->side);
        fflush(f);
    }
    fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: occmodel LEVEL.bin VIEWS SEED [PVS_CELL PVS_VIEWS [HOT_GRID HOT_YAWS HOT_VERTS HOT_VIEWS]]\n");
        return 2;
    }
    if (getenv("OCC_FOCAL"))
        F = atof(getenv("OCC_FOCAL"));
    loadLevel(argv[1]);
    facePx = malloc(sizeof(uint16_t) * (nfaces + 1));
    if (!strcmp(argv[2], "region"))
    {
        int k = 3;
        cam.x = atof(argv[k++]); cam.y = atof(argv[k++]); cam.z = atof(argv[k++]);
        int room = atoi(argv[k++]);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                R[i][j] = atof(argv[k++]);
        F = atof(argv[k++]);
        float radius = atof(argv[k++]), arc = atof(argv[k++]) / 57.3f, pitchRange = atof(argv[k++]) / 57.3f;
        region(room, radius, arc, pitchRange, atoi(argv[k++]));
        return 0;
    }
    if (!strcmp(argv[2], "one"))
    {
        int k = 3;
        cam.x = atof(argv[k++]); cam.y = atof(argv[k++]); cam.z = atof(argv[k++]);
        int room = atoi(argv[k++]);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                R[i][j] = atof(argv[k++]);
        F = atof(argv[k++]);
        viewStamp++;
        gBase.useMask = 0; gBase.usePoly = 0;
        gBase.minArea = getenv("OCC_TINY") ? atof(getenv("OCC_TINY")) : 0;
        for (int i = 0; i < MAXROOMS; i++) {
            gNearest[i] = 1e30f;
            gFrom[i] = -1;
        }
        walkRun(&gBase, room, 0);
        Work w = listWork(&gBase);
        oracle(&gBase);
        if (getenv("OCC_WINDOWS"))
        {
            FILE* f = fopen(getenv("OCC_WINDOWS"), "w");
            for (int i = 0; i < gBase.nlist; i++)
            {
                int t = gBase.list[i];
                Rect c = gBase.clip[t];
                fprintf(f, "%d %d %.0f %.0f %.0f %.0f %.0f\n", t, gFrom[t], gNearest[t], c.x0, c.y0, c.x1, c.y1);
            }
            fclose(f);
        }
        // the view's flat faces by plane (OCC_PLANES names the file): up or down, height, see-through or
        // not, faces kept, pixels drawn, pixels shown; first a line of every kept face's
        if (getenv("OCC_PLANES"))
        {
            FILE* f = fopen(getenv("OCC_PLANES"), "w");
            double allFaces = 0, allPx = 0, allShown = 0;
            for (int i = 0; i < gBase.nlist; i++)
            {
                int t = gBase.list[i];
                Room* r = &rooms[t];
                Rect c = gBase.clip[t];
                for (int q = 0; q < r->nq; q++)
                {
                    int idx[4] = { r->q[q].a, r->q[q].b, r->q[q].c, r->q[q].d };
                    if (!faceFacing(r, idx) || faceCulled(r, idx, 4, c))
                        continue;
                    double area = faceArea(r, idx, 4), shown = facePx[r->faceBase + q];
                    allFaces++;
                    allPx += area;
                    allShown += shown;
                    float y = r->v[idx[0]].y;
                    if (r->v[idx[1]].y != y || r->v[idx[2]].y != y || r->v[idx[3]].y != y)
                        continue;
                    fprintf(f, "%c %.0f %d %.0f %.0f %d\n", cam.y < y ? 'F' : 'C', y, r->q[q].transp, area, shown, t);
                }
            }
            fprintf(f, "A 0 0 %.0f %.0f %.0f\n", allPx, allShown, allFaces);
            fclose(f);
        }
        // each listed room's work (OCC_ROOMS names the file): room, vertices, those its kept faces
        // use, faces kept, dropped by the window, facing away, pixels shown
        if (getenv("OCC_ROOMS"))
        {
            FILE* f = fopen(getenv("OCC_ROOMS"), "w");
            for (int i = 0; i < gBase.nlist; i++)
            {
                int t = gBase.list[i];
                Room* r = &rooms[t];
                Rect c = gBase.clip[t];
                static uint8_t used[65536];
                memset(used, 0, r->nv + 1);
                int kept = 0, dropped = 0, away = 0, verts = 0;
                for (int q = 0; q < r->nq + r->nt; q++)
                {
                    int quad = q < r->nq, n = quad ? 4 : 3;
                    int idx[4];
                    if (quad) {
                        idx[0] = r->q[q].a; idx[1] = r->q[q].b; idx[2] = r->q[q].c; idx[3] = r->q[q].d;
                    } else {
                        memcpy(idx, r->t[q - r->nq], 12);
                    }
                    if (!faceFacing(r, idx)) {
                        away++;
                    } else if (faceCulled(r, idx, n, c)) {
                        dropped++;
                    } else {
                        kept++;
                        for (int j = 0; j < n; j++)
                            used[idx[j]] = 1;
                    }
                }
                for (int k = 0; k < r->nv; k++)
                    verts += used[k];
                fprintf(f, "%d %d %d %d %d %d %ld\n", t, r->nv, verts, kept, dropped, away, ownerPx[t]);
            }
            fclose(f);
        }
        // each half-resolution pixel's room, for a picture (OCC_OWNERS names the file)
        if (getenv("OCC_OWNERS"))
        {
            FILE* f = fopen(getenv("OCC_OWNERS"), "wb");
            for (int y = 0; y < ZH; y++)
                for (int x = 0; x < ZW; x++) {
                    int16_t room16 = owner[y][x] >= 0 ? faceRoom(owner[y][x]) : -1;
                    fwrite(&room16, 2, 1, f);
                }
            fclose(f);
        }
        if (getenv("OCC_FACEPX"))
        {
            FILE* f = fopen(getenv("OCC_FACEPX"), "wb");
            fwrite(facePx, sizeof(uint16_t), nfaces, f);
            fclose(f);
        }
        printf("rooms %d verts %.0f kept %.0f dropped %.0f px %.0f list:", gBase.nlist, w.verts, w.kept, w.dropped, w.px);
        for (int i = 0; i < gBase.nlist; i++)
            printf(" %d(%ld)", gBase.list[i], ownerPx[gBase.list[i]]);
        printf("\n");
        return 0;
    }
    if (!strcmp(argv[2], "holes"))
    {
        rngState = (uint32_t)atoi(argv[4]) * 2654435761u + 12345u;
        const char* name = strrchr(argv[1], '/');
        holes(name ? name + 1 : argv[1], atoi(argv[3]));
        return 0;
    }
    if (!strcmp(argv[2], "walkstat"))
    {
        gPlaceInner = 1;
        rngState = (uint32_t)atoi(argv[4]) * 2654435761u + 12345u;
        const char* name = strrchr(argv[1], '/');
        walkStat(name ? name + 1 : argv[1], atoi(argv[3]));
        return 0;
    }
    if (!strcmp(argv[2], "boxes"))
    {
        boxesLoad(argv[3]);
        gPlaceInner = 1;
        rngState = (uint32_t)atoi(argv[5]) * 2654435761u + 12345u;
        const char* name = strrchr(argv[1], '/');
        boxes(name ? name + 1 : argv[1], atoi(argv[4]));
        return 0;
    }
    if (!strcmp(argv[2], "boxsearch"))
    {
        gPlaceInner = 1;
        rngState = (uint32_t)atoi(argv[6]) * 2654435761u + 12345u;
        const char* name = strrchr(argv[1], '/');
        boxSearch(name ? name + 1 : argv[1], argv[3], atoi(argv[4]), atoi(argv[5]), argc > 7 ? argv[7] : NULL);
        return 0;
    }
    if (!strcmp(argv[2], "cameras"))
    {
        rngState = (uint32_t)atoi(argv[4]) * 2654435761u + 12345u;
        const char* name = strrchr(argv[1], '/');
        cameras(name ? name + 1 : argv[1], atoi(argv[3]));
        return 0;
    }
    int views = atoi(argv[2]);
    rngState = (uint32_t)atoi(argv[3]) * 2654435761u + 12345u;
    const char* name = strrchr(argv[1], '/');
    name = name ? name + 1 : argv[1];
    if (argc > 5 && atoi(argv[4])) {
        pvsCell = atoi(argv[4]);
        pvsViews = atoi(argv[5]);
        pvsTrain();
    }
    if (argc > 9) {
        hotG = atoi(argv[6]);
        hotY = atoi(argv[7]);
        hotT = atoi(argv[8]);
        hotD = atoi(argv[9]);
        hotTrain();
    }
    run(name, views);
    return 0;
}
