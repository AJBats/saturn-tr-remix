#ifndef H_NAV
#define H_NAV

#include "common.h"

void Nav::init(uint32 boxIndex)
{
    switch (stepHeight)
    {
        case 256 : zoneType = ZONE_GROUND_1; break;
        case 512 : zoneType = ZONE_GROUND_2; break;
        default  : zoneType = ZONE_FLY;
    }

    weight = 0;
    endBox = NO_BOX;
    nextBox = NO_BOX;

    headBox = NO_BOX;
    tailBox = NO_BOX;

    mask = BOX_BLOCKED; // (0x400 here took overlap indices from 1024 on for the flag)

    for (int32 i = 0; i < level.boxesCount; i++)
    {
        cells[i].end = NO_BOX;
        cells[i].next = NO_BOX;
        cells[i].weight = 0;
    }

    const uint16* defZones = level.zones[0][zoneType];
    const uint16* altZones = level.zones[1][zoneType];

    uint16 defZone = defZones[boxIndex];
    uint16 altZone = altZones[boxIndex];

    cellsCount = 0;
    Nav::Cell* cell = cells;

    for (int32 i = 0; i < level.boxesCount; i++)
    {
        if ((defZone == defZones[i]) || (altZone == altZones[i]))
        {
            (*cell++).boxIndex = i;
            cellsCount++;
        }
    }

    ASSERT(cellsCount > 0);
}

vec3i Nav::getWaypoint(uint32 boxIndex, const vec3i &from)
{
    if (nextBox != NO_BOX && nextBox != endBox)
    {
        endBox = nextBox;

        Nav::Cell &cell = cells[endBox];

        if (cell.next == NO_BOX && tailBox != endBox)
        {
            cell.next = headBox;

            if (headBox == NO_BOX) {
                tailBox = endBox;
            }

            headBox = endBox;
        }

        weight++;
        cell.weight = weight;
        cell.end = NO_BOX;
    }

    if (headBox != NO_BOX)
    {
        const uint16* zones = level.zones[gSaveGame.flipped][zoneType];
        uint16 zone = zones[headBox];

        for (int32 i = 0; (i < NAV_STEPS) && (headBox != NO_BOX); i++)
        {
            search(zone, zones);
        }
    }

    if (boxIndex == endBox)
        return pos;

    // retail's CalculateTarget (TR1X box.c): along the path's boxes toward the
    // target box, the farthest point the boxes passed so far all let the
    // creature head for in a line; a flyer at least a sector above each box's
    // floor, a walker no lower than it; with the path cut short, a random
    // point in the last box reached. Boxes: z is retail's left and right, x
    // its top and bottom, z tested first.
    enum {
        CLIP_LEFT      = (1 << 0),
        CLIP_RIGHT     = (1 << 1),
        CLIP_TOP       = (1 << 2),
        CLIP_BOTTOM    = (1 << 3),
        CLIP_ALL       = (CLIP_LEFT | CLIP_RIGHT | CLIP_TOP | CLIP_BOTTOM),
        CLIP_SECONDARY = (1 << 4)
    };

    vec3i wp = from;

    if (boxIndex == NO_BOX)
        return wp;

    int32 left = 0, right = 0, top = 0, bottom = 0;
    int32 primeFree = CLIP_ALL;
    const Box* box;

    do {
        box = level.boxes + boxIndex;
        int32 bLeft   = box->minZ << 10;
        int32 bRight  = (box->maxZ << 10) - 1;
        int32 bTop    = box->minX << 10;
        int32 bBottom = (box->maxX << 10) - 1;

        int32 floor = box->floor - ((zoneType == ZONE_FLY) ? 1024 : 0);
        if (wp.y > floor) {
            wp.y = floor;
        }

        if (from.z >= bLeft && from.z <= bRight && from.x >= bTop && from.x <= bBottom)
        {
            left = bLeft;
            right = bRight;
            top = bTop;
            bottom = bBottom;
        }
        else
        {
            if (from.z < bLeft)
            {
                if ((primeFree & CLIP_LEFT) && from.x >= bTop && from.x <= bBottom)
                {
                    if (wp.z < bLeft + 512) {
                        wp.z = bLeft + 512;
                    }
                    if (primeFree & CLIP_SECONDARY)
                        return wp;
                    top = X_MAX(top, bTop);
                    bottom = X_MIN(bottom, bBottom);
                    primeFree = CLIP_LEFT;
                }
                else if (primeFree != CLIP_LEFT)
                {
                    wp.z = right - 512;
                    if (primeFree != CLIP_ALL)
                        return wp;
                    primeFree |= CLIP_SECONDARY;
                }
            }
            else if (from.z > bRight)
            {
                if ((primeFree & CLIP_RIGHT) && from.x >= bTop && from.x <= bBottom)
                {
                    if (wp.z > bRight - 512) {
                        wp.z = bRight - 512;
                    }
                    if (primeFree & CLIP_SECONDARY)
                        return wp;
                    top = X_MAX(top, bTop);
                    bottom = X_MIN(bottom, bBottom);
                    primeFree = CLIP_RIGHT;
                }
                else if (primeFree != CLIP_RIGHT)
                {
                    wp.z = left + 512;
                    if (primeFree != CLIP_ALL)
                        return wp;
                    primeFree |= CLIP_SECONDARY;
                }
            }

            if (from.x < bTop)
            {
                if ((primeFree & CLIP_TOP) && from.z >= bLeft && from.z <= bRight)
                {
                    if (wp.x < bTop + 512) {
                        wp.x = bTop + 512;
                    }
                    if (primeFree & CLIP_SECONDARY)
                        return wp;
                    left = X_MAX(left, bLeft);
                    right = X_MIN(right, bRight);
                    primeFree = CLIP_TOP;
                }
                else if (primeFree != CLIP_TOP)
                {
                    wp.x = bottom - 512;
                    if (primeFree != CLIP_ALL)
                        return wp;
                    primeFree |= CLIP_SECONDARY;
                }
            }
            else if (from.x > bBottom)
            {
                if ((primeFree & CLIP_BOTTOM) && from.z >= bLeft && from.z <= bRight)
                {
                    if (wp.x > bBottom - 512) {
                        wp.x = bBottom - 512;
                    }
                    if (primeFree & CLIP_SECONDARY)
                        return wp;
                    left = X_MAX(left, bLeft);
                    right = X_MIN(right, bRight);
                    primeFree = CLIP_BOTTOM;
                }
                else if (primeFree != CLIP_BOTTOM)
                {
                    wp.x = top + 512;
                    if (primeFree != CLIP_ALL)
                        return wp;
                    primeFree |= CLIP_SECONDARY;
                }
            }
        }

        if (boxIndex == endBox)
        {
            if (primeFree & (CLIP_LEFT | CLIP_RIGHT)) {
                wp.z = pos.z;
            } else if (!(primeFree & CLIP_SECONDARY)) {
                if (wp.z < bLeft + 512) {
                    wp.z = bLeft + 512;
                } else if (wp.z > bRight - 512) {
                    wp.z = bRight - 512;
                }
            }

            if (primeFree & (CLIP_TOP | CLIP_BOTTOM)) {
                wp.x = pos.x;
            } else if (!(primeFree & CLIP_SECONDARY)) {
                if (wp.x < bTop + 512) {
                    wp.x = bTop + 512;
                } else if (wp.x > bBottom - 512) {
                    wp.x = bBottom - 512;
                }
            }

            wp.y = pos.y;
            return wp;
        }

        boxIndex = cells[boxIndex].end;
        if (boxIndex != NO_BOX && (level.boxes[boxIndex].overlap & mask))
            break;
    } while (boxIndex != NO_BOX);

    int32 bLeft   = box->minZ << 10;
    int32 bRight  = (box->maxZ << 10) - 1;
    int32 bTop    = box->minX << 10;
    int32 bBottom = (box->maxX << 10) - 1;

    if (primeFree & (CLIP_LEFT | CLIP_RIGHT)) {
        wp.z = bLeft + 512 + RAND_LOGIC(bRight - bLeft - 1024);
    } else if (!(primeFree & CLIP_SECONDARY)) {
        if (wp.z < bLeft + 512) {
            wp.z = bLeft + 512;
        } else if (wp.z > bRight - 512) {
            wp.z = bRight - 512;
        }
    }

    if (primeFree & (CLIP_TOP | CLIP_BOTTOM)) {
        wp.x = bTop + 512 + RAND_LOGIC(bBottom - bTop - 1024);
    } else if (!(primeFree & CLIP_SECONDARY)) {
        if (wp.x < bTop + 512) {
            wp.x = bTop + 512;
        } else if (wp.x > bBottom - 512) {
            wp.x = bBottom - 512;
        }
    }

    wp.y = box->floor - ((zoneType == ZONE_FLY) ? 384 : 0);

    return wp;
}

