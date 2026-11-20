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
prints N items."""
import argparse
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, 'tools')
sys.path.insert(0, HERE)
from capture import Mednafen  # noqa: E402

CUE = ROOT + '/external_resources/tomb_raider_usa/Tomb Raider (USA).cue'
BIN = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/'
# low work RAM retail leaves alone (all zero from boot into the Caves:
# 0x00200010..0x00245d10), but for its bottom: the inventory ring, opened in a
# level, writes from 0x00200000 on (over the stub where it was, at 0x00200100);
# everything here is above 0x0020e000, which it left alone
STUB = 0x0020E000       # to 0x0020e400, its vars in it
RING = 0x0020E400       # 256 entries of 96 bytes, to 0x00214400
SCRIPT = 0x00214400     # to 0x00219400
TABLE = 0x00219400      # to 0x00245c00
MAX_TICKS = (0x00245C00 - TABLE) // 2 - 64
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


def differs(recs, host, kept, i):
    """Retail's record i against the host's, but for the first of a level: retail's is
    the new level as it loads, the host's the old one's last tick (the host numbers the
    levels one on)."""
    if not 1 <= i < len(kept):
        return False
    h = host[kept[i] - 1]
    if recs[i]['level'] + 1 != h['level']:
        return False
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
    args = ap.parse_args()
    level, keys = read_replay(args.replay)
    if args.ticks:
        keys = keys[:args.ticks]
    want = LEVELS[level]
    if want != 1:
        sys.exit('only a new game into the Caves so far')
    host = host_records(args.replay, len(keys))
    kept, script = split_inventory(keys, host)
    if len(kept) > MAX_TICKS or len(script) > MAX_SCRIPT:
        sys.exit('too many ticks or inventory frames for the tables')
    code, vars_off = assemble(args.name)
    m = Mednafen(CUE, args.name)
    recs = {}
    try:
        m.send('deterministic', 'deterministic', 10)
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
        table = b''.join(struct.pack('>H', keys[t]) for t in kept) + struct.pack('>H', NOTHING) * 64
        poke(m, TABLE, table)
        poke(m, SCRIPT, b''.join(struct.pack('>H', k) for k in script))
        poke(m, RING, bytes(RING_SIZE * ENTRY))
        poke(m, STUB, code)
        stop = len(kept) if args.items else 0xFFFFFFFF
        poke(m, STUB + vars_off, struct.pack('>8I', 1, 0, TABLE, RING, stop, SCRIPT, 0, args.watch))
        for addr, data, why in PATCHES:
            poke(m, addr, data)
        poke(m, POOL, struct.pack('>I', STUB))
        print(f'armed at vblank {m.send("status", "status", 10).split("frame=")[1].split()[0]}: {len(keys)} ticks, '
              f'{len(keys) - len(kept)} of them in the inventory ({script.count(0)} stays)', flush=True)
        vblanks = 0
        # the game runs a tick in two vblanks at most: the ring holds 512 vblanks' worth
        step = 300      # (with --items the game holds before the tick after the last: no overshoot)
        last, still = -1, 0
        part = None     # the first record that differs: the run stops a few ticks on
        while vblanks < args.max_vblanks:
            m.send(f'frame_advance {step}', 'done frame_advance', 600)
            vblanks += step
            index = m.word(STUB + vars_off + 4)
            ring = m.read(RING, RING_SIZE * ENTRY)
            for e in range(RING_SIZE):
                b = ring[e * ENTRY:(e + 1) * ENTRY]
                i, seed, lvl = struct.unpack('>3I', b[0:12])
                if i in recs or i >= index or (i == 0 and seed == 0):
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
                stub = m.read(STUB, len(code))
                print(f'    the stub {"intact" if stub == code else "WRITTEN OVER"}; table head '
                      f'{"intact" if m.read(TABLE, 64) == table[:64] else "WRITTEN OVER"}', flush=True)
                low = m.read(0x00200000, 0x50000)
                spans, start = [], None
                for at in range(0, len(low), 256):
                    ours = STUB - 0x00200000 <= at < 0x00245C00 - 0x00200000
                    busy = any(low[at:at + 256]) and not ours
                    if busy and start is None:
                        start = at
                    elif not busy and start is not None:
                        spans.append((start, at))
                        start = None
                print('    low work RAM in use outside ours: ' + ', '.join(f'{0x00200000 + a:08x}..{0x00200000 + b:08x}' for a, b in spans[:12]), flush=True)
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
