// VDP1 VRAM layout (offsets from 0x25C00000). tools/pack_level.py reads
// VRAM_TEX_BASE from this file to address textures.
#ifndef VRAM_H
#define VRAM_H

#define VRAM_MAX_CMDS       1792

// slots 0-2 set up clipping, slot 3 jumps to the command bank VDP1 draws
#define VRAM_SLOT_JUMP      3
#define VRAM_CMD_BANK(i)    (0x00100 + (i) * VRAM_MAX_CMDS * 32)
#define VRAM_GOURAUD_BANK(i) (0x1C100 + (i) * VRAM_MAX_CMDS * 8)

// level textures (DATA/<level>.TEX) are loaded here, up to the end of VRAM
#define VRAM_TEX_BASE       0x23100
#define VRAM_TEX_SIZE       (0x80000 - VRAM_TEX_BASE)

#endif
