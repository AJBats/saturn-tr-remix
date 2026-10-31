// Page 5, the game's meshes: the master's meshes lit by normals in a frame at
// the game's ledge (Lara, a door, two wolves and twelve bridge planks: 80
// meshes, 1210 vertices, in the order the master drew them in a Mednafen
// trace of main at a90e0f1), made up to those sizes and wholly on the frame.
// Their lights are TR1's as the game had them in the wolves' fight (ambient
// about 5400, range about 1350), so that no vertex's shade clamps.
// The game's own transform (lab/gamemeshxform.s transformMeshAsm, its shaded
// fast path) against meshlight2.dsp and meshlight3.dsp (gen_meshlight3.py)
// rotating and lighting them and the master projecting what comes out
// (meshproject.s); then as a frame's work: each mesh's setup, its vertices
// and a stand-in for its faces (kernels.s kFaces), with the DSP's jobs
// written and taken as the game would, the master projecting and facing the
// meshes the DSP has done while it works on the next. The master's work and
// its waits for the DSP apart.
// Page 6 times each part of the master's work in those frames on its own.

#include "bench.h"
#include "dsp.h"
#include "meshlight2_dsp.h"
#include "meshlight3_dsp.h"

// saturn/render.h's, as meshxform.s and the DSP programs read them
struct MeshVertex
{
    int16_t x, y, z;
};

struct Vertex
{
    uint32_t xy;    // int16 x, y
    uint32_t zg;    // int16 z, uint8 g, clip
};

struct MeshXform
{
    int16_t rows[9];
    int16_t pad;
    int32_t tx, ty, tz;     // << 14
    const uint16_t* divTable;
    int32_t ambient;
    int32_t lx, ly, lz;
    int32_t range;
    const int8_t* normals;
    const uint8_t* shades;
    int32_t g;
    int32_t inside;
};

// A job of meshlight2.dsp's list: size its vertices; 0 ends the list, and is
// written last
struct MeshLightJob
{
    uint32_t size;
    uint32_t normals, src, dst;     // DSP addresses
    MeshXform xf;
};

// A job of meshlight3.dsp's list: its coefficients ready to use; groups its
// groups of 4 vertices - 1 (-1 ends the list), written last
struct MeshJob3
{
    uint32_t src;                   // its DSP-ready vertices / 4
    int32_t groups;
    int32_t k[16];                  // 4t 4m 4m 4m a row, ambient << 16 | 0xFFFF and l * range * -4
};

static_assert(sizeof(MeshXform) == 72 && sizeof(MeshLightJob) == 22 * 4 && sizeof(MeshJob3) == 18 * 4,
              "as the game's, and as the DSP programs read them");

extern "C" void transformMeshAsm(const MeshVertex* src, int32_t count, Vertex* dst, MeshXform* xf);
extern "C" void meshProjectAsm(Vertex* v, int32_t count, const uint16_t* divTable);
extern "C" void kFaces(void* p, uint32_t n);

// What lab/gamemeshxform.s takes from the game for its renderMeshAsm, which
// isn't run here.
int32_t gBenchMode;
uint32_t gVertices[2];
uint16_t divTable[1281];
extern "C" void faceAddMeshQuadsBody() {}
extern "C" void faceAddMeshTrianglesBody() {}

// The ledge's meshes: vertices and faces, an object's in the order drawn.
static const uint8_t kLara[][2] = {
    {20, 24}, {34, 34}, {23, 29}, {16, 18}, {34, 34}, {23, 29}, {16, 18}, {46, 40},
    {16, 23}, {13, 16}, {8, 6}, {16, 23}, {13, 16}, {8, 6}, {46, 85}
};
static const uint8_t kDoor[][2] = { {38, 24} };
static const uint8_t kWolf[][2] = {
    {11, 16}, {16, 19}, {10, 9}, {27, 35}, {6, 5}, {6, 5}, {8, 6}, {14, 15}, {8, 6},
    {8, 6}, {8, 6}, {14, 15}, {8, 6}, {8, 6}, {8, 6}, {12, 14}, {8, 6}, {12, 10},
    {8, 6}, {12, 10}, {8, 6}, {12, 10}, {8, 6}, {8, 6}, {8, 6}, {8, 6}
};
static const uint8_t kPlank[][2] = { {26, 16} };

#define MESHES      80
#define OBJECTS     16
#define SENTINEL    0xFFFFFFFF  // in a job's last output longword until the DSP's write (clip byte 0)

