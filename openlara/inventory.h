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
#define INV_CAMERA_Z        598     // retail's ring camera, at FOV 80 (focal 209.75 on 352)
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
    INV_STATE_PAGE_OPTIONS,
    INV_STATE_PRESS_START       // the title's ring closed by B: back to Press START
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
    INV_PAGE_STATS,
    INV_PAGE_BACKUP,
    INV_PAGE_PRESS_START
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
    SLOT_GAMMA,
    SLOT_CONTROLS,
    SLOT_HOME,
    SLOT_MAX
};

// Retail's control methods (S_UpdateInput 060374b4, its table at 0x060637fc):
// what A, B, C, X, Y, Z, L and R do in each, and the Controls page's names.
enum ControlAction { CTRL_JUMP, CTRL_ACTION, CTRL_DRAW, CTRL_STEP_LEFT, CTRL_LOOK, CTRL_STEP_RIGHT, CTRL_WALK, CTRL_ROLL };
static const uint16 kControlKeys[8] = { IK_A, IK_B, IK_C, IK_X, IK_Y, IK_Z, IK_L, IK_R };
static const uint16 kControlInputs[8] = { IN_JUMP, IN_ACTION, IN_WEAPON, IN_STEP_LEFT, IN_LOOK, IN_STEP_RIGHT, IN_WALK, IN_ROLL };
static const uint8 kControlMethods[3][8] = {
    { CTRL_JUMP, CTRL_ACTION, CTRL_DRAW, CTRL_STEP_LEFT, CTRL_LOOK, CTRL_STEP_RIGHT, CTRL_WALK, CTRL_ROLL },
    { CTRL_JUMP, CTRL_ACTION, CTRL_ROLL, CTRL_LOOK, CTRL_WALK, CTRL_DRAW, CTRL_STEP_LEFT, CTRL_STEP_RIGHT },
    { CTRL_ROLL, CTRL_ACTION, CTRL_JUMP, CTRL_DRAW, CTRL_WALK, CTRL_LOOK, CTRL_STEP_LEFT, CTRL_STEP_RIGHT }
};

enum PassportPage {
    PASSPORT_PAGE_LOAD_GAME,
    PASSPORT_PAGE_RESTART,
    PASSPORT_PAGE_EXIT_TO_TITLE
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

// selDist, selRotPre, selRotX, selRotY: retail's item structs (0x06061b40 ..
// 0x06062280, as TR1X invvars.c) where ours differed by more than a degree
const InvItem INV_SLOTS[SLOT_MAX] = {
    { ITEM_INV_LEADBAR        , SND_INV_SHOW      ,   352   ,   3616        ,   8160        ,   4096       ,   STR_LEAD_BAR      },
    { ITEM_INV_KEY_ITEM_1     , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_2     , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_3     , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_4     , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_KEY           },
    { ITEM_INV_PUZZLE_4       , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_3       , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_2       , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_1       , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_PUZZLE        },
    { ITEM_INV_SCION          , SND_INV_SHOW      ,   256   ,   7200        ,   4352        ,   0          ,   STR_SCION         },
    { ITEM_INV_COMPASS        , SND_INV_COMPASS   ,   456   ,   4352        ,   8192        ,   0          ,   STR_COMPASS       },
    { ITEM_INV_PISTOLS        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_PISTOLS       },
    { ITEM_INV_AMMO_PISTOLS   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_PISTOLS  },
    { ITEM_INV_SHOTGUN        , SND_INV_WEAPON    ,   296   ,   3200        ,   0           ,   8192       ,   STR_SHOTGUN       },
    { ITEM_INV_AMMO_SHOTGUN   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_SHOTGUN  },
    { ITEM_INV_MAGNUMS        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_MAGNUMS       },
    { ITEM_INV_AMMO_MAGNUMS   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_MAGNUMS  },
    { ITEM_INV_UZIS           , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_UZIS          },
    { ITEM_INV_AMMO_UZIS      , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_UZIS     },
    { ITEM_INV_MEDIKIT_BIG    , SND_INV_SHOW      ,   352   ,   ANGLE(20)   ,   ANGLE(44)   ,   ANGLE(22)  ,   STR_MEDI_BIG      },
    { ITEM_INV_MEDIKIT_SMALL  , SND_INV_SHOW      ,   216   ,   ANGLE(22)   ,   ANGLE(40)   ,   ANGLE(22)  ,   STR_MEDI_SMALL    },
    { ITEM_INV_PASSPORT       , SND_INV_SHOW      ,   384   ,   ANGLE(25)   ,   ANGLE(23)   ,   ANGLE(0)   ,   STR_GAME          },
    { ITEM_INV_GAMMA          , SND_INV_SHOW      ,   440   ,   3904        ,   0           ,   ANGLE(0)   ,   STR_GAMMA         },  // retail's 0x06062240
    { ITEM_INV_CONTROLS       , SND_INV_CONTROLS  ,   352   ,   5504        ,   -1536       ,   ANGLE(0)   ,   STR_CONTROLS      },  // retail's 0x06062200
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
    int16 itemRot[SLOT_MAX];    // each ring item's own turn (retail's inv_item->y_rot)
    uint16 skipInputs[8];       // watchSkip's: the last eight new inputs
    int32 skipAt;               // ... where the next goes
    uint32 skipLast;            // ... the inputs of the frame before
    bool skipOn;                // ... the code has been keyed
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

    int32 gammaWait;            // ticks till held left or right steps the gamma again
    bool controlsShown;         // the Controls page, the title's picture dimmed under it

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

    // the backup memory's pages at boot (INV_PAGE_BACKUP)
    int32 backupPage;           // BACKUP_PAGE_*
    uint32 backupBlocks;        // the blocks a file takes in the memory the low page is about
    bool backupChecked;         // once, before the first Press START
    int32 idleTicks;            // Press START's, toward an attract demo

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
        add(ITEM_INV_GAMMA);
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
    }

