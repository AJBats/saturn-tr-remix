// The playtest bot (host/host.cpp BOT=ROUTE): Lara played through a level by
// a route of goals, each met by closed-loop control (she is steered by what
// the game says of her each tick, so a route keeps working as the logic
// changes) and, where a move needs it, by trials: the host forks, the child
// tries a variant and reports, and the parent plays the best one for real.
// The run's pad, tick by tick, is written as a tick replay (tools/replay.py)
// that a replay build plays the same on Mednafen and the Saturn.
//
// A route, one step a line ('#' starts a comment):
//   start LEVEL newgame|kit       the level and how it starts (as a replay's)
//   goto ROOM X Z [walk] [RADIUS] to that spot (within RADIUS units, 96 if none)
//   goto3 ROOM X Y Z [RADIUS]     to the floor at height Y there (a bridge's top)
//   face DEGREES                  turned to that heading (0 is +z, 90 is +x)
//   keys KEYS TICKS               those keys held (UP+A ...), for a move by hand
//   wait TICKS                    no key held
//   pickup ITEM                   walked to it and picked it up
//   switch ITEM                   stood at it and pulled it
//   lever ITEM                    an underwater lever, swum to and pulled
//   usehole ITEM                  its key or puzzle item put in, from the ring
//   collapse ITEM [DEG]           a break-away tile brought down (stepped onto heading DEG)
//   timed ROOM X Z [MAXWAIT]      to that spot past swinging blades: each wait
//                                 first tried, the one losing least health played
//   push|pull ITEM DEGREES TIMES  a movable block moved that way, a sector a time
//   swimto ROOM X Y Z             to a spot in the water
//   draw                          guns out
//   endlevel ROOM X Z             to the level's end there, through its
//                                 statistics and "Save Game?" to the next level
//   use small|big                 a medipack from the ring (also used by
//                                 themselves when she's low)
//   safe ITEM [ROOM X Z]          that creature (awake) shot dead from a spot
//                                 it can't reach, that one or the bot's pick
//   fight [RANGE]                 guns drawn, every creature awake within
//                                 RANGE (4096 if none) shot dead
//   attempt ROOM X Z [TRIES]      the lines to `end` (keys, wait, face, goto)
//   ...                           tried with their keys' ticks varied, till
//   end                           she stands on that sector (a move by hand)
//   expect room ROOM | secrets N | pickups N | kills N | alive
//                                 the run fails unless it is so

#include <sys/mman.h>
#include <sys/wait.h>
#include <math.h>

namespace bot {

#define BOT_MAX_TICKS   (30 * 60 * 120)     // two hours of play a level
#define BOT_TRIAL_TICKS 8192

static uint32 gLog[BOT_MAX_TICKS];  // the run's keys, by tick
static int32 gTick;                 // the ticks run
static int32 gLine;                 // the route's line being played
static int32 gDepth;                // trials inside trials

// what a trial's child reports to its parent
struct TrialOut
{
    int32 score;                    // < 0: failed
    int32 ticks;
    uint32 keys[BOT_TRIAL_TICKS];
};
static TrialOut* gTrialOut[4];
static uint32 gJumpBest[BOT_TRIAL_TICKS];

static Lara* lara()
{
    return players[0];
}

// A tick with these keys held.
static void step(uint32 k)
{
    if (gTick >= BOT_MAX_TICKS) {
        fprintf(stderr, "bot: out of ticks\n");
        exit(2);
    }
    hostTick(gTick, k);
    gLog[gTick++] = k;
    // BOT_TRACE=N: Lara every N ticks, her guns, the creatures awake, and
    // WATCH=ITEM,ITEM... those items
    static int32 every = getenv("BOT_TRACE") ? atoi(getenv("BOT_TRACE")) : 0;
    if (every && gTick % every == 0 && players[0])
    {
        const Lara* l = players[0];
        const ExtraInfoLara* e = l->extraL;
        printf("  tick %5d keys %04x room %3d pos %6d %6d %6d yaw %5d pitch %6d state %2d anim %3d:%d speed %3d vspeed %3d water %d "
               "health %4d gun %d/%d target %d aim %d arm %d:%d\n",
               gTick, k, int32(l->room - rooms), l->pos.x, l->pos.y, l->pos.z, uint16(l->angle.y), l->angle.x, l->state, l->animIndex,
               l->frameIndex, l->hSpeed, l->vSpeed, l->waterState, l->health, e->weapon, e->weaponState,
               e->armR.target ? int32(e->armR.target - items) : -1, e->armR.aim, e->armR.animIndex, e->armR.frameIndex);
        if (l->state == Lara::STATE_HANG || getenv("BOT_CINFO")) {
            printf("     hang: front %d/%d left %d/%d right %d/%d mid %d/%d static %d type %d\n", cinfo.f.floor, cinfo.f.ceiling,
                   cinfo.l.floor, cinfo.l.ceiling, cinfo.r.floor, cinfo.r.ceiling, cinfo.m.floor, cinfo.m.ceiling, cinfo.staticHit,
                   cinfo.type);
        }
        for (const ItemObj* item = ItemObj::sFirstActive; item; item = item->nextActive)
        {
            if (item->type >= ITEM_WOLF && item->type <= ITEM_ADAM) {
                printf("     awake %3d: type %3d room %3d pos %6d %6d %6d state %2d health %d flags %04x\n", int32(item - items),
                       item->type, int32(item->room - rooms), item->pos.x, item->pos.y, item->pos.z, item->state, item->health,
                       item->flags);
            }
        }
        for (const char* w = getenv("WATCH"); w && *w; w = strchr(w, ',') ? strchr(w, ',') + 1 : "")
        {
            const ItemObj* item = items + atoi(w);
            printf("     item %3d: room %3d pos %6d %6d %6d state %2d health %d flags %04x\n", atoi(w),
                   item->room ? int32(item->room - rooms) : -1, item->pos.x, item->pos.y, item->pos.z, item->state, item->health,
                   item->flags);
        }
    }
}

static void fail(const char* why)
{
    const Lara* l = lara();
    printf("FAIL line %d tick %d: %s (room %d pos %d %d %d yaw %d state %d health %d)\n", gLine, gTick, why,
           l ? int32(l->room - rooms) : -1, l ? l->pos.x : 0, l ? l->pos.y : 0, l ? l->pos.z : 0,
           l ? uint16(l->angle.y) : 0, l ? l->state : -1, l ? l->health : 0);
}

// f run in a child from here on: its score (< 0 failed) and keys come back,
// this process as it was. play() then runs those keys here for real.
template <class F> int32 trial(F f, uint32* keys, int32* ticks)
{
    if (gDepth == X_COUNT(gTrialOut)) {
        fprintf(stderr, "bot: trials too deep\n");
        exit(2);
    }
    TrialOut* out = gTrialOut[gDepth];
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0)
    {
        int32 start = gTick;
        gDepth++;
        static bool childOut = getenv("BOT_CHILD_OUT") != NULL;     // debugging: the trials' traces too
        if (!childOut && !freopen("/dev/null", "w", stdout)) {
            _exit(2);
        }
        int32 score = f();
        out->ticks = X_MIN(gTick - start, BOT_TRIAL_TICKS);
        memcpy(out->keys, gLog + start, out->ticks * sizeof(uint32));
        out->score = score;
        _exit(0);
    }
    int status = 0;
    out->score = -1;
    out->ticks = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        fprintf(stderr, "bot: a trial's child ended badly (status %x) at tick %d\n", status, gTick);
        return -1;
    }
    if (keys) {
        memcpy(keys, out->keys, out->ticks * sizeof(uint32));
    }
    if (ticks) {
        *ticks = out->ticks;
    }
    return out->score;
}

static void play(const uint32* keys, int32 ticks)
{
    for (int32 i = 0; i < ticks; i++) {
        step(keys[i]);
    }
}

// Headings: the game's (0 is +z, 0x4000 +x), as phd_atan gives them.
static int16 headingTo(int32 x, int32 z)
{
    const Lara* l = lara();
    return int16(phd_atan(z - l->pos.z, x - l->pos.x));
}

static int32 distTo(int32 x, int32 z)
{
    const Lara* l = lara();
    double dx = x - l->pos.x, dz = z - l->pos.z;
    return int32(sqrt(dx * dx + dz * dz));
}

static bool standing()
{
    return lara()->state == Lara::STATE_STOP;
}

// No key till she stands still (a run or a pickup ends, a turn's speed runs
// out: it keeps turning her a few ticks after the key, into a run-up too) in
// a standing animation (a landing's own takes no key for 40 ticks).
static bool settle(int32 maxTicks = 90)
{
    for (int32 i = 0; i < maxTicks; i++)
    {
        int32 anim = lara()->animIndex;     // Lara's animations come first
        if (standing() && lara()->hSpeed == 0 && lara()->turnSpeed == 0 && (anim == Lara::ANIM_STAND || anim == Lara::ANIM_STAND_NORMAL))
            return true;
        step(0);
    }
    return standing();
}

// Turned on the spot to within tol of heading.
static bool turnTo(int16 heading, int32 tol = ANGLE(2))
{
    for (int32 tries = 0; tries < 40; tries++)
    {
        if (!settle())
            return false;
        int16 d = int16(heading - lara()->angle.y);
        if (abs(d) <= tol)
            return true;
        uint32 k = (d > 0) ? IK_RIGHT : IK_LEFT;
        if (abs(d) > ANGLE(20))
        {
            // held till near, then let go: the turn runs on a little
            for (int32 i = 0; i < 90 && abs(int16(heading - lara()->angle.y)) > ANGLE(12); i++) {
                step(k);
            }
        } else {
            // held n ticks: from standing a turn of 45 n (n + 1) / 2 units
            int32 n = 1;
            while (n < 12 && 45 * n * (n + 1) / 2 < abs(d) * 3 / 4) {
                n++;
            }
            for (int32 i = 0; i < n; i++) {
                step(k);
            }
        }
    }
    return false;
}

// Run (or walk) straight at a point, steering; true once within radius.
static ItemObj* nearestFoe(int32 range);
static bool gThreatsOnly;   // while she's on her way: nearestFoe() sees threats only
static bool gFleeing;       // on her way to a safe spot: nearestFoe() sees none

static bool goStraight(int32 x, int32 z, int32 radius, bool walk, int32 maxTicks = 30 * 120, bool wary = false)
{
    int32 best = distTo(x, z), since = 0;
    for (int32 i = 0; i < maxTicks; i++)
    {
        if (wary && nearestFoe(3072))
            return false;
        if (gNextLevel != LVL_MAX)
            return true;    // the level's end took over
        int32 dist = distTo(x, z);
        if (dist <= radius)
            return true;
        if (dist < best - 32) {
            best = dist;
            since = 0;
        } else if (++since > 45) {
            return false;   // stuck
        }
        int16 d = int16(headingTo(x, z) - lara()->angle.y);
        if (abs(d) > ANGLE(35))
        {
            if (!turnTo(headingTo(x, z), ANGLE(4)))
                return false;
            continue;
        }
        uint32 k = IK_UP | (walk ? IK_L : 0);
        if (d > ANGLE(2)) {
            k |= IK_RIGHT;
        } else if (d < -ANGLE(2)) {
            k |= IK_LEFT;
        }
        step(k);
    }
    return false;
}

// The level as Lara walks it: a grid of sectors (1024 units) with their
// floors as her collision reads them, slopes and all, rooms crossed through
// their portals. A sector beside another is a walk if its floor is within a
// click, a drop if it is up to BOT_MAX_DROP lower (run off), a climb if it is
// two clicks to seven and a half higher (stood at the wall, forward and
// Action). A straight run between two points is taken where every point
// along it, and either side of it by her width, is a walk from the last.
#define BOT_MAX_DROP    2048
#define BOT_SAFE_DROP   1664       // run off, she lands below 140 a tick: no harm (more: 66 at seven clicks, 395 at 7.5)
#define BOT_MAX_HANG_DROP 3584     // hung from the edge first: her height and reach off the fall
#define BOT_MAX_CLIMB   1920
#define BOT_CELLS       16384

struct Spot
{
    Room* room;
    int32 floor;    // WALL if none
    int32 ceiling;
};

// The floor and ceiling at x, z, reached from a room at height y.
static Spot spotAt(Room* from, int32 x, int32 y, int32 z)
{
    Spot sp;
    sp.room = from->getRoom(x, y, z);
    const Sector* sector = sp.room->getSector(x, z);
    sp.floor = sector->getFloor(x, y, z);
    sp.ceiling = sector->getCeiling(x, y, z);
    return sp;
}

// What stands in her way that the floor doesn't show: the rooms' static
// meshes that collide (statues, columns, the mummy) and save crystals, as
// boxes grown by her radius.
struct Obstacle
{
    int32 minX, maxX, minY, maxY, minZ, maxZ;
    // turned (a body): x, z also inside its own box, lx/lz, about cx, cz by
    // the turn's sin and cos (0, 0: not turned)
    int32 cx, cz, s, c;
    int32 lminX, lmaxX, lminZ, lmaxZ;
};
static Obstacle gObstacles[1024];
static int32 gObstaclesCount;

// Break-away tiles still up (shaking counts: it falls whoever is on it):
// the planner never has her stand on one.
struct Trap
{
    int32 sx, sz, top;
};
static Trap gTraps[256];
static int32 gTrapsCount;

static bool trapAt(int32 sx, int32 sz, int32 floor)
{
    for (int32 i = 0; i < gTrapsCount; i++)
    {
        const Trap &t = gTraps[i];
        if (t.sx == sx && t.sz == sz && abs(t.top - floor) < 64)
            return true;
    }
    return false;
}

// One obstacle in the way of her standing at x, z on that floor.
static bool inObstacle(const Obstacle &o, int32 x, int32 floor, int32 z)
{
    if (x <= o.minX || x >= o.maxX || z <= o.minZ || z >= o.maxZ || o.maxY <= floor - LARA_HEIGHT || o.minY >= floor - 64)
        return false;
    if (!o.s && !o.c)
        return true;
    int32 dx = x - o.cx, dz = z - o.cz;
    int32 lx = (dx * o.c - dz * o.s) >> FIXED_SHIFT, lz = (dz * o.c + dx * o.s) >> FIXED_SHIFT;
    return lx > o.lminX && lx < o.lmaxX && lz > o.lminZ && lz < o.lmaxZ;
}

static void obstaclesFind()
{
    gTrapsCount = 0;
    for (int32 i = 0; i < level.itemsCount && gTrapsCount < int32(X_COUNT(gTraps)); i++)
    {
        const ItemObj* item = items + i;
        if (item->type != ITEM_TRAP_FLOOR || !item->room || item->state > 1)
            continue;
        Trap &t = gTraps[gTrapsCount++];
        t.sx = item->pos.x >> 10;
        t.sz = item->pos.z >> 10;
        t.top = item->pos.y - 512;
    }
    gObstaclesCount = 0;
    for (int32 r = 0; r < level.roomsCount; r++)
    {
        const Room &room = rooms[r];
        for (int32 i = 0; i < room.info->meshesCount && gObstaclesCount < int32(X_COUNT(gObstacles)); i++)
        {
            const RoomMesh* mesh = room.data.meshes + i;
            const StaticMesh* sm = level.staticMeshes + STATIC_MESH_ID(mesh->zf);
            if (sm->flags & STATIC_MESH_FLAG_NO_COLLISION)
                continue;
            AABBi box(sm->cbox);
            boxRotateYQ(box, STATIC_MESH_QUADRANT(mesh->zf));
            int32 x = (room.info->x << 8) + (int32(mesh->xy) >> 16);
            int32 y = int32(mesh->xy) << 16 >> 16;
            int32 z = (room.info->z << 8) + (int32(mesh->zf) >> 16);
            if ((box.maxX - box.minX < 32 || box.maxZ - box.minZ < 32) && getenv("BOT_THIN") == NULL)
                continue;
            Obstacle &o = gObstacles[gObstaclesCount++] = Obstacle();
            o.minX = x + box.minX - LARA_RADIUS;
            o.maxX = x + box.maxX + LARA_RADIUS;
            o.minY = y + box.minY;
            o.maxY = y + box.maxY;
            o.minZ = z + box.minZ - LARA_RADIUS;
            o.maxZ = z + box.maxZ + LARA_RADIUS;
        }
    }
    for (int32 i = 0; i < level.itemsCount && gObstaclesCount < int32(X_COUNT(gObstacles)); i++)
    {
        const ItemObj* item = items + i;
        if (item->type != ITEM_CRYSTAL || !item->room)
            continue;
        Obstacle &o = gObstacles[gObstaclesCount++] = Obstacle();
        o.minX = item->pos.x - 160 - LARA_RADIUS;
        o.maxX = item->pos.x + 160 + LARA_RADIUS;
        o.minY = item->pos.y - 1024;
        o.maxY = item->pos.y;
        o.minZ = item->pos.z - 160 - LARA_RADIUS;
        o.maxZ = item->pos.z + 160 + LARA_RADIUS;
    }
    // the big dead: a body pushes her to the edge of its box (retail's
    // CreatureCollision), and one too long to be pushed round (the T-rex's)
    // stalls her walking into it; small ones she slides past
    for (int32 i = 0; i < level.itemsCount && gObstaclesCount < int32(X_COUNT(gObstacles)); i++)
    {
        const ItemObj* item = items + i;
        if (item->type < ITEM_WOLF || item->type > ITEM_ADAM || item->type == ITEM_SKATEBOARD || !item->room || item->health > 0)
            continue;
        const AABBs &box = item->getBoundingBox(false);
        if (X_MAX(box.maxX - box.minX, box.maxZ - box.minZ) < 1536)
            continue;
        int32 s = sinRetail(item->angle.y), c = cosRetail(item->angle.y);
        Obstacle &o = gObstacles[gObstaclesCount++] = Obstacle();
        o.cx = item->pos.x;
        o.cz = item->pos.z;
        o.s = s;
        o.c = c;
        o.lminX = box.minX - 64;
        o.lmaxX = box.maxX + 64;
        o.lminZ = box.minZ - 64;
        o.lmaxZ = box.maxZ + 64;
        o.minX = o.minZ = 0x7FFFFFFF;
        o.maxX = o.maxZ = -0x7FFFFFFF;
        for (int32 k = 0; k < 4; k++)
        {
            int32 lx = (k & 1) ? box.maxX : box.minX, lz = (k & 2) ? box.maxZ : box.minZ;
            int32 wx = item->pos.x + ((lx * c + lz * s) >> FIXED_SHIFT);
            int32 wz = item->pos.z + ((lz * c - lx * s) >> FIXED_SHIFT);
            o.minX = X_MIN(o.minX, wx - 64);
            o.maxX = X_MAX(o.maxX, wx + 64);
            o.minZ = X_MIN(o.minZ, wz - 64);
            o.maxZ = X_MAX(o.maxZ, wz + 64);
        }
        o.minY = item->pos.y + box.minY;
        o.maxY = item->pos.y + box.maxY;
        // her own spot in its box: what stops her is its body, not all its
        // box (a tail's reach), so there's room there; this plan goes without it
        if (lara() && inObstacle(o, lara()->pos.x, lara()->pos.y, lara()->pos.z)) {
            gObstaclesCount--;
        }
    }
}

