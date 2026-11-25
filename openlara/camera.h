#ifndef H_CAMERA
#define H_CAMERA

#include "common.h"

#define CAM_DIST_FOLLOW         1536
#define CAM_ANGLE_MAX           ANGLE(85)

void Camera::initCinematic()
{
    switch (gLevelID)
    {
        case LVL_TR1_CUT_1:
            gCinematicCamera.view.pos.x = 36668;
            gCinematicCamera.view.pos.z = 63180;
            gCinematicCamera.targetAngle.y = -ANGLE(128);
            break;
        case LVL_TR1_CUT_2:
            gCinematicCamera.view.pos.x = 51962;
            gCinematicCamera.view.pos.z = 53760;
            gCinematicCamera.targetAngle.y = ANGLE_90 - 4;
            break;
        case LVL_TR1_CUT_3:
            gCinematicCamera.targetAngle.y = ANGLE_90;
            flipMap();      // retail's cutscene start (0600d574), its case 0x12
            break;
        case LVL_TR1_CUT_4:
            gCinematicCamera.targetAngle.y = ANGLE_90;
            break;
        default:
            ASSERT(false);
            break;
    }
}

void Camera::updateCinematic()
{
    const CameraFrame &frame = level.cameraFrames[timer++];

    int32 px = frame.pos.x;
    int32 py = frame.pos.y;
    int32 pz = frame.pos.z;

    int32 dx = frame.target.x - px;
    int32 dy = frame.target.y - py;
    int32 dz = frame.target.z - pz;

    anglesFromVector(dx, dy, dz, angle.x, angle.y);

    int32 s, c;
    sincos(targetAngle.y, s, c);

    X_ROTXY(pz, px, s, c);

    px += view.pos.x;
    py += view.pos.y;
    pz += view.pos.z;

    cameraViewFov(_vec3i(px, py, pz), angle.x, angle.y + targetAngle.y, frame.fov);

    Room* nextRoom = view.room->getRoom(px, py, pz);
    if (nextRoom) {
        view.room = nextRoom;
    }

    // its end, or START (retail's DoCinematic, 0600d744): the pad's START is IK_SELECT (saturn/main.cpp)
    static uint32 lastKeys = IK_SELECT;
    bool skip = (keys & IK_SELECT) && !(lastKeys & IK_SELECT);
    lastKeys = keys;

    if (timer >= level.cameraFramesCount || skip) {
        timer = level.cameraFramesCount - 1;
        nextLevel(LevelID(gLevelID + 1));
    }
}

// In-level cinematics (the scions, the Midas hand) play the level's camera
// frames around a point, as TR1's CAM_CINEMATIC does.
void Camera::doCutscene(const vec3i &pos, int16 angleY)
{
    if (!level.cameraFramesCount)
        return;

    mode = CAMERA_MODE_CUTSCENE;
    cutPos = pos;
    cutAngle = angleY;
    timer = 0;
}

// Retail's InGameCinematicCamera (0600daa4): the next frame (the first never
// shown), held at the last; it ends only when Lara's animation hands her the
// chase camera back (FX_LARA_NORMAL). The target, which the sounds are heard
// from, goes where the frame looks.
void Camera::updateCutscene()
{
    if (++timer >= level.cameraFramesCount) {
        timer = level.cameraFramesCount - 1;
    }

    const CameraFrame &frame = level.cameraFrames[timer];

    int32 s, c;
    sincos(cutAngle, s, c);

    vec3i eye, at;
    eye.x = cutPos.x + ((frame.pos.x * c + frame.pos.z * s) >> FIXED_SHIFT);
    eye.y = cutPos.y + frame.pos.y;
    eye.z = cutPos.z + ((frame.pos.z * c - frame.pos.x * s) >> FIXED_SHIFT);
    at.x = cutPos.x + ((frame.target.x * c + frame.target.z * s) >> FIXED_SHIFT);
    at.y = cutPos.y + frame.target.y;
    at.z = cutPos.z + ((frame.target.z * c - frame.target.x * s) >> FIXED_SHIFT);

    vec3i dir = at - eye;
    anglesFromVector(dir.x, dir.y, dir.z, angle.x, angle.y);

    target.pos = at;
    view.pos = eye;
    Room* nextRoom = view.room->getRoom(eye.x, eye.y, eye.z);
    if (nextRoom) {
        view.room = nextRoom;
    }

    cameraViewFov(eye, angle.x, angle.y, frame.fov);
}

