#ifndef H_GAME
#define H_GAME

#include "common.h"
#include "room.h"
#include "camera.h"
#include "item.h"
#include "draw.h"
#include "nav.h"
#include "level.h"
#include "inventory.h"

EWRAM_DATA LevelID gNextLevel = LVL_MAX;
EWRAM_DATA uint32 gLevelStarts;     // levels, saves and cutscenes started: the clock restarts after each
EWRAM_DATA Nav::Cell* gLaraNavCells;  // the players' path searches' cells, a level's boxes each (gameLoadLevel)
static bool gSavePromptDue;     // the level's end asks "Save Game?" before the next loads
static bool gStatsDue;          // and before that shows the finished level's statistics
EWRAM_DATA LevelStats gLevelStats;

// Attract demos (retail's StartDemo 06011d48, ProcessDemoInput 06011f48, and
// ControlPhase 0600f66c's demo checks). Left idle, Press START plays the next
// level with a demo after the last level started (Vilcabamba, then Lost
// Valley): Lara with that level's start but her pistols holstered, both
// random seeds as at power-on, and DEMODATA's pad input, a word each tick in
// place of the pad's. START, the input's end, or her death (10 s, or 2 s at
// any input) goes back to the title, and the level's start is put back.
EWRAM_DATA bool gDemo;
EWRAM_DATA uint16 gDemoInput;
static const int32* gDemoWords;
static int32 gDemoAt;                   // the next word's
static uint32 gDemoTicks;               // the master's, for the flashing line
static LevelID gDemoLevel;
static StartInfo gDemoKept;             // the level's start, put back after
static LevelID gLastLevel = LVL_TR1_1;  // retail's 06060c28: 1 at boot, then the last level started

#define DEMO_HEADER 7       // Lara's x, y, z, her angles x, y, z, her room
#define DEMO_WORDS  9000    // retail reads no further

void gameStartDemo()
{
    gDemoLevel = (gLastLevel == LVL_TR1_2) ? LVL_TR1_3A : LVL_TR1_2;
    gDemoKept = gProgress.starts[gDemoLevel];
    StartInfo &s = gProgress.starts[gDemoLevel];
    s.ammo[WEAPON_PISTOLS] = 1000;
    s.guns |= 1 << WEAPON_PISTOLS;
    s.gun = WEAPON_PISTOLS;
    s.ready = 0;
    s.available = 1;
    gDemo = true;
    gDemoTicks = 0;
    sndStopTrack();
    gNextLevel = gDemoLevel;
}

void gameDemoEnd()
{
    if (gNextLevel == LVL_MAX) {
        gNextLevel = LVL_TR1_TITLE;
    }
}

// The level loaded: Lara where the demo starts her, its input from the
// first word, the seeds as at power-on.
static void demoBegin()
{
    gDemoWords = osLoadDemo(gLevelID);
    gDemoAt = DEMO_HEADER;
    Lara* lara = players[0];
    if (!gDemoWords || !lara)
    {
        gameDemoEnd();
        return;
    }

    const int32* h = gDemoWords;
    lara->pos = _vec3i(h[0], h[1], h[2]);
    lara->angle = _vec3s(int16(h[3]), int16(h[4]), int16(h[5]));
    if (lara->room != rooms + h[6])
    {
        lara->room->remove(lara);
        rooms[h[6]].add(lara);
    }

    gRandSeedLogic = gRandSeedDraw = int32(0xD371F947);
}

// Retail's input bits (S_UpdateInput, 060374b4) as ours; OPTION (0x100) ends
// a demo at the pad, never in its data.
static uint16 demoInput(int32 word)
{
    static const uint16 kBits[13] = {
        IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_JUMP, IN_WEAPON, IN_ACTION, IN_WALK,
        0, IN_LOOK, IN_STEP_LEFT, IN_STEP_RIGHT, IN_ROLL
    };
    uint16 in = 0;
    for (int32 b = 0; b < 13; b++)
    {
        if (word & (1 << b)) {
            in |= kBits[b];
        }
    }
    return in;
}