// In the way of her standing at x, z on that floor.
static bool blocked(int32 x, int32 floor, int32 z)
{
    for (int32 i = 0; i < gObstaclesCount; i++)
    {
        if (inObstacle(gObstacles[i], x, floor, z))
            return true;
    }
    return false;
}

struct Cell
{
    int16 sx, sz;
    int32 floor;    // a water cell's: its water's bottom
    int32 top;      // a water cell's: its water's top (the surface, or a ceiling)
    bool water;
    bool air;       // a water cell's: air above its top (a surface to come up to)
    Room* room;
    int32 cost, from;
    int8 move;      // how it was reached from `from` (Move)
    bool done;
};

static Cell gCells[BOT_CELLS];
static int32 gCellsCount;
static int32 gCellHash[BOT_CELLS * 2];

static int32 cellFind(int32 sx, int32 sz, int32 floor, bool add, Room* room, bool water = false)
{
    uint32 h = (uint32(sx) * 73856093u ^ uint32(sz) * 19349663u ^ uint32(floor >> 7) * 83492791u) & (BOT_CELLS * 2 - 1);
    while (gCellHash[h] >= 0)
    {
        const Cell &c = gCells[gCellHash[h]];
        if (c.sx == sx && c.sz == sz && abs(c.floor - floor) < 128 && c.water == water)
            return gCellHash[h];
        h = (h + 1) & (BOT_CELLS * 2 - 1);
    }
    if (!add || gCellsCount == BOT_CELLS)
        return -1;
    Cell &c = gCells[gCellsCount];
    c.sx = int16(sx);
    c.sz = int16(sz);
    c.floor = floor;
    c.top = floor;
    c.water = water;
    c.air = false;
    c.room = room->getRoom((sx << 10) + 512, floor - 64, (sz << 10) + 512);    // the room she stands in there
    c.cost = 0x7FFFFFFF;
    c.from = -1;
    c.move = -1;
    c.done = false;
    gCellHash[h] = gCellsCount;
    return gCellsCount++;
}

enum Move { MOVE_WALK, MOVE_DROP, MOVE_HANG_DROP, MOVE_CLIMB, MOVE_JUMP, MOVE_WATER_IN, MOVE_SWIM, MOVE_WATER_OUT, MOVE_NONE };

// A slope too steep to stand on: the floor's slant there more than two clicks
// a sector either way, as Lara slides (her collision reads the same slant).
static bool steepAt(Room* room, int32 x, int32 y, int32 z)
{
    Room* r = room->getRoom(x, y, z);
    if (r->getSector(x, z)->getFloor(x, y, z) == WALL)
        return false;
    return abs(FD_SLANT_X(gLastFloorSlant)) > 2 || abs(FD_SLANT_Z(gLastFloorSlant)) > 2;
}

// How Lara gets from a's sector to its neighbour at b's: b filled in. Onto a
// steep slope is a drop (she slides down it), never up one.
static Move moveBetweenFlat(const Cell &a, int32 dx, int32 dz, Spot &b);

static Move moveBetween(const Cell &a, int32 dx, int32 dz, Spot &b)
{
    Move m = moveBetweenFlat(a, dx, dz, b);
    if (m == MOVE_WALK || m == MOVE_CLIMB)
    {
        int32 nx = (a.sx << 10) + 512 + dx * 1024, nz = (a.sz << 10) + 512 + dz * 1024;
        if (steepAt(b.room, nx, b.floor - 640, nz))
            return (m == MOVE_WALK && b.floor >= a.floor - 64) ? MOVE_DROP : MOVE_NONE;
    }
    return m;
}

static Move moveBetweenFlat(const Cell &a, int32 dx, int32 dz, Spot &b)
{
    int32 cx = (a.sx << 10) + 512, cz = (a.sz << 10) + 512;
    int32 nx = cx + dx * 1024, nz = cz + dz * 1024;
    int32 y = a.floor - 640;     // above a bridge up to 2.5 clicks higher, so its floor is seen
    // the side between them first: a wall there or a step not seen at the centres
    Spot side = spotAt(a.room, cx + dx * 448, y, cz + dz * 448);
    if (side.floor == WALL)
        return MOVE_NONE;
    Spot edge = spotAt(side.room, cx + dx * 576, y, cz + dz * 576);
    b = spotAt(edge.room, nx, y, nz);
    if (edge.floor == WALL || b.floor == WALL)
        return MOVE_NONE;
    if (b.floor - b.ceiling < LARA_HEIGHT)
        return MOVE_NONE;
    int32 up = a.floor - X_MIN(b.floor, edge.floor);   // > 0: higher
    int32 down = b.floor - a.floor;
    if (abs(side.floor - a.floor) <= 256 && abs(edge.floor - side.floor) <= 256 && abs(b.floor - edge.floor) <= 256)
        return MOVE_WALK;
    if (up > 256 && up <= BOT_MAX_CLIMB && abs(b.floor - edge.floor) <= 256 && b.floor - b.ceiling >= LARA_HEIGHT)
    {
        int32 ex = cx + dx * 544, ez = cz + dz * 544;
        Spot l = spotAt(edge.room, ex + dz * 72, y - 2048, ez + dx * 72);
        Spot r = spotAt(edge.room, ex - dz * 72, y - 2048, ez - dx * 72);
        if (l.floor != WALL && r.floor != WALL && abs(l.floor - r.floor) < LARA_HANG_SLANT)
            return MOVE_CLIMB;
        return MOVE_NONE;
    }
    // the wall a little past the sides (a rock's lip): her floor runs on, the
    // higher one seen a third of a sector into the neighbour
    if (abs(side.floor - a.floor) <= 256 && abs(edge.floor - side.floor) <= 256 && b.floor < a.floor - 256 &&
        a.floor - b.floor <= BOT_MAX_CLIMB && b.floor - b.ceiling >= LARA_HEIGHT)
    {
        Spot far = spotAt(edge.room, cx + dx * 832, y, cz + dz * 832);
        if (far.floor != WALL && abs(b.floor - far.floor) <= 256)
        {
            int32 ex = cx + dx * 800, ez = cz + dz * 800;
            Spot l = spotAt(far.room, ex + dz * 72, y - 2048, ez + dx * 72);
            Spot r = spotAt(far.room, ex - dz * 72, y - 2048, ez - dx * 72);
            if (l.floor != WALL && r.floor != WALL && abs(l.floor - r.floor) < LARA_HANG_SLANT)
                return MOVE_CLIMB;
        }
    }
    // a floor under water is no floor to drop onto (waterMoves: into the water)
    if (ROOM_FLAG_WATER(b.room->getRoom(nx, b.floor - 32, nz)->info->flags))
        return MOVE_NONE;
    // a fall that would hurt from a level edge is hung from first
    bool level = abs(side.floor - a.floor) <= 64;
    if (down > BOT_SAFE_DROP && down <= BOT_MAX_HANG_DROP && level && edge.floor >= side.floor - 256)
        return MOVE_HANG_DROP;
    if (down > 256 && down <= BOT_MAX_DROP && edge.floor >= side.floor - 256)
        return MOVE_DROP;
    return MOVE_NONE;
}

// A climb onto a floor above the neighbour's, seen from above (a bridge, a
// ledge over open floor): the highest floor there within reach, level across
// its edge, with room to stand on; b filled in.
static bool climbBetween(const Cell &a, int32 dx, int32 dz, const Spot &low, Spot &b)
{
    int32 cx = (a.sx << 10) + 512, cz = (a.sz << 10) + 512;
    int32 y = a.floor - 640;     // above a bridge up to 2.5 clicks higher, so its floor is seen
    Spot side = spotAt(a.room, cx + dx * 448, y, cz + dz * 448);
    if (side.floor == WALL || abs(side.floor - a.floor) > 64)
        return false;
    int32 high = a.floor - BOT_MAX_CLIMB - 64;
    Spot over = spotAt(a.room, cx + dx * 448, high, cz + dz * 448);
    if (abs(over.floor - a.floor) > 64)
        return false;   // something over her at the edge (under a bridge): no wall to climb
    Spot hs = spotAt(over.room, cx + dx * 576, high, cz + dz * 576);
    if (hs.floor == WALL)
        return false;
    b = spotAt(hs.room, cx + dx * 1024, high, cz + dz * 1024);
    if (b.floor == WALL || b.floor > a.floor - 384 || b.floor < a.floor - BOT_MAX_CLIMB)
        return false;
    if (low.floor != WALL && b.floor >= low.floor - 256)
        return false;   // the same floor the low look sees: moveBetween's
    if (abs(hs.floor - b.floor) > 256 || b.floor - b.ceiling < LARA_HEIGHT)
        return false;
    int32 ex = cx + dx * 544, ez = cz + dz * 544;
    Spot l = spotAt(hs.room, ex + dz * 72, high, ez + dx * 72);
    Spot r = spotAt(hs.room, ex - dz * 72, high, ez - dx * 72);
    return l.floor != WALL && r.floor != WALL && abs(l.floor - r.floor) < LARA_HANG_SLANT;
}

// The stretch of water at x, y, z, reached from a room at height y: its
// bottom, its top (up through the water rooms above), and whether there's air
// over that top. False if there's no water there.
struct WaterCol
{
    Room* room;
    int32 top, bottom;
    bool air;
};

static bool waterAt(Room* from, int32 x, int32 y, int32 z, WaterCol &w)
{
    Room* room = from->getRoom(x, y, z);
    if (!ROOM_FLAG_WATER(room->info->flags))
        return false;
    const Sector* sector = room->getSector(x, z);
    if (sector->floor == NO_FLOOR)
        return false;
    w.room = room;
    w.bottom = sector->getFloor(x, y, z);
    Room* r = room;
    const Sector* sc = sector;
    while (sc->roomAbove != NO_ROOM && ROOM_FLAG_WATER(rooms[sc->roomAbove].info->flags))
    {
        r = rooms + sc->roomAbove;
        sc = r->getSector(x, z);
    }
    w.top = sc->ceiling << 8;
    w.air = sc->roomAbove != NO_ROOM;
    // a shut trapdoor over it (its own sector and the one ahead) is a lid, not air
    for (int32 i = 0; w.air && i < level.itemsCount; i++)
    {
        const ItemObj* item = items + i;
        if ((item->type == ITEM_TRAP_DOOR_1 || item->type == ITEM_TRAP_DOOR_2) && item->room &&
            item->state == 0 && item->getTrapDoorFloor(x, z) != WALL && abs(item->pos.y - w.top) < 512) {
            w.air = false;
        }
    }
    return w.bottom - w.top >= 400;
}

// The argument of that action in the trigger on the floor there; -1 if it
// has none.
static int32 triggerArg(Room* from, int32 x, int32 y, int32 z, int32 what)
{
    Room* room = from->getRoom(x, y, z);
    room->getSector(x, z)->getFloor(x, y, z);
    const FloorData* fd = gLastFloorData;
    if (!fd)
        return -1;
    if (FD_FLOOR_TYPE(*fd) == FLOOR_TYPE_LAVA)
    {
        if (FD_END(*fd))
            return -1;
        fd++;
    }
    uint16 cmd = *fd++;
    fd++;   // timer and mask
    if (FD_TRIGGER_TYPE(cmd) == TRIGGER_TYPE_SWITCH || FD_TRIGGER_TYPE(cmd) == TRIGGER_TYPE_KEY ||
        FD_TRIGGER_TYPE(cmd) == TRIGGER_TYPE_PICKUP) {
        fd++;
    }
    for (int32 i = 0; i < 16; i++)
    {
        uint16 action = *fd++;
        if (FD_ACTION(action) == what)
            return FD_ARGS(action);
        if (FD_ACTION(action) == TRIGGER_ACTION_ACTIVATE_CAMERA) {
            action = *fd++;     // the camera's own word ends the list if any does
        }
        if (FD_END(action))
            break;
    }
    return -1;
}

// A current not with her way into the water at x, z (heading dx, dz): a
// flow trigger on its floor carries her to its sink (a stream over its
// falls), so she swims only the way it goes (within 60 degrees of its sink),
// never across or against it.
static bool againstFlow(Room* from, int32 x, int32 y, int32 z, int32 dx, int32 dz)
{
    int32 sink = triggerArg(from, x, y, z, TRIGGER_ACTION_FLOW);
    if (sink < 0 || getenv("BOT_NOFLOW"))
        return false;
    const FixedCamera &cam = level.cameras[sink];
    double vx = cam.pos.x - x, vz = cam.pos.z - z;
    return vx * dx + vz * dz <= 0.5 * sqrt(vx * vx + vz * vz);
}

// moves that failed when tried, left out of the next plan
struct Blocked
{
    int16 sx, sz, dx, dz;
};
static Blocked gBlocked[64];
static int32 gBlockedCount;

static bool isBlocked(const Cell &a, int32 dx, int32 dz)
{
    for (int32 i = 0; i < gBlockedCount; i++)
    {
        const Blocked &b = gBlocked[i];
        if (b.sx == a.sx && b.sz == a.sz && b.dx == dx && b.dz == dz)
            return true;
    }
    return false;
}

static void block(const Cell &a, int32 dx, int32 dz)
{
    if (gBlockedCount < int32(X_COUNT(gBlocked)))
    {
        Blocked &b = gBlocked[gBlockedCount++];
        b.sx = a.sx;
        b.sz = a.sz;
        b.dx = int16(dx);
        b.dz = int16(dz);
    }
}

static const int32 kDirX[4] = { 1, -1, 0, 0 };
static const int32 kDirZ[4] = { 0, 0, 1, -1 };
// jumps: along the grid, then diagonally
static const int32 kJumpX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int32 kJumpZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

// A jump from a's sector across `gap` sectors of pit (open above her) onto
// the sector beyond, along the grid or diagonally: b filled in. Along the
// grid the far floor can be up to five clicks higher (grabbed and pulled up
// onto) or lower; diagonally no more than a click higher, with the two
// sectors beside every corner she crosses open and no higher than either
// end. `high`: the far floor looked for from five and a half clicks up, as a
// ledge over open floor is seen (from below, its underside is a ceiling).
static bool jumpBetween(const Cell &a, int32 dx, int32 dz, int32 gap, Spot &b, bool high = false)
{
    int32 cx = (a.sx << 10) + 512, cz = (a.sz << 10) + 512;
    int32 y = a.floor - 640;     // above a bridge up to 2.5 clicks higher, so its floor is seen
    Room* way[6];                // the room of each sector along the way
    way[0] = a.room;
    if (gap > 5 || (dx && dz && gap > 2))
        return false;
    Spot side = spotAt(a.room, cx + dx * 448, y, cz + dz * 448);
    if (side.floor == WALL || abs(side.floor - a.floor) > 256)
        return false;
    // break-away tiles level with her first (run across: along the grid,
    // two at most), then no more than three sectors of pit (two diagonally)
    Room* room = side.room;
    int32 tiles = 0;
    for (int32 g = 1; g <= gap; g++)
    {
        Spot mid = spotAt(room, cx + dx * 1024 * g, y, cz + dz * 1024 * g);
        if (mid.floor == WALL || mid.ceiling > a.floor - 1280)
            return false;
        if (mid.floor >= a.floor - 64 && mid.floor <= a.floor + 64 && trapAt(a.sx + dx * g, a.sz + dz * g, mid.floor) &&
            tiles == g - 1 && tiles < 2 && !(dx && dz)) {
            tiles++;
        } else if (mid.floor <= a.floor + 512) {
            return false;
        }
        room = way[g] = mid.room;
    }
    if (gap - tiles > 3 || (dx && dz && gap > 2))
        return false;
    if (high)
    {
        int32 yh = a.floor - 1408;
        room = spotAt(a.room, cx, yh, cz).room;
        for (int32 g = 1; g <= gap; g++)
        {
            Spot mid = spotAt(room, cx + dx * 1024 * g, yh, cz + dz * 1024 * g);
            if (mid.floor == WALL)
                return false;
            room = mid.room;
        }
        b = spotAt(room, cx + dx * 1024 * (gap + 1), yh, cz + dz * 1024 * (gap + 1));
    } else {
        b = spotAt(room, cx + dx * 1024 * (gap + 1), y, cz + dz * 1024 * (gap + 1));
    }
    if (b.floor == WALL || b.floor < a.floor - 1280 || b.floor > a.floor + 1280 || b.floor - b.ceiling < LARA_HEIGHT)
        return false;
    if (dx && dz)
    {
        if (b.floor < a.floor - 256)
            return false;
        int32 top = X_MIN(a.floor, b.floor);
        for (int32 k = 0; k <= gap; k++)
        {
            for (int32 s = 0; s < 2; s++)
            {
                int32 nx = cx + dx * 1024 * (k + (s == 0)), nz = cz + dz * 1024 * (k + (s == 1));
                Spot n = spotAt(way[k], nx, y, nz);
                if (n.floor == WALL || n.floor < top - 64 || n.ceiling > top - LARA_HEIGHT - 256)
                    return false;
            }
        }
    }
    return true;
}

