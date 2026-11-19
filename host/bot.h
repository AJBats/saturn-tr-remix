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
    static int32 every = getenv("BOT_TRACE") ? atoi(getenv("BOT_TRACE")) : 0;
    if (every && gTick % every == 0 && players[0])
    {
        const Lara* l = players[0];
        printf("  tick %5d keys %04x room %3d pos %6d %6d %6d yaw %5d state %2d anim %3d speed %3d health %4d\n", gTick, k,
               int32(l->room - rooms), l->pos.x, l->pos.y, l->pos.z, uint16(l->angle.y), l->state, l->animIndex, l->hSpeed, l->health);
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
        freopen("/dev/null", "w", stdout);
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
    if (!settle())
        return false;
    for (int32 i = 0; i < 120; i++)
    {
        int16 d = int16(heading - lara()->angle.y);
        if (abs(d) <= tol)
        {
            if (settle())
                return abs(int16(heading - lara()->angle.y)) <= tol * 2;
            continue;
        }
        // near it, a tick at a time and no key between, so the turn doesn't run on past
        if (abs(d) < ANGLE(12) && lara()->state != Lara::STATE_STOP) {
            step(0);
            continue;
        }
        step(d > 0 ? IK_RIGHT : IK_LEFT);
    }
    return false;
}

// Run (or walk) straight at a point, steering; true once within radius.
static bool goStraight(int32 x, int32 z, int32 radius, bool walk, int32 maxTicks = 30 * 120)
{
    int32 best = distTo(x, z), since = 0;
    for (int32 i = 0; i < maxTicks; i++)
    {
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
        if (abs(d) > ANGLE(60))
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

// The level's boxes as Lara walks them: a step up or down of one click.
static const Box& box(int32 i)
{
    return level.boxes[i];
}


static bool boxHas(int32 b, int32 x, int32 z)
{
    const Box &bx = box(b);
    return x >= (bx.minX << 10) && x < (bx.maxX << 10) && z >= (bx.minZ << 10) && z < (bx.maxZ << 10);
}

// The boxes from a to b, a walk between each and the next (none: empty).
static int32 boxPath(int32 a, int32 b, uint16* path, int32 maxLen)
{
    static int32 from[4096], dist[4096];
    static uint16 queue[4096];
    int32 count = level.boxesCount;
    for (int32 i = 0; i < count; i++) {
        dist[i] = -1;
    }
    int32 head = 0, tail = 0;
    queue[tail++] = uint16(a);
    dist[a] = 0;
    from[a] = -1;
    while (head < tail && dist[b] < 0)
    {
        int32 c = queue[head++];
        uint16 o = box(c).overlap & NAV_INDEX;
        bool end = false;
        do {
            uint16 n = level.overlaps[o++];
            end = (n & NAV_BLOCKED) != 0;
            n &= NAV_INDEX;
            if (dist[n] >= 0)
                continue;
            if (box(n).overlap & BOX_BLOCKED)
                continue;
            int32 dy = box(n).floor - box(c).floor;
            if (dy < -256 || dy > 256)
                continue;
            dist[n] = dist[c] + 1;
            from[n] = c;
            queue[tail++] = n;
        } while (!end);
    }
    if (dist[b] < 0)
        return 0;
    int32 len = dist[b] + 1;
    if (len > maxLen)
        return 0;
    for (int32 i = len - 1, c = b; i >= 0; i--, c = from[c]) {
        path[i] = uint16(c);
    }
    return len;
}

// Where the walk from box a to box b crosses: the middle of their shared side.
static void crossing(int32 a, int32 b, int32 &x, int32 &z)
{
    const Box &p = box(a), &q = box(b);
    int32 x0 = X_MAX(p.minX, q.minX) << 10, x1 = X_MIN(p.maxX, q.maxX) << 10;
    int32 z0 = X_MAX(p.minZ, q.minZ) << 10, z1 = X_MIN(p.maxZ, q.maxZ) << 10;
    x = (x0 + x1) / 2;
    z = (z0 + z1) / 2;
}

// To a spot in a room along the boxes, running, walking the last of it.
static bool goTo(int32 roomIndex, int32 x, int32 z, bool walk, int32 radius = 96)
{
    Lara* l = lara();
    int32 a = l->room->getSector(l->pos.x, l->pos.z)->boxIndex;
    int32 b = rooms[roomIndex].getSector(x, z)->boxIndex;
    static uint16 path[512];
    int32 len = boxPath(a, b, path, X_COUNT(path));
    if (!len) {
        fail("no walk along the boxes to there");
        return false;
    }
    for (int32 i = 1; i < len; i++)
    {
        int32 cx, cz;
        crossing(path[i - 1], path[i], cx, cz);
        // past a crossing once inside the box after it
        if (boxHas(path[i], l->pos.x, l->pos.z))
            continue;
        if (!goStraight(cx, cz, 384, walk)) {
            fail("stuck on the way");
            return false;
        }
    }
    // the last of it walked, so she stops on the spot
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
    if (!goTo(int32(item->room - rooms), item->pos.x, item->pos.z, true, 64))
        return false;
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
    if (!goTo(int32(item->room - rooms), x, z, true, 64))
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

// Guns out, turned toward the nearest creature awake, Action held while she
// aims, till none is left within range.
static bool fight(int32 range = 8192, int32 maxTicks = 30 * 60)
{
    ExtraInfoLara* e = lara()->extraL;
    int32 quiet = 0;
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
        uint32 k = 0;
        int16 d = int16(headingTo(foe->pos.x, foe->pos.z) - lara()->angle.y);
        if (d > ANGLE(10)) {
            k |= IK_RIGHT;
        } else if (d < -ANGLE(10)) {
            k |= IK_LEFT;
        }
        if (e->armR.target || e->armL.target) {
            k |= IK_B;
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
        } else if (!strcmp(cmd, "fight")) {
            ok = fight();
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