struct Mesh
{
    int32_t count, faces;
    MeshVertex* src;
    int8_t* normals;
    Vertex* out;                    // whole batches, on cache lines, mesh after mesh
    MeshXform xf;
    uint32_t dspSrc;
    volatile uint32_t* last;        // meshlight2.dsp's sentinel: its last vertex's zg, uncached
    int32_t groups;                 // meshlight3.dsp's: of 4 vertices
    uint32_t dsp3;                  // its vertices laid out for it / 4
    Vertex* out3;                   // whole groups, mesh after mesh
    volatile uint32_t* last3;
};

static Mesh gMeshes[MESHES];
static int32_t gVertexCount;
static MeshLightJob gJobs[MESHES + 1];
static uint8_t gData[48 * 1024] __attribute__((aligned(16)));
static Vertex gOut[174 * 8] __attribute__((aligned(16)));  // the outputs, whole batches
// meshlight3.dsp's: the vertices x y z nx ny nz a longword, groups of 4
static int32_t gDsp3[320 * 4 * 6] __attribute__((aligned(16)));
static Vertex gOut3[320 * 4] __attribute__((aligned(16)));
static MeshJob3 gJobs3[MESHES + 1];
static Vertex gRef[1280];               // transformMeshAsm's, mesh after mesh
static uint32_t gFaceStores[1280 * 3];  // kFaces'
static uint32_t gScratch[16] __attribute__((aligned(16)));  // meshlight2.dsp's first output, of nothing

// The groups the DSP is fed in: each ends before the index given.
static int32_t gMeshEnds[MESHES];
static int32_t gObjectEnds[OBJECTS];
static const int32_t kFrameEnd[1] = { MESHES };

static uint32_t batches(int32_t count)
{
    return (count + 7) / 8;
}

static void* take(uint8_t*& p, uint32_t bytes)
{
    void* at = p;
    p += (bytes + 15) & ~15;
    return at;
}

static void add(uint8_t*& p, Vertex*& out, int32_t*& dsp3, Vertex*& out3, uint32_t& seed,
                const uint8_t (*list)[2], int32_t n, int32_t& m)
{
    static const int16_t rows[4][9] = {
        { 14189, 0, -8192, 4096, 15851, 7094, 7094, -4096, 12288 },
        { 14189, 4096, 7094, 0, 15851, -4096, -8192, 7094, 12288 },
        { 16384, 0, 0, 0, 14189, -8192, 0, 8192, 14189 },
        { 11585, 0, 11585, 0, 16384, 0, -11585, 0, 11585 }
    };
    for (int32_t i = 0; i < n; i++, m++)
    {
        Mesh& mesh = gMeshes[m];
        mesh.count = list[i][0];
        mesh.faces = list[i][1];
        uint32_t whole = batches(mesh.count) * 8;
        mesh.src = (MeshVertex*)take(p, whole * sizeof(MeshVertex));
        mesh.normals = (int8_t*)take(p, whole * 4);
        mesh.out = out;
        out += whole;
        for (uint32_t v = 0; v < whole; v++)
        {
            int16_t* c = &mesh.src[v].x;
            for (int32_t k = 0; k < 3; k++)
            {
                seed = seed * 1664525 + 1013904223;
                c[k] = int16_t(((seed >> 16) & 511) - 256);
            }
            for (int32_t k = 0; k < 4; k++)
            {
                seed = seed * 1664525 + 1013904223;
                mesh.normals[v * 4 + k] = int8_t(k == 3 ? seed >> 24 : ((seed >> 16) % 147) - 73);
            }
        }
        MeshXform& x = mesh.xf;
        for (int32_t k = 0; k < 9; k++) {
            x.rows[k] = rows[m & 3][k];
        }
        x.pad = 0;
        seed = seed * 1664525 + 1013904223;
        x.tx = (int32_t((seed >> 8) % 601) - 300) << 14;
        x.ty = (int32_t((seed >> 18) % 401) - 200) << 14;
        x.tz = (2500 + int32_t((seed >> 4) % 6500)) << 14;
        x.divTable = divTable;
        x.ambient = 5000 + int32_t((seed >> 12) % 800);
        x.lx = int32_t((seed >> 20) % 147) - 73;
        x.ly = int32_t((seed >> 12) % 147) - 73;
        x.lz = int32_t((seed >> 4) % 147) - 73;
        x.range = 1250 + int32_t((seed >> 2) % 200);
        x.normals = mesh.normals;
        x.shades = nullptr;
        x.g = 0;
        x.inside = 1;
        mesh.dspSrc = dspAddress(mesh.src);
        mesh.last = uncached(&mesh.out[mesh.count - 1].zg);
        mesh.groups = (mesh.count + 3) / 4;
        mesh.dsp3 = dspAddress(dsp3);
        for (int32_t v = 0; v < mesh.groups * 4; v++)
        {
            for (int32_t k = 0; k < 3; k++)
            {
                dsp3[k] = v < mesh.count ? (&mesh.src[v].x)[k] : 0;
                dsp3[3 + k] = v < mesh.count ? mesh.normals[v * 4 + k] : 0;
            }
            dsp3 += 6;
        }
        mesh.out3 = out3;
        out3 += mesh.groups * 4;
        mesh.last3 = uncached(&mesh.out3[mesh.count - 1].zg);
        gVertexCount += mesh.count;
        gMeshEnds[m] = m + 1;
    }
}