static void reach(int32 c, int32 n, int32 cost, Move m)
{
    if (n < 0 || gCells[n].done)
        return;
    if (gCells[c].cost + cost < gCells[n].cost)
    {
        gCells[n].cost = gCells[c].cost + cost;
        gCells[n].from = c;
        gCells[n].move = int8(m);
    }
}

// From a land cell into water beside it (a surface below her feet, up to
// four sectors down); from a water cell on along the water (a stretch beside
// it meeting this one by half a sector of height), or out onto a floor up
// to two clicks over the surface. True if c is a water cell (no land moves).
static bool waterMoves(int32 c, int32 gsx, int32 gsz)
{
    const Cell a = gCells[c];
    int32 cx = (a.sx << 10) + 512, cz = (a.sz << 10) + 512;
    for (int32 d = 0; d < 4; d++)
    {
        int32 dx = kDirX[d], dz = kDirZ[d];
        int32 nsx = a.sx + dx, nsz = a.sz + dz;
        int32 nx = cx + dx * 1024, nz = cz + dz * 1024;
        if (!a.water)
        {
            WaterCol w;
            Spot side = spotAt(a.room, cx + dx * 448, a.floor - 256, cz + dz * 448);
            if (side.floor == WALL)
                continue;
            // water below her feet there: looked for a click at a time down to four sectors
            bool wet = false;
            for (int32 y = a.floor + 128; y <= a.floor + 4096 && !wet; y += 256) {
                wet = waterAt(side.room, nx, y, nz, w);
            }
            if (!wet || !w.air)
                continue;
            if (w.top < a.floor - 64 || w.top > a.floor + 4096 || againstFlow(w.room, nx, w.bottom - 64, nz, dx, dz))
                continue;
            int32 n = cellFind(nsx, nsz, w.bottom, true, w.room, true);
            if (n >= 0) {
                gCells[n].top = w.top;
                gCells[n].air = w.air;
            }
            reach(c, n, 2048, MOVE_WATER_IN);
            continue;
        }
        // on through the water: the neighbour's stretch looked for at heights down this one
        // (every stretch down the neighbour's column: a tunnel can run under a hall)
        for (int32 y = a.top + 200; y < a.floor - 100; y += 256)
        {
            Spot side = spotAt(a.room, cx + dx * 448, y, cz + dz * 448);
            if (side.floor == WALL)
                continue;
            WaterCol w;
            if (!waterAt(side.room, nx, y, nz, w))
                continue;
            int32 lo = X_MAX(w.top, a.top), hi = X_MIN(w.bottom, a.floor);
            y = X_MAX(y, w.bottom - 256);   // on below this stretch
            if (hi - lo < 300 || againstFlow(w.room, nx, w.bottom - 64, nz, dx, dz))     // room to swim through: a sloping tunnel overlaps by less than its height
                continue;
            int32 n = cellFind(nsx, nsz, w.bottom, true, w.room, true);
            if (n >= 0) {
                gCells[n].top = w.top;
                gCells[n].air = w.air;
            }
            reach(c, n, 1024, MOVE_SWIM);
        }
        // out onto a floor beside the surface
        if (a.air)
        {
            // looked for from the room over the surface (a shaft up from a
            // tunnel opens into a room the water's own room doesn't reach)
            Room* air = a.room->getRoom(cx, a.top - 64, cz);
            Spot b = spotAt(air, nx, a.top - 640, nz);
            Spot edge = spotAt(air, cx + dx * 576, a.top - 640, cz + dz * 576);
            if (b.floor != WALL && edge.floor != WALL && !ROOM_FLAG_WATER(b.room->info->flags) &&
                b.floor >= a.top - 512 && b.floor <= a.top + 100 && b.floor - b.ceiling >= LARA_HEIGHT &&
                !blocked(nx, b.floor, nz) && !trapAt(nsx, nsz, b.floor)) {
                reach(c, cellFind(nsx, nsz, b.floor, true, b.room), 2048, MOVE_WATER_OUT);
            }
        }
    }
    return a.water;
}

// The sectors from Lara's to the one holding x, z in a room, the cheapest way
// (walks by their length, drops and climbs dearer); none: 0.
// A goal in the water: its height (NO_GOAL_Y: a goal on a floor).
#define NO_GOAL_Y (-0x7FFFFFFF)
static int32 gGoalY = NO_GOAL_Y;
static int32 gGoalFloorY = NO_GOAL_Y;   // a goal on a floor at this height (a bridge, a ledge over a floor); none: the room's middle's
static bool gGoalAir;   // the goal: the nearest water with air over it

static int32 sectorPath(int32 roomIndex, int32 x, int32 z, int32* path, int32 maxLen)
{
    Lara* l = lara();
    Room* goalRoom = rooms + roomIndex;
    Spot goal = spotAt(goalRoom, x, gGoalFloorY != NO_GOAL_Y ? gGoalFloorY - 128 : (goalRoom->info->yTop + goalRoom->info->yBottom) / 2, z);
    WaterCol gw;
    bool waterGoal = gGoalY != NO_GOAL_Y && waterAt(goalRoom, x, gGoalY, z, gw);
    gCellsCount = 0;
    for (int32 i = 0; i < BOT_CELLS * 2; i++) {
        gCellHash[i] = -1;
    }
    int32 start;
    WaterCol sw;
    if (l->waterState != WATER_STATE_ABOVE && waterAt(l->room, l->pos.x, l->pos.y, l->pos.z, sw))
    {
        start = cellFind(l->pos.x >> 10, l->pos.z >> 10, sw.bottom, true, sw.room, true);
        gCells[start].top = sw.top;
        gCells[start].air = sw.air;
    } else {
        start = cellFind(l->pos.x >> 10, l->pos.z >> 10, l->pos.y, true, l->room);
    }
    gCells[start].cost = 0;
    int32 gsx = x >> 10, gsz = z >> 10, found = -1;
    while (1)
    {
        int32 c = -1, best = 0x7FFFFFFF;
        for (int32 i = 0; i < gCellsCount; i++)
        {
            const Cell &cl = gCells[i];
            if (cl.done || cl.cost == 0x7FFFFFFF)
                continue;
            int32 f = cl.cost + (gGoalAir ? 0 : (abs(cl.sx - gsx) + abs(cl.sz - gsz)) * 1024);
            if (f < best) {
                best = f;
                c = i;
            }
        }
        if (c < 0)
            break;
        gCells[c].done = true;
        bool atGoal = waterGoal ? (gCells[c].water && abs(gCells[c].floor - gw.bottom) < 128)
                                : (abs(gCells[c].floor - goal.floor) < 384 && !gCells[c].water);
        if (gGoalAir ? (gCells[c].water && gCells[c].air) : (gCells[c].sx == gsx && gCells[c].sz == gsz && atGoal)) {
            found = c;
            break;
        }
        if (waterMoves(c, gsx, gsz))
            continue;
        for (int32 d = 0; d < 4; d++)
        {
            for (int32 look = 0; look < 2; look++)
            {
                Spot low, b;
                Move m = moveBetween(gCells[c], kDirX[d], kDirZ[d], low);
                b = low;
                if (look == 1)
                {
                    if (!climbBetween(gCells[c], kDirX[d], kDirZ[d], low, b))
                        continue;
                    m = MOVE_CLIMB;
                }
                if (m == MOVE_NONE || (m != MOVE_WALK && isBlocked(gCells[c], kDirX[d], kDirZ[d])))
                    continue;
                int32 nsx = gCells[c].sx + kDirX[d], nsz = gCells[c].sz + kDirZ[d];
                if ((blocked((nsx << 10) + 512, b.floor, (nsz << 10) + 512) || trapAt(nsx, nsz, b.floor)) && !(nsx == gsx && nsz == gsz))
                    continue;
                // a wall of a static between them (a bridge's rail): no way across
                // (looked for from her own spot in the sector she starts in, if a
                // static stands in its middle)
                int32 ox = (gCells[c].sx << 10) + 512, oz = (gCells[c].sz << 10) + 512;
                if (gCells[c].cost == 0 && blocked(ox, gCells[c].floor, oz)) {
                    ox = lara()->pos.x;
                    oz = lara()->pos.z;
                }
                bool across = false;
                for (int32 t = 128; t < 1024 && !across; t += 128) {
                    across = blocked(ox + kDirX[d] * t, X_MIN(b.floor, gCells[c].floor), oz + kDirZ[d] * t);
                }
                if (across && m != MOVE_CLIMB && !(nsx == gsx && nsz == gsz))
                    continue;
                int32 n = cellFind(nsx, nsz, b.floor, true, b.room);
                if (n < 0 || gCells[n].done)
                    continue;
                int32 cost = gCells[c].cost + 1024;
                if (m == MOVE_DROP) {
                    cost += 2048 + (b.floor - gCells[c].floor) + (b.floor - gCells[c].floor > BOT_SAFE_DROP ? 16384 : 0);
                } else if (m == MOVE_HANG_DROP) {
                    cost += 4096 + (b.floor - gCells[c].floor);
                } else if (m == MOVE_CLIMB) {
                    cost += 2048 + (gCells[c].floor - b.floor) * 2;
                }
                if (cost < gCells[n].cost) {
                    gCells[n].cost = cost;
                    gCells[n].from = c;
                    gCells[n].move = int8(m);
                }
            }
        }
        for (int32 d = 0; d < 8; d++)
        {
            for (int32 gap = 1; gap <= 5; gap++)
            {
                int32 lowFloor = WALL;
                for (int32 look = 0; look < 2; look++)
                {
                Spot b;
                if (!jumpBetween(gCells[c], kJumpX[d], kJumpZ[d], gap, b, look == 1) ||
                    isBlocked(gCells[c], kJumpX[d] * (gap + 1), kJumpZ[d] * (gap + 1)))
                    continue;
                if (look == 0) {
                    lowFloor = b.floor;
                } else if (abs(b.floor - lowFloor) < 128) {
                    continue;   // the same floor both looks see
                }
                int32 jsx = gCells[c].sx + kJumpX[d] * (gap + 1), jsz = gCells[c].sz + kJumpZ[d] * (gap + 1);
                if (blocked((jsx << 10) + 512, b.floor, (jsz << 10) + 512) || (trapAt(jsx, jsz, b.floor) && !(jsx == gsx && jsz == gsz)))
                    continue;
                int32 n = cellFind(jsx, jsz, b.floor, true, b.room);
                if (n < 0 || gCells[n].done)
                    continue;
                // a jump takes as long as a run of eight sectors or so (the run-up, the
                // landing)
                int32 cost = gCells[c].cost + 8192 + gap * ((kJumpX[d] && kJumpZ[d]) ? 1448 : 1024);
                if (cost < gCells[n].cost) {
                    gCells[n].cost = cost;
                    gCells[n].from = c;
                    gCells[n].move = MOVE_JUMP;
                }
                }
            }
        }
    }
    static bool plan = getenv("BOT_PLAN") != NULL;
    if (plan) {
        printf("  plan: %d cells looked at, %s\n", gCellsCount, found < 0 ? "no way" : "a way");
    }
    if (found < 0)
    {
        if (plan && getenv("BOT_PLAN")[0] == '2')
        {
            for (int32 i = 0; i < gCellsCount; i++) {
                printf("    seen %d,%d floor %d room %d%s cost %d", gCells[i].sx << 10, gCells[i].sz << 10, gCells[i].floor,
                       int32(gCells[i].room - rooms), gCells[i].water ? " water" : "", gCells[i].cost);
                if (gCells[i].water) {
                    printf(" top %d%s", gCells[i].top, gCells[i].air ? " air" : "");
                }
                printf("\n");
            }
        }
        return 0;
    }
    int32 len = 0;
    for (int32 c = found; c >= 0; c = gCells[c].from) {
        len++;
    }
    if (plan) {
        for (int32 c = found; c >= 0; c = gCells[c].from) {
            printf("    %d,%d floor %d room %d move %d\n", gCells[c].sx << 10, gCells[c].sz << 10, gCells[c].floor,
                   int32(gCells[c].room - rooms), gCells[c].move);
        }
    }
    if (len > maxLen)
        return 0;
    for (int32 i = len - 1, c = found; i >= 0; i--, c = gCells[c].from) {
        path[i] = c;
    }
    return len;
}

// A straight walk from Lara to x, z: every 64 units along it, and her width
// either side, a walk from the point before.
static bool clearWalk(int32 x, int32 z)
{
    Lara* l = lara();
    int32 dx = x - l->pos.x, dz = z - l->pos.z;
    int32 len = int32(sqrt(double(dx) * dx + double(dz) * dz));
    if (len < 64)
        return true;
    int32 px = -dz * 128 / len, pz = dx * 128 / len;     // her width, across the way
    for (int32 side = -1; side <= 1; side++)
    {
        Room* room = l->room;
        int32 floor = l->pos.y;
        for (int32 t = 64; t <= len; t += 64)
        {
            int32 sx = l->pos.x + dx * t / len + px * side, sz = l->pos.z + dz * t / len + pz * side;
            Spot sp = spotAt(room, sx, floor - 256, sz);
            if (sp.floor == WALL || abs(sp.floor - floor) > 192 || sp.floor - sp.ceiling < LARA_HEIGHT)
                return false;
            if ((side == 0 && blocked(sx, sp.floor, sz)) || trapAt(sx >> 10, sz >> 10, sp.floor))
                return false;
            room = sp.room;
            floor = sp.floor;
        }
    }
    return true;
}

static int32 cellX(const Cell &c)
{
    return (c.sx << 10) + 512;
}

static int32 cellZ(const Cell &c)
{
    return (c.sz << 10) + 512;
}

// Up onto the sector beyond the side at x, z (heading across it): stood square
// at the wall, forward and Action held, as a player climbs (two or three
// clicks) or jumps up, grabs and pulls up (to seven and a half); or, at a
// ledge over open floor, walked to the edge first.
// Straight to x, z, round an obstacle in the way (a save crystal in a narrow
// passage): first aside, as far as there's floor, then on.
static bool goStraightAround(int32 x, int32 z, int32 radius, bool walk)
{
    const Lara* l = lara();
    int32 dx = x - l->pos.x, dz = z - l->pos.z;
    int32 len = int32(sqrt(double(dx) * dx + double(dz) * dz));
    bool hit = false;
    for (int32 t = 64; t < len && !hit; t += 64) {
        hit = blocked(l->pos.x + dx * t / len, l->pos.y, l->pos.z + dz * t / len);
    }
    if (hit && len > 0)
    {
        // aside on whichever side has floor level with hers, then on past it
        for (int32 side = -1; side <= 1; side += 2)
        {
            int32 px = l->pos.x - dz * 400 * side / len, pz = l->pos.z + dx * 400 * side / len;
            Spot sp = spotAt(l->room, px, l->pos.y - 256, pz);
            if (sp.floor == WALL || abs(sp.floor - l->pos.y) > 128 || blocked(px, sp.floor, pz))
                continue;
            if (goStraight(px, pz, 64, true) && settle() && goStraight(x - dz * 400 * side / len, z + dx * 400 * side / len, 96, true))
                break;
        }
    }
    return goStraight(x, z, radius, walk);
}