// A tick's input; false when the demo is over.
static bool demoTick()
{
    if (gNextLevel != LVL_MAX)
        return false;
    int32 word = (gDemoAt < DEMO_WORDS) ? gDemoWords[gDemoAt] : -1;
    if ((keys & IK_SELECT) || word == -1)
    {
        gameDemoEnd();
        return false;
    }
    gDemoAt++;
    gDemoInput = demoInput(word);
    return true;
}

// A level Lara plays: not the title, nor a cutscene.
static bool isPlayLevel(LevelID id)
{
    return (id != LVL_TR1_TITLE) && (id < LVL_LOAD) && (id != LVL_TR1_CUT_1) && (id != LVL_TR1_CUT_2) &&
           (id != LVL_TR1_CUT_3) && (id != LVL_TR1_CUT_4);
}

void nextLevel(LevelID next)
{
    // a level's end: what she takes into the next level she plays (retail's
    // CreateStartInfo, past the cutscene between), which is reached now; her
    // position saved before is behind her (retail clears start[21]). Coming
    // into a level from the one before, or its cutscene, "Save Game?" first.
    // the finished play level's statistics, shown on the way to the next
    // play level past any cutscene between (retail's level-complete
    // sequencer, 060163d8), or after the Pyramid before the ending
    if (next > gLevelID && isPlayLevel(gLevelID) && gLevelID != LVL_TR1_GYM)
    {
        gLevelStats.time = gSaveGame.time;
        gLevelStats.kills = gSaveGame.kills;
        gLevelStats.pickups = gSaveGame.pickups;
        gLevelStats.secrets = gSaveGame.secrets;
        gLevelStats.level = uint8(gLevelID);
        gStatsDue = true;
    }

    // the Pyramid finished: the bonus game from now, offered to save
    // (retail's stats screen sets the flag, then the save prompt)
    if (gLevelID == LVL_TR1_10C && next > gLevelID && next < LVL_LOAD)
    {
        gProgress.bonus = 1;
        gProgress.position = 0;
        gSavePromptDue = true;
    }

    if (next > gLevelID && next <= LVL_TR1_10C)
    {
        if (isPlayLevel(gLevelID) && gLevelID != LVL_TR1_GYM && players[0])
        {
            startInfoCreate(isPlayLevel(next) ? next : LevelID(next + 1), players[0]->extraL);
            gProgress.position = 0;
        }
        if (isPlayLevel(next) && (isPlayLevel(gLevelID) || isCutsceneLevel()) && gLevelID != LVL_TR1_GYM) {
            gSavePromptDue = true;
        }
    }

#if defined(__SATURN__) // the Saturn has every level, and the ending
    if (next > LVL_TR1_10C && next < LVL_LOAD)
    {
        osGameComplete();
        next = LVL_TR1_TITLE;
    }
#else
    if ((next == LVL_TR1_3A) && (inventory.state == INV_STATE_NONE)) // alpha version
    {
        inventory.open(players[0], INV_PAGE_END);
        return;
    }
#endif
    gNextLevel = next;
}

// A level started afresh from what she reached it with (the passport's
// Restart Level, Start Game, or a level from its Load Game list).
void gameStartLevel(LevelID id)
{
    gSavePromptDue = false;
    gStatsDue = false;
    gNextLevel = id;
}

// The progress alone, at a level's end: the levels reached, no position.
bool gameSaveProgress()
{
    gProgress.position = 0;
    gSaveGame.dataSize = 0;
    return osSaveGame();
}

// The saved game's progress and level, for the passport's list; the game
// played stays as it is (the file is read whole over the save buffer, whose
// item data is only ever a save's or a load's).
bool readSaveProgress(Progress &progress, uint8 &level)
{
    SaveGame game = gSaveGame;
    Progress keep = gProgress;
    bool ok = osReadSave();
    progress = gProgress;
    level = gSaveGame.level;
    gSaveGame = game;
    gProgress = keep;
    return ok;
}

