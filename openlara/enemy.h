#ifndef H_ENEMY
#define H_ENEMY

#include "common.h"
#include "item.h"

#define ENEMY_TURN_1    ANGLE(1)
#define ENEMY_TURN_2    ANGLE(2)
#define ENEMY_TURN_4    ANGLE(4)
#define ENEMY_TURN_5    ANGLE(5)

// At the top of low work RAM (os.cpp keeps levels below): 64 KB of path searches, read by a few enemies at a time,
// is more than room faces want it than they do.
#define enemiesExtra    ((ExtraInfoEnemy*)(LOW_RAM_TOP - ENEMIES_EXTRA_BYTES))

// A creature taken out of the game is no longer Lara's target: her guns look
// for another, or aim ahead (retail's KillItem, 0601a764)
static void untarget(const ItemObj* item)
{
    ExtraInfoLara &lara = playersExtra[0];
    if (lara.armR.target == item) {
        lara.armR.target = NULL;
    }
    if (lara.armL.target == item) {
        lara.armL.target = NULL;
    }
}

enum AggressionLevel
{
    AGGRESSION_LVL_1   = 0x400,
    AGGRESSION_LVL_2   = 0x2000,
    AGGRESSION_LVL_3   = 0x4000,
    AGGRESSION_LVL_MAX = 0x7FFF
};

struct Enemy : ItemObj
{
    Enemy(Room* room, int32 _health, int32 _radius, int32 _headOffset, int32 _aggression) : ItemObj(room)
    {
    #ifndef STATIC_ITEMS
        // retail's InitialiseCreature; the mummy has its own, without
        if (type != ITEM_MUMMY) {
            angle.y += (rand_logic() - 0x4000) >> 1;
        }
    #endif

        ASSERT(_radius < 512);

    #ifdef HW_NO_ENEMIES
        health = 0;     // testing: they never wake
    #else
        health = _health;
    #endif
        radius = _radius;  // retail's object radius (OBJECT_INFO +0x2a), whole
        headOffset = _headOffset;
        aggression = _aggression;
    }

    void setExtra(ExtraInfoEnemy* extra)
    {
        ASSERT(!extraE);
        ASSERT(extra);

        if (extra->enemy)
        {
            extra->enemy->flags &= ~ITEM_FLAG_STATUS;
            extra->enemy->flags |= ITEM_FLAG_STATUS_INVISIBLE;
            extra->enemy->disable();
        }

        extra->enemy = this;
        extra->rotHead = extra->rotNeck = 0;
        extra->maxTurn = ANGLE_1;   // as retail's InitialiseSlot sets it
        mood = MOOD_SLEEP;          // and the mood bored (the field holds whatever it held)
        extra->nav.stepHeight = 256;
        extra->nav.dropHeight = -256;
        extra->nav.vSpeed = 0;
        extraE = extra;

        initExtra();

        const Sector* sector = room->getSector(pos.x, pos.z);
        ASSERT(sector->boxIndex != NO_BOX);
        extraE->nav.init(sector->boxIndex);

        // the T-rex, the winged mutant and the centaur never path into a
        // box a block could stand in (retail's InitialiseSlot, 06023278)
        if (type == ITEM_REX || type == ITEM_MUTANT_1 || type == ITEM_CENTAUR) {
            extraE->nav.mask = BOX_BLOCKABLE;
        }
    }

    bool enable(bool forced)
    {
        if (extraE)
            return true;

        int32 index = -1;

        for (int32 i = 0; i < MAX_ENEMIES; i++)
        {
            if (!enemiesExtra[i].enemy)
            {
                index = i;
                break;
            }
        }

        if (index == -1)
        {
            for (int32 i = 0; i < MAX_PLAYERS; i++)
            {
                if (!players[i])
                    continue;

                vec3i &viewPos = players[i]->extraL->camera.view.pos;

                int32 maxDistQ = 0;

                if (!forced)
                {
                    vec3i d = pos - viewPos;
                    maxDistQ = X_SQR(d.x >> 8) + X_SQR(d.y >> 8) + X_SQR(d.z >> 8);
                }

                for (int32 j = 0; j < MAX_ENEMIES; j++)
                {
                    vec3i d = enemiesExtra[j].enemy->pos - viewPos;
                    int32 distQ = X_SQR(d.x >> 8) + X_SQR(d.y >> 8) + X_SQR(d.z >> 8);
                    if (distQ > maxDistQ)
                    {
                        maxDistQ = distQ;
                        index = j;
                    }
                }
            }
        }

        if (index == -1)
            return false;

        setExtra(enemiesExtra + index);
        return true;
    }

    void disable()
    {
        ASSERT(extraE);
        extraE->enemy = NULL;
        extraE = NULL;
    }

    void updateTargetInfo()
    {
        tinfo.target = getLara(pos);

        tinfo.tilt = 0;
        tinfo.turn = 0;
        tinfo.pos = pos;

        tinfo.dist = 16 * 1024;
        tinfo.angle = 0;
        tinfo.front = false;
        tinfo.behind = false;
        tinfo.canAttack = false;
        tinfo.rotHead = 0;

        // update navigation target
        const uint16* zones = getZones();

        tinfo.boxIndex = room->getSector(pos.x, pos.z)->boxIndex;
        tinfo.boxIndexTarget = tinfo.target->room->getSector(tinfo.target->pos.x, tinfo.target->pos.z)->boxIndex;
        tinfo.zoneIndex = zones[tinfo.boxIndex];
        tinfo.zoneIndexTarget = zones[tinfo.boxIndexTarget];

        // Lara out of reach (never this zone) when her box is blocked to me,
        // or my box was reached only through blocked boxes (retail's
        // CreatureAIInfo, 06009950)
        if ((tinfo.boxIndexTarget != NO_BOX && (level.boxes[tinfo.boxIndexTarget].overlap & extraE->nav.mask)) || searchBlocked()) {
            tinfo.zoneIndexTarget |= BOX_BLOCKED;
        }

        if (health <= 0)
        {
            tinfo.waypoint = pos;
            tinfo.waypoint.y = extraE->waypointY;
            return;
        }

        int32 dx = tinfo.target->pos.x - pos.x;
        int32 dz = tinfo.target->pos.z - pos.z;

        if (headOffset)
        {
            int32 s, c;
            sincos(angle.y, s, c);
            dx -= s * headOffset >> FIXED_SHIFT;
            dz -= c * headOffset >> FIXED_SHIFT;
        }

                int16 rot = phd_atan(dz, dx);

        // retail's CreatureAIInfo (06009950): the squared distance, far past
        // 32000 either way; "Lara faces me" the turn between them wrapped
        tinfo.dist2 = (abs(dx) > 32000 || abs(dz) > 32000) ? 0x7FFFFFFF : dx * dx + dz * dz;
        tinfo.dist = phd_sqrt(tinfo.dist2);
        tinfo.angle = rot - angle.y;
        tinfo.front = abs(tinfo.angle) < ANGLE_90;
        tinfo.behind = abs(int16(rot - tinfo.target->angle.y + ANGLE_180)) < ANGLE_90;
        tinfo.canAttack = tinfo.front && (abs(tinfo.target->pos.y - pos.y) < 256);
        tinfo.rotHead = tinfo.front ? tinfo.angle : 0;

        // Pierre running off: Lara never in his zone and him hit, for his mood
        // (retail's PierreControl, 06028ef0, before its CreatureMood)
        if (type == ITEM_PIERRE && turnSpeed)
        {
            tinfo.zoneIndexTarget = 0xFFFF;
            flags |= ITEM_FLAG_INJURED;
        }
    }

    // my box was reached only through blocked boxes in the latest search
    X_INLINE bool searchBlocked() const
    {
        return extraE->nav.cells[tinfo.boxIndex].weight == uint16(extraE->nav.weight | NAV_BLOCKED);
    }

    void updateMood()
    {
        updateMood(isViolent());
    }

    void updateMood(bool violent)
    {
        // retail's CreatureMood (0600a0fc): no way there drops the box asked
        // for; a target box gone bad drops it too, and with Lara in reach
        // turns the creature bored, to pick again
        if (searchBlocked()) {
            extraE->nav.nextBox = NO_BOX;
        }

        if (mood != MOOD_ATTACK && extraE->nav.nextBox != NO_BOX && !checkZone(extraE->nav.endBox))
        {
            if (checkZone()) {
                mood = MOOD_SLEEP;
            }
            extraE->nav.nextBox = NO_BOX;
        }

        uint32 nextMood = tinfo.target->health <= 0 ? MOOD_SLEEP : (violent ? getMoodWild() : getMoodNormal());


        if (mood != nextMood)
        {
            if (mood == MOOD_ATTACK) {
                setNextBox(extraE->nav.endBox);
            }
            extraE->nav.nextBox = NO_BOX;
            mood = nextMood;
        }

        switch (mood)
        {
            case MOOD_SLEEP  : moodSleep();  break;
            case MOOD_STALK  : moodStalk();  break;
            case MOOD_ATTACK : moodAttack(); break;
            case MOOD_ESCAPE : moodEscape(); break;
        }

        if (extraE->nav.endBox == NO_BOX) {
            setNextBox(tinfo.boxIndex);
        }
    }

    void moodSleep()
    {
        int32 boxIndex = getRandomBox();

        if (!checkZone(boxIndex))
            return;

        if (checkStalk(boxIndex))
        {
            mood = MOOD_STALK;
            setNextBox(boxIndex);
            return;
        }

        if (extraE->nav.nextBox != NO_BOX)
            return;

        setNextBox(boxIndex);
    }

    void moodStalk()
    {
        if (extraE->nav.nextBox != NO_BOX && checkStalk(extraE->nav.nextBox))
            return;

        int32 boxIndex = getRandomBox();

        if (!checkZone(boxIndex))
            return;

        if (checkStalk(boxIndex))
        {
            setNextBox(boxIndex);
            return;
        }

        if (extraE->nav.nextBox != NO_BOX)
            return;

        setNextBox(boxIndex);

        if (tinfo.zoneIndex != tinfo.zoneIndexTarget) {
            mood = MOOD_SLEEP;
        }
    }

    void moodAttack()
    {
        if (rand_logic() >= aggression)
            return;

        extraE->nav.pos = tinfo.target->pos;
        extraE->nav.nextBox = tinfo.boxIndexTarget;

        // a flyer goes for her head, but in water for her middle (retail's
        // CreatureMood: the frame's top only while lara.water_status is 0)
        if (extraE->nav.zoneType == ZONE_FLY && tinfo.target->waterState == WATER_STATE_ABOVE) {
            extraE->nav.pos.y += tinfo.target->getFrame()->box.minY;
        }
    }

    void moodEscape()
    {
        int32 boxIndex = getRandomBox();

        if (!checkZone(boxIndex) || (extraE->nav.nextBox != NO_BOX))
            return;

        if (checkEscape(boxIndex))
        {
            setNextBox(boxIndex);
            return;
        }

        if ((tinfo.zoneIndex != tinfo.zoneIndexTarget) || !checkStalk(boxIndex))
            return;

        mood = MOOD_STALK;
        setNextBox(boxIndex);
    }

    void updateNavigation()
    {
        tinfo.waypoint = extraE->nav.getWaypoint(tinfo.boxIndex, pos);
        extraE->waypointX = tinfo.waypoint.x;
        extraE->waypointY = tinfo.waypoint.y;
        extraE->waypointZ = tinfo.waypoint.z;

        if (health > 0) {
            turnTo(tinfo.waypoint.x, tinfo.waypoint.z, extraE->maxTurn);
        }
    }

    // retail's CreatureTurn (0600be48): toward the spot, by at most maxTurn
    // (moving, alive or dead)
    void turnTo(int32 x, int32 z, int16 maxTurn)
    {
        if (!hSpeed || !maxTurn)
            return;

        int32 dx = x - pos.x;
        int32 dz = z - pos.z;
        int16 turn = phd_atan(dz, dx) - angle.y;
        int32 r = (hSpeed << FIXED_SHIFT) / maxTurn; //@DIV
        bool aim = (turn >= -ANGLE_90) && (turn <= ANGLE_90);     // a quarter turn either way still ahead (retail)

        // within its turning circle the turn is halved; squares as retail
        // compares them (a creature going backwards has a negative r), in 32
        // bits as the SH-2 multiplies
        int32 d2 = int32(uint32(dx) * uint32(dx) + uint32(dz) * uint32(dz));
        int32 r2 = int32(uint32(r) * uint32(r));
        if (!aim && d2 < r2)
            maxTurn >>= 1;

        // turned now, so it moves along the new heading
        tinfo.turn = X_CLAMP(turn, -maxTurn, maxTurn);
        angle.y += tinfo.turn;
    }

    void setNextBox(int16 boxIndex)
    {
        boxIndex &= 0x7FFF;
        const Box* box = level.boxes + boxIndex;

        // retail's TargetBox (06009e3c): z picked before x, 512 in from the
        // box's sides (its far sides at the last unit inside, as retail's
        // boxes store them), a flyer's height 384 above the floor, a
        // walker's on it
        int32 left = box->minZ << 10, right = (box->maxZ << 10) - 1;
        int32 top = box->minX << 10, bottom = (box->maxX << 10) - 1;
        extraE->nav.nextBox = boxIndex;
        extraE->nav.pos.z = left + 512 + RAND_LOGIC(right - left - 1024);
        extraE->nav.pos.x = top + 512 + RAND_LOGIC(bottom - top - 1024);
        extraE->nav.pos.y = box->floor;
        if (extraE->nav.zoneType == ZONE_FLY)
            extraE->nav.pos.y -= 384;
    }

    // Retail's CreatureMood violent argument (its call sites in issue 75):
    // these attack whenever Lara is in their zone and flee only when hit while
    // she's out of it. Mutants are timid on the ground (they're violent only
    // flying, issue 86); Natla is from her second stage.
    bool isViolent() const
    {
        switch (type)
        {
            case ITEM_BEAR:
            case ITEM_CROCODILE_LAND:
            case ITEM_CROCODILE_WATER:
            case ITEM_LION_MALE:
            case ITEM_LION_FEMALE:
            case ITEM_PUMA:
            case ITEM_RAPTOR:
            case ITEM_REX:
            case ITEM_RAT_WATER:
            case ITEM_MR_T:
            case ITEM_CENTAUR:
            case ITEM_ADAM:
                return true;
            default:
                return false;
        }
    }

    uint32 getMoodWild()
    {
        bool inZone = checkZone();

        if (mood == MOOD_SLEEP || mood == MOOD_STALK)
            return inZone ? MOOD_ATTACK : ((flags & ITEM_FLAG_INJURED) ? MOOD_ESCAPE : mood);

        if (mood == MOOD_ATTACK)
            return inZone ? mood : MOOD_SLEEP;

        return inZone ? MOOD_ATTACK : mood;
    }