static void build()
{
    for (int32_t i = 1; i < 1281; i++) {
        divTable[i] = uint16_t((1 << 16) / i);
    }
    divTable[0] = 0xFFFF;
    uint8_t* p = gData;
    Vertex* out = gOut;
    int32_t* dsp3 = gDsp3;
    Vertex* out3 = gOut3;
    uint32_t seed = 0x2B7E1516;
    int32_t m = 0, o = 0;
    add(p, out, dsp3, out3, seed, kLara, 15, m);
    gObjectEnds[o++] = m;
    add(p, out, dsp3, out3, seed, kDoor, 1, m);
    gObjectEnds[o++] = m;
    add(p, out, dsp3, out3, seed, kWolf, 26, m);
    gObjectEnds[o++] = m;
    add(p, out, dsp3, out3, seed, kWolf, 26, m);
    gObjectEnds[o++] = m;
    for (int32_t i = 0; i < 12; i++)
    {
        add(p, out, dsp3, out3, seed, kPlank, 1, m);
        gObjectEnds[o++] = m;
    }
    // the list's end names vertices too: meshlight3.dsp fetches a group past
    // a job's last before it knows the list has ended
    for (MeshJob3& job : gJobs3) {
        job.src = gMeshes[0].dsp3;
    }
}

// The setup's stores: a MeshXform written whole, as the game's setup does.
__attribute__((noinline)) static void writeXform(MeshXform* dst, const MeshXform* src)
{
    const uint32_t* s = (const uint32_t*)src;
    volatile uint32_t* d = (volatile uint32_t*)dst;
    for (int32_t i = 0; i < 18; i++) {
        d[i] = s[i];
    }
}

// Drops this CPU's copies of the lines a mesh's output lies on.
static void dropLines(const void* p, uint32_t bytes)
{
    for (uint32_t a = uintptr_t(p) & ~15; a < uintptr_t(p) + bytes; a += 16) {
        *(volatile uint32_t*)(a | 0x40000000) = 0;
    }
}

// The same vertices as the game's: each mesh's output, once projected.
static bool exact(const Vertex* out, int32_t count, const Vertex* ref)
{
    for (int32_t v = 0; v < count; v++)
    {
        if (out[v].xy != ref[v].xy || out[v].zg != ref[v].zg)
            return false;
    }
    return true;
}

static bool allExact(bool v3 = false)
{
    bool ok = true;
    const Vertex* ref = gRef;
    for (const Mesh& mesh : gMeshes)
    {
        ok = ok && exact(v3 ? mesh.out3 : mesh.out, mesh.count, ref);
        ref += mesh.count;
    }
    return ok;
}

// The vertices on the SH-2, the game's transform.
static uint32_t sh2Vertices()
{
    Vertex* ref = gRef;
    uint32_t t0 = cycles();
    for (Mesh& mesh : gMeshes)
    {
        transformMeshAsm(mesh.src, mesh.count, ref, &mesh.xf);
        ref += mesh.count;
        cycles();
    }
    return cycles() - t0;
}

// The DSP's output projected by the master.
static uint32_t projectAlone(bool v3 = false)
{
    cachePurge();
    uint32_t t0 = cycles();
    for (Mesh& mesh : gMeshes)
    {
        meshProjectAsm(v3 ? mesh.out3 : mesh.out, mesh.count, divTable);
        cycles();
    }
    return cycles() - t0;
}