bool gameSave()
{
    gProgress.position = 1;
    gSaveGame.level = gLevelID;
    gSaveGame.track = gCurTrack;

    memset(gSaveGame.invSlots, 0, sizeof(gSaveGame.invSlots));
    memcpy(gSaveGame.invSlots, inventory.counts, sizeof(inventory.counts));

    uint8* ptr = gSaveData;
    ItemObj* item = items;
    for (int32 i = 0; i < level.itemsCount; i++, item++)
    {
        ptr = item->save(ptr);
    }
    gSaveGame.dataSize = ptr - gSaveData;

    return osSaveGame();
}

bool gameLoad()
{
    if (!osLoadGame())
        return false;

    SaveGame tmp = gSaveGame;
    gLevelID = (LevelID)gSaveGame.level;
    startLevel(gLevelID);
    gSaveGame = tmp;

    // the level loads unflipped
    if (gSaveGame.flipped)
    {
        gSaveGame.flipped = 0;
        flipMap();
    }

    inventory.setSlots(gSaveGame.invSlots);
    inventory.replaceGuns();

    ItemObj::sFirstActive = NULL;
    ItemObj::sFirstFree = items + level.itemsCount;
    ItemObj::sFreeCount = MAX_ITEMS - level.itemsCount;

    uint8* ptr = gSaveData;
    ItemObj* item = items;
    for (int32 i = 0; i < level.itemsCount; i++, item++)
    {
        ptr = item->load(ptr);

        if (item->flags & ITEM_FLAG_ACTIVE) {
            item->activate();
        }
    }

    return true;
}

#if defined(START_LEVEL) || defined(TRS_HOST) || defined(TICK_REPLAY)
bool gTestNewGame;
#endif

#if defined(TICK_REPLAY) || defined(TRS_HOST)
// A tick replay's check (make REPLAY, host/host.cpp REPLAY): the game after a
// tick, Lara and a sum over the active items (but the effects that only look),
// alike on the host and the Saturn while the two run alike
// (tools/replaycheck.py sets them side by side).
struct ReplayRec
{
    uint32 tick;
    uint16 level, room;
    int32 x, y, z;
    uint16 yaw, anim, frame;
    int16 health;
    uint32 seed, items;
};

void replayRecord(ReplayRec &r, uint32 tick)
{
    memset(&r, 0, sizeof(r));
    r.tick = tick;
    r.level = uint16(gLevelID);
    if (const ItemObj* lara = players[0])
    {
        r.room = uint16(lara->room - rooms);
        r.x = lara->pos.x;
        r.y = lara->pos.y;
        r.z = lara->pos.z;
        r.yaw = uint16(lara->angle.y);
        r.anim = lara->animIndex;
        r.frame = lara->frameIndex;
        r.health = lara->health;
    }
    // (the effects that only look: they take the drawing's random numbers,
    // which the Saturn's renderer draws from too and the host never does)
    uint32 sum = 0;
    for (const ItemObj* item = ItemObj::sFirstActive; item; item = item->nextActive)
    {
        switch (item->type)
        {
            case ITEM_EXPLOSION: case ITEM_SPLASH: case ITEM_BUBBLE: case ITEM_BLOOD: case ITEM_SMOKE:
            case ITEM_RICOCHET: case ITEM_SPARKLES: case ITEM_MUZZLE_FLASH:
                continue;
            default:
                break;
        }
        sum = sum * 31 + uint32(item->pos.x) + uint32(item->pos.y) * 7 + uint32(item->pos.z) * 13 +
              uint32(item->animIndex) * 17 + item->frameIndex + uint32(item->health) * 19;
    }
    r.items = sum;
    r.seed = uint32(gRandSeedLogic);
}
#endif