    uint32 getMoodNormal()
    {
        bool inZone = checkZone();

        if (mood == MOOD_SLEEP || mood == MOOD_STALK)
        {
            if ((flags & ITEM_FLAG_INJURED) && (rand_logic() < 0x800 || !inZone))
                return MOOD_ESCAPE;
            if (inZone)
                return ((tinfo.dist < 3072) || (mood == MOOD_STALK && extraE->nav.nextBox == NO_BOX)) ? MOOD_ATTACK : MOOD_STALK;
            return mood;
        }
         
        if (mood == MOOD_ATTACK)
            return ((flags & ITEM_FLAG_INJURED) && (rand_logic() < 0x800 || !inZone)) ? MOOD_ESCAPE : (!inZone ? MOOD_SLEEP : mood);

        return (inZone && rand_logic() < 0x100) ? MOOD_STALK : mood;
    }

    X_INLINE bool checkZone()
    {
        return tinfo.zoneIndex == tinfo.zoneIndexTarget;
    }

    // retail's random box (CreatureMood, 0600a0fc): the zone's count times a
    // random number, divided by 0x7FFF where the PC shifts down 15, so the
    // top random number reads one past the zone's list; retail takes the
    // box left there from an earlier list, here none (no box passes)
    int32 getRandomBox()
    {
        int32 i = rand_logic() * int32(extraE->nav.cellsCount) / 0x7FFF;
        return (i < int32(extraE->nav.cellsCount)) ? extraE->nav.cells[i].boxIndex : NO_BOX;
    }

    bool checkZone(int16 boxIndex)
    {
        if (uint16(boxIndex) >= level.boxesCount || getZones()[boxIndex] != tinfo.zoneIndex)
            return false;

        const Box &b = level.boxes[boxIndex];

        if (b.overlap & extraE->nav.mask)   // blocked to me (retail's ValidBox, 0600a04c)
            return false;

        return (pos.x <= (b.minX << 10)) || (pos.x >= ((b.maxX << 10) - 1)) || 
               (pos.z <= (b.minZ << 10)) || (pos.z >= ((b.maxZ << 10) - 1));
    }

    // a box's middle as retail's StalkBox (06009ee4) and EscapeBox find it,
    // from its far sides at the last unit inside: a unit short of halfway
    static int32 boxMidX(const Box &b)
    {
        return ((b.minX << 10) + (b.maxX << 10) - 1) >> 1;
    }

    static int32 boxMidZ(const Box &b)
    {
        return ((b.minZ << 10) + (b.maxZ << 10) - 1) >> 1;
    }

    bool checkStalk(int16 boxIndex)
    {
        const Box &b = level.boxes[boxIndex];

        int32 dx = boxMidX(b) - tinfo.target->pos.x;
        int32 dz = boxMidZ(b) - tinfo.target->pos.z;

        if (X_MAX(abs(dx), abs(dz)) > 3072)
            return false;

        int32 qLara = (tinfo.target->angle.y >> 14) + 2;
        int32 qBox = (dz > 0) ? ((dx > 0) ? 2 : 1) : ((dx > 0) ? 3 : 0);

        if (qLara == qBox) // Lara looks at this box
            return false;

        if (abs(qLara - qBox) != 2) // Lara looks in the opposite direction
            return true;

        dx = pos.x - tinfo.target->pos.x;
        dz = pos.z - tinfo.target->pos.z;

        int32 qEnemy = (dz > 0) ? ((dx > 0) ? 2 : 1) : ((dx > 0) ? 3 : 0);

        return qLara != qEnemy; // Lara looks at enemy
    }

    bool checkEscape(int16 boxIndex)
    {
        const Box &b = level.boxes[boxIndex];

        int32 dx = boxMidX(b) - tinfo.target->pos.x;
        int32 dz = boxMidZ(b) - tinfo.target->pos.z;

        if (X_MAX(abs(dx), abs(dz)) < 5120)
            return false;

        // not across Lara from me on both axes, a side being "past her" or
        // not (retail's EscapeBox, in CreatureMood 0600a0fc)
        bool crossX = (dx > 0) != (pos.x > tinfo.target->pos.x);
        bool crossZ = (dz > 0) != (pos.z > tinfo.target->pos.z);
        return !(crossX && crossZ);
    }

    const uint16* getZones()
    {
        return level.zones[gSaveGame.flipped][extraE->nav.zoneType];
    }

    void bite(int32 damage, const vec3i &offset, int32 joint)
    {
        if (!tinfo.target)
            return;
        
        vec3i fxPos = pos + getJoint(joint, offset);
        fxBlood(fxPos, angle.y, hSpeed);    // moving with it (retail's CreatureEffect, 0600bf8c)
        
        tinfo.target->hit(damage, offset, 0);
    }

    // retail's CreatureTilt: the bank eases toward four times the turn, 3
    // degrees a tick
    void tilt(int32 value)
    {
        angle.z += X_CLAMP((value << 2) - angle.z, -ANGLE(3), ANGLE(3));
    }

    // TR1's box test (TRX Box_BadFloor): a floor the creature can't step to
    // from the height of its box, a blocked or missing box, or a step down
    // into a box higher than the one its path goes through next.
    bool badFloor(Room* from, int32 x, int32 y, int32 z, int32 boxHeight, int32 nextHeight)
    {
        const Sector* sector = from->getRoom(x, y, z)->getSector(x, z);
        if (sector->boxIndex == NO_BOX)
            return true;

        const Box &b = level.boxes[sector->boxIndex];
        if (b.overlap & extraE->nav.mask)
            return true;

        int32 d = boxHeight - b.floor;
        if (d > extraE->nav.stepHeight || d < extraE->nav.dropHeight)
            return true;

        if (d < -extraE->nav.stepHeight && b.floor > nextHeight)
            return true;

        return extraE->nav.zoneType == ZONE_FLY && y > b.floor + extraE->nav.vSpeed;
    }