void Camera::init(ItemObj* lara)
{
    ASSERT(lara->extraL);

    target.pos = lara->pos;
    target.pos.y -= 1024;
    target.room = lara->room;

    view = target;
    view.pos.z -= 100;

    targetDist = CAM_DIST_FOLLOW;
    targetAngle = _vec3s(0, 0, 0);
    angle = _vec3s(0, 0, 0);

    laraItem = lara;
    lastItem = NULL;
    lookAtItem = NULL;

    speed = 1;
    timer = 0;
    index = -1;
    lastIndex = -1;
    bounce = 0;
    shift = 0;

    mode = CAMERA_MODE_FOLLOW;

    lastFixed = false;
    flags = 0;
}

bool checkWall(Room* room, int32 x, int32 y, int32 z)
{
    Room* nextRoom = room->getRoom(x, y, z);

    const Sector* sector = nextRoom->getSector(x, z);
    int32 floor = sector->getFloor(x, y, z);
    int32 ceiling = sector->getCeiling(x, y, z);
    return (floor == WALL || ceiling == WALL || ceiling >= floor || y > floor || y < ceiling);
}

// A trigger's target looked at (CalculateCamera 0600d128): half the way
// round to it, within 50 degrees of her yaw and 85 of level, the head 4
// degrees a tick (it still eases back an eighth a tick, lara.h
// updateLookBack, so a far turn settles near 32). Out of reach she just
// doesn't look; the target stays hers, to look at again turned back.
void Camera::lookAt(int32 offset)
{
    int32 dx = lookAtItem->pos.x - laraItem->pos.x;
    int32 dz = lookAtItem->pos.z - laraItem->pos.z;

    int16 ay = int16(phd_atan(dz, dx) - laraItem->angle.y) >> 1;
    if (ay < -0x238B || ay > 0x238B)
        return;

    const AABBs& box = lookAtItem->getBoundingBox(true);

    offset -= lookAtItem->pos.y + ((box.minY + box.maxY) >> 1);

    int16 ax = int16(phd_atan(phd_sqrt(X_SQR(dx) + X_SQR(dz)), offset)) >> 1;
    if (ax < -0x3C6D || ax > 0x3C6D)
        return;

    laraItem->extraL->head.angle.x = angleLerp(laraItem->extraL->head.angle.x, ax, 0x2D8);
    laraItem->extraL->head.angle.y = angleLerp(laraItem->extraL->head.angle.y, ay, 0x2D8);

    laraItem->extraL->torso.angle = laraItem->extraL->head.angle;

    lookAtItem->flags |= ITEM_FLAG_LOOKED_AT;
    mode = CAMERA_MODE_LOOK;
}

// --- the camera ------------------------------------------------------------
// CalculateCamera and what it calls (retail 0600c054 .. 0600d128), with its
// field of view.

#define CHASE_SPEED         12
#define COMBAT_SPEED        8
#define LOOK_SPEED          4
#define COMBAT_DISTANCE     2560
#define GROUND_SHIFT        256
#define MIN_SQUARE          (256 * 256)
#define FOV_SCALE           13424   // 209.75 / 256, 2^14: the view's x and y rows make the 256 px focal length retail's

static int32 targetSquare;  // the chase's distance on the ground, squared

// The view, its x and y rows scaled (2^14) to a focal length of scale / 64 px
static void viewScaled(const vec3i &pos, int32 angleX, int32 angleY, int32 scale)
{
    matrixSetView(pos, angleX, angleY);
    Matrix &m = matrixGet();
    m.e00 = m.e00 * scale >> 14;
    m.e01 = m.e01 * scale >> 14;
    m.e02 = m.e02 * scale >> 14;
    m.e10 = m.e10 * scale >> 14;
    m.e11 = m.e11 * scale >> 14;
    m.e12 = m.e12 * scale >> 14;
    gFocalScale = scale;
    for (int32 i = 0; i < 6; i++) {
        gViewRotation[i] = int16(gViewRotation[i] * scale >> 14);
    }
}

void cameraView(const vec3i &pos, int32 angleX, int32 angleY)
{
    viewScaled(pos, angleX, angleY, FOV_SCALE);
}