void gameInit()
{
    drawInit();

    gSaveGame.dataSize = 0;

    inventory.init();

#if defined(START_LEVEL) || defined(TRS_HOST) || defined(TICK_REPLAY)
    // a level started straight from boot (make START_LEVEL) is a test's:
    // every gun, or as a new game starts it (gTestNewGame)
    if (gLevelID != LVL_TR1_TITLE)
    {
        if (gTestNewGame) {
            startInfoNewGame();
        } else {
            startInfoTestingKit(gLevelID);
        }
    }
#endif

    startLevel(gLevelID);
}

void gameFree()
{
    drawLevelFree();
    drawFree();
}

void resetLara(int32 index, int32 roomIndex, const vec3i &pos, int32 angleY)
{
    Lara* lara = players[index];

    lara->room->remove(lara);

    lara->pos = pos;
    lara->angle.y = angleY;
    lara->health = LARA_MAX_HEALTH;

    lara->extraL->camera.target.pos = lara->pos;
    lara->extraL->camera.target.room = lara->room;
    lara->extraL->camera.view = lara->extraL->camera.target;

    rooms[roomIndex].add(lara);
}

void gameLoadLevel(const void* data)
{
    drawLevelFree();

    memset(&gSaveGame, 0, sizeof(gSaveGame));
    memset(enemiesExtra, 0, ENEMIES_EXTRA_BYTES);

    ItemObj::sFirstActive = NULL;
    ItemObj::sFirstFree = NULL;

    gCurTrack = -1;

    readLevel((uint8*)data);
    pickupsClear();

    // from the low work RAM the level leaves: the creatures' and Lara's path
    // searches (hers follows water currents), a cell a box each, then all the
    // rest for the rooms' changeable sectors
    Nav::Cell* cells = (Nav::Cell*)osLowAlloc((MAX_ENEMIES + MAX_PLAYERS) * level.boxesCount * sizeof(Nav::Cell));
    for (int32 i = 0; i < MAX_ENEMIES; i++) {
        enemiesExtra[i].nav.cells = cells + i * level.boxesCount;
    }
    gLaraNavCells = cells + MAX_ENEMIES * level.boxesCount;
    dynSectorsCount = 0;
    dynSectorsMax = int32(osLowLeft() / sizeof(Sector));
    dynSectors = (Sector*)osLowAlloc(dynSectorsMax * sizeof(Sector));

    setEffect(FX_NONE, NULL);
    soundLoopsStop();
    soundSlotsClear();

    // a level she plays starts from what she reached it with (her
    // constructor takes her guns from it, the ring is filled from it below)
    bool play = isPlayLevel(gLevelID);

    // prepare rooms
    for (int32 i = 0; i < level.roomsCount; i++)
    {
        rooms[i].reset();
    }

    // prepare items free list
    items[MAX_ITEMS - 1].nextItem = NULL;
    for (int32 i = MAX_ITEMS - 2; i >= level.itemsCount; i--)
    {
        items[i].nextItem = items + i + 1;
    }
        ItemObj::sFirstFree = items + level.itemsCount;
    ItemObj::sFreeCount = MAX_ITEMS - level.itemsCount;

    for (int32 i = 0; i < MAX_PLAYERS; i++)
    {
        players[i] = NULL;
    }

    if (gLevelID == LVL_TR1_TITLE) {
        // init dummy Lara for updateInput()
        items->extraL = playersExtra;
        items->extraL->camera.mode = CAMERA_MODE_FOLLOW;
        inventory.open(items, INV_PAGE_PRESS_START);
    } else {
        inventory.page = INV_PAGE_MAIN;

        // init items
        for (int32 i = 0; i < level.itemsCount; i++)
        {
            const ItemObjInfo* info = level.itemsInfo + i;
            ItemObj* item = items + i;

            item->type = info->type;
            item->intensity = uint8(info->intensity);

            item->pos.x = info->pos.x + (rooms[info->roomIndex].info->x << 8);
            item->pos.y = info->pos.y;
            item->pos.z = info->pos.z + (rooms[info->roomIndex].info->z << 8);

            item->angle.y = ((info->flags >> 14) - 2) * ANGLE_90;
            item->flags = info->flags;

            if (item->type == ITEM_LARA) {
                players[0] = (Lara*)item;
            }

            item->init(rooms + info->roomIndex);
        }

        if (play) {
            inventory.startLevel();
        }

        if (isCutsceneLevel())
        {
            gCinematicCamera.initCinematic();
        }


    // gym
        //resetLara(0, 7, _vec3i(39038, -1280, 51712), ANGLE_90); // start
        //resetLara(0, 8, _vec3i(55994, 0, 52603), ANGLE_90); // piano
        //resetLara(0, 9, _vec3i(47672, 256, 40875), ANGLE_90); // hall
        //resetLara(0, 13, _vec3i(38953, 3328, 63961), ANGLE_90 + ANGLE_45); // pool
    // level 1
        //resetLara(0, 0, _vec3i(74588, 3072, 19673), ANGLE_0); // first darts
        //resetLara(0, 9, _vec3i(49669, 7680, 57891), ANGLE_0); // first door
        //resetLara(0, 10, _vec3i(43063, 7168, 61198), ANGLE_0); // transp
        //resetLara(0, 14, _vec3i(20215, 6656, 52942), ANGLE_90 + ANGLE_45); // bridge
        //resetLara(0, 25, _vec3i(8789, 5632, 80173), 0); // portal
        //resetLara(0, 17, _vec3i(16475, 6656, 59845), ANGLE_90); // bear
        //resetLara(0, 26, _vec3i(24475, 6912, 83505), ANGLE_90); // switch timer 1
        //resetLara(0, 35, _vec3i(35149, 2048, 74189), ANGLE_90); // switch timer 2
    // level 2
        //resetLara(0, 15, _vec3i(66179, 0, 25920), -ANGLE_90 - ANGLE_45); // sprites
        //resetLara(0, 19, _vec3i(61018, 1024, 31214), ANGLE_180); // block
        //resetLara(0, 14, _vec3i(64026, 512, 20806), ANGLE_0); // key and puzzle
        //resetLara(0, 5, _vec3i(55644, 0, 29155), -ANGLE_90); // keyhole
        //resetLara(0, 71, _vec3i(12705, -768, 30195), -ANGLE_90); // puzzle
        //resetLara(0, 63, _vec3i(31055, -2048, 33406), ANGLE_0); // right room
        //resetLara(0, 44, _vec3i(27868, -1024, 29191), -ANGLE_90); // swing blades
    // level 3a
        //resetLara(0, 44, _vec3i(73798, 2304, 9819), ANGLE_90); // uw gears
        //resetLara(0, 51, _vec3i(41015, 3584, 34494), ANGLE_180); // valley
    }

    drawLevelInit();
}

