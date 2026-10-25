// VDP1 VRAM layout (offsets from 0x25C00000). tools/pack_level.py reads
// VRAM_TEX_BASE from this file to address textures.
#ifndef VRAM_H
#define VRAM_H

#define VRAM_MAX_CMDS       1792

// slots 0-2 set up clipping, the frame's commands follow
#define VRAM_CMD_BASE       0x00000
#define VRAM_FRAME_CMDS     0x00060
#define VRAM_GOURAUD_BASE   (VRAM_FRAME_CMDS + VRAM_MAX_CMDS * 32)

// an 8x1 character of colour 0, which the frame's first command stretches over
// the screen to clear it
#define VRAM_CLEAR_TEX      (VRAM_GOURAUD_BASE + VRAM_MAX_CMDS * 8)

// level textures (DATA/<level>.TEX) are loaded here; the rest of VRAM caches
// full-res room textures (detail.h)
#define VRAM_TEX_BASE       0x11880     // 32-byte aligned, as colour lookup tables must be
#define VRAM_TEX_SIZE       (0x80000 - VRAM_TEX_BASE)

#endif