// A cutscene frame's view with its own field of view (retail's
// phd_AlterFOV, every frame of CalculateCinematicCamera 0600d850 and
// InGameCinematicCamera 0600daa4): half the screen's width over tan(fov / 2).
// Its roll, a quarter of a degree at most, is left out.
void cameraViewFov(const vec3i &pos, int32 angleX, int32 angleY, int32 fov)
{
    int32 s, c;
    sincos(fov >> 1, s, c);
    viewScaled(pos, angleX, angleY, (FRAME_WIDTH / 2) * 64 * c / X_MAX(s, 1));
}

static bool badPosition(Room* room, int32 x, int32 y, int32 z)
{
    const Sector* sector = room->getRoom(x, y, z)->getSector(x, z);
    return y >= sector->getFloor(x, y, z) || y <= sector->getCeiling(x, y, z);
}

static void clipCamera(int32* x, int32* y, int32 targetSquare, int32 tx, int32 ty, int32 left, int32 top, int32 right, int32 bottom)
{
    if ((right > left) != (tx < left))
    {
        *y = ty + (*y - ty) * (left - tx) / (*x - tx);
        *x = left;
    }

    if ((bottom > top && ty > top && *y < top) || (bottom < top && ty < top && *y > top))
    {
        *x = tx + (*x - tx) * (top - ty) / (*y - ty);
        *y = top;
    }
}

static void shiftCamera(int32* x, int32* y, int32 targetSquare, int32 tx, int32 ty, int32 left, int32 top, int32 right, int32 bottom)
{
    int32 shift;

    int32 tlSquare = X_SQR(tx - left) + X_SQR(ty - top);
    int32 blSquare = X_SQR(tx - left) + X_SQR(ty - bottom);
    int32 trSquare = X_SQR(tx - right) + X_SQR(ty - top);

    if (targetSquare < tlSquare)
    {
        *x = left;
        shift = targetSquare - X_SQR(tx - left);
        if (shift < 0)
            return;
        shift = phd_sqrt(shift);
        *y = ty + ((top < bottom) ? -shift : shift);
    }
    else if (tlSquare > MIN_SQUARE)
    {
        *x = left;
        *y = top;
    }
    else if (targetSquare < blSquare)
    {
        *x = left;
        shift = targetSquare - X_SQR(tx - left);
        if (shift < 0)
            return;
        shift = phd_sqrt(shift);
        *y = ty + ((top < bottom) ? shift : -shift);
    }
    else if (blSquare > MIN_SQUARE)
    {
        *x = left;
        *y = bottom;
    }
    else if (targetSquare < trSquare)
    {
        shift = targetSquare - X_SQR(ty - top);
        if (shift < 0)
            return;
        shift = phd_sqrt(shift);
        *x = tx + ((left < right) ? shift : -shift);
        *y = top;
    }
    else
    {
        *x = right;
        *y = top;
    }
}

// The box index of a room's sector by its row and column, as the original
// reads r->floor[], but in the room.
static uint16 sectorBox(const Room* room, int32 sz, int32 sx)
{
    if (sx < 0 || sz < 0 || sx >= room->info->xSectors || sz >= room->info->zSectors)
        return NO_BOX;
    return room->sectors[sx * room->info->zSectors + sz].boxIndex;
}