// A frame's meshes on the SH-2: setup, vertices, faces.
static uint32_t frameSh2()
{
    static MeshXform xf;
    Vertex* ref = gRef;
    uint32_t* faces = gFaceStores;
    uint32_t t0 = cycles();
    for (Mesh& mesh : gMeshes)
    {
        writeXform(&xf, &mesh.xf);
        transformMeshAsm(mesh.src, mesh.count, ref, &xf);
        kFaces(faces, mesh.faces);
        ref += mesh.count;
        faces += mesh.faces * 3;
        cycles();
    }
    return cycles() - t0;
}

// The master's frame with the DSP: its time, and of it the time it waited
// for the DSP's output.
struct Frame
{
    uint32_t cycles, waiting;
};

// --- meshlight2.dsp ------------------------------------------------------

static void meshLight2Load(const uint32_t* program, int32_t count)
{
    static const uint32_t constants[][2] = {
        { 4, 0xFFFF0000 }, { 9, 0xFFFF }, { 14, 0xFFFF0000 }, { 19, 0xFFFFFFFC },
        { 21, 0xFFFFE000 }, { 22, 0x1F00 }, { 37, 65536 }, { 38, 256 },
        { 39, 0xFF000000 }, { 40, 0xFFFF }, { 41, 0x8000 }, { 42, 4 }
    };
    dspLoad(program, count);
    for (const auto& c : constants) {
        dspWrite(0, c[0], &c[1], 1);
    }
    const uint32_t scratch = dspAddress(gScratch);
    dspWrite(0, 51, &scratch, 1);
}

static void start2(int32_t i)
{
    const uint32_t job = dspAddress(gJobs + i);
    const uint32_t out = dspAddress(gMeshes[i].out);
    dspWrite(0, 46, &job, 1);
    dspWrite(0, 50, &out, 1);
    dspStart(MESHLIGHT2_DSP_START);
}

// Job i written for mesh i, the slot after it ending the list, published:
// its setup, its vertices' address, its size (meshlight2.dsp takes the
// normals from the setup and puts the outputs one after another).
static void queue2(int32_t i)
{
    const Mesh& mesh = gMeshes[i];
    MeshLightJob& job = gJobs[i];
    *mesh.last = SENTINEL;
    writeXform(&job.xf, &mesh.xf);
    job.src = mesh.dspSrc;
    gJobs[i + 1].size = 0;
    asm volatile("" ::: "memory");
    *(volatile uint32_t*)&job.size = mesh.count;
}

// The DSP started again at the first job it hasn't done, if it has stopped
// short of the jobs published (it ends at the first job of 0 it reads).
static void keepRunning(int32_t first, int32_t queued)
{
    if (dspBusy())
        return;
    while (first < queued && *gMeshes[first].last != SENTINEL) {
        first++;
    }
    if (first < queued) {
        start2(first);
    }
}

// Whether mesh m is done, reading as few sentinels as can be: the DSP does
// its jobs in order, so a mesh a few on done means every mesh before it is.
// done: every mesh before it is known done.
static bool done2(int32_t m, int32_t queued, int32_t& done)
{
    if (m < done)
        return true;
    int32_t far = m + 8 < queued ? m + 8 : queued - 1;
    if (far > m && *gMeshes[far].last != SENTINEL)
    {
        done = far + 1;
        return true;
    }
    if (*gMeshes[m].last != SENTINEL)
    {
        done = m + 1;
        return true;
    }
    return false;
}

// Every mesh's job, then the DSP's run with the master off the bus.
static uint32_t dspAlone2()
{
    for (int32_t i = 0; i < MESHES; i++) {
        queue2(i);
    }
    uint32_t t0 = cycles();
    start2(0);
    dspWaitQuietly();
    return cycles() - t0;
}

