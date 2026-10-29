// Page 5, the game's meshes: the master's meshes lit by normals in a frame at
// the game's ledge (Lara, a door, two wolves and twelve bridge planks: 80
// meshes, 1210 vertices, in the order the master drew them in a Mednafen
// trace of main at a90e0f1), made up to those sizes and wholly on the frame.
// The game's own transform (saturn/meshxform.s transformMeshAsm, its shaded
// fast path) against meshlight.dsp and meshlight2.dsp rotating and lighting
// them and the master projecting what comes out (meshproject.s); then as a
// frame's work: each mesh's setup, its vertices and a stand-in for its faces
// (kernels.s kFaces), with the DSP's jobs written and taken as the game would,
// the master projecting and facing the meshes the DSP has done while it works
// on the next. The master's work and its waits for the DSP apart.
// Page 6 times each part of the master's work in those frames on its own.

#include "bench.h"
#include "dsp.h"
#include "meshlight_dsp.h"
#include "meshlight2_dsp.h"
#include "meshlight2nodma_dsp.h"

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

// A job of the DSP's list: meshlight.dsp's size is batches of 8 vertices,
// meshlight2.dsp's vertices; 0 ends the list, and is written last
struct MeshLightJob
{
    uint32_t size;
    uint32_t normals, src, dst;     // DSP addresses
    MeshXform xf;
};

static_assert(sizeof(MeshXform) == 72 && sizeof(MeshLightJob) == 22 * 4, "as the game's");

extern "C" void transformMeshAsm(const MeshVertex* src, int32_t count, Vertex* dst, MeshXform* xf);
extern "C" void meshProjectAsm(Vertex* v, int32_t count, const uint16_t* divTable);
extern "C" void kFaces(void* p, uint32_t n);

// What saturn/meshxform.s takes from the game for its renderMeshAsm, which
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
};

static Mesh gMeshes[MESHES];
static int32_t gVertexCount;
static MeshLightJob gJobs[MESHES + 1];
static uint8_t gData[48 * 1024] __attribute__((aligned(16)));
static Vertex gOut[174 * 8] __attribute__((aligned(16)));  // the outputs, whole batches
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

