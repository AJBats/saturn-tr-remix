// The master's meshes projected on the SCU DSP (meshjobs.dsp). The master
// writes each mesh's job into a ring and goes on drawing; the DSP reads the
// jobs itself by DMA, one after another, until it comes to one not written
// yet, and is started again from there when it has stopped. The master adds
// a mesh's faces once its vertices are out (render.cpp), and whatever the
// DSP hasn't reached by then it projects itself: the two give the same
// vertices to the bit, so a job both start is no harm.
//
// The DSP is the slave's for the rooms (project.dsp) until it hands it over
// for the frame (rooms.cpp dspFeed); the master loads meshjobs.dsp then, and
// the slave loads its own again at its next frame.

#include <string.h>

#include "render.h"
#include "dsp.h"
#include "slave.h"
#include "system.h"
#include "trace.h"
#include "project_dsp.h"
#include "meshjobs_dsp.h"

// A mesh for meshjobs.dsp.
struct DspMeshJob
{
    uint32 batches;         // of 8 vertices; 0 ends the list
    uint32 step;            // the normals' longwords a batch: 8, or 0 for a flat mesh
    uint32 src, dst, normals;   // DSP addresses
    int32 m[12];            // m00 m01 m02 tx  m10 m11 m12 ty  m20 m21 m22 tz
    int32 light[6];         // lx ly lz  range << 8  4  ambient
};
static_assert(sizeof(DspMeshJob) == 23 * 4, "meshjobs.dsp reads 23 longwords a job");

extern "C" const uint32 gDspNoNormals[8] = {};  // a flat mesh's, read again each batch (and meshxform.s's)

// What the menu's DSP option gives it after the rooms.
int32 gDspMeshes = DSP_ROOMS_MESHES;

// The ring: a job's number picks its slot. The one past the last always ends
// the list: the DSP stops there, and is started at the first slot again.
#define DSP_JOBS        64
static DspMeshJob gDspJobs[DSP_JOBS + 1];
static const uint32* gDspSentinels[DSP_JOBS];   // past the cache
static uint32 gJobsMade;        // jobs written
static uint32 gJobsFinished;    // done or taken back, in order: their slots are free
static uint32 gDspNext;         // the first the DSP may not have done
static bool gDspMine;           // the master has it, meshjobs.dsp loaded
static bool gDspRunning;        // started here and not yet seen to stop

bool meshDspRunning()
{
    return gDspRunning;
}

// Between the CPUs, uncached.
static volatile uint32 gDspHanded;      // the slave has handed the DSP over this frame
static volatile uint32 gDspWanted;      // the master has queued a job this frame
static bool gWantSaid;                  // ... as the master knows it
static volatile uint32 gDspMeshProgram; // meshjobs.dsp is loaded

// meshjobs.dsp's constants (the DSP stopped).
static void meshDspInit()
{
    const int32 md0[29] = {
        VIEW_MIN_F, VIEW_MAX_F, 4, 4096, int32(dspAddress(gDspDivide)),
        1023 - FRAME_WIDTH / 2, -1024 - FRAME_WIDTH / 2, FRAME_WIDTH / 2,
        1023 - FRAME_HEIGHT / 2, -1024 - FRAME_HEIGHT / 2, FRAME_HEIGHT / 2,
        0, FRAME_WIDTH, 0, FRAME_HEIGHT, 0xFFFF, 0x8000, 65536, 1,
        int32(0xFF000000), 0, 0, 0, 0, 0, 0, 0x1FFF, 0x1F00, 0x1FFF
    };
    dspWrite(0, 12, (const uint32_t*)md0, 29);
}