// A frame's meshes with meshlight2.dsp, fed a group of jobs at a time (each
// group ends before ends[g]): the next group written and published, the DSP
// kept running from the group's first job on, then this group's meshes
// projected and faced as the DSP finishes them. The DSP's output reaches the
// master through its cache: either each mesh's lines dropped before it is
// read, or the whole cache purged once before the first is (none is read
// before the DSP has done it, nor twice).
static Frame frameDsp2(const int32_t* ends, int32_t groups, bool dropEach)
{
    Frame f = { 0, 0 };
    uint32_t* faces = gFaceStores;
    int32_t queued = 0, m = 0, done = 0;
    gJobs[0].size = 0;
    uint32_t t0 = cycles();
    for (int32_t g = -1; g < groups; g++)
    {
        if (g + 1 < groups)
        {
            queue2(queued++);
            keepRunning(m, queued);
            if (queued < ends[g + 1])
            {
                while (queued < ends[g + 1]) {
                    queue2(queued++);
                }
                keepRunning(m, queued);
            }
        }
        if (g == 0 && !dropEach) {
            cachePurge();
        }
        for (; g >= 0 && m < ends[g]; m++)
        {
            Mesh& mesh = gMeshes[m];
            if (!done2(m, queued, done))
            {
                uint32_t w0 = cycles();
                while (!done2(m, queued, done))
                {
                    keepRunning(m, queued);
                    kRegisters(16);
                }
                f.waiting += cycles() - w0;
            }
            if (dropEach) {
                dropLines(mesh.out, mesh.count * sizeof(Vertex));
            }
            meshProjectAsm(mesh.out, mesh.count, divTable);
            kFaces(faces, mesh.faces);
            faces += mesh.faces * 3;
            cycles();
        }
    }
    f.cycles = cycles() - t0;
    dspWaitQuietly();
    return f;
}

// --- meshlight3.dsp ------------------------------------------------------

static void meshLight3Load()
{
    static const uint32_t md0[3] = { 65536, 0xFFFF, 0x1F00 };
    static const uint32_t one = 1;
    dspLoad(meshlight3_dsp, MESHLIGHT3_DSP_COUNT);
    dspWrite(0, 16, md0, 3);
    dspWrite(3, 16, &one, 1);
}

// The DSP started on the list from job i, its outputs from mesh i's.
static void start3(const MeshJob3* job, const Vertex* out)
{
    const uint32_t first = dspAddress(job);
    const uint32_t md0[2] = { first + 18, 20 };     // the record after; where the first is staged
    const uint32_t ends[2] = { dspAddress(out), first };
    dspWrite(0, 20, md0, 2);
    dspWrite(0, 23, ends, 2);
    dspStart(MESHLIGHT3_DSP_START);
}

// The setup meshlight3.dsp takes, from the one the game's setup makes: its
// rows and translations times 4, the light times its range and -4, the
// ambient where t's shift and rounding come out exact.
__attribute__((noinline)) static void writeJob3(MeshJob3* job, const MeshXform& x)
{
    volatile int32_t* k = job->k;
    for (int32_t r = 0; r < 3; r++)
    {
        k[r * 4] = (&x.tx)[r] * 4;
        k[r * 4 + 1] = x.rows[r * 3] * 4;
        k[r * 4 + 2] = x.rows[r * 3 + 1] * 4;
        k[r * 4 + 3] = x.rows[r * 3 + 2] * 4;
    }
    int32_t range = x.range * -4;
    k[12] = int32_t(uint32_t(x.ambient) << 16 | 0xFFFF);
    k[13] = x.lx * range;
    k[14] = x.ly * range;
    k[15] = x.lz * range;
}

// Job i written for mesh i, the slot after it ending the list, published.
static void queue3(int32_t i)
{
    const Mesh& mesh = gMeshes[i];
    MeshJob3& job = gJobs3[i];
    *mesh.last3 = SENTINEL;
    writeJob3(&job, mesh.xf);
    job.src = mesh.dsp3;
    gJobs3[i + 1].groups = -1;
    asm volatile("" ::: "memory");
    *(volatile int32_t*)&job.groups = mesh.groups - 1;
}

static void keepRunning3(int32_t first, int32_t queued)
{
    if (dspBusy())
        return;
    while (first < queued && *gMeshes[first].last3 != SENTINEL) {
        first++;
    }
    if (first < queued) {
        start3(gJobs3 + first, gMeshes[first].out3);
    }
}

static bool done3(int32_t m, int32_t queued, int32_t& done)
{
    if (m < done)
        return true;
    int32_t far = m + 8 < queued ? m + 8 : queued - 1;
    if (far > m && *gMeshes[far].last3 != SENTINEL)
    {
        done = far + 1;
        return true;
    }
    if (*gMeshes[m].last3 != SENTINEL)
    {
        done = m + 1;
        return true;
    }
    return false;
}

static uint32_t dspAlone3()
{
    for (int32_t i = 0; i < MESHES; i++) {
        queue3(i);
    }
    uint32_t t0 = cycles();
    start3(gJobs3, gOut3);
    dspWaitQuietly();
    return cycles() - t0;
}