    // Retail's Display_Inventory (06017420), each vblank: the current item
    // spins 0x100 (not while left or right is held, unless it's alone), turns
    // to rest 0x400 at a time while it's chosen, and unwinds 0x200 at a time
    // while the ring turns; the others unwind 0x100 at a time.
    void spinItems(int32 vblanks)
    {
        const bool turning = (lara->input & (IN_LEFT | IN_RIGHT)) && itemsCount > 1;
        for (; vblanks > 0; vblanks--)
        {
            for (int32 i = 0; i < itemsCount; i++)
            {
                int16 &r = itemRot[i];
                if (i != itemIndex) {
                    r = (r == 0) ? 0 : int16(r + (r < 0 ? 0x100 : -0x100));
                } else if (state == INV_STATE_SPIN) {
                    r = (r == 0) ? 0 : int16(r + (r < 0 ? 0x200 : -0x200));
                } else if (state == INV_STATE_SELECT || state == INV_STATE_SHOW || state == INV_STATE_DESELECT) {
                    if (r != 0) {
                        r = int16((r + (uint16(-r - 1) < 0x7FFF ? 0x400 : -0x400)) & 0xFC00);
                    }
                } else if (!turning) {
                    r += 0x100;
                }
            }
        }
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
            // retail's ring items each keep their own frame (inv_item->current_frame); ours share
            // one, which goes back to the start here: the compass and the passport run on to their
            // last frame as they're put back, and the next item turned to would draw at it
            frameIndex = 0;
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
        memset(itemRot, 0, sizeof(itemRot));

        #define ADD_SLOT(slot) if (counts[slot]) itemsList[itemsCount++] = slot;

        switch (page)
        {
            // retail's Construct_Inventory (06018518), its list at
            // 0x060623e4: Game and Gamma, at the title Controls and the Gym
            // too (its Sound and Detail Levels are never shown)
            case INV_PAGE_DEATH:
            case INV_PAGE_TITLE:
            case INV_PAGE_OPTIONS:
            {
                ADD_SLOT(SLOT_PASSPORT);
                ADD_SLOT(SLOT_GAMMA);
                if (page == INV_PAGE_TITLE)
                {
                    ADD_SLOT(SLOT_CONTROLS);
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

    // Retail's level-skip code (06018bf8): the inventory's last eight new
    // inputs, the game's as the control method maps them; step right, look,
    // step right, look, step left three times, START (Z Y Z Y X X X START with
    // method 1) plays SFX 116, and the passport's Exit to Title then finishes
    // the level. Opening the inventory forgets it, so it's keyed in with the
    // passport open (START closes the ring).
    void watchSkip()
    {
        static const uint16 kCode[8] = {
            IN_STEP_RIGHT, IN_LOOK, IN_STEP_RIGHT, IN_LOOK, IN_STEP_LEFT, IN_STEP_LEFT, IN_STEP_LEFT, IN_SELECT
        };
        static const uint16 kPadKeys[5] = { IK_UP, IK_DOWN, IK_LEFT, IK_RIGHT, IK_SELECT };
        static const uint16 kPadInputs[5] = { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_SELECT };

        uint32 in = 0;
        for (int32 i = 0; i < 8; i++)
        {
            if (keys & kControlKeys[i]) {
                in |= kControlInputs[kControlMethods[gControlMethod][i]];
            }
        }
        for (int32 i = 0; i < 5; i++)
        {
            if (keys & kPadKeys[i]) {
                in |= kPadInputs[i];
            }
        }

        if (in == skipLast)
            return;
        skipLast = in;
        if (!in || skipOn)
            return;

        skipInputs[skipAt] = uint16(in);
        skipAt = (skipAt + 1) & 7;
        for (int32 i = 0; i < 8; i++)
        {
            if (skipInputs[(skipAt + i) & 7] != kCode[i])
                return;
        }
        soundPlay(SND_HEALTH, NULL);
        skipOn = true;
    }

    void open(ItemObj* lara, InvPage page, int32 arg = 0)
    {
        this->lara = lara;
        skipOn = false;

        if (page == INV_PAGE_PRESS_START)
        {
            if (!backupChecked)
            {
                backupChecked = true;
                if (openBackup())
                    return;
            }
            openPressStart();
            return;
        }

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

        // the keys' ring opens on its first item, whatever the hole (retail's
        // Inv_RingInit with the current item reset)
        if (page == INV_PAGE_USE)
        {
            itemIndex = nextIndex = 0;
            setRot(ANGLE_180, 0);
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

    // Backup memory at boot, before the title's first ring, as retail's check
    // (0603b76c, 0603bdb8), over the title's picture dimmed: memory not
    // formatted stops there with how to clear it; with a cartridge in, the
    // memory to use is asked, SYSTEM first; a memory with no file and too
    // little room for one asks whether to start anyway, YES first (YES: no
    // saving; NO: how to make room, and it stops there).
    enum {
        BACKUP_PAGE_NOT_READY_SYSTEM,
        BACKUP_PAGE_NOT_READY_CART,
        BACKUP_PAGE_WHICH,
        BACKUP_PAGE_LOW,
        BACKUP_PAGE_RESTART
    };

    // false when there's nothing to show: the system memory is used
    bool openBackup()
    {
        uint32 sysBlocks, cartBlocks;
        BackupState sys = osBackupState(BACKUP_SYSTEM, &sysBlocks);
        BackupState cart = osBackupState(BACKUP_CARTRIDGE, &cartBlocks);
        if (sys == BACKUP_UNFORMATTED) {
            backupPage = BACKUP_PAGE_NOT_READY_SYSTEM;
        } else if (cart == BACKUP_UNFORMATTED) {
            backupPage = BACKUP_PAGE_NOT_READY_CART;
        } else if (cart != BACKUP_ABSENT) {
            backupPage = BACKUP_PAGE_WHICH;
        } else if (sys == BACKUP_FULL) {
            backupPage = BACKUP_PAGE_LOW;
            backupBlocks = sysBlocks;
        } else {
            osBackupUse(BACKUP_SYSTEM);
            return false;
        }
        background = osPressStart();
        osDimTitle(true);
        page = INV_PAGE_BACKUP;
        state = INV_STATE_READY;
        nextState = INV_STATE_NONE;
        timer = 0;
        optionIndex = 0;
        return true;
    }

    void onBackup()
    {
        if (backupPage != BACKUP_PAGE_WHICH && backupPage != BACKUP_PAGE_LOW)
            return;     // stopped: the player resets

        bool across = backupPage == BACKUP_PAGE_LOW;    // YES and NO side by side, SYSTEM over CARTRIDGE
        if (lara->isKeyHit(across ? IN_LEFT : IN_UP)) {
            optionIndex = 0;
        }
        if (lara->isKeyHit(across ? IN_RIGHT : IN_DOWN)) {
            optionIndex = 1;
        }
        if (!lara->isKeyHit(IN_ACTION))
            return;

        if (backupPage == BACKUP_PAGE_WHICH)
        {
            int32 device = optionIndex ? BACKUP_CARTRIDGE : BACKUP_SYSTEM;
            if (osBackupState(device, &backupBlocks) == BACKUP_FULL)
            {
                backupPage = BACKUP_PAGE_LOW;
                optionIndex = 0;
                return;
            }
            osBackupUse(device);
        }
        else if (optionIndex == 0)
        {
            osBackupUse(-1);
        }
        else
        {
            backupPage = BACKUP_PAGE_RESTART;
            return;
        }

        osDimTitle(false);
        closeBackground();
        open(lara, INV_PAGE_PRESS_START);
    }

    // Press START (retail's 060334d0): the title's own picture, the title's
    // track, "Press START button" 22 from the bottom. START opens the ring;
    // left 600 vblanks, an attract demo starts.
    void openPressStart()
    {
        background = osPressStart();
        page = INV_PAGE_PRESS_START;
        state = INV_STATE_READY;
        nextState = INV_STATE_NONE;
        timer = 0;
        idleTicks = 0;
        useSlot = SLOT_MAX;
    }

    void onPressStart(int32 frames)
    {
        if (lara->isKeyHit(IN_SELECT))
        {
            closeBackground();
            open(lara, INV_PAGE_TITLE);
            return;
        }

        idleTicks += frames;
        if (idleTicks >= 300)
        {
            closeBackground();
            state = INV_STATE_NONE;
            gameStartDemo();
        }
    }

    void drawLines(int32 first, int32 last, int32 y, int32 step)
    {
        for (int32 i = first; i <= last; i++, y += step) {
            drawText(0, y, STR[i], TEXT_ALIGN_CENTER);
        }
    }

    void drawChoice(int32 x, int32 y, StringID id, bool chosen)
    {
        const char* str = STR[id];
        drawText(x, y, str, TEXT_ALIGN_CENTER);
        if (chosen)
        {
            int32 w = getTextWidth(str) + 8;
            renderBorder(FRAME_WIDTH / 2 + x - w / 2, y - 14, w, 18, 15, 15, 1);
        }
    }

    // each line where retail prints it (y from the top)
    void drawBackup()
    {
        switch (backupPage)
        {
            case BACKUP_PAGE_NOT_READY_SYSTEM:
            case BACKUP_PAGE_NOT_READY_CART:
                drawText(0, 20, STR[backupPage == BACKUP_PAGE_NOT_READY_CART ? STR_BACKUP_NOT_READY_CART : STR_BACKUP_NOT_READY_SYSTEM], TEXT_ALIGN_CENTER);
                drawLines(STR_BACKUP_CLEAR_1, STR_BACKUP_SELECT_2, 40, 20);
                break;
            case BACKUP_PAGE_WHICH:
                drawText(0, 40, STR[STR_BACKUP_TITLE], TEXT_ALIGN_CENTER);
                drawLines(STR_BACKUP_WHICH_1, STR_BACKUP_WHICH_2, 100, 16);
                drawChoice(0, 140, STR_BACKUP_SYSTEM, optionIndex == 0);
                drawChoice(0, 160, STR_BACKUP_CARTRIDGE, optionIndex == 1);
                break;
            case BACKUP_PAGE_LOW:
            {
                drawLines(STR_BACKUP_LOW_1, STR_BACKUP_LOW_2, 20, 17);
                drawLines(STR_BACKUP_LOW_3, STR_BACKUP_LOW_6, 60, 17);
                // the blocks a file takes (retail's is always 55)
                char buf[48];
                char* p = append(buf, STR[STR_BACKUP_LOW_7]);
                p = appendInt(p, int32(backupBlocks));
                append(p, STR[STR_BACKUP_LOW_8]);
                drawText(0, 134, buf, TEXT_ALIGN_CENTER);
                drawText(0, 151, STR[STR_BACKUP_LOW_9], TEXT_ALIGN_CENTER);
                drawText(0, 180, STR[STR_BACKUP_LOW_10], TEXT_ALIGN_CENTER);
                drawChoice(-24, 197, STR_YES, optionIndex == 0);
                drawChoice(24, 197, STR_NO, optionIndex == 1);
                break;
            }
            case BACKUP_PAGE_RESTART:
                drawLines(STR_BACKUP_RESTART_1, STR_BACKUP_RESTART_4, 20, 17);
                drawLines(STR_BACKUP_RESTART_5, STR_BACKUP_RESTART_9, 100, 17);
                drawLines(STR_BACKUP_SELECT_1, STR_BACKUP_SELECT_2, 199, 17);
                break;
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
        LVLCHECK(LVL_TR1_8B,    PUZZLE_3,   STR_PUZZLE_ANUBIS);     // retail's table at 0x06060fd0
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

    // the current item's animation's real last frame (getAnimLength's is
    // where the passport and the compass stop to be used)
    int32 animEnd() const
    {
        const Anim &anim = level.anims[level.models[INV_SLOTS[itemsList[itemIndex]].type].animIndex];
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
            // the level finished instead with the skip code keyed (retail's
            // StartGame, 06016214)
            nextLevel(skipOn ? LevelID(gLevelID + 1) : LVL_TR1_TITLE);
        }

        frameTarget = animEnd();    // the book closed forward
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

    // Retail's Option_Gamma (06027014): left and right step the screen's
    // colour offset by 8 in -127..127, again every 10 vblanks held; A or C
    // goes back.
    void onGamma(int32 frames)
    {
        gammaWait -= frames;
        int32 step = (lara->input & IN_LEFT) ? -8 : ((lara->input & IN_RIGHT) ? 8 : 0);
        if (step && gammaWait <= 0)
        {
            gGamma = X_CLAMP(gGamma + step, -127, 127);
            gammaWait = 5;
            osSetGamma(gGamma);
        }
        // chosen: back to the ring at the title, out of the inventory in
        // game (retail's Display_Inventory, 06017420, its case 8)
        if (lara->isKeyHit(IN_ACTION))
        {
            frameTarget = 0;
            nextState = (page == INV_PAGE_TITLE) ? INV_STATE_READY : INV_STATE_CLOSE;
        }
    }

    // Retail's Option_Control (0602733c, 060274b8): up and down pick the
    // control method, A, B or C goes back; the title's picture dims under it.
    void onControls()
    {
        if (!controlsShown)
        {
            controlsShown = true;
            osDimTitle(true);
        }
        if (lara->isKeyHit(IN_DOWN)) {
            gControlMethod = (gControlMethod + 1) % 3;
        }
        if (lara->isKeyHit(IN_UP)) {
            gControlMethod = (gControlMethod + 2) % 3;
        }
        if (lara->isKeyHit(IN_ACTION) || (lara->input & IN_JUMP))
        {
            controlsShown = false;
            osDimTitle(false);
            frameTarget = 0;
            nextState = INV_STATE_READY;
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

    bool onItem(int32 frames)
    {
        onOption();

        InvSlot slot = itemsList[itemIndex];

        // back to the ring with SFX 112, the animation run back; the
        // compass's and the passport's run on to their end (retail's
        // Option_Compass 060272ec, which takes no Action, and Option_Passport)
        if ((lara->input & IN_JUMP) && (page != INV_PAGE_DEATH || slot != SLOT_PASSPORT))
        {
            soundPlay(SND_INV_HIDE, NULL);
            frameTarget = (slot == SLOT_COMPASS || slot == SLOT_PASSPORT) ? animEnd() : 0;
            nextState = INV_STATE_READY;
        }

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
                nextState = INV_STATE_CLOSE;
                useSlot = slot;
                break;
            case SLOT_MEDIKIT_BIG:
            case SLOT_MEDIKIT_SMALL:
                soundPlay(SND_HEALTH, NULL);    // chosen, at full health too (Option_DoInventory 06026880)
                nextState = INV_STATE_CLOSE;
                useSlot = slot;
                break;
            case SLOT_COMPASS:
                break;
            case SLOT_PASSPORT:
                onPassport();
                break;
            case SLOT_GAMMA:
                onGamma(frames);
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
        watchSkip();

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

        if (page == INV_PAGE_BACKUP)
        {
            onBackup();
            return;
        }

        if (page == INV_PAGE_PRESS_START)
        {
            onPressStart(frames);
            return;
        }

        if (page == INV_PAGE_STATS)
        {
            onStats(frames);
            return;
        }

        spinItems(frames * 2);

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
                    } else if (lara->isKeyHit(IN_SELECT) && (page != INV_PAGE_TITLE) && (page != INV_PAGE_DEATH)) {
                        // only START closes the ring; back does nothing here
                        // (retail's Display_Inventory, 06017420)
                        soundPlay(SND_INV_HIDE, NULL);
                        useSlot = SLOT_MAX;
                        close();
                    } else if ((page == INV_PAGE_TITLE) && lara->isKeyHit(IN_JUMP)) {
                        // B at the title: the ring closes, back to Press
                        // START (retail's Display_Inventory, 06017420)
                        soundPlay(SND_INV_HIDE, NULL);
                        setState(INV_STATE_CLOSING, INV_STATE_PRESS_START, 16);
                        setHeight(-1536);
                        setRadius(0);
                        setRot(ANGLE_180, rot - ANGLE_180);
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

                if (animate(frames))
                    break;

                if (!onItem(frames) && (frameIndex == frameTarget))
                {
                    soundPlay(SND_INV_HIDE, NULL);  // retail's RNG_DESELECT (Display_Inventory, 06017420)
                    setState(INV_STATE_DESELECT, nextState, 8);
                    setSelection(false);
                    break;
                }

                break;
            }

            case INV_STATE_PRESS_START:
            {
                closeBackground();
                open(lara, INV_PAGE_PRESS_START);
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
                    renderBar(FRAME_WIDTH / 2, y + 6, 80, opt.value * 80 >> 8, BAR_OPTION);
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

    void drawSlot(InvSlot slot, int16 itemYaw)
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

        if (type == ITEM_INV_COMPASS) {
            item.visibleMask &= ~2u;    // the needle, drawn after
        }
        renderModelUnder(&item);

        // the compass's needle (its node 1) swings to north each frame drawn,
        // as retail's DrawInventoryItem (0601880c) turns it
        if (type == ITEM_INV_COMPASS)
        {
            static int16 needle;
            static int32 speed;
            speed = speed * 19 / 20 + int16(-lara->angle.y - itemYaw - needle) / 50;
            needle += int16(speed);

            const AnimFrame* frame = item.getFrame();
            const uint32* angles = (const uint32*)(frame->angles + 1);
            const ModelNode* node = level.nodes + level.models[type].nodeIndex;
            matrixPush();
            matrixFrame(&frame->pos, angles);
            matrixFrame(&node->pos, angles + 1);
            matrixRotateY(needle);
            renderMesh(level.models[type].start + 1, gLightAmbient, gLightRange, true);    // over the dial
            matrixPop();
        }

        if (current && (state == INV_STATE_READY || state == INV_STATE_SELECT || state == INV_STATE_SHOW))
        {
            // what it holds, in small digits (retail's RingNotActive,
            // 06018f8c), up while it's chosen too: a gun its rounds (the
            // shotgun's in shots), a gun's boxes twice their count, anything
            // else its count if more than one
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
                drawText(64, FRAME_HEIGHT - 56, buf, TEXT_ALIGN_CENTER);
            }

            // a medipack: the health bar where the HUD has it, and kept up
            // 40 ticks after
            if (slot == SLOT_MEDIKIT_SMALL || slot == SLOT_MEDIKIT_BIG)
            {
                lara->extraL->healthTimer = 40;
                renderBar(32, 28, 100, X_CLAMP(lara->health, 0, LARA_MAX_HEALTH) / 10, BAR_HEALTH);
            }
        }
    }

    void drawPage()
    {
        int16 angleX, angleY;

        anglesFromVector(0, -(height + INV_CAMERA_Y), -INV_CAMERA_Z, angleX, angleY);

        vec3i pos = _vec3i(0, height, radius + INV_CAMERA_Z);

        // the ring's light, as the original's (Inv_RingLight): falling along
        // (-1536, 256, 1024), so towards it (1536, -256, -1024) as a unit
        // << 14, turned into the view (its rows unscaled); its range the
        // divider 0x6000's
        matrixSetView(pos, angleX + pitch, angleY);
        const int16* r = gViewRotation;
        gLightView.x = (r[0] * 13504 - r[1] * 2251 - r[2] * 9002) >> FIXED_SHIFT;
        gLightView.y = (r[3] * 13504 - r[4] * 2251 - r[5] * 9002) >> FIXED_SHIFT;
        gLightView.z = (r[6] * 13504 - r[7] * 2251 - r[8] * 9002) >> FIXED_SHIFT;
        gLightRange = 2731;

        cameraView(pos, angleX + pitch, angleY);
        matrixTranslateAbs(0, 0, 0);

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
            }

            matrixRotateY(itemRot[i]);
            drawSlot(itemsList[i], itemRot[i]);

            matrixPop();
        }

        if (frameIndex == frameTarget) {
            drawOptions();
            if (state == INV_STATE_SHOW && itemsList[itemIndex] == SLOT_GAMMA) {
                drawGamma();
            }
            if (state == INV_STATE_SHOW && itemsList[itemIndex] == SLOT_CONTROLS) {
                drawControls();
            }
        }
    }

    // Retail's slider over the Gamma item (its sprite list at 0x06061598): a
    // frame 132 x 16, 66 left of the item's middle and 80 above, the bar in
    // it 126 long at the gamma's 0..126.
    void drawGamma()
    {
        renderBar(FRAME_WIDTH / 2 - 66, FRAME_HEIGHT / 2 - 80, 126, gGamma / 2 + 63, BAR_OPTION);
    }

    // Retail's Controls page (Option_Control 060274b8): the method's number,
    // the pad's buttons (the font's punctuation draws them) and what each
    // does, where retail prints them (x from the left, or right-aligned from
    // the right edge; y from the middle).
    void drawControls()
    {
        static const char* const kNames[8] = { "Jump", "Action", "Draw Weapon", "Step Left", "Look", "Step Right", "Walk", "Roll" };
        static const char* const kPad[5] = { "Run", "Back", "Left", "Right", "Inventory" };
        // A, B, C, X, Y, Z on the right; L right-aligned on the left, R on the right; the d-pad and START on the left
        static const char kGlyphs[] = ";<>\\^_`{=~()*";
        static const int16 kGlyphX[13] = { 210, 210, 210, 210, 210, 210, -190, 190, -210, -210, -210, -210, -204 };
        static const int8 kRows[13] = { 40, 55, 70, -20, -5, 10, -40, -40, -20, -5, 10, 25, 70 };
        const int32 cy = FRAME_HEIGHT / 2;

        char title[] = "Control Method 1";
        title[15] = char('1' + gControlMethod);
        drawText(0, 35, title, TEXT_ALIGN_CENTER);

        for (int32 i = 0; i < 13; i++)
        {
            const char glyph[2] = { kGlyphs[i], 0 };
            const int32 x = kGlyphX[i];
            const char* name = i < 8 ? kNames[kControlMethods[gControlMethod][i]] : kPad[i - 8];
            drawText(x, cy + kRows[i], glyph, x < 0 ? TEXT_ALIGN_RIGHT : TEXT_ALIGN_LEFT);
            drawText(x < 0 ? (i == 12 ? -230 : x - 20) : x + 20, cy + kRows[i], name, x < 0 ? TEXT_ALIGN_RIGHT : TEXT_ALIGN_LEFT);
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

        if (page == INV_PAGE_BACKUP)
        {
            drawBackup();
            return;
        }

        if (page == INV_PAGE_PRESS_START)
        {
            drawText(0, FRAME_HEIGHT - 22, STR[STR_PRESS_START], TEXT_ALIGN_CENTER);
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
            // GAME OVER stays up over the passport
            if (page == INV_PAGE_DEATH || (state != INV_STATE_SHOW   &&
                state != INV_STATE_SELECT &&
                state != INV_STATE_DESELECT))
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

#if defined(START_LEVEL) || defined(TRS_HOST) || defined(TICK_REPLAY)
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
#endif

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