void startLevel(LevelID id)
{
    gLevelTrack = -1;

    sndStop();
    sndFreeSamples();

    const void* data = osLoadLevel(id);
    gameLoadLevel(data);

    if (id == LVL_TR1_TITLE)
    {
        if (gDemo)
        {
            gProgress.starts[gDemoLevel] = gDemoKept;
            gDemo = false;
        }
    }
    else
    {
        gLastLevel = id;
        if (gDemo) {
            demoBegin();
        }
    }

    sndInitSamples();
    if (inventory.page != INV_PAGE_BACKUP) {    // the title's backup pages are silent, as retail's: its track starts with Press START
        sndPlayTrack(getAmbientTrack());
    }
    if (isCutsceneLevel()) {
        sndWaitTrack();     // its pictures keep time with the track (retail's StartSyncedAudio, 0603c794)
    }
}

// What retail keeps as effects, not items (its FX list: gun flashes,
// splashes, blood, missiles, flames...)
static bool isEffect(const ItemObj* item)
{
    switch (item->type)
    {
        case ITEM_EXPLOSION:
        case ITEM_SPLASH:
        case ITEM_BUBBLE:
        case ITEM_BLOOD:
        case ITEM_SMOKE:
        case ITEM_RICOCHET:
        case ITEM_SPARKLES:
        case ITEM_MUZZLE_FLASH:
        case ITEM_MUTANT_BULLET:
        case ITEM_CENTAUR_BULLET:
        case ITEM_BODY_PART:
        case ITEM_LAVA_PARTICLE:
        case ITEM_FLAME:
            return true;
        default:
            return false;
    }
}

