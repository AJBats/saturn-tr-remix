// Page 5, the game's meshes: the master's meshes lit by normals in a frame at
// the game's ledge (Lara, a door, two wolves and twelve bridge planks: 80
// meshes, 1210 vertices, in the order the master drew them in a Mednafen
// trace of main at a90e0f1), made up to those sizes and wholly on the frame.
// The game's own transform (saturn/meshxform.s transformMeshAsm, its shaded
// fast path) against meshlight.dsp rotating and lighting them and the master
// projecting what comes out (meshproject.s); then as a frame's work: each
// mesh's setup, its vertices and a stand-in for its faces (kernels.s kFaces),
// with the DSP's jobs written and taken as the game would, the master
// projecting and facing one mesh while the DSP works on the next.

#include "bench.h"
#include "dsp.h"
#include "meshlight_dsp.h"

// saturn/render.h's, as meshxform.s and meshlight.dsp read them
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

// meshlight.dsp's job: batches of 8 vertices (0 ends the list, and is
// written last), DSP addresses, the setup
struct MeshLightJob
{
    uint32_t batches;
    uint32_t normals, src, dst;
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
    {8, 6}, {12, 14}, {8, 6}, {12, 10}, {8, 6}, {8, 6}, {8, 6}, {8, 6}
};
static const uint8_t kPlank[][2] = { {26, 16} };

#define MESHES      80
#define SENTINEL    0xFFFFFFFF  // in a job's last output longword until the DSP's write (clip byte 0)

struct Mesh
{
    int32_t count, faces;
    MeshVertex* src;
    int8_t* normals;
    Vertex* out;            // whole batches, on cache lines
    MeshXform xf;
};

static Mesh gMeshes[MESHES];
static int32_t gVertexCount;
static MeshLightJob gJobs[MESHES + 1];
static uint8_t gData[48 * 1024] __attribute__((aligned(16)));
static Vertex gRef[1280];               // transformMeshAsm's, mesh after mesh
static uint32_t gFaceStores[1280 * 3];  // kFaces'

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

static void add(uint8_t*& p, uint32_t& seed, const uint8_t (*list)[2], int32_t n, int32_t& m)
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
        mesh.out = (Vertex*)take(p, whole * sizeof(Vertex));
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
        gVertexCount += mesh.count;
    }
}

static void build()
{
    for (int32_t i = 1; i < 1281; i++) {
        divTable[i] = uint16_t((1 << 16) / i);
    }
    divTable[0] = 0xFFFF;
    uint8_t* p = gData;
    uint32_t seed = 0x2B7E1516;
    int32_t m = 0;
    add(p, seed, kLara, 15, m);
    add(p, seed, kDoor, 1, m);
    add(p, seed, kWolf, 26, m);
    add(p, seed, kWolf, 26, m);
    for (int32_t i = 0; i < 12; i++) {
        add(p, seed, kPlank, 1, m);
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
    gJobs[i + 1].batches = 0;
    *(volatile uint32_t*)&job.batches = batches(mesh.count);
}

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

// The next job the DSP hasn't finished, from first on (published up to end).
static int32_t unfinished(int32_t first, int32_t end)
{
    while (first < end && *sentinel(gMeshes[first]) != SENTINEL) {
        first++;
    }
    return first;
}

struct Frame
{
    uint32_t cycles, waiting;
};

// A frame's meshes with the DSP: each mesh's job written and published, the
// DSP started again if it had stopped, then the mesh before projected and
// faced once the DSP is past it.
static Frame frameDsp()
{
    Frame f = { 0, 0 };
    uint32_t* faces = gFaceStores;
    int32_t done = 0;      // the DSP is past every job before
    gJobs[0].batches = 0;
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

// The faces' stand-in for every mesh, the DSP running every job meanwhile or not.
static uint32_t facesOnly(bool dsp)
{
    if (dsp)
    {
        for (int32_t i = 0; i < MESHES; i++) {
            queue(i);
        }
        startAt(gJobs);
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

static void row(Page& page, const char* name, uint32_t cycles, int32_t perVertex, const char* note)
{
    char line[64];
    char* p = put(line, name);
    p = pad(line, p, 22);
    p = putMs(p, cycles, 6);
    p = perVertex >= 0 ? putTenths(p, perVertex, 9) : pad(line, p, 37);
    if (note)
    {
        p = put(p, " ");
        put(p, note);
    }
    page.add(line);
}

void pageMeshes(Page& page)
{
    page.add("DSP BENCH  page 5: the game's meshes");
    page.add("(the master's lit meshes of a frame at");
    page.add("the ledge: 80 meshes, 1210 vertices, made");
    page.add("up to those sizes, wholly on the frame)");
    page.add("");
    build();
    meshLightLoad();

    page.add("                          ms a vertex ok");
    uint32_t sh2 = sh2Vertices();
    row(page, "SH-2 vertices (game's)", sh2, sh2 * 10 / gVertexCount, nullptr);
    uint32_t dsp = dspAlone();
    row(page, "DSP rotate+light alone", dsp, dsp * 10 / gVertexCount, nullptr);
    uint32_t project = projectAlone();
    row(page, "master projecting it", project, project * 10 / gVertexCount, allExact() ? "yes" : "NO");

    page.add("");
    page.add("A frame's meshes: setup, vertices, faces");
    uint32_t frame = frameSh2();
    row(page, "  SH-2 alone", frame, -1, nullptr);
    Frame withDsp = frameDsp();
    row(page, "  with the DSP", withDsp.cycles, -1, allExact() ? "yes" : "NO");
    row(page, "  of which waiting", withDsp.waiting, -1, nullptr);
    row(page, "faces' stand-in alone", facesOnly(false), -1, nullptr);
    row(page, "  beside the DSP", facesOnly(true), -1, nullptr);
    page.add("");
    page.add("(ok: the same vertices as the game's)");
}
