/* Saturn hardware registers (cache-through addresses). */
#ifndef HW_H
#define HW_H

#include <stdint.h>

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))

/* VDP1 */
#define VDP1_VRAM   0x25C00000
#define VDP1_TVMR   REG16(0x25D00000)
#define VDP1_FBCR   REG16(0x25D00002)
#define VDP1_PTMR   REG16(0x25D00004)
#define VDP1_EWDR   REG16(0x25D00006)
#define VDP1_EWLR   REG16(0x25D00008)
#define VDP1_EWRR   REG16(0x25D0000A)
#define VDP1_ENDR   REG16(0x25D0000C)
#define VDP1_EDSR   REG16(0x25D00010)

/* VDP1 command table entry (32 bytes, in VDP1 VRAM) */
typedef struct {
    uint16_t ctrl, link, pmod, colr, srca, size;
    int16_t xa, ya, xb, yb, xc, yc, xd, yd;
    uint16_t grda, _pad;
} vdp1_cmd_t;

#define CMD_POLYGON     0x0004
#define CMD_USER_CLIP   0x0008
#define CMD_SYSTEM_CLIP 0x0009
#define CMD_LOCAL_COORD 0x000A
#define CMD_END         0x8000

#define PMOD_GOURAUD    0x0004

/* VDP2 */
#define VDP2_VRAM   0x25E00000
#define VDP2_TVMD   REG16(0x25F80000)
#define VDP2_TVSTAT REG16(0x25F80004)
#define VDP2_BGON   REG16(0x25F80020)
#define VDP2_BKTAU  REG16(0x25F800AC)
#define VDP2_BKTAL  REG16(0x25F800AE)
#define VDP2_SPCTL  REG16(0x25F800E0)
#define VDP2_CCCTL  REG16(0x25F800EC)
#define VDP2_PRISA  REG16(0x25F800F0)
#define VDP2_PRISB  REG16(0x25F800F2)
#define VDP2_PRISC  REG16(0x25F800F4)
#define VDP2_PRISD  REG16(0x25F800F6)
#define VDP2_CLOFEN REG16(0x25F80110)

#define TVMD_DISP       0x8000
#define TVSTAT_VBLANK   0x0008
#define SPCTL_SPCLMD    0x0020

/* 15-bit colour; bit 15 marks an RGB (non-palette) pixel for VDP1 */
#define RGB(r, g, b) (0x8000 | ((b) << 10) | ((g) << 5) | (r))

#endif