static bool climbAt(int16 heading, int32 x, int32 z, int32 floor)
{
    int32 bx = x - (sinRetail(heading) * 200 >> FIXED_SHIFT);
    int32 bz = z - (cosRetail(heading) * 200 >> FIXED_SHIFT);
    if (!goStraightAround(bx, bz, 64, true) || !settle() || !turnTo(heading, ANGLE(2))) {
        fail("couldn't stand at the climb");
        return false;
    }
    static uint32 keys[BOT_TRIAL_TICKS];
    for (int32 way = 0; way < 3; way++)
    {
        int32 ticks = 0;
        int32 score = trial([&]() -> int32 {
            if (way == 2)
            {
                // walked on till she stops at the edge, then straight up
                for (int32 i = 0; i < 60 && !(i > 4 && standing() && lara()->hSpeed == 0); i++) {
                    step(IK_UP | IK_L);
                }
            }
            for (int32 i = 0; i < 150; i++)
            {
                const Lara* l = lara();
                if (standing() && abs(l->pos.y - floor) < 320 && i > 10)
                    return l->health;
                if (l->health <= 0)
                    return -1;
                uint32 k = IK_UP | IK_B;
                if (way != 0) {
                    // straight up with Action, then forward to pull up once hanging
                    k = (l->state == Lara::STATE_HANG || l->state == Lara::STATE_HANG_UP) ? (IK_UP | IK_B) : (IK_A | IK_B);
                }
                step(k);
            }
            return -1;
        }, keys, &ticks);
        if (score >= 0)
        {
            play(keys, ticks);
            return true;
        }
    }
    fail("couldn't climb up");
    return false;
}

// One way of jumping from a's sector over the gap: from `back` units short of
// its edge, forward held `run` ticks before Jump is added (0: a standing
// jump), grabbing or not; true once she stands on the far sector.
static bool jumpTry(const Cell &a, int32 dx, int32 dz, int32 gap, int32 floor, int32 back, int32 run, bool grab)
{
    int16 heading = int16(phd_atan(dz, dx));
    // break-away tiles first: run across them (a sector in 22 ticks or so)
    for (int32 g = 1; g <= 2 && !(dx && dz); g++)
    {
        int32 tx = a.sx + dx * g, tz = a.sz + dz * g;
        Spot t = spotAt(a.room, (tx << 10) + 512, a.floor - 640, (tz << 10) + 512);
        if (t.floor == WALL || abs(t.floor - a.floor) > 64 || !trapAt(tx, tz, t.floor))
            break;
        run += 22;
    }
    bool diagonal = dx && dz;
    int32 ex = cellX(a) + dx * 440, ez = cellZ(a) + dz * 440;     // diagonally: near the corner
    int32 unit = diagonal ? 724 : 1024;
    int32 sx = ex - dx * back * unit / 1024, sz = ez - dz * back * unit / 1024;
    static bool tell = getenv("BOT_JUMPS") != NULL;
    // to the run-up's start by way of the sector's middle, square to the gap
    int32 stage = 0;
    if (!settle() || (stage = 1, !goStraight(cellX(a), cellZ(a), 64, true)) || (stage = 2, !settle()) ||
        (stage = 3, !goStraight(sx, sz, 48, true)) || (stage = 4, !settle()) || (stage = 5, !turnTo(heading, ANGLE(1))))
    {
        if (tell) {
            fprintf(stderr, "  no take-off at %d,%d (stage %d): Lara at %d,%d yaw %d state %d hspeed %d turn %d\n", sx, sz, stage,
                    lara()->pos.x, lara()->pos.z, uint16(lara()->angle.y), lara()->state, lara()->hSpeed, lara()->turnSpeed);
            fprintf(stderr, "     cinfo: front %d/%d left %d/%d right %d/%d mid %d/%d static %d type %d\n", cinfo.f.floor, cinfo.f.ceiling,
                    cinfo.l.floor, cinfo.l.ceiling, cinfo.r.floor, cinfo.r.ceiling, cinfo.m.floor, cinfo.m.ceiling, cinfo.staticHit, cinfo.type);
        }
        return false;
    }
    // landed: on the far side's floor, past the gap and no more than two
    // sectors further, within a sector of the line jumped along (measured in
    // sectors along and across it: a diagonal step counts two along)
    int32 near = (gap + 1) * (diagonal ? 2 : 1);
    for (int32 i = 0; i < 300; i++)
    {
        const Lara* l = lara();
        int32 ox = (l->pos.x >> 10) - a.sx, oz = (l->pos.z >> 10) - a.sz;
        int32 past = ox * dx + oz * dz - near, side = ox * dz - oz * dx;
        bool there = past >= (diagonal ? -1 : 0) && past <= (diagonal ? 3 : 2) && abs(side) <= 1 && abs(l->pos.y - floor) < 128;
        if (there && standing())
            return true;
        if (l->health <= 0)
            return false;
        uint32 k = (i < run) ? IK_UP : (IK_UP | IK_A | (grab ? IK_B : 0));
        // hanging: pull up; landed: let go to stand
        if (l->state == Lara::STATE_HANG) {
            k = IK_UP | IK_B;
        } else if (there) {
            k = 0;
        }
        step(k);
    }
    if (tell) {
        fprintf(stderr, "  not landed: Lara at %d,%d,%d room %d state %d health %d\n", lara()->pos.x, lara()->pos.y, lara()->pos.z,
                lara()->room ? int32(lara()->room - rooms) : -1, lara()->state, lara()->health);
    }
    return false;
}

static bool jumpAcross(const Cell &a, int32 dx, int32 dz, int32 gap, int32 floor)
{
    static const int32 kBack[] = { 0, 200, 400, 600, 800, 1000, 1200 };
    static const int32 kRun[] = { 0, 3, 6, 9, 12, 16 };
    static uint32 keys[BOT_TRIAL_TICKS];
    int32 bestScore = -1, bestTicks = 0;
    for (int32 grab = 0; grab < 2 && bestScore < 0; grab++)
    {
        for (int32 v = 0; v < int32(X_COUNT(kBack)); v++)
        {
            for (int32 r = 0; r < int32(X_COUNT(kRun)); r++)
            {
                int32 back = kBack[v], run = kRun[r];
                int32 ticks = 0;
                int32 score = trial([&]() { return jumpTry(a, dx, dz, gap, floor, back, run, grab != 0) ? lara()->health : -1; },
                                    keys, &ticks);
                static bool tell = getenv("BOT_JUMPS") != NULL;
                if (tell) {
                    fprintf(stderr, "jump from %d,%d by %d,%d gap %d: back %d run %d grab %d -> %d\n", cellX(a), cellZ(a), dx, dz, gap,
                            back, run, grab, score);
                }
                // the most health, then the fewest ticks
                if (score > bestScore || (score == bestScore && score >= 0 && ticks < bestTicks))
                {
                    bestScore = score;
                    bestTicks = ticks;
                    memcpy(gJumpBest, keys, ticks * sizeof(uint32));
                }
            }
        }
    }
    if (bestScore < 0)
        return false;
    printf("  jump: %d sectors, health %d\n", gap, bestScore);
    play(gJumpBest, bestTicks);
    return true;
}

// Down a drop too deep to fall: stood at the edge with her back to it, a
// step back with Action held hangs her from it, then let go. Stood off from
// the sector's middle by off (toward the drop); her health at the bottom, or
// -1. Off an edge that is a slope she slides instead: her health with
// kElsewhere if she comes to rest alive (the way planned again from there).
static const int32 kElsewhere = 0x100000;

static int32 hangTry(const Cell &a, int32 dx, int32 dz, int32 floor, int32 off)
{
    int16 heading = int16(phd_atan(-dz, -dx));     // facing away from the drop
    int32 sx = cellX(a) + dx * off, sz = cellZ(a) + dz * off;
    if (!goStraight(sx, sz, 48, true) || !settle() || !turnTo(heading, ANGLE(3)))
        return -1;
    static bool tell = getenv("BOT_JUMPS") != NULL;
    if (tell) {
        fprintf(stderr, "hang from %d,%d by %d,%d off %d: at the edge %d,%d,%d yaw %d\n", cellX(a), cellZ(a), dx, dz, off,
                lara()->pos.x, lara()->pos.y, lara()->pos.z, uint16(lara()->angle.y));
    }
    for (int32 i = 0; i < 60 && lara()->state != Lara::STATE_HANG; i++) {
        step(IK_DOWN | IK_B);
    }
    if (lara()->state != Lara::STATE_HANG)
    {
        for (int32 i = 0; i < 150 && !(standing() && lara()->hSpeed == 0) && lara()->health > 0; i++) {
            step(0);
        }
        if (tell) {
            fprintf(stderr, "  no hang: at rest %d,%d,%d state %d health %d\n", lara()->pos.x, lara()->pos.y, lara()->pos.z,
                    lara()->state, lara()->health);
        }
        return (lara()->health > 0) ? (lara()->health | kElsewhere) : -1;     // still sliding: goToOnce sees her out of it
    }
    for (int32 i = 0; i < 90; i++)
    {
        if (standing() && abs(lara()->pos.y - floor) < 128)
            return lara()->health;
        step(0);
    }
    if (tell) {
        fprintf(stderr, "  hung, not down: %d,%d,%d state %d (floor %d)\n", lara()->pos.x, lara()->pos.y, lara()->pos.z, lara()->state,
                floor);
    }
    return -1;
}

// The edge's stand-off tried a few ways, the first that hangs her and lands
// her played (1); else the slide that leaves her the most health (2: plan
// again from where she is); 0 if none.
static int32 hangDrop(const Cell &a, int32 dx, int32 dz, int32 floor)
{
    static const int32 kOff[] = { 380, 330, 430, 280, 470 };
    int32 slide = -1, slideOff = 0;
    for (int32 v = 0; v < int32(X_COUNT(kOff)); v++)
    {
        int32 ticks = 0;
        int32 score = trial([&]() { return hangTry(a, dx, dz, floor, kOff[v]); }, gJumpBest, &ticks);
        if (score >= 0 && !(score & kElsewhere))
        {
            play(gJumpBest, ticks);
            return 1;
        }
        if (score >= 0 && (score & ~kElsewhere) > slide) {
            slide = score & ~kElsewhere;
            slideOff = kOff[v];
        }
    }
    if (slide < 0) {
        fail("didn't hang from the edge and drop");
        return 0;
    }
    int32 ticks = 0;
    trial([&]() { return hangTry(a, dx, dz, floor, slideOff); }, gJumpBest, &ticks);
    play(gJumpBest, ticks);
    printf("  slid down off the edge, health %d\n", lara()->health);
    return 2;
}

// An item of the ring used, as a player does it: START opens the ring, left
// turns it till the item is in front, Action takes it and uses it.
static bool ringUse(InvSlot slot)
{
    if (!inventory.counts[slot] || lara()->health <= 0)
        return false;
    int32 had = inventory.counts[slot];
    for (int32 i = 0; i < 30 && inventory.state == INV_STATE_NONE; i++) {
        step(i == 0 ? IK_SELECT : 0);
    }
    for (int32 i = 0; i < 90 && inventory.state != INV_STATE_READY; i++) {
        step(0);
    }
    for (int32 t = 0; t < inventory.itemsCount + 1 && inventory.itemsList[inventory.itemIndex] != slot; t++)
    {
        step(IK_LEFT);
        for (int32 i = 0; i < 30 && inventory.state != INV_STATE_READY; i++) {
            step(0);
        }
    }
    if (inventory.itemsList[inventory.itemIndex] != slot)
    {
        for (int32 i = 0; i < 60 && inventory.state != INV_STATE_NONE; i++) {
            step((i & 1) ? 0 : IK_SELECT);
        }
        fail("the item isn't on the ring's page");
        return false;
    }
    // Action once on the item; then nothing till the ring has shut, or a
    // press on the way out reaches her as a jump
    step(IK_A);
    for (int32 i = 0; i < 200 && inventory.state != INV_STATE_NONE; i++) {
        step(0);
    }
    settle(60);
    return inventory.counts[slot] < had;
}

// A medipack used when she's low: a small one under 500, a large one under
// 250 or when no small one is left.
static void heal()
{
    int32 h = lara()->health;
    if (h <= 0)
        return;
    if (h < 250 && inventory.counts[SLOT_MEDIKIT_BIG]) {
        ringUse(SLOT_MEDIKIT_BIG);
    } else if (h < 500 && inventory.counts[SLOT_MEDIKIT_SMALL]) {
        ringUse(SLOT_MEDIKIT_SMALL);
    } else if (h < 500 && inventory.counts[SLOT_MEDIKIT_BIG]) {
        ringUse(SLOT_MEDIKIT_BIG);
    }
}

// Swum to x, y, z within radius: under water, Jump held to swim, left and
// right to turn, up and down to pitch toward it; at the surface, forward and
// a turn, or Jump held a moment to dive when it lies deeper.
static bool swimTo(int32 x, int32 y, int32 z, int32 radius, int32 maxTicks = 30 * 40)
{
    int32 best = 0x7FFFFFFF, since = 0;
    for (int32 i = 0; i < maxTicks; i++)
    {
        Lara* l = lara();
        if (l->health <= 0)
            return false;
        int32 dx = x - l->pos.x, dy = y - l->pos.y, dz = z - l->pos.z;
        int32 dh = int32(sqrt(double(dx) * dx + double(dz) * dz));
        int32 dist = int32(sqrt(double(dh) * dh + double(dy) * dy));
        if (dist <= radius)
            return true;
        if (dist < best - 32) {
            best = dist;
            since = 0;
        } else if (++since > 90) {
            return false;   // stuck
        }
        int16 dyaw = int16(phd_atan(dz, dx) - l->angle.y);
        uint32 k = 0;
        if (l->waterState == WATER_STATE_SURFACE)
        {
            if (dy > 384 && dh < 1536)
            {
                k = IK_A;   // held: a dive
            } else {
                if (dyaw > ANGLE(4)) {
                    k |= IK_RIGHT;
                } else if (dyaw < -ANGLE(4)) {
                    k |= IK_LEFT;
                }
                if (abs(dyaw) < ANGLE(45)) {
                    k |= IK_UP;
                }
            }
        }
        else if (l->waterState == WATER_STATE_UNDER)
        {
            int16 pitch = int16(phd_atan(dh, -dy));     // up is positive, as angle.x
            int32 dpitch = int32(pitch) - l->angle.x;      // unwrapped: her pitch goes to 100 degrees, past straight up or down
            bool overhead = dh < 384 && abs(dy) > 600;     // straight up or down: no heading to turn to
            if (overhead) {
                dyaw = 0;
            } else if (dyaw > ANGLE(3)) {
                k |= IK_RIGHT;
            } else if (dyaw < -ANGLE(3)) {
                k |= IK_LEFT;
            }
            if (dpitch > ANGLE(3)) {
                k |= IK_DOWN;   // nose up
            } else if (dpitch < -ANGLE(3)) {
                k |= IK_UP;     // nose down
            }
            // strokes while far or slow; near it a glide, so she doesn't overshoot
            bool aimed = overhead ? (abs(dpitch) < ANGLE(30)) : (abs(dyaw) < ANGLE(60));
            if (aimed && (dist > 900 || l->vSpeed < 48)) {
                k |= IK_A;
            }
        }
        else
        {
            return false;   // out of the water
        }
        step(k);
    }
    return false;
}

// Out of the water onto the floor beyond the side heading across: up to the
// surface at it, square to it, forward and Action.
static bool climbOutAt(int16 heading, int32 x, int32 z, int32 surface)
{
    int32 bx = x - (sinRetail(heading) * 300 >> FIXED_SHIFT);
    int32 bz = z - (cosRetail(heading) * 300 >> FIXED_SHIFT);
    if (lara()->waterState == WATER_STATE_UNDER && !swimTo(bx, surface + 100, bz, 384)) {
        fail("couldn't swim up to the edge");
        return false;
    }
    for (int32 i = 0; i < 90 && lara()->waterState == WATER_STATE_UNDER; i++) {
        step(IK_DOWN | IK_A);   // nose up, swimming: to the surface
    }
    if (lara()->waterState != WATER_STATE_SURFACE) {
        fail("didn't come up at the edge");
        return false;
    }
    for (int32 i = 0; i < 360; i++)   // (the climb out's animation takes a while)
    {
        Lara* l = lara();
        if (l->waterState == WATER_STATE_ABOVE && standing())
            return true;
        int16 d = int16(heading - l->angle.y);
        uint32 k = IK_UP | IK_B;
        if (abs(d) > ANGLE(2)) {
            k = (d > 0) ? IK_RIGHT : IK_LEFT;
        }
        step(l->waterState == WATER_STATE_ABOVE ? 0 : k);
    }
    fail("couldn't climb out of the water");
    return false;
}

// One way of swimming from stretch a to stretch b beside it:
//   0: at the depth she's at where both have room, up or down within a first
//   1: at the middle of the height both share, by a's middle
//   2: at that middle, straight for b's middle
//   3: high in what both share, by a's middle
// (the surface, if both have air over them and meet there and she's at it)
static bool swimTry(const Cell &a, const Cell &b, int32 way)
{
    int32 lo = X_MAX(a.top, b.top), hi = X_MIN(a.floor, b.floor);
    bool surface = a.air && b.air && lara()->waterState == WATER_STATE_SURFACE && abs(a.top - b.top) < 64;
    int32 mid = X_MIN(X_MAX((lo + hi) / 2, lo + 250), hi - 200);
    int32 y = surface ? b.top : (way == 0) ? X_MIN(X_MAX(lara()->pos.y, lo + 250), hi - 200) : (way == 3) ? X_MIN(lo + 250, hi - 200) : mid;
    int32 hx = cellX(a) - lara()->pos.x, hz = cellZ(a) - lara()->pos.z;
    bool byMiddle = way != 2 && (hx * hx + hz * hz > 500 * 500 || (!surface && abs(lara()->pos.y - y) > 300));
    if (byMiddle && !swimTo(cellX(a), surface ? a.top : y, cellZ(a), 300))
        return false;
    return swimTo(cellX(b), y, cellZ(b), 350);
}

