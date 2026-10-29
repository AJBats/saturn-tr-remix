"""Reads a capture (capture.py): the frame's room jobs from high work RAM,
and VDP1 command lists decoded into comparable records."""
import os
import struct
import subprocess

import numpy as np

from medclient import LAB, ROOT

NM = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-nm')
ROOM_SIZE = 64          # common.h Room
JOB_SIZE = 72           # rooms.cpp RoomJob


def symbols(elf=os.path.join(LAB, '0MAIN.elf')):
    syms = {}
    for line in subprocess.run([NM, '-C', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            syms.setdefault(parts[2], int(parts[0], 16))
    return syms


class Capture:
    def __init__(self, name, elf=os.path.join(LAB, '0MAIN.elf')):
        path = lambda kind: os.path.join(LAB, 'captures', f'{name}_{kind}.bin')
        self.name = name
        with open(path('hwram'), 'rb') as f:
            self.hwram = f.read()
        with open(path('vram'), 'rb') as f:
            self.vram = f.read()
        with open(path('fb_pre'), 'rb') as f:
            self.fb_pre = f.read()
        with open(path('fb_post'), 'rb') as f:
            self.fb_post = f.read()
        self.syms = symbols(elf)

    def u32(self, addr):
        return struct.unpack_from('>I', self.hwram, addr - 0x06000000)[0]

    def i32(self, addr):
        return struct.unpack_from('>i', self.hwram, addr - 0x06000000)[0]

    def jobs(self):
        """(room index, R 3x3, T 3, camera 3, viewport 4) per queued room."""
        base = self.syms['gJobs']
        rooms = self.syms['rooms']
        out = []
        for i in range(self.u32(self.syms['gJobCount'])):
            a = base + i * JOB_SIZE - 0x06000000
            room_ptr = struct.unpack_from('>I', self.hwram, a)[0]
            m = struct.unpack_from('>9h', self.hwram, a + 4)
            t = struct.unpack_from('>3i', self.hwram, a + 24)
            clip = struct.unpack_from('>4i', self.hwram, a + 36)
            cam = struct.unpack_from('>3i', self.hwram, a + 56)
            out.append(((room_ptr - rooms) // ROOM_SIZE, np.array(m, np.int64).reshape(3, 3),
                        np.array(t, np.int64), cam, clip))
        return out

    def view(self):
        """gCameraViewPos and gViewRotation."""
        pos = struct.unpack_from('>3i', self.hwram, self.syms['gCameraViewPos'] - 0x06000000)
        rot = struct.unpack_from('>9h', self.hwram, self.syms['gViewRotation'] - 0x06000000)
        return pos, np.array(rot, np.int64).reshape(3, 3)


def s13(v):
    v &= 0x1FFF
    return v - 0x2000 if v & 0x1000 else v


def decode(vram, start=0, limit=8192):
    """The command list in a VDP1 VRAM image (bytes) as drawn, one record a
    drawing command: a tuple comparable across VRAM layouts (texture and
    colour table contents instead of addresses)."""
    out = []
    addr = 0
    ret = -1
    for _ in range(limit):
        w = struct.unpack_from('>16H', vram, addr)
        if w[0] & 0x8000:
            break
        nxt = (addr + 32) & 0x7FFFF
        if not (w[0] & 0x4000):
            kind = w[0] & 0xF
            if kind >= 0xC:
                break
            if kind < 8:
                out.append(record(vram, w, addr))
        jp = (w[0] >> 12) & 3
        if jp == 1:
            nxt = (w[1] * 8) & ~31
        elif jp == 2:
            if ret < 0:
                ret = nxt
            nxt = (w[1] * 8) & ~31
        elif jp == 3 and ret >= 0:
            nxt, ret = ret, -1
        addr = nxt
    return out


def shade(c, g):
    out = c & 0x8000
    for shift in (0, 5, 10):
        out |= min(31, max(0, ((c >> shift) & 31) + ((g >> shift) & 31) - 16)) << shift
    return out


def record(vram, w, addr):
    kind = w[0] & 0xF
    pmod = w[2]
    coords = tuple(s13(v) for v in w[6:14])
    grd = tuple(struct.unpack_from('>4H', vram, w[14] * 8)) if pmod & 4 and kind in (2, 4) else None
    if kind == 2:
        tw, th = ((w[5] >> 8) & 0x3F) * 8, w[5] & 0xFF
        cm = (pmod >> 3) & 7
        nbytes = tw * th // 2 if cm in (0, 1) else tw * th * (2 if cm == 5 else 1)
        tex = bytes(vram[w[4] * 8:w[4] * 8 + nbytes])
        clut = tuple(struct.unpack_from('>16H', vram, w[3] * 8)) if cm == 1 else w[3]
        if cm == 1 and grd is not None and (pmod & 7) == 4 and len(set(grd)) == 1 and grd[0] < 0x8000:
            # one gouraud value at every corner draws as that value added to
            # the colour table (the game's shaded tables): the same pixels
            pmod &= ~7
            clut = tuple(shade(c, grd[0]) for c in clut)
            grd = None
        return ('sprite', w[0] & 0x30, pmod, clut, tw, th, hash(tex), coords, grd)
    return ('poly', kind, w[0] & 0x30, pmod, w[3], coords, grd)