    // A moving creature ahead of this one in its room's list and nearer than
    // this one's radius, as TR1 tests: each pair once, so the later one gives
    // way and the two part.
    // retail's CreatureCreature (0600ae8c): any creature, moving or not, dead
    // or alive (a dead bat falling through a live one is held where it was)
    bool overlapsCreature()
    {
        int32 r2 = X_SQR(int32(radius));
        for (ItemObj* item = room->firstItem; item && item != this; item = item->nextItem)
        {
            if (item->type == ITEM_LARA || !item->hSpeed || (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_ACTIVE)
                continue;

            int32 dx = item->pos.x - pos.x;
            int32 dy = item->pos.y - pos.y;
            int32 dz = item->pos.z - pos.z;
            if (X_SQR(dx) + X_SQR(dy) + X_SQR(dz) < r2)
                return true;
        }
        return false;
    }

    // The floor height of a sector's box, and of the next box on the path
    // from it, or the given height for a sector without a box.
    void boxHeights(const Sector* sector, int32 none, int32 &height, int32 &nextHeight)
    {
        if (sector->boxIndex == NO_BOX)
        {
            height = nextHeight = none;
            return;
        }
        height = level.boxes[sector->boxIndex].floor;
        uint16 next = extraE->nav.cells[sector->boxIndex].end;
        nextHeight = (next != NO_BOX) ? level.boxes[next].floor : height;
    }

    // TR1's CreatureAnimation (TRX Creature_Animate) once the animation has
    // moved the creature: one that walked into a sector it can't stand in is
    // put back on its side of the sector edge, and one within its radius of
    // such a sector is pushed clear of it, so it slides along walls and ledges
    // instead of stopping dead; one that overlaps a creature ahead of it goes
    // back where it was.
    void updateLocation()
    {
        const vec3i old = tinfo.pos;
        const uint16* zones = getZones();

        // its top (the water crocodile's at its origin: retail zeroes the
        // alligator's frame bounds as it flies, and they stay so)
        int32 top = (type == ITEM_CROCODILE_WATER) ? 0 : getFrame()->box.minY;
        int32 y = pos.y + top;
        int32 boxHeight = (tinfo.boxIndex == NO_BOX) ? roomFloor : level.boxes[tinfo.boxIndex].floor;

        // the room its new spot is in, its floor read from there on (retail's
        // room_num, GetFloor's own: not the room it was in, which at a portal
        // reads the wall beside it)
        Room* at = room->getRoom(pos.x, y, pos.z);
        const Sector* sector = at->getSector(pos.x, pos.z);
        int32 height, nextHeight;
        boxHeights(sector, boxHeight, height, nextHeight);

        if (sector->boxIndex == NO_BOX ||
            (extraE->nav.zoneType != ZONE_FLY && tinfo.boxIndex != NO_BOX && zones[tinfo.boxIndex] != zones[sector->boxIndex]) ||
            (boxHeight - height > extraE->nav.stepHeight) ||
            (boxHeight - height < extraE->nav.dropHeight))
        {
            // back to the edge it crossed (TR1 compares z by the x sector too)
            int32 cell = pos.x >> 10;
            int32 oldX = old.x >> 10;
            int32 oldZ = old.z >> 10;

            if (cell < oldX) {
                pos.x = old.x & ~1023;
            } else if (cell > oldX) {
                pos.x = old.x | 1023;
            }

            if (cell < oldZ) {
                pos.z = old.z & ~1023;
            } else if (cell > oldZ) {
                pos.z = old.z | 1023;
            }

            at = at->getRoom(pos.x, y, pos.z);
            sector = at->getSector(pos.x, pos.z);
            boxHeights(sector, boxHeight, height, nextHeight);
        }

        // pushed clear of bad floor within its radius of the sector's sides
        int32 x = pos.x;
        int32 z = pos.z;
        int32 px = x & 1023;
        int32 pz = z & 1023;
        int32 r = radius;
        int32 shiftX = 0;
        int32 shiftZ = 0;

        if (pz < r)
        {
            if (badFloor(at, x, y, z - r, height, nextHeight)) {
                shiftZ = r - pz;
            }

            if (px < r)
            {
                if (badFloor(at, x - r, y, z, height, nextHeight)) {
                    shiftX = r - px;
                } else if (!shiftZ && badFloor(at, x - r, y, z - r, height, nextHeight)) {
                    if (angle.y > -ANGLE_135 && angle.y < ANGLE_45) {
                        shiftZ = r - pz;
                    } else {
                        shiftX = r - px;
                    }
                }
            }
            else if (px > 1024 - r)
            {
                if (badFloor(at, x + r, y, z, height, nextHeight)) {
                    shiftX = 1024 - r - px;
                } else if (!shiftZ && badFloor(at, x + r, y, z - r, height, nextHeight)) {
                    if (angle.y > -ANGLE_45 && angle.y < ANGLE_135) {
                        shiftZ = r - pz;
                    } else {
                        shiftX = 1024 - r - px;
                    }
                }
            }
        }
        else if (pz > 1024 - r)
        {
            if (badFloor(at, x, y, z + r, height, nextHeight)) {
                shiftZ = 1024 - r - pz;
            }

            if (px < r)
            {
                if (badFloor(at, x - r, y, z, height, nextHeight)) {
                    shiftX = r - px;
                } else if (!shiftZ && badFloor(at, x - r, y, z + r, height, nextHeight)) {
                    if (angle.y > -ANGLE_45 && angle.y < ANGLE_135) {
                        shiftX = r - px;
                    } else {
                        shiftZ = 1024 - r - pz;
                    }
                }
            }
            else if (px > 1024 - r)
            {
                if (badFloor(at, x + r, y, z, height, nextHeight)) {
                    shiftX = 1024 - r - px;
                } else if (!shiftZ && badFloor(at, x + r, y, z + r, height, nextHeight)) {
                    if (angle.y > -ANGLE_135 && angle.y < ANGLE_45) {
                        shiftX = 1024 - r - px;
                    } else {
                        shiftZ = 1024 - r - pz;
                    }
                }
            }
        }
        else if (px < r)
        {
            if (badFloor(at, x - r, y, z, height, nextHeight)) {
                shiftX = r - px;
            }
        }
        else if (px > 1024 - r)
        {
            if (badFloor(at, x + r, y, z, height, nextHeight)) {
                shiftX = 1024 - r - px;
            }
        }

        if (shiftX || shiftZ)
        {
            pos.x += shiftX;
            pos.z += shiftZ;
            angle.y += tinfo.turn;  // turns twice as fast against a wall
            tilt(tinfo.tilt << 1);
        }

        if (overlapsCreature())
        {
            pos = old;
            return;
        }

        if (extraE->nav.zoneType == ZONE_FLY)
        {
            updateRoom();

            // retail's CreatureAnimation: up or down toward the waypoint at its
            // climb speed, back off a floor it's under or a ceiling its top is
            // over, and its pitch eases to its climb a degree a tick
            int32 vs = extraE->nav.vSpeed;
            int32 dy = X_CLAMP(tinfo.waypoint.y - pos.y, -vs, vs);

            const Sector* s = room->getRoom(pos.x, y, pos.z)->getSector(pos.x, pos.z);
            int32 floor = s->getFloor(pos.x, y, pos.z);

            if (pos.y + dy > floor)
            {
                if (pos.y > floor)
                {
                    pos.x = old.x;
                    pos.z = old.z;
                    dy = -vs;
                } else {
                    pos.y = floor;
                    dy = 0;
                }
            }
            else
            {
                int32 ceiling = s->getCeiling(pos.x, y, pos.z);
                if (pos.y + top + dy < ceiling)
                {
                    if (pos.y + top < ceiling)
                    {
                        pos.x = old.x;
                        pos.z = old.z;
                        dy = vs;
                    } else {
                        dy = 0;
                    }
                }
            }

            pos.y += dy;

            int32 pitch = hSpeed ? phd_atan(hSpeed, -dy) : 0;
            angle.x = angleLerp(angle.x, pitch, ANGLE_1);

            updateRoom();
        }
        else
        {
            // follows the floor, rising at once and falling at most a quarter
            // step a tick, to the height that was under it last tick: retail's
            // CreatureAnimation takes the item's floor as the tick before left
            // it, then finds the one under it now, for the next tick
            int32 floor = roomFloor;
            if (pos.y > floor) {
                pos.y = floor;
            } else if (floor - pos.y > 64) {
                pos.y += 64;
            } else {
                pos.y = floor;
            }
            angle.x = 0;
            updateRoom();
        }
    }

    virtual void activate()
    {
        if (health <= 0)
            return;

        ItemObj::activate();

        if (!enable(!(flags & ITEM_FLAG_STATUS))) {
            flags &= ~ITEM_FLAG_STATUS;
            flags |= ITEM_FLAG_STATUS_INVISIBLE;
        }
    }

    virtual void trigger()
    {
        activate();

        if (!(flags & ITEM_FLAG_STATUS) && (flags & ITEM_FLAG_ACTIVE)) {
            flags |= ITEM_FLAG_STATUS_ACTIVE;
        }
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        ASSERT(health > 0);

        health -= damage;

        if (health > 0)
        {
            if (soundId) {
                soundPlay(soundId, &pos);
            }
        } else {
            gSaveGame.kills++;
        #ifdef HIDE_CORPSES
            corpseTimer = HIDE_CORPSES;
        #endif
        }

        // blood that goes on with it, the mummy's too (retail's HitTarget,
        // 0601fff4)
        fxBlood(point, angle.y, hSpeed);

        flags |= ITEM_FLAG_INJURED;
    }

    // retail's CreatureCollision (0600ebbc): pushed to the edge of the box
    // (no growth by her radius), staggered unless it's dying, whether its AI
    // runs or not
    virtual void collide(Lara* lara, CollisionInfo* cinfo)
    {
        if (!updateHitMask(lara, cinfo))
            return;

        if (!cinfo->enemyPush)
            return;

        collidePush(lara, cinfo, health > 0, false);
    }

    virtual void update()
    {
        if ((flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE)
        {
            if (!enable(false))
                return;
            flags &= ~ITEM_FLAG_STATUS;
            flags |= ITEM_FLAG_STATUS_ACTIVE;
        }

    #ifdef HIDE_CORPSES
        if ((health <= 0) && !corpseTimer--)
        {
            deactivate();
            flags &= ~ITEM_FLAG_STATUS;
            flags |= ITEM_FLAG_STATUS_INVISIBLE;
        }
    #endif

        if (!extraE)
            return;

        updateTargetInfo();

        if (health > 0)
        {
            updateMood();
            updateNavigation();
        }

        goalState = updateState();

    #ifdef TRS_HOST
        hostTargetInfo(this);
    #endif

        // what touched or hit it is used up (retail's AnimateItem clears
        // touch_bits and hit_status as it starts)
        hitMask = 0;
        flags &= ~ITEM_FLAG_INJURED;

        tilt(tinfo.tilt);
        extraE->rotHead = angleLerp(extraE->rotHead, tinfo.rotHead, ANGLE(5));

        const vec3i before = pos;
        const int32 stateBefore = state;

        animProcess();

        updateLocation();

        moved(before, stateBefore);

        if ((flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE)
        {
            deathEnd();
            if (type != ITEM_REX) {     // the dead T-rex stays solid (retail's DinoControl)
                flags &= ~ITEM_FLAG_COLLISION;
            }
            health = NOT_ENEMY;
            disable();
        #ifndef HIDE_CORPSES
            deactivate();
        #endif
        }
    }

    virtual int32 updateState()
    {
        return goalState;
    }

    // its death played out (its animation's KILL), before it's let go
    virtual void deathEnd() {}

    // after its animation has moved it from where it was, in the state it was
    virtual void moved(const vec3i &before, int32 stateBefore) {}

    virtual void initExtra()
    {
        // empty
    }

    struct EnemySave {
        int16 vSpeed;
        int16 hSpeed;
        int16 health;
        uint8 mood;
        uint8 waterState;
    };

    virtual uint8* save(uint8* data)
    {
        data = ItemObj::save(data);
        
        EnemySave* sg = (EnemySave*)data;

        sg->vSpeed = vSpeed;
        sg->hSpeed = hSpeed;
        sg->health = health;
        sg->mood = uint8(mood);
        sg->waterState = waterState;

        return data + sizeof(EnemySave);
    }

    virtual uint8* load(uint8* data)
    {
        data = ItemObj::load(data);

        EnemySave* sg = (EnemySave*)data;

        vSpeed = sg->vSpeed;
        hSpeed = sg->hSpeed;
        health = sg->health;
        mood = sg->mood;
        waterState = sg->waterState;

        return data + sizeof(EnemySave);
    }
};


// Lara's double in Atlantis mirrors her about the middle of the arena, room
// 10, and falls when her mirrored ground is a pit Lara's isn't; her fall and
// her landing test the heavy triggers under her, which open the way on, and
// she lies dead (retail's ControlEvilLara, 06020e84). Shooting her hurts Lara.
// aggression is set once she falls.
struct Doppelganger : Enemy
{
    enum {
        MIRROR_X = 0x12000,     // twice the arena's middle: (36864, 61440)
        MIRROR_Z = 0x1E000
    };

    Doppelganger(Room* room) : Enemy(room, LARA_MAX_HEALTH, 10, 0, 0)
    {
        headOffset = 0;
        aggression = 0;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        getLara(pos)->hit(damage * 10, point, 0);
    }

    virtual void collide(Lara* lara, CollisionInfo* cinfo) {}

    virtual void update()
    {
        if (!aggression) {
            mirror();
        }

        if (aggression)
        {
            animProcess();
            updateRoom(); // roomFloor and gLastFloorData under her
            checkTrigger(gLastFloorData, NULL);

            if (pos.y >= roomFloor)
            {
                pos.y = roomFloor;
                room->getRoom(pos.x, pos.y, pos.z)->getSector(pos.x, pos.z)->getFloor(pos.x, pos.y, pos.z);
                checkTrigger(gLastFloorData, NULL);
                flags &= ~ITEM_FLAG_GRAVITY;
                vSpeed = 0;
                goalState = nextState = Lara::STATE_DEATH;
            }
        }
    }

    void mirror()
    {
        Lara* lara = getLara(pos);

        vec3i p = _vec3i(MIRROR_X - lara->pos.x, lara->pos.y, MIRROR_Z - lara->pos.z);

        Room* nextRoom = lara->room->getRoom(p.x, p.y, p.z);
        int32 floor = nextRoom->getSector(p.x, p.z)->getFloor(p.x, p.y, p.z);
        int32 laraFloor = lara->room->getSector(lara->pos.x, lara->pos.z)->getFloor(lara->pos.x, lara->pos.y, lara->pos.z);

        animIndex = lara->animIndex;
        frameIndex = lara->frameIndex;
        pos = p;
        angle = lara->angle;
        angle.y += ANGLE_180;
        vSpeed = lara->vSpeed;

        if (room != nextRoom)
        {
            room->remove(this);
            nextRoom->add(this);
        }

        if (floor < laraFloor + 1024 || (lara->flags & ITEM_FLAG_GRAVITY))
            return;

        // the pit takes her
        animSet(Lara::ANIM_SMASH_JUMP, true, 1);
        state = goalState = Lara::STATE_FALL;
        hSpeed = 0;
        vSpeed = 0;
        flags |= ITEM_FLAG_GRAVITY;
        pos.y += 50;
        aggression = 1;
    }

    // Retail's DrawEvilLara (060210b8): drawn as Lara is (DrawLara), with
    // Lara's arms, gun pose, flashes, head and torso turn and flinch, but her
    // own meshes, and Lara's shadow. The renderer reads Lara's extra as far
    // as her meshes (renderer/types.h GameLara): a copy of that, hers put in.
    virtual void draw()
    {
        static uint32 shown[(__builtin_offsetof(ExtraInfoLara, meshes) + sizeof(uint16) * JOINT_MAX + 3) / 4];
        const Lara* lara = getLara(pos);
        memcpy(shown, lara->extraL, __builtin_offsetof(ExtraInfoLara, meshes));
        ExtraInfoLara* e = (ExtraInfoLara*)shown;
        for (int32 i = 0; i < JOINT_MAX; i++) {
            e->meshes[i] = uint16(level.models[type].start + i);
        }

        const uint8 oldType = type;
        uint8* const oldExtra = extra;
        const uint16 oldAnim = animIndex;
        const uint16 oldFrame = frameIndex;
        const int8 quadrant = lara->extraL->hitQuadrant;
        if (quadrant != -1)
        {
            static const uint8 kHit[4] = { Lara::ANIM_HIT_FRONT, Lara::ANIM_HIT_LEFT, Lara::ANIM_HIT_BACK, Lara::ANIM_HIT_RIGHT };
            animIndex = kHit[quadrant];
            frameIndex = level.anims[animIndex].frameBegin + lara->extraL->hitFrame;
        }
        type = ITEM_LARA;
        extra = (uint8*)e;
        drawModel(this);
        type = oldType;
        extra = oldExtra;
        animIndex = oldAnim;
        frameIndex = oldFrame;
    }
};


struct Wolf : Enemy
{
    enum {
        HIT_MASK = 0x774F,  // body, head, front legs

                DIST_STALK = 1024 * 3,
        DIST_BITE = 345,
        DIST_ATTACK = 1024 + 512,

        DAMAGE_BITE = 100,
        DAMAGE_ATTACK = 50,

        ANIM_DEATH = 20,
        ANIM_DEATH_RUN = 21,
        ANIM_DEATH_JUMP = 22
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_JUMP, // unused
        STATE_STALK,
        STATE_ATTACK,
        STATE_HOWL,
        STATE_SLEEP,
        STATE_GROWL,
        STATE_TURN, // unused
        STATE_DEATH,
        STATE_BITE
    };

    Wolf(Room* room) : Enemy(room, 6, 341, 375, AGGRESSION_LVL_2)
    {
        frameIndex = 96;    // asleep (retail's InitialiseWolf, 06032634)
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, SND_HIT_WOLF);
    }

    // retail's WolfControl (06032664); GROWL is its crouch. Its fall, picked
    // here as retail's control does, not as it's hit: a random number
    // drawn in its turn of the tick
    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (state != STATE_DEATH) {
                animSet(level.models[type].animIndex + ANIM_DEATH + rand_logic() / 11000, true);
            }
            return goalState;
        }

        switch (state)
        {
            case STATE_SLEEP:
            {
                tinfo.rotHead = 0;
                if (mood == MOOD_ESCAPE || checkZone())
                {
                    nextState = STATE_GROWL;
                    return STATE_STOP;
                }
                if (rand_logic() < 0x20)
                {
                    nextState = STATE_WALK;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                return STATE_WALK;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;
                if (mood != MOOD_SLEEP)
                {
                    nextState = STATE_NONE;
                    return STATE_STALK;
                }
                if (rand_logic() < 0x20)
                {
                    nextState = STATE_SLEEP;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_GROWL:
            {
                if (nextState)
                    return nextState;
                if (mood == MOOD_ESCAPE)
                    return STATE_RUN;
                if (tinfo.dist < DIST_BITE && tinfo.canAttack)
                    return STATE_BITE;
                if (mood == MOOD_STALK)
                    return STATE_STALK;
                if (mood == MOOD_SLEEP)
                    return STATE_STOP;
                return STATE_RUN;
            }

            case STATE_STALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;
                if (mood == MOOD_ESCAPE)
                    return STATE_RUN;
                if (tinfo.dist < DIST_BITE && tinfo.canAttack)
                    return STATE_BITE;
                if (tinfo.dist2 > DIST_STALK * DIST_STALK)
                    return STATE_RUN;
                if (mood == MOOD_ATTACK)
                {
                    if (!tinfo.front || tinfo.dist2 > DIST_ATTACK * DIST_ATTACK || tinfo.behind)
                        return STATE_RUN;
                    break;
                }
                if (rand_logic() < 0x180)
                {
                    nextState = STATE_HOWL;
                    return STATE_GROWL;
                }
                if (mood == MOOD_SLEEP)
                    return STATE_GROWL;
                break;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_5;
                tinfo.tilt = tinfo.turn;

                // within the attack range it pounces if within half its
                // square or if Lara faces it, else it crouches to stalk
                if (tinfo.front && tinfo.dist < DIST_ATTACK)
                {
                    if (tinfo.dist2 > DIST_ATTACK * DIST_ATTACK / 2 && !tinfo.behind)
                    {
                        nextState = STATE_STALK;
                        return STATE_GROWL;
                    }
                    nextState = STATE_NONE;
                    return STATE_ATTACK;
                }
                if (mood == MOOD_STALK && tinfo.dist < DIST_STALK)
                {
                    nextState = STATE_STALK;
                    return STATE_GROWL;
                }
                if (mood == MOOD_SLEEP)
                    return STATE_GROWL;
                break;
            }

            case STATE_ATTACK:
            {
                tinfo.tilt = tinfo.turn;
                if (!nextState && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE_ATTACK, _vec3i(0, -14, 174), 6);
                    nextState = STATE_RUN;
                }
                return STATE_RUN;
            }

            case STATE_BITE:
            {
                if (!nextState && (hitMask & HIT_MASK) && tinfo.front)
                {
                    bite(DAMAGE_BITE, _vec3i(0, -14, 174), 6);
                    nextState = STATE_GROWL;
                }
                break;
            }
        }

        return goalState;
    }

    virtual void initExtra()
    {
        extraE->nav.stepHeight = 256;
        extraE->nav.dropHeight = -1024;
    }
};


struct Bear : Enemy
{
    enum {
        HIT_MASK = 0x2406C, // front legs and head

        DAMAGE_FALL = 200,
        DAMAGE_BITE = 200,
        DAMAGE_ATTACK = 400
    };

    enum {
        STATE_NONE   = 0, // WTF?
        STATE_WALK   = 0,
        STATE_STOP   ,
        STATE_HIND   ,
        STATE_RUN    ,
        STATE_HOWL   ,
        STATE_GROWL  ,
        STATE_BITE   ,
        STATE_ATTACK ,
        STATE_EAT    ,
        STATE_DEATH
    };

    Bear(Room* room) : Enemy(room, 20, 341, 500, AGGRESSION_LVL_3)
    {
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, SND_HIT_BEAR);
    }

