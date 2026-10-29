"""Runs the clean-room renderer's lab (make lab) in a Mednafen of its own and
reads back what it measured.

    lab.py [--cue build/lab/disc/tomb.cue] [--scene ledge] [--shots] [--profile]

Boots the lab, waits for its measurements (gLab), and prints its results
page as the Saturn shows it. Then checks the lab's command list against the
game's own (the scene's CMDS and GRDS) word by word as VDP1 reads them, and
draws both with the Mednafen fork's VDP1 code (tools/crunch/vdp1lib) over
the scene's VDP1 VRAM: the pixels that differ, and VDP1's time for each.
With --shots, the two frame pages as the emulator shows them, into
build/lab/shots. With --profile, each measurement's first run traced
instruction by instruction (lab.cpp labTraceBegin to labTraceEnd) and its
cycles shared out by function and, in assembly, by the label they follow:
proportions, since tracing slows Mednafen's CPU. Mednafen's times are
Mednafen's: the Saturn's are the photo of the lab's results page.
"""
import argparse
import bisect
import os
import re
import struct
import subprocess
import sys
import time
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capture as cap   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB_MAGIC = 0x4C414231
LAB_RESULTS = struct.Struct('>7I4i4I')      # lab.cpp LabResults
FIELDS = ('magic', 'facesMeshes', 'facesRooms', 'flush', 'splitMaster', 'splitSlave', 'floor',
          'facesBad', 'flushBad', 'splitBad', 'floorBad', 'vdp1Game', 'vdp1Lab', 'meshFacesGiven',
          'roomFacesGiven')
TEXT_ROWS, TEXT_COLUMNS = 26, 42
PAGE = struct.Struct(f'>{TEXT_ROWS * (TEXT_COLUMNS + 1)}s2xi')     # lab.h Page
VRAM_FRAME_CMDS = 0x60
VRAM_GOURAUD_BASE = VRAM_FRAME_CMDS + 1792 * 32
VDP1_HZ = 28636364
CMD_SKIP, CMD_SCALED_SPRITE, CMD_POLYGON, PMOD_GOURAUD = 0x4000, 0x0001, 0x0004, 0x0004
SEGMENTS = ('mesh faces', 'room faces', 'flush split', 'flush alone', 'floor')     # lab.cpp's measure()s
MARK_BREAK = 4      # lab.cpp labTraceBegin, labTraceEnd: their third instruction
ADDR2LINE = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-addr2line')