// SmartShift (0600c50c): the ideal spot's line of sight from the target, then
// kept within the target's box and those beside it the camera could stand in.
void Camera::smartShift(Location &ideal, bool look)
{
    trace(target, ideal);

    void (*shift)(int32*, int32*, int32, int32, int32, int32, int32, int32, int32) = look ? clipCamera : shiftCamera;

    const Room* r = target.room;
    uint16 itemBox = sectorBox(r, (target.pos.z - (r->info->z << 8)) >> 10, (target.pos.x - (r->info->x << 8)) >> 10);
    if (itemBox == NO_BOX)
        return;
    const Box* box = level.boxes + itemBox;

    r = ideal.room;
    int32 sz = (ideal.pos.z - (r->info->z << 8)) >> 10;
    int32 sx = (ideal.pos.x - (r->info->x << 8)) >> 10;

    uint16 cameraBox = sectorBox(r, sz, sx);
    if (cameraBox != NO_BOX && (ideal.pos.z < (box->minZ << 10) || ideal.pos.z > (box->maxZ << 10) - 1 ||
                                ideal.pos.x < (box->minX << 10) || ideal.pos.x > (box->maxX << 10) - 1)) {
        box = level.boxes + cameraBox;
    }

    int32 left = box->minZ << 10;
    int32 right = (box->maxZ << 10) - 1;
    int32 top = box->minX << 10;
    int32 bottom = (box->maxX << 10) - 1;

    int32 test = (ideal.pos.z - 1024) | 1023;
    bool badLeft = badPosition(ideal.room, ideal.pos.x, ideal.pos.y, test);
    if (!badLeft)
    {
        cameraBox = sectorBox(r, sz - 1, sx);
        if (cameraBox != NO_BOX && (level.boxes[cameraBox].minZ << 10) < left) {
            left = level.boxes[cameraBox].minZ << 10;
        }
    }

    test = (ideal.pos.z + 1024) & ~1023;
    bool badRight = badPosition(ideal.room, ideal.pos.x, ideal.pos.y, test);
    if (!badRight)
    {
        cameraBox = sectorBox(r, sz + 1, sx);
        if (cameraBox != NO_BOX && (level.boxes[cameraBox].maxZ << 10) - 1 > right) {
            right = (level.boxes[cameraBox].maxZ << 10) - 1;
        }
    }

    test = (ideal.pos.x - 1024) | 1023;
    bool badTop = badPosition(ideal.room, test, ideal.pos.y, ideal.pos.z);
    if (!badTop)
    {
        cameraBox = sectorBox(r, sz, sx - 1);
        if (cameraBox != NO_BOX && (level.boxes[cameraBox].minX << 10) < top) {
            top = level.boxes[cameraBox].minX << 10;
        }
    }

    test = (ideal.pos.x + 1024) & ~1023;
    bool badBottom = badPosition(ideal.room, test, ideal.pos.y, ideal.pos.z);
    if (!badBottom)
    {
        cameraBox = sectorBox(r, sz, sx + 1);
        if (cameraBox != NO_BOX && (level.boxes[cameraBox].maxX << 10) - 1 > bottom) {
            bottom = (level.boxes[cameraBox].maxX << 10) - 1;
        }
    }

    left += 256;
    right -= 256;
    top += 256;
    bottom -= 256;

    bool noclip = true;
    int32 &iz = ideal.pos.z, &ix = ideal.pos.x;
    if (iz < left && badLeft)
    {
        noclip = false;
        if (ix < target.pos.x) {
            shift(&iz, &ix, targetSquare, target.pos.z, target.pos.x, left, top, right, bottom);
        } else {
            shift(&iz, &ix, targetSquare, target.pos.z, target.pos.x, left, bottom, right, top);
        }
    }
    else if (iz > right && badRight)
    {
        noclip = false;
        if (ix < target.pos.x) {
            shift(&iz, &ix, targetSquare, target.pos.z, target.pos.x, right, top, left, bottom);
        } else {
            shift(&iz, &ix, targetSquare, target.pos.z, target.pos.x, right, bottom, left, top);
        }
    }

    if (noclip)
    {
        if (ix < top && badTop)
        {
            noclip = false;
            if (iz < target.pos.z) {
                shift(&ix, &iz, targetSquare, target.pos.x, target.pos.z, top, left, bottom, right);
            } else {
                shift(&ix, &iz, targetSquare, target.pos.x, target.pos.z, top, right, bottom, left);
            }
        }
        else if (ix > bottom && badBottom)
        {
            noclip = false;
            if (iz < target.pos.z) {
                shift(&ix, &iz, targetSquare, target.pos.x, target.pos.z, bottom, left, top, right);
            } else {
                shift(&ix, &iz, targetSquare, target.pos.x, target.pos.z, bottom, right, top, left);
            }
        }
    }

    if (!noclip) {
        ideal.room = ideal.room->getRoom(ideal.pos.x, ideal.pos.y, ideal.pos.z);
    }
}

