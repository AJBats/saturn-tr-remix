"""Simulator for the SCU DSP: runs a program assembled by tools/dspasm.py over
a model of high work RAM, as Mednafen's scu_dsp_*.cpp execute it, and times
it two ways:

  mednafen  a DMA moves its data at once and keeps T0 set 1 DSP cycle a
            longword; nothing waits on it but a jump on T0
  saturn    the prices dspbench measured on the Saturn: a DMA from work RAM
            29 + 1.0 SH-2 cycles a longword, to it 31 + 3.1 (page 1); a DMA
            started while one runs waits for it; an instruction touching
            the bank a DMA is using (its RAM or its CT) waits for the DMA to
            end (page 4)

DSP cycles are half the SH-2's. Used by dspbench/test_meshlight.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dspasm import assemble  # noqa: E402

M32 = 0xFFFFFFFF
M48 = 0xFFFFFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def s8(v):
    v &= 0xFF
    return v - 256 if v & 0x80 else v


class Memory:
    """High work RAM, big-endian longwords by byte address."""

    def __init__(self):
        self.words = {}

    def read(self, addr):
        return self.words.get(addr & 0x0FFFFFFC, 0)

    def write(self, addr, value):
        self.words[addr & 0x0FFFFFFC] = value & M32


class DSP:
    def __init__(self, program, memory, timing='saturn'):
        self.prog = program
        self.mem = memory
        self.timing = timing
        self.ram = [[0] * 64 for _ in range(4)]
        self.ct = [0] * 4
        self.ac = 0         # 48 bits (kept as a Python int, masked)
        self.p = 0
        self.rx = self.ry = 0
        self.ra0 = self.wa0 = 0
        self.top = 0
        self.lop = 0
        self.z = self.s = self.c = False
        self.cycles = 0     # DSP cycles
        self.stalls = 0     # of them, waiting on a DMA
        self.dma_until = 0
        self.dma_bank = None
        self.executed = 0
        self.counts = {}    # instructions executed by address

    # -- timing ---------------------------------------------------------------
    def busy(self):
        return self.cycles < self.dma_until

    def wait_dma(self):
        if self.busy():
            self.stalls += self.dma_until - self.cycles
            self.cycles = self.dma_until

    def touch(self, banks):
        if self.timing == 'saturn' and self.busy() and self.dma_bank in banks:
            self.wait_dma()

    # -- running --------------------------------------------------------------
    def run(self, pc, limit=10_000_000):
        self.pc = pc
        self.next_word, self.next_addr, self.next_looped = self.prog[pc], pc, False
        self.pc += 1
        while self.executed < limit:
            word, addr, looped = self.next_word, self.next_addr, self.next_looped
            if not looped or self.lop == 0:
                self.next_addr = self.pc & 0xFF
                self.next_word, self.next_looped = self.prog[self.next_addr], False
                self.pc += 1
            if looped:
                self.lop = (self.lop - 1) & 0xFFF
            self.counts[addr] = self.counts.get(addr, 0) + 1
            self.executed += 1
            self.cycles += 1
            top = word >> 28
            if top <= 3:
                self.general(word)
            elif top in (8, 9, 10, 11):
                self.mvi(word)
            elif top == 0xC:
                self.dma(word)
            elif top == 0xD:
                if self.cond((word >> 19) & 0x7F):
                    self.pc = word & 0xFF
            else:
                op = (word >> 27) & 3
                if op in (2, 3):
                    self.wait_dma()
                    return
                if op == 0:     # BTM
                    if self.lop:
                        self.pc = self.top
                    self.lop = (self.lop - 1) & 0xFFF
                else:           # LPS
                    self.next_looped = True
        raise RuntimeError('instruction limit')

    def cond(self, c):
        if not c & 0x40:
            return True
        r = False
        if c & 1:
            r |= self.z
        if c & 2:
            r |= self.s
        if c & 4:
            r |= self.c
        if c & 8:
            r |= self.busy()
        return r == bool(c & 0x20)

    def general(self, w):
        alu_op = (w >> 26) & 0xF
        x_op = (w >> 23) & 7
        xs = (w >> 20) & 7
        y_op = (w >> 17) & 7
        ys = (w >> 14) & 7
        d1_op = (w >> 12) & 3
        d = (w >> 8) & 0xF
        d1s = w & 0xFF
        # the banks this instruction touches, for the Saturn's stall
        banks = set()
        if x_op >= 3:
            banks.add(xs & 3)
        if y_op >= 3:
            banks.add(ys & 3)
        if d1_op & 1:
            if d1_op & 2 and d1s <= 7:
                banks.add(d1s & 3)
            if d <= 3 or d >= 12:
                banks.add(d & 3)
        self.touch(banks)

        ac = self.ac & M48
        alu = ac
        lo, hi = ac & M32, ac & ~M32 & M48
        if alu_op == 1:
            lo &= self.p & M32
            self.flags32(lo)
            alu = hi | lo
        elif alu_op == 2:
            lo |= self.p & M32
            self.flags32(lo)
            alu = hi | lo
        elif alu_op == 3:
            lo ^= self.p & M32
            self.flags32(lo)
            alu = hi | lo
        elif alu_op == 4:
            t = lo + (self.p & M32)
            self.c = bool(t >> 32 & 1)
            self.flags32(t)
            alu = hi | (t & M32)
        elif alu_op == 5:
            t = lo - (self.p & M32)
            self.c = bool(t >> 32 & 1)
            self.flags32(t)
            alu = hi | (t & M32)
        elif alu_op == 6:
            t = (ac & M48) + (self.p & M48)
            self.c = bool(t >> 48 & 1)
            t &= M48
            self.s = bool(t >> 47 & 1)
            self.z = t == 0
            alu = t
        elif alu_op == 8:
            self.c = bool(lo & 1)
            lo = (s32(lo) >> 1) & M32
            self.flags32(lo)
            alu = hi | lo
        elif alu_op == 9:
            self.c = bool(lo & 1)
            lo = (lo >> 1) | (lo & 1) << 31
            self.flags32(lo)
            alu = hi | lo
        elif alu_op == 10:
            self.c = bool(lo >> 31)
            lo = (lo << 1) & M32
            self.flags32(lo)
            alu = hi | lo
        elif alu_op == 11:
            self.c = bool(lo >> 31)
            lo = ((lo << 1) | (lo >> 31)) & M32
            self.flags32(lo)
            alu = hi | lo
        elif alu_op == 15:
            self.c = bool(lo >> 24 & 1)
            lo = ((lo << 8) | (lo >> 24)) & M32
            self.flags32(lo)
            alu = hi | lo

        read = set()
        inc = [0] * 4
        if x_op & 3 == 2:
            self.p = (s32(self.rx) * s32(self.ry)) & 0xFFFFFFFFFFFFFFFF
        if x_op >= 3:
            b = xs & 3
            v = self.ram[b][self.ct[b]]
            read.add(b)
            if xs & 4:
                inc[b] = 1
            if x_op & 3 == 3:
                self.p = s32(v) & 0xFFFFFFFFFFFFFFFF
            if x_op & 4:
                self.rx = v
        if y_op & 3 == 1:
            self.ac = 0
        elif y_op & 3 == 2:
            self.ac = alu
        if y_op >= 3:
            b = ys & 3
            v = self.ram[b][self.ct[b]]
            read.add(b)
            if ys & 4:
                inc[b] = 1
            if y_op & 3 == 3:
                self.ac = s32(v) & M48
            if y_op & 4:
                self.ry = v
        if d1_op & 1:
            src = s8(w) & M32
            if d1_op & 2:
                if d1s in (8, 11, 12, 13, 14, 15):
                    src = M32
                elif d1s <= 3:
                    src = self.ram[d1s][self.ct[d1s]]
                    read.add(d1s)
                elif d1s <= 7:
                    b = d1s & 3
                    src = self.ram[b][self.ct[b]]
                    read.add(b)
                    if d != b:
                        inc[b] = 1
                elif d1s == 9:
                    src = alu & M32
                elif d1s == 10:
                    src = (alu >> 16) & M32
            if d <= 3:
                if d not in read:
                    self.ram[d][self.ct[d]] = src
                    inc[d] = 1
            elif d == 4:
                self.rx = src
            elif d == 5:
                self.p = s32(src) & 0xFFFFFFFFFFFFFFFF
            elif d == 6:
                self.ra0 = src
            elif d == 7:
                self.wa0 = src
            elif d == 10:
                self.lop = src & 0xFFF
            elif d == 11:
                self.top = src & 0xFF
            elif d >= 12:
                self.ct[d - 12] = src & 0x3F
                inc[d - 12] = 0
        for b in range(4):
            self.ct[b] = (self.ct[b] + inc[b]) & 0x3F

    def flags32(self, v):
        v &= M32
        self.s = bool(v >> 31)
        self.z = v == 0

    def mvi(self, w):
        dest = (w >> 26) & 0xF
        cond = (w >> 19) & 0x7F
        if cond & 0x40:
            imm = w & 0x7FFFF
            imm = imm - (1 << 19) if imm & 0x40000 else imm
        else:
            imm = w & 0x1FFFFFF
            imm = imm - (1 << 25) if imm & 0x1000000 else imm
        if not self.cond(cond):
            return
        imm &= M32
        if dest <= 3:
            self.touch({dest})
            self.ram[dest][self.ct[dest]] = imm
            self.ct[dest] = (self.ct[dest] + 1) & 0x3F
        elif dest == 4:
            self.rx = imm
        elif dest == 5:
            self.p = s32(imm) & 0xFFFFFFFFFFFFFFFF
        elif dest == 6:
            self.ra0 = imm
        elif dest == 7:
            self.wa0 = imm
        elif dest == 10:
            self.lop = imm & 0xFFF
        elif dest == 12:
            self.top = (self.pc - 1) & 0xFF
            self.pc = imm & 0xFF

    def dma(self, w):
        hold = bool(w >> 14 & 1)
        fmt = bool(w >> 13 & 1)
        direction = (w >> 12) & 1
        ram = (w >> 8) & 7
        self.wait_dma()     # one transfer at a time (both ways)
        if fmt:
            b = w & 3
            count = self.ram[b][self.ct[b]] & 0xFF
            if w & 4:
                self.ct[b] = (self.ct[b] + 1) & 0x3F
        else:
            count = w & 0xFF
        count = count or 256
        b = ram & 3
        if direction:
            addr = (self.wa0 << 2) & 0x07FFFFFF
            for _ in range(count):
                self.mem.write(addr, self.ram[b][self.ct[b]])
                self.ct[b] = (self.ct[b] + 1) & 0x3F
                addr += 4
            if not hold:
                self.wa0 = (addr + 2) >> 2
            cost = (31 + 3.1 * count) / 2 if self.timing == 'saturn' else count
        else:
            addr = (self.ra0 << 2) & 0x07FFFFFF
            for _ in range(count):
                self.ram[b][self.ct[b]] = self.mem.read(addr)
                self.ct[b] = (self.ct[b] + 1) & 0x3F
                addr += 4
            if not hold:
                self.ra0 = addr >> 2
            cost = (29 + 1.0 * count) / 2 if self.timing == 'saturn' else count
        self.dma_until = self.cycles + cost
        self.dma_bank = b


def load(path):
    words, labels = assemble(open(path).read())
    return words + [0] * (256 - len(words)), labels
