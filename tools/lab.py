"""Runs the clean-room renderer's lab (make lab) in a Mednafen of its own and
reads back what it measured.

    lab.py [--cue build/lab/disc/tomb.cue] [--scene ledge] [--shots] [--profile]

Boots the lab, waits for its measurements (gLab), and prints its results
pages as the Saturn shows them. Then follows the new way's command list as
VDP1 does, checking it command by command against the game's own (the
scene's CMDS and GRDS) as VDP1 reads them, and draws both with the Mednafen
fork's VDP1 code (tools/crunch/vdp1lib) over the scene's VDP1 VRAM: the
pixels that differ, and VDP1's time for each. With --shots, the two frame
pages as the emulator shows them, into build/lab/shots. With --profile, each measurement's first run traced
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
MAX_CMDS = 1792
SHADE_CACHE = struct.Struct('>4I256I256I')      # game.h ShadeCache
SHADE_TABLE_SIZE = 32
LAB_RESULTS = struct.Struct('>7I4i4I5I6i3I2I3iIiIiIiIIiIii5Ii3I5IiI3iI2iIiI2iI3iI2iI3iI2i5I2iIiII2i4I')    # lab.cpp LabResults
FIELDS = ('magic', 'facesMeshes', 'facesRooms', 'flush', 'splitMaster', 'splitSlave', 'floor',
          'facesBad', 'flushBad', 'splitBad', 'floorBad', 'vdp1Game', 'vdp1Lab', 'meshFacesGiven',
          'roomFacesGiven', 'meshFlush', 'roomFlush', 'newMeshes', 'newRooms', 'newJoin', 'newBad', 'newBadC',
          'newVisited', 'newShaded', 'newAsmBad', 'newFrames', 'meshCommands', 'roomCommands', 'vdp1New',
          'vertsRooms', 'vertsMeshes', 'vertsBad', 'roomVertices', 'meshVertices', 'newVertsRooms',
          'newVertsBad', 'newVertsMeshes', 'newVertsSeldom', 'dspRooms', 'dspRoomsBad',
          'newVertsFeed', 'dspMeshes', 'dspMeshesBad', 'dspMeshes3', 'dspMeshes3Bad', 'feedBad',
          'together', 'togetherMaster', 'togetherMasterWait', 'togetherSlave', 'togetherSlaveWait',
          'togetherBad', 'togetherDspRooms', 'togetherDspGap', 'togetherDspMeshes',
          'stages', 'stagesMaster', 'stagesMasterWait', 'stagesSlave', 'stagesSlaveWait', 'stagesBad',
          'pose', 'poseBad', 'poseMatrices', 'poseItems', 'poseNew', 'poseNewBad', 'poseSeldom',
          'light', 'lightBad', 'lightNew', 'lightNewBad', 'lightSeldom',
          'statics', 'staticsBad', 'staticCount', 'shadowCount', 'staticsNew', 'staticsNewBad', 'staticsSeldom',
          'rooms', 'roomsBad', 'roomsSeen', 'roomsVoids', 'roomsNew', 'roomsNewBad', 'roomsSeldom',
          'frame', 'frameMaster', 'frameMasterWait', 'frameSlave', 'frameSlaveWait', 'frameBad', 'orderOff',
          'draws', 'drawsBad', 'drawCalls', 'drawsNew', 'drawsNewBad', 'drawsSeldom', 'arenaUsed', 'arenaSize',
          'lowUsed', 'lowSize')
TEXT_ROWS, TEXT_COLUMNS = 26, 42
PAGE = struct.Struct(f'>{TEXT_ROWS * (TEXT_COLUMNS + 1)}s2xi')     # lab.h Page
VRAM_FRAME_CMDS = 0x60
VRAM_GOURAUD_BASE = VRAM_FRAME_CMDS + 1792 * 32
VDP1_HZ = 28636364
CMD_SKIP, CMD_SCALED_SPRITE, CMD_POLYGON, CMD_END, PMOD_GOURAUD = 0x4000, 0x0001, 0x0004, 0x8000, 0x0004
JUMPS = 0x7000
SEGMENTS = ('pose', 'light', 'rooms seen', 'statics', 'new: light', 'new: pose', 'new: statics', 'new: rooms seen',
            'draw list', 'new: draw list',
            'room vertices', 'mesh vertices', 'new: room vertices', 'new: feeding', 'new: mesh vertices', 'mesh faces', 'room faces', 'flush split', 'flush alone', 'floor', "flush of the master's faces",
            "flush of the rooms' faces", 'new: rooms', "new: the master's draws", 'new: join')  # lab.cpp's measure()s
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


SAME_NOT, SAME, SAME_PICTURE = 0, 1, 2


def shaded_alike(copy_w1, w1, grd, keys):
    """lab/game.cpp shadedAlike: drawn through a shaded copy of a colour
    table, and with gouraud of the copy's one value at every corner."""
    key = keys(copy_w1 & 0xFFFF)
    g = grd[0] >> 16
    calc = 0x0006 << 16
    return bool(key) and w1 & calc == PMOD_GOURAUD << 16 and copy_w1 & 0xFFFF0000 == w1 & 0xFFFF0000 & ~calc \
        and grd[0] == (g << 16 | g) and grd[1] == grd[0] and key == ((w1 & 0xFFFF) << 15 | g)


