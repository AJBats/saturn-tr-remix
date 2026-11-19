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
//   goto ROOM X Z [walk]          to that spot along the level's boxes
//   face DEGREES                  turned to that heading (0 is +z, 90 is +x)
//   keys KEYS TICKS               those keys held (UP+A ...), for a move by hand
//   wait TICKS                    no key held
//   pickup ITEM                   walked to it and picked it up
//   switch ITEM                   stood at it and pulled it
//   draw                          guns out
//   fight                         guns drawn, every creature awake shot dead
//   expect room ROOM | secrets N | pickups N | kills N | alive
//                                 the run fails unless it is so

#include <sys/mman.h>
#include <sys/wait.h>
#include <math.h>

namespace bot {

#define BOT_MAX_TICKS   (30 * 60 * 120)     // two hours of play a level
#define BOT_TRIAL_TICKS 4096

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
        printf("  tick %5d keys %04x room %3d pos %6d %6d %6d yaw %5d state %2d anim %3d speed %3d health %4d "
               "gun %d/%d target %d aim %d arm %d:%d\n",
               gTick, k, int32(l->room - rooms), l->pos.x, l->pos.y, l->pos.z, uint16(l->angle.y), l->state, l->animIndex,
               l->hSpeed, l->health, e->weapon, e->weaponState, e->armR.target ? int32(e->armR.target - items) : -1,
               e->armR.aim, e->armR.animIndex, e->armR.frameIndex);
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
        if (!freopen("/dev/null", "w", stdout)) {
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
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
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

// No key till she stands still (a run or a pickup ends).
static bool settle(int32 maxTicks = 90)
{
    for (int32 i = 0; i < maxTicks; i++)
    {
        if (standing() && lara()->hSpeed == 0)
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

static bool goStraight(int32 x, int32 z, int32 radius, bool walk, int32 maxTicks = 30 * 120, bool wary = false)
{
    int32 best = distTo(x, z), since = 0;
    for (int32 i = 0; i < maxTicks; i++)
    {
        if (wary && nearestFoe(3072))
            return false;
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

struct Cell
{
    int16 sx, sz;
    int32 floor;
    Room* room;
    int32 cost, from;
    bool done;
};

static Cell gCells[BOT_CELLS];
static int32 gCellsCount;
static int32 gCellHash[BOT_CELLS * 2];

static int32 cellFind(int32 sx, int32 sz, int32 floor, bool add, Room* room)
{
    uint32 h = (uint32(sx) * 73856093u ^ uint32(sz) * 19349663u ^ uint32(floor >> 7) * 83492791u) & (BOT_CELLS * 2 - 1);
    while (gCellHash[h] >= 0)
    {
        const Cell &c = gCells[gCellHash[h]];
        if (c.sx == sx && c.sz == sz && abs(c.floor - floor) < 128)
            return gCellHash[h];
        h = (h + 1) & (BOT_CELLS * 2 - 1);
    }
    if (!add || gCellsCount == BOT_CELLS)
        return -1;
    Cell &c = gCells[gCellsCount];
    c.sx = int16(sx);
    c.sz = int16(sz);
    c.floor = floor;
    c.room = room;
    c.cost = 0x7FFFFFFF;
    c.from = -1;
    c.done = false;
    gCellHash[h] = gCellsCount;
    return gCellsCount++;
}

enum Move { MOVE_WALK, MOVE_DROP, MOVE_CLIMB, MOVE_NONE };

// How Lara gets from a's sector to its neighbour at b's: b filled in.
static Move moveBetween(const Cell &a, int32 dx, int32 dz, Spot &b)
{
    int32 cx = (a.sx << 10) + 512, cz = (a.sz << 10) + 512;
    int32 nx = cx + dx * 1024, nz = cz + dz * 1024;
    int32 y = a.floor - 256;
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
        Spot l = spotAt(edge.room, ex + dz * 160, y - 2048, ez + dx * 160);
        Spot r = spotAt(edge.room, ex - dz * 160, y - 2048, ez - dx * 160);
        if (l.floor != WALL && r.floor != WALL && abs(l.floor - r.floor) < LARA_HANG_SLANT)
            return MOVE_CLIMB;
        return MOVE_NONE;
    }
    if (down > 256 && down <= BOT_MAX_DROP && edge.floor >= side.floor - 256)
        return MOVE_DROP;
    return MOVE_NONE;
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

// The sectors from Lara's to the one holding x, z in a room, the cheapest way
// (walks by their length, drops and climbs dearer); none: 0.
static int32 sectorPath(int32 roomIndex, int32 x, int32 z, int32* path, int32 maxLen)
{
    Lara* l = lara();
    Room* goalRoom = rooms + roomIndex;
    Spot goal = spotAt(goalRoom, x, (goalRoom->info->yTop + goalRoom->info->yBottom) / 2, z);
    gCellsCount = 0;
    for (int32 i = 0; i < BOT_CELLS * 2; i++) {
        gCellHash[i] = -1;
    }
    int32 start = cellFind(l->pos.x >> 10, l->pos.z >> 10, l->pos.y, true, l->room);
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
            int32 f = cl.cost + (abs(cl.sx - gsx) + abs(cl.sz - gsz)) * 1024;
            if (f < best) {
                best = f;
                c = i;
            }
        }
        if (c < 0)
            break;
        gCells[c].done = true;
        if (gCells[c].sx == gsx && gCells[c].sz == gsz && abs(gCells[c].floor - goal.floor) < 384) {
            found = c;
            break;
        }
        for (int32 d = 0; d < 4; d++)
        {
            Spot b;
            Move m = moveBetween(gCells[c], kDirX[d], kDirZ[d], b);
            if (m == MOVE_NONE || (m != MOVE_WALK && isBlocked(gCells[c], kDirX[d], kDirZ[d])))
                continue;
            int32 n = cellFind(gCells[c].sx + kDirX[d], gCells[c].sz + kDirZ[d], b.floor, true, b.room);
            if (n < 0 || gCells[n].done)
                continue;
            int32 cost = gCells[c].cost + 1024;
            if (m == MOVE_DROP) {
                cost += 2048 + (b.floor - gCells[c].floor);
            } else if (m == MOVE_CLIMB) {
                cost += 2048 + (gCells[c].floor - b.floor) * 2;
            }
            if (cost < gCells[n].cost) {
                gCells[n].cost = cost;
                gCells[n].from = c;
            }
        }
    }
    if (found < 0)
        return 0;
    int32 len = 0;
    for (int32 c = found; c >= 0; c = gCells[c].from) {
        len++;
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
// clicks) or jumps up, grabs and pulls up (to seven and a half).
static bool climbAt(int16 heading, int32 x, int32 z, int32 floor)
{
    int32 bx = x - (sinRetail(heading) * 200 >> FIXED_SHIFT);
    int32 bz = z - (cosRetail(heading) * 200 >> FIXED_SHIFT);
    if (!goStraight(bx, bz, 64, true) || !settle() || !turnTo(heading, ANGLE(2))) {
        fail("couldn't stand at the climb");
        return false;
    }
    for (int32 i = 0; i < 150; i++)
    {
        const Lara* l = lara();
        if (standing() && abs(l->pos.y - floor) < 128 && i > 10)
            return true;
        if (l->health <= 0)
            break;
        step(IK_UP | IK_B);
    }
    fail("couldn't climb up");
    return false;
}

static bool fightBest();
static ItemObj* nearestFoe(int32 range);

// To a spot in a room: along the sectors, straight runs where they are clear,
// climbs and drops where the way has them; the last of it walked. A climb
// that fails is left out and the way planned again; a creature awake near
// her is fought first.
static bool goToOnce(int32 roomIndex, int32 x, int32 z, bool walk, int32 radius, bool &again);

static bool goTo(int32 roomIndex, int32 x, int32 z, bool walk, int32 radius = 96)
{
    gBlockedCount = 0;
    for (int32 tries = 0; tries < 8; tries++)
    {
        bool again = false;
        if (goToOnce(roomIndex, x, z, walk, radius, again))
            return true;
        if (!again || lara()->health <= 0)
            return false;
        settle(120);
    }
    fail("gave up planning the way");
    return false;
}

static bool goToOnce(int32 roomIndex, int32 x, int32 z, bool walk, int32 radius, bool &again)
{
    static int32 path[1024];
    if (nearestFoe(3072))
    {
        if (!fightBest())
            return false;
        again = true;
        return false;
    }
    int32 len = sectorPath(roomIndex, x, z, path, X_COUNT(path));
    if (!len) {
        fail("no way there");
        return false;
    }
    int32 i = 0;
    while (i < len - 1)
    {
        const Cell &a = gCells[path[i]], &b = gCells[path[i + 1]];
        int32 dx = b.sx - a.sx, dz = b.sz - a.sz;
        Spot sp;
        Move m = moveBetween(a, dx, dz, sp);
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
        if (m == MOVE_DROP)
        {
            // run off it, straight across the side
            if (!goStraight(cellX(a), cellZ(a), 256, walk) ||
                !goStraight(cellX(b), cellZ(b), 256, false) || !settle(120)) {
                fail("the drop went wrong");
                return false;
            }
            i++;
            continue;
        }
        // the furthest sector ahead along walks that is clear in a straight line
        int32 j = i + 1;
        for (int32 k = i + 2; k < len; k++)
        {
            const Cell &p = gCells[path[k - 1]], &q = gCells[path[k]];
            Spot t;
            if (moveBetween(p, q.sx - p.sx, q.sz - p.sz, t) != MOVE_WALK)
                break;
            int32 tx = (k == len - 1) ? x : cellX(q), tz = (k == len - 1) ? z : cellZ(q);
            if (!clearWalk(tx, tz))
                break;
            j = k;
        }
        int32 tx = (j == len - 1) ? x : cellX(gCells[path[j]]), tz = (j == len - 1) ? z : cellZ(gCells[path[j]]);
        if (!goStraight(tx, tz, (j == len - 1) ? X_MAX(radius, 256) : 256, walk, 30 * 120, true)) {
            if (nearestFoe(3072)) {
                again = true;
                return false;
            }
            fail("stuck on the way");
            return false;
        }
        // on to the sector she's in, if past it
        i = j;
    }
    if (!goStraight(x, z, X_MAX(radius, 256), walk)) {
        fail("stuck near the spot");
        return false;
    }
    if (!settle()) {
        fail("didn't stop");
        return false;
    }
    if (distTo(x, z) > radius && !goStraight(x, z, radius, true)) {
        fail("couldn't walk onto the spot");
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
    // stood just short of it, facing it: it must lie in front of her (to 256
    // ahead, 100 behind, 256 aside)
    if (!goTo(int32(item->room - rooms), item->pos.x, item->pos.z, false, 200))
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
    if (!goTo(int32(item->room - rooms), x, z, false, 64))
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

static bool isCreature(const ItemObj* item)
{
    return item->type >= ITEM_WOLF && item->type <= ITEM_ADAM && item->type != ITEM_SKATEBOARD;
}

// The nearest creature awake and alive, or none.
static ItemObj* nearestFoe(int32 range)
{
    ItemObj* best = NULL;
    int32 bestDist = range;
    for (ItemObj* item = ItemObj::sFirstActive; item; item = item->nextActive)
    {
        if (!isCreature(item) || item->health <= 0)
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

static bool fight(const FightStyle &st, int32 range = 8192, int32 maxTicks = 30 * 90)
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
        ItemObj* target = e->armR.target;
        if (target && target->health > 0) {
            foe = target;
        }
        // on top of her (a bat at her head, too steep to aim at): a jump back
        // puts it in front
        if (distTo(foe->pos.x, foe->pos.z) < st.tooClose && standing() && clearBehind(768))
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
        bool aimed = e->armR.aim || e->armL.aim;
        unaimed = (target && !aimed) ? unaimed + 1 : 0;
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
static bool fightBest()
{
    static uint32 keys[BOT_TRIAL_TICKS];
    int32 best = -1, bestTicks = 0, bestScore = -1;
    for (int32 v = 0; v < int32(X_COUNT(kFightStyles)); v++)
    {
        int32 ticks = 0;
        int32 score = trial([v]() { return fight(kFightStyles[v]) ? lara()->health : -1; }, NULL, NULL);
        if (score > bestScore)
        {
            bestScore = score;
            best = v;
            trial([v]() { return fight(kFightStyles[v]) ? lara()->health : -1; }, keys, &ticks);
            bestTicks = ticks;
        }
    }
    if (best < 0) {
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

// The route played from the level's start (already loaded): 0 if every step
// was met.
static int32 run(const char* routePath, const char* outPath, const char* levelName, bool newGame)
{
    for (int32 d = 0; d < int32(X_COUNT(gTrialOut)); d++)
    {
        gTrialOut[d] = (TrialOut*)mmap(NULL, sizeof(TrialOut), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (gTrialOut[d] == MAP_FAILED) {
            fprintf(stderr, "bot: no shared memory\n");
            return 2;
        }
    }
    FILE* f = fopen(routePath, "r");
    if (!f) {
        fprintf(stderr, "bot: can't open %s\n", routePath);
        return 2;
    }
    // the level's first tick, as the host's loop starts
    step(0);
    char line[256];
    bool ok = true;
    gLine = 0;
    while (ok && fgets(line, sizeof(line), f))
    {
        gLine++;
        if (char* hash = strchr(line, '#')) {
            *hash = 0;
        }
        char cmd[32], a[64], b[64], c[64], d[64];
        int32 n = sscanf(line, "%31s %63s %63s %63s %63s", cmd, a, b, c, d);
        if (n <= 0 || !strcmp(cmd, "start"))
            continue;
        int32 t0 = gTick;
        if (!strcmp(cmd, "goto") && n >= 4) {
            ok = goTo(atoi(a), atoi(b), atoi(c), n >= 5 && !strcmp(d, "walk"));
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
        } else if (!strcmp(cmd, "draw")) {
            ok = draw();
        } else if (!strcmp(cmd, "fight")) {
            ok = fightBest();
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
    writeReplay(outPath, levelName, newGame);
    printf("%s: %d ticks (%d s of play), replay %s\n", ok ? "ROUTE DONE" : "ROUTE FAILED", gTick, gTick / 30, outPath);
    return ok ? 0 : 1;
}

} // namespace bot
