#ifndef H_INVENTORY
#define H_INVENTORY

#include "common.h"

#ifdef __GBA__
extern const uint8_t TITLE_SCR[];
#else
extern const void* TITLE_SCR;
#endif

#define INV_CAMERA_HEIGHT   -1536
#define INV_CAMERA_Y        96
#define INV_CAMERA_Z        768
#define INV_RING_RADIUS     688
#define INV_TILT            ANGLE(-45)

enum InvState {
    INV_STATE_NONE,
    INV_STATE_OPENING,
    INV_STATE_CLOSING,
    INV_STATE_CLOSE,
    INV_STATE_READY,
    INV_STATE_DEATH,
    INV_STATE_SPIN,
    INV_STATE_SELECT,
    INV_STATE_DESELECT,
    INV_STATE_SHOW,
    INV_STATE_PAGE_MAIN,
    INV_STATE_PAGE_KEYS,
    INV_STATE_PAGE_OPTIONS
};

enum InvPage {
    INV_PAGE_TITLE,
    INV_PAGE_DEATH,
    INV_PAGE_END,
    INV_PAGE_USE,
    INV_PAGE_MAIN,
    INV_PAGE_KEYS,
    INV_PAGE_OPTIONS,
    INV_PAGE_SAVE,
    INV_PAGE_STATS
};

enum InvSlot {
// Items
    SLOT_LEADBAR,
    SLOT_KEY_ITEM_1,
    SLOT_KEY_ITEM_2,
    SLOT_KEY_ITEM_3,
    SLOT_KEY_ITEM_4,
    SLOT_PUZZLE_4,
    SLOT_PUZZLE_3,
    SLOT_PUZZLE_2,
    SLOT_PUZZLE_1,
    SLOT_SCION,
// Inventory
    SLOT_COMPASS,
    SLOT_PISTOLS,
    SLOT_AMMO_PISTOLS,
    SLOT_SHOTGUN,
    SLOT_AMMO_SHOTGUN,
    SLOT_MAGNUMS,
    SLOT_AMMO_MAGNUMS,
    SLOT_UZIS,
    SLOT_AMMO_UZIS,
    SLOT_MEDIKIT_BIG,
    SLOT_MEDIKIT_SMALL,
// Options
    SLOT_PASSPORT,
    SLOT_DETAIL,
    SLOT_SOUND,
    SLOT_CONTROLS,
    SLOT_HOME,
    SLOT_MAX
};

enum PassportPage {
    PASSPORT_PAGE_LOAD_GAME,
    PASSPORT_PAGE_RESTART,
    PASSPORT_PAGE_EXIT_TO_TITLE
};

enum OptionID
{
// controls
    OPT_ID_RUMBLE = 0,
    OPT_ID_SWAP = 1,
// audio
    OPT_ID_SFX = 0,
    OPT_ID_MUSIC = 1,
// video
    OPT_ID_GAMMA = 0,
    OPT_ID_FPS = 1,
    OPT_ID_VSYNC = 2,
// passport
    OPT_ID_OK = 5
};

struct InvItem
{
    uint8 type;
    uint8 snd;
    int16 selDist;
    int16 selRotPre;
    int16 selRotX;
    int16 selRotY;
    StringID str;
};

const InvItem INV_SLOTS[SLOT_MAX] = {
    { ITEM_INV_LEADBAR        , SND_INV_SHOW      ,   256   ,   ANGLE(20)   ,   ANGLE(44)   ,   ANGLE(67)  ,   STR_LEAD_BAR      },
    { ITEM_INV_KEY_ITEM_1     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_2     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_3     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_4     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_PUZZLE_4       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_3       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_2       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_1       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_SCION          , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(45)  ,   STR_SCION         },
    { ITEM_INV_COMPASS        , SND_INV_COMPASS   ,   512   ,   ANGLE(20)   ,   ANGLE(70)   ,   ANGLE(0)   ,   STR_COMPASS       },
    { ITEM_INV_PISTOLS        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_PISTOLS       },
    { ITEM_INV_AMMO_PISTOLS   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_PISTOLS  },
    { ITEM_INV_SHOTGUN        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_SHOTGUN       },
    { ITEM_INV_AMMO_SHOTGUN   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_SHOTGUN  },
    { ITEM_INV_MAGNUMS        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_MAGNUMS       },
    { ITEM_INV_AMMO_MAGNUMS   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_MAGNUMS  },
    { ITEM_INV_UZIS           , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_UZIS          },
    { ITEM_INV_AMMO_UZIS      , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_UZIS     },
    { ITEM_INV_MEDIKIT_BIG    , SND_INV_SHOW      ,   352   ,   ANGLE(20)   ,   ANGLE(44)   ,   ANGLE(22)  ,   STR_MEDI_BIG      },
    { ITEM_INV_MEDIKIT_SMALL  , SND_INV_SHOW      ,   216   ,   ANGLE(22)   ,   ANGLE(40)   ,   ANGLE(22)  ,   STR_MEDI_SMALL    },
    { ITEM_INV_PASSPORT       , SND_INV_SHOW      ,   384   ,   ANGLE(25)   ,   ANGLE(23)   ,   ANGLE(0)   ,   STR_GAME          },
    { ITEM_INV_DETAIL         , SND_INV_SHOW      ,   424   ,   ANGLE(22)   ,   ANGLE(36)   ,   ANGLE(0)   ,   STR_DETAIL        },
    { ITEM_INV_SOUND          , SND_INV_SHOW      ,   368   ,   ANGLE(26)   ,   ANGLE(12)   ,   ANGLE(0)   ,   STR_SOUND         },
    { ITEM_INV_CONTROLS       , SND_INV_CONTROLS  ,   352   ,   ANGLE(30)   ,   ANGLE(67)   ,   ANGLE(0)   ,   STR_CONTROLS      },
    { ITEM_INV_HOME           , SND_INV_HOME      ,   384   ,   ANGLE(25)   ,   ANGLE(23)   ,   ANGLE(0)   ,   STR_HOME          },
};

#define FRAME_PASSPORT 14
#define FRAME_COMPASS  10

// Each gun's ring slot, its ammo's, its pickup and its ammo's, and a box's rounds.
static const struct GunSlots { InvSlot gun, ammo; ItemType item, ammoItem; int16 box; } gGunSlots[WEAPON_NONE] = {
    { SLOT_PISTOLS, SLOT_AMMO_PISTOLS, ITEM_PISTOLS, ITEM_AMMO_PISTOLS, 0            },
    { SLOT_MAGNUMS, SLOT_AMMO_MAGNUMS, ITEM_MAGNUMS, ITEM_AMMO_MAGNUMS, AMMO_MAGNUMS },
    { SLOT_UZIS,    SLOT_AMMO_UZIS,    ITEM_UZIS,    ITEM_AMMO_UZIS,    AMMO_UZIS    },
    { SLOT_SHOTGUN, SLOT_AMMO_SHOTGUN, ITEM_SHOTGUN, ITEM_AMMO_SHOTGUN, AMMO_SHOTGUN },
};

struct Inventory
{
    InvSlot useSlot;

    int32 numKeys;

    int16 counts[SLOT_MAX];

    int32 height;
    int32 radius;
    int16 pitch;
    int16 rot;
    int16 rotItem;
    int32 selDist;
    int16 selRotPre;
    int16 selRotX;
    int16 selRotY;

    ItemObj* lara;
    InvPage page;
    InvState state;
    InvState nextState;
    int32 timer;

    int32 heightTarget;
    int32 heightInc;
    int32 radiusTarget;
    int32 radiusInc;
    int32 pitchTarget;
    int32 pitchInc;
    int32 rotTarget;
    int32 rotInc;
    int32 rotItemInc;
    int32 selDistTarget;
    int32 selDistInc;
    int32 selRotPreTarget;
    int32 selRotPreInc;
    int32 selRotXTarget;
    int32 selRotXInc;
    int32 selRotYTarget;
    int32 selRotYInc;
 
    InvSlot itemsList[SLOT_MAX];
    int32   itemsCount;
    uint32  itemVisMask;
    int32   itemIndex;
    int32   nextIndex;
    int32   optionIndex;
    int32   optionsCount;
    int32   optionsWidth;
    int32   optionsHeight;