def same(a, ga, b, gb, keys):
    """lab/game.cpp sameCommand: a command as VDP1 reads it, jumps aside, a
    shaded copy of a colour table as good as another of the same (SAME), or
    as gouraud of its value at every corner (SAME_PICTURE); keys: the copies'
    keys in the caches a's list left, and b's."""
    ca, cb = a[0] >> 16, b[0] >> 16
    if ca & ~JUMPS != cb & ~JUMPS or ca & CMD_SKIP != cb & CMD_SKIP:
        return SAME_NOT
    if cb & CMD_SKIP:
        return SAME
    kind = cb & 15
    words = [3, 5] + ([2] if kind < CMD_POLYGON else []) + ([4, 6] if kind != CMD_SCALED_SPRITE else [])
    if any(a[k] != b[k] for k in words):
        return SAME_NOT
    if a[1] != b[1]:
        key = keys[0](a[1] & 0xFFFF)
        if a[1] >> 16 == b[1] >> 16 and key and key == keys[1](b[1] & 0xFFFF):
            return SAME
        if shaded_alike(a[1], b[1], gb, keys[0]) or (ga and shaded_alike(b[1], a[1], ga, keys[1])):
            return SAME_PICTURE
        return SAME_NOT
    return SAME if not (b[1] >> 16) & PMOD_GOURAUD or ga == gb else SAME_NOT


def copies(cmds, keys):
    """Of a list's commands, those drawn through a shaded copy of a colour
    table."""
    return sum(1 for w in cmds if (w[0] >> 16) & 0xF < CMD_POLYGON and not (w[1] >> 16) & PMOD_GOURAUD
               and keys(w[1] & 0xFFFF))