void Nav::search(uint16 zone, const uint16* zones)
{
    Nav::Cell &curr = cells[headBox];
    const Box &b = level.boxes[headBox];

    uint16 overlapIndex = b.overlap & NAV_INDEX;

    bool end = false;

    do {
        uint16 boxIndex = level.overlaps[overlapIndex++];

        end = boxIndex & NAV_BLOCKED;
        if (end) {
            boxIndex &= NAV_INDEX;
        }

        if (zone != zones[boxIndex])
            continue;

        int32 diff = level.boxes[boxIndex].floor - b.floor;
        if (diff > stepHeight || diff < dropHeight)
            continue;

        Nav::Cell &next = cells[boxIndex];

        uint16 cWeight = curr.weight & NAV_WEIGHT;
        uint16 nWeight = next.weight & NAV_WEIGHT;

        if (cWeight < nWeight)
            continue;

        if (curr.weight & NAV_BLOCKED)
        {
            if (cWeight == nWeight)
                continue;

            next.weight = curr.weight;
        }
        else
        {        
            if ((cWeight == nWeight) && !(next.weight & NAV_BLOCKED))
                continue;

            if (level.boxes[boxIndex].overlap & mask)
            {
                next.weight = curr.weight | NAV_BLOCKED;
            }
            else
            {
                next.weight = curr.weight;
                next.end = headBox;
            }
        }

        if (next.next == NO_BOX && boxIndex != tailBox)
        {
            cells[tailBox].next = boxIndex;
            tailBox = boxIndex;
        }
    } while (!end);

    headBox = curr.next;
    curr.next = NO_BOX;
}

#endif
