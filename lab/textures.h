// The room texture cache's misses and uploads: saturn/detail.cpp's, which the
// new renderer keeps as it is, so both ways take them from here. A miss takes
// the least recently drawn slot of its level's pool not drawn this frame and
// queues its texture's upload, up to TEXTURE_UPLOADS a frame; the uploads go
// to VDP1 VRAM through a staging buffer in high work RAM (the SCU's DMA can't
// read the set in low work RAM), each texture whole, in quadrants or
// transposed.
//
// The cache is the scene's (DKEY, DSLT: the arrays both ways look textures up
// in); texturesReset puts back the state the frame found it in (DKY0, DSL0).
#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdint.h>

#define TEXTURE_UPLOADS 32

struct TextureUpload
{
    uint32_t src;       // in the room texture set (RTEX)
    uint32_t dst;       // its slot's VDP1 VRAM offset, the variant << 24
    uint32_t size;      // the texture's width << 16 | height, at its level
};

struct TextureQueue
{
    int32_t count;
    TextureUpload uploads[TEXTURE_UPLOADS];
};

void texturesInit();
void texturesReset();

// A texture the cache hasn't, as of frame (key: Texture.reserved - 1;
// texSize: Texture.size; variant: DetailVariant), into it: its slot in its
// level's pool, its upload queued; -1 when it can't be (past the set, the
// queue full, or every slot drawn this frame).
int32_t texturesMiss(TextureQueue &queue, uint32_t frame, uint32_t key, uint32_t texSize, uint32_t variant);

// The queue's textures into their slots. The SH-2 cycles the staging took
// (the DMA runs beside it).
uint32_t texturesUpload(const TextureQueue &queue);

// What the checks hold them to: the uploads the game queued (DUPL); entries
// of two queues that differ (a count apart counting as all); the cache now
// against the game's after its frame (DKEY, DSLT as captured): keys and
// slots that differ; the queue's textures as staged against VDP1 VRAM now
// (before the lab uploads any, the scene's image: the game's own uploads):
// longwords that differ.
const TextureQueue& texturesCaptured();
int32_t texturesQueueOff(const TextureQueue &a, const TextureQueue &b);
int32_t texturesStateOff();
int32_t texturesVramOff(const TextureQueue &queue);

#endif