    PassportPage passportPage;
    bool hasSave;               // a saved game is in backup RAM, the passport's Load Game page there

    // the passport's Load Game list: the saved game's progress and level
    Progress fileProgress;
    uint8 fileLevel;

    // the level statistics screen (INV_PAGE_STATS)
    LevelStats stats;

    // the "Save Game?" prompt (INV_PAGE_SAVE)
    int32 savePrompt;           // SAVE_ASK, SAVE_RETRY, SAVE_DONE
    bool saveProgressOnly;      // a level's end's: the progress, no position
    ItemObj* saveCrystal;       // the crystal it's for, used up by a save

    int32 frameIndex;
    int32 frameTarget;

    const void* background;

    enum OptionType {
        OPT_SPACE,
        OPT_BUTTON,
        OPT_BAR,
        OPT_SWITCH,
        OPT_VALUE,
        OPT_TEXT
    };

    #define OPTION(t,s,v)\
        options[optionsCount].type  = t;\
        options[optionsCount].str   = s;\
        options[optionsCount].value = v;\
        optionsCount++

    #define OPTION_SPACE()       optionsHeight +=  8; OPTION(OPT_SPACE, STR_EMPTY, 0)
    #define OPTION_BTN(s,v)      optionsHeight += 18; OPTION(OPT_BUTTON, s, v)
    #define OPTION_BAR(s,v)      optionsHeight += 18; OPTION(OPT_BAR, s, v)
    #define OPTION_SWITCH(s,v)   optionsHeight += 18; OPTION(OPT_SWITCH, s, v)
    #define OPTION_TEXT(s)       optionsHeight += 18; OPTION(OPT_TEXT, s, 0)

    struct Option
    {
        OptionType type;
        StringID str;
        int32 value;
    };

    Option options[32];

    void init()
    {
        memset(counts, 0, sizeof(counts));

        useSlot = SLOT_MAX;
        numKeys = 0;

        add(ITEM_INV_PASSPORT);
        add(ITEM_INV_DETAIL);
        add(ITEM_INV_SOUND);
        add(ITEM_INV_CONTROLS);
        add(ITEM_INV_HOME);

        add(ITEM_INV_COMPASS);
    }

    // The copies left in the level of a gun she has turn to boxes of its
    // ammo (retail's GlobalItemReplace).
    void replaceGuns()
    {
        for (int32 w = WEAPON_MAGNUMS; w < WEAPON_NONE; w++)
        {
            if (!counts[gGunSlots[w].gun])
                continue;
            for (int32 i = 0; i < level.itemsCount; i++)
            {
                if (items[i].type == gGunSlots[w].item) {
                    items[i].type = gGunSlots[w].ammoItem;
                }
            }
        }
    }

    // A level's start (retail's InitialiseLaraInventory, 06020b20): the ring
    // emptied but for the compass and the options, then filled with what she
    // brought (the level's start in gProgress): her guns, boxes of ammo for
    // those she hasn't, her medipacks and scions.
    void startLevel()
    {
        for (int32 i = 0; i < SLOT_PASSPORT; i++)
        {
            if (i != SLOT_COMPASS) {
                counts[i] = 0;
            }
        }

        const StartInfo &s = gProgress.starts[gLevelID];
        for (int32 w = 0; w < WEAPON_NONE; w++)
        {
            if (s.guns & (1 << w)) {
                counts[gGunSlots[w].gun] = 1;
            } else if (gGunSlots[w].box) {
                counts[gGunSlots[w].ammo] = s.ammo[w] / gGunSlots[w].box;
            }
        }
        counts[SLOT_SCION] = s.scions;
        counts[SLOT_MEDIKIT_SMALL] = s.medis;
        counts[SLOT_MEDIKIT_BIG] = s.bigMedis;
        numKeys = s.scions;

        replaceGuns();
    }

    ItemType remapToInv(ItemType type)
    {
        switch (type)
        {
            case ITEM_PISTOLS               : return ITEM_INV_PISTOLS;
            case ITEM_SHOTGUN               : return ITEM_INV_SHOTGUN;
            case ITEM_MAGNUMS               : return ITEM_INV_MAGNUMS;
            case ITEM_UZIS                  : return ITEM_INV_UZIS;
            case ITEM_AMMO_PISTOLS          : return ITEM_INV_AMMO_PISTOLS;
            case ITEM_AMMO_SHOTGUN          : return ITEM_INV_AMMO_SHOTGUN;
            case ITEM_AMMO_MAGNUMS          : return ITEM_INV_AMMO_MAGNUMS;
            case ITEM_AMMO_UZIS             : return ITEM_INV_AMMO_UZIS;
            case ITEM_MEDIKIT_SMALL         : return ITEM_INV_MEDIKIT_SMALL;
            case ITEM_MEDIKIT_BIG           : return ITEM_INV_MEDIKIT_BIG;
            case ITEM_PUZZLE_1              : return ITEM_INV_PUZZLE_1;
            case ITEM_PUZZLE_2              : return ITEM_INV_PUZZLE_2;
            case ITEM_PUZZLE_3              : return ITEM_INV_PUZZLE_3;
            case ITEM_PUZZLE_4              : return ITEM_INV_PUZZLE_4;
            case ITEM_LEADBAR               : return ITEM_INV_LEADBAR;
            case ITEM_KEY_ITEM_1            : return ITEM_INV_KEY_ITEM_1;
            case ITEM_KEY_ITEM_2            : return ITEM_INV_KEY_ITEM_2;
            case ITEM_KEY_ITEM_3            : return ITEM_INV_KEY_ITEM_3;
            case ITEM_KEY_ITEM_4            : return ITEM_INV_KEY_ITEM_4;
            case ITEM_SCION_PICKUP_QUALOPEC :
            case ITEM_SCION_PICKUP_DROP     :
            case ITEM_SCION_PICKUP_HOLDER   : return ITEM_INV_SCION;
            default                         : ;
        }
        return type;
    }

    InvSlot remapToSlot(ItemType type)
    {
        type = remapToInv(type);

        for (int32 i = 0; i < X_COUNT(INV_SLOTS); i++)
        {
            if (INV_SLOTS[i].type == type)
                return (InvSlot)i;
        }

        ASSERT(false);
        return SLOT_COMPASS;
    }

    InvSlot remapHoleToSlot(ItemType type)
    {
        switch (type)
        {
            case ITEM_PUZZLEHOLE_1 : return SLOT_PUZZLE_1;
            case ITEM_PUZZLEHOLE_2 : return SLOT_PUZZLE_2;
            case ITEM_PUZZLEHOLE_3 : return SLOT_PUZZLE_3;
            case ITEM_PUZZLEHOLE_4 : return SLOT_PUZZLE_4;
            case ITEM_KEYHOLE_1    : return SLOT_KEY_ITEM_1;
            case ITEM_KEYHOLE_2    : return SLOT_KEY_ITEM_2;
            case ITEM_KEYHOLE_3    : return SLOT_KEY_ITEM_3;
            case ITEM_KEYHOLE_4    : return SLOT_KEY_ITEM_4;
            case ITEM_MIDAS_HAND   : return SLOT_LEADBAR;
            default                : ;
        }
        return SLOT_MAX;
    }

    void add(ItemType type, int32 count = 1)
    {
        InvSlot slot = remapToSlot(type);

        // retail's Inv_AddItem (060196d8): a gun takes in the boxes she
        // carried and a box's worth of its own, and its copies in the level
        // become boxes; a box goes into the gun she has, or into the ring
        for (int32 w = WEAPON_MAGNUMS; w < WEAPON_NONE; w++)
        {
            const GunSlots &g = gGunSlots[w];
            int16 &ammo = playersExtra[0].ammo[w];
            if (slot == g.gun)
            {
                ammo += (counts[g.ammo] + 1) * g.box;
                counts[g.ammo] = 0;
                counts[slot] = 1;
                replaceGuns();
                return;
            }
            if (slot == g.ammo && counts[g.gun])
            {
                ammo += count * g.box;
                return;
            }
        }

        counts[slot] += count;
        // TODO check max

        if (slot < SLOT_COMPASS) {
            numKeys += count;
        }
    }

    void remove(InvSlot slot, int32 count)
    {
        counts[slot] -= count;

        if (slot < SLOT_COMPASS) {
            numKeys -= count;
        }
    }

