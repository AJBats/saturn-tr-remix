// The game's logic alone, headless: a level loaded from its Saturn pack, run
// tick by tick with the pad given, no picture and no sound. Built as the
// Saturn build is (host/build.sh: __SATURN__ structures, big-endian 32-bit),
// so the packs load unchanged; what the logic asks of the Saturn's hardware
// is stubbed here.
//
//   logic DATA_DIR LEVEL TICKS [KEYS@FIRST-LAST ...] [ACTION@TICK ...]
//         [--items] [--items-end] [--list-end]
//
// KEYS: UP, DOWN, LEFT, RIGHT, A, B, C, X, Y, Z, L, R, START joined by '+',
// held from tick FIRST to LAST. ACTION: hit:ITEM:DAMAGE, flag:ITEM:HEX or
// wake:ITEM (actsAt). Lara's state is printed every 30 ticks (EVERY in the
// environment); --items prints every item slot before the first tick,
// --items-end after the last, --list-end the items in rooms after the last.

#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>

// TINFO=ITEM: that creature's view of Lara each tick, as its state code sees it
void hostTargetInfo(const ItemObj* enemy)
{
    static int32 watched = getenv("TINFO") ? atoi(getenv("TINFO")) : -1;
    if (enemy - items != watched)
        return;
    printf("     tinfo %d: dist %d angle %d front %d behind %d bite %d zone %d/%d box %d/%d mood %d state %d goal %d next %d hit %08x\n",
           watched, tinfo.dist, tinfo.angle, tinfo.front, tinfo.behind, tinfo.canAttack, tinfo.zoneIndex, tinfo.zoneIndexTarget,
           tinfo.boxIndex, tinfo.boxIndexTarget, ((const Enemy*)enemy)->mood, enemy->state, enemy->goalState, enemy->nextState,
           enemy->hitMask);
}

// WATCHDOG=SECONDS: a run still going then prints where it is (addresses for
// addr2line -e build/host/logic) and stops
static void watchdog(int)
{
    void* frames[32];
    int32 count = backtrace(frames, 32);
    fprintf(stderr, "watchdog: still running, stack:");
    for (int32 i = 0; i < count; i++) {
        fprintf(stderr, " %p", frames[i]);
    }
    fprintf(stderr, "\n");
    _exit(3);
}

int32 fps;
CpuState gHostCpuState;
extern "C" { int32 gFocalScale = 1 << 14; }
bool gListPending;

// what newlib gives the Saturn build (host_platform.h)
char* itoa(int value, char* str, int base)
{
    sprintf(str, base == 16 ? "%x" : "%d", value);
    return str;
}

// Low work RAM where the Saturn has it: the level's pack loads at its bottom
// (saturn/os.cpp LEVEL_ADDR), the creatures' path searches sit at its top
// (enemy.h enemiesExtra), so the game's addresses are the Saturn's.
#define LOW_RAM     0x00200000
#define LEVEL_ADDR  ((uint8*)LOW_RAM)
#define LEVEL_MAX   (LOW_RAM_TOP - uint32(LOW_RAM) - ENEMIES_EXTRA_BYTES)

static const char* gDataDir = ".";

static void lowRamMap()
{
    void* p = mmap((void*)LOW_RAM, 0x100000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void*)LOW_RAM) {
        fprintf(stderr, "can't map low work RAM at %08x\n", LOW_RAM);
        exit(1);
    }
}

const void* osLoadLevel(LevelID id)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.PKD", gDataDir, (const char*)gLevelInfo[id].data);
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "can't open %s\n", path);
        exit(1);
    }
    size_t n = fread(LEVEL_ADDR, 1, LEVEL_MAX, f);
    fclose(f);
    if (n == LEVEL_MAX) {
        fprintf(stderr, "%s is bigger than low work RAM leaves\n", path);
        exit(1);
    }
    return LEVEL_ADDR;
}

const void* osLoadScreen(LevelID id) { return NULL; }
int32 osGetSystemTimeMS() { return 0; }     // as the Saturn's (saturn/os.cpp): every level's seeds 0
void osGameComplete() {}
void osDrawDebug() {}
void osJoyVibrate(int32 index, int32 L, int32 R) {}
void osSetPalette(const uint16* palette) {}
void osPlaceRooms() {}
void osRoomFacesFlipped() {}