// Retail's ControlPhase: every item's control, then every effect's, newest
// first, so an effect an item makes this tick (a creature's gun flash) runs
// this tick too
void updateItems()
{
    for (int32 pass = 0; pass < 2; pass++)
    {
        ItemObj* item = ItemObj::sFirstActive;
        while (item)
        {
            ItemObj* next = item->nextActive;
            if (isEffect(item) == (pass == 1)) {
                item->update();
            }
            item = next;
        }
    }

    if (isCutsceneLevel())
    {
        gCinematicCamera.updateCinematic();
    }
    else
    {
        for (int32 i = 0; i < MAX_PLAYERS; i++)
        {
            if (players[i]) {
                players[i]->update();
            }
        }
    }
}

// Play, as against the inventory, the title, a cutscene or a level ending:
// what the items' update runs in.
bool gameInPlay()
{
    return inventory.page != INV_PAGE_TITLE && inventory.state == INV_STATE_NONE && gNextLevel == LVL_MAX &&
           !isCutsceneLevel() && players[0];
}

// The items' and the level's update for frames ticks: gameUpdate's in play,
// and on the Saturn the slave's for the next frame (saturn/main.cpp).
// The level's sound sources, every tick: those of the rooms as they are,
// unflipped (0x80) or flipped (0x40) (retail's SoundEffects, 06014fbc).
void soundSources()
{
    uint16 mask = gSaveGame.flipped ? 0x40 : 0x80;
    for (int32 i = 0; i < level.soundSourcesCount; i++)
    {
        const SoundSource &s = level.soundSources[i];
        if (s.flags & mask) {
            soundPlay(s.id, &s.pos);
        }
    }
}

void gameUpdateItems(int32 frames)
{
    for (int32 i = 0; i < frames; i++)
    {
        if (gDemo && !demoTick())
            break;
        updateItems();
        updateEffect();
        soundSources();
    }
    gSaveGame.time += frames;   // the level's time taken (retail's ControlPhase counts each tick)
    updateLevel(frames);
    soundLoopsEnd();
    soundSlotsUpdate();
}

// What the menus read the pad through: Lara, or with none (a cutscene's end)
// a stand-in holding only her input.
ItemObj* menuLara()
{
    if (players[0])
        return players[0];
    static uint32 standIn[(sizeof(ItemObj) + 3) / 4];
    ItemObj* item = (ItemObj*)standIn;
    item->extraL = playersExtra;
    return item;
}