def chunks(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, count = struct.unpack('>II', data[:8])
    out = {}
    for i in range(count):
        cid, offset, size, _ = struct.unpack('>4sIII', data[8 + 16 * i:24 + 16 * i])
        out[cid.decode()] = data[offset:offset + size]
    return out


def matches(a, ga, b, gb):
    """lab/game.cpp gameCommandMatches: a command as VDP1 reads it."""
    if a[0] != b[0]:
        return False
    ctrl = b[0] >> 16
    if ctrl & CMD_SKIP:
        return True
    kind = ctrl & 15
    words = [1, 3, 5, 7] + ([2] if kind < CMD_POLYGON else []) + ([4, 6] if kind != CMD_SCALED_SPRITE else [])
    if any(a[k] != b[k] for k in words):
        return False
    return not (b[1] >> 16) & PMOD_GOURAUD or ga == gb


def draw(vram, cmds, grds):
    """The list drawn over the VRAM image: the frame and VDP1's cycles."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'crunch'))
    from vdp1 import Vdp1, rgb
    image = bytearray(vram)
    image[VRAM_FRAME_CMDS:VRAM_FRAME_CMDS + len(cmds)] = cmds
    image[VRAM_GOURAUD_BASE:VRAM_GOURAUD_BASE + len(grds)] = grds
    v = Vdp1()
    v.load_vram(bytes(image))
    totals, _ = v.plot()
    return rgb(v.fb), totals.cycles


def trace(med, syms, folder):
    """Each measurement's first run, traced: a unified trace file each."""
    begin = cap.symbol(syms, '_labTraceBegin') + MARK_BREAK
    end = cap.symbol(syms, '_labTraceEnd') + MARK_BREAK
    paths = []
    for _ in SEGMENTS:
        med.send(f'breakpoint {begin:08x}', 'ok breakpoint', 10)
        med.send('run', f'addr=0x{begin:08X}', 600)
        med.send(f'breakpoint_remove {begin:08x}', 'ok breakpoint_remove', 10)
        path = os.path.join(folder, f'trace{len(paths)}.txt').replace(os.sep, '/')
        med.send(f'unified_trace {path}', 'ok unified_trace', 10)
        med.send('insn_trace_unified 3 400000000', 'ok insn_trace_unified', 10)
        med.send(f'breakpoint {end:08x}', 'ok breakpoint', 10)
        med.send('run', f'addr=0x{end:08X}', 3600)
        med.send('insn_trace_stop', 'ok insn_trace_stop', 60)
        med.send('unified_trace_stop', 'ok unified_trace_stop', 60)
        med.send(f'breakpoint_remove {end:08x}', 'ok breakpoint_remove', 10)
        paths.append(path)
    return paths


class Places:
    """Code addresses to names: a C function's, or in assembly the label the
    instruction follows (a macro's own name inside one)."""

    def __init__(self, elf, syms):
        self.elf = elf
        funcs = sorted((a, n) for n, places in syms.items() for a, size in places)
        self.starts = [a for a, _ in funcs]
        self.names = [n for _, n in funcs]
        self.sources = {}
        self.lines = {}

    def function(self, pc):
        i = bisect.bisect_right(self.starts, pc) - 1
        return self.names[i] if i >= 0 else '?'

    def labels(self, path):
        """A source's labels and macros, by line."""
        if path not in self.sources:
            marks = []
            try:
                with open(path, errors='replace') as f:
                    for n, line in enumerate(f, 1):
                        m = re.match(r'\s*\.macro\s+(\w+)', line)
                        if m:
                            marks.append((n, 'macro ' + m.group(1)))
                        elif re.match(r'\s*\.endm', line):
                            marks.append((n, None))
                        else:
                            m = re.match(r'([A-Za-z_.][\w.]*):', line)
                            if m:
                                marks.append((n, m.group(1)))
            except OSError:
                pass
            self.sources[path] = marks
        return self.sources[path]

    def names_of(self, pcs):
        out = {}
        pcs = sorted(pcs)
        lines = subprocess.run([ADDR2LINE, '-e', self.elf] + [f'{pc:x}' for pc in pcs],
                               capture_output=True, text=True).stdout.splitlines()
        for pc, where in zip(pcs, lines):
            name = self.function(pc)
            path, _, line = where.rpartition(':')
            if path.endswith('.s') and line.isdigit():
                # a macro's instructions are its use's line
                label = None
                for n, mark in self.labels(path):
                    if n > int(line):
                        break
                    label = mark
                text = self.text(path, int(line))
                name = f'{os.path.basename(path)}:{line} {label or name}: {text}'
            out[pc] = name
        return out

    def text(self, path, line):
        if path not in self.lines:
            with open(path, errors='replace') as f:
                self.lines[path] = f.read().splitlines()
        source = self.lines[path]
        return source[line - 1].split('!')[0].strip() if line <= len(source) else ''



def profile(paths, elf, syms):
    places = Places(elf, syms)
    for title, path in zip(SEGMENTS, paths):
        cost = defaultdict(int)
        prev = {}
        with open(path, errors='replace') as f:
            for line in f:
                p = line.split(None, 3)
                if len(p) < 3 or p[1] not in ('m', 's'):
                    continue
                t, cpu, pc = int(p[0]), p[1], int(p[2], 16) & 0x07FFFFFF
                if cpu in prev:
                    ppc, pt = prev[cpu]
                    if 0 <= t - pt < 100000:
                        cost[(cpu, ppc)] += t - pt
                prev[cpu] = (pc, t)
        names = places.names_of({pc for _, pc in cost})
        blocks = defaultdict(int)
        total = defaultdict(int)
        for (cpu, pc), c in cost.items():
            blocks[(cpu, names[pc])] += c
            total[cpu] += c
        print(f'\n{title}: ' + ', '.join(f'{"master" if c == "m" else "slave"} {total[c]} cycles'
                                         for c in sorted(total)))
        for (cpu, name), c in sorted(blocks.items(), key=lambda kv: -kv[1]):
            share = c / total[cpu]
            if share >= 0.005:
                print(f'  {cpu} {100 * share:5.1f}%  {name}')
        os.remove(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cue', default=os.path.join(ROOT, 'build/lab/disc/tomb.cue'))
    ap.add_argument('--scene', default='ledge', help="the disc's scene (make lab LAB_SCENE=...)")
    ap.add_argument('--shots', action='store_true')
    ap.add_argument('--profile', action='store_true')
    args = ap.parse_args()

    elf = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(args.cue))), '0MAIN.elf')
    syms = cap.symbols(elf)
    lab = cap.symbol(syms, '_gLab')
    page = cap.symbol(syms, '__ZL8gResults')
    commands = cap.symbol(syms, '_gCmds')
    gouraud = cap.symbol(syms, '_gGouraud')
    end_at = cap.symbol(syms, '__ZL7gLabEnd')

    med = cap.Mednafen(os.path.abspath(args.cue), 'lab')
    try:
        med.send('deterministic', 'deterministic', 10)
        traces = trace(med, syms, os.path.join(ROOT, 'build/lab')) if args.profile else []
        results = None
        start = time.time()
        for _ in range(100):
            med.send('frame_advance 60', 'done frame_advance', 120)
            results = dict(zip(FIELDS, LAB_RESULTS.unpack(med.read(lab, LAB_RESULTS.size))))
            if results['magic'] == LAB_MAGIC:
                break
        else:
            sys.exit('the lab never finished measuring')
        frame = int(med.send('status', 'status', 10).split('frame=')[1].split()[0])
        print(f'measured by vblank {frame} ({time.time() - start:.0f} s); off: faces {results["facesBad"]}, '
              f'flush {results["flushBad"]}, split {results["splitBad"]}, floor {results["floorBad"]}')
        text, count = PAGE.unpack(med.read(page, PAGE.size))
        for r in range(count):
            row = text[r * (TEXT_COLUMNS + 1):(r + 1) * (TEXT_COLUMNS + 1)]
            print('  ' + row.split(b'\0')[0].decode('ascii', 'replace'))
        end = struct.unpack('>i', med.read(end_at, 4))[0]
        lab_cmds = med.read(commands, (end + 1) * 32)
        lab_grds = med.read(gouraud, end * 8)
        if args.shots:
            shots = os.path.join(ROOT, 'build/lab/shots')
            os.makedirs(shots, exist_ok=True)
            for name in ('game', 'lab'):
                med.send('input R', 'ok input', 10)
                med.send('frame_advance 10', 'done frame_advance', 30)
                med.send('input_release R', 'ok input_release', 10)
                med.send('frame_advance 20', 'done frame_advance', 30)
                shot = os.path.join(shots, f'{name}.png')
                if os.path.exists(shot):
                    os.remove(shot)
                med.send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30)
                time.sleep(0.5)
                print(f'-> {os.path.normpath(shot)}')
    finally:
        med.quit()

    # the lab's list against the game's
    import numpy as np
    scene = chunks(os.path.join(ROOT, 'build/lab', args.scene.upper() + '.SCN'))
    ref_cmds, ref_grds = scene['CMDS'], scene['GRDS']
    want = len(ref_grds) // 8
    bad = []
    for i in range(1, min(end, want)):
        a = struct.unpack('>8I', lab_cmds[32 * i:32 * i + 32])
        b = struct.unpack('>8I', ref_cmds[32 * i:32 * i + 32])
        ga = struct.unpack('>2I', lab_grds[8 * i:8 * i + 8])
        gb = struct.unpack('>2I', ref_grds[8 * i:8 * i + 8])
        if not matches(a, ga, b, gb):
            if not bad:
                print(f'command {i}: lab {[hex(w) for w in a]} {[hex(g) for g in ga]}')
                print(f'       game {[hex(w) for w in b]} {[hex(g) for g in gb]}')
            bad.append(i)
    print(f'lab list: {end} commands (game {want}); {len(bad)} differ from the game\'s'
          + (f': {bad[:10]}' if bad else ''))

    game_frame, game_cycles = draw(scene['VRAM'], ref_cmds, ref_grds)
    lab_frame, lab_cycles = draw(scene['VRAM'], lab_cmds, lab_grds)
    differ = int(np.any(game_frame != lab_frame, axis=2).sum())
    print(f'drawn (Mednafen VDP1): {differ} pixels differ; VDP1 game {game_cycles * 1000 / VDP1_HZ:.2f} ms, '
          f'lab {lab_cycles * 1000 / VDP1_HZ:.2f} ms')
    if traces:
        profile(traces, elf, syms)
    sys.exit(1 if bad or differ or results['facesBad'] or results['flushBad'] or results['splitBad'] else 0)


if __name__ == '__main__':
    main()
