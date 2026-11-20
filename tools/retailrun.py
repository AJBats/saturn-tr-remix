"""A tick replay (tools/replay.py, the playtest bot's) played by retail TR1 in Mednafen,
a tick at a time, beside the host build playing it: the first tick where Lara or the
control seed part, or that they ran alike.

    python -I tools/retailrun.py REPLAY OUT.txt [--ticks N] [--items N]

OUT.txt gets retail's records as host/host.cpp's REC lines (tools/recdiff.py compares two
such files); OUT_host.txt the host's for the same ticks.

Retail's S_UpdateInput, called once a tick by ControlPhase, reaches the pad scan through
the pool word at 0x06037644; that word is turned to tools/retailpad.s (assembled with the
pinned toolchain) at STUB. For a game tick it feeds pad 1 from the tick table, one entry a
tick, and keeps Lara's item and the seed in a ring; for a frame of the inventory's own loop
(retail's ring is modal, an input a frame and its animation by the vblanks gone) it feeds
the inventory script (so does the level's statistics screen). The host runs the inventory
and the statistics as ticks with the game held: those ticks leave the tick table, and their
presses, spaced out, make the script; a run of routes chained (host/bot.h) is followed from
level to level. Armed at the first ControlPhase of the replay's level, reached from boot by
the title's menus (a new game: the Caves only, so far). --items N holds the game before the tick after the replay's last and
prints N items. build/retail_clean_sav holds retail's backup memory files from a clean boot (a run's
Mednafen home's sav/ before any run saved a game): each run starts from them."""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, 'tools')
sys.path.insert(0, HERE)
from capture import Mednafen  # noqa: E402

CUE = ROOT + '/external_resources/tomb_raider_usa/Tomb Raider (USA).cue'
BIN = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/'
# Low work RAM retail leaves alone: its bottom. The inventory ring, opened in a
# level, writes 0x00200000..0x00200100; a level's data fills it from higher up
# down (the Caves from 0x00245d10, Lost Valley lower: it wrote over a tick
# table that reached 0x00245c00). Everything here is below 0x0020e000, zero in
# Lost Valley; the tick table is a window the driver keeps filled ahead.
STUB = 0x00201000       # to 0x00201400, its vars in it
RING = 0x00201400       # 256 entries of 96 bytes, to 0x00207400
SCRIPT = 0x00207400     # to 0x0020b400
TABLE = 0x0020B400      # to 0x0020d400: entry i of the tick table at i % WINDOW
WINDOW = 4096
OURS_END = TABLE + WINDOW * 2
MAX_SCRIPT = (TABLE - SCRIPT) // 2
RING_SIZE = 256
POOL = 0x06037644       # S_UpdateInput's pool word for the pad scan
CONTROL_PHASE = 0x0600F66C
LEVEL = 0x06060CB4
ENTRY = 96
NOTHING = 0xFFFF
OPEN_WAIT, PRESS_GAP = 40, 30   # inventory frames before the first press, between presses

PAD = {'UP': 0x1000, 'DOWN': 0x2000, 'LEFT': 0x4000, 'RIGHT': 0x8000, 'START': 0x0800, 'A': 0x0400, 'C': 0x0200,
       'B': 0x0100, 'R': 0x0080, 'X': 0x0040, 'Y': 0x0020, 'Z': 0x0010, 'L': 0x0008}
LEVELS = {'LEVEL1': 1, 'LEVEL2': 2, 'LEVEL3A': 3, 'LEVEL3B': 4}
# Retail's own bugs we keep the fixed way (PC TR1, TRX), patched out of retail so a run
# can be followed past them: (address, bytes, why)
PATCHES = [
    (0x0603047C, bytes([0x40, 0x00]), 'dart emitter: darts from one facing +x start 412 behind it, as on PC (retail tests 0x3fec)'),
]
TITLE_TO_NEW_GAME = ['2400', '+start', '2700', '+start', '3600', '+a', '4100', '+start', '4400', '+a', '4600', '+a',
                     '5000', '+start']
ITEMS = 0x06072BDC     # the items' base (a pointer)
FIELDS = ('room', 'x', 'y', 'z', 'yaw', 'anim', 'frame', 'health', 'seed')


def read_replay(path):
    keys = []
    level = None
    for line in open(path):
        w = line.split('#')[0].split()
        if not w:
            continue
        if w[0] == 'start':
            level = w[1]
            continue
        ticks, names = int(w[0]), w[1]
        bits = 0
        if names != '-':
            for n in names.split('+'):
                bits |= PAD[n]
        keys += [NOTHING ^ bits] * ticks
    return level, keys