static void add(uint8_t*& p, Vertex*& out, uint32_t& seed, const uint8_t (*list)[2], int32_t n, int32_t& m)
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
        x.ambient = 3000 + int32_t((seed >> 12) % 5000);
        x.lx = int32_t((seed >> 20) % 147) - 73;
        x.ly = int32_t((seed >> 12) % 147) - 73;
        x.lz = int32_t((seed >> 4) % 147) - 73;
        x.range = 5000 + int32_t((seed >> 2) % 11000);
        x.normals = mesh.normals;
        x.shades = nullptr;
        x.g = 0;
        x.inside = 1;
        mesh.dspSrc = dspAddress(mesh.src);
        mesh.last = uncached(&mesh.out[mesh.count - 1].zg);
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
    uint32_t seed = 0x2B7E1516;
    int32_t m = 0, o = 0;
    add(p, out, seed, kLara, 15, m);
    gObjectEnds[o++] = m;
    add(p, out, seed, kDoor, 1, m);
    gObjectEnds[o++] = m;
    add(p, out, seed, kWolf, 26, m);
    gObjectEnds[o++] = m;
    add(p, out, seed, kWolf, 26, m);
    gObjectEnds[o++] = m;
    for (int32_t i = 0; i < 12; i++)
    {
        add(p, out, seed, kPlank, 1, m);
        gObjectEnds[o++] = m;
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
static bool exact(const Mesh& mesh, const Vertex* ref)
{
    for (int32_t v = 0; v < mesh.count; v++)
    {
        if (mesh.out[v].xy != ref[v].xy || mesh.out[v].zg != ref[v].zg)
            return false;
    }
    return true;
}

static bool allExact()
{
    bool ok = true;
    const Vertex* ref = gRef;
    for (const Mesh& mesh : gMeshes)
    {
        ok = ok && exact(mesh, ref);
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
static uint32_t projectAlone()
{
    cachePurge();
    uint32_t t0 = cycles();
    for (Mesh& mesh : gMeshes)
    {
        meshProjectAsm(mesh.out, mesh.count, divTable);
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

// --- meshlight.dsp ------------------------------------------------------

static void meshLightLoad()
{
    static const uint32_t md0[8] = { 4, 0, 0x1FFF, 0x1F00, 0x1FFF, 0xFFFF, 0x8000, 0xFF000000 };
    static const uint32_t md1[3] = { 0xFFFF, 0x8000, 256 };
    static const uint32_t md3[4] = { 0xFFFF0000, 0xFFFF, 0, 4 };
    dspLoad(meshlight_dsp, MESHLIGHT_DSP_COUNT);
    dspWrite(0, 16, md0, 8);
    dspWrite(1, 48, md1, 3);
    dspWrite(3, 10, md3, 4);
}

static void startAt(const MeshLightJob* job)
{
    const uint32_t next = dspAddress(job);
    dspWrite(3, 40, &next, 1);
    dspStart(0);
}

static volatile uint32_t* sentinel(const Mesh& mesh)
{
    return uncached(&mesh.out[batches(mesh.count) * 8 - 1].zg);
}

// Job i written for mesh i, the slot after it ending the list, published.
static void queue(int32_t i)
{
    const Mesh& mesh = gMeshes[i];
    MeshLightJob& job = gJobs[i];
    *sentinel(mesh) = SENTINEL;
    writeXform(&job.xf, &mesh.xf);
    job.normals = dspAddress(mesh.normals);
    job.src = dspAddress(mesh.src);
    job.dst = dspAddress(mesh.out);
    gJobs[i + 1].size = 0;
    *(volatile uint32_t*)&job.size = batches(mesh.count);
}

// Every mesh's job, then the DSP's run with the master off the bus.
static uint32_t dspAlone()
{
    for (int32_t i = 0; i < MESHES; i++) {
        queue(i);
    }
    uint32_t t0 = cycles();
    startAt(gJobs);
    dspWaitQuietly();
    return cycles() - t0;
}

// The next job the DSP hasn't finished, from first on (published up to end).
static int32_t unfinished(int32_t first, int32_t end)
{
    while (first < end && *sentinel(gMeshes[first]) != SENTINEL) {
        first++;
    }
    return first;
}

// A frame's meshes with meshlight.dsp: each mesh's job written and
// published, the DSP started again if it had stopped, then the mesh before
// projected and faced once the DSP is past it.
static Frame frameDsp()
{
    Frame f = { 0, 0 };
    uint32_t* faces = gFaceStores;
    int32_t done = 0;      // the DSP is past every job before
    gJobs[0].size = 0;
    uint32_t t0 = cycles();
    for (int32_t i = 0; i <= MESHES; i++)
    {
        if (i < MESHES)
        {
            queue(i);
            if (!dspBusy())
            {
                done = unfinished(done, i + 1);
                if (done <= i) {
                    startAt(gJobs + done);
                }
            }
        }
        if (i == 0)
            continue;
        Mesh& mesh = gMeshes[i - 1];
        if (*sentinel(mesh) == SENTINEL)
        {
            uint32_t w0 = cycles();
            while (*sentinel(mesh) == SENTINEL)
            {
                if (!dspBusy())
                {
                    done = unfinished(done, i);
                    if (*sentinel(mesh) == SENTINEL) {
                        startAt(gJobs + done);
                    }
                }
                kRegisters(16);
            }
            f.waiting += cycles() - w0;
        }
        dropLines(mesh.out, mesh.count * sizeof(Vertex));
        meshProjectAsm(mesh.out, mesh.count, divTable);
        kFaces(faces, mesh.faces);
        faces += mesh.faces * 3;
        cycles();
    }
    f.cycles = cycles() - t0;
    return f;
}

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

// The faces' stand-in for every mesh, meshlight2.dsp running every job
// meanwhile or not.
static uint32_t facesOnly(bool dsp)
{
    if (dsp)
    {
        for (int32_t i = 0; i < MESHES; i++) {
            queue2(i);
        }
        start2(0);
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

// name, ms, SH-2 cycles a vertex
static void row(Page& page, const char* name, uint32_t cycles, const char* ok)
{
    char line[64];
    char* p = pad(line, put(line, name), 22);
    p = putMs(p, cycles, 6);
    p = putTenths(p, cycles * 10 / gVertexCount, 9);
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
    meshLightLoad();
    uint32_t v1 = dspAlone();
    projectAlone();
    row(page, "DSP v1 rotate+light", v1, yes(allExact()));
    meshLight2Load(meshlight2_dsp, MESHLIGHT2_DSP_COUNT);
    uint32_t v2 = dspAlone2();
    uint32_t project = projectAlone();
    row(page, "DSP v2 rotate+light", v2, yes(allExact()));
    row(page, "master projecting it", project, nullptr);
    meshLight2Load(meshlight2nodma_dsp, MESHLIGHT2NODMA_DSP_COUNT);
    row(page, "v2, transfers skipped", dspAlone2(), nullptr);

    page.add("");
    page.add("a frame's meshes, ms    work    wait ok");
    Frame sh2 = { frameSh2(), 0 };
    rowFrame(page, "SH-2 alone", sh2, nullptr);
    meshLightLoad();
    Frame f = frameDsp();
    rowFrame(page, "v1, a mesh ahead", f, yes(allExact()));
    meshLight2Load(meshlight2_dsp, MESHLIGHT2_DSP_COUNT);
    f = frameDsp2(gMeshEnds, MESHES, false);
    rowFrame(page, "v2, a mesh ahead", f, yes(allExact()));
    f = frameDsp2(gObjectEnds, OBJECTS, false);
    rowFrame(page, "v2, an object ahead", f, yes(allExact()));
    f = frameDsp2(kFrameEnd, 1, false);
    rowFrame(page, "v2, the frame ahead", f, yes(allExact()));
    f = frameDsp2(kFrameEnd, 1, true);
    rowFrame(page, "  lines dropped each", f, yes(allExact()));
    Frame faces = { facesOnly(false), 0 };
    rowFrame(page, "faces' stand-in alone", faces, nullptr);
    faces.cycles = facesOnly(true);
    rowFrame(page, "  beside DSP v2", faces, nullptr);
    page.add("");
    page.add("(ok: the same vertices as the game's)");
}

// --- page 6: the master's parts --------------------------------------------

// Each part of the master's work in page 5's frames, for every mesh in one
// loop, its setups and jobs written to a list of their own (the DSP may be
// running the frame's).
static MeshLightJob gPartJobs[MESHES + 1];

static uint32_t partSetups(bool jobs)
{
    static MeshXform xf;
    uint32_t t0 = cycles();
    for (int32_t i = 0; i < MESHES; i++) {
        writeXform(jobs ? &gPartJobs[i].xf : &xf, &gMeshes[i].xf);
    }
    return cycles() - t0;
}

// queue2's stores but the setup's
static uint32_t partJobs()
{
    uint32_t t0 = cycles();
    for (int32_t i = 0; i < MESHES; i++)
    {
        const Mesh& mesh = gMeshes[i];
        MeshLightJob& job = gPartJobs[i];
        *mesh.last = SENTINEL;
        job.src = mesh.dspSrc;
        gPartJobs[i + 1].size = 0;
        asm volatile("" ::: "memory");
        *(volatile uint32_t*)&job.size = mesh.count;
    }
    return cycles() - t0;
}

static uint32_t partChecks()
{
    uint32_t pending = 0;
    int32_t done = 0;
    uint32_t t0 = cycles();
    for (int32_t m = 0; m < MESHES; m++) {
        pending += !done2(m, MESHES, done);
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
        meshProjectAsm(mesh.out, mesh.count, divTable);
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
    dspAlone2();
}

// A part timed beside meshlight2.dsp running every mesh's job.
static uint32_t beside(uint32_t (*part)())
{
    for (int32_t i = 0; i < MESHES; i++) {
        queue2(i);
    }
    start2(0);
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
        queue2(i);
    }
    for (const Mesh& mesh : gMeshes) {
        *mesh.last = 0;
    }
    start2(0);
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
    page.add("on its own: alone, and beside DSP v2)");
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

    page.add("DSP v2 frame, ms         alone  beside");
    meshLight2Load(meshlight2_dsp, MESHLIGHT2_DSP_COUNT);
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
        "setups (MeshXform)", "the jobs' other stores", "checking each is done",
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
    Frame f = frameDsp2(kFrameEnd, 1, false);
    rowParts(page, "the frame ahead: work", f.cycles - f.waiting, -1);
    rowParts(page, "                 wait", f.waiting, -1);
}