// MoveCamera (0600c054): a speedth of the way there, out of the floor and
// ceiling by a shift of the height it looks from.
// Retail's bounce (MoveCamera, 0600c054): below 0, each tick a random jolt of
// up to half of it on each axis, to the camera and its target alike (retail
// gives the target y the x jolt too), easing by 5; above 0, both drop by it
// for one tick.
static void bounceView(vec3i &pos, vec3i &target, int32 &bounce)
{
    if (bounce > 0)
    {
        pos.y += bounce;
        target.y += bounce;
        bounce = 0;
    }
    else if (bounce < 0)
    {
        int32 d = (rand_logic() - 0x4000) * bounce / 0x7FFF;
        pos.x += d;
        target.y += d;
        d = (rand_logic() - 0x4000) * bounce / 0x7FFF;
        pos.y += d;
        target.y += d;
        d = (rand_logic() - 0x4000) * bounce / 0x7FFF;
        pos.z += d;
        target.z += d;
        bounce += 5;
    }
}

void Camera::moveRetail(Location &ideal, int32 speed)
{
    gChunky = false;

    view.pos.x += (ideal.pos.x - view.pos.x) / speed;
    view.pos.z += (ideal.pos.z - view.pos.z) / speed;
    view.pos.y += (ideal.pos.y - view.pos.y) / speed;
    view.room = ideal.room->getRoom(view.pos.x, view.pos.y, view.pos.z);

    const Sector* sector = view.room->getSector(view.pos.x, view.pos.z);
    int32 height = sector->getFloor(view.pos.x, view.pos.y, view.pos.z) - GROUND_SHIFT;

    if (view.pos.y >= height && ideal.pos.y >= height)
    {
        trace(target, view);
        view.room = view.room->getRoom(view.pos.x, view.pos.y, view.pos.z);
        sector = view.room->getSector(view.pos.x, view.pos.z);
        height = sector->getFloor(view.pos.x, view.pos.y, view.pos.z) - GROUND_SHIFT;
    }

    int32 ceiling = sector->getCeiling(view.pos.x, view.pos.y, view.pos.z) + GROUND_SHIFT;

    bounceView(view.pos, target.pos, bounce);

    shift = 0;
    if (height != WALL - GROUND_SHIFT && ceiling != WALL + GROUND_SHIFT)
    {
        if (height < ceiling) {
            ceiling = (height + ceiling) >> 1;
            height = ceiling;
        }

        if (view.pos.y > height) {
            shift = height - view.pos.y;
        } else if (view.pos.y < ceiling) {
            shift = ceiling - view.pos.y;
        }
    }

    view.room = view.room->getRoom(view.pos.x, view.pos.y + shift, view.pos.z);
}

// ShiftClamp (0600cb20): a place kept clamp off walls of its box; the height
// to shift it by off the floor and ceiling.
static int32 shiftClamp(Location &loc, int32 clamp)
{
    int32 x = loc.pos.x, y = loc.pos.y, z = loc.pos.z;

    loc.room = loc.room->getRoom(x, y, z);
    const Sector* sector = loc.room->getSector(x, z);

    if (sector->boxIndex != NO_BOX)
    {
        const Box &box = level.boxes[sector->boxIndex];
        if (z < (box.minZ << 10) + clamp && badPosition(loc.room, x, y, z - clamp)) {
            loc.pos.z = (box.minZ << 10) + clamp;
        } else if (z > (box.maxZ << 10) - 1 - clamp && badPosition(loc.room, x, y, z + clamp)) {
            loc.pos.z = (box.maxZ << 10) - 1 - clamp;
        }

        if (x < (box.minX << 10) + clamp && badPosition(loc.room, x - clamp, y, z)) {
            loc.pos.x = (box.minX << 10) + clamp;
        } else if (x > (box.maxX << 10) - 1 - clamp && badPosition(loc.room, x + clamp, y, z)) {
            loc.pos.x = (box.maxX << 10) - 1 - clamp;
        }
    }

    int32 height = sector->getFloor(x, y, z) - clamp;
    int32 ceiling = sector->getCeiling(x, y, z) + clamp;

    if (height < ceiling) {
        ceiling = (height + ceiling) >> 1;
        height = ceiling;
    }

    if (y > height)
        return height - y;
    if (loc.pos.y < ceiling)
        return ceiling - y;
    return 0;
}