def host_records(replay, ticks):
    """The host's record after each tick: {tick: dict}, inv the inventory open after it."""
    rel = os.path.relpath(replay, ROOT).replace('\\', '/')
    env = dict(os.environ, REPLAY=rel, REC='1', MSYS_NO_PATHCONV='1', WSLENV='REPLAY:REC')
    out = subprocess.run(['wsl.exe', '-d', 'Ubuntu-24.04', '--cd', ROOT.replace('\\', '/'), '--', 'qemu-ppc',
                          'build/host/logic', 'build/data', '-', str(ticks)], env=env, capture_output=True, text=True)
    if out.returncode:
        sys.exit(f'host: {out.stderr.strip()}')
    recs = {}
    for line in out.stdout.splitlines():
        w = line.split()
        if w and w[0] == 'rec':
            recs[int(w[1])] = dict(level=int(w[3]), room=int(w[5]), x=int(w[7]), y=int(w[8]), z=int(w[9]), yaw=int(w[11]),
                                   anim=int(w[13]), frame=int(w[15]), health=int(w[17]), seed=int(w[19], 16),
                                   inv=int(w[23]) if len(w) > 23 else 0)
    return recs


def split_inventory(keys, host):
    """The ticks retail plays as game ticks (those the host starts and ends with the
    inventory open leave), and the inventory script: each stay's presses, spaced. A
    press that shuts it (the host's tick that shuts it is also a game tick) goes to the
    script too, the tick's own entry left with nothing held (keys is changed)."""
    kept, script = [], []
    t = 0
    while t < len(keys):
        if t > 0 and host[t - 1]['inv'] and host[t]['inv']:
            # a stay in the inventory: its presses (a run of the same keys held is one)
            stay = []
            while t < len(keys) and host[t - 1]['inv'] and host[t]['inv']:
                stay.append(keys[t])
                t += 1
            if t < len(keys) and keys[t] != NOTHING:
                stay.append(keys[t])
                keys[t] = NOTHING
            presses = [k for i, k in enumerate(stay) if k != NOTHING and (i == 0 or stay[i - 1] != k)]
            script += [NOTHING] * OPEN_WAIT
            for k in presses:
                script += [k] + [NOTHING] * PRESS_GAP
            script.append(0)        # the end of this stay
            continue
        kept.append(t)
        t += 1
    script.append(1)                # the end of them all
    return kept, script


def assemble(name='retailrun'):
    src = os.path.join(HERE, 'retailpad.s')
    folder = os.path.join(ROOT, 'build/retailpad', name)     # a run beside another assembles its own
    os.makedirs(folder, exist_ok=True)
    obj = os.path.join(folder, 'retailpad.o')
    out = os.path.join(folder, 'retailpad.bin')
    subprocess.run([BIN + 'sh2eb-elf-as', '-o', obj, src], check=True)
    subprocess.run([BIN + 'sh2eb-elf-objcopy', '-O', 'binary', obj, out], check=True)
    syms = subprocess.run([BIN + 'sh2eb-elf-nm', obj], capture_output=True, text=True, check=True).stdout
    vars_off = next(int(l.split()[0], 16) for l in syms.splitlines() if l.split()[-1] == 'vars')
    return open(out, 'rb').read(), vars_off


def poke(m, addr, data):
    for at in range(0, len(data), 2048):
        chunk = data[at:at + 2048]
        m.send(f'poke {addr + at:x} ' + ' '.join(f'{b:02x}' for b in chunk), 'ok poke', 30)


def tap(m, k):
    m.send(f'input {k}', 'ok input', 10)
    m.send('frame_advance 10', 'done frame_advance', 30)
    m.send(f'input_release {k}', 'ok input_release', 10)
    m.send('frame_advance 10', 'done frame_advance', 30)


def rec_line(t, r):
    extra = f' state {r["state"]} goal {r["goal"]}' if 'state' in r else ''
    if 'mood' in r:
        extra += f' mood {r["mood"]} maxturn {r["maxturn"]} target {r["x"] + r["tdx"]} {r["z"] + r["tdz"]}'
    return (f'rec {t} level {r["level"]} room {r["room"]} pos {r["x"]} {r["y"]} {r["z"]} yaw {r["yaw"]} anim {r["anim"]} '
            f'frame {r["frame"]} health {r["health"]} seed {r["seed"]:08x}{extra}\n')


