// What room culling could save, before any of it is built: random views about a level, the
// game's portal walk (renderer/rooms.s) as the baseline, a true depth buffer as the oracle of
// which listed rooms show a pixel, and the tests against both:
//   poly   windows as the portals' true shapes, clipped portal by portal, not rectangles
//   mask   big solid faces baked per room (export.py) drawn into a coarse screen mask as the
//          walk enters their rooms; a portal wholly behind what's there isn't entered
//   pvs    the rooms seen from a camera's cell over training views, baked; held-out views test it
//   tiny   a portal showing fewer than so many pixels of window isn't entered, its window filled
//   hot    pvs for only the camera cells and headings whose views are heavy, trained hard
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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 352
#define H 224
#define NEAR 64.0f
#define VIEW_MAX 20480.0f
#define MAXROOMS 256
#define CELLW 44
#define CELLH 28
#define CELL 8
#define ZW (W / 2)
#define ZH (H / 2)

static float F = 256.0f;      // the game's: x 256 / z

typedef struct { float x, y, z; } V3;
typedef struct { int target; float n[3]; V3 c[4]; } Portal;
typedef struct { V3 c[4]; float n[3]; } Occ;
typedef struct { int a, b, c, d, transp; } Quad;
typedef struct {
    int x, z, ytop, ybot, xs, zs, alt, nv, nq, nt, np, ns, nocc, nstatic;
    V3* v;
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
        r->vv = malloc(sizeof(V3) * (r->nv + 1));
        r->sx = malloc(4 * (r->nv + 1));
        r->sy = malloc(4 * (r->nv + 1));
        for (int k = 0; k < r->nv; k++) {
            int c[3];
            fread(c, 4, 3, f);
            r->v[k] = (V3){ c[0], c[1], c[2] };
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

// --- the walks ---------------------------------------------------------------------------
typedef struct {
    int useMask, occK, usePoly;
    float minArea;      // a window smaller is filled, its room not entered
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
static int checkRect(Walk* w, int room, const Portal* p, Rect* out)
{
    w->checks++;
    if (!portalFacing(&rooms[room], p))
        return 0;
    V3 pv[4];
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, zmin = 1e30f;
    int near = 0, far = 0;
    for (int i = 0; i < 4; i++)
    {
        V3 v = toView(p->c[i]);
        pv[i] = v;
        if (v.z <= 0) { near++; continue; }
        if (v.z > VIEW_MAX) far++;
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
    if (w->useMask && !near && maskHides(win, zmin))
        return 0;
    if (w->minArea > 0 && !near && (x1 - x0) * (y1 - y0) < w->minArea) {
        w->fills += 1;
        w->fillPx += (x1 - x0) * (y1 - y0);
        return 0;
    }
    int t = p->target;
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
    int l = 1, rr = 1, t = 1, b = 1, nz = 1, fz = 1;
    for (int i = 0; i < n; i++)
    {
        float x = r->sx[idx[i]], y = r->sy[idx[i]], z = r->vv[idx[i]].z;
        if (x >= w.x0) l = 0;
        if (x <= w.x1) rr = 0;
        if (y >= w.y0) t = 0;
        if (y <= w.y1) b = 0;
        if (z >= NEAR) nz = 0;
        if (z <= VIEW_MAX) fz = 0;
    }
    return l || rr || t || b || nz || fz;
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
        kept++;
        px += faceArea(r, idx, 4);
    }
    for (int i = 0; i < r->nt; i++)
    {
        int* idx = r->t[i];
        if (!faceFacing(r, idx))
            continue;
        if (faceCulled(r, idx, 3, w) || faceOut(r->faceBase + r->nq + i)) { dropped++; continue; }
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
            if (faceFacing(r, idx) && !faceCulled(r, idx, 4, c))
                fillFace(r, idx, 4, r->faceBase + i);
        }
        for (int i = 0; i < r->nt; i++)
        {
            int* idx = r->t[i];
            if (faceFacing(r, idx) && !faceCulled(r, idx, 3, c))
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

// a place in a room's air
static int placeIn(int room, int sector, V3* pos)
{
    Room* r = &rooms[room];
    int floor = r->s[sector][0], ceil = r->s[sector][1];
    if (floor <= ceil + 200 || floor == -127 * 256 || ceil == -127 * 256)
        return 0;
    int sx = sector / r->zs, sz = sector % r->zs;
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
enum { T_BASE, T_ORACLE, T_ORACLE_F, T_POLY, T_M8, T_M32, T_M32P1, T_W16, T_W32, T_W64, T_W128, T_W256, T_W512, T_W1024, T_PVS, T_PVS8, T_PVSF, T_PVSF8, T_HOT, T_NEAR64, T_NEAR256, T_NEAR1024, T_NEARR, T_COUNT };
static const char* const kNames[T_COUNT] = { "base", "oracle", "oracle-faces", "poly", "mask8", "mask32", "mask32+1", "tiny16", "tiny32", "tiny64", "tiny128", "tiny256", "tiny512", "tiny1024", "pvs", "pvs8", "pvs-faces", "pvs8-faces", "hot", "near64", "near256", "near1024", "near-rooms" };

typedef struct { double rooms, verts, statics, kept, px, cycles, vdp1, test, lost, hit, over; } Sum;
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
            double n = 0, lostViews = 0, lost16 = 0, worst = 0;
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
                lost16 += s->lost >= 64;
                if (s->lost > worst) worst = s->lost;
                n++;
            }
            if (n == 0)
                continue;
            printf("%s %s %s views %.0f rooms %.2f verts %.1f statics %.1f kept %.1f px %.0f cycles %.0f vdp1 %.0f test %.0f lost %.2f lostviews %.0f lost64 %.0f worst %.0f hit %.0f over %.0f\n",
                   name, kBands[band], kNames[t], n, a.rooms / n, a.verts / n, a.statics / n, a.kept / n, a.px / n,
                   a.cycles / n, a.vdp1 / n, a.test / n, a.lost / n, lostViews, lost16, worst, a.hit, a.over);
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
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: occmodel LEVEL.bin VIEWS SEED [PVS_CELL PVS_VIEWS [HOT_GRID HOT_YAWS HOT_VERTS HOT_VIEWS]]\n");
        return 2;
    }
    loadLevel(argv[1]);
    facePx = malloc(sizeof(uint16_t) * (nfaces + 1));
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
        walkRun(&gBase, room, 0);
        Work w = listWork(&gBase);
        oracle(&gBase);
        printf("rooms %d verts %.0f kept %.0f dropped %.0f px %.0f list:", gBase.nlist, w.verts, w.kept, w.dropped, w.px);
        for (int i = 0; i < gBase.nlist; i++)
            printf(" %d(%ld)", gBase.list[i], ownerPx[gBase.list[i]]);
        printf("\n");
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