def cache_keys(caches):
    """A colour table address / 8 to the key of the shaded copy there in the
    caches (two ShadeCaches), or 0."""
    cache = [SHADE_CACHE.unpack_from(caches, i * SHADE_CACHE.size) for i in range(2)]

    def keys(colr):
        address = colr << 3
        for c in cache:
            vram = c[2]
            if vram and vram <= address < vram + 256 * SHADE_TABLE_SIZE:
                return c[4 + (address - vram) // SHADE_TABLE_SIZE]
        return 0
    return keys


def follow(cmds, grds, ref_cmds, ref_grds, keys):
    """The list from slot 0 as VDP1 follows its jumps, against the game's
    sequential one: the commands followed and those that differ. Of the
    first that differs, where each list has the other's command. The
    commands of each drawn through shaded copies."""
    count = len(ref_grds) // 8
    slot, bad, other, seq, followed = 0, [], 0, [], 0
    for k in range(count + 1):
        if not 0 <= slot < len(cmds) // 32:
            bad += list(range(k, count + 1))
            break
        a = struct.unpack_from('>8I', cmds, 32 * slot)
        followed = k + 1
        if k == count:
            bad += [] if a[0] >> 16 & CMD_END else [k]
            break
        if a[0] >> 16 & CMD_END:
            bad += list(range(k, count + 1))
            break
        b = struct.unpack_from('>8I', ref_cmds, 32 * k)
        ga = struct.unpack_from('>2I', grds, 8 * slot) if 8 * slot + 8 <= len(grds) else None
        gb = struct.unpack_from('>2I', ref_grds, 8 * k)
        seq.append((a, ga))
        alike = same(a, ga, b, gb, keys)
        other += alike == SAME_PICTURE
        if alike == SAME_NOT:
            if not bad:
                print(f'command {k} (slot {slot}): new {[hex(w) for w in a]} {ga and [hex(g) for g in ga]}')
                print(f'               game {[hex(w) for w in b]} {[hex(g) for g in gb]}')
            bad.append(k)
        jump = (a[0] >> 28) & 7
        slot = ((a[0] & 0xFFFF) - (VRAM_FRAME_CMDS >> 3)) // 4 if jump in (1, 5) else slot + 1
    if bad and bad[0] < len(seq):
        first = bad[0]
        b = struct.unpack_from('>8I', ref_cmds, 32 * first)
        gb = struct.unpack_from('>2I', ref_grds, 8 * first)
        mine = [j for j, (a, ga) in enumerate(seq) if same(a, ga, b, gb, keys) != SAME_NOT]
        a, ga = seq[first]
        theirs = [i for i in range(count) if same(a, ga, struct.unpack_from('>8I', ref_cmds, 32 * i),
                                                  struct.unpack_from('>2I', ref_grds, 8 * i), keys) != SAME_NOT]
        print(f"  the game's command {first} is the new list's {mine or 'none'}; "
              f"the new list's is the game's {theirs or 'none'}")
    shaded = (copies([a for a, _ in seq], keys[0]),
              copies([struct.unpack_from('>8I', ref_cmds, 32 * i) for i in range(count)], keys[1]))
    return followed, bad, other, shaded


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
    instruction follows (a macro's own name inside one, or the local label
    inside it, the lab's assembly keeping them: make lab)."""

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
                if name.startswith('.L'):
                    label = name
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
    pages = [cap.symbol(syms, '__ZL8gResults'), cap.symbol(syms, '__ZL9gPosePage'),
             cap.symbol(syms, '__ZL13gVerticesPage'), cap.symbol(syms, '__ZL11gNewResults'),
             cap.symbol(syms, '__ZL10gFramePage')]
    commands = cap.symbol(syms, '__ZL8gNewCmds')
    gouraud = cap.symbol(syms, '__ZL7gNewGrd')
    end_at = cap.symbol(syms, '__ZL7gNewEnd')
    caches_at = cap.symbol(syms, '__ZL12gShadeCaches')
    walk_at = cap.symbol(syms, '__ZL9gWalkList')     # lab.cpp: game.h GameList

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
              f'flush {results["flushBad"]}, split {results["splitBad"]}, floor {results["floorBad"]}; '
              f'high work RAM arena {results["arenaUsed"] / 1024:.1f} of {results["arenaSize"] / 1024:.0f} KB, '
              f'low work RAM {results["lowUsed"] / 1024:.1f} of {results["lowSize"] / 1024:.0f} KB')
        for page in pages:
            text, count = PAGE.unpack(med.read(page, PAGE.size))
            for r in range(count):
                row = text[r * (TEXT_COLUMNS + 1):(r + 1) * (TEXT_COLUMNS + 1)]
                print('  ' + row.split(b'\0')[0].decode('ascii', 'replace'))
            print()
        end = struct.unpack('>i', med.read(end_at, 4))[0]
        new_cmds = med.read(commands, MAX_CMDS * 32)
        new_grds = med.read(gouraud, MAX_CMDS * 8)
        caches = med.read(struct.unpack('>I', med.read(caches_at, 4))[0], 2 * SHADE_CACHE.size)
        walk_cmds_at, walk_grds_at, walk_end = struct.unpack('>IIi', med.read(walk_at, 12))
        walk_caches = med.read(walk_at + 12, 2 * SHADE_CACHE.size)
        walk_cmds = med.read(walk_cmds_at, (walk_end + 1) * 32)
        walk_grds = med.read(walk_grds_at, walk_end * 8)
        vram = med.read(cap.VDP1_VRAM, cap.VDP1_BYTES)
        if args.shots:
            shots = os.path.join(ROOT, 'build/lab/shots')
            os.makedirs(shots, exist_ok=True)
            for name in ('', '', '', '', 'game', 'new'):
                med.send('input R', 'ok input', 10)
                med.send('frame_advance 10', 'done frame_advance', 30)
                med.send('input_release R', 'ok input_release', 10)
                med.send('frame_advance 20', 'done frame_advance', 30)
                if not name:
                    continue    # the new way's results pages
                shot = os.path.join(shots, f'{name}.png')
                if os.path.exists(shot):
                    os.remove(shot)
                med.send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30)
                time.sleep(0.5)
                print(f'-> {os.path.normpath(shot)}')
    finally:
        med.quit()

    # the game's list with its rooms in the walk's order (the new way's)
    # against the capture's, and the new list against it, as VDP1 follows them
    import numpy as np
    scene = chunks(os.path.join(ROOT, 'build/lab', args.scene.upper() + '.SCN'))
    game_cmds, game_grds = scene['CMDS'], scene['GRDS']
    copies = lambda caches: [(c[2],) + c[4:4 + 256] for c in
                             (SHADE_CACHE.unpack_from(caches, i * SHADE_CACHE.size) for i in range(2))]
    if copies(walk_caches) != copies(scene['SHCA']):
        print('the game\'s list in walk order made shaded copies the capture lacks: its picture is not exact')
    followed, moved, _, _ = follow(walk_cmds, walk_grds, game_cmds, game_grds,
                                   (cache_keys(walk_caches), cache_keys(scene['SHCA'])))
    print(f'the game\'s list, rooms in walk order: {followed} commands followed (the capture\'s '
          f'{len(game_grds) // 8 + 1}); {len(moved)} not the capture\'s in that place')
    keys = (cache_keys(caches), cache_keys(walk_caches))
    followed, bad, other, shaded = follow(new_cmds, new_grds, walk_cmds, walk_grds, keys)
    print(f'new list: {followed} commands followed (the game\'s in walk order {walk_end + 1}); '
          f'{len(bad)} differ from it' + (f': {bad[:10]}' if bad else '')
          + f'; {other} shaded another way (a shaded copy of a colour table for gouraud of one value); '
          f'drawn through shaded copies: new {shaded[0]}, game {shaded[1]}; the new way\'s frames to settle '
          f'{results["newFrames"]}')

    # each drawn over VDP1 VRAM with its own shaded copies: the game's as the
    # scene has them, the new way's as the lab left them
    game_frame, game_cycles = draw(scene['VRAM'], game_cmds, game_grds)
    walk_frame, walk_cycles = draw(scene['VRAM'], walk_cmds, walk_grds)
    new_frame, new_cycles = draw(vram, new_cmds[:(end + 1) * 32], new_grds[:end * 8])
    order = int(np.any(game_frame != walk_frame, axis=2).sum())
    differ = int(np.any(walk_frame != new_frame, axis=2).sum())
    print(f'drawn (Mednafen VDP1): new against the game\'s in walk order {differ} pixels differ; the room '
          f'order itself moves {order} pixels; VDP1 game {game_cycles * 1000 / VDP1_HZ:.2f} ms, in walk order '
          f'{walk_cycles * 1000 / VDP1_HZ:.2f}, new {new_cycles * 1000 / VDP1_HZ:.2f} ms')
    if traces:
        profile(traces, elf, syms)
    sys.exit(1 if bad or differ or any(results[k] for k in ('poseBad', 'lightBad', 'poseNewBad', 'poseSeldom', 'lightNewBad', 'lightSeldom', 'staticsBad',
                                              'staticsNewBad', 'staticsSeldom', 'roomsBad', 'roomsNewBad', 'drawsBad', 'drawsNewBad',
                                              'drawsSeldom',
                                              'roomsSeldom', 'vertsBad', 'newVertsBad', 'newVertsSeldom', 'dspRoomsBad', 'dspMeshesBad', 'dspMeshes3Bad', 'feedBad', 'togetherBad', 'stagesBad', 'frameBad', 'facesBad', 'flushBad', 'splitBad', 'newBad', 'newBadC', 'newAsmBad'))
             else 0)


if __name__ == '__main__':
    main()