    // retail's BearControl (0600947c). Our states: WALK its stroll on all
    // fours, HIND its upright walk, HOWL its rearing, GROWL its roar, BITE
    // and ATTACK its two attacks. turnSpeed is its flag: hurt, alive (it
    // drops to all fours and strolls); falling dead from rearing, crushing
    // Lara under it. A next state of 0 (the stroll) means none.
    virtual int32 updateState()
    {
        if (health <= 0)
        {
            // still turning to where it last made for, a degree a tick
            turnTo(extraE->waypointX, extraE->waypointZ, ENEMY_TURN_1);

            switch (state)
            {
                case STATE_HIND:
                    return STATE_HOWL;
                case STATE_WALK:
                case STATE_RUN:
                    return STATE_STOP;
                case STATE_HOWL:
                    turnSpeed = 1;
                    return STATE_DEATH;
                case STATE_STOP:
                    turnSpeed = 0;
                    return STATE_DEATH;
                case STATE_DEATH:
                    if (turnSpeed && (hitMask & HIT_MASK))
                    {
                        tinfo.target->hit(DAMAGE_FALL, _vec3i(0, 0, 0), 0);
                        turnSpeed = 0;
                    }
                    break;
            }
            return goalState;
        }

        bool laraDead = tinfo.target->health <= 0;

        if (flags & ITEM_FLAG_INJURED) {
            turnSpeed = 1;
        }

        switch (state)
        {
            case STATE_STOP:
            {
                if (laraDead)
                    return (tinfo.canAttack && tinfo.dist < 768) ? STATE_EAT : STATE_WALK;
                if (nextState)
                    return nextState;
                return (mood == MOOD_SLEEP) ? STATE_WALK : STATE_RUN;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;

                if (laraDead && (hitMask & HIT_MASK) && tinfo.front)
                    return STATE_STOP;      // to eat her

                if (mood != MOOD_SLEEP)
                {
                    if (mood == MOOD_ESCAPE) {
                        nextState = STATE_WALK;
                    }
                    return STATE_STOP;
                }

                if (rand_logic() < 0x50)
                {
                    nextState = STATE_GROWL;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_5;

                if (hitMask & HIT_MASK) {
                    tinfo.target->hit(3, pos, 0);
                }

                if (mood == MOOD_SLEEP || laraDead)
                    return STATE_STOP;

                if (tinfo.front && !nextState)
                {
                    if (!turnSpeed && tinfo.dist < 2048 && rand_logic() < 0x300)
                    {
                        nextState = STATE_HOWL;
                        return STATE_STOP;
                    }
                    if (tinfo.dist < 1024)
                        return STATE_BITE;
                }
                break;
            }

            case STATE_HOWL:
            {
                if (turnSpeed)
                {
                    nextState = STATE_WALK;
                    return STATE_STOP;
                }

                if (nextState)
                    return nextState;

                if (mood == MOOD_SLEEP || mood == MOOD_ESCAPE)
                    return STATE_STOP;

                if (tinfo.canAttack && tinfo.dist < 600)
                    return STATE_ATTACK;

                return STATE_HIND;
            }

            case STATE_HIND:
            {
                if (turnSpeed)
                {
                    nextState = STATE_WALK;
                    return STATE_HOWL;
                }

                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_HOWL;

                if (mood == MOOD_ESCAPE)
                {
                    nextState = STATE_WALK;
                    return STATE_HOWL;
                }

                if (mood == MOOD_SLEEP || rand_logic() < 0x50)
                {
                    nextState = STATE_GROWL;
                    return STATE_HOWL;
                }

                if (tinfo.dist2 > 2048 * 2048 || rand_logic() < 0x600)
                {
                    nextState = STATE_STOP;
                    return STATE_HOWL;
                }
                break;
            }

            case STATE_BITE:
            {
                if (!nextState && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE_BITE, _vec3i(0, 96, 335), 14);
                    nextState = STATE_STOP;
                }
                break;
            }

            case STATE_ATTACK:
            {
                if (!nextState && (hitMask & HIT_MASK))
                {
                    tinfo.target->hit(DAMAGE_ATTACK, _vec3i(0, 0, 0), 0);
                    nextState = STATE_HOWL;
                }
                break;
            }
        }

        return goalState;
    }
};


struct Bat : Enemy
{
    enum {
        DAMAGE_ATTACK = 2
    };

    enum {
        STATE_NONE,
        STATE_AWAKE,
        STATE_FLY,
        STATE_ATTACK,
        STATE_CIRCLING,
        STATE_DEATH
    };

    Bat(Room* room) : Enemy(room, 1, 102, 0, AGGRESSION_LVL_1) {}

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            hSpeed = 0;
            if (pos.y < roomFloor) {
                flags |= ITEM_FLAG_GRAVITY;
            } else {
                flags &= ~ITEM_FLAG_GRAVITY;
            }

            if (flags & ITEM_FLAG_GRAVITY)
                return STATE_CIRCLING;

            pos.y = roomFloor;
            return STATE_DEATH;
        }

        switch (state)
        {
            case STATE_AWAKE:
            {
                return STATE_FLY;
            }

            case STATE_FLY:
            {
                if (hitMask)
                    return STATE_ATTACK;
                break;
            }

            case STATE_ATTACK:
            {
                if (!hitMask) {
                    mood = MOOD_SLEEP;
                    return STATE_FLY;
                }
                bite(DAMAGE_ATTACK, _vec3i(0, 16, 45), 4);
                break;
            }
        }

        return goalState;
    }

    virtual void initExtra()
    {
        extraE->nav.stepHeight = 20 * 1024;
        extraE->nav.dropHeight = -20 * 1024;
        extraE->nav.vSpeed = 16;
        extraE->maxTurn = ANGLE(20);
    }
};


// The crocodile on land and the alligator in water, as retail's CrocControl
// and AlligatorControl (06011698): one turns into the other as it enters an
// underwater room or leaves the water; the alligator swims up and down at
// 16 a tick, kept a step below the surface, and dead it floats up 32 a tick
// to the surface, where its AI is let go.
struct Crocodile : Enemy
{
    enum {
        HIT_MASK = 0x3FC,
        DIST_BITE = 435,
        DIST_FAST_TURN = 3 * 1024,
        DAMAGE_BITE = 100,
        ANIM_DEATH_LAND = 11,
        ANIM_DEATH_WATER = 4
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_RUN,
        STATE_WALK,
        STATE_FAST_TURN,
        STATE_ATTACK,
        STATE_ATTACK_2,
        STATE_DEATH
    };

    enum {
        STATE_SWIM = 1,
        STATE_SWIM_ATTACK,
        STATE_SWIM_DEATH
    };

    Crocodile(Room* room) : Enemy(room, 20, 341, 600, AGGRESSION_LVL_2)
    {
        if (type == ITEM_CROCODILE_WATER) {
            aggression = 0x400;     // the alligator's smartness (retail's object table)
        }
    }

    // the land crocodile walking its ground path, or the alligator swimming
    // its at 16 a tick, with each one's smartness
    void setForm(ItemType form)
    {
        type = form;
        bool water = (form == ITEM_CROCODILE_WATER);
        aggression = water ? 0x400 : AGGRESSION_LVL_2;
        if (extraE)
        {
            extraE->nav.stepHeight = water ? 20 * 1024 : 256;
            extraE->nav.dropHeight = water ? -20 * 1024 : -256;
            extraE->nav.vSpeed = water ? 16 : 0;
            extraE->nav.zoneType = water ? ZONE_FLY : ZONE_GROUND_1;
        }
    }

    // the other form, on its first animation; the state number carries over
    // from the animation it was on (retail reads it before the change)
    int32 changeForm(ItemType form)
    {
        int32 s = level.anims[animIndex].state;
        setForm(form);
        animIndex = level.models[type].animIndex;
        frameIndex = level.anims[animIndex].frameBegin;
        state = goalState = s;
        return s;
    }

    virtual void update()
    {
        if (type == ITEM_CROCODILE_WATER && health <= 0)
        {
            if (state != STATE_SWIM_DEATH)
            {
                animSet(level.models[type].animIndex + ANIM_DEATH_WATER, true);
                health = NOT_ENEMY;
            }

            int32 wh = getWaterLevel();
            if (wh == WALL)
            {
                setForm(ITEM_CROCODILE_LAND);
                animSet(level.models[type].animIndex + ANIM_DEATH_LAND, true);
                pos.y = room->getRoom(pos.x, pos.y, pos.z)->getSector(pos.x, pos.z)->getFloor(pos.x, pos.y, pos.z);
                angle.x = 0;
            }
            else if (pos.y > wh + 32)
            {
                pos.y -= 32;
            }
            else if (pos.y < wh)
            {
                pos.y = wh;
                if (extraE) {
                    disable();
                }
            }

            animProcess();
            updateRoom();
            return;
        }

        Enemy::update();
    }

    virtual int32 updateState()
    {
        if (type == ITEM_CROCODILE_WATER)
        {
            // dead it's update()'s; its turn made, a wall's push turns it no
            // more (retail's AlligatorControl gives CreatureAnimation no turn)
            extraE->maxTurn = ANGLE(3);
            tinfo.turn = 0;

            int32 goal = goalState;
            if (state == STATE_SWIM)
            {
                if (tinfo.canAttack && hitMask)
                    goal = STATE_SWIM_ATTACK;
            }
            else if (state == STATE_SWIM_ATTACK)
            {
                if (frameIndex == level.anims[animIndex].frameBegin) {
                    nextState = STATE_NONE;
                }
                if (!tinfo.canAttack || !hitMask) {
                    goal = STATE_SWIM;
                } else if (!nextState) {
                    bite(DAMAGE_BITE, _vec3i(5, -21, 467), 9);
                    nextState = STATE_SWIM;
                }
            }

            int32 wh = getWaterLevel();
            if (wh == WALL)
            {
                pos.y = roomFloor;
                angle.x = 0;
                return changeForm(ITEM_CROCODILE_LAND);
            }
            if (pos.y < wh + 256) {
                pos.y = wh + 256;
            }
            return goal;
        }

        int32 goal = goalState;

        if (health <= 0)
        {
            // its fall, from its control as retail's CrocControl
            if (state != STATE_DEATH) {
                animSet(level.models[type].animIndex + ANIM_DEATH_LAND, true);
                goal = goalState;
            }
        }
        else
        {
            extraE->maxTurn = ANGLE(3);

            // the fast turn's 6 degrees a tick in place of the path's turn
            if (state == STATE_FAST_TURN)
            {
                angle.y += ANGLE(6) - tinfo.turn;
                tinfo.turn = 0;
            }

            bool fastTurn = (mood == MOOD_ATTACK) && (tinfo.dist2 > DIST_FAST_TURN * DIST_FAST_TURN) &&
                            (abs(tinfo.angle) > ANGLE_90);

            switch (state)
            {
                case STATE_STOP:
                    if (tinfo.canAttack && tinfo.dist < DIST_BITE) {
                        goal = STATE_ATTACK;
                    } else if (mood == MOOD_ESCAPE) {
                        goal = STATE_RUN;
                    } else if (mood == MOOD_ATTACK) {
                        goal = fastTurn ? STATE_FAST_TURN : STATE_RUN;
                    } else if (mood == MOOD_STALK) {
                        goal = STATE_WALK;
                    }
                    break;

                case STATE_WALK:
                    if (tinfo.front && (hitMask & HIT_MASK)) {
                        goal = STATE_STOP;
                    } else if (mood == MOOD_ATTACK || mood == MOOD_ESCAPE) {
                        goal = STATE_RUN;
                    } else if (mood == MOOD_SLEEP) {
                        goal = STATE_STOP;
                    }
                    break;

                case STATE_FAST_TURN:
                    if (abs(tinfo.angle) < ANGLE_90) {
                        goal = STATE_WALK;
                    }
                    break;

                case STATE_RUN:
                    if (tinfo.front && (hitMask & HIT_MASK)) {
                        goal = STATE_STOP;
                    } else if (mood == MOOD_STALK) {
                        goal = STATE_WALK;
                    } else if (mood == MOOD_SLEEP || fastTurn) {
                        goal = STATE_STOP;
                    }
                    break;

                case STATE_ATTACK:
                    if (!nextState)
                    {
                        bite(DAMAGE_BITE, _vec3i(5, -21, 467), 9);
                        nextState = STATE_STOP;
                    }
                    break;
            }
        }

        // alive or dead, in an underwater room it's the alligator
        if (ROOM_FLAG_WATER(room->info->flags))
            return changeForm(ITEM_CROCODILE_WATER);

        return goal;
    }

    virtual void initExtra()
    {
        setForm(ItemType(type));
    }
};


struct Lion : Enemy
{
    Lion(Room* room) : Enemy(room, 1, 341, 400, AGGRESSION_LVL_2)
    {
        switch (type)
        {
            case ITEM_LION_MALE   : health = 30; aggression = AGGRESSION_LVL_MAX; break;
            case ITEM_LION_FEMALE : health = 25; break;
            case ITEM_PUMA        : health = 45; break;   // retail's object table, entry 14
        }
    }

    enum {
        HIT_MASK = 0x380066,
        DIST_POUNCE = 1024,
        DAMAGE_BITE = 250,
        DAMAGE_POUNCE = 150,
        ANIM_DEATH_LION = 7,
        ANIM_DEATH_PUMA = 4
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_POUNCE,
        STATE_DEATH,
        STATE_WARNING,
        STATE_BITE
    };

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, (type == ITEM_PUMA) ? 0 : SND_HIT_LION);
    }

    // TR1's LionControl, its fall picked here as retail's
    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (state != STATE_DEATH) {
                int32 anim = (type == ITEM_PUMA) ? ANIM_DEATH_PUMA : ANIM_DEATH_LION;
                animSet(level.models[type].animIndex + anim + RAND_LOGIC(2), true);
            }
            return goalState;
        }

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if (mood == MOOD_SLEEP)
                    return STATE_WALK;
                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_BITE;
                if (tinfo.front && tinfo.dist < DIST_POUNCE)
                    return STATE_POUNCE;
                return STATE_RUN;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;

                if (mood != MOOD_SLEEP)
                    return STATE_STOP;
                if (rand_logic() < 128)
                {
                    nextState = STATE_WARNING;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_5;
                tinfo.tilt = tinfo.turn;

                if (mood == MOOD_SLEEP)
                    return STATE_STOP;
                if (tinfo.front && tinfo.dist < DIST_POUNCE)
                    return STATE_STOP;
                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_STOP;
                if (mood != MOOD_ESCAPE && rand_logic() < 128)
                {
                    nextState = STATE_WARNING;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_POUNCE:
            {
                if (!nextState && (hitMask & HIT_MASK))
                {
                    tinfo.target->hit(DAMAGE_POUNCE, pos, 0);
                    nextState = STATE_STOP;
                }
                break;
            }

            case STATE_BITE:
            {
                if (!nextState && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE_BITE, _vec3i(-2, -10, 132), 21);
                    nextState = STATE_STOP;
                }
                break;
            }
        }

        return goalState;
    }

    virtual void initExtra()
    {
        extraE->nav.stepHeight = 256;
        extraE->nav.dropHeight = -1024;
    }
};


// TR1's ApeControl, without the vault onto blocks. turnSpeed keeps its
// flags: provoked, and a pending quarter turn either way.
struct Gorilla : Enemy
{
    enum {
        HIT_MASK = 0xFF00,
        DIST_ATTACK = 430,
        DIST_PANIC = 2 * 1024,
        DAMAGE = 200,
        ANIM_DEATH = 7,

        FLAG_ATTACK = 1,
        FLAG_TURN_L = 2,
        FLAG_TURN_R = 4
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_ATTACK,
        STATE_DEATH,
        STATE_WARNING_1,
        STATE_WARNING_2,
        STATE_RUN_LEFT,
        STATE_RUN_RIGHT,
        STATE_JUMP,
        STATE_VAULT
    };

    enum {
        ANIM_VAULT = 19,
        VAULT_SHIFT = 75
    };

