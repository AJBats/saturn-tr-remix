/* Boot test: a spinning gouraud quad drawn by VDP1 over a VDP2 back screen. */
#include "hw.h"

#define BACK_COLOR_ADDR 0x7FFFE  /* last word of VDP2 VRAM */
#define GOURAUD_ADDR    0x1000   /* 4-entry gouraud table in VDP1 VRAM */

static volatile vdp1_cmd_t *const cmds = (volatile vdp1_cmd_t *)VDP1_VRAM;

static void vdp2_init(void)
{
    VDP2_TVMD = 0;
    VDP2_BGON = 0;
    VDP2_CCCTL = 0;
    VDP2_CLOFEN = 0;
    VDP2_SPCTL = SPCTL_SPCLMD;
    VDP2_PRISA = VDP2_PRISB = VDP2_PRISC = VDP2_PRISD = 0x0606;

    REG16(VDP2_VRAM + BACK_COLOR_ADDR) = RGB(2, 4, 10);
    VDP2_BKTAU = (BACK_COLOR_ADDR >> 17) & 7;
    VDP2_BKTAL = (BACK_COLOR_ADDR >> 1) & 0xFFFF;

    VDP2_TVMD = TVMD_DISP;  /* 320x224, non-interlaced */
}

static void vdp1_init(void)
{
    volatile uint16_t *g = (volatile uint16_t *)(VDP1_VRAM + GOURAUD_ADDR);
    int i;

    VDP1_TVMR = 0;          /* 320x224, 16bpp */
    VDP1_FBCR = 0;          /* swap and erase every field */
    VDP1_EWDR = 0;          /* erase to transparent */
    VDP1_EWLR = 0;
    VDP1_EWRR = (40 << 9) | 223;

    g[0] = RGB(31, 0, 0);
    g[1] = RGB(0, 31, 0);
    g[2] = RGB(0, 0, 31);
    g[3] = RGB(31, 31, 0);

    for (i = 0; i < 5; i++) {
        cmds[i].link = 0;
    }
    cmds[0].ctrl = CMD_SYSTEM_CLIP;
    cmds[0].xc = 319;
    cmds[0].yc = 223;
    cmds[1].ctrl = CMD_USER_CLIP;
    cmds[1].xa = 0;
    cmds[1].ya = 0;
    cmds[1].xc = 319;
    cmds[1].yc = 223;
    cmds[2].ctrl = CMD_LOCAL_COORD;
    cmds[2].xa = 160;
    cmds[2].ya = 112;
    cmds[3].ctrl = CMD_POLYGON;
    cmds[3].pmod = 0x00C0 | PMOD_GOURAUD;
    cmds[3].colr = RGB(31, 31, 31);
    cmds[3].grda = GOURAUD_ADDR >> 3;
    cmds[4].ctrl = CMD_END;

    VDP1_PTMR = 2;          /* draw at every frame change */
}

static void wait_vblank(void)
{
    while (VDP2_TVSTAT & TVSTAT_VBLANK)
        ;
    while (!(VDP2_TVSTAT & TVSTAT_VBLANK))
        ;
}

int main(void)
{
    /* Minsky circle: (c, s) rotates in place with shifts only (8.8 fixed) */
    int c = 80 << 8, s = 0;

    vdp2_init();
    vdp1_init();

    for (;;) {
        int x = c >> 8, y = s >> 8;

        wait_vblank();
        cmds[3].xa = x;  cmds[3].ya = y;
        cmds[3].xb = -y; cmds[3].yb = x;
        cmds[3].xc = -x; cmds[3].yc = -y;
        cmds[3].xd = y;  cmds[3].yd = -x;

        c -= s >> 5;
        s += c >> 5;
    }
}
