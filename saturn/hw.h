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
#define VDP1_COPR   REG16(0x25D00014)   /* the command being drawn, address / 8 */

/* VDP1 command table entry (32 bytes, in VDP1 VRAM) */
typedef struct {
    uint16_t ctrl, link, pmod, colr, srca, size;
    int16_t xa, ya, xb, yb, xc, yc, xd, yd;
    uint16_t grda, _pad;
} vdp1_cmd_t;

#define CMD_SCALED_SPRITE    0x0001  /* ZP 0: corners A (top left) and C (bottom right) */
#define CMD_DISTORTED_SPRITE 0x0002
#define CMD_POLYGON     0x0004
#define CMD_POLYLINE    0x0005
#define CMD_USER_CLIP   0x0008
#define CMD_SYSTEM_CLIP 0x0009
#define CMD_LOCAL_COORD 0x000A
#define CMD_SKIP        0x4000  /* JP=100: don't draw, continue with the next */
#define CMD_SKIP_JUMP   0x5000  /* JP=101: don't draw, continue at CMDLINK */
#define CMD_END         0x8000

#define PMOD_SHADOW     0x0001  /* halve the frame buffer pixel under the shape */
#define PMOD_GOURAUD    0x0004
#define PMOD_GOURAUD_HALF 0x0006  /* gouraud, then half luminance */
#define PMOD_LUT16      0x0008  /* 4-bit pixels through a 16-colour lookup table */
#define PMOD_MESH       0x0100  /* every other pixel, a checkerboard */
#define PMOD_SPD_OFF    0x0040  /* pixel code 0 is drawn, not transparent */
#define PMOD_ECD_OFF    0x0080  /* end codes are ordinary pixels */
#define PMOD_HSS        0x1000  /* high-speed shrink: a reduced sprite reads every other texel */
#define PMOD_NO_ECD_SPD 0x00C0  /* disable end codes and transparent pixels */

/* SCU DMA level 0 */
#define SCU_D0R     REG32(0x25FE0000)
#define SCU_D0W     REG32(0x25FE0004)
#define SCU_D0C     REG32(0x25FE0008)
#define SCU_D0AD    REG32(0x25FE000C)
#define SCU_D0EN    REG32(0x25FE0010)
#define SCU_D0MD    REG32(0x25FE0014)
#define SCU_DSTA    REG32(0x25FE007C)
#define SCU_T0C     REG32(0x25FE0090)
#define SCU_T1MD    REG32(0x25FE0098)

#define DSTA_D0_BUSY 0x00000030 /* level 0 DMA waiting (D0WT) or moving (D0MV) */

/* VDP2 */
#define VDP2_VRAM   0x25E00000
#define VDP2_TVMD   REG16(0x25F80000)
#define VDP2_EXTEN  REG16(0x25F80002)   /* reading it latches HCNT/VCNT */
#define VDP2_TVSTAT REG16(0x25F80004)
#define VDP2_HCNT   REG16(0x25F80008)
#define VDP2_VCNT   REG16(0x25F8000A)
#define VDP2_RAMCTL REG16(0x25F8000E)
#define VDP2_CYCA0L REG16(0x25F80010)
#define VDP2_CYCA0U REG16(0x25F80012)
#define VDP2_CYCA1L REG16(0x25F80014)
#define VDP2_CYCA1U REG16(0x25F80016)
#define VDP2_CYCB0L REG16(0x25F80018)
#define VDP2_CYCB0U REG16(0x25F8001A)
#define VDP2_CYCB1L REG16(0x25F8001C)
#define VDP2_CYCB1U REG16(0x25F8001E)
#define VDP2_BGON   REG16(0x25F80020)
#define VDP2_MZCTL  REG16(0x25F80022)
#define VDP2_CHCTLA REG16(0x25F80028)
#define VDP2_BMPNA  REG16(0x25F8002C)
#define VDP2_MPOFN  REG16(0x25F8003C)
#define VDP2_SCXIN0 REG16(0x25F80070)
#define VDP2_SCXDN0 REG16(0x25F80072)
#define VDP2_SCYIN0 REG16(0x25F80074)
#define VDP2_SCYDN0 REG16(0x25F80076)
#define VDP2_ZMXIN0 REG16(0x25F80078)
#define VDP2_ZMXDN0 REG16(0x25F8007A)
#define VDP2_ZMYIN0 REG16(0x25F8007C)
#define VDP2_ZMYDN0 REG16(0x25F8007E)
#define VDP2_ZMCTL  REG16(0x25F80098)
#define VDP2_SCRCTL REG16(0x25F8009A)
#define VDP2_BKTAU  REG16(0x25F800AC)
#define VDP2_BKTAL  REG16(0x25F800AE)
#define VDP2_WCTLA  REG16(0x25F800D0)
#define VDP2_SPCTL  REG16(0x25F800E0)
#define VDP2_CCCTL  REG16(0x25F800EC)
#define VDP2_PRISA  REG16(0x25F800F0)
#define VDP2_PRISB  REG16(0x25F800F2)
#define VDP2_PRISC  REG16(0x25F800F4)
#define VDP2_PRISD  REG16(0x25F800F6)
#define VDP2_PRINA  REG16(0x25F800F8)
#define VDP2_CLOFEN REG16(0x25F80110)

#define TVMD_DISP       0x8000
#define TVMD_VRES_240   0x0010
#define TVMD_HRES_352   0x0001
#define TVSTAT_VBLANK   0x0008
#define RAMCTL_VRAMD    0x0100  /* VRAM A as banks A0 and A1 */
#define RAMCTL_VRBMD    0x0200  /* VRAM B as banks B0 and B1 */
#define CYCP_N0CG       0x4     /* cycle pattern slots: NBG0 character/bitmap read */
#define CYCP_CPU        0xE     /* CPU access */
#define BGON_N0ON       0x0001
#define CHCTLA_N0BMEN   0x0002  /* NBG0 is a bitmap (512x256 with size bits 0) */
#define CHCTLA_N0_32K   0x0030  /* NBG0 in 15-bit RGB */
#define SPCTL_SPCLMD    0x0020

/* SMPC (odd bytes only) */
#define SMPC_REG(x)     (*(volatile uint8_t*)(0x20100000 + (x)))
#define SMPC_IREG(n)    SMPC_REG(0x01 + (n) * 2)
#define SMPC_COMREG     SMPC_REG(0x1F)
#define SMPC_OREG(n)    SMPC_REG(0x21 + (n) * 2)
#define SMPC_SR         SMPC_REG(0x61)
#define SMPC_SF         SMPC_REG(0x63)

/* 15-bit colour; bit 15 marks an RGB (non-palette) pixel for VDP1 */
#define RGB(r, g, b) (0x8000 | ((b) << 10) | ((g) << 5) | (r))

#endif