    Gorilla(Room* room) : Enemy(room, 22, 341, 250, AGGRESSION_LVL_MAX)
    {
        turnSpeed = 0;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);
        turnSpeed |= FLAG_ATTACK;
    }

    // a sidestep's quarter turn is undone in the same tick it's made
    void undoSidestepTurn()
    {
        if (turnSpeed & FLAG_TURN_L)
        {
            angle.y -= ANGLE_90;
            turnSpeed &= ~FLAG_TURN_L;
        }
        else if (turnSpeed & FLAG_TURN_R)
        {
            angle.y += ANGLE_90;
            turnSpeed &= ~FLAG_TURN_R;
        }
    }

    // Retail's ApeControl (06029748) and ApeVault (06029570), with TRX's fix
    // to the turn back after a left sidestep: retail tests the item's flags
    // there instead of the gorilla's, so its turn stays (issue 99)
    virtual int32 updateState()
    {
        int32 goal = think();
        if (state != STATE_VAULT)
            undoSidestepTurn();
        return goal;
    }

    // up onto a block: a rise of 384 or more in a tick, not vaulting
    // already, takes the climb, set 75 in from the edge it crossed, facing
    // over it
    virtual void moved(const vec3i &before, int32 stateBefore)
    {
        if (stateBefore == STATE_VAULT || pos.y > before.y - 384)
            return;

        const int32 x0 = before.x >> 10, z0 = before.z >> 10;
        const int32 x1 = pos.x >> 10, z1 = pos.z >> 10;
        if (z0 == z1)
        {
            if (x0 == x1)
                return;
            if (x0 < x1) {
                pos.x = (x1 << 10) - VAULT_SHIFT;
                angle.y = ANGLE_90;
            } else {
                pos.x = (x0 << 10) + VAULT_SHIFT;
                angle.y = -ANGLE_90;
            }
        }
        else if (x0 == x1)
        {
            if (z0 < z1) {
                pos.z = (z1 << 10) - VAULT_SHIFT;
                angle.y = 0;
            } else {
                pos.z = (z0 << 10) + VAULT_SHIFT;
                angle.y = ANGLE_180;
            }
        }

        pos.y = before.y;
        animSet(level.models[type].animIndex + ANIM_VAULT, true);
    }

    int32 think()
    {
        if (health <= 0)
        {
            if (state != STATE_DEATH) {     // its fall, picked here as retail's
                animSet(level.models[type].animIndex + ANIM_DEATH + RAND_LOGIC(2), true);
            }
            return goalState;
        }

        if (tinfo.dist < DIST_PANIC) {
            turnSpeed |= FLAG_ATTACK;
        }

        switch (state)
        {
            case STATE_STOP:
            {
                undoSidestepTurn();

                if (nextState)
                    return nextState;
                if (tinfo.canAttack && tinfo.dist < DIST_ATTACK)
                    return STATE_ATTACK;
                if (!(turnSpeed & FLAG_ATTACK) && checkZone() && tinfo.front)
                {
                    int32 r = rand_logic() >> 5;
                    if (r < 160) return STATE_JUMP;
                    if (r < 320) return STATE_WARNING_1;
                    if (r < 480) return STATE_WARNING_2;
                    extraE->maxTurn = 0;
                    return (r < 752) ? STATE_RUN_LEFT : STATE_RUN_RIGHT;
                }
                return STATE_RUN;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_5;

                if (!turnSpeed && abs(tinfo.angle) < ANGLE_45)
                    return STATE_STOP;
                if (tinfo.front && (hitMask & HIT_MASK))
                {
                    nextState = STATE_ATTACK;
                    return STATE_STOP;
                }
                if (mood != MOOD_ESCAPE)
                {
                    int32 r = rand_logic();
                    if (r < 160) { nextState = STATE_JUMP; return STATE_STOP; }
                    if (r < 320) { nextState = STATE_WARNING_1; return STATE_STOP; }
                    if (r < 480) { nextState = STATE_WARNING_2; return STATE_STOP; }
                }
                break;
            }

            case STATE_RUN_LEFT:
            {
                if (!(turnSpeed & FLAG_TURN_R))
                {
                    angle.y -= ANGLE_90;
                    turnSpeed |= FLAG_TURN_R;
                }
                return STATE_STOP;
            }

            case STATE_RUN_RIGHT:
            {
                if (!(turnSpeed & FLAG_TURN_L))
                {
                    angle.y += ANGLE_90;
                    turnSpeed |= FLAG_TURN_L;
                }
                return STATE_STOP;
            }

            case STATE_ATTACK:
            {
                if (!nextState && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE, _vec3i(0, -19, 75), 15);
                    nextState = STATE_STOP;
                }
                break;
            }
        }

        return goalState;
    }

    virtual void initExtra()
    {
        extraE->nav.stepHeight = 512;
        extraE->nav.dropHeight = -1024;
    }
};


// The rat, and the water rat (vole) it is in water, as retail's RatControl
// and VoleControl (0602ba74): a rat in water becomes a water rat at the
// surface, a water rat out of it a rat; the water rat walks its ground path
// and rises or sinks 32 a tick to the surface.
struct Rat : Enemy
{
    Rat(Room* room) : Enemy(room, 5, 204, 200, AGGRESSION_LVL_2) {}

    enum {
        HIT_MASK = 0x300018F,
        DIST_BITE = 341,
        DIST_CHARGE = 1536,
        DAMAGE = 20,
        ANIM_DEATH_LAND = 8,
        ANIM_DEATH_WATER = 2
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_CHARGE,
        STATE_RUN,
        STATE_BITE,
        STATE_DEATH,
        STATE_POSE
    };

    enum {
        STATE_SWIM = 1,
        STATE_SWIM_BITE,
        STATE_SWIM_DEATH
    };

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, type == ITEM_RAT_LAND ? SND_HIT_RAT : 0);   // the land rat's alone (HitTarget)
    }

    // the water rat's height before the move, and the water's (retail's
    // VoleControl keeps them across CreatureAnimation)
    static int32 sHeight;
    static int32 sWater;
    static bool sRise;

    virtual void update()
    {
        if (type == ITEM_RAT_WATER && health <= 0)
        {
            if (state != STATE_SWIM_DEATH) {
                animSet(level.models[type].animIndex + ANIM_DEATH_WATER, true);
            }
            animProcess();
            if (getWaterLevel() == WALL)
            {
                type = ITEM_RAT_LAND;
                animSet(level.models[type].animIndex + ANIM_DEATH_LAND, true);
                pos.y = roomFloor;
            }
            return;
        }

        sRise = false;
        Enemy::update();

        if (sRise)
        {
            if (sWater - sHeight < -32) {
                pos.y = sHeight - 32;
            } else if (sWater - sHeight > 32) {
                pos.y = sHeight + 32;
            } else {
                pos.y = sWater;
            }
            updateRoom();
        }
    }

    // the other form, on its first animation
    int32 changeForm(ItemType form)
    {
        type = form;
        animIndex = level.models[type].animIndex;
        frameIndex = level.anims[animIndex].frameBegin;
        state = goalState = level.anims[animIndex].state;
        return state;
    }

    virtual int32 updateState()
    {
        if (type == ITEM_RAT_WATER)
        {
            extraE->maxTurn = ANGLE(3);

            int32 goal = goalState;
            if (state == STATE_SWIM)
            {
                if (tinfo.front && (hitMask & HIT_MASK))
                    goal = STATE_SWIM_BITE;
            }
            else if (state == STATE_SWIM_BITE)
            {
                if (!nextState && tinfo.front && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE, _vec3i(0, -11, 108), 3);
                    nextState = STATE_SWIM;
                }
                goal = STATE_NONE;
            }

            sWater = getWaterLevel();
            if (sWater == WALL) {
                goal = changeForm(ITEM_RAT_LAND);
            }

            // it moves on the floor, its height set after
            sHeight = pos.y;
            sRise = true;
            pos.y = roomFloor;
            return goal;
        }

        int32 goal = goalState;

        if (health <= 0)
        {
            // its fall, from its control as retail's RatControl
            if (state != STATE_DEATH) {
                animSet(level.models[type].animIndex + ANIM_DEATH_LAND, true);
                goal = goalState;
            }
        }
        else
        {
            extraE->maxTurn = ANGLE(6);

            switch (state)
            {
                case STATE_STOP:
                    if (nextState) {
                        goal = nextState;
                    } else if (tinfo.canAttack && tinfo.dist < DIST_BITE) {
                        goal = STATE_BITE;
                    } else {
                        goal = STATE_RUN;
                    }
                    break;

                case STATE_RUN:
                    if (tinfo.front && (hitMask & HIT_MASK)) {
                        goal = STATE_STOP;
                    } else if (tinfo.canAttack && tinfo.dist < DIST_CHARGE) {
                        goal = STATE_CHARGE;
                    } else if (tinfo.front && rand_logic() < 256) {
                        nextState = STATE_POSE;
                        goal = STATE_STOP;
                    }
                    break;

                case STATE_BITE:
                case STATE_CHARGE:
                    if (!nextState && tinfo.front && (hitMask & HIT_MASK))
                    {
                        bite(DAMAGE, _vec3i(0, -11, 108), 3);
                        nextState = (state == STATE_BITE) ? STATE_STOP : STATE_RUN;
                    }
                    break;

                case STATE_POSE:
                    if (mood != MOOD_SLEEP || rand_logic() < 256) {
                        goal = STATE_STOP;
                    }
                    break;
            }
        }

        // alive or dead, in water it's the water rat, at the surface
        int32 wh = getWaterLevel();
        if (wh != WALL)
        {
            pos.y = wh;
            return changeForm(ITEM_RAT_WATER);
        }

        return goal;
    }
};

int32 Rat::sHeight;
int32 Rat::sWater;
bool Rat::sRise;


// A creature's kill: Lara at the creature and turned as it is, out of the
// fall, in the special state on her extra model's animation, dead, no air,
// hands busy and unarmed (retail's LaraDinoDeath 060125f4, and the torso's
// attack 3 in AbortionControl 06024b34). Her special state sets the camera.
void laraKilledBy(Lara* lara, ItemObj* by, int32 anim)
{
    if (lara->room != by->room)
    {
        lara->room->remove(lara);
        by->room->add(lara);
    }
    lara->pos = by->pos;
    lara->angle = _vec3s(0, by->angle.y, 0);
    lara->flags &= ~ITEM_FLAG_GRAVITY;
    lara->animSet(level.models[ITEM_LARA_SPEC].animIndex + anim, false);
    lara->state = lara->goalState = Lara::STATE_SPECIAL;
    lara->health = -1;
    lara->oxygen = -1;
    lara->extraL->weaponState = WEAPON_STATE_BUSY;
    lara->extraL->weapon = WEAPON_NONE;
}

struct Rex : Enemy
{
    enum {
        HIT_MASK = 0x00003000,  // head
        DIST_BITE = 1500,
        DIST_WALK = 4096,
        DIST_RUN = 5120,
        DAMAGE_RUN = 10,
        DAMAGE_WALK = 1,
        DAMAGE_FATAL = 1000
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_UNUSED,
        STATE_DEATH,
        STATE_ROAR,
        STATE_BITE,
        STATE_FATAL
    };

    Rex(Room* room) : Enemy(room, 100, 341, 2000, AGGRESSION_LVL_MAX) {}

    virtual int32 updateState()
    {
        // the head turns half the way to Lara, 5 degrees a tick, and the
        // neck as far again (retail's CreatureHead(head >> 1), neck = head)
        tinfo.rotHead >>= 1;
        extraE->rotHead = angleLerp(extraE->rotHead, tinfo.rotHead, ANGLE(5));
        extraE->rotNeck = extraE->rotHead;
        tinfo.rotHead = extraE->rotHead;

        if (health <= 0) {
            return (state == STATE_STOP) ? STATE_DEATH : STATE_STOP;
        }


        if (hitMask)
        {
            tinfo.target->hit((state == STATE_RUN) ? DAMAGE_RUN : DAMAGE_WALK, _vec3i(0, 0, 0), 0);
        }

        bool walk = (tinfo.canAttack && (tinfo.dist2 > DIST_BITE * DIST_BITE) && (tinfo.dist < DIST_WALK)) ||
                    (tinfo.behind && !tinfo.front && (mood != MOOD_ESCAPE));

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if (tinfo.canAttack && (tinfo.dist < DIST_BITE))
                    return STATE_BITE;
                if ((mood == MOOD_SLEEP) || walk)
                    return STATE_WALK;
                return STATE_RUN;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;
                if ((mood != MOOD_SLEEP) || !walk)   // stop-and-go (retail's DinoControl, 060122fc)
                    return STATE_STOP;
                if (tinfo.front && (rand_logic() < 0x200))
                {
                    nextState = STATE_ROAR;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_4;
                if (((tinfo.dist < DIST_RUN) && tinfo.canAttack) || walk)
                    return STATE_STOP;
                if ((mood != MOOD_ESCAPE) && tinfo.front && (rand_logic() < 0x200))
                {
                    nextState = STATE_ROAR;
                    return STATE_STOP;
                }
                if (mood == MOOD_SLEEP)
                    return STATE_STOP;
                break;
            }

            case STATE_BITE:
            {
                nextState = STATE_WALK;
                if (hitMask & HIT_MASK)
                {
                    Lara* lara = (Lara*)tinfo.target;
                    laraKilledBy(lara, this, 1);
                    lara->meshSwap(ITEM_LARA_SPEC, 0xFFFFFFFF);
                    return STATE_FATAL;
                }
                break;
            }
        }

        return goalState;
    }
};


struct Raptor : Enemy
{
    enum {
        HIT_MASK = 0xFF7C00,  // hands and head

        DIST_BITE = 680,
        DIST_ATTACK = 1536,

        DAMAGE_ATTACK = 100,

        ANIM_DEATH_1 = 9,
        ANIM_DEATH_2 = 10
    };

    enum {
        STATE_NONE = 0,
        STATE_DEATH = 0,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_ATTACK_1,
        STATE_UNUSED,
        STATE_ROAR,
        STATE_ATTACK_2,
        STATE_BITE
    };

    Raptor(Room* room) : Enemy(room, 20, 341, 400, AGGRESSION_LVL_3) {}

    virtual void hit(int32 damage, const vec3i& point, int32 soundId)
    {
        Enemy::hit(damage, point, 0);
    }