#ifdef HW_LOG
// Writes job for count vertices from src to dst as xf says, all but its
// batches, which it returns (meshxform.s renderMeshPendAsm's work, for the
// boot test).
static uint32 meshDspFill(DspMeshJob &job, const MeshXform &xf, const MeshVertex* src, Vertex* dst, int32 count)
{
    for (int32 r = 0; r < 3; r++)
    {
        job.m[r * 4] = xf.rows[r * 3];
        job.m[r * 4 + 1] = xf.rows[r * 3 + 1];
        job.m[r * 4 + 2] = xf.rows[r * 3 + 2];
    }
    job.m[3] = xf.tx;
    job.m[7] = xf.ty;
    job.m[11] = xf.tz;
    // meshxform.s multiplies the light's direction and range as 16 bits
    job.light[0] = int16(xf.lx);
    job.light[1] = int16(xf.ly);
    job.light[2] = int16(xf.lz);
    job.light[3] = int16(xf.range) * 256;
    job.light[4] = 4;
    job.light[5] = xf.normals ? xf.ambient : xf.g << 8;
    job.step = xf.normals ? 8 : 0;
    job.src = dspAddress(src);
    job.dst = dspAddress(dst);
    job.normals = dspAddress(xf.normals ? (const void*)xf.normals : gDspNoNormals);
    return (count + 7) >> 3;
}
#endif

#ifdef MESH_DSP_CHECK
// The fields of job (meshxform.s's) that differ from meshDspFill's for xf, a
// bit each longword (the light's direction only for a mesh lit by normals).
uint32 meshDspCheck(const DspMeshJob* job, const MeshXform &xf, const MeshVertex* src, Vertex* dst, int32 count)
{
    DspMeshJob ref;
    ref.batches = meshDspFill(ref, xf, src, dst, count);
    const uint32* a = (const uint32*)job;
    const uint32* b = (const uint32*)&ref;
    uint32 bad = 0;
    for (int32 i = 0; i < 23; i++)
    {
        if (i >= 17 && i < 20 && !ref.step)
            continue;
        if (a[i] != b[i]) {
            bad |= 1 << i;
        }
    }
    return bad;
}
#endif

// Starts meshjobs.dsp on the list at jobs.
static void meshDspStart(const DspMeshJob* jobs)
{
    uint32_t at = dspAddress(jobs);
    dspWrite(3, 40, &at, 1);
    dspStart(0);
}

DspMeshJob* meshDspSlot()
{
    if (gJobsMade - gJobsFinished >= DSP_JOBS - 1)
        return NULL;
    uint32 slot = gJobsMade % DSP_JOBS;
    gDspJobs[slot + 1].batches = 0;     // the list's end until the next is written
    return gDspJobs + slot;
}

uint32 meshDspPublish(const uint32* sentinel)
{
    if (!gWantSaid)
    {
        gWantSaid = true;
        *uncached(&gDspWanted) = 1;
    }
    gDspSentinels[gJobsMade % DSP_JOBS] = sentinel;
    return gJobsMade++;
}

static bool meshDspDone(uint32 job)
{
    return *(volatile const uint32*)gDspSentinels[job % DSP_JOBS] != DSP_SENTINEL;
}

void meshDspFinished(uint32 job)
{
    if (int32(job + 1 - gJobsFinished) > 0) {
        gJobsFinished = job + 1;
    }
}

// Takes the DSP over once the slave has handed it over.
static bool meshDspTakeOver()
{
    if (!*uncached(&gDspHanded))
        return false;
    dspLoad(meshjobs_dsp, MESHJOBS_DSP_COUNT);
    meshDspInit();
    *uncached(&gDspMeshProgram) = 1;
    gDspMine = true;
    return true;
}

bool meshDspReady()
{
    return gDspMine || gDspMeshes == DSP_MESHES_FIRST || meshDspTakeOver();
}

void meshDspPoll()
{
    if (!gDspMine && (gJobsMade == gDspNext || !meshDspTakeOver()))
        return;
    if (gDspRunning)
    {
        if (dspBusy())
            return;
        traceEnd(TRACE_DSP_MESH, TR_DSP_MESHES);
        gDspRunning = false;
    }
    // past the jobs finished, whose slots may hold newer jobs, then those the
    // DSP has done since it started
    if (int32(gJobsFinished - gDspNext) > 0) {
        gDspNext = gJobsFinished;
    }
    while (gDspNext != gJobsMade && meshDspDone(gDspNext)) {
        gDspNext++;
    }
    if (gDspNext == gJobsMade)
        return;
    traceBegin(TRACE_DSP_MESH, TR_DSP_MESHES, gDspNext % DSP_JOBS);
    meshDspStart(gDspJobs + gDspNext % DSP_JOBS);
    gDspRunning = true;
}

