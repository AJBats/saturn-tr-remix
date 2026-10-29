"""The Mednafen fork's VDP1 drawing code (build/crunch/vdp1lib.dll, built by
tools/crunch/build_vdp1lib.sh) driven from Python.

VRAM and the frame buffer are numpy views of the library's arrays: 16-bit
words in host order, as Mednafen keeps them. Saturn memory dumps are
big-endian bytes; load_vram / load_fb convert.
"""
import ctypes
import os
import re

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DLL = os.path.join(ROOT, 'build', 'crunch', 'vdp1lib.dll')
CFG = os.path.join(ROOT, 'build', 'mednafen_home', 'mednafen.cfg')

FB_WIDTH = 512      # words a frame buffer line, 16bpp
FB_LINES = 256


class PlotTotals(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ('commands', 'cycles', 'lines', 'pixels', 'texels', 'gouraud')]


class CommandCost(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ('addr', 'cycles', 'lines', 'pixels', 'texels', 'gouraud')]


def calibrated_costs(cfg=CFG):
    """The ss.vdp1.cost.* settings tools/mednafen_timing.py fitted to the
    Saturn, in 1/16 cycle: texel, tex_pixel, gouraud, pixel, line."""
    costs = dict(texel=16, tex_pixel=0, gouraud=0, pixel=0, line=0)
    try:
        with open(cfg, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = re.match(r'ss\.vdp1\.cost\.(\w+) (\d+)', line)
                if m and m.group(1) in costs:
                    costs[m.group(1)] = int(m.group(2))
    except OSError:
        pass
    return costs


class Vdp1:
    def __init__(self, tvmr=0, fbcr=0x03, costs=None):
        self.lib = ctypes.CDLL(DLL)
        self.lib.vdp1_vram.restype = ctypes.POINTER(ctypes.c_uint16)
        self.lib.vdp1_fb.restype = ctypes.POINTER(ctypes.c_uint16)
        self.lib.vdp1_plot.restype = ctypes.c_int32
        self.lib.vdp1_plot.argtypes = [ctypes.POINTER(PlotTotals), ctypes.POINTER(CommandCost), ctypes.c_int32]
        self.vram = np.ctypeslib.as_array(self.lib.vdp1_vram(), shape=(0x40000,))
        self.fb = np.ctypeslib.as_array(self.lib.vdp1_fb(), shape=(FB_LINES, FB_WIDTH))
        self.costs = costs or calibrated_costs()
        self.tvmr, self.fbcr = tvmr, fbcr

    def load_vram(self, data):
        """data: VDP1 VRAM as the Saturn sees it (big-endian bytes, up to 512 KB)."""
        words = np.frombuffer(data, dtype='>u2')
        self.vram[:len(words)] = words

    def load_fb(self, data):
        self.fb.reshape(-1)[:] = np.frombuffer(data, dtype='>u2')

    def plot(self, log_max=0):
        """Runs the command list from address 0. Returns the totals and, with
        log_max, a CommandCost a command (cycles are VDP1 cycles)."""
        c = self.costs
        self.lib.vdp1_reset(self.tvmr, self.fbcr, c['texel'], c['tex_pixel'], c['gouraud'], c['pixel'], c['line'])
        totals = PlotTotals()
        log = (CommandCost * max(log_max, 1))()
        n = self.lib.vdp1_plot(ctypes.byref(totals), log, log_max)
        return totals, [log[i] for i in range(min(n, log_max))]


def rgb(fb, width=352, height=224):
    """A 16bpp frame buffer's RGB (MSB-on pixels, 15-bit BGR) as an 8-bit
    height x width x 3 array; palette-format pixels (MSB off) show black."""
    p = fb[:height, :width].astype(np.uint32)
    out = np.zeros((height, width, 3), np.uint8)
    on = (p & 0x8000) != 0
    for ch, shift in enumerate((0, 5, 10)):
        v = (p >> shift) & 31
        out[..., ch] = np.where(on, (v << 3) | (v >> 2), 0)
    return out