// The program's own pace: the whole of page 5's vertices as one job of
// mesh 0's setup, no job's start or part group among them.
static uint32_t dspLong3()
{
    static MeshJob3 jobs[2];
    writeJob3(&jobs[0], gMeshes[0].xf);
    jobs[0].src = gMeshes[0].dsp3;
    jobs[0].groups = (gMeshes[MESHES - 1].out3 - gOut3) / 4 + gMeshes[MESHES - 1].groups - 1;
    jobs[1].src = gMeshes[0].dsp3;
    jobs[1].groups = -1;
    uint32_t t0 = cycles();
    start3(jobs, gOut3);
    dspWaitQuietly();
    return cycles() - t0;
}

// A frame's meshes with meshlight3.dsp, as frameDsp2 with meshlight2.dsp.
static Frame frameDsp3(const int32_t* ends, int32_t groups)
{
    Frame f = { 0, 0 };
    uint32_t* faces = gFaceStores;
    int32_t queued = 0, m = 0, done = 0;
    gJobs3[0].groups = -1;
    uint32_t t0 = cycles();
    for (int32_t g = -1; g < groups; g++)
    {
        if (g + 1 < groups)
        {
            queue3(queued++);
            keepRunning3(m, queued);
            if (queued < ends[g + 1])
            {
                while (queued < ends[g + 1]) {
                    queue3(queued++);
                }
                keepRunning3(m, queued);
            }
        }
        if (g == 0) {
            cachePurge();
        }
        for (; g >= 0 && m < ends[g]; m++)
        {
            Mesh& mesh = gMeshes[m];
            if (!done3(m, queued, done))
            {
                uint32_t w0 = cycles();
                while (!done3(m, queued, done))
                {
                    keepRunning3(m, queued);
                    kRegisters(16);
                }
                f.waiting += cycles() - w0;
            }
            meshProjectAsm(mesh.out3, mesh.count, divTable);
            kFaces(faces, mesh.faces);
            faces += mesh.faces * 3;
            cycles();
        }
    }
    f.cycles = cycles() - t0;
    dspWaitQuietly();
    return f;
}

// The faces' stand-in for every mesh, meshlight3.dsp running every job
// meanwhile or not.
static uint32_t facesOnly(bool dsp)
{
    if (dsp)
    {
        for (int32_t i = 0; i < MESHES; i++) {
            queue3(i);
        }
        start3(gJobs3, gOut3);
    }
    uint32_t* faces = gFaceStores;
    uint32_t t0 = cycles();
    for (Mesh& mesh : gMeshes)
    {
        kFaces(faces, mesh.faces);
        faces += mesh.faces * 3;
        cycles();
    }
    uint32_t t = cycles() - t0;
    dspWaitQuietly();
    return t;
}

// --- the page ------------------------------------------------------------

static char* putMs(char* p, uint32_t cycles, int32_t width)
{
    uint32_t h = (cycles * 25 + 3579) / 7159;   // hundredths of a ms at 28.636 MHz
    char digits[12];
    int32_t n = 0;
    digits[n++] = '0' + h % 10;
    digits[n++] = '0' + h / 10 % 10;
    digits[n++] = '.';
    uint32_t whole = h / 100;
    do
    {
        digits[n++] = '0' + whole % 10;
        whole /= 10;
    } while (whole);
    for (int32_t i = n; i < width; i++) {
        *p++ = ' ';
    }
    while (n) {
        *p++ = digits[--n];
    }
    *p = 0;
    return p;
}

static void note(Page& page, char* line, char* p, const char* text)
{
    if (text)
    {
        p = put(p, " ");
        put(p, text);
    }
    page.add(line);
}

// name, ms, SH-2 cycles a vertex (of vertices, page 5's by default)
static void row(Page& page, const char* name, uint32_t cycles, const char* ok, int32_t vertices = 0)
{
    char line[64];
    char* p = pad(line, put(line, name), 22);
    p = putMs(p, cycles, 6);
    p = putTenths(p, cycles * 10 / (vertices ? vertices : gVertexCount), 9);
    note(page, line, p, ok);
}

// name, the master's work ms, its waits ms
static void rowFrame(Page& page, const char* name, const Frame& f, const char* ok)
{
    char line[64];
    char* p = pad(line, put(line, name), 22);
    p = putMs(p, f.cycles - f.waiting, 6);
    p = putMs(p, f.waiting, 8);
    note(page, line, p, ok);
}