    void setSlots(uint16* invSlots)
    {
        numKeys = 0;

        memcpy(counts, invSlots, sizeof(counts));

        for (int32 i = 0; i < SLOT_COMPASS; i++)
        {
            numKeys += counts[i];
        }
    }

    bool applyItem(ItemObj* hole)
    {
        #define CHECK_CASE(A, B) case A: { if (useSlot != B) return false; break; }

        switch (hole->type)
        {
            CHECK_CASE(ITEM_PUZZLEHOLE_1, SLOT_PUZZLE_1);
            CHECK_CASE(ITEM_PUZZLEHOLE_2, SLOT_PUZZLE_2);
            CHECK_CASE(ITEM_PUZZLEHOLE_3, SLOT_PUZZLE_3);
            CHECK_CASE(ITEM_PUZZLEHOLE_4, SLOT_PUZZLE_4);
            CHECK_CASE(ITEM_KEYHOLE_1,    SLOT_KEY_ITEM_1);
            CHECK_CASE(ITEM_KEYHOLE_2,    SLOT_KEY_ITEM_2);
            CHECK_CASE(ITEM_KEYHOLE_3,    SLOT_KEY_ITEM_3);
            CHECK_CASE(ITEM_KEYHOLE_4,    SLOT_KEY_ITEM_4);
            default: return false;
        }

        remove(useSlot, 1);
        useSlot = SLOT_MAX;

        return true;
    }

    void setState(InvState state, InvState nextState, int32 timer)
    {
        this->state = state;
        this->nextState = nextState;
        this->timer = timer;

        rotItemInc = -rotItem / timer;
    }

    void setHeight(int32 target)
    {
        heightTarget = target;
        heightInc = (target - height) / timer;
    }

    void setRadius(int32 target)
    {
        radiusTarget = target;
        radiusInc = (target - radius) / timer; 
    }

    void setPitch(int32 target)
    {
        pitchTarget = target;
        pitchInc = (target - pitch) / timer; 
    }

    void setRot(int32 delta, int32 target)
    {
        rotTarget = target;
        rotInc = delta / timer;
    }

    void setSelection(bool selected)
    {
        InvSlot slot = itemsList[itemIndex];

        if (selected) {
            const InvItem &item = INV_SLOTS[slot];
            selDistTarget = item.selDist;
            selRotPreTarget = item.selRotPre;
            selRotXTarget = item.selRotX;
            selRotYTarget = item.selRotY;
        } else {
            selDistTarget = 0;
            selRotPreTarget = 0;
            selRotXTarget = 0;
            selRotYTarget = 0;
        }

        selDistInc = (selDistTarget - selDist) / timer;
        selRotPreInc = (selRotPreTarget - selRotPre) / timer;
        selRotXInc = (selRotXTarget - selRotX) / timer;
        selRotYInc = (selRotYTarget - selRotY) / timer;

        frameTarget = (selected) ? getAnimLength() : 0;
        itemVisMask = 0xFFFFFFFF;

        if (selected && slot == SLOT_PASSPORT) {
            hasSave = osCheckSave();
            passportPage = hasSave ? PASSPORT_PAGE_LOAD_GAME : PASSPORT_PAGE_RESTART;
            frameTarget += passportPage * 5;
        }
    }

    void setPage(InvPage page)
    {
        this->page = page;

        itemsCount = 0;

        #define ADD_SLOT(slot) if (counts[slot]) itemsList[itemsCount++] = slot;

        switch (page)
        {
            case INV_PAGE_DEATH:
            {
                ADD_SLOT(SLOT_PASSPORT);
                ADD_SLOT(SLOT_DETAIL);
                break;
            }

            case INV_PAGE_TITLE:
            case INV_PAGE_OPTIONS:
            {
                ADD_SLOT(SLOT_PASSPORT);
                ADD_SLOT(SLOT_CONTROLS);
                ADD_SLOT(SLOT_DETAIL);
                ADD_SLOT(SLOT_SOUND);
                if (page == INV_PAGE_TITLE)
                {
                    ADD_SLOT(SLOT_HOME);
                }
                break;
            }

            case INV_PAGE_MAIN:
            {
                ADD_SLOT(SLOT_COMPASS);

                if (gLevelID != LVL_TR1_GYM)
                {
                    // a gun's boxes take its place, as she can't have both
                    ADD_SLOT(SLOT_PISTOLS);
                    ADD_SLOT(SLOT_SHOTGUN);
                    ADD_SLOT(SLOT_AMMO_SHOTGUN);
                    ADD_SLOT(SLOT_MAGNUMS);
                    ADD_SLOT(SLOT_AMMO_MAGNUMS);
                    ADD_SLOT(SLOT_UZIS);
                    ADD_SLOT(SLOT_AMMO_UZIS);
                    ADD_SLOT(SLOT_MEDIKIT_BIG);
                    ADD_SLOT(SLOT_MEDIKIT_SMALL);
                }
                break;
            }

            case INV_PAGE_USE:
            case INV_PAGE_KEYS:
            {
                ADD_SLOT(SLOT_LEADBAR);
                ADD_SLOT(SLOT_KEY_ITEM_1);
                ADD_SLOT(SLOT_KEY_ITEM_2);
                ADD_SLOT(SLOT_KEY_ITEM_3);
                ADD_SLOT(SLOT_KEY_ITEM_4);
                ADD_SLOT(SLOT_PUZZLE_4);
                ADD_SLOT(SLOT_PUZZLE_3);
                ADD_SLOT(SLOT_PUZZLE_2);
                ADD_SLOT(SLOT_PUZZLE_1);
                ADD_SLOT(SLOT_SCION);
                break;
            }
            default: ;
        }

        itemIndex = nextIndex = 0;

        if (itemsCount > 0) {
            setRot(ANGLE_180, itemIndex * ANGLE_360 / itemsCount);
        }

        rot = rotTarget - ANGLE_180;
    }

    int32 getItemIndexForSlot(InvSlot slot)
    {
        for (int32 i = 0; i < itemsCount; i++)
        {
            if (itemsList[i] == slot)
                return i;
        }
        return 0;
    }

    void open(ItemObj* lara, InvPage page, int32 arg = 0)
    {
        this->lara = lara;

        if (page != INV_PAGE_TITLE)
        {
            soundLoopsStop();
            sndPause();
        }


        if (page == INV_PAGE_TITLE) {
            background = osLoadScreen(LVL_TR1_TITLE);
        } else {
            background = copyBackground();
        }

        height = INV_CAMERA_HEIGHT;
        pitch = (page == INV_PAGE_TITLE) ? 1024 : 0;
        radius = 0;
        frameIndex = 0;
        useSlot = SLOT_MAX;
        itemVisMask = 0xFFFFFFFF;

        if (page == INV_PAGE_END)
        {
            sndPlayTrack(3);
            soundPlay(SND_HEALTH, NULL);
            this->page = page;
            this->state = INV_STATE_READY;
            return;
        }

        setState(INV_STATE_OPENING, (page == INV_PAGE_DEATH) ? INV_STATE_DEATH : INV_STATE_READY, 16);
        setHeight(-256);
        setRadius(INV_RING_RADIUS);
        setPage(page);

        if (page == INV_PAGE_USE)
        {
            itemIndex = nextIndex = getItemIndexForSlot(remapHoleToSlot((ItemType)arg));
            setRot(ANGLE_180, itemIndex * ANGLE_360 / itemsCount);
            rot = rotTarget - ANGLE_180;
        }

        soundPlay(SND_INV_SHOW, NULL);

        update(1);
    }

    // A finished level's statistics (retail's 06016558) on the options
    // picture: its name, kills, pickups, secrets of its total, and the time
    // taken; 4 frames, then select goes on.
    void openStats(ItemObj* lara, const LevelStats &levelStats)
    {
        this->lara = lara;
        stats = levelStats;
        background = osLoadScreen(LVL_TR1_TITLE);
        page = INV_PAGE_STATS;
        state = INV_STATE_READY;
        nextState = INV_STATE_NONE;
        timer = 4;
    }

    void onStats(int32 frames)
    {
        if (timer > 0)
        {
            timer -= frames;
            return;
        }

        if (lara->isKeyHit(IN_ACTION))
        {
            closeBackground();
            page = INV_PAGE_MAIN;
            state = INV_STATE_NONE;
        }
    }

