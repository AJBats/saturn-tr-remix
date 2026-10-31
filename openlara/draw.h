#ifndef H_DRAW
#define H_DRAW

#include "common.h"
#include "item.h"
#include "trace.h"

//#define TEST_ROOM_CACHE

#ifdef TEST_ROOM_CACHE
RoomVertex roomVert[512];
EWRAM_DATA RoomQuad roomQuads[512];
EWRAM_DATA RoomTriangle roomTri[64];
#endif

void drawInit()
{
    renderInit();

    for (int32 i = 0; i < MAX_RAND_TABLE; i++)
    {
        gRandTable[i] = (rand_draw() >> 5) - 511;
    }

    for (int32 i = 0; i < MAX_CAUSTICS; i++)
    {
        int16 rot = i * (ANGLE_90 * 4) / MAX_CAUSTICS;
        gCaustics[i] = sin(rot) * 768 >> FIXED_SHIFT;
    }
}

void drawFree()
{
    renderFree();
}

void drawLevelInit()
{
    renderLevelInit();

#ifdef TEST_ROOM_CACHE
    Room &room = rooms[14];

    memcpy(roomVert, room.data.vertices, sizeof(RoomVertex) * room.info->verticesCount);
    memcpy(roomQuads, room.data.quads, sizeof(RoomQuad) * room.info->quadsCount);
    memcpy(roomTri, room.data.triangles, sizeof(RoomTriangle) * room.info->trianglesCount);

    room.data.vertices = roomVert;
    room.data.quads = roomQuads;
    room.data.triangles = roomTri;
#endif
}

void drawLevelFree()
{
    renderLevelFree();
}

const static uint8 char_width[110] = {
    14, 11, 11, 11, 11, 11, 11, 13, 8, 11, 12, 11, 13, 13, 12, 11, 12, 12, 11, 12, 13, 13, 13, 12, 12, 11, // A-Z
    9, 9, 9, 9, 9, 9, 9, 9, 5, 9, 9, 5, 12, 10, 9, 9, 9, 8, 9, 8, 9, 9, 11, 9, 9, 9, // a-z
    12, 8, 10, 10, 10, 10, 10, 9, 10, 10, // 0-9
    5, 5, 5, 11, 9, 7, 8, 6, 0, 7, 7, 3, 8, 8, 13, 7, 9, 4, 12, 12, 
    7, 5, 7, 7, 7, 7, 7, 7, 7, 7, 16, 14, 14, 14, 16, 16, 16, 16, 16, 12, 14, 8, 8, 8, 8, 8, 8, 8
};

static const uint8 char_map[102] = {
    0, 64, 66, 78, 77, 74, 78, 79, 69, 70, 92, 72, 63, 71, 62, 68, 52, 53, 54, 55, 56, 57, 58, 59, 
    60, 61, 73, 73, 66, 74, 75, 65, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 
    18, 19, 20, 21, 22, 23, 24, 25, 80, 76, 81, 97, 98, 77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 100, 101, 102, 67, 0, 0, 0, 0, 0, 0, 0
};

X_INLINE int32 charRemap(char c)
{
    if (c < 11)
        return c + 81;
    if (c < 16)
        return c + 91;
    return char_map[c - 32];
}

int32 getTextWidth(const char* text)
{
    int32 w = 0;

    char c;
    while ((c = *text++) != 0)
    {
        if (c == ' ') {
            w += 6;
            continue;
        }
        w += char_width[charRemap(c)] + 1;
    }

    return w;
}

void drawText(int32 x, int32 y, const char* text, TextAlign align)
{
    if (!text || !*text)
        return;

    if (align == TEXT_ALIGN_CENTER) {
        x += (FRAME_WIDTH - getTextWidth(text)) >> 1;
    }

    if (align == TEXT_ALIGN_RIGHT) {
        x += FRAME_WIDTH - getTextWidth(text);
    }

    int32 index;
    char c;
    while ((c = *text++) != 0)
    {
        if (c == ' ') {
            x += 6;
            continue;
        }

        if (c == '$') { // special char
            index = *text++;
        } else {
            index = charRemap(c);
        }

        int32 iy = y;

        if (c == 'p') { // TODO investigate!
            iy--;
        }

        renderGlyph(x, iy, level.models[ITEM_GLYPHS].start + index);
        x += char_width[index] + 1;
    }
}

// The item's shadow (the renderer's: its size 160), on the floor under it.
void drawShadow(const ItemObj* item)
{
    const Sector* sector = item->room->getSector(item->pos.x, item->pos.z);
    int32 floor = sector->getFloor(item->pos.x, item->pos.y, item->pos.z);
    renderShadowCall(item, floor);
}

void drawSprite(const ItemObj* item)
{
    renderSprite(item->pos.x, item->pos.y, item->pos.z, item->intensity << 5, level.models[item->type].start + item->frameIndex);
}

// The renderer tests the model's sphere, poses and lights it, and draws its
// shadow if it is shown.
void drawModel(const ItemObj* item)
{
    renderModel(item);
    if (item->flags & ITEM_FLAG_SHADOW) {
        drawShadow(item);
    }
}