static const char* yes(bool ok)
{
    return ok ? "yes" : "NO";
}

void pageMeshes(Page& page)
{
    page.add("DSP BENCH  page 5: the game's meshes");
    page.add("(the master's lit meshes of a frame at");
    page.add("the ledge: 80 meshes, 1210 vertices, made");
    page.add("up to those sizes, wholly on the frame)");
    page.add("");
    build();

    page.add("vertices alone            ms a vertex ok");
    row(page, "SH-2 (the game's)", sh2Vertices(), nullptr);
    meshLight2Load(meshlight2_dsp, MESHLIGHT2_DSP_COUNT);
    uint32_t v2 = dspAlone2();
    projectAlone();
    row(page, "DSP v2 rotate+light", v2, yes(allExact()));
    meshLight3Load();
    uint32_t v3 = dspAlone3();
    uint32_t project = projectAlone(true);
    row(page, "DSP v3 rotate+light", v3, yes(allExact(true)));
    row(page, "  v3, one long job", dspLong3(), nullptr, (gMeshes[MESHES - 1].out3 - gOut3) + gMeshes[MESHES - 1].groups * 4);
    row(page, "master projecting it", project, nullptr);

    page.add("");
    page.add("a frame's meshes, ms    work    wait ok");
    Frame sh2 = { frameSh2(), 0 };
    rowFrame(page, "SH-2 alone", sh2, nullptr);
    meshLight2Load(meshlight2_dsp, MESHLIGHT2_DSP_COUNT);
    Frame f = frameDsp2(kFrameEnd, 1, false);
    rowFrame(page, "v2, the frame ahead", f, yes(allExact()));
    meshLight3Load();
    f = frameDsp3(gMeshEnds, MESHES);
    rowFrame(page, "v3, a mesh ahead", f, yes(allExact(true)));
    f = frameDsp3(gObjectEnds, OBJECTS);
    rowFrame(page, "v3, an object ahead", f, yes(allExact(true)));
    f = frameDsp3(kFrameEnd, 1);
    rowFrame(page, "v3, the frame ahead", f, yes(allExact(true)));
    Frame faces = { facesOnly(false), 0 };
    rowFrame(page, "faces' stand-in alone", faces, nullptr);
    faces.cycles = facesOnly(true);
    rowFrame(page, "  beside DSP v3", faces, nullptr);
    page.add("");
    page.add("(ok: the same vertices as the game's)");
}

// --- page 6: the master's parts --------------------------------------------

// Each part of the master's work in page 5's frames, for every mesh in one
// loop, its setups and jobs written to a list of their own (the DSP may be
// running the frame's).
static MeshJob3 gPartJobs[MESHES + 1];

// The game's setup for the SH-2's transform; meshlight3.dsp's for the DSP
static uint32_t partSetups(bool jobs)
{
    static MeshXform xf;
    uint32_t t0 = cycles();
    for (int32_t i = 0; i < MESHES; i++)
    {
        if (jobs) {
            writeJob3(&gPartJobs[i], gMeshes[i].xf);
        } else {
            writeXform(&xf, &gMeshes[i].xf);
        }
    }
    return cycles() - t0;
}

// queue3's stores but the setup's
static uint32_t partJobs()
{
    uint32_t t0 = cycles();
    for (int32_t i = 0; i < MESHES; i++)
    {
        const Mesh& mesh = gMeshes[i];
        MeshJob3& job = gPartJobs[i];
        *mesh.last3 = SENTINEL;
        job.src = mesh.dsp3;
        gPartJobs[i + 1].groups = -1;
        asm volatile("" ::: "memory");
        *(volatile int32_t*)&job.groups = mesh.groups - 1;
    }
    return cycles() - t0;
}

static uint32_t partChecks()
{
    uint32_t pending = 0;
    int32_t done = 0;
    uint32_t t0 = cycles();
    for (int32_t m = 0; m < MESHES; m++) {
        pending += !done3(m, MESHES, done);
    }
    uint32_t t = cycles() - t0;
    asm volatile("" :: "r"(pending));
    return t;
}

static uint32_t partPurge()
{
    uint32_t t0 = cycles();
    cachePurge();
    return cycles() - t0;
}