// settings and the saved game: none in backup RAM, as a fresh Saturn
bool osSaveSettings() { return true; }
bool osLoadSettings() { return false; }
bool osCheckSave() { return false; }
bool osSaveGame() { return true; }
bool osLoadGame() { return false; }
bool osReadSave() { return false; }

// sound: none; SOUNDS=1 prints what the game plays and stops, by tick
static int32 gHostTick;

void sndInitSamples() {}
void sndFreeSamples() {}
void* sndPlaySample(int32 index, int32 volume, int32 pitch, int32 mode)
{
    if (getenv("SOUNDS")) {
        printf("tick %4d: sample %3d level %2d pitch %3d mode %d\n", gHostTick, index, volume, pitch, mode);
    }
    return NULL;
}
void sndPlayTrack(int32 track) {}
bool sndTrackIsPlaying() { return true; }
void sndStopTrack() {}
void sndStopSample(int32 index)
{
    if (getenv("SOUNDS")) {
        printf("tick %4d: sample %3d stopped\n", gHostTick, index);
    }
}
void sndStop() {}

// the renderer: none
void renderInit() {}
void renderFree() {}
void renderLevelInit() {}
void renderLevelFree() {}
void renderUploadReady() {}
void renderRoomsFlipped() {}
void renderTexturesShifted() {}
static Room* gNoRooms[1];
Room** renderWalk(Room* cameraRoom) { return gNoRooms; }
void renderModel(const ItemObj* item) {}
void renderModelUnder(const ItemObj* item) {}
void renderShadowCall(const ItemObj* item, int32 floor) {}
void renderStatic(const RoomMesh* mesh, int32 rx, int32 ry, int32 rz, int32 room, int32 index) {}
void renderSprite(int32 vx, int32 vy, int32 vz, int32 vg, int32 index) {}
void renderGlyph(int32 vx, int32 vy, int32 index) {}
void renderFill(int32 x, int32 y, int32 width, int32 height, int32 shade, int32 z) {}
void renderBorder(int32 x, int32 y, int32 width, int32 height, int32 color1, int32 color2, int32 z) {}
void renderBar(int32 x, int32 y, int32 width, int32 value, BarType type) {}
bool renderBackground(const void* background) { return false; }
void* copyBackground() { return NULL; }
void closeBackground() {}
void setViewport(const RectMinMax &vp) {}
void setPaletteIndex(int32 index) {}
void setCameraWater(bool water) {}
void setSceneClear(bool clear) {}
void clear() {}
void flush_c() {}

// The pad, as tick ranges of held keys.
struct Hold
{
    uint32 keys;
    int32 first, last;
};
static Hold gHolds[64];
static int32 gHoldCount;

