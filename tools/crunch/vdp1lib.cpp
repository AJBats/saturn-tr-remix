// vdp1lib: the Mednafen fork's own VDP1 drawing code as a library, so the
// crunch lab draws command lists exactly as Mednafen does.
//
// The drawing files (vdp1_line.cpp, vdp1_sprite.cpp, vdp1_poly.cpp,
// vdp1_common.h) compile unchanged; the pieces of vdp1.cpp they need (the clip
// and local coordinate commands, texel fetches, line and edge setup) come in as
// vdp1_parts.inc, cut from the fork's vdp1.cpp by build_vdp1lib.sh. What is
// written here: the state vdp1.cpp defines, and its command loop (DoDrawing)
// without the time slicing: a plot runs to the list's end in one call.
#include "vdp1_common.h"

#include <vector>

namespace MDFN_IEN_SS
{

namespace VDP1
{

uint8 spr_w_shift_tab[8];
uint8 gouraud_lut[0x40];

line_data LineData;
line_inner_data LineInnerData;
prim_data PrimData;

int32 SysClipX, SysClipY;
int32 UserClipX0, UserClipY0, UserClipX1, UserClipY1;
int32 LocalX, LocalY;

uint8 TVMR;
uint8 FBCR;

uint16* FBDrawWhichPtr;

uint32 DTACounter;
uint32 CostTexel16, CostTexPixel16, CostGouraud16, CostPixel16, CostLine16;
uint32 ExtraCycles16;
uint32 LogLines, LogPixels, LogTexels, LogGouraud;

uint16 VRAM[0x40000];
uint16 FB[2][0x20000];

static INLINE void VRAMUsageDrawRead(uint32 A) { }

#include "vdp1_parts.inc"

}

}

using namespace MDFN_IEN_SS::VDP1;

struct CommandCost
{
 uint32 addr;       // the command's VRAM address / 8
 uint32 cycles, lines, pixels, texels, gouraud;
};

struct PlotTotals
{
 uint32 commands, cycles, lines, pixels, texels, gouraud;
};

extern "C" {

__declspec(dllexport) uint16* vdp1_vram(void) { return VRAM; }
__declspec(dllexport) uint16* vdp1_fb(void) { return FB[0]; }

// Draw costs in 1/16 cycle, as the fork's ss.vdp1.cost.* settings.
__declspec(dllexport) void vdp1_reset(uint32 tvmr, uint32 fbcr, uint32 texel, uint32 tex_pixel, uint32 gouraud,
                                      uint32 pixel, uint32 line)
{
 for(int i = 0; i < 0x40; i++)
  gouraud_lut[i] = std::min<int>(31, std::max<int>(0, i - 16));

 for(int i = 0; i < 8; i++)
  spr_w_shift_tab[i] = (7 - i) / 3;

 TVMR = tvmr;
 FBCR = fbcr;
 FBDrawWhichPtr = FB[0];
 SysClipX = SysClipY = 0;
 UserClipX0 = UserClipY0 = UserClipX1 = UserClipY1 = 0;
 LocalX = LocalY = 0;
 DTACounter = 0;
 CostTexel16 = texel;
 CostTexPixel16 = tex_pixel;
 CostGouraud16 = gouraud;
 CostPixel16 = pixel;
 CostLine16 = line;
 ExtraCycles16 = 0;
}

// Draws the list from VRAM address 0 into FB[0], as a plot does. Each command
// drawn gets a CommandCost while 'log' has room; returns the commands run.
__declspec(dllexport) int32 vdp1_plot(PlotTotals* totals, CommandCost* log, int32 log_max)
{
 static int32 (*const command_table[0xC])(const uint16* cmd_data) =
 {
  CMD_NormalSprite, CMD_ScaledSprite,   CMD_DistortedSprite, CMD_DistortedSprite,
  CMD_Polygon,      CMD_Line,           CMD_Line,            CMD_Line,
  CMD_SetUserClip,  CMD_SetSystemClip,  CMD_SetLocalCoord,   CMD_SetUserClip
 };

 static int32 (*const resume_table[0x8])(const uint16* cmd_data) =
 {
  RESUME_Sprite,  RESUME_Sprite, RESUME_Sprite, RESUME_Sprite,
  RESUME_Polygon, RESUME_Line,   RESUME_Line,   RESUME_Line,
 };

 uint32 addr = 0;
 int32 ret_addr = -1;
 int32 count = 0, logged = 0;
 uint16 cmd[0x10];

 memset(totals, 0, sizeof(*totals));

 for(int32 guard = 0; guard < 0x10000; guard++)
 {
  memcpy(cmd, &VRAM[addr], sizeof(cmd));
  uint32 cycles = 16;
  const uint32 lines0 = LogLines, pixels0 = LogPixels, texels0 = LogTexels, gouraud0 = LogGouraud;

  if(!(cmd[0] & 0xC000))
  {
   if((cmd[0] & 0xF) >= 0xC)
    break;

   cycles += command_table[cmd[0] & 0xF](cmd);
   if(!(cmd[0] & 0x8))
   {
    for(;;)
    {
     const int32 c = resume_table[cmd[0] & 0x7](cmd);

     if(!c)
      break;

     cycles += c;
    }
   }
  }
  else if(cmd[0] & 0x8000)
   break;

  count++;
  totals->commands++;
  totals->cycles += cycles;
  if(logged < log_max)
  {
   CommandCost& c = log[logged++];
   c.addr = addr >> 2;
   c.cycles = cycles;
   c.lines = LogLines - lines0;
   c.pixels = LogPixels - pixels0;
   c.texels = LogTexels - texels0;
   c.gouraud = LogGouraud - gouraud0;
  }

  addr = (addr + 0x10) & 0x3FFFF;
  switch((cmd[0] >> 12) & 0x3)
  {
   case 0:
    break;

   case 1:
    addr = (cmd[1] << 2) &~ 0xF;
    break;

   case 2:
    if(ret_addr < 0)
     ret_addr = addr;
    addr = (cmd[1] << 2) &~ 0xF;
    break;

   case 3:
    if(ret_addr >= 0)
    {
     addr = ret_addr;
     ret_addr = -1;
    }
    break;
  }
 }

 totals->lines = LogLines;
 totals->pixels = LogPixels;
 totals->texels = LogTexels;
 totals->gouraud = LogGouraud;
 LogLines = LogPixels = LogTexels = LogGouraud = 0;
 return count;
}

}