    static char* append(char* p, const char* s)
    {
        while (*s) {
            *p++ = *s++;
        }
        *p = 0;
        return p;
    }

    static char* appendInt(char* p, int32 value)
    {
        char num[16];
        int2str(value, num);
        return append(p, num);
    }

    void drawStats()
    {
        char buf[64];
        char* p;

        // bottom up, 30 apart, centred
        drawText(0, FRAME_HEIGHT - 150, STR[gLevelInfo[stats.level].title], TEXT_ALIGN_CENTER);

        p = append(buf, "KILLS ");
        appendInt(p, stats.kills);
        drawText(0, FRAME_HEIGHT - 120, buf, TEXT_ALIGN_CENTER);

        p = append(buf, "PICKUPS ");
        appendInt(p, stats.pickups);
        drawText(0, FRAME_HEIGHT - 90, buf, TEXT_ALIGN_CENTER);

        int32 secrets = 0;
        for (int32 i = 0; i < 8; i++) {
            secrets += (stats.secrets >> i) & 1;
        }
        p = append(buf, "SECRETS ");
        p = appendInt(p, secrets);
        p = append(p, " of ");
        appendInt(p, gLevelInfo[stats.level].secrets);
        drawText(0, FRAME_HEIGHT - 60, buf, TEXT_ALIGN_CENTER);

        // m:ss, or h:mm:ss past an hour
        int32 s = stats.time / 30;
        int32 h = s / 3600;
        int32 m = (s / 60) % 60;
        s %= 60;
        p = append(buf, "TIME TAKEN ");
        if (h)
        {
            p = appendInt(p, h);
            p = append(p, ":");
            p = appendInt(p, m / 10);
            p = appendInt(p, m % 10);
        } else {
            p = appendInt(p, m);
        }
        p = append(p, ":");
        p = appendInt(p, s / 10);
        appendInt(p, s % 10);
        drawText(0, FRAME_HEIGHT - 30, buf, TEXT_ALIGN_CENTER);
    }

    enum { SAVE_ASK, SAVE_RETRY, SAVE_DONE };

    // The "Save Game?" prompt (retail's 0603c1bc and 0602cb54), over the
    // game's picture: at a crystal it saves her position, at a level's end
    // the progress only. YES is chosen first; "Save Complete!" shows for a
    // second after; a failed write asks "Try Again?". A crystal is used up
    // only by a save.
    void openSave(ItemObj* lara, ItemObj* crystal, bool progressOnly)
    {
        this->lara = lara;
        background = progressOnly ? osLoadScreen(LVL_TR1_TITLE) : copyBackground();
        page = INV_PAGE_SAVE;
        state = INV_STATE_READY;
        nextState = INV_STATE_NONE;
        timer = 0;
        savePrompt = SAVE_ASK;
        saveCrystal = crystal;
        saveProgressOnly = progressOnly;
        optionIndex = 0;
    }