    // retail's RaptorControl (06011f94); BITE is its third attack. Its fall
    // is picked here as retail's, a third one rarely.
    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (state != STATE_DEATH) {
                animSet(level.models[type].animIndex + ANIM_DEATH_1 + rand_logic() / 16200, true);
            }
            return goalState;
        }

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if ((hitMask & HIT_MASK) || (tinfo.canAttack && tinfo.dist < DIST_BITE))
                    return STATE_BITE;
                if (tinfo.canAttack && tinfo.dist < DIST_ATTACK)
                    return STATE_ATTACK_1;
                if (mood == MOOD_SLEEP)
                    return STATE_WALK;
                return STATE_RUN;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_1;
                if (mood != MOOD_SLEEP)
                    return STATE_STOP;
                if (tinfo.front && rand_logic() < 0x100)
                {
                    nextState = STATE_ROAR;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_4;
                tinfo.tilt = tinfo.turn;

                if (hitMask & HIT_MASK)
                    return STATE_STOP;

                if (tinfo.canAttack && tinfo.dist < DIST_ATTACK)
                {
                    if (goalState != STATE_RUN)
                        break;
                    return (rand_logic() < 0x2000) ? STATE_STOP : STATE_ATTACK_2;
                }

                if (tinfo.front && mood != MOOD_ESCAPE && rand_logic() < 0x100)
                {
                    nextState = STATE_ROAR;
                    return STATE_STOP;
                }

                if (mood == MOOD_SLEEP)
                    return STATE_STOP;
                break;
            }

            case STATE_ATTACK_1:
            case STATE_ATTACK_2:
            case STATE_BITE:
            {
                tinfo.tilt = tinfo.turn;
                // the lunges need Lara ahead, the bite doesn't
                if (!nextState && (hitMask & HIT_MASK) && (tinfo.front || state == STATE_BITE))
                {
                    bite(DAMAGE_ATTACK, _vec3i(0, 66, 318), 22);
                    nextState = (state == STATE_ATTACK_2) ? STATE_RUN : STATE_STOP;
                }
                break;
            }
        }

        return goalState;
    }
};


// The mummy in Khamoon stands until shot or touched, then topples (TR1's
// MummyControl).
// Qualopec's mummies (retail's InitialiseMummy 06031b68, MummyControl
// 06031ba8): no AI, so none of the eight slots, and no random turn.
// Standing, its head follows Lara within 90 degrees, 5 a tick; shot dead or
// touched, it falls. Solid without a stagger (ObjectCollision). turnSpeed is
// its head's turn, and extra points at it, the one angle the renderer takes.
struct Mummy : Enemy
{
    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_DEATH
    };

    Mummy(Room* room) : Enemy(room, 18, 10, 0, AGGRESSION_LVL_MAX)
    {
        visibleMask = 0xFFFF87FF;
        turnSpeed = 0;
        extra = (uint8*)&turnSpeed;
    }

    virtual void activate()
    {
        ItemObj::activate();
    }

    virtual void collide(Lara* lara, CollisionInfo* cinfo)
    {
        if (updateHitMask(lara, cinfo) && cinfo->enemyPush) {
            collidePush(lara, cinfo, false);
        }
    }

    virtual void update()
    {
        int16 head = 0;
        if (state == STATE_STOP)
        {
            const Lara* lara = getLara(pos);
            head = X_CLAMP(int16(phd_atan(lara->pos.z - pos.z, lara->pos.x - pos.x) - angle.y), -ANGLE_90, ANGLE_90);
            if (health <= 0 || hitMask) {
                goalState = STATE_DEATH;
            }
        }
        turnSpeed = angleLerp(turnSpeed, head, ANGLE(5));
        hitMask = 0;

        animProcess();

        if ((flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE)
        {
            deactivate();
            health = NOT_ENEMY;
        }
    }
};


// People with guns (TR1's Larson, Pierre, the skater, the cowboy and Mr. T).
// turnSpeed counts shots within a burst, as the original's creature flags do.
struct Gunman : Enemy
{
    enum {
        DIST_SHOOT = 7 * 1024
    };

    Gunman(Room* room, int32 _health, int32 _radius) : Enemy(room, _health, _radius, 0, AGGRESSION_LVL_MAX)
    {
        turnSpeed = 0;
    }

    // TR1's Targetable: ahead, in range and in sight
    bool canShoot()
    {
        if (!tinfo.target || !tinfo.front || tinfo.dist >= DIST_SHOOT)
            return false;

        Location from, to;
        from.room = room;
        from.pos = _vec3i(pos.x, pos.y - 768, pos.z);
        to.room = tinfo.target->room;
        to.pos = _vec3i(tinfo.target->pos.x, tinfo.target->pos.y - 768, tinfo.target->pos.z);
        return trace(from, to);
    }

    // TR1's ShotLara (060289b8): the nearer, the likelier a hit, by the
    // squared distance, none past the range. A hit (GunHit 06028778) bleeds
    // her at a random joint, going on with her, with the bullet-hit sound; a
    // miss (GunMiss 06028890) ricochets off her floor within 256 of her.
    // Either way the gun flashes (GunShot 060286e4), turned with the head.
    bool shoot(int32 damage, const vec3i &muzzle, int32 joint, int32 dist2)
    {
        ItemObj* target = tinfo.target;
        if (!target)
            return false;

        const int32 range = DIST_SHOOT * DIST_SHOOT;
        bool hit = dist2 <= range && rand_logic() < (range - dist2) / (range / 0x7FFF) - 0x2000;

        if (hit)
        {
            // retail picks of 25 joints; Lara has 15 (one random number: X_MIN
            // would draw it twice)
            int32 j = (rand_logic() * 25) / 0x7FFF;
            j = X_MIN(j, level.models[target->type].count - 1);
            target->fxBlood(target->pos + target->getJoint(j, _vec3i(0, 0, 0)), target->angle.y, target->hSpeed);
            soundPlay(SND_BULLET_HIT, &target->pos);
            target->hit(damage, target->pos, 0);
        }
        else
        {
            vec3i p;
            p.x = target->pos.x + (rand_draw() - 0x4000) * 512 / 0x7FFF;
            p.y = target->roomFloor;
            p.z = target->pos.z + (rand_draw() - 0x4000) * 512 / 0x7FFF;
            fxRicochet(target->room, p, true);
        }

        if (level.models[ITEM_MUZZLE_FLASH].count > 0) {
            ItemObj::add(ITEM_MUZZLE_FLASH, room, pos + getJoint(joint, muzzle), angle.y + tinfo.rotHead);
        }
        return hit;
    }

    // A shot that flies (retail's ShardGun and RocketGun through
    // CreatureEffect): from the joint's offset, aimed at Lara's chest, a
    // little off (ShootAtLara). NULL if the level hasn't the model.
    ItemObj* fire(ItemType fxType, const vec3i &offset, int32 joint)
    {
        Lara* lara = getLara(pos);
        if (!level.models[fxType].count || !lara)
            return NULL;

        ItemObj* fx = ItemObj::add(fxType, room, pos + getJoint(joint, offset), angle.y);
        if (!fx)
            return NULL;

        const AABBs &box = lara->getBoundingBox(true);
        vec3i d = lara->pos - fx->pos;
        d.y += box.maxY + (box.minY - box.maxY) * 3 / 4;
        int32 dist = phd_sqrt(d.x * d.x + d.z * d.z);
        fx->angle.x = int16(-phd_atan(dist, d.y) + (rand_logic() - 0x4000) / 0x40);
        fx->angle.y = int16(phd_atan(d.z, d.x) + (rand_logic() - 0x4000) / 0x40);
        fx->hSpeed = (fxType == ITEM_MUTANT_BULLET) ? 250 : 220;
        return fx;
    }

    // dead, its fall as the control first finds it so, not as it's hit
    // (retail's controls); true then, for what it carried
    bool fall(int32 anim)
    {
        int32 index = level.models[type].animIndex + anim;
        if (state == level.anims[index].state)
            return false;
        animSet(index, true);
        return true;
    }

    // some leave what they carried where they fall (TR1's SpawnItem)
    void drop(ItemType type)
    {
        if (!level.models[type].count)
            return;

        ItemObj* item = ItemObj::add(type, room, pos, angle.y);
        if (item) {
            item->flags &= ~ITEM_FLAG_STATUS;
        }
    }
};


struct Larson : Gunman
{
    enum {
        DIST_WALK = 3 * 1024,
        DAMAGE = 50,
        ANIM_DEATH = 15,
        POSE_CHANCE = 0x60
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_AIM,
        STATE_DEATH,
        STATE_POSE,
        STATE_SHOOT
    };

    Larson(Room* room) : Gunman(room, 50, 102) {}


    // TR1's LarsonControl; Pierre's is the same with two guns and a nerve
    // that breaks (see Pierre)
    int32 updatePerson(int32 damage, bool twoGuns)
    {
        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if (mood == MOOD_SLEEP)
                    return (rand_logic() < POSE_CHANCE) ? STATE_POSE : STATE_WALK;
                if (mood == MOOD_ESCAPE)
                    return STATE_RUN;
                return STATE_WALK;
            }

            case STATE_POSE:
            {
                if (mood != MOOD_SLEEP)
                    return STATE_STOP;
                if (rand_logic() < POSE_CHANCE)
                {
                    nextState = STATE_WALK;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ANGLE(3);

                if (mood == MOOD_SLEEP && rand_logic() < POSE_CHANCE)
                    nextState = STATE_POSE;
                else if (mood == MOOD_ESCAPE)
                    nextState = STATE_RUN;
                else if (canShoot())
                    nextState = STATE_AIM;
                else if (!tinfo.front || tinfo.dist2 > DIST_WALK * DIST_WALK)
                    nextState = STATE_RUN;
                else
                    break;
                return STATE_STOP;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ANGLE(6);
                tinfo.tilt = tinfo.turn >> 1;

                if (mood == MOOD_SLEEP && rand_logic() < POSE_CHANCE)
                    nextState = STATE_POSE;
                else if (canShoot())
                    nextState = STATE_AIM;
                else if (tinfo.front && tinfo.dist < DIST_WALK)
                    nextState = STATE_WALK;
                else
                    break;
                return STATE_STOP;
            }

            case STATE_AIM:
            {
                if (nextState)
                    return nextState;
                return canShoot() ? STATE_SHOOT : STATE_STOP;
            }

            case STATE_SHOOT:
            {
                if (!nextState)
                {
                    if (twoGuns) {
                        shoot(damage >> 1, _vec3i(60, 200, 0), 11, tinfo.dist2);
                        shoot(damage >> 1, _vec3i(-57, 200, 0), 14, tinfo.dist2);
                    } else {
                        shoot(damage, _vec3i(-60, 170, 0), 14, tinfo.dist2);
                    }
                    nextState = STATE_AIM;
                }
                if (mood == MOOD_ESCAPE && (!twoGuns || rand_logic() > 0x2000)) {
                    nextState = STATE_STOP;
                }
                break;
            }
        }

        return goalState;
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            fall(ANIM_DEATH);
            return goalState;
        }
        return updatePerson(DAMAGE, false);
    }
};


// Pierre can't be killed until the level means him to be (his trigger has
// spent itself): until then he runs off at 40 health, and vanishes once out
// of the camera's sight for a while, or in water. One at a time: a second
// woken is removed, or the first if the second's trigger has spent itself
// (TR1's PierreControl, 06028ef0).
struct Pierre;
static Pierre* gPierre;

struct Pierre : Larson
{
    enum {
        DAMAGE = 50,
        ANIM_DEATH = 12,
        HEALTH_RUN = 40,
        DISAPPEAR = 10
    };

    Pierre(Room* room) : Larson(room)
    {
        health = 70;
        gPierre = NULL;
    }

    void remove()
    {
        untarget(this);
        health = NOT_ENEMY;
        flags &= ~ITEM_FLAG_STATUS;
        flags |= ITEM_FLAG_STATUS_INVISIBLE;
        if (extraE) {
            disable();
        }
        deactivate();
        if (gPierre == this) {
            gPierre = NULL;
        }
    }

    virtual void update()
    {
        // removed by the other, earlier in this frame's list
        if (!(flags & ITEM_FLAG_ACTIVE))
            return;

        if (!gPierre)
        {
            gPierre = this;
        }
        else if (gPierre != this)
        {
            if (!(flags & ITEM_FLAG_ONCE))
            {
                remove();
                return;
            }
            gPierre->remove();
            gPierre = this;
        }

        Larson::update();

        if (!extraE)
            return;

        if (turnSpeed)
        {
            Location from = playersExtra[0].camera.view;
            Location to;
            to.room = room;
            to.pos = _vec3i(pos.x, pos.y - 1024, pos.z);

            if (trace(from, to))
            {
                turnSpeed = 1;
            }
            else if (++turnSpeed > DISAPPEAR)
            {
                remove();
                return;
            }
        }

        if (getWaterLevel() != WALL) {
            remove();
        }
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);

        if (!(flags & ITEM_FLAG_ONCE) && health <= HEALTH_RUN) {
            health = HEALTH_RUN;
            turnSpeed++;
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (fall(ANIM_DEATH)) {     // retail's PierreControl: his magnums, the scion piece, a key
                drop(ITEM_MAGNUMS);
                drop(ITEM_SCION_PICKUP_DROP);
                drop(ITEM_KEY_ITEM_1);
            }
            return goalState;
        }

        return updatePerson(DAMAGE, true);
    }
};


// The skater kid shoots from his board, stopping at middle range
// (TR1's SkateKidControl); the board is drawn with him.
struct Skater : Gunman
{
    enum {
        DIST_STOP = 4 * 1024,
        DIST_DONT_STOP = 2560,
        DIST_TOO_CLOSE = 1024,
        DAMAGE_STOP = 50,
        DAMAGE_SKATE = 40,
        ANIM_DEATH = 13
    };

    enum {
        STATE_STOP,
        STATE_SHOOT_1,
        STATE_SKATE,
        STATE_PUSH,
        STATE_SHOOT_2,
        STATE_DEATH
    };

    Skater(Room* room) : Gunman(room, 125, 204)
    {
        state = STATE_SKATE;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, SND_HIT_SKATER);
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (fall(ANIM_DEATH)) {
                drop(ITEM_UZIS);
            }
            return goalState;
        }

        // his music once he's hurt (retail's 06029ac0)
        if (health < 120 && gLevelTrack != 56) {
            musicPlay(56);
        }

        extraE->maxTurn = ANGLE(4);

        switch (state)
        {
            case STATE_STOP:
            {
                turnSpeed = 0;
                if (nextState)
                    return nextState;
                return canShoot() ? STATE_SHOOT_1 : STATE_SKATE;
            }

            case STATE_SKATE:
            {
                turnSpeed = 0;
                if (rand_logic() < 0x200)
                    return STATE_PUSH;
                if (canShoot())
                {
                    if (tinfo.dist2 > DIST_DONT_STOP * DIST_DONT_STOP && tinfo.dist < DIST_STOP && mood != MOOD_ESCAPE)
                        return STATE_STOP;
                    return STATE_SHOOT_2;
                }
                break;
            }

            case STATE_PUSH:
            {
                if (rand_logic() < 0x400)
                    return STATE_SKATE;
                break;
            }

            case STATE_SHOOT_1:
            case STATE_SHOOT_2:
            {
                if (!turnSpeed && canShoot())
                {
                    int32 damage = (state == STATE_SHOOT_1) ? DAMAGE_STOP : DAMAGE_SKATE;
                    shoot(damage, _vec3i(0, 150, 34), 7, tinfo.dist2);
                    shoot(damage, _vec3i(0, 150, 37), 4, tinfo.dist2);
                    turnSpeed = 1;
                }
                if (mood == MOOD_ESCAPE || tinfo.dist < DIST_TOO_CLOSE) {
                    nextState = STATE_SKATE;
                }
                break;
            }
        }

        return goalState;
    }

    virtual void draw()
    {
        Gunman::draw();

        if (!level.models[ITEM_SKATEBOARD].count)
            return;

        // the board, in step with him
        uint8 oldType = type;
        uint16 oldAnim = animIndex;
        uint16 oldFrame = frameIndex;
        const Model &kid = level.models[ITEM_SKATER];
        const Model &board = level.models[ITEM_SKATEBOARD];
        type = ITEM_SKATEBOARD;
        animIndex = board.animIndex + (oldAnim - kid.animIndex);
        frameIndex = level.anims[animIndex].frameBegin + (oldFrame - level.anims[oldAnim].frameBegin);
        Gunman::draw();
        type = oldType;
        animIndex = oldAnim;
        frameIndex = oldFrame;
    }
};