// ChaseCamera (0600ca08)
void Camera::chaseRetail(ItemObj* item)
{
    targetAngle.x = X_CLAMP(targetAngle.x + item->angle.x, -CAM_ANGLE_MAX, CAM_ANGLE_MAX);

    int32 s, c;
    sincos(targetAngle.x, s, c);
    int32 distance = targetDist * c >> FIXED_SHIFT;

    Location ideal;
    ideal.pos.y = target.pos.y + (targetDist * s >> FIXED_SHIFT);

    targetSquare = X_SQR(distance);

    sincos(int16(item->angle.y + targetAngle.y), s, c);
    ideal.pos.x = target.pos.x - (distance * s >> FIXED_SHIFT);
    ideal.pos.z = target.pos.z - (distance * c >> FIXED_SHIFT);
    ideal.room = view.room;

    smartShift(ideal, false);
    moveRetail(ideal, lastFixed ? speed : CHASE_SPEED);
}

// CombatCamera (0600cd34)
void Camera::combatRetail(ItemObj* item)
{
    target.pos.x = item->pos.x;
    target.pos.z = item->pos.z;

    targetAngle.x = item->angle.x;
    targetAngle.y = item->angle.y;

    ExtraInfoLara* extra = item->extraL;
    if (extra->armR.target || extra->armL.target)
    {
        int32 aX = extra->armR.angleAim.x + extra->armL.angleAim.x;
        int32 aY = extra->armR.angleAim.y + extra->armL.angleAim.y;
        if (extra->armR.target && extra->armL.target) {
            targetAngle.x += aX >> 1;
            targetAngle.y += aY >> 1;
        } else {
            targetAngle.x += aX;
            targetAngle.y += aY;
        }
    }
    else
    {
        targetAngle.x += extra->head.angle.x + extra->torso.angle.x;
        targetAngle.y += extra->head.angle.y + extra->torso.angle.y;
    }

    targetDist = COMBAT_DISTANCE;

    int32 s, c;
    sincos(targetAngle.x, s, c);
    int32 distance = targetDist * c >> FIXED_SHIFT;

    Location ideal;
    ideal.pos.y = target.pos.y + (targetDist * s >> FIXED_SHIFT);

    // targetSquare stays the last chase camera's, as retail's and TRX's
    // (issue 11): the shift along a wall keeps that distance

    sincos(targetAngle.y, s, c);
    ideal.pos.x = target.pos.x - (distance * s >> FIXED_SHIFT);
    ideal.pos.z = target.pos.z - (distance * c >> FIXED_SHIFT);
    ideal.room = view.room;

    smartShift(ideal, false);
    moveRetail(ideal, speed);
}

// LookCamera (0600ce74)
void Camera::lookRetail(ItemObj* item)
{
    vec3i old = target.pos;

    target.pos.x = item->pos.x;
    target.pos.z = item->pos.z;

    ExtraInfoLara* extra = item->extraL;
    targetAngle.y = item->angle.y + extra->torso.angle.y + extra->head.angle.y;
    targetAngle.x = item->angle.x + extra->torso.angle.x + extra->head.angle.x;
    targetDist = CAM_DIST_FOLLOW;

    int32 s, c;
    sincos(targetAngle.x, s, c);
    int32 distance = targetDist * c >> FIXED_SHIFT;
    int32 sinElevation = s;

    int32 sy, cy;
    sincos(item->angle.y, sy, cy);
    shift = -512 * sinElevation >> FIXED_SHIFT;
    target.pos.z += shift * cy >> FIXED_SHIFT;
    target.pos.x += shift * sy >> FIXED_SHIFT;

    if (badPosition(target.room, target.pos.x, target.pos.y, target.pos.z))
    {
        target.pos.x = item->pos.x;
        target.pos.z = item->pos.z;
    }

    target.pos.y += shiftClamp(target, 256 + 50);

    Location ideal;
    ideal.pos.y = target.pos.y + (targetDist * sinElevation >> FIXED_SHIFT);
    sincos(targetAngle.y, s, c);
    ideal.pos.x = target.pos.x - (distance * s >> FIXED_SHIFT);
    ideal.pos.z = target.pos.z - (distance * c >> FIXED_SHIFT);
    ideal.room = view.room;

    smartShift(ideal, true);

    target.pos.z = old.z + (target.pos.z - old.z) / speed;
    target.pos.x = old.x + (target.pos.x - old.x) / speed;

    moveRetail(ideal, speed);
}