// What stands in a room seen: sprites, static meshes, items.
void drawRoomObjects(Room* room)
{
    setViewport(room->clip);

    const RoomInfo* info = room->info;
    const RoomData& data = room->data;

    int32 rx = info->x << 8;
    int32 ry = info->yTop;
    int32 rz = info->z << 8;

    setPaletteIndex(ROOM_FLAG_WATER(info->flags) ? 1 : 0);

    for (int32 i = 0; i < info->spritesCount; i++)
    {
        const RoomSprite* sprite = data.sprites + i;
        renderSprite(sprite->pos.x + rx, sprite->pos.y, sprite->pos.z + rz, sprite->g << 5, sprite->index);
    }

    rx -= gCameraViewPos.x;
    ry = -gCameraViewPos.y;
    rz -= gCameraViewPos.z;

#ifdef NO_STATIC_MESH_PLANTS
    #error "the renderer draws every static mesh"
#endif
    for (int32 i = 0; i < info->meshesCount; i++) {
        renderStatic(data.meshes + i, rx, ry, rz, room - rooms, i);
    }

    ItemObj* item = room->firstItem;
    while (item)
    {
        if ((item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE)
        {
            traceDetailBegin(TR_ITEM, item->type);
            item->draw();
            traceDetailEnd(TR_ITEM, item->type);
        }
        item = item->nextItem;
    }
}

void drawRooms(Camera* camera)
{
    traceBegin(TRACE_MASTER, TR_VIEW);
    RectMinMax vp = viewport;

    setCameraWater(ROOM_FLAG_WATER(camera->view.room->info->flags));
    // a camera pushed into a wall sees past the rooms' faces, where nothing
    // would draw this frame
    setSceneClear(checkWall(camera->view.room, camera->view.pos.x, camera->view.pos.y + camera->shift, camera->view.pos.z));

    Room** visRoom = renderWalk(camera->view.room);
    traceEnd(TRACE_MASTER, TR_VIEW);

    // Lara before the rooms' objects and whether or not her room is seen, as
    // the original draws her: climbing out of a room, she is still in it
    uint32 laraStatus[MAX_PLAYERS] = {};
    setViewport(vp);
    traceBegin(TRACE_MASTER, TR_LARA);
    for (int32 i = 0; i < MAX_PLAYERS; i++)
    {
        Lara* lara = players[i];
        if (lara)
        {
            laraStatus[i] = lara->flags & ITEM_FLAG_STATUS;
            if (laraStatus[i] != ITEM_FLAG_STATUS_INVISIBLE)
            {
                setPaletteIndex(ROOM_FLAG_WATER(lara->room->info->flags) ? 1 : 0);
                lara->draw();
            }
            lara->flags |= ITEM_FLAG_STATUS_INVISIBLE; // skip drawing in the general pass
        }
    }
    traceEnd(TRACE_MASTER, TR_LARA);

    while (*visRoom)
    {
        Room* room = *visRoom++;
        traceDetailBegin(TR_OBJECTS, room - rooms);
        drawRoomObjects(room);
        traceDetailEnd(TR_OBJECTS, room - rooms);
        room->reset();
    }

    for (int32 i = 0; i < MAX_PLAYERS; i++)
    {
        Lara* lara = players[i];
        if (lara)
        {
            lara->flags = (lara->flags & ~ITEM_FLAG_STATUS) | laraStatus[i];
        }
    }

    setCameraWater(false);
    setPaletteIndex(0);
    setViewport(vp);
}

void drawCinematicRooms()
{
    RectMinMax vp = viewport;
    setCameraWater(ROOM_FLAG_WATER(gCinematicCamera.view.room->info->flags));

    Room** visRoom = renderWalk(gCinematicCamera.view.room);
    while (*visRoom)
    {
        Room* room = *visRoom++;
        drawRoomObjects(room);
        room->reset();
    }

    setCameraWater(false);
    setPaletteIndex(0);
    setViewport(vp);
}

void drawHUD(Lara* lara)
{
    int32 x = (FRAME_WIDTH - (100 + 2 + 2)) - 4;
    int32 y = 4;

    if (lara->waterState == WATER_STATE_SURFACE || lara->waterState == WATER_STATE_UNDER)
    {
        int32 v = (lara->oxygen << 8) / LARA_MAX_OXYGEN;
        renderBar(x, y, 100, v, BAR_OXYGEN);
        y += 10;
    }

    if (lara->extraL->healthTimer || lara->extraL->weaponState == WEAPON_STATE_READY)
    {
        int32 v = (lara->health << 8) / LARA_MAX_HEALTH;
        renderBar(x, y, 100, v, BAR_HEALTH);
    }
}

void drawFPS()
{
#if defined(__SATURN__)
    osDrawDebug(); // the fps too, over the frame on VDP2
    return;
#endif

#ifdef __GBA__
    if (gLevelID == LVL_TR1_TITLE)
    {
        if (REG_WSCNT != (WS_ROM0_N2 | WS_ROM0_S1 | WS_PREFETCH))
        {
            drawText(0, 15, "! slow cartridge !", TEXT_ALIGN_CENTER);
        }
    }
#endif

    if (!gSettings.video_fps)
        return;

    char buf[32];
    int2str(fps, buf);
    drawText(2, 16, buf, TEXT_ALIGN_LEFT);
}

#ifdef PROFILING
void drawProfiling()
{
    for (int32 i = 0; i < CNT_MAX; i++)
    {
        char buf[32];
        int2str(gCounters[i], buf);
        drawText(2, 16 + 32 + i * 16, buf, TEXT_ALIGN_LEFT);
    }
    flush();
}
#endif

#endif