// The cowboy fires both revolvers, the second a few frames after the first
// (TR1's CowboyControl).
struct Cowboy : Gunman
{
    enum {
        DIST_WALK = 3 * 1024,
        DAMAGE = 70,
        ANIM_DEATH = 7
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_AIM,
        STATE_DEATH,
        STATE_SHOOT
    };

    Cowboy(Room* room) : Gunman(room, 150, 102) {}

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (fall(ANIM_DEATH)) {
                drop(ITEM_MAGNUMS);
            }
            return goalState;
        }

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if (canShoot())
                    return STATE_AIM;
                return (mood == MOOD_SLEEP) ? STATE_WALK : STATE_RUN;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ANGLE(3);

                if (mood == MOOD_ESCAPE || !tinfo.front)
                    nextState = STATE_RUN;
                else if (canShoot())
                    nextState = STATE_AIM;
                else if (tinfo.dist2 > DIST_WALK * DIST_WALK)
                    nextState = STATE_RUN;
                else
                    break;
                return STATE_STOP;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ANGLE(6);
                tinfo.tilt = tinfo.turn >> 1;

                if (mood != MOOD_ESCAPE || tinfo.front)
                {
                    if (canShoot())
                        nextState = STATE_AIM;
                    else if (tinfo.front && tinfo.dist < DIST_WALK)
                        nextState = STATE_WALK;
                    else
                        break;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_AIM:
            {
                turnSpeed = 0;
                if (nextState)
                    return STATE_STOP;
                return canShoot() ? STATE_SHOOT : STATE_STOP;
            }

            case STATE_SHOOT:
            {
                if (turnSpeed == 0) {
                    shoot(DAMAGE, _vec3i(1, 200, 41), 5, tinfo.dist2);
                } else if (turnSpeed == 6 && canShoot()) {
                    shoot(DAMAGE, _vec3i(-2, 200, 40), 8, tinfo.dist2);
                }
                turnSpeed++;
                if (mood == MOOD_ESCAPE) {
                    nextState = STATE_RUN;
                }
                break;
            }
        }

        return goalState;
    }
};


// Mr. T and his shotgun (TR1's BaldyControl): as the cowboy, one heavy shot.
struct MrT : Gunman
{
    enum {
        DIST_WALK = 4 * 1024,
        DAMAGE = 150,
        ANIM_DEATH = 14
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_AIM,
        STATE_DEATH,
        STATE_SHOOT
    };

    MrT(Room* room) : Gunman(room, 200, 102)
    {
        state = STATE_RUN;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (fall(ANIM_DEATH)) {
                drop(ITEM_SHOTGUN);
            }
            return goalState;
        }

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if (canShoot())
                    return STATE_AIM;
                return (mood == MOOD_SLEEP) ? STATE_WALK : STATE_RUN;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ANGLE(3);

                if (mood == MOOD_ESCAPE || !tinfo.front)
                    nextState = STATE_RUN;
                else if (canShoot())
                    nextState = STATE_AIM;
                else if (tinfo.dist2 > DIST_WALK * DIST_WALK)
                    nextState = STATE_RUN;
                else
                    break;
                return STATE_STOP;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ANGLE(6);
                tinfo.tilt = tinfo.turn >> 1;

                if (mood != MOOD_ESCAPE || tinfo.front)
                {
                    if (canShoot())
                        nextState = STATE_AIM;
                    else if (tinfo.front && tinfo.dist < DIST_WALK)
                        nextState = STATE_WALK;
                    else
                        break;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_AIM:
            {
                turnSpeed = 0;
                if (nextState)
                    return STATE_STOP;
                return canShoot() ? STATE_SHOOT : STATE_STOP;
            }

            case STATE_SHOOT:
            {
                if (!turnSpeed)
                {
                    shoot(DAMAGE, _vec3i(-20, 440, 20), 9, tinfo.dist2 >> 1);   // retail halves Kold's squared distance
                    turnSpeed = 1;
                }
                if (mood == MOOD_ESCAPE) {
                    nextState = STATE_RUN;
                }
                break;
            }
        }

        return goalState;
    }
};


// Atlantis' mutants, as retail's FlyerControl (06030ffc): they claw at Lara
// close up and, the shooter and the winged one, fire shards and rockets on
// her from afar. The winged one takes to the air when it can neither reach
// nor shoot her, or flees, and lands once it can reach her. turnSpeed is
// retail's creature flags (BULLET_1, BULLET_2, FLY_MODE, TWIST).
struct Mutant : Gunman
{
    enum {
        HIT_MASK = 0x678,
        DIST_WALK = 4608,
        DIST_ATTACK_1 = 600,
        DIST_ATTACK_2 = 2560,
        DIST_ATTACK_3 = 300,
        DIST_SHOOT = 3840,
        DAMAGE_CHARGE = 100,
        DAMAGE_LUNGE = 150,
        DAMAGE_PUNCH = 200
    };

    enum {
        BULLET_1 = 1,
        BULLET_2 = 2,
        FLY_MODE = 4,
        TWIST    = 8
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_WALK,
        STATE_RUN,
        STATE_ATTACK_1,
        STATE_DEATH,
        STATE_POSE,
        STATE_ATTACK_2,
        STATE_ATTACK_3,
        STATE_AIM_1,
        STATE_AIM_2,
        STATE_SHOOT,
        STATE_MUMMY,
        STATE_FLY
    };

    Mutant(Room* room) : Gunman(room, 50, 341)
    {
        headOffset = 150;
        if (type != ITEM_MUTANT_1) {
            visibleMask = 0xFFE07FFF; // no wings
        }
        if (type == ITEM_MUTANT_2) {
            aggression = 0x2000;    // the shooter's smartness (retail's object table)
        }
    }

    // its path on the ground, or in the air at 32 a tick
    void ground()
    {
        extraE->nav.stepHeight = 256;
        extraE->nav.dropHeight = -256;
        extraE->nav.vSpeed = 0;
        extraE->nav.zoneType = ZONE_GROUND_1;
    }

    void air()
    {
        extraE->nav.stepHeight = 30 * 1024;
        extraE->nav.dropHeight = -30 * 1024;
        extraE->nav.vSpeed = 32;
        extraE->nav.zoneType = ZONE_FLY;
    }

    virtual void update()
    {
        if ((flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE)
        {
            if (!enable(false))
                return;
            flags &= ~ITEM_FLAG_STATUS;
            flags |= ITEM_FLAG_STATUS_ACTIVE;
        }

        if (!extraE)
            return;

        // dead, it bursts (FlyerControl: ExplodingDeath, 100 a part)
        if (health <= 0)
        {
            if (explodingDeath(this, 0xFFFFFFFF, 100))
            {
                soundPlay(SND_MUTANT_DEATH, &pos);
                untarget(this);
                health = NOT_ENEMY;
                flags &= ~ITEM_FLAG_STATUS;
                flags |= ITEM_FLAG_STATUS_INVISIBLE;
                disable();
                deactivate();
            }
            return;
        }

        ground();
        updateTargetInfo();

        // a shot: in sight, out of reach or far, its side picking the gun
        bool shoot1 = false;
        bool shoot2 = false;
        if (type != ITEM_MUTANT_3 && canShoot() && (!checkZone() || tinfo.dist2 > DIST_SHOOT * DIST_SHOOT))
        {
            if (tinfo.angle > 0 && tinfo.angle < ANGLE_45) {
                shoot1 = true;
            } else if (tinfo.angle < 0 && tinfo.angle > -ANGLE_45) {
                shoot2 = true;
            }
        }

        if (type == ITEM_MUTANT_1)
        {
            if (state == STATE_FLY)
            {
                if ((turnSpeed & FLY_MODE) && mood != MOOD_ESCAPE && checkZone()) {
                    turnSpeed &= ~FLY_MODE;
                }
                if (!(turnSpeed & FLY_MODE)) {
                    updateMood(true);
                }
                air();
                updateTargetInfo();
            }
            else if ((!checkZone() && !shoot1 && !shoot2 && (!tinfo.front || mood == MOOD_SLEEP)) || mood == MOOD_ESCAPE)
            {
                turnSpeed |= FLY_MODE;
            }
        }

        int16 head = tinfo.front ? tinfo.angle : 0;

        if (state != STATE_FLY) {
            updateMood(false);
        } else if (turnSpeed & FLY_MODE) {
            updateMood(true);
        }

        updateNavigation();

        switch (state)
        {
            case STATE_MUMMY:
                goalState = STATE_STOP;
                break;

            case STATE_STOP:
                turnSpeed &= ~(BULLET_1 | BULLET_2 | TWIST);
                if (turnSpeed & FLY_MODE) {
                    goalState = STATE_FLY;
                } else if ((hitMask & HIT_MASK) || (tinfo.canAttack && tinfo.dist < DIST_ATTACK_3)) {
                    goalState = STATE_ATTACK_3;
                } else if (tinfo.canAttack && tinfo.dist < DIST_ATTACK_1) {
                    goalState = STATE_ATTACK_1;
                } else if (shoot1) {
                    goalState = STATE_AIM_1;
                } else if (shoot2) {
                    goalState = STATE_AIM_2;
                } else if (mood == MOOD_SLEEP || (mood == MOOD_STALK && tinfo.dist < DIST_WALK)) {
                    goalState = STATE_POSE;
                } else {
                    goalState = STATE_RUN;
                }
                break;

            case STATE_POSE:
                head = 0;
                if (shoot1 || shoot2 || (turnSpeed & FLY_MODE)) {
                    goalState = STATE_STOP;
                } else if (mood == MOOD_STALK) {
                    if (tinfo.dist < DIST_WALK) {
                        if (checkZone() || rand_logic() < 0x100) {
                            goalState = STATE_WALK;
                        }
                    } else {
                        goalState = STATE_STOP;
                    }
                } else if (mood == MOOD_SLEEP && rand_logic() < 0x100) {
                    goalState = STATE_WALK;
                } else if (mood == MOOD_ATTACK || mood == MOOD_ESCAPE) {
                    goalState = STATE_STOP;
                }
                break;

            case STATE_WALK:
                extraE->maxTurn = ENEMY_TURN_2;
                if (shoot1 || shoot2 || (turnSpeed & FLY_MODE)) {
                    goalState = STATE_STOP;
                } else if (mood == MOOD_ATTACK || mood == MOOD_ESCAPE) {
                    goalState = STATE_STOP;
                } else if (mood == MOOD_SLEEP || (mood == MOOD_STALK && !checkZone())) {
                    if (rand_logic() < 80) {
                        goalState = STATE_POSE;
                    }
                } else if (mood == MOOD_STALK && tinfo.dist2 > DIST_WALK * DIST_WALK) {
                    goalState = STATE_STOP;
                }
                break;

            case STATE_RUN:
                extraE->maxTurn = ANGLE(6);
                if (turnSpeed & FLY_MODE) {
                    goalState = STATE_STOP;
                } else if (hitMask & HIT_MASK) {
                    goalState = STATE_STOP;
                } else if (tinfo.canAttack && tinfo.dist < DIST_ATTACK_1) {
                    goalState = STATE_STOP;
                } else if (tinfo.front && tinfo.dist < DIST_ATTACK_2) {
                    goalState = STATE_ATTACK_2;
                } else if (shoot1 || shoot2) {
                    goalState = STATE_STOP;
                } else if (mood == MOOD_SLEEP || (mood == MOOD_STALK && tinfo.dist < DIST_WALK)) {
                    goalState = STATE_STOP;
                }
                break;

            case STATE_ATTACK_1:
            case STATE_ATTACK_2:
            case STATE_ATTACK_3:
                if (!nextState && (hitMask & HIT_MASK))
                {
                    int32 damage = (state == STATE_ATTACK_1) ? DAMAGE_LUNGE :
                                   (state == STATE_ATTACK_2) ? DAMAGE_CHARGE : DAMAGE_PUNCH;
                    bite(damage, _vec3i(-27, 98, 0), 10);
                    nextState = (state == STATE_ATTACK_2) ? STATE_RUN : STATE_STOP;
                }
                break;

            case STATE_AIM_1:
                turnSpeed |= TWIST | BULLET_1;
                goalState = shoot1 ? STATE_SHOOT : STATE_STOP;
                break;

            case STATE_AIM_2:
                turnSpeed |= BULLET_2;
                goalState = shoot2 ? STATE_SHOOT : STATE_STOP;
                break;

            case STATE_SHOOT:
                if (turnSpeed & BULLET_1)
                {
                    turnSpeed &= ~BULLET_1;
                    fire(ITEM_MUTANT_BULLET, _vec3i(-35, 269, 0), 9);
                }
                else if (turnSpeed & BULLET_2)
                {
                    turnSpeed &= ~BULLET_2;
                    fire(ITEM_CENTAUR_BULLET, _vec3i(51, 213, 0), 14);
                }
                break;

            case STATE_FLY:
                if (!(turnSpeed & FLY_MODE) && pos.y == roomFloor) {
                    goalState = STATE_STOP;
                }
                break;
        }

        // the head turns with its body twisted to aim, the neck otherwise
        if (!(turnSpeed & TWIST)) {
            extraE->rotHead = extraE->rotNeck;
        }
        extraE->rotHead = angleLerp(extraE->rotHead, head, ANGLE(5));
        if (!(turnSpeed & TWIST))
        {
            extraE->rotNeck = extraE->rotHead;
            extraE->rotHead = 0;
        } else {
            extraE->rotNeck = 0;
        }

    #ifdef TRS_HOST
        hostTargetInfo(this);
    #endif

        hitMask = 0;
        flags &= ~ITEM_FLAG_INJURED;

        animProcess();
        updateLocation();
    }
};


// Centaurs rear up and kick close by, and fire rockets on Lara from afar
// (TR1's CentaurControl). They burst when their death is done.
struct Centaur : Gunman
{
    enum {
        HIT_MASK = 0x30199,
        DIST_REAR = 1536,
        DAMAGE_REAR = 200,
        ANIM_DEATH = 8
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_SHOOT,
        STATE_RUN,
        STATE_AIM,
        STATE_DEATH,
        STATE_WARNING
    };