static uint32 parseKeys(const char* s)
{
    static const struct { const char* name; uint32 key; } names[] = {
        { "UP", IK_UP }, { "DOWN", IK_DOWN }, { "LEFT", IK_LEFT }, { "RIGHT", IK_RIGHT },
        { "A", IK_A }, { "B", IK_B }, { "C", IK_C }, { "X", IK_X }, { "Y", IK_Y }, { "Z", IK_Z },
        { "L", IK_L }, { "R", IK_R }, { "START", IK_SELECT }    // inventory, as saturn/main.cpp maps it
    };
    uint32 k = 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", s);
    for (char* t = strtok(buf, "+"); t; t = strtok(NULL, "+"))
    {
        bool found = false;
        for (uint32 i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        {
            if (!strcmp(t, names[i].name)) {
                k |= names[i].key;
                found = true;
            }
        }
        if (!found) {
            fprintf(stderr, "no key %s\n", t);
            exit(1);
        }
    }
    return k;
}

// Test actions on an item at a tick, before that tick's update:
//   hit:ITEM:DAMAGE@TICK    the item hit (ItemObj::hit at its own position)
//   flag:ITEM:HEX@TICK      flag bits set (0x100 ONCE, as a spent trigger leaves it)
//   clear:ITEM:HEX@TICK     flag bits cleared (0x3E00 the code bits, as an antipad clears them)
//   wake:ITEM@TICK          its code bits completed and triggered, as a trigger's object action
//   fx:EFFECT@TICK          the level effect started, as a trigger's flip starts it
//   next:LEVEL@TICK         nextLevel to that LevelID, as a level's end (its "Save Game?" answered by A),
//                           or from the title a new game
//   give:TYPE@TICK          the pickup of that item type, as Lara picking it up
//   use:SLOT@TICK           that ring slot used, as chosen in the ring (a gun, a medipack)
struct Act
{
    char kind;
    int32 item, value, tick;
};
static Act gActs[64];
static int32 gActCount;

static bool parseAct(const char* arg)
{
    Act a;
    a.kind = arg[0];
    if (gActCount == X_COUNT(gActs) || (sscanf(arg, "hit:%d:%d@%d", &a.item, &a.value, &a.tick) != 3 &&
        sscanf(arg, "flag:%d:%x@%d", &a.item, &a.value, &a.tick) != 3 &&
        (a.kind = 'c', sscanf(arg, "clear:%d:%x@%d", &a.item, &a.value, &a.tick) != 3) &&
        (a.value = 0, a.kind = 'w', sscanf(arg, "wake:%d@%d", &a.item, &a.tick) != 2) &&
        (a.item = 0, a.kind = 'x', sscanf(arg, "fx:%d@%d", &a.value, &a.tick) != 2) &&
        (a.kind = 'n', sscanf(arg, "next:%d@%d", &a.value, &a.tick) != 2) &&
        (a.kind = 'g', sscanf(arg, "give:%d@%d", &a.value, &a.tick) != 2) &&
        (a.kind = 'u', sscanf(arg, "use:%d@%d", &a.value, &a.tick) != 2))) {
        return false;
    }
    gActs[gActCount++] = a;
    return true;
}

static void actsAt(int32 tick)
{
    for (int32 i = 0; i < gActCount; i++)
    {
        const Act &a = gActs[i];
        if (a.tick != tick)
            continue;
        ItemObj* item = items + a.item;
        if (a.kind == 'h') {
            item->hit(a.value, item->pos, 0);
        } else if (a.kind == 'f') {
            item->flags |= a.value;
        } else if (a.kind == 'c') {
            item->flags &= ~a.value;
        } else if (a.kind == 'x') {
            setEffect(a.value, players[0]);
        } else if (a.kind == 'u') {
            players[0]->useItem(InvSlot(a.value));
        } else if (a.kind == 'g') {
            inventory.add(ItemType(a.value));
        } else if (a.kind == 'n') {
            if (gLevelID == LVL_TR1_TITLE) {
                startInfoNewGame();     // the passport's Start Game
            }
            nextLevel(LevelID(a.value));
            inventory.state = INV_STATE_NONE;   // the ring shut, as its passport shuts it
        } else if (!(item->flags & ITEM_FLAG_ACTIVE)) {
            item->flags |= ITEM_FLAG_MASK;
            item->trigger();
        }
    }
}

// The items in rooms: index, type, room, position, status, health.
static void listItems()
{
    static const char* status[] = { "none", "active", "inactive", "invisible" };
    for (int32 i = 0; i < MAX_ITEMS; i++)
    {
        const ItemObj* item = items + i;
        if (!item->room)
            continue;
        printf("item %3d: type %3d room %3d pos %6d %6d %6d yaw %5d status %-9s health %d timer %d\n", i, item->type,
               int32(item->room - rooms), item->pos.x, item->pos.y, item->pos.z, uint16(item->angle.y),
               status[(item->flags & ITEM_FLAG_STATUS) >> ITEM_FLAGS_STATUS_SHIFT], item->health, item->timer);
    }
}

static uint32 keysAt(int32 tick)
{
    uint32 k = 0;
    for (int32 i = 0; i < gHoldCount; i++)
    {
        if (tick >= gHolds[i].first && tick <= gHolds[i].last) {
            k |= gHolds[i].keys;
        }
    }
    return k;
}

static void printLara(int32 tick)
{
    const Lara* lara = players[0];
    if (!lara) {
        printf("tick %4d: no Lara\n", tick);
        return;
    }
    printf("tick %4d: room %3d pos %6d %6d %6d angle %6d anim %3d frame %4d state %2d goal %2d health %4d water %d\n",
           tick, int32(lara->room - rooms), lara->pos.x, lara->pos.y, lara->pos.z, uint16(lara->angle.y),
           lara->animIndex, lara->frameIndex, lara->state, lara->goalState, lara->health, lara->waterState);
    if (getenv("INV"))      // INV=1: the level, the ring's counts and her rounds
    {
        printf("     level %d inv state %d page %d ring", gLevelID, inventory.state, inventory.page);
        for (int32 i = 0; i < SLOT_PASSPORT; i++) {
            if (inventory.counts[i]) {
                printf(" %d:%d", i, inventory.counts[i]);
            }
        }
        printf(" rounds %d %d %d %d\n", lara->extraL->ammo[0], lara->extraL->ammo[1], lara->extraL->ammo[2], lara->extraL->ammo[3]);
    }
    if (getenv("GUNS"))     // GUNS=1: her weapon, its state and her target
    {
        const ItemObj* target = lara->extraL->armR.target;
        printf("     head %d %d torso %d %d\n", lara->extraL->head.angle.x, lara->extraL->head.angle.y, lara->extraL->torso.angle.x, lara->extraL->torso.angle.y);
        printf("     weapon %d state %d goal %d target %d arm %d:%d aim %d\n", lara->extraL->weapon, lara->extraL->weaponState,
               lara->extraL->goalWeapon, target ? int32(target - items) : -1, lara->extraL->armR.animIndex,
               level.anims[lara->extraL->armR.animIndex].frameBegin + lara->extraL->armR.frameIndex, lara->extraL->armR.aim);
    }
}

// Every item slot's bytes, the pointers in them as indices (they differ from
// the Saturn's addresses): host/satitems.py prints the Saturn's the same way.
static int32 itemIndex(const ItemObj* p)
{
    return p ? int32(p - items) : -1;
}

static void printItems()
{
    printf("layout size %d room %d extra %d next %d active %d sizeof(Room) %d items %d\n",
           int32(sizeof(ItemObj)), int32(offsetof(ItemObj, room)), int32(offsetof(ItemObj, extra)),
           int32(offsetof(ItemObj, nextItem)), int32(offsetof(ItemObj, nextActive)), int32(sizeof(Room)),
           level.itemsCount);
    for (int32 i = 0; i < MAX_ITEMS; i++)
    {
        const ItemObj* item = items + i;
        uint8 raw[sizeof(ItemObj)];
        memcpy(raw, item, sizeof(raw));
        memset(raw, 0, 4);      // the vtable
        memset(raw + offsetof(ItemObj, room), 0, 4);
        memset(raw + offsetof(ItemObj, extra), 0, 4);
        memset(raw + offsetof(ItemObj, nextItem), 0, 4);
        memset(raw + offsetof(ItemObj, nextActive), 0, 4);
        printf("item %3d: room %3d extra %d next %3d active %3d raw ", i, item->room ? int32(item->room - rooms) : -1,
               item->extra ? 1 : 0, itemIndex(item->nextItem), itemIndex(item->nextActive));
        for (uint32 j = 0; j < sizeof(raw); j++) {
            printf("%02x", raw[j]);
        }
        printf("\n");
    }
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: logic DATA_DIR LEVEL TICKS [KEYS@FIRST-LAST ...] [--items]\n");
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);   // a line at a time, so a run that hangs still shows where
    if (getenv("WATCHDOG")) {
        signal(SIGALRM, watchdog);
        alarm(atoi(getenv("WATCHDOG")));
    }
    gDataDir = argv[1];
    int32 ticks = atoi(argv[3]);
    bool itemsDump = false, itemsEnd = false, listEnd = false;
    for (int32 i = 4; i < argc; i++)
    {
        if (!strcmp(argv[i], "--items")) {
            itemsDump = true;
            continue;
        }
        if (!strcmp(argv[i], "--items-end")) {
            itemsEnd = true;
            continue;
        }
        if (!strcmp(argv[i], "--list-end")) {
            listEnd = true;
            continue;
        }
        if (strchr(argv[i], ':')) {
            if (!parseAct(argv[i])) {
                fprintf(stderr, "bad action %s\n", argv[i]);
                return 1;
            }
            continue;
        }
        char* at = strchr(argv[i], '@');
        if (!at || gHoldCount == X_COUNT(gHolds)) {
            fprintf(stderr, "bad hold %s\n", argv[i]);
            return 1;
        }
        *at = 0;
        Hold &h = gHolds[gHoldCount++];
        h.keys = parseKeys(argv[i]);
        h.first = h.last = atoi(at + 1);
        if (const char* dash = strchr(at + 1, '-')) {
            h.last = atoi(dash + 1);
        }
    }

    gLevelID = LVL_MAX;
    for (int32 i = 0; i < LVL_MAX; i++)
    {
        const char* name = (const char*)gLevelInfo[i].data;
        if (name && !strcmp(name, argv[2])) {
            gLevelID = LevelID(i);
        }
    }
    if (gLevelID == LVL_MAX) {
        fprintf(stderr, "no level %s\n", argv[2]);
        return 1;
    }

    lowRamMap();

    // the master's CPU state, as saturn/slave.cpp cpuInit
    gHostCpuState.slave = false;
    gHostCpuState.matrixPtr = gHostCpuState.matrixStack;

    gameInit();
    if (itemsDump) {
        printItems();
    }
    for (const char* m = getenv("MODELS"); m && *m; m = strchr(m, ',') ? strchr(m, ',') + 1 : "")
    {
        const Model &model = level.models[atoi(m)];     // MODELS=TYPE,TYPE...: their models
        printf("model %3d: count %d start %d node %d anim %d", atoi(m), model.count, model.start, model.nodeIndex, model.animIndex);
        if (model.count > 0 && model.animIndex != 0xFFFF)
        {
            const Anim &anim = level.anims[model.animIndex];
            printf(" (frames %d-%d rate %d)", anim.frameBegin, anim.frameEnd, anim.frameRate);
        }
        printf("\n");
    }
    for (const char* m = getenv("ANIMS"); m && *m; m = strchr(m, ',') ? strchr(m, ',') + 1 : "")
    {
        const Anim &anim = level.anims[atoi(m)];       // ANIMS=INDEX,INDEX...: those animations and their state changes
        printf("anim %3d: state %d frames %d-%d rate %d speed %d accel %d next %d:%d changes", atoi(m), anim.state,
               anim.frameBegin, anim.frameEnd, anim.frameRate, anim.speed, anim.accel, anim.nextAnimIndex, anim.nextFrameIndex);
        for (int32 i = 0; i < anim.statesCount; i++)
        {
            const AnimState &s = level.animStates[anim.statesStart + i];
            for (int32 j = 0; j < s.rangesCount; j++)
            {
                const AnimRange &r = level.animRanges[s.rangesStart + j];
                printf(" [%d: %d-%d -> %d:%d]", s.state, r.frameBegin, r.frameEnd, r.nextAnimIndex, r.nextFrameIndex);
            }
        }
        printf("\n");
    }

    // As saturn/main.cpp runs it, a tick a frame: the first frame's update
    // whole; after it, while in play, the items' update a frame ahead (the
    // slave's), then the rest at the frame's top.
    clock_t t0 = clock();
    bool handed = false;
    for (int32 tick = 0; tick < ticks; tick++)
    {
        gHostTick = tick;
        keys = keysAt(tick);
        actsAt(tick);
        bool done = false;
        if (handed)
        {
            gameUpdateItems(1);
            done = true;
        }
        levelCommit();
        gameUpdate(1, !done);
        levelCommit();
        handed = gameInPlay();

        if (getenv("BOUNCE") && players[0] && players[0]->extraL->camera.bounce) {
            printf("tick %4d: camera bounce %d\n", tick, players[0]->extraL->camera.bounce);
        }

        if (tick % (getenv("EVERY") ? atoi(getenv("EVERY")) : 30) == 0 || tick == ticks - 1) {
            printLara(tick);
            for (const char* w = getenv("WATCH"); w && *w; w = strchr(w, ',') ? strchr(w, ',') + 1 : "")
            {
                const ItemObj* item = items + atoi(w);    // WATCH=ITEM,ITEM...: those too
                printf("     item %3d: room %3d pos %6d %6d %6d anim %3d frame %4d state %2d goal %2d flags %04x floor %6d health %d%s\n",
                       atoi(w), item->room ? int32(item->room - rooms) : -1, item->pos.x, item->pos.y, item->pos.z,
                       item->animIndex, item->frameIndex, item->state, item->goalState, item->flags, item->roomFloor,
                       item->health, item->extra ? "" : " no AI");
            }
        }
    }
    double s = double(clock() - t0) / CLOCKS_PER_SEC;
    if (itemsEnd) {
        printItems();
    }
    if (listEnd) {
        listItems();
    }
    if (getenv("DYN")) {
        printf("dynamic sectors %d of %d\n", dynSectorsCount, MAX_DYN_SECTORS);
    }
    fprintf(stderr, "%d ticks in %.3f s: %.0f ticks a second\n", ticks, s, s > 0 ? ticks / s : 0.0);
    return 0;
}
