"""A tick replay (tools/replay.py, the playtest bot's) played by retail TR1 in Mednafen,
a tick at a time, beside the host build playing it: the first tick where Lara or the
control seed part, or that they ran alike.

    python -I tools/retailrun.py REPLAY OUT.txt [--ticks N] [--items N] [--go-on]

OUT.txt gets retail's records as host/host.cpp's REC lines (tools/recdiff.py compares two
such files, tools/retaildiff.py sorts their differences by what they do to play);
OUT_host.txt the host's for the same ticks.

Retail's S_UpdateInput, called once a tick by ControlPhase, reaches the pad scan through
the pool word at 0x06037644; that word is turned to tools/retailpad.s (assembled with the
pinned toolchain) at STUB. For a game tick it feeds pad 1 from the tick table, one entry a
tick, and keeps Lara's item, the control seed and the level's statistics so far in a ring;
for a frame of the inventory's own loop (retail's ring is modal, an input a frame and its
animation by the vblanks gone) it feeds the inventory script (so do the level's statistics
screen and "Save Game?"). The host runs the inventory and the statistics as ticks with the
game held: those ticks leave the tick table, and their presses, spaced out, make the
script; a run of routes chained (host/bot.h) is followed from level to level, past the
in-game films and movies between them (retail plays those in loops of their own: the
host's film ticks leave the table too). The tick table is a window the driver keeps filled
ahead, the script a part of it the driver moves on. Armed at the first ControlPhase of the
replay's level, reached from boot by the title's menus (a new game: the Caves only, so
far). --items N holds the game before the tick after the replay's last and prints N items.
build/retail_clean_sav holds retail's backup memory files from a clean boot (a run's
Mednafen home's sav/ before any run saved a game): each run starts from them.

Mednafen runs as fast as it can. A run stops a few ticks past the first record that
differs; with --go-on it plays on, and stops once retail is lost: in another level than
the host's for LOST ticks (a level it didn't finish), or with no game tick for six minutes.
--save-every N saves a state every N game ticks (build/bot/rNAME_TICK.mcs), to resume from
(a state holds the stub as the run assembled it: one from before a change to retailpad.s
doesn't resume).
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from array import array

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, 'tools')
sys.path.insert(0, HERE)
from capture import Mednafen  # noqa: E402

CUE = ROOT + '/external_resources/tomb_raider_usa/Tomb Raider (USA).cue'
BIN = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/'
HOST = os.environ.get('HOST_LOGIC', 'build/host/logic')
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
LOST = 900              # game ticks in another level than the host's: retail is lost

PAD = {'UP': 0x1000, 'DOWN': 0x2000, 'LEFT': 0x4000, 'RIGHT': 0x8000, 'START': 0x0800, 'A': 0x0400, 'C': 0x0200,
       'B': 0x0100, 'R': 0x0080, 'X': 0x0040, 'Y': 0x0020, 'Z': 0x0010, 'L': 0x0008}
LEVELS = {'LEVEL1': 1, 'LEVEL2': 2, 'LEVEL3A': 3, 'LEVEL3B': 4}
FILMS = (6, 12, 17, 19)     # the host's cutscene levels (LVL_TR1_CUT_1..4)
# retail's level numbers (PC TR1's: the gym 0, the levels 1..15, the cutscenes 16..19) by the host's
# (common.h LevelID: the title 0, the gym 1, the cutscenes among the levels)
RETAIL_LEVEL = {1: 0, 2: 1, 3: 2, 4: 3, 5: 4, 6: 16, 7: 5, 8: 6, 9: 7, 10: 8, 11: 9, 12: 17, 13: 10, 14: 11, 15: 12,
                16: 13, 17: 18, 18: 14, 19: 19, 20: 15}
# Retail's own bugs we keep the fixed way (PC TR1, TRX), patched out of retail so a run
# can be followed past them: (address, bytes, why)
PATCHES = [
    (0x0603047C, bytes([0x40, 0x00]), 'dart emitter: darts from one facing +x start 412 behind it, as on PC (retail tests 0x3fec)'),
    # GetSpheres' joint loop: "tst r9,r9 / bt past the turns / nop / nop" over "mov #0x1c,r1 /
    # and r8,r1 / tst r1,r1 / bt"; r9 is item->data, and each turn tests its own flag after
    (0x0602E666, bytes([0x29, 0x98, 0x89, 0x18, 0x00, 0x09, 0x00, 0x09]),
     'a creature with no AI slot turns no head or neck sphere, as TRX (retail reads the turns from address 0; issue 336)'),
    # ApeVault: "mov.w @r7,r0 / nop / nop" over "mov r8,r1 / add #0x28,r1 / mov.w @r1,r0"; r7 is the
    # creature's flags, r8 the item
    (0x060295B4, bytes([0x60, 0x71, 0x00, 0x09, 0x00, 0x09]),
     "the gorilla's turn back after a left sidestep tests its own flag, as TRX (retail tests the item's flags; issue 99)"),
]
TITLE_TO_NEW_GAME = ['2400', '+start', '2700', '+start', '3600', '+a', '4100', '+start', '4400', '+a', '4600', '+a',
                     '5000', '+start']
ITEMS = 0x06072BDC     # the items' base (a pointer)
FIELDS = ('room', 'x', 'y', 'z', 'yaw', 'anim', 'frame', 'health', 'seed')
HOST_FIELDS = ('level', 'room', 'x', 'y', 'z', 'yaw', 'anim', 'frame', 'health', 'seed', 'items', 'inv', 'kills',
               'pickups', 'secrets')
RETAIL_FIELDS = ('level', 'room', 'x', 'y', 'z', 'yaw', 'anim', 'frame', 'health', 'seed', 'state', 'goal', 'kills',
                 'pickups', 'secrets')
WATCH_FIELDS = ('mood', 'maxturn', 'tdx', 'tdz')


class Records:
    """Records by tick, kept compact (a whole chained run is over a million ticks)."""

    def __init__(self, names):
        self.cols = {k: array('I' if k in ('seed', 'items', 'mood') else 'i') for k in names}
        self.have = bytearray()

    def __setitem__(self, t, rec):
        if t >= len(self.have):
            add = t + 1 - len(self.have) + 4096
            for c in self.cols.values():
                c.frombytes(bytes(add * c.itemsize))
            self.have.extend(bytes(add))
        for k, v in rec.items():
            self.cols[k][t] = v
        self.have[t] = 1

    def __contains__(self, t):
        return 0 <= t < len(self.have) and self.have[t]

    def __getitem__(self, t):
        return {k: c[t] for k, c in self.cols.items()}

    def __iter__(self):
        return (t for t, h in enumerate(self.have) if h)

    def __len__(self):
        return self.have.count(1)


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


def host_records(replay, ticks, path):
    """The host's record after each tick (written to path as it prints them); inv: the
    inventory open after it."""
    rel = os.path.relpath(replay, ROOT).replace('\\', '/')
    env = dict(os.environ, REPLAY=rel, REC='1', MSYS_NO_PATHCONV='1', WSLENV='REPLAY:REC')
    with open(path, 'w') as f:
        out = subprocess.run(['wsl.exe', '-d', 'Ubuntu-24.04', '--cd', ROOT.replace('\\', '/'), '--', 'qemu-ppc',
                              HOST, 'build/data', '-', str(ticks)], env=env, stdout=f, stderr=subprocess.PIPE, text=True)
    if out.returncode:
        sys.exit(f'host: {out.stderr.strip()}')
    recs = Records(HOST_FIELDS)
    for line in open(path):
        w = line.split()
        if w and w[0] == 'rec':
            # rec T level L room R pos X Y Z yaw A anim N frame F health H seed S items I inv V
            # kills K pickups P secrets B
            recs[int(w[1])] = dict(level=int(w[3]), room=int(w[5]), x=int(w[7]), y=int(w[8]), z=int(w[9]),
                                   yaw=int(w[11]), anim=int(w[13]), frame=int(w[15]), health=int(w[17]),
                                   seed=int(w[19], 16), items=int(w[21], 16), inv=int(w[23]), kills=int(w[25]),
                                   pickups=int(w[27]), secrets=int(w[29], 16))
    return recs


def split_inventory(keys, host):
    """The ticks retail plays as game ticks (those the host starts and ends with the
    inventory open leave), and the inventory script: each stay's presses, spaced. A
    press that shuts it (the host's tick that shuts it is also a game tick) goes to the
    script too, the tick's own entry left with nothing held (keys is changed). A film
    between levels is no game tick (retail plays it in a loop of its own, the pad
    unread by the stub) but its first, the level's end; the statistics and "Save
    Game?" after it are a stay from their first tick."""
    def staying(t):
        return t > 0 and host[t]['inv'] and (host[t - 1]['inv'] or host[t]['level'] in FILMS)

    kept, script = [], []
    t = 0
    while t < len(keys):
        if host[t]['level'] in FILMS and not host[t]['inv']:
            # but the film's first tick: the level's end, where retail reads the pad
            # once more before it ends the level (as at a level's end before its
            # statistics, the host's tick that opens them)
            if t > 0 and host[t - 1]['level'] not in FILMS:
                kept.append(t)
            t += 1
            continue
        if staying(t):
            # a stay in the inventory: its presses (a run of the same keys held is one)
            stay = []
            while t < len(keys) and staying(t):
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
    stats = f' kills {r["kills"]} pickups {r["pickups"]} secrets {r["secrets"]:02x}' if 'kills' in r else ''
    return (f'rec {t} level {r["level"]} room {r["room"]} pos {r["x"]} {r["y"]} {r["z"]} yaw {r["yaw"]} anim {r["anim"]} '
            f'frame {r["frame"]} health {r["health"]} seed {r["seed"]:08x}{stats}{extra}\n')


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


class Script:
    """The inventory script in retail's memory: the whole script's part from `base` on
    (the stub's position counts from there, the stub's +36 keeps it), moved on once the
    position is half way through it."""

    def __init__(self, entries, vars_off):
        self.entries = entries
        self.vars = STUB + vars_off
        self.base = 0

    def part(self):
        return b''.join(struct.pack('>H', k) for k in self.entries[self.base:self.base + MAX_SCRIPT])

    def write(self, m):
        poke(m, SCRIPT, self.part())
        poke(m, self.vars + 36, struct.pack('>I', self.base))

    def follow(self, m):
        pos = m.word(self.vars + 24)
        if pos > MAX_SCRIPT // 2:
            self.base += pos
            self.write(m)
            poke(m, self.vars + 24, struct.pack('>I', 0))

    def position(self, m):
        return self.base + m.word(self.vars + 24)


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
    poke(m, RING, bytes(RING_SIZE * ENTRY))
    poke(m, STUB, code)
    poke(m, STUB + vars_off, struct.pack('>10I', 1, 0, TABLE, RING, stop, SCRIPT, 0, watch, 0, 0))
    script.write(m)
    for addr, data, why in PATCHES:
        poke(m, addr, data)
    poke(m, POOL, struct.pack('>I', STUB))


def differs(recs, host, kept, i):
    """Retail's record i against the host's, but for the first of a level: retail's is
    the new level as it loads, the host's the old one's last tick (levels numbered as
    RETAIL_LEVEL maps them). Straight after a stay in the ring only the seed: a ring a
    hole opens is retail's from inside the tick, and the key it gives is used before
    that tick ends; the host's ring takes ticks and uses it as the ring shuts."""
    if not 1 <= i < len(kept):
        return False
    h = host[kept[i] - 1]
    r = recs[i]
    if r['level'] != RETAIL_LEVEL.get(h['level']):
        return False
    if h['inv']:
        return r['seed'] != h['seed']
    return any(r[k] != h[k] for k in FIELDS)


def held_report(m, args, index, script, code, vars_off, window):
    """Where retail is held when no game tick comes: a picture, ours intact or not,
    the RAM free about it, the master's PC."""
    shot = os.path.splitext(args.out)[0] + '_held.png'
    m.send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30)
    print(f'  no game tick since tick {index} (script at {script.position(m)} of {len(script.entries)}, '
          f'picture {shot}): stopped', flush=True)
    stub = m.read(STUB, vars_off)      # its code (its vars change as it runs)
    print(f'    the stub {"intact" if stub == code[:vars_off] else "WRITTEN OVER"}; the table window '
          f'{"intact" if m.read(TABLE, WINDOW * 2) == bytes(window.mem) else "WRITTEN OVER"}; the script '
          f'{"intact" if m.read(SCRIPT, len(script.part())) == script.part() else "WRITTEN OVER"}', flush=True)
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
    for _ in range(4):
        regs = m.send('dump_regs', 'R0=', 10)
        pc = re.search(r'PC=([0-9A-Fa-f]{8})', regs)
        pr = re.search(r'PR=([0-9A-Fa-f]{8})', regs)
        print(f'    held at PC {pc.group(1) if pc else "?"} PR {pr.group(1) if pr else "?"}', flush=True)
        m.send('frame_advance 7', 'done frame_advance', 60)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('replay')
    ap.add_argument('out')
    ap.add_argument('--max-vblanks', type=int, default=4000000)
    ap.add_argument('--ticks', type=int, default=0)
    ap.add_argument('--items', type=int, default=0, help='at the end, this many items printed (raw)')
    ap.add_argument('--watch', type=int, default=0, help="this item in the ring instead of Lara's (no comparison)")
    ap.add_argument('--name', default='retailrun', help='its Mednafen (build/NAME_home): another name runs beside it')
    ap.add_argument('--save-at', type=int, default=0, help='a save state once retail reaches this game tick (to --state)')
    ap.add_argument('--state', help='the save state file: written with --save-at, else loaded to start there (a replay that runs the same to its tick)')
    ap.add_argument('--from', dest='start', help='a save state to start from while --save-at writes --state')
    ap.add_argument('--go-on', action='store_true', help='on past differences, till retail is lost')
    ap.add_argument('--save-every', type=int, default=0, help='a save state every N game ticks (build/bot/rNAME_TICK.mcs)')
    args = ap.parse_args()
    level, keys = read_replay(args.replay)
    if args.ticks:
        keys = keys[:args.ticks]
    want = LEVELS[level]
    if want != 1:
        sys.exit('only a new game into the Caves so far')
    t0 = time.time()
    host = host_records(args.replay, len(keys), os.path.splitext(args.out)[0] + '_hostrun.txt')
    kept, script_entries = split_inventory(keys, host)
    print(f'host: {len(keys)} ticks in {time.time() - t0:.0f} s', flush=True)
    code, vars_off = assemble(args.name)
    script = Script(script_entries, vars_off)
    # retail's backup memory as it boots clean: a run that went astray at a
    # "Save Game?" prompt leaves a save there, and with one the title's
    # menus take other presses
    clean = os.path.join(ROOT, 'build/retail_clean_sav')
    if os.path.isdir(clean):
        sav = os.path.join(ROOT, f'build/{args.name}_home/sav')
        os.makedirs(sav, exist_ok=True)
        for f in os.listdir(clean):
            shutil.copyfile(os.path.join(clean, f), os.path.join(sav, f))
    m = Mednafen(CUE, args.name, fast=True)
    recs = Records(RETAIL_FIELDS + (WATCH_FIELDS if args.watch else ()))
    window = Window(keys, kept)
    load = args.start or (args.state if not args.save_at else None)
    resume = bool(load)
    t0 = time.time()
    try:
        m.send('deterministic', 'deterministic', 10)
        if resume:
            # the game as a run of this replay left it (stub, tables and all)
            m.send(f'load_state {os.path.abspath(load).replace(os.sep, "/")}', 'ok load_state', 120)
            poke(m, STUB + vars_off + 28, struct.pack('>I', args.watch))
            # no hold left over from the state (one saved held before a tick)
            poke(m, STUB + vars_off + 16, struct.pack('>I', len(kept) if args.items else 0xFFFFFFFF))
            # this replay's tables (one that runs as the state's did up to its tick
            # but parts later, a bot run after a fix, resumes too): the script from
            # where the state's run had moved it to (none in a state from before)
            script.base = m.word(STUB + vars_off + 36)
            script.write(m)
            window.fill(m, m.word(STUB + vars_off + 4))
            print(f'resumed at game tick {m.word(STUB + vars_off + 4)}', flush=True)
        if not resume:
            arm(m, want, window, script, code, vars_off, len(kept) if args.items else 0xFFFFFFFF, args.watch)
        print(f'armed at vblank {m.send("status", "status", 10).split("frame=")[1].split()[0]}: {len(keys)} ticks, '
              f'{len(keys) - len(kept)} of them in the inventory ({script_entries.count(0)} stays)', flush=True)
        # (a resumed run's ring still holds the records from before the state:
        # perhaps another item's, a --watch run's; only those from here on)
        since = m.word(STUB + vars_off + 4) if resume else 0
        saved = False
        next_save = (since // args.save_every + 1) * args.save_every if args.save_every else 0
        vblanks = 0
        # the game runs a tick in two vblanks at most: the ring holds 512 vblanks' worth
        step = 300      # (with --items the game holds before the tick after the last: no overshoot)
        last, still = -1, 0
        part = None     # the first record that differs: the run stops a few ticks on
        checked = since     # records before it compared
        astray = 0      # game ticks in a row in another level than the host's
        level_now = None
        while vblanks < args.max_vblanks:
            m.send(f'frame_advance {step}', 'done frame_advance', 600)
            vblanks += step
            index = m.word(STUB + vars_off + 4)
            window.fill(m, index)
            script.follow(m)
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
                rec = dict(level=lvl, room=room, x=x, y=y, z=z, yaw=yaw, anim=anim, frame=frame, health=hp, seed=seed,
                           state=state, goal=goal)
                if args.watch:
                    mood, = struct.unpack('>I', b[12:16])
                    turn, tdx, tdz = struct.unpack('>3h', b[88:94])
                    rec.update(mood=mood, maxturn=turn, tdx=tdx, tdz=tdz, kills=0, pickups=0, secrets=0)
                else:
                    kills, secrets, pickups = struct.unpack('>3H', b[88:94])
                    rec.update(kills=kills, pickups=pickups, secrets=secrets)
                recs[i] = rec
            # the records read, in order, up to the first not read yet (one the ring
            # lost is passed once it can't come any more)
            checked = max(checked, index - RING_SIZE)
            while checked < index and (checked in recs or checked < 1):
                i = checked
                checked += 1
                if i < 1 or i >= len(kept) or args.watch:
                    continue
                r = recs[i]
                if r['level'] != level_now:
                    level_now = r['level']
                    print(f'  game tick {i} (host {kept[i] - 1}): retail level {level_now} '
                          f'(vblank {vblanks}, {time.time() - t0:.0f} s)', flush=True)
                h = host[kept[i] - 1]
                astray = astray + 1 if r['level'] != RETAIL_LEVEL.get(h['level']) and h['level'] not in FILMS else 0
                if part is None and differs(recs, host, kept, i):
                    part = i
                    print(f"  parted at the host's tick {kept[i] - 1}" +
                          (': on, till lost' if args.go_on else ': on to a few ticks past it'), flush=True)
            if args.save_at and not saved and index >= args.save_at:
                m.send(f'save_state {os.path.abspath(args.state).replace(os.sep, "/")}', 'ok save_state', 120)
                print(f'  saved the state at game tick {index}: {args.state}', flush=True)
                saved = True
            if next_save and index >= next_save:
                path = os.path.join(ROOT, f'build/bot/r{args.name}_{index}.mcs')
                m.send(f'save_state {path.replace(os.sep, "/")}', 'ok save_state', 120)
                print(f'  saved a state at game tick {index}: {path}', flush=True)
                next_save = (index // args.save_every + 1) * args.save_every
            if index >= len(kept):
                break
            if part is not None and not args.go_on and index > part + 8:
                break
            if astray >= LOST:
                print(f'  LOST: retail in another level than the host\'s for {astray} game ticks (to game tick '
                      f'{checked - 1}): stopped', flush=True)
                break
            # no game tick for six minutes: held somewhere the replay doesn't
            # lead it out of (a level's load, its in-game film and a movie between
            # levels, Qualopec's after its film, take less)
            still = still + 1 if index == last else 0
            last = index
            if still >= 72 and not args.items:
                held_report(m, args, index, script, code, vars_off, window)
                break
            if vblanks % 36000 == 0:
                print(f'  vblank {vblanks}: tick {index} ({vblanks / (time.time() - t0):.0f} vblanks a second)',
                      flush=True)
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
        for i in recs:
            if i < 1 or i >= len(kept):
                continue
            t = kept[i] - 1
            f.write(rec_line(t, recs[i]))
            fh.write(rec_line(t, host[t]))
            if first is None and differs(recs, host, kept, i):
                first = i
    print(f'{len(recs)} records of {len(kept)} game ticks -> {args.out} ({time.time() - t0:.0f} s)')
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