void meshDspXform(uint32 job, const int8* normals, MeshXform &xf)
{
    const DspMeshJob &j = gDspJobs[job % DSP_JOBS];
    for (int32 r = 0; r < 3; r++)
    {
        xf.rows[r * 3] = j.m[r * 4];
        xf.rows[r * 3 + 1] = j.m[r * 4 + 1];
        xf.rows[r * 3 + 2] = j.m[r * 4 + 2];
    }
    xf.tx = j.m[3];
    xf.ty = j.m[7];
    xf.tz = j.m[11];
    xf.divTable = divTable;
    xf.lx = j.light[0];
    xf.ly = j.light[1];
    xf.lz = j.light[2];
    xf.range = j.light[3] >> 8;
    xf.ambient = j.light[5];
    xf.g = j.light[5] >> 8;
    xf.normals = j.step ? normals : NULL;
    xf.shades = NULL;
    xf.inside = 0;
}

void meshDspTake(uint32 job, const int8* normals, MeshXform &xf)
{
    gDspJobs[job % DSP_JOBS].batches = 0;
    meshDspXform(job, normals, xf);
}

void meshDspStop()
{
    gDspNext = gJobsMade;
    gJobsFinished = gJobsMade;
}

void meshDspEnd()
{
    meshDspStop();
    *uncached(&gDspHanded) = 0;     // the slave is done for the frame: none of its runs to come
    *uncached(&gDspWanted) = 0;
    gWantSaid = false;
    if (!gDspMine)
        return;
    // a job taken back may still run, writing where the next frame's
    // vertices go
    if (gDspRunning)
    {
        if (dspBusy())
        {
            traceBegin(TRACE_MASTER, TR_DSP_WAIT);
            while (dspBusy());
            traceEnd(TRACE_MASTER, TR_DSP_WAIT);
        }
        traceEnd(TRACE_DSP_MESH, TR_DSP_MESHES);
        gDspRunning = false;
    }
    gDspMine = false;
}

bool meshDspWanted()
{
    return *uncached(&gDspWanted);
}

void meshDspHandOver()
{
    *uncached(&gDspHanded) = 1;
}

void meshDspRooms()
{
    if (!*uncached(&gDspMeshProgram))
        return;
    while (dspBusy());
    dspLoad(project_dsp, PROJECT_DSP_COUNT);
    *uncached(&gDspMeshProgram) = 0;
}