class Window:
    """The tick table's window in retail's memory: entry i at i % WINDOW, those from
    the stub's next one on kept written (the stub reads a few hundred between two
    looks)."""

    def __init__(self, keys, kept):
        self.entries = [keys[t] for t in kept]
        self.mem = bytearray(struct.pack('>H', NOTHING) * WINDOW)
        self.written = 0    # entries written: 0 .. written - 1

    def entry(self, i):
        return self.entries[i] if i < len(self.entries) else NOTHING

    def fill(self, m, index):
        """Written ahead from the stub's index (a new run, or a resumed one, starts there)."""
        if index > self.written or index + WINDOW <= self.written:
            self.written = index
        end = index + WINDOW - 64
        while self.written < end:
            at = self.written % WINDOW
            n = min(end - self.written, WINDOW - at)
            data = b''.join(struct.pack('>H', self.entry(self.written + k)) for k in range(n))
            self.mem[at * 2:(at + n) * 2] = data
            poke(m, TABLE + at * 2, data)
            self.written += n


def arm(m, want, window, script, code, vars_off, stop, watch):
    """Retail run from its title to the level's first game tick, then the stub, its
    tables and the patches poked in and the pad scan pointed at the stub."""
    for st in TITLE_TO_NEW_GAME:
        if st.startswith('+'):
            tap(m, st[1:])
        else:
            m.send(f'run_to_frame {st}', 'done run_to_frame', 1200)
    m.send(f'breakpoint {CONTROL_PHASE:08X}', 'breakpoint', 10)
    while True:
        m.send('run', 'break pc=', 1200)
        if m.word(LEVEL) == want:
            break
    m.send('breakpoint_clear', 'breakpoint', 10)
    window.fill(m, 0)
    poke(m, SCRIPT, b''.join(struct.pack('>H', k) for k in script))
    poke(m, RING, bytes(RING_SIZE * ENTRY))
    poke(m, STUB, code)
    poke(m, STUB + vars_off, struct.pack('>9I', 1, 0, TABLE, RING, stop, SCRIPT, 0, watch, 0))
    for addr, data, why in PATCHES:
        poke(m, addr, data)
    poke(m, POOL, struct.pack('>I', STUB))