    void closeSave(bool saved)
    {
        if (saveCrystal && !saved) {
            saveCrystal->flags = (saveCrystal->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
        }
        saveCrystal = NULL;
        closeBackground();
        page = INV_PAGE_MAIN;
        state = INV_STATE_NONE;
    }

    void onSave(int32 frames)
    {
        if (savePrompt == SAVE_DONE)
        {
            timer -= frames;
            if (timer <= 0) {
                closeSave(true);
            }
            return;
        }

        if (lara->isKeyHit(IN_LEFT) || lara->isKeyHit(IN_RIGHT))
        {
            optionIndex ^= 1;
            soundPlay(SND_INV_PAGE, NULL);
        }

        if (!lara->isKeyHit(IN_ACTION))
            return;

        if (optionIndex == 1)
        {
            closeSave(false);
            return;
        }

        if (saveProgressOnly ? gameSaveProgress() : gameSave())
        {
            savePrompt = SAVE_DONE;
            timer = 30;
        } else {
            savePrompt = SAVE_RETRY;
            optionIndex = 0;
        }
    }

    void drawSave()
    {
        int32 y = FRAME_HEIGHT / 2;

        if (savePrompt == SAVE_DONE)
        {
            drawText(0, y, STR[STR_SAVE_COMPLETE], TEXT_ALIGN_CENTER);
            return;
        }

        if (savePrompt == SAVE_RETRY)
        {
            drawText(0, y - 20, STR[STR_SAVE_FAILED], TEXT_ALIGN_CENTER);
            drawText(0, y, STR[STR_TRY_AGAIN], TEXT_ALIGN_CENTER);
        } else {
            drawText(0, y, STR[STR_SAVEGAME], TEXT_ALIGN_CENTER);
        }

        // YES and NO side by side, the one chosen framed
        for (int32 i = 0; i < 2; i++)
        {
            const char* str = STR[i ? STR_NO : STR_YES];
            int32 x = i ? 26 : -26;
            drawText(x, y + 20, str, TEXT_ALIGN_CENTER);
            if (optionIndex == i)
            {
                int32 w = getTextWidth(str) + 8;
                renderBorder(FRAME_WIDTH / 2 + x - w / 2, y + 20 - 14, w, 18, 15, 15, 1);
            }
        }
    }

    void close()
    {
        closeBackground();
        setState(INV_STATE_CLOSING, INV_STATE_NONE, 16);
        setHeight(-1536);
        setRadius(0);
        setRot(ANGLE_180, rot - ANGLE_180);
    }

    StringID getTitleStr()
    {
        StringID title = STR_EMPTY;

        switch (page)
        {
            case INV_PAGE_MAIN:
            {
                title = STR_INV_TITLE_MAIN;
                break;
            }

            case INV_PAGE_USE:
            case INV_PAGE_KEYS:
            {
                title = STR_INV_TITLE_KEYS;
                break;
            }

            case INV_PAGE_OPTIONS:
            {
                title = STR_INV_TITLE_OPTIONS;
                break;
            }

            case INV_PAGE_DEATH:
            {
                title = STR_INV_TITLE_GAME_OVER;
                break;
            }
            default: ;
        }
        return title;
    }

    StringID getItemStr()
    {
        const InvItem &item = INV_SLOTS[itemsList[itemIndex]];
        int32 type = item.type;

        #define LVLCHECK(L, T, S) if (gLevelID == L && type == ITEM_INV_##T) return S;

        LVLCHECK(LVL_TR1_2,     KEY_ITEM_1, STR_KEY_SILVER);
        LVLCHECK(LVL_TR1_2,     PUZZLE_1,   STR_PUZZLE_GOLD_IDOL);

        LVLCHECK(LVL_TR1_3A,    PUZZLE_1,   STR_PUZZLE_COG);

        LVLCHECK(LVL_TR1_4,     KEY_ITEM_1, STR_KEY_NEPTUNE);
        LVLCHECK(LVL_TR1_4,     KEY_ITEM_2, STR_KEY_ATLAS);
        LVLCHECK(LVL_TR1_4,     KEY_ITEM_3, STR_KEY_DAMOCLES);
        LVLCHECK(LVL_TR1_4,     KEY_ITEM_4, STR_KEY_THOR);

        LVLCHECK(LVL_TR1_5,     KEY_ITEM_1, STR_KEY_RUSTY);

        LVLCHECK(LVL_TR1_6,     PUZZLE_1,   STR_PUZZLE_GOLD_BAR);

        LVLCHECK(LVL_TR1_7A,    KEY_ITEM_1, STR_KEY_GOLD);
        LVLCHECK(LVL_TR1_7A,    KEY_ITEM_2, STR_KEY_SILVER);
        LVLCHECK(LVL_TR1_7A,    KEY_ITEM_3, STR_KEY_RUSTY);

        LVLCHECK(LVL_TR1_7B,    KEY_ITEM_1, STR_KEY_GOLD);
        LVLCHECK(LVL_TR1_7B,    KEY_ITEM_2, STR_KEY_RUSTY);
        LVLCHECK(LVL_TR1_7B,    KEY_ITEM_3, STR_KEY_RUSTY);

        LVLCHECK(LVL_TR1_8A,    KEY_ITEM_1, STR_KEY_SAPPHIRE);

        LVLCHECK(LVL_TR1_8B,    KEY_ITEM_1, STR_KEY_SAPPHIRE);
        LVLCHECK(LVL_TR1_8B,    PUZZLE_2,   STR_PUZZLE_SCARAB);
        LVLCHECK(LVL_TR1_8B,    PUZZLE_3,   STR_PUZZLE_HORUS);
        LVLCHECK(LVL_TR1_8B,    PUZZLE_4,   STR_PUZZLE_ANKH);
        LVLCHECK(LVL_TR1_8B,    PUZZLE_1,   STR_PUZZLE_HORUS);

        LVLCHECK(LVL_TR1_8C,    KEY_ITEM_1, STR_KEY_GOLD);
        LVLCHECK(LVL_TR1_8C,    PUZZLE_1,   STR_PUZZLE_ANKH);
        LVLCHECK(LVL_TR1_8C,    PUZZLE_2,   STR_PUZZLE_SCARAB);

        LVLCHECK(LVL_TR1_10A,   PUZZLE_1,   STR_PUZZLE_FUSE);
        LVLCHECK(LVL_TR1_10A,   PUZZLE_2,   STR_PUZZLE_PYRAMID);

        LVLCHECK(LVL_TR1_EGYPT, KEY_ITEM_1, STR_KEY_GOLD);
        LVLCHECK(LVL_TR1_CAT,   KEY_ITEM_1, STR_KEY_ORNATE);

        if (state == INV_STATE_SHOW)
        {
            if (type == ITEM_INV_PASSPORT)
            {
                if (passportPage == PASSPORT_PAGE_LOAD_GAME)
                    return STR_LOAD_GAME;
                if (passportPage == PASSPORT_PAGE_RESTART)
                    return (page == INV_PAGE_TITLE) ? STR_START_GAME : STR_RESTART_LEVEL;
                if (passportPage == PASSPORT_PAGE_EXIT_TO_TITLE)
                    return STR_EXIT_TO_TITLE;
            }
        }

        return item.str;
    }

    int32 getAnimLength()
    {
        int32 type = INV_SLOTS[itemsList[itemIndex]].type;

        // HACK! override max animation length
        if (type == ITEM_INV_PASSPORT)
        {
            return FRAME_PASSPORT;
        }

        if (type == ITEM_INV_COMPASS)
        {
            return FRAME_COMPASS;
        }

        const Anim &anim = level.anims[level.models[type].animIndex];

        return anim.frameEnd - anim.frameBegin;
    }

    bool animate(int32 frames)
    {
        if (frameIndex == frameTarget)
            return false;

        if (frameIndex < frameTarget)
        {
            frameIndex += frames;
            if (frameIndex >= frameTarget)
            {
                frameIndex = frameTarget;
                updateVisMask();
                return false;
            }
        }

        if (frameIndex > frameTarget)
        {
            frameIndex -= frames;
            if (frameIndex <= frameTarget)
            {
                frameIndex = frameTarget;
                updateVisMask();
                return false;
            }
        }

        updateVisMask();
        return true;
    }

    void updateVisMask()
    {
        #define PM(x) (1 << x)

        if (itemsList[itemIndex] == SLOT_PASSPORT)
        {
            itemVisMask = PM(0) | PM(1) | PM(4);
            if (frameIndex <= 14) {
                itemVisMask |= PM(2) | PM(6);
            } else if (frameIndex > 14 && frameIndex < 19) {
                itemVisMask |= PM(2) | PM(3) | PM(6);
            } else if (frameIndex == 19) {
                itemVisMask |= PM(3) | PM(6);
            } else if (frameIndex > 19 && frameIndex < 24) {
                itemVisMask |= PM(3) | PM(5) | PM(6);
            } else if (frameIndex >= 24 && frameIndex < 29) {
                itemVisMask |= PM(3) | PM(5);
            }
        }
    }

    void onOption()
    {
        if (optionsCount == 0)
            return;

        if (lara->isKeyHit(IN_DOWN))
        {
            for (int32 i = 0; i < optionsCount; i++)
            {
                optionIndex++;

                if (optionIndex >= optionsCount) {
                    optionIndex = 0;
                }

                if ((options[optionIndex].type == OPT_SPACE) || (options[optionIndex].type == OPT_TEXT)) {
                    continue;
                }

                break;
            }
        }

        if (lara->isKeyHit(IN_UP))
        {
            for (int32 i = 0; i < optionsCount; i++)
            {
                optionIndex--;

                if (optionIndex < 0) {
                    optionIndex = optionsCount - 1;
                }

                if ((options[optionIndex].type == OPT_SPACE) || (options[optionIndex].type == OPT_TEXT)) {
                    continue;
                }

                break;
            }
        }

        Option &opt = options[optionIndex];

        switch (opt.type)
        {
            case OPT_BAR:
            {
                if (lara->isKeyHit(IN_LEFT))
                {
                    opt.value -= 16;
                    if (opt.value < 0) {
                        opt.value = 0;
                    }
                }

                if (lara->isKeyHit(IN_RIGHT))
                {
                    opt.value += 16;
                    if (opt.value > 256) {
                        opt.value = 256;
                    }
                }
                break;
            }

            case OPT_SWITCH:
            {
                if (lara->isKeyHit(IN_LEFT) || lara->isKeyHit(IN_RIGHT))
                {
                    opt.value = !opt.value;
                }
                break;
            }

            default: ;
        }
    }

    void onKey()
    {
        if (lara->input & IN_ACTION)
        {
            nextState = INV_STATE_CLOSE;
            useSlot = itemsList[itemIndex];
        }
    }

    void onCompass()
    {
        if (lara->input & IN_ACTION)
        {
            frameTarget = 0;
            nextState = INV_STATE_CLOSE;
        }
    }

    // The passport (retail's Option_Passport, 06026a90): Load Game (only
    // with a saved game), then Start Game at the title or Restart Level, then
    // Exit to Title (not at the title). There is no saving here: that's the
    // crystals' and the levels' ends'.
    void onPassport()
    {
        int32 first = hasSave ? PASSPORT_PAGE_LOAD_GAME : PASSPORT_PAGE_RESTART;
        int32 last = (page == INV_PAGE_TITLE) ? PASSPORT_PAGE_RESTART : PASSPORT_PAGE_EXIT_TO_TITLE;

        if (lara->isKeyHit(IN_RIGHT) && (passportPage < last))
        {
            passportPage = PassportPage(passportPage + 1);
            frameTarget += 5;
            initOptions();
            soundPlay(SND_INV_PAGE, NULL);
        }
        else if (lara->isKeyHit(IN_LEFT) && (passportPage > first))
        {
            passportPage = PassportPage(passportPage - 1);
            frameTarget -= 5;
            initOptions();
            soundPlay(SND_INV_PAGE, NULL);
        }

        if (!lara->isKeyHit(IN_ACTION))
            return;

        if (passportPage == PASSPORT_PAGE_LOAD_GAME)
        {
            const Option &opt = options[optionIndex];
            if (opt.type != OPT_BUTTON)
                return;     // "No save games!"

            if (opt.value == LVL_LOAD) {
                nextLevel(LVL_LOAD);
            } else {
                gProgress = fileProgress;   // the saved game's, the level from its start
                gameStartLevel(LevelID(opt.value));
            }
        }
        else if (passportPage == PASSPORT_PAGE_RESTART)
        {
            if (page == INV_PAGE_TITLE)
            {
                startInfoNewGame();
                gameStartLevel(LVL_TR1_1);
            } else {
                gameStartLevel(gLevelID);
            }
        }
        else
        {
            nextLevel(LVL_TR1_TITLE);
        }

        frameTarget = 0;
        nextState = INV_STATE_CLOSE;
    }

    // The Load Game list (retail's 06016a58, after reading the file,
    // 0602c040): every level the saved game has reached, and after its
    // level "Current Position" if it holds one, chosen first.
    void initLoadList()
    {
        optionsWidth = 272;

        int32 chosen = -1;
        if (readSaveProgress(fileProgress, fileLevel))
        {
            for (int32 id = LVL_TR1_1; id <= LVL_TR1_10C; id++)
            {
                if (fileProgress.starts[id].available && gLevelInfo[id].title != STR_EMPTY) {
                    OPTION_BTN(gLevelInfo[id].title, id);
                }
                if ((id == fileLevel) && fileProgress.position)
                {
                    OPTION_BTN(STR_CURRENT_POSITION, LVL_LOAD);
                    chosen = optionsCount - 1;
                }
            }
        }

        if (optionsCount == 0) {
            OPTION_TEXT(STR_NO_SAVE_GAMES);
        }
        optionIndex = (chosen >= 0) ? chosen : (optionsCount - 1);
    }

    void onDetail()
    {
        Option &opt = options[optionIndex];
        
        if (optionIndex == OPT_ID_GAMMA)
        {
            int32 gamma = opt.value >> 4;

            if (gSettings.video_gamma != gamma)
            {
                gSettings.video_gamma = gamma;
                palSet(level.palette, gamma << 4, gBrightness);
                osSaveSettings();
            }
        }

        if (optionIndex == OPT_ID_FPS)
        {
            if (gSettings.video_fps != opt.value)
            {
                gSettings.video_fps = opt.value;
                osSaveSettings();
            }
        }

        if (optionIndex == OPT_ID_VSYNC)
        {
            if (gSettings.video_vsync != opt.value)
            {
                gSettings.video_vsync = opt.value;
                osSaveSettings();
            }
        }
    }

    void onSound()
    {
        Option &opt = options[optionIndex];
        
        if ((optionIndex == OPT_ID_SFX) && (gSettings.audio_sfx != opt.value))
        {
            gSettings.audio_sfx = opt.value;
            osSaveSettings();
        }

        if ((optionIndex == OPT_ID_MUSIC) && (gSettings.audio_music != opt.value))
        {
            gSettings.audio_music = opt.value;
            osSaveSettings();
        }
    }

    void onControls()
    {
        Option &opt = options[optionIndex];

        if ((optionIndex == OPT_ID_RUMBLE) && (gSettings.controls_vibration != opt.value))
        {
            gSettings.controls_vibration = opt.value;
            osJoyVibrate(0, 0xFF, 0xFF);
            osSaveSettings();
        }

        if ((optionIndex == OPT_ID_SWAP) && (gSettings.controls_swap != opt.value))
        {
            gSettings.controls_swap = opt.value;
            osSaveSettings();
        }
    }

    void onHome()
    {
        if (lara->input & IN_ACTION)
        {
            nextState = INV_STATE_CLOSE;
            useSlot = itemsList[itemIndex];
            nextLevel(LVL_TR1_GYM);
        }
    }

    void onEnd()
    {
        if (lara->isKeyHit(IN_ACTION) || lara->isKeyHit(IN_JUMP) || lara->isKeyHit(IN_SELECT))
        {
            nextLevel(LVL_TR1_TITLE);
            state = INV_STATE_NONE;
        }
    }

    bool onItem()
    {
        onOption();

        if ((lara->input & (IN_JUMP | IN_SELECT)) && (page != INV_PAGE_DEATH || itemsList[itemIndex] != SLOT_PASSPORT))
        {
            frameTarget = 0;
            nextState = INV_STATE_READY;
        }

        InvSlot slot = itemsList[itemIndex];

        switch (slot)
        {
            case SLOT_LEADBAR:
            case SLOT_KEY_ITEM_1:
            case SLOT_KEY_ITEM_2:
            case SLOT_KEY_ITEM_3:
            case SLOT_KEY_ITEM_4:
            case SLOT_PUZZLE_4:
            case SLOT_PUZZLE_3:
            case SLOT_PUZZLE_2:
            case SLOT_PUZZLE_1:
            case SLOT_SCION:
            case SLOT_PISTOLS:
            case SLOT_SHOTGUN:
            case SLOT_MAGNUMS:
            case SLOT_UZIS:
            case SLOT_MEDIKIT_BIG:
            case SLOT_MEDIKIT_SMALL:
                nextState = INV_STATE_CLOSE;
                useSlot = slot;
                break;
            case SLOT_COMPASS:
                onCompass();
                break;
            case SLOT_PASSPORT:
                onPassport();
                break;
            case SLOT_DETAIL:
                onDetail();
                break;
            case SLOT_SOUND:
                onSound();
                break;
            case SLOT_CONTROLS:
                onControls();
                break;
            case SLOT_HOME:
                onHome();
                break;
            default: ;
        }

        return nextState == INV_STATE_NONE;
    }

    void update(int32 frames)
    {
        updateFading(frames);

        if (page == INV_PAGE_END)
        {
            onEnd();
            return;
        }

        if (page == INV_PAGE_SAVE)
        {
            onSave(frames);
            return;
        }

        if (page == INV_PAGE_STATS)
        {
            onStats(frames);
            return;
        }

        if (state != INV_STATE_SHOW)
        {
            rotItem += rotItemInc * frames;
        }

        if (timer > 0)
        {
            timer -= frames;

            height += heightInc * frames;
            radius += radiusInc * frames;
            pitch += pitchInc * frames;
            rot += rotInc * frames;
            selDist += selDistInc * frames;
            selRotPre += selRotPreInc * frames;
            selRotX += selRotXInc * frames;
            selRotY += selRotYInc * frames;

            if (timer <= 0)
            {
                timer = 0;
                state = nextState;
                nextState = INV_STATE_NONE;
                height = heightTarget;
                radius = radiusTarget;
                pitch = pitchTarget;
                rot = rotTarget;
                selDist = selDistTarget;
                selRotPre = selRotPreTarget;
                selRotX = selRotXTarget;
                selRotY = selRotYTarget;

                heightInc = 0;
                radiusInc = 0;
                pitchInc = 0;
                rotInc = 0;
                rotItem = 0;
                rotItemInc = 512;
                selDistInc = 0;
                selRotPreInc = 0;
                selRotXInc = 0;
                selRotYInc = 0;

                itemIndex = nextIndex;
            }
        }

        switch (state)
        {
            case INV_STATE_CLOSE:
            {
                close();
                break;
            }

            case INV_STATE_READY:
            {
                if ((lara->input & IN_LEFT) && (itemsCount > 1))  {
                    soundPlay(SND_INV_SPIN, NULL);
                    nextIndex = itemIndex + 1;
                    if (nextIndex >= itemsCount) {
                        nextIndex -= itemsCount;
                    }
                    setState(INV_STATE_SPIN, INV_STATE_READY, 12);
                    setRot(ANGLE_360 / itemsCount, nextIndex * ANGLE_360 / itemsCount);
                } else if ((lara->input & IN_RIGHT) && (itemsCount > 1)) {
                    soundPlay(SND_INV_SPIN, NULL);
                    nextIndex = itemIndex - 1;
                    if (nextIndex < 0) {
                        nextIndex += itemsCount;
                    }
                    setState(INV_STATE_SPIN, INV_STATE_READY, 12);
                    setRot(-ANGLE_360 / itemsCount, nextIndex * ANGLE_360 / itemsCount);
                } else {
                    if (lara->input & IN_UP) {
                        if (page == INV_PAGE_OPTIONS) {
                            setState(INV_STATE_CLOSING, INV_STATE_PAGE_MAIN, 12);
                            setRadius(0);
                            setRot(ANGLE_180, rot - ANGLE_180);
                            setPitch(ANGLE_45);
                        } else if ((page == INV_PAGE_MAIN) && (numKeys > 0)) {
                            setState(INV_STATE_CLOSING, INV_STATE_PAGE_KEYS, 12);
                            setRadius(0);
                            setRot(ANGLE_180, rot - ANGLE_180);
                            setPitch(ANGLE_45);
                        }
                    } else if (lara->input & IN_DOWN) {
                        if (page == INV_PAGE_KEYS) {
                            setState(INV_STATE_CLOSING, INV_STATE_PAGE_MAIN, 12);
                            setRadius(0);
                            setRot(ANGLE_180, rot - ANGLE_180);
                            setPitch(-ANGLE_45);
                        } else if (page == INV_PAGE_MAIN) {
                            setState(INV_STATE_CLOSING, INV_STATE_PAGE_OPTIONS, 12);
                            setRadius(0);
                            setRot(ANGLE_180, rot - ANGLE_180);
                            setPitch(-ANGLE_45);
                        }
                    } else if ((lara->isKeyHit(IN_SELECT) || lara->isKeyHit(IN_JUMP)) && (page != INV_PAGE_TITLE) && (page != INV_PAGE_DEATH)) {
                        soundPlay(SND_INV_HIDE, NULL);
                        useSlot = SLOT_MAX;
                        close();
                    } else if (lara->input & IN_ACTION) {
                        soundPlay(INV_SLOTS[itemsList[itemIndex]].snd, NULL);
                        setState(INV_STATE_SELECT, INV_STATE_SHOW, 8);
                        setSelection(true);
                        initOptions();
                    }
                }

                break;
            }

            case INV_STATE_DEATH:
            {
                soundPlay(INV_SLOTS[itemsList[itemIndex]].snd, NULL);
                setState(INV_STATE_SELECT, INV_STATE_SHOW, 8);
                setSelection(true);
                initOptions();
                break;
            }

            case INV_STATE_SHOW:
            {
                rotItem = 0;

                if (animate(frames))
                    break;

                if (!onItem() && (frameIndex == frameTarget))
                {
                    setState(INV_STATE_DESELECT, nextState, 8);
                    setSelection(false);
                    break;
                }

                break;
            }

            case INV_STATE_PAGE_MAIN:
            {
                pitch = -pitch;
                setState(INV_STATE_OPENING, INV_STATE_READY, 12);
                setRadius(INV_RING_RADIUS);
                setPitch(0);
                setPage(INV_PAGE_MAIN);
                break;
            }

            case INV_STATE_PAGE_KEYS:
            {
                pitch = -pitch;
                setState(INV_STATE_OPENING, INV_STATE_READY, 12);
                setRadius(INV_RING_RADIUS);
                setPitch(0);
                setPage(INV_PAGE_KEYS);
                break;
            }

            case INV_STATE_PAGE_OPTIONS:
            {
                pitch = -pitch;
                setState(INV_STATE_OPENING, INV_STATE_READY, 12);
                setRadius(INV_RING_RADIUS);
                setPitch(0);
                setPage(INV_PAGE_OPTIONS);
                break;
            }

            default: ;
        }
    }

    void initOptions()
    {
        optionsCount = 0;
        optionIndex = 0;
        optionsWidth = 216;
        optionsHeight = 4;

        InvSlot slot = itemsList[itemIndex];

        switch (slot)
        {
            case SLOT_PASSPORT:
            {
                if (passportPage == PASSPORT_PAGE_LOAD_GAME) {
                    initLoadList();
                }
                break;
            }
            case SLOT_DETAIL:
            {
                OPTION_BAR(STR_OPT_DETAIL_GAMMA, gSettings.video_gamma << 4);
                OPTION_SWITCH(STR_OPT_DETAIL_FPS, gSettings.video_fps);
                OPTION_SWITCH(STR_OPT_DETAIL_VSYNC, gSettings.video_vsync);
                break;
            }
            case SLOT_SOUND:
            {
                OPTION_SWITCH(STR_OPT_SOUND_SFX, gSettings.audio_sfx);
                OPTION_SWITCH(STR_OPT_SOUND_MUSIC, gSettings.audio_music);
                break;
            }
            case SLOT_CONTROLS:
            {
                OPTION_SWITCH(STR_OPT_CONTROLS_VIBRATION, gSettings.controls_vibration);
                OPTION_SWITCH(STR_OPT_CONTROLS_SWAP, gSettings.controls_swap);
            /*
                OPTION_SPACE();
                OPTION_CTRL(STR_CTRL_RUN, 0);
                OPTION_CTRL(STR_CTRL_BACK, 0);
                OPTION_CTRL(STR_CTRL_RIGHT, 0);
                OPTION_CTRL(STR_CTRL_LEFT, 0);
                OPTION_CTRL(STR_CTRL_WALK, 0);
                OPTION_CTRL(STR_CTRL_JUMP, 0);
                OPTION_CTRL(STR_CTRL_ACTION, 0);
                OPTION_CTRL(STR_CTRL_WEAPON, 0);
                OPTION_CTRL(STR_CTRL_LOOK, 0);
                OPTION_CTRL(STR_CTRL_ROLL, 0);
                OPTION_CTRL(STR_CTRL_INVENTORY, 0);
                OPTION_CTRL(STR_CTRL_PAUSE, 0);
            */
                break;
            }
            default: ;
        }
    }

    void drawOptions()
    {
        if (state != INV_STATE_SHOW)
            return;

        if (optionsCount == 0)
            return;

        int32 w = optionsWidth;
        int32 h = optionsHeight;
        int32 y = (FRAME_HEIGHT - h) / 2 - 12;

        renderFill((FRAME_WIDTH - w) / 2 + 1, y + 1, w - 2, h - 2, 25, 2);
        renderBorder((FRAME_WIDTH - w) / 2, y, w, h, 14, 10, 2);

        w -= 4;
        h = 18;
        y += 2;

        for (int32 i = 0; i < optionsCount; i++)
        {
            const Option &opt = options[i];

            if (optionIndex == i) {
                renderBorder((FRAME_WIDTH - w) / 2, y, w, h, 15, 15, 1);
            }

            switch (opt.type)
            {
                case OPT_SPACE:
                {
                    y -= 10;
                    break;
                }

                case OPT_BUTTON:
                {
                    drawText(0, y + 16, STR[opt.str], TEXT_ALIGN_CENTER);
                    break;
                }

                case OPT_BAR:
                {
                    renderBar(FRAME_WIDTH / 2, y + 6, 80, opt.value, BAR_OPTION);
                    drawText(-FRAME_WIDTH / 2 - 8, y + 16, STR[opt.str], TEXT_ALIGN_RIGHT);
                    break;
                }

                case OPT_SWITCH:
                {
                    drawText(-FRAME_WIDTH / 2 - 8, y + 16, STR[opt.str], TEXT_ALIGN_RIGHT);
                    drawText(44, y + 16, STR[opt.value ? STR_ON : STR_OFF], TEXT_ALIGN_CENTER);
                    break;
                }

                case OPT_VALUE:
                {
                    break;
                }

                case OPT_TEXT:
                {
                    drawText(0, y + 16, STR[opt.str], TEXT_ALIGN_CENTER);
                    break;
                }
            }

            y += 18;
        }
    }

    void drawSlot(InvSlot slot)
    {
        int32 type = INV_SLOTS[slot].type;

        bool current = itemsList[itemIndex] == slot;
        bool selected = current && ((state == INV_STATE_SHOW) || (frameTarget != frameIndex));

        if ((type == ITEM_INV_PASSPORT) && !selected) {
            type = ITEM_INV_PASSPORT_CLOSED;
        }

        // the current item brighter, unless the ring is turning (the
        // original's g_LsAdder, 0x400 darker than the PC's)
        gLightAmbient = (current && state != INV_STATE_SPIN) ? 0x1400 : 0x1800;

        ItemObj item;
        memset(&item, 0, sizeof(item));
        item.type        = type;
        item.intensity   = 255;
        item.visibleMask = itemVisMask;
        item.animIndex   = level.models[type].animIndex;
        item.frameIndex  = level.anims[item.animIndex].frameBegin + (selected ? frameIndex : 0);

        renderModelUnder(&item);

        if ((state == INV_STATE_READY) && current)
        {
            // what it holds, in small digits (retail's RingNotActive,
            // 06018f8c): a gun its rounds (the shotgun's in shots), a gun's
            // boxes twice their count, anything else its count if more than one
            char buf[32];
            buf[0] = 0;
            const ExtraInfoLara* e = lara->extraL;
            switch (slot)
            {
                case SLOT_SHOTGUN       : if (!gProgress.bonus) ammoString(buf, e->ammo[WEAPON_SHOTGUN] / 6, 5, 'A'); break;
                case SLOT_MAGNUMS       : if (!gProgress.bonus) ammoString(buf, e->ammo[WEAPON_MAGNUMS], 5, 'B'); break;
                case SLOT_UZIS          : if (!gProgress.bonus) ammoString(buf, e->ammo[WEAPON_UZIS], 5, 'C'); break;
                case SLOT_AMMO_SHOTGUN  :
                case SLOT_AMMO_MAGNUMS  :
                case SLOT_AMMO_UZIS     : ammoString(buf, counts[slot] * 2, 0, 0); break;
                case SLOT_PISTOLS       : break;
                default                 :
                    if (counts[slot] > 1) {
                        ammoString(buf, counts[slot], 0, 0);
                    }
            }
            if (buf[0]) {
                drawText(64, FRAME_HEIGHT - 48, buf, TEXT_ALIGN_CENTER);
            }

            if (slot == SLOT_MEDIKIT_SMALL || slot == SLOT_MEDIKIT_BIG)
            {
                int32 v = (lara->health << 8) / LARA_MAX_HEALTH;
                renderBar((FRAME_WIDTH - 104) / 2, 24, 100, v, BAR_HEALTH);
            }
        }
    }

    void drawPage()
    {
        int16 angleX, angleY;

        anglesFromVector(0, -(height + INV_CAMERA_Y), -INV_CAMERA_Z, angleX, angleY);

        vec3i pos = _vec3i(0, height, radius + INV_CAMERA_Z);

        matrixSetView(pos, angleX + pitch, angleY);
        matrixTranslateAbs(0, 0, 0);

        // the ring's light, as the original's (Inv_RingLight): falling along
        // (-1536, 256, 1024), so towards it (1536, -256, -1024) as a unit
        // << 14, turned into the view; its range the divider 0x6000's
        const int16* r = gViewRotation;
        gLightView.x = (r[0] * 13504 - r[1] * 2251 - r[2] * 9002) >> FIXED_SHIFT;
        gLightView.y = (r[3] * 13504 - r[4] * 2251 - r[5] * 9002) >> FIXED_SHIFT;
        gLightView.z = (r[6] * 13504 - r[7] * 2251 - r[8] * 9002) >> FIXED_SHIFT;
        gLightRange = 2731;

        for (int32 i = itemsCount - 1; i >= 0; i--)
        {
            matrixPush();
            matrixRotateY(i * ANGLE_360 / itemsCount - rot - ANGLE_90);
            matrixTranslateRel(radius, 0, 0);
            matrixRotateYXZ(0, ANGLE_90, 0);

            if (itemIndex == i)
            {
                matrixRotateX(selRotPre);
                matrixTranslateRel(0, 0, selDist);
                matrixRotateYXZ(-selRotX, -selRotY, 0);
                matrixRotateY(rotItem);
            }

            drawSlot(itemsList[i]);

            matrixPop();
        }

        if (frameIndex == frameTarget) {
            drawOptions();
        }
    }

    void drawEndPage()
    {
        int32 y = 48;
        for (int32 i = 0; i <= STR_ALPHA_END_6 - STR_ALPHA_END_1; i++)
        {
            drawText(0, y, STR[STR_ALPHA_END_1 + i], TEXT_ALIGN_CENTER);
            y += 16;
        }
    }

    void draw()
    {
        clear();
        ASSERT(background);
        if (!renderBackground(background))
            return;

        if (page == INV_PAGE_END)
        {
            drawEndPage();
            return;
        }

        if (page == INV_PAGE_SAVE)
        {
            drawSave();
            return;
        }

        if (page == INV_PAGE_STATS)
        {
            drawStats();
            return;
        }

        drawPage();

        if (state == INV_STATE_READY    ||
            state == INV_STATE_SPIN     ||
            state == INV_STATE_SELECT   ||
            state == INV_STATE_DESELECT ||
            state == INV_STATE_SHOW)
        {
            if (state != INV_STATE_SHOW   &&
                state != INV_STATE_SELECT &&
                state != INV_STATE_DESELECT)
            {
                drawText(0, 20, STR[getTitleStr()], TEXT_ALIGN_CENTER);

                if ((page == INV_PAGE_OPTIONS) || (page == INV_PAGE_MAIN && numKeys))
                {
                    drawText(4, 4 + 16, "[", TEXT_ALIGN_LEFT);
                    drawText(-6, 4 + 16, "[", TEXT_ALIGN_RIGHT);
                }

                if (page == INV_PAGE_MAIN || page == INV_PAGE_KEYS)
                {
                    drawText(4, FRAME_HEIGHT - 5, "]", TEXT_ALIGN_LEFT);
                    drawText(-6, FRAME_HEIGHT - 5, "]", TEXT_ALIGN_RIGHT);
                }
            }

            if ((frameIndex == frameTarget) && (state != INV_STATE_SPIN))
            {
                const char* str = STR[getItemStr()];

                drawText(0, FRAME_HEIGHT - 8, str, TEXT_ALIGN_CENTER);
            }

        }
    }
};

EWRAM_DATA Inventory inventory;

// The level she starts (retail's ModifyStartInfo, 0602c0ec): she holds her
// pistols, with 1000 rounds. The Caves are a new game's start, pistols only;
// the Mines take her guns (their ammo stays with her, as boxes); her home
// gives her nothing.
void startInfoModify(LevelID id)
{
    StartInfo &s = gProgress.starts[id];
    s.guns |= 1 << WEAPON_PISTOLS;
    s.gun = WEAPON_PISTOLS;
    s.ammo[WEAPON_PISTOLS] = 1000;

    if (id == LVL_TR1_1)
    {
        s.ammo[WEAPON_MAGNUMS] = s.ammo[WEAPON_UZIS] = s.ammo[WEAPON_SHOTGUN] = 0;
        s.guns = 1 << WEAPON_PISTOLS;
        s.medis = s.bigMedis = s.scions = 0;
        s.ready = 0;
    }
    else if (id == LVL_TR1_10A)
    {
        s.guns = 0;
        s.scions = 0;
        s.ready = 0;
        s.gun = WEAPON_NONE;
    }
    else if (id == LVL_TR1_GYM)
    {
        uint8 available = s.available;
        memset(&s, 0, sizeof(s));
        s.gun = WEAPON_NONE;
        s.available = available;
    }

    // the bonus game: every gun with 1234 rounds, the uzis to hand (retail's
    // ModifyStartInfo, 0602c0ec, past the Gym)
    if (gProgress.bonus && id != LVL_TR1_GYM)
    {
        s.guns = (1 << WEAPON_NONE) - 1;
        s.ammo[WEAPON_MAGNUMS] = s.ammo[WEAPON_UZIS] = s.ammo[WEAPON_SHOTGUN] = 1234;
        s.gun = WEAPON_UZIS;
    }
}

// A new game (retail's InitialiseStartInfo, 0602c088): every level's start
// as a new game gives it, the Gym and the Caves the only ones reached.
void startInfoNewGame()
{
    if (gProgress.bonus)
    {
        gProgress.position = 0;
        return;
    }

    memset(&gProgress, 0, sizeof(gProgress));
    for (int32 i = 0; i < LVL_MAX; i++)
    {
        gProgress.starts[i].gun = WEAPON_NONE;
        startInfoModify(LevelID(i));
    }
    gProgress.starts[LVL_TR1_GYM].available = 1;
    gProgress.starts[LVL_TR1_1].available = 1;
}

// A level started straight from boot (make START_LEVEL), for testing: every
// gun with plenty of ammo and medipacks, as the GBA demo gave, then what
// that level does to them.
void startInfoTestingKit(LevelID id)
{
    startInfoNewGame();
    StartInfo &s = gProgress.starts[id];
    s.guns = (1 << WEAPON_NONE) - 1;
    for (int32 w = 0; w < WEAPON_NONE; w++) {
        s.ammo[w] = 9999;
    }
    s.gun = WEAPON_PISTOLS;
    s.medis = 10;
    s.bigMedis = 5;
    s.available = 1;
    startInfoModify(id);
}

// A level's end (retail's CreateStartInfo, 0602c20c, then ModifyStartInfo):
// what she takes into level id, which is reached now.
void startInfoCreate(LevelID id, const ExtraInfoLara* extra)
{
    StartInfo &s = gProgress.starts[id];
    s.guns = 0;
    for (int32 w = 0; w < WEAPON_NONE; w++)
    {
        const GunSlots &g = gGunSlots[w];
        if (inventory.counts[g.gun]) {
            s.guns |= 1 << w;
            s.ammo[w] = extra->ammo[w];
        } else {
            s.ammo[w] = inventory.counts[g.ammo] * g.box;
        }
    }
    s.ammo[WEAPON_PISTOLS] = 1000;
    s.medis = uint8(inventory.counts[SLOT_MEDIKIT_SMALL]);
    s.bigMedis = uint8(inventory.counts[SLOT_MEDIKIT_BIG]);
    s.scions = uint8(inventory.counts[SLOT_SCION]);
    s.gun = extra->weapon;
    s.ready = (extra->weaponState == WEAPON_STATE_READY);
    s.available = 1;
    startInfoModify(id);
}

#endif