static uint32_t partProjecting()
{
    uint32_t t0 = cycles();
    for (Mesh& mesh : gMeshes) {
        meshProjectAsm(mesh.out3, mesh.count, divTable);
    }
    return cycles() - t0;
}

static uint32_t partFaces()
{
    uint32_t* faces = gFaceStores;
    uint32_t t0 = cycles();
    for (const Mesh& mesh : gMeshes)
    {
        kFaces(faces, mesh.faces);
        faces += mesh.faces * 3;
    }
    return cycles() - t0;
}

static uint32_t partVertices()
{
    Vertex* ref = gRef;
    uint32_t t0 = cycles();
    for (Mesh& mesh : gMeshes)
    {
        transformMeshAsm(mesh.src, mesh.count, ref, &mesh.xf);
        ref += mesh.count;
    }
    return cycles() - t0;
}

// the frames' own timer reads: one a mesh
static uint32_t partTimer()
{
    uint32_t t0 = cycles();
    for (int32_t i = 0; i < MESHES; i++) {
        cycles();
    }
    return cycles() - t0;
}

// The DSP's output made again, so that each projection starts from it.
static void remake()
{
    dspAlone3();
}

// A part timed beside meshlight3.dsp running every mesh's job.
static uint32_t beside(uint32_t (*part)())
{
    for (int32_t i = 0; i < MESHES; i++) {
        queue3(i);
    }
    start3(gJobs3, gOut3);
    uint32_t t = part();
    dspWaitQuietly();
    return t;
}

static uint32_t besideSetups() { return partSetups(true); }

// The checks beside the DSP as a frame meets them, its meshes done: their
// sentinels cleared (the DSP writes them again the same).
static uint32_t besideChecks()
{
    for (int32_t i = 0; i < MESHES; i++) {
        queue3(i);
    }
    for (const Mesh& mesh : gMeshes) {
        *mesh.last3 = 0;
    }
    start3(gJobs3, gOut3);
    uint32_t t = partChecks();
    dspWaitQuietly();
    return t;
}

static void rowParts(Page& page, const char* name, uint32_t alone, int32_t besides)
{
    char line[64];
    char* p = pad(line, put(line, name), 22);
    p = putMs(p, alone, 8);
    if (besides >= 0) {
        putMs(p, besides, 8);
    }
    page.add(line);
}

void pageParts(Page& page)
{
    page.add("DSP BENCH  page 6: the master's parts");
    page.add("(page 5's frames, each part of the");
    page.add("master's work for the 80 meshes timed");
    page.add("on its own: alone, and beside DSP v3)");
    page.add("");

    page.add("SH-2 frame, ms           alone");
    uint32_t setups = partSetups(false);
    uint32_t vertices = partVertices();
    uint32_t faces = partFaces();
    uint32_t timer = partTimer();
    rowParts(page, "setups (MeshXform)", setups, -1);
    rowParts(page, "vertices (the game's)", vertices, -1);
    rowParts(page, "faces' stand-in", faces, -1);
    rowParts(page, "timer reads", timer, -1);
    rowParts(page, "the parts' sum", setups + vertices + faces + timer, -1);
    rowParts(page, "the frame, measured", frameSh2(), -1);
    page.add("");

    page.add("DSP v3 frame, ms         alone  beside");
    meshLight3Load();
    uint32_t a[6], b[6];
    a[0] = partSetups(true);
    b[0] = beside(besideSetups);
    a[1] = partJobs();
    b[1] = beside(partJobs);
    remake();
    a[2] = partChecks();
    b[2] = besideChecks();
    remake();
    a[3] = partPurge();
    a[4] = partProjecting();
    remake();
    b[3] = beside(partPurge);
    remake();
    b[4] = beside(partProjecting);
    a[5] = partFaces();
    b[5] = beside(partFaces);
    static const char* const names[6] = {
        "setups (v3's)", "the jobs' other stores", "checking each is done",
        "purging the cache", "projecting", "faces' stand-in"
    };
    uint32_t sumA = timer, sumB = timer;
    for (int32_t i = 0; i < 6; i++)
    {
        rowParts(page, names[i], a[i], b[i]);
        sumA += a[i];
        sumB += b[i];
    }
    rowParts(page, "timer reads", timer, -1);
    rowParts(page, "the parts' sum", sumA, sumB);
    Frame f = frameDsp3(kFrameEnd, 1);
    rowParts(page, "the frame ahead: work", f.cycles - f.waiting, -1);
    rowParts(page, "                 wait", f.waiting, -1);
}