// The game's update for frames ticks; without the items' if they are updated
// already (items false).
void gameUpdate(int32 frames, bool items = true)
{
    PROFILE(CNT_UPDATE);

    // A+B+C+START: back to the title (retail's soft reset, its vblank handler 0603938c)
    const uint32 reset = IK_A | IK_B | IK_C | IK_SELECT;     // the pad's START is IK_SELECT (saturn/main.cpp)
    if ((keys & reset) == reset && gLevelID != LVL_TR1_TITLE && gNextLevel == LVL_MAX)
    {
        if (inventory.state != INV_STATE_NONE)
        {
            closeBackground();
            inventory.state = INV_STATE_NONE;
        }
        gameStartLevel(LVL_TR1_TITLE);
    }


    if (frames > MAX_UPDATE_FRAMES) {
        frames = MAX_UPDATE_FRAMES;
    }
    gDemoTicks += frames;

    // the title's music loops, from Press START on (none on the backup
    // pages, as retail's); a level has none of its own, only what its
    // triggers play, once (retail plays no level-start track)
    if (gLevelID == LVL_TR1_TITLE && inventory.page != INV_PAGE_BACKUP && !sndTrackIsPlaying()) {
        gCurTrack = -1;
        sndPlayTrack(getAmbientTrack());
    }

    if (inventory.state != INV_STATE_NONE)
    {
        soundLoopsEnd();    // nothing plays them here: the loops stop, as in retail's inventory
        Lara* lara = (Lara*)inventory.lara;
        ASSERT(lara);
        lara->updateInput();
        inventory.update(frames);

        // closed onto the game, the CD plays on (retail's ControlPhase)
        if (inventory.state == INV_STATE_NONE && gNextLevel == LVL_MAX) {
            sndResume();
        }


        if ((inventory.page != INV_PAGE_TITLE) && (inventory.state == INV_STATE_NONE))
        {
            if (lara->useItem(inventory.useSlot)) {
                inventory.useSlot = SLOT_MAX;
            } else if (inventory.page == INV_PAGE_USE && inventory.useSlot != SLOT_MAX) {
                lara->useChosenAtHole();
            }
        }
    }

    if ((inventory.page != INV_PAGE_TITLE) && (inventory.state == INV_STATE_NONE) && (gNextLevel == LVL_MAX) && items) {
        gameUpdateItems(frames);
    }

    if ((gNextLevel != LVL_MAX) && (inventory.state == INV_STATE_NONE) && gStatsDue &&
        (isPlayLevel(gNextLevel) || gNextLevel == LVL_TR1_TITLE))
    {
        gStatsDue = false;
        osFadeOut();    // retail's stats screen fades to black first (06016558)
        inventory.openStats(menuLara(), gLevelStats);
        return;
    }

    if ((gNextLevel != LVL_MAX) && (inventory.state == INV_STATE_NONE) && gSavePromptDue)
    {
        gSavePromptDue = false;
        if (osBackupOn())
        {
            inventory.openSave(menuLara(), NULL, true);
            return;
        }
    }

    if ((gNextLevel != LVL_MAX) && (inventory.state == INV_STATE_NONE))
    {
        gLevelID = gNextLevel;
        gNextLevel = LVL_MAX;
        if (gLevelID == LVL_LOAD) {
            gameLoad();
        } else {
            startLevel(gLevelID);
        }
        gLevelStarts++;

        // The new level's first tick reads the pad after the load, as retail's
        // ControlPhase does: the press that shut the last menu is not held
        // into it (a player has let go by then; ours would still see it, the
        // load done in the same tick, and Lara hopped at the start).
        uint32 held = keys;
        keys = 0;
        gameUpdate(1);
        keys = held;
    }
}

void gameRender()
{
    {
        PROFILE(CNT_RENDER);

        setViewport(RectMinMax(0, 0, FRAME_WIDTH, FRAME_HEIGHT));

        if (inventory.state == INV_STATE_NONE)
        {
            clear();

            for (int32 i = 0; i < MAX_PLAYERS; i++)
            {
                // TODO set viewports for coop
            #ifndef PROFILE_SOUNDTIME
                if (isCutsceneLevel()) {
                    drawCinematicRooms();
                } else {
                    drawRooms(&players[i]->extraL->camera);
                }
            #endif

                if (players[i])
                {
                    drawHUD(players[i]);
                }

                // retail's StartDemo: "DEMO PLAY" top left, and "Press START
                // button" 16 from the bottom flashing every 20 vblanks
                if (gDemo)
                {
                    drawText(16, 26, STR[STR_DEMO_PLAY], TEXT_ALIGN_LEFT);
                    if (!((gDemoTicks / 10) & 1)) {
                        drawText(0, FRAME_HEIGHT - 16, STR[STR_PRESS_START], TEXT_ALIGN_CENTER);
                    }
                }
            }
        } else {
            inventory.draw();
        }

        //if (inventory.state == INV_STATE_NONE)
        {
            drawFPS();
        }

        flush();
    }

#ifdef PROFILING
    drawProfiling();
    #ifndef PROFILE_SOUNDTIME
        PROFILE_CLEAR();
    #endif
#endif
}

#endif