def differs(recs, host, kept, i):
    """Retail's record i against the host's, but for the first of a level: retail's is
    the new level as it loads, the host's the old one's last tick (the host numbers the
    levels one on). Straight after a stay in the ring only the seed: a ring a hole opens
    is retail's from inside the tick, and the key it gives is used before that tick ends;
    the host's ring takes ticks and uses it as the ring shuts."""
    if not 1 <= i < len(kept):
        return False
    h = host[kept[i] - 1]
    if recs[i]['level'] + 1 != h['level']:
        return False
    if h['inv']:
        return recs[i]['seed'] != h['seed']
    return any(recs[i][k] != h[k] for k in FIELDS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('replay')
    ap.add_argument('out')
    ap.add_argument('--max-vblanks', type=int, default=400000)
    ap.add_argument('--ticks', type=int, default=0)
    ap.add_argument('--items', type=int, default=0, help='at the end, this many items printed (raw)')
    ap.add_argument('--watch', type=int, default=0, help="this item in the ring instead of Lara's (no comparison)")
    ap.add_argument('--name', default='retailrun', help='its Mednafen (build/NAME_home): another name runs beside it')
    ap.add_argument('--save-at', type=int, default=0, help='a save state once retail reaches this game tick (to --state)')
    ap.add_argument('--state', help='the save state file: written with --save-at, else loaded to start there (a replay that runs the same to its tick)')
    ap.add_argument('--from', dest='start', help='a save state to start from while --save-at writes --state')
    args = ap.parse_args()
    level, keys = read_replay(args.replay)
    if args.ticks:
        keys = keys[:args.ticks]
    want = LEVELS[level]
    if want != 1:
        sys.exit('only a new game into the Caves so far')
    host = host_records(args.replay, len(keys))
    kept, script = split_inventory(keys, host)
    if len(script) > MAX_SCRIPT:
        sys.exit('too many inventory frames for the script')
    code, vars_off = assemble(args.name)
    # retail's backup memory as it boots clean: a run that went astray at a
    # "Save Game?" prompt leaves a save there, and with one the title's
    # menus take other presses
    clean = os.path.join(ROOT, 'build/retail_clean_sav')
    if os.path.isdir(clean):
        sav = os.path.join(ROOT, f'build/{args.name}_home/sav')
        os.makedirs(sav, exist_ok=True)
        for f in os.listdir(clean):
            shutil.copyfile(os.path.join(clean, f), os.path.join(sav, f))
    m = Mednafen(CUE, args.name)
    recs = {}
    window = Window(keys, kept)
    load = args.start or (args.state if not args.save_at else None)
    resume = bool(load)
    try:
        m.send('deterministic', 'deterministic', 10)
        if resume:
            # the game as a run of this replay left it (stub, tables and all)
            m.send(f'load_state {os.path.abspath(load).replace(os.sep, "/")}', 'ok load_state', 120)
            poke(m, STUB + vars_off + 28, struct.pack('>I', args.watch))
            # this replay's tables (one that runs as the state's did up to its
            # tick but parts later, a bot run after a fix, resumes too)
            poke(m, SCRIPT, b''.join(struct.pack('>H', k) for k in script))
            window.fill(m, m.word(STUB + vars_off + 4))
            print(f'resumed at game tick {m.word(STUB + vars_off + 4)}', flush=True)
        if not resume:
            arm(m, want, window, script, code, vars_off, len(kept) if args.items else 0xFFFFFFFF, args.watch)
        print(f'armed at vblank {m.send("status", "status", 10).split("frame=")[1].split()[0]}: {len(keys)} ticks, '
              f'{len(keys) - len(kept)} of them in the inventory ({script.count(0)} stays)', flush=True)
        # (a resumed run's ring still holds the records from before the state:
        # perhaps another item's, a --watch run's; only those from here on)
        since = m.word(STUB + vars_off + 4) if resume else 0
        saved = False
        vblanks = 0
        # the game runs a tick in two vblanks at most: the ring holds 512 vblanks' worth
        step = 300      # (with --items the game holds before the tick after the last: no overshoot)
        last, still = -1, 0
        part = None     # the first record that differs: the run stops a few ticks on
        while vblanks < args.max_vblanks:
            m.send(f'frame_advance {step}', 'done frame_advance', 600)
            vblanks += step
            index = m.word(STUB + vars_off + 4)
            window.fill(m, index)
            ring = m.read(RING, RING_SIZE * ENTRY)
            for e in range(RING_SIZE):
                b = ring[e * ENTRY:(e + 1) * ENTRY]
                i, seed, lvl = struct.unpack('>3I', b[0:12])
                if i in recs or i >= index or i < since or (i == 0 and seed == 0):
                    continue
                item = b[16:16 + 0x48]
                state, goal, req, anim, frame, room = struct.unpack('>6h', item[0x0E:0x1A])
                hp, = struct.unpack('>h', item[0x22:0x24])
                x, y, z = struct.unpack('>3i', item[0x30:0x3C])
                yaw, = struct.unpack('>H', item[0x3E:0x40])
                recs[i] = dict(level=lvl, room=room, x=x, y=y, z=z, yaw=yaw, anim=anim, frame=frame, health=hp, seed=seed,
                               state=state, goal=goal)
                if args.watch:
                    mood, = struct.unpack('>I', b[12:16])
                    turn, tdx, tdz = struct.unpack('>3h', b[88:94])
                    recs[i].update(mood=mood, maxturn=turn, tdx=tdx, tdz=tdz)
                if part is None and not args.watch and differs(recs, host, kept, i):
                    part = i
                    print(f"  parted at the host's tick {kept[i] - 1}: on to a few ticks past it", flush=True)
            if args.save_at and not saved and index >= args.save_at:
                m.send(f'save_state {os.path.abspath(args.state).replace(os.sep, "/")}', 'ok save_state', 120)
                print(f'  saved the state at game tick {index}: {args.state}', flush=True)
                saved = True
            if index >= len(kept) or (part is not None and index > part + 8):
                break
            # no game tick for two minutes: held somewhere the replay doesn't
            # lead it out of (a level's load and its film take less)
            still = still + 1 if index == last else 0
            last = index
            if still >= 24 and not args.items:
                shot = os.path.splitext(args.out)[0] + '_held.png'
                m.send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30)
                print(f'  no game tick since tick {index} (script at {m.word(STUB + vars_off + 24)} of {len(script)}, '
                      f'picture {shot}): stopped', flush=True)
                # ours still in place? (the game may have written over low work RAM)
                stub = m.read(STUB, vars_off)      # its code (its vars change as it runs)
                print(f'    the stub {"intact" if stub == code[:vars_off] else "WRITTEN OVER"}; the table window '
                      f'{"intact" if m.read(TABLE, WINDOW * 2) == bytes(window.mem) else "WRITTEN OVER"}; the script '
                      f'{"intact" if m.read(SCRIPT, len(script) * 2) == b"".join(struct.pack(">H", k) for k in script) else "WRITTEN OVER"}',
                      flush=True)
                low = m.read(0x00200000, 0x50000)
                spans, start = [], None
                for at in range(0, len(low), 256):
                    ours = STUB - 0x00200000 <= at < OURS_END - 0x00200000
                    busy = any(low[at:at + 256]) and not ours
                    if busy and start is None:
                        start = at
                    elif not busy and start is not None:
                        spans.append((start, at))
                        start = None
                print('    low work RAM in use outside ours: ' + ', '.join(f'{0x00200000 + a:08x}..{0x00200000 + b:08x}' for a, b in spans[:12]), flush=True)
                # room elsewhere: runs of zeros of 16 KB or more in work RAM low
                # and high and in sound RAM
                for base, size in ((0x00200000, 0x100000), (0x06000000, 0x100000), (0x25A00000, 0x80000)):
                    mem = m.read(base, size)
                    runs, start = [], None
                    for at in range(0, size, 1024):
                        zero = not any(mem[at:at + 1024])
                        if zero and start is None:
                            start = at
                        elif not zero and start is not None:
                            if at - start >= 0x4000:
                                runs.append((start, at))
                            start = None
                    if start is not None and size - start >= 0x4000:
                        runs.append((start, size))
                    print(f'    zero runs from {base:08x}: ' + ', '.join(f'{base + a:08x}..{base + b:08x}' for a, b in runs[:16]),
                          flush=True)
                # where it is held: the master's PC and PR a few frames apart
                for _ in range(4):
                    regs = m.send('dump_regs', 'R0=', 10)
                    pc = re.search(r'PC=([0-9A-Fa-f]{8})', regs)
                    pr = re.search(r'PR=([0-9A-Fa-f]{8})', regs)
                    print(f'    held at PC {pc.group(1) if pc else "?"} PR {pr.group(1) if pr else "?"}', flush=True)
                    m.send('frame_advance 7', 'done frame_advance', 60)
                break
            if vblanks % 6000 == 0:
                print(f'  vblank {vblanks}: tick {index}', flush=True)
        if args.items:
            # held before the tick after the table's last: the items as they stand after it
            base = m.word(ITEMS)
            raw = m.read(base, 0x48 * args.items)
            print(f'items at tick {m.word(STUB + vars_off + 4)}:')
            for n in range(args.items):
                it = raw[n * 0x48:(n + 1) * 0x48]
                obj, state, goal, req, anim, frame, room = struct.unpack('>7h', it[0x0C:0x1A])
                speed, fall, hp, box, timer, flags = struct.unpack('>hhhhhH', it[0x1E:0x2A])
                x, y, z = struct.unpack('>3i', it[0x30:0x3C])
                yaw, = struct.unpack('>H', it[0x3E:0x40])
                print(f'  item {n:3}: obj {obj:3} room {room:3} pos {x:6} {y:6} {z:6} yaw {yaw:5} state {state} goal {goal} '
                      f'anim {anim} frame {frame} speed {speed} hp {hp} flags {flags:04x} raw {it.hex()}')
    finally:
        m.quit()
    # retail's entry i is the game before its game tick i: after the host's tick before
    # kept[i]. (After a stay in the inventory Lara's health may differ for that one record:
    # retail uses a medipack in the tick that opened it, the host as the inventory shuts.)
    first = None
    with open(args.out, 'w') as f, open(os.path.splitext(args.out)[0] + '_host.txt', 'w') as fh:
        for i in sorted(recs):
            if i < 1 or i >= len(kept):
                continue
            t = kept[i] - 1
            f.write(rec_line(t, recs[i]))
            fh.write(rec_line(t, host[t]))
            if first is None and differs(recs, host, kept, i):
                first = i
    print(f'{len(recs)} records of {len(kept)} game ticks -> {args.out}')
    if args.watch:
        print(f'item {args.watch} recorded, not compared (host: WATCH={args.watch} EVERY=1)')
        return 0
    if first is None:
        print('ALIKE: Lara and the seed on every tick read')
        return 0
    print(f"PART at the host's tick {kept[first] - 1} (retail's game tick {first}):")
    for k in range(first - 3, first + 4):
        if k in recs and 1 <= k < len(kept):
            tt = kept[k] - 1
            for name, r in (('retail', recs[k]), ('host  ', host[tt])):
                print(f'  {tt} {name}: ' + ' '.join(f'{f} {r[f]:08x}' if f == 'seed' else f'{f} {r[f]}' for f in FIELDS))
    return 1


if __name__ == '__main__':
    sys.exit(main())