// FixedCamera (0600d090)
void Camera::fixedRetail()
{
    const FixedCamera* cam = level.cameras + index;

    Location ideal;
    ideal.pos = cam->pos;
    ideal.room = rooms + cam->roomIndex;

    if (!trace(target, ideal)) {
        shiftClamp(ideal, 256);
    }

    lastFixed = true;
    moveRetail(ideal, speed);

    if (timer != 0)
    {
        timer--;
        if (timer == 0) {
            timer = -1;
        }
    }
}

// CalculateCamera (0600d128), past the underwater sound, a cutscene and our
// free camera.
void Camera::updateRetail()
{
    // the camera looks for its place with steep slopes flat (but in a
    // slide), till it moves (retail's ChunkyFlag)
    if (flags != CAM_NO_CHUNKY) {
        gChunky = true;
    }

    bool isFixed = lookAtItem && (mode == CAMERA_MODE_FIXED || mode == CAMERA_MODE_OBJECT);
    ItemObj* item = isFixed ? lookAtItem : laraItem;

    const AABBs &box = item->getBoundingBox(true);

    int32 y = item->pos.y;
    if (isFixed) {
        y += (box.minY + box.maxY) / 2;
    } else {
        y += box.maxY + ((box.minY - box.maxY) * 3 >> 2);
    }

    if (!isFixed && lookAtItem) {
        lookAt(y);
    }

    if (mode == CAMERA_MODE_LOOK || mode == CAMERA_MODE_COMBAT)
    {
        y -= 256;
        target.room = item->room;

        if (lastFixed) {
            target.pos.y = y;
            speed = 1;
        } else {
            target.pos.y += (y - target.pos.y) >> 2;
            speed = (mode == CAMERA_MODE_LOOK) ? LOOK_SPEED : COMBAT_SPEED;
        }

        lastFixed = false;

        if (mode == CAMERA_MODE_LOOK) {
            lookRetail(item);
        } else {
            combatRetail(item);
        }
    }
    else
    {
        target.pos.x = item->pos.x;
        target.pos.z = item->pos.z;

        if (flags == CAM_CENTER)
        {
            int32 s, c;
            sincos(item->angle.y, s, c);
            int32 dz = (box.minZ + box.maxZ) / 2;
            target.pos.z += c * dz >> FIXED_SHIFT;
            target.pos.x += s * dz >> FIXED_SHIFT;
        }

        target.room = item->room;

        if (lastFixed != isFixed)
        {
            target.pos.y = y;
            lastFixed = true;
            speed = 1;
        }
        else
        {
            target.pos.y += (y - target.pos.y) / 4;
            lastFixed = false;
        }

        target.room = target.room->getRoom(target.pos.x, target.pos.y, target.pos.z);

        // a target under the flattened floor: slopes as they are
        if (target.pos.y > target.room->getSector(target.pos.x, target.pos.z)->getFloor(target.pos.x, target.pos.y, target.pos.z)) {
            gChunky = false;
        }

        if (mode == CAMERA_MODE_FOLLOW || flags == CAM_CHASE_OBJECT) {
            chaseRetail(item);
        } else {
            fixedRetail();
        }
    }

    lastIndex = index;
    lastFixed = isFixed;
    gChunky = false;

    if (mode != CAMERA_MODE_OBJECT || timer == -1)
    {
        mode = CAMERA_MODE_FOLLOW;
        index = -1;
        lastItem = lookAtItem;
        lookAtItem = NULL;
        targetAngle.x = 0;
        targetAngle.y = 0;
        targetDist = CAM_DIST_FOLLOW;
        flags = 0;
    }

    vec3i pos = view.pos;
    pos.y += shift;

    vec3i dir = target.pos - pos;
    anglesFromVector(dir.x, dir.y, dir.z, angle.x, angle.y);

    cameraView(pos, angle.x, angle.y);
}

void Camera::update()
{
    // the underwater woosh, keyed again as it ends, while the camera is in
    // water (the original's CalculateCamera, 0600d128)
    if (ROOM_FLAG_WATER(view.room->info->flags)) {
        soundPlay(SND_UNDERWATER, NULL);
    } else {
        soundStop(SND_UNDERWATER);
    }

    if (mode == CAMERA_MODE_CUTSCENE)
    {
        updateCutscene();
        return;
    }

    updateRetail();
}

void Camera::toCombat()
{
    if (mode == CAMERA_MODE_CUTSCENE)
        return;

    if (mode == CAMERA_MODE_LOOK)
        return;

    mode = CAMERA_MODE_COMBAT;
}

#endif