// A stretch swum: each way tried, the quickest that gets there played.
static bool swimStep(const Cell &a, const Cell &b)
{
    static uint32 keys[BOT_TRIAL_TICKS];
    static uint32 best[BOT_TRIAL_TICKS];
    int32 bestTicks = -1;
    for (int32 way = 0; way < 4; way++)
    {
        int32 ticks = 0;
        int32 score = trial([&]() -> int32 { return swimTry(a, b, way) ? lara()->health : -1; }, keys, &ticks);
        if (score >= 0 && (bestTicks < 0 || ticks < bestTicks))
        {
            bestTicks = ticks;
            memcpy(best, keys, ticks * sizeof(uint32));
        }
    }
    if (bestTicks < 0)
        return false;
    play(best, bestTicks);
    return true;
}

static bool fightBest(int32 range = 4096);
static ItemObj* nearestFoe(int32 range);

// To a spot in a room: along the sectors, straight runs where they are clear,
// climbs and drops where the way has them; the last of it walked. A climb
// that fails is left out and the way planned again; a creature awake near
// her is fought first.
static bool goToOnce(int32 roomIndex, int32 x, int32 z, bool walk, int32 radius, bool &again);

static int32 gNoWayWaits;

static bool goTo(int32 roomIndex, int32 x, int32 z, bool walk, int32 radius = 96)
{
    gBlockedCount = 0;
    gNoWayWaits = 0;
    bool threatsOnly = gThreatsOnly;
    gThreatsOnly = true;
    bool ok = false;
    int32 tries = 0;
    for (; tries < 24 && !ok; tries++)
    {
        heal();
        bool again = false;
        ok = goToOnce(roomIndex, x, z, walk, radius, again);
        if (!ok && (!again || lara()->health <= 0))
            break;
        if (!ok) {
            settle(120);
        }
    }
    gThreatsOnly = threatsOnly;
    if (!ok && tries == 24) {
        fail("gave up planning the way");
    }
    return ok;
}

static bool goToOnce(int32 roomIndex, int32 x, int32 z, bool walk, int32 radius, bool &again)
{
    static int32 path[1024];
    // sliding or falling (a drop that went on down a slope): to rest first; a
    // slide wedged against a wall (no way on) left with a jump
    vec3i last = lara()->pos;
    int32 still = 0;
    for (int32 i = 0; i < 300 && lara()->waterState == WATER_STATE_ABOVE && !standing() && lara()->health > 0; i++)
    {
        int32 st = lara()->state;
        if (st != Lara::STATE_SLIDE && st != Lara::STATE_SLIDE_BACK && st != Lara::STATE_FALL && st != Lara::STATE_FALL_BACK)
            break;
        still = (lara()->pos.x == last.x && lara()->pos.z == last.z) ? still + 1 : 0;
        last = lara()->pos;
        step(still > 30 && (st == Lara::STATE_SLIDE || st == Lara::STATE_SLIDE_BACK) ? IK_A : 0);
    }
    if (nearestFoe(3072))
    {
        if (!fightBest())
            return false;
        // moves that failed with it about may work now
        gBlockedCount = 0;
        again = true;
        return false;
    }
    obstaclesFind();
    int32 len = sectorPath(roomIndex, x, z, path, X_COUNT(path));
    if (!len)
    {
        // a door a switch just opened may still be opening: a few looks, a second apart
        if (gNoWayWaits < 4 && gBlockedCount == 0)
        {
            gNoWayWaits++;
            for (int32 i = 0; i < 30; i++) {
                step(0);
            }
            again = true;
            return false;
        }
        fail("no way there");
        return false;
    }
    int32 i = 0;
    while (i < len - 1)
    {
        const Cell &a = gCells[path[i]], &b = gCells[path[i + 1]];
        int32 dx = b.sx - a.sx, dz = b.sz - a.sz;
        // low before a drop or a jump (a fall of seven clicks takes a fifth
        // of her health): a medipack first
        if ((abs(dx) + abs(dz) > 1 || b.move == MOVE_DROP || b.move == MOVE_HANG_DROP) && lara()->health < 500 && settle()) {
            heal();
        }
        if (abs(dx) + abs(dz) > 1)
        {
            int32 gap = X_MAX(abs(dx), abs(dz)) - 1;
            int32 ux = (dx > 0) - (dx < 0), uz = (dz > 0) - (dz < 0);
            if (!jumpAcross(a, ux, uz, gap, b.floor))
            {
                block(a, dx, dz);
                again = true;
                return false;
            }
            if ((lara()->pos.x >> 10) != b.sx || (lara()->pos.z >> 10) != b.sz)
            {
                again = true;   // landed further on: the rest planned from there
                return false;
            }
            i++;
            continue;
        }
        Move m = Move(b.move);
        if (m == MOVE_WATER_IN)
        {
            // run in off the side, then plan again from the water
            if (!goStraight(cellX(a), cellZ(a), 256, walk) || !settle() || !turnTo(int16(phd_atan(dz, dx)), ANGLE(4))) {
                fail("couldn't stand at the water");
                block(a, dx, dz);
                again = true;
                return false;
            }
            for (int32 t = 0; t < 120 && lara()->waterState == WATER_STATE_ABOVE; t++) {
                step(IK_UP);
            }
            for (int32 t = 0; t < 20; t++) {
                step(0);
            }
            if (lara()->waterState == WATER_STATE_ABOVE) {
                fail("didn't get into the water");
                return false;
            }
            again = true;
            return false;
        }
        if (m == MOVE_SWIM && !gGoalAir && lara()->waterState == WATER_STATE_UNDER)
        {
            // the next air along the way; none (a goal under water): the way
            // there (the next step checks again, and a lever often opens air)
            int32 ahead = len - 1 - i;
            for (int32 k = i + 1; k < len; k++)
            {
                const Cell &q = gCells[path[k]];
                if (!q.water || q.air) {
                    ahead = k - i;
                    break;
                }
            }
            // (with her air all but full there's nothing better to do than go)
            if (lara()->oxygen < ahead * 30 * 13 / 10 + 200 && lara()->oxygen < LARA_MAX_OXYGEN - 150)
            {
                gGoalAir = true;
                bool breathed = goTo(0, 0, 0, false);
                gGoalAir = false;
                if (!breathed) {
                    fail("couldn't get to air");
                    return false;
                }
                again = true;
                return false;
            }
        }
        if (m == MOVE_SWIM)
        {
            if (!swimStep(a, b))
            {
                block(a, dx, dz);
                again = true;
                return false;
            }
            i++;
            continue;
        }
        if (m == MOVE_WATER_OUT)
        {
            int16 heading = int16(phd_atan(dz, dx));
            if (!climbOutAt(heading, cellX(a) + dx * 512, cellZ(a) + dz * 512, a.top))
                return false;
            again = true;
            return false;
        }
        if (m == MOVE_CLIMB)
        {
            int16 heading = int16(phd_atan(dz, dx));
            if (!climbAt(heading, cellX(a) + dx * 512, cellZ(a) + dz * 512, b.floor))
            {
                block(a, dx, dz);
                again = true;
                return false;
            }
            i++;
            continue;
        }
        if (m == MOVE_HANG_DROP)
        {
            // not as planned (slid off it somewhere): not tried again from there
            if (hangDrop(a, dx, dz, b.floor) != 1)
            {
                block(a, dx, dz);
                again = true;
                return false;
            }
            i++;
            continue;
        }
        if (m == MOVE_DROP)
        {
            // run off it, straight across the side; then plan again from
            // where she comes to rest (a slope slides her on)
            if (!goStraight(cellX(a), cellZ(a), 256, walk) || !settle() ||
                !turnTo(int16(phd_atan(dz, dx)), ANGLE(4))) {
                fail("couldn't stand at the drop");
                block(a, dx, dz);
                again = true;
                return false;
            }
            for (int32 t = 0; t < 90 && (lara()->pos.x >> 10) == a.sx && (lara()->pos.z >> 10) == a.sz; t++) {
                step(IK_UP);
            }
            if (!settle(150) && lara()->health > 0 && lara()->waterState == WATER_STATE_ABOVE) {     // in water: planned on from there
                fail("didn't come to rest after the drop");
                return false;
            }
            again = true;
            return false;
        }
        // the furthest sector ahead along walks that is clear in a straight line
        int32 j = i + 1;
        for (int32 k = i + 2; k < len; k++)
        {
            const Cell &q = gCells[path[k]];
            if (q.move != MOVE_WALK)
                break;
            int32 tx = (k == len - 1) ? x : cellX(q), tz = (k == len - 1) ? z : cellZ(q);
            if (!clearWalk(tx, tz))
                break;
            j = k;
        }
        int32 tx = (j == len - 1) ? x : cellX(gCells[path[j]]), tz = (j == len - 1) ? z : cellZ(gCells[path[j]]);
        if (!goStraight(tx, tz, (j == len - 1) ? X_MAX(radius, 256) : 256, walk, 30 * 120, true)) {
            // a creature to fight, or stuck (against one, or a corner): plan
            // again from here (goTo's tries run out if it keeps happening)
            if (!nearestFoe(3072)) {
                fail("stuck on the way, planning again");
                settle(30);
            }
            again = true;
            return false;
        }
        // on to the sector she's in, if past it
        i = j;
    }
    if (gGoalAir)
    {
        const Cell &e = gCells[path[len - 1]];
        if (lara()->waterState == WATER_STATE_UNDER && !swimTo(cellX(e), e.top + 150, cellZ(e), 300)) {
            fail("couldn't swim up for air");
            return false;
        }
        for (int32 i = 0; i < 120 && lara()->waterState == WATER_STATE_UNDER; i++) {
            step(IK_DOWN | IK_A);   // nose up, swimming
        }
        for (int32 i = 0; i < 240 && lara()->waterState == WATER_STATE_SURFACE && lara()->oxygen < LARA_MAX_OXYGEN - 50; i++) {
            step(0);
        }
        return lara()->waterState != WATER_STATE_UNDER;
    }
    if (gGoalY != NO_GOAL_Y)
    {
        if (!swimTo(x, gGoalY, z, X_MAX(radius, 160))) {
            fail("couldn't swim to the spot");
            return false;
        }
        return true;
    }
    // (a creature on her here: fought, then on again)
    if (!goStraight(x, z, X_MAX(radius, 256), walk, 30 * 120, true)) {
        again = nearestFoe(3072) != NULL;
        if (!again) {
            fail("stuck near the spot");
        }
        return false;
    }
    if (!settle()) {
        fail("didn't stop");
        return false;
    }
    if (distTo(x, z) > radius && !goStraight(x, z, radius, true, 30 * 120, true)) {
        again = nearestFoe(3072) != NULL;
        if (!again) {
            fail("couldn't walk onto the spot");
        }
        return false;
    }
    return settle();
}

static uint32 parseBotKeys(const char* s)
{
    return strcmp(s, "-") ? parseKeys(s) : 0;
}

