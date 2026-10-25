#ifndef H_ENEMY
#define H_ENEMY

#include "common.h"
#include "item.h"

#define ENEMY_TURN_1    ANGLE(1)
#define ENEMY_TURN_2    ANGLE(2)
#define ENEMY_TURN_4    ANGLE(4)
#define ENEMY_TURN_5    ANGLE(5)

EWRAM_DATA ExtraInfoEnemy enemiesExtra[MAX_ENEMIES];

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
        flags |= ITEM_FLAG_SHADOW;

    #ifndef STATIC_ITEMS
        angle.y += (rand_logic() - 0x4000) >> 1;
    #endif

        ASSERT(_radius < 512);

        health = _health;
        radius = _radius >> 1;
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
        extra->maxTurn = 0;
        extra->nav.stepHeight = 256;
        extra->nav.dropHeight = -256;
        extra->nav.vSpeed = 0;
        extraE = extra;

        initExtra();

        const Sector* sector = room->getSector(pos.x, pos.z);
        ASSERT(sector->boxIndex != NO_BOX);
        extraE->nav.init(sector->boxIndex);
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

        //@TODO blocking

        if (health <= 0)
            return;

        if (tinfo.target->health <= 0)
        {
            hitMask = 0;
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

        tinfo.dist = fastLength(dx, dz);
        tinfo.angle = rot - angle.y;
        tinfo.front = abs(tinfo.angle) < ANGLE_90;
        tinfo.behind = abs(rot - tinfo.target->angle.y - ANGLE_180) < ANGLE_90;
        tinfo.canAttack = tinfo.front && (abs(tinfo.target->pos.y - pos.y) < 256);
        tinfo.rotHead = (tinfo.front && mood != MOOD_SLEEP) ? tinfo.angle : 0;
    }

    void updateMood()
    {
        //if (extraE->nav.cells[extraE->boxIndex].weight == ) //@TODO blocking

        if (mood != MOOD_ATTACK && extraE->nav.nextBox != NO_BOX && !checkZone(extraE->nav.endBox))
        {
            bool inZone = checkZone();
            if (!inZone) {
                mood = MOOD_SLEEP;
            }
            extraE->nav.nextBox = NO_BOX;
        }

        uint32 nextMood = tinfo.target->health <= 0 ? MOOD_SLEEP : ((flags & ITEM_FLAG_ANIMATED) ? getMoodWild() : getMoodNormal());

        flags &= ~ITEM_FLAG_INJURED;

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

        if (extraE->nav.zoneType == ZONE_FLY) {
            extraE->nav.pos.y += tinfo.target->getFrame()->box.minY; // attack Laras head
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

        int16 maxTurn = extraE->maxTurn;

        if (health <= 0 || !hSpeed || !maxTurn)
            return;

        int32 dx = tinfo.waypoint.x - pos.x;
        int32 dz = tinfo.waypoint.z - pos.z;
        int16 turn = phd_atan(dz, dx) - angle.y;
        int32 r = (hSpeed << FIXED_SHIFT) / maxTurn; //@DIV
        int32 d = fastLength(dx, dz);
        bool aim = (turn > -ANGLE_90) && (turn < ANGLE_90);

        if (!aim && (d < r))
            maxTurn >>= 1;

        tinfo.turn = X_CLAMP(turn, -maxTurn, maxTurn);
    }

    void setNextBox(int16 boxIndex)
    {
        boxIndex &= 0x7FFF;
        const Box* box = level.boxes + boxIndex;

        int32 bMinX = (box->minX << 10) + 512;
        int32 bMaxX = (box->maxX << 10) - 512;
        int32 bMinZ = (box->minZ << 10) + 512;
        int32 bMaxZ = (box->maxZ << 10) - 512;
    
        extraE->nav.nextBox = boxIndex;
        extraE->nav.pos.x = bMinX + RAND_LOGIC(bMaxX - bMinX);
        extraE->nav.pos.z = bMinZ + RAND_LOGIC(bMaxZ - bMinZ);
        extraE->nav.pos.y = box->floor;
        if (extraE->nav.zoneType != ZONE_FLY)
            extraE->nav.pos.y -= 384;
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
            return ((flags & ITEM_FLAG_INJURED) && rand_logic() < 0x800) ? MOOD_ESCAPE : (!inZone ? MOOD_SLEEP : mood);

        return (inZone && rand_logic() < 0x100) ? MOOD_STALK : mood;
    }

    X_INLINE bool checkZone()
    {
        return tinfo.zoneIndex == tinfo.zoneIndexTarget;
    }

    X_INLINE int32 getRandomBox()
    {
        return extraE->nav.cells[RAND_LOGIC(extraE->nav.cellsCount)].boxIndex;
    }

    bool checkZone(int16 boxIndex)
    {
        if (getZones()[boxIndex] != tinfo.zoneIndex)
            return false;

        const Box &b = level.boxes[boxIndex];

        //@TODO blocking

        return (pos.x <= (b.minX << 10)) || (pos.x >= ((b.maxX << 10) - 1)) || 
               (pos.z <= (b.minZ << 10)) || (pos.z >= ((b.maxZ << 10) - 1));
    }

    bool checkStalk(int16 boxIndex)
    {
        const Box &b = level.boxes[boxIndex];

        int32 dx = (((b.minX + b.maxX) << 10) >> 1) - tinfo.target->pos.x;
        int32 dz = (((b.minZ + b.maxZ) << 10) >> 1) - tinfo.target->pos.z;

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

        int32 dx = (((b.minX + b.maxX) << 10) >> 1) - tinfo.target->pos.x;
        int32 dz = (((b.minZ + b.maxZ) << 10) >> 1) - tinfo.target->pos.z;

        if (X_MAX(abs(dx), abs(dz)) < 5120)
            return false;

        int32 px = pos.x - tinfo.target->pos.x;
        int32 pz = pos.z - tinfo.target->pos.z;

        return !(((dx ^ px) & 0x80000000) && ((dz ^ pz) & 0x80000000));
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
        fxBlood(fxPos, 0, 0);
        
        tinfo.target->hit(damage, offset, 0);
    }

    // TR1's box test (TRX Box_BadFloor): a floor the creature can't step to
    // from the height of its box, a blocked or missing box, or a step down
    // into a box higher than the one its path goes through next.
    bool badFloor(int32 x, int32 y, int32 z, int32 boxHeight, int32 nextHeight)
    {
        const Sector* sector = room->getRoom(x, y, z)->getSector(x, z);
        if (sector->boxIndex == NO_BOX)
            return true;

        const Box &b = level.boxes[sector->boxIndex];
        if (b.overlap & extraE->nav.mask)
            return true;

        int32 d = boxHeight - b.floor;
        if (d > extraE->nav.stepHeight || d < extraE->nav.dropHeight)
            return true;

        return d < -extraE->nav.stepHeight && b.floor > nextHeight;
    }

    // A moving creature ahead of this one in its room's list and nearer than
    // this one's radius, as TR1 tests: each pair once, so the later one gives
    // way and the two part.
    bool overlapsCreature()
    {
        if (!hSpeed || health <= 0)
            return false;

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

        int32 y = pos.y + getFrame()->box.minY;
        int32 boxHeight = (tinfo.boxIndex == NO_BOX) ? roomFloor : level.boxes[tinfo.boxIndex].floor;

        const Sector* sector = room->getRoom(pos.x, y, pos.z)->getSector(pos.x, pos.z);
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

            sector = room->getRoom(pos.x, y, pos.z)->getSector(pos.x, pos.z);
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
            if (badFloor(x, y, z - r, height, nextHeight)) {
                shiftZ = r - pz;
            }

            if (px < r)
            {
                if (badFloor(x - r, y, z, height, nextHeight)) {
                    shiftX = r - px;
                } else if (!shiftZ && badFloor(x - r, y, z - r, height, nextHeight)) {
                    if (angle.y > -ANGLE_135 && angle.y < ANGLE_45) {
                        shiftZ = r - pz;
                    } else {
                        shiftX = r - px;
                    }
                }
            }
            else if (px > 1024 - r)
            {
                if (badFloor(x + r, y, z, height, nextHeight)) {
                    shiftX = 1024 - r - px;
                } else if (!shiftZ && badFloor(x + r, y, z - r, height, nextHeight)) {
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
            if (badFloor(x, y, z + r, height, nextHeight)) {
                shiftZ = 1024 - r - pz;
            }

            if (px < r)
            {
                if (badFloor(x - r, y, z, height, nextHeight)) {
                    shiftX = r - px;
                } else if (!shiftZ && badFloor(x - r, y, z + r, height, nextHeight)) {
                    if (angle.y > -ANGLE_45 && angle.y < ANGLE_135) {
                        shiftX = r - px;
                    } else {
                        shiftZ = 1024 - r - pz;
                    }
                }
            }
            else if (px > 1024 - r)
            {
                if (badFloor(x + r, y, z, height, nextHeight)) {
                    shiftX = 1024 - r - px;
                } else if (!shiftZ && badFloor(x + r, y, z + r, height, nextHeight)) {
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
            if (badFloor(x - r, y, z, height, nextHeight)) {
                shiftX = r - px;
            }
        }
        else if (px > 1024 - r)
        {
            if (badFloor(x + r, y, z, height, nextHeight)) {
                shiftX = 1024 - r - px;
            }
        }

        if (shiftX || shiftZ)
        {
            pos.x += shiftX;
            pos.z += shiftZ;
            angle.y += tinfo.turn; // turns twice as fast against a wall
            tinfo.tilt <<= 1;
        }

        if (overlapsCreature())
        {
            pos = old;
            return;
        }

        updateRoom();

        if (extraE->nav.zoneType == ZONE_FLY)
        {
            int32 dy = X_CLAMP(tinfo.waypoint.y - pos.y, -extraE->nav.vSpeed, extraE->nav.vSpeed);
            int32 fy = pos.y + dy;

            const Sector* s = room->getSector(pos.x, pos.z);

            int32 floor = s->getFloor(pos.x, fy, pos.z);
            if (fy > floor)
            {
                if (pos.y > floor)
                {
                    pos.x = old.x;
                    pos.z = old.z;
                    dy = -extraE->nav.vSpeed; // go up
                } else {
                    pos.y = floor;
                    dy = 0;
                }
            }

            int32 ceiling = s->getCeiling(pos.x, fy, pos.z);
            if (fy < ceiling)
            {
                if (pos.y < ceiling)
                {
                    pos.x = old.x;
                    pos.z = old.z;
                    dy = extraE->nav.vSpeed; // go down
                } else {
                    pos.y = ceiling;
                    dy = 0;
                }
            }

            pos.y += dy;

            int32 pitch = 0;
            if (hSpeed) {
                pitch = phd_atan(hSpeed, -dy);
            }
            angle.x = angleLerp(angle.x, pitch, ANGLE_1);

            updateRoom();
        }
        else
        {
            // follows the floor, rising at most a quarter step a frame
            if (pos.y > roomFloor) {
                pos.y = roomFloor;
            } else if (roomFloor - pos.y > 64) {
                pos.y += 64;
            } else {
                pos.y = roomFloor;
            }
            angle.x = 0;
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

        if (type != ITEM_MUMMY) {
            fxBlood(point, 0, 0);
        }

        flags |= ITEM_FLAG_INJURED;
    }

    virtual void collide(Lara* lara, CollisionInfo* cinfo)
    {
        if (!extraE) // disabled
            return;

        if (!updateHitMask(lara, cinfo))
            return;

        if (!cinfo->enemyPush)
            return;
        
        int32 r = cinfo->radius;
        cinfo->radius = 0;
        collidePush(lara, cinfo, true);
        cinfo->radius = r;
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

        animProcess();

        updateLocation();

        extraE->rotHead = angleLerp(extraE->rotHead, tinfo.rotHead, ANGLE(5));
        angle.z += X_CLAMP((tinfo.tilt << 2) - angle.z, -ANGLE(3), ANGLE(3));
        angle.y += tinfo.turn;

        if ((flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE)
        {
            flags &= ~ITEM_FLAG_COLLISION;
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


// Lara's double in Atlantis mirrors her about the centre of the room she
// starts in, and dies when her mirrored ground gives way under her (TR1's
// BaconLaraControl). Shooting her hurts Lara. turnSpeed counts the frames her
// fall has been pending (two, as she runs a frame behind); aggression is set
// once she falls.
struct Doppelganger : Enemy
{
    Doppelganger(Room* room) : Enemy(room, LARA_MAX_HEALTH, 10, 0, 0)
    {
        headOffset = 0;
        turnSpeed = 0;
        aggression = 0;
        flags &= ~ITEM_FLAG_SHADOW;
        roomFloor = room - rooms; // the room she mirrors about, until she runs
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        getLara(pos)->hit(damage * 10, point, 0);
    }

    virtual void collide(Lara* lara, CollisionInfo* cinfo) {}

    virtual void update()
    {
        Lara* lara = getLara(pos);

        if (aggression)
        {
            animProcess();
            updateRoom();
            if (pos.y >= roomFloor)
            {
                pos.y = roomFloor;
                flags &= ~(ITEM_FLAG_GRAVITY | ITEM_FLAG_STATUS);
                flags |= ITEM_FLAG_STATUS_INACTIVE;
                vSpeed = 0;
                health = 0;
                deactivate();
            }
            return;
        }

        const RoomInfo* home = rooms[roomFloor].info;
        int32 cx = (home->x << 8) + (home->xSectors << 9);
        int32 cz = (home->z << 8) + (home->zSectors << 9);

        vec3i p = _vec3i(2 * cx - lara->pos.x, lara->pos.y, 2 * cz - lara->pos.z);

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
        {
            turnSpeed = 0;
            return;
        }

        if (++turnSpeed < 2)
            return;

        // the pit takes her
        animSet(Lara::ANIM_SMASH_JUMP, true, 1);
        state = goalState = Lara::STATE_FALL;
        hSpeed = 0;
        vSpeed = 0;
        flags |= ITEM_FLAG_GRAVITY;
        pos.y += 50;
        aggression = 1;
        roomFloor = floor;
    }
};


struct Wolf : Enemy
{
    enum {
        HIT_MASK = 0x774F,  // body, head, front legs

        DIST_STALK = 1023 * 3,
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
        frameIndex = level.anims[animIndex].frameEnd - 1;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, SND_HIT_WOLF);

        if (health <= 0) {
            animSet(level.models[type].animIndex + ANIM_DEATH + RAND_LOGIC(3), true);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                return STATE_WALK;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;
                tinfo.tilt = tinfo.turn >> 1;

                if (mood != MOOD_SLEEP) {
                    nextState = STATE_NONE;
                    return STATE_STALK;
                }
                if (rand_logic() < 0x20) {
                    nextState = STATE_SLEEP;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_5;
                tinfo.tilt = tinfo.turn;

                if (tinfo.front && tinfo.dist < DIST_ATTACK)
                {
                    if (tinfo.dist <= (DIST_ATTACK >> 1) && !tinfo.behind)
                    {
                        nextState = STATE_NONE;
                        return STATE_ATTACK;
                    }
                    nextState = STATE_STALK;
                    return STATE_GROWL;
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

            case STATE_STALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;

                if (mood == MOOD_ESCAPE)
                    return STATE_RUN;
                if ((tinfo.dist < DIST_BITE) && tinfo.canAttack && (tinfo.target->health > 0))
                    return STATE_BITE;
                if (tinfo.dist > DIST_STALK)
                    return STATE_RUN;
                if (mood == MOOD_ATTACK)
                {
                    if (!tinfo.front || (tinfo.dist > DIST_ATTACK) || tinfo.behind)
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

            case STATE_SLEEP:
            {
                if ((mood == MOOD_ESCAPE) || checkZone())
                    nextState = STATE_GROWL;
                else if (rand_logic() < 0x20)
                    nextState = STATE_WALK;
                else
                    break;
                return STATE_STOP;
            }

            case STATE_GROWL:
            {
                if (nextState)
                    return nextState;
                if (mood == MOOD_ESCAPE)
                    return STATE_RUN;
                if (tinfo.dist < DIST_BITE && tinfo.canAttack && (tinfo.target->health > 0))
                    return STATE_BITE;
                if (mood == MOOD_STALK)
                    return STATE_STALK;
                if (mood == MOOD_SLEEP)
                    return STATE_STOP;
                return STATE_RUN;
            }

            case STATE_ATTACK:
            {
                tinfo.tilt = tinfo.turn;

                if (!nextState && (hitMask & HIT_MASK)) {
                    bite(DAMAGE_ATTACK, _vec3i(0, -14, 174), 6);
                    nextState = STATE_RUN;
                }
                return STATE_RUN;
            }

            case STATE_BITE:
            {
                if (!nextState && (hitMask & HIT_MASK)) {
                    bite(DAMAGE_BITE, _vec3i(0, -14, 174), 6);
                    nextState = STATE_GROWL;
                }
                return STATE_RUN;
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
        flags |= ITEM_FLAG_ANIMATED; // TODO WILD flag
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, SND_HIT_BEAR);
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            switch (state)
            {
                case STATE_HIND:
                    return STATE_HOWL;
                case STATE_WALK:
                case STATE_RUN:
                    return STATE_STOP;
                case STATE_STOP:
                    flags &= ~ITEM_FLAG_REVERSE; // TODO special flag
                    return STATE_DEATH;
                case STATE_HOWL:
                    flags |= ITEM_FLAG_REVERSE;
                    return STATE_DEATH;
                case STATE_DEATH:
                    if ((flags & ITEM_FLAG_REVERSE) && (hitMask & HIT_MASK) && tinfo.target) // fall on Lara 
                    {
                        tinfo.target->hit(DAMAGE_FALL, _vec3i(0, 0, 0), 0);
                        flags &= ~ITEM_FLAG_REVERSE;
                    }
                    return STATE_DEATH;
                default:
                    return goalState;
            }
        }

        // hold the injured flag until death
        if (flags & ITEM_FLAG_INJURED) {
            flags |= ITEM_FLAG_REVERSE;
        } else {
            if (flags & ITEM_FLAG_REVERSE) {
                flags |= ITEM_FLAG_INJURED;
            }
        }

        switch (state)
        {
            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;

                if (tinfo.target->health <= 0 && tinfo.front && (hitMask & HIT_MASK))
                    return nextState = STATE_STOP; // eat lara! >:E

                if (mood != MOOD_SLEEP)
                {
                    if (mood == MOOD_ESCAPE) {
                        nextState = STATE_NONE;
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

            case STATE_STOP:
            {
                if (tinfo.target->health <= 0)
                    return (tinfo.canAttack && tinfo.dist < 768) ? STATE_EAT : STATE_WALK;
                if (nextState)
                    return nextState;
                return (mood == MOOD_SLEEP) ? STATE_WALK : STATE_RUN;
            }

            case STATE_HIND:
            {
                if (flags & ITEM_FLAG_INJURED) {
                    nextState = STATE_NONE;
                    return STATE_HOWL;
                }
                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_HOWL;
                if (mood == MOOD_ESCAPE) {
                    nextState = STATE_NONE;
                    return STATE_HOWL;
                }
                if (mood == MOOD_SLEEP || rand_logic() < 0x50) {
                    nextState = STATE_GROWL;
                    return STATE_HOWL;
                }
                if (tinfo.dist > 2048 || rand_logic() < 0x600) {
                    nextState = STATE_STOP;
                    return STATE_HOWL;
                }
                return STATE_HOWL;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ENEMY_TURN_5;

                if (hitMask & HIT_MASK) {
                    tinfo.target->hit(3, pos, 0);
                    if (health >= 0) {
                        nextState = STATE_NONE;
                    }
                }

                if (tinfo.target->health <= 0 || mood == MOOD_SLEEP)
                    return STATE_STOP;

                if (!nextState && tinfo.front)
                {
                    if (tinfo.dist < 2048)
                    {
                        if (!(flags & ITEM_FLAG_INJURED) && (rand_logic() < 0x300))
                        {
                            nextState = STATE_HOWL;
                            return STATE_STOP;
                        }
                        if (tinfo.dist < 1024)
                            return STATE_BITE;
                    }
                }
                break;
            }

            case STATE_HOWL:
            {
                if (flags & ITEM_FLAG_INJURED)
                {
                    nextState = STATE_NONE;
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

            case STATE_BITE:
            {
                if (!nextState && (hitMask & HIT_MASK)) {
                    bite(DAMAGE_BITE, _vec3i(0, 96, 335), 14);
                    nextState = STATE_STOP;
                }
                break;
            }

            case STATE_ATTACK:
            {
                if (!nextState && (hitMask & HIT_MASK) && tinfo.target) {
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


// The crocodile on land, the alligator in water (TR1's CrocodileControl and
// AlligatorControl).
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

    Crocodile(Room* room) : Enemy(room, 20, 341, 600, AGGRESSION_LVL_2) {}

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);

        if (health <= 0) {
            bool water = (type == ITEM_CROCODILE_WATER);
            animSet(level.models[type].animIndex + (water ? ANIM_DEATH_WATER : ANIM_DEATH_LAND), true);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

        if (type == ITEM_CROCODILE_WATER)
        {
            extraE->maxTurn = ENEMY_TURN_2 + ANGLE(1);

            if (state == STATE_SWIM)
            {
                if (tinfo.canAttack && hitMask)
                    return STATE_SWIM_ATTACK;
            }
            else if (state == STATE_SWIM_ATTACK)
            {
                if (frameIndex == level.anims[animIndex].frameBegin) {
                    nextState = STATE_NONE;
                }
                if (!tinfo.canAttack || !hitMask)
                    return STATE_SWIM;
                if (!nextState)
                {
                    bite(DAMAGE_BITE, _vec3i(5, -21, 467), 9);
                    nextState = STATE_SWIM;
                }
            }
            return goalState;
        }

        bool fastTurn = (mood == MOOD_ATTACK) && (tinfo.dist > DIST_FAST_TURN) &&
                        (abs(tinfo.angle) > ANGLE_90);

        if (state == STATE_FAST_TURN) {
            angle.y += ANGLE(6);
            extraE->maxTurn = 0;
        } else {
            extraE->maxTurn = ENEMY_TURN_2 + ANGLE(1);
        }

        switch (state)
        {
            case STATE_STOP:
            {
                if (tinfo.canAttack && tinfo.dist < DIST_BITE)
                    return STATE_ATTACK;
                if (mood == MOOD_ESCAPE)
                    return STATE_RUN;
                if (mood == MOOD_ATTACK)
                    return fastTurn ? STATE_FAST_TURN : STATE_RUN;
                if (mood == MOOD_STALK)
                    return STATE_WALK;
                break;
            }

            case STATE_WALK:
            {
                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_STOP;
                if (mood == MOOD_ATTACK || mood == MOOD_ESCAPE)
                    return STATE_RUN;
                if (mood == MOOD_SLEEP)
                    return STATE_STOP;
                break;
            }

            case STATE_FAST_TURN:
            {
                if (abs(tinfo.angle) < ANGLE_90)
                    return STATE_WALK;
                break;
            }

            case STATE_RUN:
            {
                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_STOP;
                if (mood == MOOD_STALK)
                    return STATE_WALK;
                if (mood == MOOD_SLEEP || fastTurn)
                    return STATE_STOP;
                break;
            }

            case STATE_ATTACK:
            {
                if (!nextState)
                {
                    bite(DAMAGE_BITE, _vec3i(5, -21, 467), 9);
                    nextState = STATE_STOP;
                }
                break;
            }
        }

        return goalState;
    }

    virtual void initExtra()
    {
        if (type == ITEM_CROCODILE_LAND) {
            extraE->nav.stepHeight = 256;
            extraE->nav.dropHeight = -256;
        } else {
            extraE->nav.stepHeight = 20 * 1024;
            extraE->nav.dropHeight = -20 * 1024;
        }
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
            case ITEM_PUMA        : health = 40; break;
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

        if (health <= 0) {
            int32 anim = (type == ITEM_PUMA) ? ANIM_DEATH_PUMA : ANIM_DEATH_LION;
            animSet(level.models[type].animIndex + anim + RAND_LOGIC(2), true);
        }
    }

    // TR1's LionControl
    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

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
        STATE_JUMP
    };

    Gorilla(Room* room) : Enemy(room, 22, 341, 250, AGGRESSION_LVL_MAX)
    {
        turnSpeed = 0;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);
        turnSpeed |= FLAG_ATTACK;

        if (health <= 0) {
            animSet(level.models[type].animIndex + ANIM_DEATH + RAND_LOGIC(2), true);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

        if (tinfo.dist < DIST_PANIC) {
            turnSpeed |= FLAG_ATTACK;
        }

        switch (state)
        {
            case STATE_STOP:
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
        STATE_SWIM_BITE
    };

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, SND_HIT_RAT);

        if (health <= 0) {
            bool water = (type == ITEM_RAT_WATER);
            animSet(level.models[type].animIndex + (water ? ANIM_DEATH_WATER : ANIM_DEATH_LAND), true);
        }
    }

    // TR1's RatControl and VoleControl
    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

        extraE->maxTurn = ANGLE(6);

        if (type == ITEM_RAT_WATER)
        {
            extraE->maxTurn = ANGLE(3);

            if (state == STATE_SWIM)
            {
                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_SWIM_BITE;
            }
            else if (state == STATE_SWIM_BITE)
            {
                if (!nextState && tinfo.front && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE, _vec3i(0, -11, 108), 3);
                    nextState = STATE_SWIM;
                }
                return STATE_NONE;
            }
            return goalState;
        }

        switch (state)
        {
            case STATE_STOP:
            {
                if (nextState)
                    return nextState;
                if (tinfo.canAttack && tinfo.dist < DIST_BITE)
                    return STATE_BITE;
                return STATE_RUN;
            }

            case STATE_RUN:
            {
                if (tinfo.front && (hitMask & HIT_MASK))
                    return STATE_STOP;
                if (tinfo.canAttack && tinfo.dist < DIST_CHARGE)
                    return STATE_CHARGE;
                if (tinfo.front && rand_logic() < 256)
                {
                    nextState = STATE_POSE;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_BITE:
            case STATE_CHARGE:
            {
                if (!nextState && tinfo.front && (hitMask & HIT_MASK))
                {
                    bite(DAMAGE, _vec3i(0, -11, 108), 3);
                    nextState = (state == STATE_BITE) ? STATE_STOP : STATE_RUN;
                }
                break;
            }

            case STATE_POSE:
            {
                if (mood != MOOD_SLEEP || rand_logic() < 256)
                    return STATE_STOP;
                break;
            }
        }

        return goalState;
    }
};


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
        if (health <= 0) {
            return (state == STATE_STOP) ? STATE_DEATH : STATE_STOP;
        }

        extraE->rotNeck = (extraE->rotHead >>= 1);

        if (hitMask)
        {
            tinfo.target->hit((state == STATE_RUN) ? DAMAGE_RUN : DAMAGE_WALK, _vec3i(0, 0, 0), 0);
        }

        bool walk = (tinfo.canAttack && (tinfo.dist > DIST_BITE) && (tinfo.dist < DIST_WALK)) ||
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
                if ((mood != MOOD_SLEEP) && !walk)
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
                    angle.x = 0;
                    angle.z = 0;

                    tinfo.target->pos = pos;
                    tinfo.target->angle = angle;
                    tinfo.target->flags &= ~ITEM_FLAG_GRAVITY;
                    tinfo.target->hit(DAMAGE_FATAL, _vec3i(0, 0, 0), 0);
                    tinfo.target->meshSwap(ITEM_LARA_SPEC, 0xFFFFFFFF);
                    tinfo.target->animSet(level.models[ITEM_LARA_SPEC].animIndex + 1, true);

                    if (tinfo.target->room != room)
                    {
                        tinfo.target->room->remove(tinfo.target);
                        room->add(tinfo.target);
                    }

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

        if (health <= 0) {
            animSet(level.models[type].animIndex + ANIM_DEATH_1 + RAND_LOGIC(2), true);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

        switch (state)
        {
            case STATE_STOP: {
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

            case STATE_WALK: {
                extraE->maxTurn = ENEMY_TURN_1;
                tinfo.tilt = tinfo.turn >> 1;

                if (mood != MOOD_SLEEP)
                    return STATE_STOP;

                if (rand_logic() < 0x100) {
                    nextState = STATE_ROAR;
                    return STATE_STOP;
                }
                break;
            }

            case STATE_RUN: {
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

                if (tinfo.front && (mood != MOOD_ESCAPE) && (rand_logic() < 0x100))
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
                if (!nextState && tinfo.front && (hitMask & HIT_MASK)) {
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
    }

    virtual int32 updateState()
    {
        extraE->maxTurn = 0;

        if (state == STATE_STOP && (health <= 0 || hitMask))
            return STATE_DEATH;

        return goalState;
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
        return trace(from, to, false);
    }

    // TR1's ShotLara: the nearer, the likelier a hit; a miss ricochets by her
    bool shoot(int32 damage, const vec3i &muzzle, int32 joint, int32 dist)
    {
        if (!tinfo.target || dist >= DIST_SHOOT)
            return false;

        const int32 range = X_SQR(DIST_SHOOT >> 4);
        bool hit = rand_logic() < (range - X_SQR(dist >> 4)) / (range / 0x7FFF) - 0x2000;

        if (hit) {
            bite(damage, muzzle, joint);
        } else {
            vec3i p = tinfo.target->pos;
            p.x += (rand_logic() - 0x4000) >> 6;
            p.y -= 512 + (rand_logic() >> 5);
            p.z += (rand_logic() - 0x4000) >> 6;
            fxRicochet(tinfo.target->room, p, true);
        }
        return hit;
    }

    void deathAnim(int32 anim)
    {
        if (health <= 0) {
            animSet(level.models[type].animIndex + anim, true);
        }
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

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);
        deathAnim(ANIM_DEATH);
    }

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
                else if (!tinfo.front || tinfo.dist > DIST_WALK)
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
                        shoot(damage >> 1, _vec3i(60, 200, 0), 11, tinfo.dist);
                        shoot(damage >> 1, _vec3i(-57, 200, 0), 14, tinfo.dist);
                    } else {
                        shoot(damage, _vec3i(-60, 170, 0), 14, tinfo.dist);
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
            return goalState;
        return updatePerson(DAMAGE, false);
    }
};


// Pierre can't be killed until the level means him to be (his trigger has
// spent itself): until then he runs off at 40 health, and vanishes once out
// of the camera's sight for a while.
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
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);

        if (!(flags & ITEM_FLAG_ONCE) && health <= HEALTH_RUN) {
            health = HEALTH_RUN;
            turnSpeed++;
        }
        deathAnim(ANIM_DEATH);
        if (health <= 0) {
            drop(ITEM_SCION_PICKUP_DROP);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

        if (turnSpeed) {
            mood = MOOD_ESCAPE;
        }

        int32 goal = updatePerson(DAMAGE, true);

        if (turnSpeed)
        {
            Location from = playersExtra[0].camera.view;
            Location to;
            to.room = room;
            to.pos = _vec3i(pos.x, pos.y - 1024, pos.z);

            if (trace(from, to, false)) {
                turnSpeed = 1;
            } else if (++turnSpeed > DISAPPEAR) {
                health = NOT_ENEMY;
                flags &= ~ITEM_FLAG_STATUS;
                flags |= ITEM_FLAG_STATUS_INVISIBLE;
                disable();
                deactivate();
            }
        }

        return goal;
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
        deathAnim(ANIM_DEATH);
        if (health <= 0) {
            drop(ITEM_UZIS);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

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
                    if (tinfo.dist > DIST_DONT_STOP && tinfo.dist < DIST_STOP && mood != MOOD_ESCAPE)
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
                    shoot(damage, _vec3i(0, 150, 34), 7, tinfo.dist);
                    shoot(damage, _vec3i(0, 150, 37), 4, tinfo.dist);
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
        deathAnim(ANIM_DEATH);
        if (health <= 0) {
            drop(ITEM_MAGNUMS);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

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
                else if (tinfo.dist > DIST_WALK)
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
                    shoot(DAMAGE, _vec3i(1, 200, 41), 5, tinfo.dist);
                } else if (turnSpeed == 6 && canShoot()) {
                    shoot(DAMAGE, _vec3i(-2, 200, 40), 8, tinfo.dist);
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
        deathAnim(ANIM_DEATH);
        if (health <= 0) {
            drop(ITEM_SHOTGUN);
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

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
                else if (tinfo.dist > DIST_WALK)
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
                    shoot(DAMAGE, _vec3i(-20, 440, 20), 9, tinfo.dist >> 1);
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


// Atlantis' mutants: they claw at Lara close up and, the shooter and the
// winged one, fire on her from afar (TR1's mutant control, on the ground:
// the winged one doesn't take to the air here, and their shards and bombs
// strike at once rather than fly). They burst when killed.
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
        DAMAGE_PUNCH = 200,
        DAMAGE_SHOT = 100
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
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);

        if (health <= 0)
        {
            soundPlay(SND_MUTANT_DEATH, &pos);
            fxBlood(pos, angle.y, 0);
            health = NOT_ENEMY;
            flags &= ~ITEM_FLAG_STATUS;
            flags |= ITEM_FLAG_STATUS_INVISIBLE;
            disable();
            deactivate();
        }
    }

    virtual int32 updateState()
    {
        if (health <= 0)
            return goalState;

        bool canFire = (type != ITEM_MUTANT_3) && canShoot() && (!checkZone() || tinfo.dist > DIST_SHOOT) &&
                     abs(tinfo.angle) < ANGLE_45;

        switch (state)
        {
            case STATE_MUMMY:
            case STATE_FLY:
                return STATE_STOP;

            case STATE_STOP:
            {
                if (hitMask & HIT_MASK)
                    return STATE_ATTACK_3;
                if (tinfo.canAttack && tinfo.dist < DIST_ATTACK_3)
                    return STATE_ATTACK_3;
                if (tinfo.canAttack && tinfo.dist < DIST_ATTACK_1)
                    return STATE_ATTACK_1;
                if (canFire)
                    return (tinfo.angle > 0) ? STATE_AIM_1 : STATE_AIM_2;
                if (mood == MOOD_SLEEP || (mood == MOOD_STALK && tinfo.dist < DIST_WALK))
                    return STATE_POSE;
                return STATE_RUN;
            }

            case STATE_POSE:
            {
                if (canFire)
                    return STATE_STOP;
                if (mood == MOOD_STALK)
                {
                    if (tinfo.dist >= DIST_WALK)
                        return STATE_STOP;
                    if (checkZone() || rand_logic() < 256)
                        return STATE_WALK;
                }
                else if (mood == MOOD_SLEEP)
                {
                    if (rand_logic() < 256)
                        return STATE_WALK;
                }
                else
                {
                    return STATE_STOP;
                }
                break;
            }

            case STATE_WALK:
            {
                extraE->maxTurn = ENEMY_TURN_2;

                if (canFire || mood == MOOD_ATTACK || mood == MOOD_ESCAPE)
                    return STATE_STOP;
                if (mood == MOOD_SLEEP || (mood == MOOD_STALK && !checkZone()))
                {
                    if (rand_logic() < 80)
                        return STATE_POSE;
                }
                else if (mood == MOOD_STALK && tinfo.dist > DIST_WALK)
                {
                    return STATE_STOP;
                }
                break;
            }

            case STATE_RUN:
            {
                extraE->maxTurn = ANGLE(6);

                if (hitMask & HIT_MASK)
                    return STATE_STOP;
                if (tinfo.canAttack && tinfo.dist < DIST_ATTACK_1)
                    return STATE_STOP;
                if (tinfo.front && tinfo.dist < DIST_ATTACK_2)
                    return STATE_ATTACK_2;
                if (canFire)
                    return STATE_STOP;
                if (mood == MOOD_SLEEP || (mood == MOOD_STALK && tinfo.dist < DIST_WALK))
                    return STATE_STOP;
                break;
            }

            case STATE_ATTACK_1:
            case STATE_ATTACK_2:
            case STATE_ATTACK_3:
            {
                if (!nextState && (hitMask & HIT_MASK))
                {
                    int32 damage = (state == STATE_ATTACK_1) ? DAMAGE_LUNGE :
                                   (state == STATE_ATTACK_2) ? DAMAGE_CHARGE : DAMAGE_PUNCH;
                    bite(damage, _vec3i(-27, 98, 0), 10);
                    nextState = (state == STATE_ATTACK_2) ? STATE_RUN : STATE_STOP;
                }
                break;
            }

            case STATE_AIM_1:
            case STATE_AIM_2:
            {
                turnSpeed = (state == STATE_AIM_1) ? 1 : 2;
                return canFire ? STATE_SHOOT : STATE_STOP;
            }

            case STATE_SHOOT:
            {
                if (turnSpeed == 1) {
                    shoot(DAMAGE_SHOT, _vec3i(-35, 269, 0), 9, tinfo.dist);
                } else if (turnSpeed == 2) {
                    shoot(DAMAGE_SHOT, _vec3i(51, 213, 0), 14, tinfo.dist);
                }
                turnSpeed = 0;
                break;
            }
        }

        return goalState;
    }
};


// Centaurs rear up and kick close by, and fire on Lara from afar (TR1's
// CentaurControl; the bomb strikes at once rather than flying). They burst
// when their death is done.
struct Centaur : Gunman
{
    enum {
        HIT_MASK = 0x30199,
        DIST_REAR = 1536,
        DAMAGE_REAR = 200,
        DAMAGE_SHOT = 100,
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

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        Enemy::hit(damage, point, soundId);
        deathAnim(ANIM_DEATH);
    }

    virtual int32 updateState()
    {
        if (health <= 0)
        {
            if (animIsEnd(0))
            {
                soundPlay(SND_MUTANT_DEATH, &pos);
                flags &= ~ITEM_FLAG_STATUS;
                flags |= ITEM_FLAG_STATUS_INVISIBLE;
            }
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
                    shoot(DAMAGE_SHOT, _vec3i(11, 415, 41), 13, tinfo.dist);
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


// Natla, in two stages (TR1's NatlaControl, on the ground: she doesn't fly
// here, and her bombs and shards strike at once). Brought down to half her
// health she falls and lies still for sixteen seconds, then rises with that
// half and runs at Lara firing. turnSpeed is her shot timer, aggression marks
// the second stage.
struct Natla : Gunman
{
    enum {
        HEALTH_STAGE_2 = 200,
        DIE_TIME = 30 * 16,
        DAMAGE_BOMB = 100,
        DAMAGE_SHARD = 100
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

    Natla(Room* room) : Gunman(room, 400, 204)
    {
        aggression = 0;
    }

    virtual void hit(int32 damage, const vec3i &point, int32 soundId)
    {
        if (state == STATE_SEMIDEATH)
            return;

        health -= damage;
        fxBlood(point, 0, 0);

        if (!aggression && health <= HEALTH_STAGE_2) {
            health = 1; // she plays dead before the second stage
        } else if (health <= 0) {
            gSaveGame.kills++;
        }
    }

    virtual int32 updateState()
    {
        turnSpeed++;

        if (health <= 0 && state != STATE_SEMIDEATH)
            return STATE_DEATH;

        bool canFire = abs(tinfo.angle) < ANGLE(30) && canShoot();

        if (aggression || health <= 1)
        {
            extraE->maxTurn = ANGLE(6);

            switch (state)
            {
                case STATE_FALL:
                {
                    if (pos.y >= roomFloor)
                    {
                        flags &= ~ITEM_FLAG_GRAVITY;
                        pos.y = roomFloor;
                        turnSpeed = 0;
                        return STATE_SEMIDEATH;
                    }
                    flags |= ITEM_FLAG_GRAVITY;
                    break;
                }

                case STATE_STAND:
                case STATE_RUN:
                {
                    if (state == STATE_RUN) {
                        tinfo.tilt = tinfo.turn;
                    }
                    if (turnSpeed >= 20)
                    {
                        shoot(DAMAGE_SHARD, _vec3i(5, 220, 7), 4, tinfo.dist);
                        turnSpeed = 0;
                    }
                    if (state == STATE_STAND && !canFire)
                        return STATE_RUN;
                    if (state == STATE_RUN && canFire)
                        return STATE_STAND;
                    break;
                }

                case STATE_SEMIDEATH:
                {
                    extraE->maxTurn = 0;
                    if (turnSpeed >= DIE_TIME)
                    {
                        health = HEALTH_STAGE_2;
                        aggression = 1;
                        turnSpeed = 0;
                        return STATE_STAND;
                    }
                    break;
                }

                case STATE_FLY:
                    turnSpeed = 0;
                    return STATE_FALL;

                case STATE_STOP:
                case STATE_AIM:
                case STATE_SHOOT:
                    turnSpeed = 0;
                    return STATE_SEMIDEATH;
            }
            return goalState;
        }

        extraE->maxTurn = ANGLE(5);

        switch (state)
        {
            case STATE_STOP:
                turnSpeed = 0;
                return STATE_AIM;

            case STATE_FLY:
                return STATE_STOP;

            case STATE_AIM:
            {
                if (nextState)
                    return nextState;
                return canFire ? STATE_SHOOT : STATE_STOP;
            }

            case STATE_SHOOT:
            {
                if (!nextState)
                {
                    for (int32 i = 0; i < 3; i++) {
                        shoot(DAMAGE_BOMB, _vec3i(5, 220, 7), 4, tinfo.dist + i * 1024);
                    }
                    nextState = STATE_STOP;
                }
                break;
            }
        }

        return goalState;
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

        if (health <= 0) {
            animSet(level.models[type].animIndex + ANIM_DEATH, true);
        }
    }

    virtual int32 updateState()
    {
        extraE->maxTurn = 0;

        if (health <= 0)
        {
            if (animIsEnd(0) && (flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE)
            {
                soundPlay(SND_MUTANT_DEATH, &pos);
                room->getSector(pos.x, pos.z)->getFloor(pos.x, pos.y, pos.z);
                checkTrigger(gLastFloorData, NULL);
                flags &= ~ITEM_FLAG_STATUS;
                flags |= ITEM_FLAG_STATUS_INVISIBLE;
            }
            return goalState;
        }

        Lara* lara = (Lara*)tinfo.target;
        int16 turn = tinfo.angle;

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
                    shakeCamera(15, false);
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
                angle.y += X_CLAMP(turn, -ANGLE(3), ANGLE(3));
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
                if ((hitMask & HIT_MASK_RIGHT) || lara->health <= 0)
                {
                    lara->hit(LARA_MAX_HEALTH, pos, 0);
                    return STATE_KILL;
                }
                break;
            }
        }

        return goalState;
    }
};

#endif