    Centaur(Room* room) : Gunman(room, 120, 341)
    {
        headOffset = 400;
    }


    // it bursts (CentaurControl), 100 a part
    virtual void deathEnd()
    {
        soundPlay(SND_MUTANT_DEATH, &pos);
        explodingDeath(this, 0xFFFFFFFF, 100);
        untarget(this);
        flags &= ~ITEM_FLAG_STATUS;
        flags |= ITEM_FLAG_STATUS_INVISIBLE;
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            fall(ANIM_DEATH);
            return goalState;
        }

        extraE->maxTurn = ENEMY_TURN_4;

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if (tinfo.canAttack && tinfo.dist < DIST_REAR)
                    return STATE_RUN;
                if (canShoot())
                    return STATE_AIM;
                return STATE_RUN;
            }

            case STATE_RUN:
            {
                if (tinfo.canAttack && tinfo.dist < DIST_REAR)
                    nextState = STATE_WARNING;
                else if (canShoot())
                    nextState = STATE_AIM;
                else if (rand_logic() < 96)
                    nextState = STATE_WARNING;
                else
                    break;
                return STATE_STOP;
            }

            case STATE_AIM:
            {
                if (nextState)
                    return nextState;
                return canShoot() ? STATE_SHOOT : STATE_STOP;
            }

            case STATE_SHOOT:
            {
                if (!nextState)
                {
                    nextState = STATE_AIM;
                    fire(ITEM_CENTAUR_BULLET, _vec3i(11, 415, 41), 13);
                }
                break;
            }

            case STATE_WARNING:
            {
                if (!nextState && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE_REAR, _vec3i(50, 30, 0), 5);
                    nextState = STATE_STOP;
                }
                break;
            }
        }

        return goalState;
    }
};


// Natla, as retail's NatlaControl (0602500c). Above 200 hit points she
// flies, firing a rocket every 30 ticks, until she has a shot, when she
// lands (1 in 256 a tick) and fires three rockets at once from the ground,
// taking off again whenever she has none. Brought down to 200 she falls and
// lies untargetable for 16 seconds, then rises to the music of track 54 and
// runs at Lara firing shards every 20 ticks. In flight her body turns to
// Lara 5 degrees a tick while she moves along her path. turnSpeed is
// retail's creature flags: the tick count below, FLY_MODE above.
struct Natla : Gunman
{
    enum {
        NEAR_DEATH = 200,
        DIE_TIME   = 30 * 16,
        FLY_MODE   = 0x8000,
        TIMER      = 0x7FFF
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_FLY,
        STATE_RUN,
        STATE_AIM,
        STATE_SEMIDEATH,
        STATE_SHOOT,
        STATE_FALL,
        STATE_STAND,
        STATE_DEATH
    };

    Natla(Room* room) : Gunman(room, 400, 204) {}

    // her path on the ground, or in the air at 32 a tick
    void ground()
    {
        extraE->nav.stepHeight = 256;
        extraE->nav.dropHeight = -256;
        extraE->nav.vSpeed = 0;
        extraE->nav.zoneType = ZONE_GROUND_1;
    }

    void air()
    {
        extraE->nav.stepHeight = 20 * 1024;
        extraE->nav.dropHeight = -20 * 1024;
        extraE->nav.vSpeed = 32;
        extraE->nav.zoneType = ZONE_FLY;
    }

    bool hasShot()
    {
        return abs(tinfo.angle) < ANGLE(30) && canShoot();
    }

    // a shot from her gun, its pitch held by her gun arm, and its sound
    void shoot(ItemType fxType, int16 &gun, bool sound)
    {
        if (ItemObj* fx = fire(fxType, _vec3i(5, 220, 7), 4))
        {
            gun = fx->angle.x;
            if (sound) {
                soundPlay(SND_NATLA_SHOT, &fx->pos);
            }
        }
    }

    virtual void update()
    {
        static int16 facing;    // retail's own static: her body's turn from her path

        if ((flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE)
        {
            if (!enable(false))
                return;
            flags &= ~ITEM_FLAG_STATUS;
            flags |= ITEM_FLAG_STATUS_ACTIVE;
        }

        if (!extraE)
            return;

        int16 head = 0;
        int16 turn = 0;
        int16 bank = 0;
        int16 gun = int16(extraE->rotHead * 7 / 8);
        int32 timer = turnSpeed & TIMER;

        if (health <= 0 && health > NOT_ENEMY)
        {
            goalState = STATE_DEATH;
        }
        else if (health <= NEAR_DEATH)
        {
            ground();
            updateTargetInfo();
            if (tinfo.front) {
                head = tinfo.angle;
            }
            updateMood(true);
            extraE->maxTurn = ANGLE(6);
            updateNavigation();
            turn = tinfo.turn;

            bool shot = hasShot();

            if (facing)
            {
                angle.y += facing;
                facing = 0;
            }

            switch (state)
            {
                case STATE_FALL:
                    if (pos.y < roomFloor)
                    {
                        flags |= ITEM_FLAG_GRAVITY;
                        hSpeed = 0;
                    }
                    else
                    {
                        flags &= ~ITEM_FLAG_GRAVITY;
                        goalState = STATE_SEMIDEATH;
                        pos.y = roomFloor;
                        timer = 0;
                    }
                    break;

                case STATE_STAND:
                    if (!shot) {
                        goalState = STATE_RUN;
                    }
                    if (timer >= 20)
                    {
                        shoot(ITEM_MUTANT_BULLET, gun, true);
                        timer = 0;
                    }
                    break;

                case STATE_RUN:
                    bank = turn;
                    if (timer >= 20)
                    {
                        shoot(ITEM_MUTANT_BULLET, gun, true);
                        timer = 0;
                    }
                    if (shot) {
                        goalState = STATE_STAND;
                    }
                    break;

                case STATE_SEMIDEATH:
                    if (timer == DIE_TIME)
                    {
                        goalState = STATE_STAND;
                        turnSpeed = 0;
                        timer = 0;
                        health = NEAR_DEATH;
                        musicPlay(54);
                    } else {
                        health = NOT_ENEMY;     // Lara can't target her
                    }
                    break;

                case STATE_FLY:
                    goalState = STATE_FALL;
                    timer = 0;
                    break;

                case STATE_STOP:
                case STATE_AIM:
                case STATE_SHOOT:
                    goalState = STATE_SEMIDEATH;
                    timer = 0;
                    break;
            }
        }
        else
        {
            ground();
            updateTargetInfo();

            bool shot = hasShot();

            if (state == STATE_FLY && (turnSpeed & FLY_MODE))
            {
                if (shot && rand_logic() < 0x100) {
                    turnSpeed &= ~FLY_MODE;
                }
                if (!(turnSpeed & FLY_MODE)) {
                    updateMood(true);
                }
                air();
                updateTargetInfo();
            }
            else if (!shot)
            {
                turnSpeed |= FLY_MODE;
            }

            if (tinfo.front) {
                head = tinfo.angle;
            }

            if (state != STATE_FLY || (turnSpeed & FLY_MODE)) {
                updateMood(false);
            }

            angle.y -= facing;
            extraE->maxTurn = ANGLE(5);
            updateNavigation();
            turn = tinfo.turn;

            if (state == STATE_FLY)
            {
                facing += X_CLAMP(tinfo.angle, -ANGLE(5), ANGLE(5));
                angle.y += facing;
            }
            else
            {
                angle.y += facing - turn;
                facing = 0;
            }

            switch (state)
            {
                case STATE_STOP:
                    timer = 0;
                    goalState = (turnSpeed & FLY_MODE) ? STATE_FLY : STATE_AIM;
                    break;

                case STATE_FLY:
                    if (!(turnSpeed & FLY_MODE) && pos.y == roomFloor) {
                        goalState = STATE_STOP;
                    }
                    if (timer >= 30)
                    {
                        shoot(ITEM_CENTAUR_BULLET, gun, true);
                        timer = 0;
                    }
                    break;

                case STATE_AIM:
                    if (nextState) {
                        goalState = nextState;
                    } else {
                        goalState = shot ? STATE_SHOOT : STATE_STOP;
                    }
                    break;

                case STATE_SHOOT:
                    if (!nextState)
                    {
                        // three rockets, two of them spread
                        shoot(ITEM_CENTAUR_BULLET, gun, false);
                        for (int32 i = 0; i < 2; i++)
                        {
                            if (ItemObj* fx = fire(ITEM_CENTAUR_BULLET, _vec3i(5, 220, 7), 4)) {
                                fx->angle.y += (rand_logic() - 0x4000) / 4;
                            }
                        }
                        nextState = STATE_STOP;
                    }
                    break;
            }
        }

        tilt(bank);
        extraE->rotNeck = -head;
        if (gun) {
            extraE->rotHead = gun;
        }

        timer++;
        turnSpeed = (turnSpeed & FLY_MODE) | (timer & TIMER);

    #ifdef TRS_HOST
        hostTargetInfo(this);
    #endif

        hitMask = 0;
        flags &= ~ITEM_FLAG_INJURED;

        tinfo.turn = turn;
        angle.y -= facing;
        animProcess();
        updateLocation();
        angle.y += facing;

        if ((flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE)
        {
            flags &= ~ITEM_FLAG_COLLISION;
            health = NOT_ENEMY;
            disable();
            deactivate();
        }
    }
};


// The torso in the Great Pyramid (TR1's TorsoControl): drops in, turns
// toward Lara in heavy steps, swipes, and crushes her when she is weak. Its
// death opens the way on (the trigger under it). turnSpeed marks a landed blow.
struct Adam : Enemy
{
    enum {
        HIT_MASK_LEFT = 0x7FF0,
        HIT_MASK_RIGHT = 0x3FF8000,
        HIT_MASK = HIT_MASK_LEFT | HIT_MASK_RIGHT,
        DIST_ATTACK = 2600,
        DIST_CLOSE = 2250,
        DAMAGE_ATTACK = 500,
        DAMAGE_TOUCH = 5,
        ANIM_TURN_L = 8,
        ANIM_DEATH = 13,
        ANIM_TURN_R = 17
    };

    enum {
        STATE_NONE,
        STATE_STOP,
        STATE_TURN_L,
        STATE_TURN_R,
        STATE_ATTACK_1,
        STATE_ATTACK_2,
        STATE_ATTACK_3,
        STATE_FORWARD,
        STATE_SET,
        STATE_FALL,
        STATE_DEATH,
        STATE_KILL
    };

    Adam(Room* room) : Enemy(room, 500, 341, 0, AGGRESSION_LVL_MAX)
    {
        turnSpeed = 0;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, SND_HIT_ADAM);
    }

    // it bursts, 250 a part, and sets off the trigger under it
    // (AbortionControl)
    virtual void deathEnd()
    {
        soundPlay(SND_MUTANT_DEATH, &pos);
        explodingDeath(this, 0xFFFFFFFF, 250);
        room->getSector(pos.x, pos.z)->getFloor(pos.x, pos.y, pos.z);
        checkTrigger(gLastFloorData, NULL);
        untarget(this);
        flags &= ~ITEM_FLAG_STATUS;
        flags |= ITEM_FLAG_STATUS_INVISIBLE;
    }

    virtual int32 updateState()
    {
        extraE->maxTurn = 0;

        if (health <= 0)
        {
            if (state != STATE_DEATH)       // its fall, picked here as AbortionControl's
            {
                animSet(level.models[type].animIndex + ANIM_DEATH, true);
                // retail sets the state itself: the death animation's own is
                // 0, and taken from it the fall began again every tick
                state = goalState = STATE_DEATH;
            }
            return goalState;
        }

        Lara* lara = (Lara*)tinfo.target;

        // it steers for its path's target, not for Lara (AbortionControl)
        int16 turn = phd_atan(tinfo.waypoint.z - pos.z, tinfo.waypoint.x - pos.x) - angle.y;

        if (hitMask) {
            lara->hit(DAMAGE_TOUCH, pos, 0);
        }

        int32 frame = frameIndex - level.anims[animIndex].frameBegin;
        int32 anim = animIndex - level.models[type].animIndex;

        switch (state)
        {
            case STATE_SET:
                flags |= ITEM_FLAG_GRAVITY;
                return STATE_FALL;

            case STATE_FALL:
            {
                if (pos.y >= roomFloor)
                {
                    flags &= ~ITEM_FLAG_GRAVITY;
                    pos.y = roomFloor;
                    cameraBounce(500);  // retail's 06024b34
                    return STATE_STOP;
                }
                break;
            }

            case STATE_STOP:
            {
                if (lara->health <= 0)
                    break;
                turnSpeed = 0;
                if (turn > ANGLE_45)
                    return STATE_TURN_R;
                if (turn < -ANGLE_45)
                    return STATE_TURN_L;
                if (tinfo.dist >= DIST_ATTACK)
                    return STATE_FORWARD;
                if (lara->health > DAMAGE_ATTACK)
                    return (rand_logic() < 0x4000) ? STATE_ATTACK_1 : STATE_ATTACK_2;
                if (tinfo.dist < DIST_CLOSE)
                    return STATE_ATTACK_3;
                return STATE_FORWARD;
            }

            case STATE_FORWARD:
            {
                // never turns: retail adds the turn to the goal state, not to
                // the yaw (a TR1 bug, the Saturn's too), so it stops to turn
                if (abs(turn) > ANGLE_45 || tinfo.dist < DIST_ATTACK)
                    return STATE_STOP;
                break;
            }

            case STATE_TURN_L:
            {
                if (anim == ANIM_TURN_L && frame >= 14 && frame <= 22) {
                    angle.y -= ANGLE(9);
                }
                if (turn > -ANGLE_45)
                    return STATE_STOP;
                break;
            }

            case STATE_TURN_R:
            {
                if (anim == ANIM_TURN_R && frame >= 17 && frame <= 22) {
                    angle.y += ANGLE(14);
                }
                if (turn < ANGLE_45)
                    return STATE_STOP;
                break;
            }

            case STATE_ATTACK_1:
            case STATE_ATTACK_2:
            {
                uint32 mask = (state == STATE_ATTACK_1) ? HIT_MASK_RIGHT : HIT_MASK;
                if (!turnSpeed && (hitMask & mask))
                {
                    lara->hit(DAMAGE_ATTACK, pos, 0);
                    turnSpeed = 1;
                }
                break;
            }

            case STATE_ATTACK_3:
            {
                // it takes her in its hand and crushes her: her extra model's
                // first animation, the camera in to 2 sectors
                if ((hitMask & HIT_MASK_RIGHT) || lara->health <= 0)
                {
                    laraKilledBy(lara, this, 0);
                    lara->extraL->camera.targetDist = 2048;
                    return STATE_KILL;
                }
                break;
            }

            case STATE_KILL:
            {
                lara->extraL->camera.targetDist = 2048;
                break;
            }
        }

        return goalState;
    }
};

#endif