// Picked up: walked onto it, Action till she bends for it.
static bool pickup(int32 index)
{
    ItemObj* item = items + index;
    if (!item->room || (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE) {
        fail("no such item to pick up");
        return false;
    }
    uint8 before = gSaveGame.pickups;
    if (ROOM_FLAG_WATER(item->room->info->flags))
    {
        // under water: swum to just above it, at rest, then Action (facing it)
        int32 y = item->pos.y - 200;
        gGoalY = y;
        bool there = goTo(int32(item->room - rooms), item->pos.x, item->pos.z, false, 200);
        gGoalY = NO_GOAL_Y;
        if (!there)
            return false;
        for (int32 tries = 0; tries < 6; tries++)
        {
            for (int32 i = 0; i < 60 && lara()->vSpeed > 0; i++) {
                step(0);
            }
            int32 ddx = item->pos.x - lara()->pos.x, ddy = y - lara()->pos.y, ddz = item->pos.z - lara()->pos.z;
            if (sqrt(double(ddx) * ddx + double(ddy) * ddy + double(ddz) * ddz) < 300)
                break;
            swimTo(item->pos.x, y, item->pos.z, 120);
        }
        for (int32 i = 0; i < 150 && gSaveGame.pickups == before; i++)
        {
            int16 d = int16(headingTo(item->pos.x, item->pos.z) - lara()->angle.y);
            uint32 k = IK_B;
            if (d > ANGLE(10)) {
                k |= IK_RIGHT;
            } else if (d < -ANGLE(10)) {
                k |= IK_LEFT;
            }
            step(k);
        }
        for (int32 i = 0; i < 180 && lara()->state != Lara::STATE_UW_TREAD; i++) {
            step(0);
        }
        if (gSaveGame.pickups == before) {
            fail("couldn't pick it up under water");
            return false;
        }
        return true;
    }
    // stood just short of it, facing it: it must lie in front of her (to 256
    // ahead, 100 behind, 256 aside)
    gGoalFloorY = item->pos.y;
    bool there = goTo(int32(item->room - rooms), item->pos.x, item->pos.z, false, 200);
    gGoalFloorY = NO_GOAL_Y;
    if (!there)
        return false;
    if (distTo(item->pos.x, item->pos.z) > 32 && !turnTo(headingTo(item->pos.x, item->pos.z), ANGLE(4))) {
        fail("couldn't face the item");
        return false;
    }
    for (int32 i = 0; i < 60 && distTo(item->pos.x, item->pos.z) > 160; i++) {
        step(IK_UP | IK_L);
    }
    settle();
    for (int32 i = 0; i < 30 && lara()->state != Lara::STATE_PICKUP; i++) {
        step(IK_B);
    }
    if (lara()->state != Lara::STATE_PICKUP) {
        fail("Action didn't pick it up");
        return false;
    }
    settle(120);
    if (gSaveGame.pickups == before) {
        fail("picked up nothing");
        return false;
    }
    return true;
}

// Pulled: stood in front of it, facing as it does, then Action.
static bool pullSwitch(int32 index)
{
    ItemObj* item = items + index;
    int16 yaw = item->angle.y;
    int32 x = item->pos.x + (sinRetail(yaw) * 400 >> FIXED_SHIFT);
    int32 z = item->pos.z + (cosRetail(yaw) * 400 >> FIXED_SHIFT);
    gGoalFloorY = item->pos.y;
    bool there = goTo(int32(item->room - rooms), x, z, false, 64);
    gGoalFloorY = NO_GOAL_Y;
    if (!there)
        return false;
    if (!turnTo(yaw, ANGLE(3))) {
        fail("couldn't face the switch");
        return false;
    }
    int32 state = item->state;
    for (int32 i = 0; i < 30 && lara()->state != Lara::STATE_SWITCH_DOWN && lara()->state != Lara::STATE_SWITCH_UP; i++) {
        step(IK_B);
    }
    if (lara()->state != Lara::STATE_SWITCH_DOWN && lara()->state != Lara::STATE_SWITCH_UP) {
        fail("Action didn't pull it");
        return false;
    }
    settle(120);
    if (item->state == state && item->goalState == state) {
        fail("the switch didn't move");
        return false;
    }
    return true;
}

// An underwater lever pulled: swum to in front of it, turned to face as it
// does, Action till it moves.
static bool pullLever(int32 index)
{
    ItemObj* item = items + index;
    int16 yaw = item->angle.y;
    int32 x = item->pos.x + (sinRetail(yaw) * 300 >> FIXED_SHIFT);
    int32 z = item->pos.z + (cosRetail(yaw) * 300 >> FIXED_SHIFT);
    gGoalY = item->pos.y - 256;
    bool there = goTo(int32(item->room - rooms), x, z, false, 160);
    gGoalY = NO_GOAL_Y;
    if (!there)
        return false;
    // at rest in front of it: let the glide die, come back, till she stops close
    for (int32 tries = 0; tries < 6; tries++)
    {
        for (int32 i = 0; i < 60 && lara()->vSpeed > 0; i++) {
            step(0);
        }
        int32 ddx = x - lara()->pos.x, ddy = item->pos.y - 256 - lara()->pos.y, ddz = z - lara()->pos.z;
        int32 off = int32(sqrt(double(ddx) * ddx + double(ddy) * ddy + double(ddz) * ddz));
        if (off < 320)
            break;
        swimTo(x, item->pos.y - 256, z, 120);
    }
    int32 state = item->state;
    for (int32 i = 0; i < 120; i++)
    {
        if (item->state != state || item->goalState != state)
            break;
        // levelled first: steeper than 80 degrees the lever can't be pulled
        // (and treading keeps the pitch she swam up or down at)
        int16 d = int16(yaw - lara()->angle.y);
        uint32 k = IK_B;
        if (lara()->angle.x > ANGLE(30)) {
            k = IK_UP;
        } else if (lara()->angle.x < -ANGLE(30)) {
            k = IK_DOWN;
        } else if (d > ANGLE(6)) {
            k = IK_RIGHT;
        } else if (d < -ANGLE(6)) {
            k = IK_LEFT;
        }
        step(k);
    }
    // the pull's animation played out (she treads water again)
    for (int32 i = 0; i < 30; i++) {
        step(0);
    }
    for (int32 i = 0; i < 180 && lara()->state != Lara::STATE_UW_TREAD; i++) {
        step(0);
    }
    if (item->state == state && item->goalState == state) {
        fail("the lever didn't move");
        return false;
    }
    return true;
}

// A movable block moved a sector at a time `times` times toward heading (a
// quarter turn): pushed, Lara behind it facing that way, or pulled, Lara in
// front facing back; Action held till she takes hold, then forward or back.
static bool moveBlock(int32 index, int16 heading, int32 times, bool pull)
{
    ItemObj* block = items + index;
    for (int32 t = 0; t < times; t++)
    {
        int32 dx = sinRetail(heading) >> (FIXED_SHIFT - 10), dz = cosRetail(heading) >> (FIXED_SHIFT - 10);  // 1024 a sector
        int32 side = pull ? 1 : -1;
        int32 sx = block->pos.x + side * dx * 600 / 1024, sz = block->pos.z + side * dz * 600 / 1024;
        int16 face = pull ? int16(heading + ANGLE_180) : heading;
        int32 bx = block->pos.x, bz = block->pos.z;
        if (!goTo(int32(block->room - rooms), sx, sz, false, 48) || !turnTo(face, ANGLE(1))) {
            fail("couldn't stand at the block");
            return false;
        }
        for (int32 i = 0; i < 40 && lara()->state != Lara::STATE_BLOCK_READY; i++) {
            step(IK_B);
        }
        if (lara()->state != Lara::STATE_BLOCK_READY) {
            fail("couldn't take hold of the block");
            return false;
        }
        for (int32 i = 0; i < 200 && block->pos.x == bx && block->pos.z == bz; i++) {
            step(IK_B | (pull ? IK_DOWN : IK_UP));
        }
        for (int32 i = 0; i < 200 && (block->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_ACTIVE; i++) {
            step(IK_B);     // the move played out
        }
        settle(60);
        if (abs(block->pos.x - bx) + abs(block->pos.z - bz) < 1000) {
            fail("the block didn't move");
            return false;
        }
    }
    return true;
}

// A key or puzzle item put in its hole: stood in front of it as at a
// switch, Action opens the ring on what can be used there, left turns it to
// the item that fits, Action uses it.
static bool useHole(int32 index)
{
    ItemObj* hole = items + index;
    InvSlot slot;
    if (hole->type >= ITEM_KEYHOLE_1 && hole->type <= ITEM_KEYHOLE_4) {
        slot = InvSlot(SLOT_KEY_ITEM_1 + (hole->type - ITEM_KEYHOLE_1));
    } else if (hole->type >= ITEM_PUZZLEHOLE_1 && hole->type <= ITEM_PUZZLEHOLE_4) {
        slot = InvSlot(SLOT_PUZZLE_1 - (hole->type - ITEM_PUZZLEHOLE_1));
    } else {
        fail("not a keyhole or a puzzle hole");
        return false;
    }
    if (!inventory.counts[slot]) {
        fail("nothing to put in it");
        return false;
    }
    int32 had = inventory.counts[slot];
    int16 yaw = hole->angle.y;
    int32 x = hole->pos.x + (sinRetail(yaw) * 400 >> FIXED_SHIFT);
    int32 z = hole->pos.z + (cosRetail(yaw) * 400 >> FIXED_SHIFT);
    gGoalFloorY = hole->pos.y;
    bool there = goTo(int32(hole->room - rooms), x, z, false, 64);
    gGoalFloorY = NO_GOAL_Y;
    if (!there || !turnTo(yaw, ANGLE(3))) {
        fail("couldn't stand at the hole");
        return false;
    }
    for (int32 i = 0; i < 30 && inventory.state == INV_STATE_NONE; i++) {
        step(IK_B);
    }
    for (int32 i = 0; i < 90 && inventory.state != INV_STATE_READY; i++) {
        step(0);
    }
    for (int32 t = 0; t < inventory.itemsCount + 1 && inventory.itemsList[inventory.itemIndex] != slot; t++)
    {
        step(IK_LEFT);
        for (int32 i = 0; i < 30 && inventory.state != INV_STATE_READY; i++) {
            step(0);
        }
    }
    if (inventory.itemsList[inventory.itemIndex] != slot) {
        fail("the item isn't on the ring");
        return false;
    }
    // Action once on the item; then nothing till the ring has shut, or a
    // press on the way out reaches her as a jump
    step(IK_A);
    for (int32 i = 0; i < 200 && inventory.state != INV_STATE_NONE; i++) {
        step(0);
    }
    // in: the item gone from the ring (the hole's own status is cleared again
    // when what it opens is triggered)
    for (int32 i = 0; i < 200 && inventory.counts[slot] == had; i++) {
        step(0);
    }
    settle(150);
    if (inventory.counts[slot] == had) {
        fail("the item didn't go in");
        return false;
    }
    return true;
}

// A break-away tile brought down: stood on the sector beside it (the side
// she steps onto it from heading DEG, or any she can reach), a walk onto it
// starts it shaking and she gets back off before it falls; tried a few ways.
static bool collapse(int32 index, int32 deg)
{
    ItemObj* tile = items + index;
    if (tile->type != ITEM_TRAP_FLOOR) {
        fail("not a break-away tile");
        return false;
    }
    int32 tsx = tile->pos.x >> 10, tsz = tile->pos.z >> 10, top = tile->pos.y - 512;
    static int32 path[1024];
    static uint32 keys[BOT_TRIAL_TICKS];
    for (int32 d = 0; d < 4 && tile->state == 0; d++)
    {
        int32 dx = kDirX[d], dz = kDirZ[d];     // from the tile to the side she stands on
        int16 heading = int16(phd_atan(-dz, -dx));
        if (deg >= 0 && abs(int16(heading - int16(deg * 0x10000 / 360))) > ANGLE(10))
            continue;
        int32 sx = ((tsx + dx) << 10) + 512, sz = ((tsz + dz) << 10) + 512;
        Spot side = spotAt(tile->room, sx, top - 256, sz);
        obstaclesFind();
        if (side.floor == WALL || abs(side.floor - top) > 64 || trapAt(tsx + dx, tsz + dz, side.floor) ||
            !sectorPath(int32(side.room - rooms), sx, sz, path, X_COUNT(path)))
            continue;
        if (!goTo(int32(side.room - rooms), sx, sz, true, 64) || !settle() || !turnTo(heading, ANGLE(3)))
            return false;
        int32 bestScore = -1, bestTicks = 0;
        for (int32 way = 0; way < 3; way++)
        {
            int32 ticks = 0;
            int32 score = trial([&]() -> int32 {
                int32 i = 0;
                for (; i < 120 && tile->state == 0; i++) {
                    step(IK_UP | IK_L);
                }
                if (way != 1)
                {
                    for (int32 t = 0; t < 30 && !standing(); t++) {
                        step(0);
                    }
                }
                for (int32 t = 0; t < 60 && ((lara()->pos.x >> 10) != tsx + dx || (lara()->pos.z >> 10) != tsz + dz); t++) {
                    step(way == 2 ? IK_DOWN : (IK_DOWN | IK_A));
                }
                for (int32 t = 0; t < 150 && tile->state < 2; t++) {
                    step(0);
                }
                settle();
                const Lara* l = lara();
                if (tile->state < 2 || !standing() || (l->pos.x >> 10) != tsx + dx || (l->pos.z >> 10) != tsz + dz ||
                    abs(l->pos.y - side.floor) > 64)
                    return -1;
                return l->health;
            }, keys, &ticks);
            if (score > bestScore || (score == bestScore && score >= 0 && ticks < bestTicks))
            {
                bestScore = score;
                bestTicks = ticks;
                memcpy(gJumpBest, keys, ticks * sizeof(uint32));
            }
        }
        if (bestScore >= 0)
        {
            play(gJumpBest, bestTicks);
            return true;
        }
    }
    if (tile->state != 0)
        return true;
    fail("couldn't bring the tile down");
    return false;
}

// To a spot past something that swings or sweeps (blades): stood still a
// while first, each wait up to MAXWAIT tried; the one that gets her there
// with the most health, then soonest, is played.
static bool timed(int32 roomIndex, int32 x, int32 z, int32 maxWait)
{
    static uint32 keys[BOT_TRIAL_TICKS];
    int32 bestScore = -1, bestTicks = 0;
    for (int32 w = 0; w <= maxWait; w += 3)
    {
        int32 ticks = 0;
        int32 score = trial([&]() -> int32 {
            for (int32 i = 0; i < w; i++) {
                step(0);
            }
            if (!goTo(roomIndex, x, z, false, 160))
                return -1;
            return lara()->health;
        }, keys, &ticks);
        if (score > bestScore || (score == bestScore && score >= 0 && ticks < bestTicks))
        {
            bestScore = score;
            bestTicks = ticks;
            memcpy(gJumpBest, keys, ticks * sizeof(uint32));
        }
    }
    if (bestScore < 0) {
        fail("no wait got her there");
        return false;
    }
    play(gJumpBest, bestTicks);
    return true;
}

static bool isCreature(const ItemObj* item)
{
    return item->type >= ITEM_WOLF && item->type <= ITEM_ADAM && item->type != ITEM_SKATEBOARD;
}

// The nearest creature awake and alive, or none.
// A threat to her where she is: one that flies or shoots, or one on her level
// (within four clicks); a bear pacing a floor far below her is no threat.
static bool isThreat(const ItemObj* item)
{
    switch (item->type)
    {
        case ITEM_BAT: case ITEM_MUTANT_1: case ITEM_MUTANT_2: case ITEM_CENTAUR: case ITEM_LARSON: case ITEM_PIERRE:
        case ITEM_SKATER: case ITEM_COWBOY: case ITEM_MR_T: case ITEM_NATLA:
            return true;
        default:
            return abs(item->pos.y - lara()->pos.y) <= 1024;
    }
}

static ItemObj* nearestFoe(int32 range)
{
    ItemObj* best = NULL;
    int32 bestDist = range;
    if (gFleeing)
        return NULL;
    for (ItemObj* item = ItemObj::sFirstActive; item; item = item->nextActive)
    {
        if (!isCreature(item) || item->health <= 0 || (gThreatsOnly && !isThreat(item)))
            continue;
        int32 d = distTo(item->pos.x, item->pos.z);
        if (d < bestDist) {
            best = item;
            bestDist = d;
        }
    }
    return best;
}

// Room behind her for a jump back: no drop, no wall, for dist.
static bool clearBehind(int32 dist)
{
    const Lara* l = lara();
    int32 x = l->pos.x - (sinRetail(l->angle.y) * dist >> FIXED_SHIFT);
    int32 z = l->pos.z - (cosRetail(l->angle.y) * dist >> FIXED_SHIFT);
    return clearWalk(x, z);
}

// Guns out, turned toward her target (or the nearest creature awake), Action
// held while she aims at it; a target she can't aim at for a while let go
// (Action released a tick), so she takes the best in front. Till none is
// left within range.
struct FightStyle
{
    int32 deadband;     // turned toward the foe past this
    int32 tooClose;     // a jump back when the foe is nearer than this
    int32 letGo;        // ticks unaimed before a new target
};

static const FightStyle kFightStyles[] = {
    { ANGLE(8), 384, 20 }, { ANGLE(15), 384, 20 }, { ANGLE(8), 512, 12 }, { ANGLE(4), 256, 30 },
    { ANGLE(25), 0, 20 }, { ANGLE(8), 640, 20 }, { ANGLE(12), 384, 8 }, { ANGLE(6), 448, 40 },
};

static bool fight(const FightStyle &st, int32 range = 4096, int32 maxTicks = 30 * 90)
{
    ExtraInfoLara* e = lara()->extraL;
    int32 quiet = 0, unaimed = 0;
    for (int32 i = 0; i < maxTicks; i++)
    {
        if (lara()->health <= 0) {
            fail("died fighting");
            return false;
        }
        ItemObj* foe = nearestFoe(range);
        if (!foe)
        {
            if (++quiet > 30)
                break;
            step(0);
            continue;
        }
        quiet = 0;
        if (e->weaponState == WEAPON_STATE_FREE) {
            step(IK_C);     // draw
            step(0);
            continue;
        }
        if (lara()->health < 250 && standing() && (inventory.counts[SLOT_MEDIKIT_SMALL] || inventory.counts[SLOT_MEDIKIT_BIG]))
        {
            heal();
            continue;
        }
        ItemObj* target = e->armR.target;
        if (target && target->health > 0) {
            foe = target;
        }
        // on top of her (a bat at her head, too steep to aim at): a jump back
        // puts it in front
        if (distTo(foe->pos.x, foe->pos.z) < st.tooClose && abs(foe->pos.y - lara()->pos.y) < 1024 && standing() &&
            clearBehind(1536))
        {
            for (int32 j = 0; j < 12; j++) {
                step(IK_DOWN | IK_A | IK_B);
            }
            continue;
        }
        uint32 k = 0;
        int16 d = int16(headingTo(foe->pos.x, foe->pos.z) - lara()->angle.y);
        if (d > st.deadband) {
            k |= IK_RIGHT;
        } else if (d < -st.deadband) {
            k |= IK_LEFT;
        }
        // aimed at nothing, or still at one that's dead (the lock holds while
        // Action is held): let go a moment for a new target
        bool aimed = e->armR.aim || e->armL.aim;
        unaimed = (target && (!aimed || target->health <= 0)) ? unaimed + 1 : 0;
        if (e->weaponState == WEAPON_STATE_READY && unaimed < st.letGo) {
            k |= IK_B;
        } else if (unaimed >= st.letGo) {
            unaimed = 0;    // let go a tick: a new target
        }
        step(k);
    }
    if (e->weaponState != WEAPON_STATE_FREE)
    {
        step(IK_C);     // holster
        step(0);
        settle(60);
    }
    return nearestFoe(range) == NULL;
}

// A fight played each way in kFightStyles in a trial, the one that leaves
// her the most health played for real.
// One tick of shooting from where she stands: guns drawn, turned to it,
// Action while it's near (let go now and then for a new aim).
static uint32 shootKeys(ItemObj* foe, int32 i)
{
    ExtraInfoLara* e = lara()->extraL;
    if (e->weaponState == WEAPON_STATE_FREE)
        return (i & 1) ? 0 : IK_C;
    uint32 k = 0;
    int16 d = int16(headingTo(foe->pos.x, foe->pos.z) - lara()->angle.y);
    if (d > ANGLE(8)) {
        k |= IK_RIGHT;
    } else if (d < -ANGLE(8)) {
        k |= IK_LEFT;
    }
    ItemObj* target = e->armR.target;
    bool aimed = (e->armR.aim || e->armL.aim) && target && target->health > 0;
    if (e->weaponState == WEAPON_STATE_READY && distTo(foe->pos.x, foe->pos.z) < 8192 && (aimed || (i & 15) < 12)) {
        k |= IK_B;
    }
    return k;
}

static void holster()
{
    for (int32 i = 0; i < 60 && lara()->extraL->weaponState != WEAPON_STATE_FREE; i++) {
        step(i == 0 ? IK_C : 0);
    }
}

// The sector of its zone nearest her by the planner's way, on the ground:
// where she stands to be in its reach (false if none).
static bool lureSpot(ItemObj* foe, int32 &room, int32 &x, int32 &z)
{
    const Nav &nav = foe->extraE->nav;
    const uint16* zones = level.zones[gSaveGame.flipped][nav.zoneType];
    uint16 zone = zones[foe->room->getSector(foe->pos.x, foe->pos.z)->boxIndex];
    static int32 path[1024];
    obstaclesFind();
    sectorPath(int32(lara()->room - rooms), -0x400000, -0x400000, path, X_COUNT(path));
    int32 best = -1;
    for (int32 i = 0; i < gCellsCount; i++)
    {
        const Cell &c = gCells[i];
        if (c.water || c.cost == 0x7FFFFFFF)
            continue;
        uint16 box = c.room->getSector(cellX(c), cellZ(c))->boxIndex;
        if (box == NO_BOX || zones[box] != zone || (level.boxes[box].overlap & nav.mask))
            continue;
        if (best < 0 || c.cost < gCells[best].cost)
            best = i;
    }
    if (best < 0)
        return false;
    room = int32(gCells[best].room - rooms);
    x = cellX(gCells[best]);
    z = cellZ(gCells[best]);
    return true;
}

// A lure: into its zone, shot at as it comes, back to the spot before it's
// within near. Its score: her health, with 32 for each hit on it; < 0 if she
// didn't get back, it wasn't hit, or it cost her more than a fifth of her
// health (a bite: the next lure is safer).
static int32 lureTry(ItemObj* foe, int32 lroom, int32 lx, int32 lz, int32 sroom, int32 sx, int32 sz, int32 near)
{
    int32 hp = foe->health, health = lara()->health;
    holster();
    gFleeing = true;
    bool ok = goTo(lroom, lx, lz, false, 256);
    for (int32 i = 0; ok && i < 30 * 60 && foe->health > 0 && distTo(foe->pos.x, foe->pos.z) >= near; i++)
    {
        if (lara()->health <= 0)
            return -1;
        step(shootKeys(foe, i));
    }
    holster();
    ok = ok && goTo(sroom, sx, sz, false, 256);
    gFleeing = false;
    for (int32 i = 0; ok && i < 150; i++) {
        step(shootKeys(foe, i));
    }
    int32 hits = hp - X_MAX(foe->health, 0);
    if (!ok || lara()->health <= 0 || hits <= 0 || health - lara()->health > 200)
        return -1;
    return hits * 32 + lara()->health;
}

// Stood where it can't reach her: turned to it and shot whenever it's near,
// up to 40 minutes. A creature hit while she's out of its zone runs from her
// till she's in it again (retail's Creature_Mood: escape, then attack only
// when she's in its zone), so after 15 quiet seconds she lures it (lureTry,
// the nearest it may come tried three ways, the best played).
static bool shootFromHere(ItemObj* foe)
{
    int32 t0 = gTick;
    int32 sroom = int32(lara()->room - rooms), sx = lara()->pos.x, sz = lara()->pos.z;
    int32 lroom = 0, lx = 0, lz = 0;
    bool lure = lureSpot(foe, lroom, lx, lz);
    if (lure) {
        printf("  lure spot: %d,%d room %d\n", lx, lz, lroom);
    }
    static uint32 keys[BOT_TRIAL_TICKS];
    static const int32 kNear[] = { 8192, 6144, 4096 };
    int32 quiet = 0;
    for (int32 i = 0; gTick - t0 < 30 * 60 * 40 && foe->health > 0; i++)
    {
        if (lara()->health <= 0) {
            fail("died at the safe spot");
            return false;
        }
        quiet = (distTo(foe->pos.x, foe->pos.z) < 8192) ? 0 : quiet + 1;
        if (lure && quiet > 30 * 15)
        {
            quiet = 0;
            int32 best = -1, bestNear = 0;
            for (int32 n = 0; n < int32(X_COUNT(kNear)); n++)
            {
                int32 near = kNear[n];
                int32 score = trial([&]() { return lureTry(foe, lroom, lx, lz, sroom, sx, sz, near); }, NULL, NULL);
                if (score > best) {
                    best = score;
                    bestNear = near;
                }
            }
            if (best >= 0)
            {
                int32 ticks = 0, hp = foe->health;
                trial([&]() { return lureTry(foe, lroom, lx, lz, sroom, sx, sz, bestNear); }, keys, &ticks);
                play(keys, ticks);
                printf("  lure: back as it came within %d, %d hits on it, her health %d\n", bestNear, hp - X_MAX(foe->health, 0),
                       lara()->health);
            }
            continue;
        }
        step(shootKeys(foe, i));
    }
    if (foe->health > 0) {
        fail("couldn't kill it from the safe spot");
        return false;
    }
    printf("  fight from safety: %d s, health %d\n", (gTick - t0) / 30, lara()->health);
    // guns away (a switch wants her hands free)
    holster();
    settle(60);
    heal();
    return true;
}

// A creature too strong to fight in the open (the T-rex) fought from where
// it can't follow: a sector she can reach that lies in no box the creature
// walks (its zone, as its own pathfinding sees it) and two sectors from every
// one of them: the one whose way there crosses least of its ground, then with
// the most of its ground within shot (2 to 6 sectors off), then the nearest;
// run to, then shot from, standing.
static bool fightFromSafety(ItemObj* foe, int32 range)
{
    if (!foe->extraE || foe->extraE->nav.zoneType == ZONE_FLY)
        return false;
    const Nav &nav = foe->extraE->nav;
    const uint16* zones = level.zones[gSaveGame.flipped][nav.zoneType];
    uint16 zone = zones[foe->room->getSector(foe->pos.x, foe->pos.z)->boxIndex];
    // every sector she can reach: the planner's search with nowhere to stop
    static int32 path[1024];
    obstaclesFind();
    sectorPath(int32(lara()->room - rooms), -0x400000, -0x400000, path, X_COUNT(path));
    // how many of the sectors on her way to each lie in its zone (the fewer,
    // the less she meets it on the way)
    static int16 exposure[BOT_CELLS];
    for (int32 i = 0; i < gCellsCount; i++) {
        exposure[i] = -1;
    }
    for (int32 i = 0; i < gCellsCount; i++)
    {
        int32 chain[256], n = 0, c = i;
        while (c >= 0 && exposure[c] < 0 && n < 256)
        {
            chain[n++] = c;
            c = gCells[c].from;
        }
        int32 e = (c >= 0 && exposure[c] >= 0) ? exposure[c] : 0;
        while (n > 0)
        {
            const Cell &q = gCells[chain[--n]];
            uint16 box = q.room->getSector(cellX(q), cellZ(q))->boxIndex;
            e += (!q.water && box != NO_BOX && zones[box] == zone) ? 1 : 0;
            exposure[chain[n]] = int16(X_MIN(e, 0x7FFF));
        }
    }
    int32 best = -1, bestSeen = 0;
    for (int32 i = 0; i < gCellsCount; i++)
    {
        const Cell &c = gCells[i];
        if (c.water || c.cost == 0x7FFFFFFF || c.cost > 60 * 1024)
            continue;
        int32 cx = cellX(c), cz = cellZ(c);
        int32 nearest = 0x7FFFFFFF, seen = 0;
        for (int32 b = 0; b < level.boxesCount && nearest >= 2048; b++)
        {
            const Box &box = level.boxes[b];
            if (zones[b] != zone || (box.overlap & nav.mask))
                continue;
            int32 dx = X_MAX(0, X_MAX((box.minX << 10) - cx, cx - (box.maxX << 10)));
            int32 dz = X_MAX(0, X_MAX((box.minZ << 10) - cz, cz - (box.maxZ << 10)));
            int32 d = X_MAX(dx, dz);
            // a ledge four clicks over its floor is out of its reach from
            // below (a bite reaches a click up: retail's CreatureAIInfo)
            if (c.floor >= box.floor - 1024) {
                nearest = X_MIN(nearest, d);
            }
            if (d <= 6144) {
                seen += (box.maxX - box.minX) * (box.maxZ - box.minZ);
            }
        }
        if (nearest < 2048 || seen == 0)
            continue;
        if (best < 0 || exposure[i] < exposure[best] ||
            (exposure[i] == exposure[best] && (seen > bestSeen || (seen == bestSeen && c.cost < gCells[best].cost))))
        {
            best = i;
            bestSeen = seen;
        }
    }
    if (best < 0) {
        fail("no safe spot to fight it from");
        return false;
    }
    const Cell target = gCells[best];
    printf("  safe spot: %d,%d floor %d room %d (%d sectors of its ground within shot, %d of it on the way)\n", cellX(target),
           cellZ(target), target.floor, int32(target.room - rooms), bestSeen, exposure[best]);
    gFleeing = true;
    bool there = goTo(int32(target.room - rooms), cellX(target), cellZ(target), false, 256);
    gFleeing = false;
    if (!there)
        return false;
    return shootFromHere(foe);
}

static bool fightBest(int32 range)
{
    static uint32 keys[BOT_TRIAL_TICKS];
    int32 best = -1, bestTicks = 0, bestScore = -1;
    for (int32 v = 0; v < int32(X_COUNT(kFightStyles)); v++)
    {
        int32 ticks = 0;
        int32 score = trial([v, range]() {
            bool won = fight(kFightStyles[v], range);
            static bool tell = getenv("BOT_FIGHTS") != NULL;
            if (tell) {
                const ItemObj* foe = nearestFoe(range);
                fprintf(stderr, "  fight style %d at tick %d: %s, health %d, foe %d (health %d)\n", v, gTick, won ? "won" : "lost",
                        lara()->health, foe ? int32(foe - items) : -1, foe ? foe->health : 0);
            }
            return won ? lara()->health : -1;
        }, NULL, NULL);
        if (score > bestScore)
        {
            bestScore = score;
            best = v;
            trial([v, range]() { return fight(kFightStyles[v], range) ? lara()->health : -1; }, keys, &ticks);
            bestTicks = ticks;
        }
    }
    if (best < 0)
    {
        if (ItemObj* foe = nearestFoe(range))
        {
            if (fightFromSafety(foe, X_MAX(range, 8192)))
                return true;
        }
        fail("every way of fighting died");
        return false;
    }
    printf("  fight: style %d of %d, health %d\n", best, int32(X_COUNT(kFightStyles)), bestScore);
    play(keys, bestTicks);
    return true;
}

// Guns out, standing.
static bool draw()
{
    ExtraInfoLara* e = lara()->extraL;
    if (!settle())
        return false;
    for (int32 i = 0; i < 60 && e->weaponState != WEAPON_STATE_READY; i++) {
        step(i == 0 ? IK_C : 0);
    }
    if (e->weaponState != WEAPON_STATE_READY) {
        fail("couldn't draw");
        return false;
    }
    return true;
}

static bool expect(const char* what, int32 value)
{
    char why[96];
    bool ok;
    int32 have;
    if (!strcmp(what, "room")) {
        have = int32(lara()->room - rooms);
    } else if (!strcmp(what, "secrets")) {
        have = 0;
        for (int32 b = 0; b < 8; b++) {
            have += (gSaveGame.secrets >> b) & 1;
        }
    } else if (!strcmp(what, "pickups")) {
        have = gSaveGame.pickups;
    } else if (!strcmp(what, "kills")) {
        have = gSaveGame.kills;
    } else if (!strcmp(what, "alive")) {
        have = lara()->health > 0;
        value = 1;
    } else {
        fail("no such expectation");
        return false;
    }
    ok = (have == value);
    if (!ok)
    {
        snprintf(why, sizeof(why), "expected %s %d, have %d", what, value, have);
        fail(why);
    }
    return ok;
}

// An attempt block (attempt ROOM X Z [TRIES] ... end): a few steps by hand
// (keys, wait, face, goto) tried in trials, each `keys` line's ticks moved by
// up to three either way in all but the first try; the try that leaves her
// standing on the sector at X, Z in ROOM with the most health, then the
// fewest ticks, is played.
#define BOT_BLOCK_LINES 32
static char gBlock[BOT_BLOCK_LINES][256];
static int32 gBlockCount;

static bool blockTry(int32 roomIndex, int32 x, int32 z, int32 floor, int32 variant)
{
    int32 keysLine = 0;
    for (int32 i = 0; i < gBlockCount; i++)
    {
        char cmd[32], a[64], b[64], c[64];
        int32 n = sscanf(gBlock[i], "%31s %63s %63s %63s", cmd, a, b, c);
        if (n <= 0)
            continue;
        if (!strcmp(cmd, "keys") && n >= 3)
        {
            int32 ticks = atoi(b);
            if (variant > 0) {
                ticks += int32((uint32(variant) * 7919u + uint32(keysLine) * 104729u) % 7u) - 3;
            }
            keysLine++;
            uint32 k = parseBotKeys(a);
            for (int32 t = 0; t < ticks; t++) {
                step(k);
            }
        } else if (!strcmp(cmd, "wait") && n >= 2) {
            for (int32 t = atoi(a); t > 0; t--) {
                step(0);
            }
        } else if (!strcmp(cmd, "face") && n >= 2) {
            if (!turnTo(int16(atoi(a) * 0x10000 / 360)))
                return false;
        } else if (!strcmp(cmd, "goto") && n >= 4) {
            char d[64] = "", e[64] = "";
            sscanf(gBlock[i], "%*s %*s %*s %*s %63s %63s", d, e);
            bool walk = !strcmp(d, "walk");
            int32 radius = atoi(walk ? e : d);
            if (!goTo(atoi(a), atoi(b), atoi(c), walk, radius > 0 ? radius : 96))
                return false;
        } else {
            return false;
        }
    }
    settle(150);
    const Lara* l = lara();
    return standing() && (l->pos.x >> 10) == (x >> 10) && (l->pos.z >> 10) == (z >> 10) && abs(l->pos.y - floor) < 128 &&
           l->health > 0;
}

static bool attempt(int32 roomIndex, int32 x, int32 z, int32 tries)
{
    Room* r = rooms + roomIndex;
    int32 floor = spotAt(r, x, (r->info->yTop + r->info->yBottom) / 2, z).floor;
    static uint32 keys[BOT_TRIAL_TICKS];
    static uint32 best[BOT_TRIAL_TICKS];
    int32 bestScore = -1, bestTicks = 0;
    for (int32 v = 0; v < tries; v++)
    {
        int32 ticks = 0;
        int32 score = trial([&]() -> int32 { return blockTry(roomIndex, x, z, floor, v) ? lara()->health : -1; }, keys, &ticks);
        if (score > bestScore || (score == bestScore && score >= 0 && ticks < bestTicks))
        {
            bestScore = score;
            bestTicks = ticks;
            memcpy(best, keys, ticks * sizeof(uint32));
        }
    }
    if (bestScore < 0) {
        fail("no try of the attempt got there");
        return false;
    }
    play(best, bestTicks);
    return true;
}

static void writeReplay(const char* path, const char* levelName, bool newGame)
{
    static const struct { const char* name; uint32 key; } names[] = {
        { "UP", IK_UP }, { "DOWN", IK_DOWN }, { "LEFT", IK_LEFT }, { "RIGHT", IK_RIGHT },
        { "A", IK_A }, { "B", IK_B }, { "C", IK_C }, { "X", IK_X }, { "Y", IK_Y }, { "Z", IK_Z },
        { "L", IK_L }, { "R", IK_R }, { "START", IK_SELECT }
    };
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "bot: can't write %s\n", path);
        return;
    }
    fprintf(f, "# the playtest bot's run (host/bot.h), %d ticks\n", gTick);
    fprintf(f, "start %s %s\n", levelName, newGame ? "newgame" : "kit");
    for (int32 i = 0; i < gTick;)
    {
        int32 j = i;
        while (j < gTick && gLog[j] == gLog[i]) {
            j++;
        }
        char k[96] = "";
        for (uint32 n = 0; n < X_COUNT(names); n++)
        {
            if (gLog[i] & names[n].key) {
                if (k[0]) {
                    strcat(k, "+");
                }
                strcat(k, names[n].name);
            }
        }
        fprintf(f, "%d %s\n", j - i, k[0] ? k : "-");
        i = j;
    }
    fclose(f);
}