#ifdef HW_LOG
// meshjobs.dsp against transformMeshAsm on random meshes: the same 512
// vertices as 8 jobs of 64 and as 64 jobs of 8 (their difference is the
// DSP's cost a job), this CPU's ticks, and the vertices that differ.
void meshDspTest(uint32 &longTicks, uint32 &shortTicks, uint32 &cpuTicks, int32 &errors)
{
    const int32 runs = 8, count = 64, total = runs * count;
    MeshVertex* src = (MeshVertex*)(gVertices + MAX_VERTICES / 2);
    int8* normals = (int8*)(src + total);
    Vertex* ref = gVertices;
    Vertex* out = gVertices + total;
    DspMeshJob* jobs = (DspMeshJob*)(out + total);
    static_assert(MAX_VERTICES / 2 + 512 * (6 + 4) / sizeof(Vertex) <= MAX_VERTICES &&
                  2 * 512 * sizeof(Vertex) + 65 * sizeof(DspMeshJob) <= MAX_VERTICES / 2 * sizeof(Vertex), "they fit");

    uint32 seed = 0x13579BDF;
    int16* c = (int16*)src;
    for (int32 i = 0; i < total * 3; i++)
    {
        seed = seed * 1664525 + 1013904223;
        c[i] = int16(((seed >> 16) & 2047) - 1024);
    }
    for (int32 i = 0; i < total * 4; i++)
    {
        seed = seed * 1664525 + 1013904223;
        normals[i] = int8((i & 3) == 3 ? seed >> 24 : ((seed >> 16) % 147) - 73);
    }

    // views (in world units) that put vertices behind the camera, before the
    // near plane, past the far plane, off each side of the frame and past
    // what VDP1 can address; flat and lit by normals, with lights that take
    // vertices below the darkest and past the brightest
    static const int32 moves[runs][3] = {
        { 0, 0, 2000 }, { 0, 0, 600 }, { 0, 0, -1500 }, { 0, 0, 20000 },
        { -3000, 2000, 3000 }, { 3000, -2000, 3000 }, { 25000, -25000, 1500 }, { 300, -200, 1200 }
    };
    static const int16 rows[2][9] = {
        { 14189, 0, -8192, 4096, 15851, 7094, 7094, -4096, 12288 },
        { 14189, 4096, 7094, 0, 15851, -4096, -8192, 7094, 12288 }
    };
    static const int32 lights[runs][2] = {     // ambient, range
        { 0, 0 }, { 707, 0 }, { 8117, 5822 }, { 4000, 16000 }, { 0, 0 }, { -320, 0 }, { 1000, 12000 }, { 3000, -16000 }
    };

    MeshXform xf[runs];
    cpuTicks = 0;
    for (int32 r = 0; r < runs; r++)
    {
        MeshXform &x = xf[r];
        for (int32 i = 0; i < 9; i++) {
            x.rows[i] = rows[r & 1][i];
        }
        x.tx = moves[r][0] << FIXED_SHIFT;
        x.ty = moves[r][1] << FIXED_SHIFT;
        x.tz = moves[r][2] << FIXED_SHIFT;
        x.divTable = divTable;
        x.inside = 0;
        seed = seed * 1664525 + 1013904223;
        x.g = r * 4 + 3;
        x.normals = lights[r][1] ? normals + r * count * 4 : NULL;
        x.shades = NULL;
        x.ambient = lights[r][0];
        x.range = lights[r][1];
        x.lx = int32((seed >> 20) % 147) - 73;
        x.ly = int32((seed >> 12) % 147) - 73;
        x.lz = int32((seed >> 4) % 147) - 73;

        uint32 t0 = timerTicks();
        transformMeshAsm(src + r * count, count, ref + r * count, &x);
        cpuTicks += (timerTicks() - t0) & 0xFFFF;
    }

    dspLoad(meshjobs_dsp, MESHJOBS_DSP_COUNT);
    meshDspInit();

    // 8 jobs of 64
    for (int32 r = 0; r < runs; r++) {
        jobs[r].batches = meshDspFill(jobs[r], xf[r], src + r * count, out + r * count, count);
    }
    jobs[runs].batches = 0;
    uint32 t0 = timerTicks();
    meshDspStart(jobs);
    while (dspBusy());
    longTicks = (timerTicks() - t0) & 0xFFFF;
    cachePurge();   // the DSP wrote behind the cache
    errors = 0;
    for (int32 i = 0; i < total; i++) {
        errors += memcmp(ref + i, out + i, sizeof(Vertex)) != 0;
    }

    // 64 jobs of 8
    memset(out, 0, total * sizeof(Vertex));
    for (int32 j = 0; j < total / 8; j++)
    {
        const MeshXform &x = xf[j / (count / 8)];
        MeshXform part = x;
        if (part.normals) {
            part.normals += (j % (count / 8)) * 8 * 4;
        }
        jobs[j].batches = meshDspFill(jobs[j], part, src + j * 8, out + j * 8, 8);
    }
    jobs[total / 8].batches = 0;
    t0 = timerTicks();
    meshDspStart(jobs);
    while (dspBusy());
    shortTicks = (timerTicks() - t0) & 0xFFFF;
    cachePurge();
    for (int32 i = 0; i < total; i++) {
        errors += memcmp(ref + i, out + i, sizeof(Vertex)) != 0;
    }

    dspLoad(project_dsp, PROJECT_DSP_COUNT);   // the rooms' again
}
#endif