// The routes played from the first's level's start (already loaded), each on
// from where the last left her (its endlevel took her into the next level;
// the start line is the first's alone): 0 if every step was met.
static int32 run(const char* routePaths, const char* outPath, const char* levelName, bool newGame)
{
    for (int32 d = 0; d < int32(X_COUNT(gTrialOut)); d++)
    {
        gTrialOut[d] = (TrialOut*)mmap(NULL, sizeof(TrialOut), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (gTrialOut[d] == MAP_FAILED) {
            fprintf(stderr, "bot: no shared memory\n");
            return 2;
        }
    }
    // the level's first tick, as the host's loop starts
    step(0);
    bool ok = true;
    for (const char* rp = routePaths; ok && *rp; rp = strchr(rp, ',') ? strchr(rp, ',') + 1 : "")
    {
    char routePath[512];
    snprintf(routePath, sizeof(routePath), "%.*s", int(strchr(rp, ',') ? strchr(rp, ',') - rp : strlen(rp)), rp);
    FILE* f = fopen(routePath, "r");
    if (!f) {
        fprintf(stderr, "bot: can't open %s\n", routePath);
        return 2;
    }
    printf("route %s from tick %d\n", routePath, gTick);
    char line[256];
    gLine = 0;
    while (ok && fgets(line, sizeof(line), f))
    {
        gLine++;
        if (char* hash = strchr(line, '#')) {
            *hash = 0;
        }
        char cmd[32], a[64], b[64], c[64], d[64], e[64];
        int32 n = sscanf(line, "%31s %63s %63s %63s %63s %63s", cmd, a, b, c, d, e);
        if (n <= 0 || !strcmp(cmd, "start"))
            continue;
        int32 t0 = gTick;
        if (strcmp(cmd, "keys") && strcmp(cmd, "wait") && strcmp(cmd, "expect")) {
            heal();
        }
        if (!strcmp(cmd, "goto") && n >= 4) {
            // goto ROOM X Z [walk] [RADIUS]
            bool walk = n >= 5 && !strcmp(d, "walk");
            int32 radius = (n >= 5 && !walk) ? atoi(d) : (n >= 6 ? atoi(e) : 0);
            ok = goTo(atoi(a), atoi(b), atoi(c), walk, radius > 0 ? radius : 96);
        } else if (!strcmp(cmd, "goto3") && n >= 5) {
            // goto3 ROOM X Y Z [RADIUS]: to the floor at that height there
            gGoalFloorY = atoi(c);
            ok = goTo(atoi(a), atoi(b), atoi(d), false, n >= 6 && atoi(e) > 0 ? atoi(e) : 96);
            gGoalFloorY = NO_GOAL_Y;
        } else if (!strcmp(cmd, "face") && n >= 2) {
            ok = turnTo(int16(atoi(a) * 0x10000 / 360));
            if (!ok) {
                fail("couldn't turn");
            }
        } else if (!strcmp(cmd, "keys") && n >= 3) {
            uint32 k = parseBotKeys(a);
            for (int32 i = atoi(b); i > 0; i--) {
                step(k);
            }
        } else if (!strcmp(cmd, "wait") && n >= 2) {
            for (int32 i = atoi(a); i > 0; i--) {
                step(0);
            }
        } else if (!strcmp(cmd, "pickup") && n >= 2) {
            ok = pickup(atoi(a));
        } else if (!strcmp(cmd, "switch") && n >= 2) {
            ok = pullSwitch(atoi(a));
        } else if (!strcmp(cmd, "explain") && n >= 5) {
            // explain ROOM X Y Z: the moves the planner sees from that sector (Y above its floor)
            Spot sp = spotAt(rooms + atoi(a), atoi(b), atoi(c), atoi(d));
            Cell cl;
            cl.sx = int16(atoi(b) >> 10);
            cl.sz = int16(atoi(d) >> 10);
            cl.floor = sp.floor;
            cl.room = sp.room;
            printf("  explain %d,%d floor %d room %d:\n", cl.sx << 10, cl.sz << 10, cl.floor, int32(cl.room - rooms));
            for (int32 dir = 0; dir < 4; dir++)
            {
                Spot low, hi;
                Move m = moveBetween(cl, kDirX[dir], kDirZ[dir], low);
                bool climb = climbBetween(cl, kDirX[dir], kDirZ[dir], low, hi);
                printf("    dir %d,%d: move %d to floor %d ceiling %d room %d; climb %s %d\n", kDirX[dir], kDirZ[dir], m, low.floor,
                       low.ceiling, low.room ? int32(low.room - rooms) : -1, climb ? "yes" : "no", climb ? hi.floor : 0);
            }
            for (int32 dir = 0; dir < 8; dir++)
            {
                for (int32 gap = 1; gap <= 5; gap++)
                {
                    for (int32 look = 0; look < 2; look++)
                    {
                        Spot jb;
                        bool j = jumpBetween(cl, kJumpX[dir], kJumpZ[dir], gap, jb, look == 1);
                        int32 jx = (cl.sx + kJumpX[dir] * (gap + 1)) << 10, jz = (cl.sz + kJumpZ[dir] * (gap + 1)) << 10;
                        if (j) {
                            printf("    jump %d,%d gap %d%s: floor %d%s\n", kJumpX[dir], kJumpZ[dir], gap, look ? " (from above)" : "",
                                   jb.floor, blocked(jx + 512, jb.floor, jz + 512) ? " (in the way of something)" : "");
                        }
                    }
                }
            }
        } else if (!strcmp(cmd, "explainwater") && n >= 5) {
            // explainwater ROOM X Y Z: the water moves from that sector's stretch of water
            WaterCol w;
            if (!waterAt(rooms + atoi(a), atoi(b), atoi(c), atoi(d), w)) {
                printf("  no water there\n");
            } else {
                gCellsCount = 0;
                for (int32 i = 0; i < BOT_CELLS * 2; i++) {
                    gCellHash[i] = -1;
                }
                int32 c0 = cellFind(atoi(b) >> 10, atoi(d) >> 10, w.bottom, true, w.room, true);
                gCells[c0].top = w.top;
                gCells[c0].air = w.air;
                gCells[c0].cost = 0;
                waterMoves(c0, -1, -1);
                printf("  water at %d,%d room %d top %d bottom %d air %d:\n", atoi(b) >> 10 << 10, atoi(d) >> 10 << 10,
                       int32(w.room - rooms), w.top, w.bottom, w.air);
                for (int32 i = 1; i < gCellsCount; i++) {
                    printf("    to %d,%d %s floor %d top %d room %d move %d\n", gCells[i].sx << 10, gCells[i].sz << 10,
                           gCells[i].water ? "water" : "land", gCells[i].floor, gCells[i].top, int32(gCells[i].room - rooms),
                           gCells[i].move);
                }
            }
        } else if (!strcmp(cmd, "sector") && n >= 4) {
            // sector ROOM SX SZ: debugging, its raw fields and floor data
            const Room &room = rooms[atoi(a)];
            const Sector* sc = room.sectors + atoi(b) * room.info->zSectors + atoi(c);
            printf("    sector %d,%d of room %d: floorIndex %d box %d floor %d ceiling %d above %d below %d; data", atoi(b), atoi(c), atoi(a),
                   sc->floorIndex, sc->boxIndex, sc->floor, sc->ceiling, sc->roomAbove, sc->roomBelow);
            for (int32 k = 0; sc->floorIndex && k < 8; k++) {
                printf(" %04x", level.floors[sc->floorIndex + k]);
            }
            printf("\n");
        } else if (!strcmp(cmd, "portals") && n >= 2) {
            // portals ROOM: debugging, its sectors that lead on (sideways, up, down)
            const Room &room = rooms[atoi(a)];
            const RoomInfo* ri = room.info;
            for (int32 sz = 0; sz < ri->zSectors; sz++)
            {
                for (int32 sx = 0; sx < ri->xSectors; sx++)
                {
                    const Sector* sc = room.sectors + sx * ri->zSectors + sz;
                    Room* next = sc->getNextRoom();
                    if (next || sc->roomAbove != NO_ROOM || sc->roomBelow != NO_ROOM)
                        printf("    sector %d,%d (x %d z %d): floor %d ceiling %d side %d above %d below %d\n", sx, sz, (ri->x << 8) + (sx << 10),
                               (ri->z << 8) + (sz << 10), sc->floor << 8, sc->ceiling << 8, next ? int32(next - rooms) : -1,
                               sc->roomAbove == NO_ROOM ? -1 : sc->roomAbove, sc->roomBelow == NO_ROOM ? -1 : sc->roomBelow);
                }
            }
        } else if (!strcmp(cmd, "rooms") && n >= 4) {
            // rooms X Y Z: debugging, every room whose box holds the point, its sector there
            int32 x = atoi(a), y = atoi(b), z = atoi(c);
            for (int32 r = 0; r < level.roomsCount; r++)
            {
                const RoomInfo* ri = rooms[r].info;
                if (x < (ri->x << 8) || z < (ri->z << 8) || x >= (ri->x << 8) + (ri->xSectors << 10) ||
                    z >= (ri->z << 8) + (ri->zSectors << 10) || y < ri->yTop || y > ri->yBottom)
                    continue;
                const Sector* sc = rooms[r].getSector(x, z);
                Room* next = sc->getNextRoom();
                printf("    room %d alt %d (x %d z %d, %dx%d sectors, y %d..%d): sector floor %d ceiling %d above %d below %d portal %d\n", r, rooms[r].alternateRoom == NO_ROOM ? -1 : rooms[r].alternateRoom, ri->x << 8,
                       ri->z << 8, ri->xSectors, ri->zSectors, ri->yTop, ri->yBottom, sc->floor << 8, sc->ceiling << 8,
                       sc->roomAbove == NO_ROOM ? -1 : sc->roomAbove, sc->roomBelow == NO_ROOM ? -1 : sc->roomBelow,
                       next ? int32(next - rooms) : -1);
            }
        } else if (!strcmp(cmd, "spot") && n >= 5) {
            // spot ROOM X Y Z: debugging, the floor and ceiling there as the planner reads them
            Spot sp = spotAt(rooms + atoi(a), atoi(b), atoi(c), atoi(d));
            printf("    spot %d %d %d: room %d floor %d ceiling %d\n", atoi(b), atoi(c), atoi(d), int32(sp.room - rooms), sp.floor, sp.ceiling);
        } else if (!strcmp(cmd, "triggers") && n >= 2) {
            // triggers ACTION: debugging, each sector whose trigger has that action (7: the level's end)
            for (int32 r = 0; r < level.roomsCount; r++)
            {
                const RoomInfo* ri = rooms[r].info;
                for (int32 sx = 1; sx < ri->xSectors - 1; sx++)
                {
                    for (int32 sz = 1; sz < ri->zSectors - 1; sz++)
                    {
                        int32 x = (ri->x << 8) + (sx << 10) + 512, z = (ri->z << 8) + (sz << 10) + 512;
                        const Sector* sector = rooms[r].getSector(x, z);
                        if (sector->floor == NO_FLOOR)
                            continue;
                        int32 y = (sector->floor << 8) - 64;
                        int32 arg = triggerArg(rooms + r, x, y, z, atoi(a));
                        if (arg >= 0)
                            printf("    trigger %d: room %d at %d %d %d (sector %d,%d) arg %d (camera at %d %d %d, room %d)\n", atoi(a), r, x,
                                   y + 64, z, sx, sz, arg, level.cameras[arg].pos.x, level.cameras[arg].pos.y, level.cameras[arg].pos.z,
                                   level.cameras[arg].roomIndex);
                    }
                }
            }
        } else if (!strcmp(cmd, "floors") && n >= 5) {
            // floors X0 Z0 X1 Z1: debugging, each sector's floors from the top
            // down (every room stacked there), ceilings after the slash
            for (int32 sz = atoi(d) >> 10; sz >= (atoi(b) >> 10); sz--)
            {
                printf("    z %6d:", sz << 10);
                for (int32 sx = atoi(a) >> 10; sx <= (atoi(c) >> 10); sx++)
                {
                    int32 x = (sx << 10) + 512, z = (sz << 10) + 512, last = WALL;
                    char cellText[64] = "";
                    int32 len = 0;
                    for (int32 y = -12288; y <= 12288 && len < 48; y += 128)
                    {
                        for (int32 r = 0; r < level.roomsCount; r++)
                        {
                            const RoomInfo* ri = rooms[r].info;
                            if (x < (ri->x << 8) + 1024 || z < (ri->z << 8) + 1024 || x >= (ri->x << 8) + (ri->xSectors << 10) - 1024 ||
                                z >= (ri->z << 8) + (ri->zSectors << 10) - 1024 || y < ri->yTop || y > ri->yBottom)
                                continue;
                            Spot sp = spotAt(rooms + r, x, y, z);
                            if (sp.floor != WALL && sp.floor != last && sp.floor >= y && sp.floor - y < 128) {
                                len += snprintf(cellText + len, sizeof(cellText) - len, "%s%d", last == WALL ? "" : ",", sp.floor);
                                last = sp.floor;
                            }
                            break;
                        }
                    }
                    printf(" %13s", cellText[0] ? cellText : "-");
                }
                printf("\n");
            }
            printf("    x       :");
            for (int32 sx = atoi(a) >> 10; sx <= (atoi(c) >> 10); sx++) {
                printf(" %13d", sx << 10);
            }
            printf("\n");
        } else if (!strcmp(cmd, "statics") && n >= 3) {
            // statics X Z: debugging, every static mesh within two sectors, its box as Lara meets it
            for (int32 r = 0; r < level.roomsCount; r++)
            {
                const Room &room = rooms[r];
                for (int32 i = 0; i < room.info->meshesCount; i++)
                {
                    const RoomMesh* mesh = room.data.meshes + i;
                    const StaticMesh* sm = level.staticMeshes + STATIC_MESH_ID(mesh->zf);
                    int32 x = (room.info->x << 8) + (int32(mesh->xy) >> 16);
                    int32 y = int32(mesh->xy) << 16 >> 16;
                    int32 z = (room.info->z << 8) + (int32(mesh->zf) >> 16);
                    if (abs(x - atoi(a)) > 2048 || abs(z - atoi(b)) > 2048)
                        continue;
                    AABBi box(sm->cbox);
                    boxRotateYQ(box, STATIC_MESH_QUADRANT(mesh->zf));
                    printf("    static room %d at %d %d %d: x %d..%d y %d..%d z %d..%d%s\n", r, x, y, z, x + box.minX, x + box.maxX,
                           y + box.minY, y + box.maxY, z + box.minZ, z + box.maxZ,
                           (sm->flags & STATIC_MESH_FLAG_NO_COLLISION) ? " (no collision)" : "");
                }
            }
        } else if (!strcmp(cmd, "teleport") && n >= 5) {
            // teleport ROOM X Y Z [DEG]: debugging only (the replay no longer plays the same)
            Lara* l = lara();
            l->pos = _vec3i(atoi(b), atoi(c), atoi(d));
            l->room = rooms + atoi(a);
            l->roomFloor = l->pos.y;
            if (n >= 6) {
                l->angle.y = int16(atoi(e) * 0x10000 / 360);
            }
            settle();
        } else if (!strcmp(cmd, "jumptest") && n >= 4) {
            // jumptest DX:DZ:GAP:FLOOR BACK RUN GRAB: one jump try, here, for real
            int32 jdx, jdz, gap, floor;
            sscanf(a, "%d:%d:%d:%d", &jdx, &jdz, &gap, &floor);
            Cell cl;
            cl.sx = int16(lara()->pos.x >> 10);
            cl.sz = int16(lara()->pos.z >> 10);
            cl.floor = lara()->pos.y;
            cl.room = lara()->room;
            ok = jumpTry(cl, jdx, jdz, gap, floor, atoi(b), atoi(c), n >= 5 && atoi(d));
        } else if (!strcmp(cmd, "attempt") && n >= 4) {
            // the block's lines, to its end
            gBlockCount = 0;
            char bl[256];
            while (fgets(bl, sizeof(bl), f))
            {
                gLine++;
                if (char* hash = strchr(bl, '#')) {
                    *hash = 0;
                }
                char w[32];
                if (sscanf(bl, "%31s", w) == 1 && !strcmp(w, "end"))
                    break;
                if (gBlockCount < BOT_BLOCK_LINES) {
                    snprintf(gBlock[gBlockCount++], sizeof(gBlock[0]), "%s", bl);
                }
            }
            ok = attempt(atoi(a), atoi(b), atoi(c), n >= 5 ? atoi(d) : 24);
        } else if (!strcmp(cmd, "endlevel") && n >= 4) {
            // endlevel ROOM X Z: to the end trigger there, then Action through
            // the statistics and the "Save Game?" prompt till the next level is up
            LevelID was = gLevelID;
            goTo(atoi(a), atoi(b), atoi(c), false);
            // (at "Save Game?" No, as a QA run leaves the backup memory alone)
            for (int32 i = 0; i < 30 * 120 && (gLevelID == was || !players[0]); i++)
            {
                bool ask = inventory.state != INV_STATE_NONE && inventory.page == INV_PAGE_SAVE && inventory.optionIndex == 0;
                step((i % 8) == 0 ? (ask ? IK_RIGHT : IK_A) : 0);
            }
            ok = gLevelID != was;
            if (ok) {
                printf("  level %d up after %d ticks\n", gLevelID, gTick);
            } else {
                fail("the level didn't end");
            }
        } else if (!strcmp(cmd, "swimto") && n >= 5) {
            // swimto ROOM X Y Z: to a spot in the water
            gGoalY = atoi(c);
            ok = goTo(atoi(a), atoi(b), atoi(d), false, 200);
            gGoalY = NO_GOAL_Y;
        } else if ((!strcmp(cmd, "push") || !strcmp(cmd, "pull")) && n >= 4) {
            // push|pull ITEM DEGREES TIMES: a block moved that way, a sector a time
            ok = moveBlock(atoi(a), int16(atoi(b) * 0x10000 / 360), atoi(c), !strcmp(cmd, "pull"));
        } else if (!strcmp(cmd, "safe") && n >= 2) {
            // safe ITEM [ROOM X Z]: the creature fought from a spot it can't
            // reach (that one, or the bot's pick), till it's dead
            ItemObj* foe = items + atoi(a);
            if (foe->health <= 0) {
                ok = true;
            } else if (n >= 5) {
                gFleeing = true;
                ok = goTo(atoi(b), atoi(c), atoi(d), false, 128);
                gFleeing = false;
                ok = ok && shootFromHere(foe);
            } else {
                ok = fightFromSafety(foe, 8192);
            }
        } else if (!strcmp(cmd, "timed") && n >= 4) {
            ok = timed(atoi(a), atoi(b), atoi(c), n >= 5 ? atoi(d) : 90);
        } else if (!strcmp(cmd, "collapse") && n >= 2) {
            ok = collapse(atoi(a), n >= 3 ? atoi(b) : -1);
        } else if (!strcmp(cmd, "usehole") && n >= 2) {
            ok = useHole(atoi(a));
        } else if (!strcmp(cmd, "lever") && n >= 2) {
            ok = pullLever(atoi(a));
        } else if (!strcmp(cmd, "use") && n >= 2) {
            // use small|big: a medipack from the ring
            ok = ringUse(!strcmp(a, "big") ? SLOT_MEDIKIT_BIG : SLOT_MEDIKIT_SMALL);
        } else if (!strcmp(cmd, "draw")) {
            ok = draw();
        } else if (!strcmp(cmd, "fight")) {
            ok = fightBest(n >= 2 ? atoi(a) : 4096);
            if (!ok && lara()->health > 0) {
                fail("creatures left awake");
            }
        } else if (!strcmp(cmd, "expect") && n >= 2) {
            ok = expect(a, n >= 3 ? atoi(b) : 0);
        } else {
            fail("no such step");
            ok = false;
        }
        const Lara* l = lara();
        printf("line %3d %-7s ticks %5d..%5d %s room %3d pos %6d %6d %6d yaw %5d health %4d\n", gLine, cmd, t0, gTick,
               ok ? "ok  " : "FAIL", int32(l->room - rooms), l->pos.x, l->pos.y, l->pos.z, uint16(l->angle.y), l->health);
        if (ok && l->health <= 0) {
            fail("Lara died");
            ok = false;
        }
    }
    fclose(f);
    // the last level finished's statistics, as its screen shows them
    int32 secrets = 0;
    for (int32 b = 0; b < 8; b++) {
        secrets += (gLevelStats.secrets >> b) & 1;
    }
    printf("last level finished: kills %d, pickups %d, secrets %d, %d s\n", gLevelStats.kills, gLevelStats.pickups, secrets,
           gLevelStats.time / 30);
    }
    writeReplay(outPath, levelName, newGame);
    printf("%s: %d ticks (%d s of play), replay %s\n", ok ? "ROUTE DONE" : "ROUTE FAILED", gTick, gTick / 30, outPath);
    return ok ? 0 : 1;
}

} // namespace bot
