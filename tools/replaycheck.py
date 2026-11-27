"""A tick replay run on the host and on a replay build in Mednafen, the game
after each tick set side by side: the first tick where they part, or that
they ran alike; the levels the run went through on the way, and how the game
went on after its last tick (a whole run's end: the ending's films and the
credits, in pictures).

    python -I tools/replaycheck.py REPLAY BUILD [--ticks N] [--make] [--name NAME]

REPLAY is the replay file (tools/replay.py), BUILD the folder `make disc
BUILD=BUILD REPLAY=REPLAY` built (--make builds it first). The host
(build/host/logic, host/build.sh; HOST_LOGIC names another) prints its records
with REC=1, to BUILD/replaycheck_host.txt. The replay build keeps the last 64
in gReplayRing (saturn/main.cpp), read every READ_EVERY vertical blanks,
Mednafen running as fast as it can. A replay too long for the build (a whole
chained bot run) is a window in it (tools/replay.py), kept written ahead of
the game's cursor.

Films and videos between levels play out as they would (the game ticks none
of them); the level's statistics, "Save Game?" and the passport take the
replay's own presses, as on the host. While no tick comes, a picture every
STILL_SHOT vertical blanks (the videos, the ending's credits); no tick for
STUCK vertical blanks: held somewhere, a picture of it taken and the run
stopped.

After the replay's last tick it runs --after vertical blanks more, a picture
every --shot-every of them in BUILD/replaycheck_shots/.
"""
import argparse
import os
import struct
import subprocess
import sys
import time
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from capture import Mednafen, symbols, symbol  # noqa: E402
import replay as replay_file  # noqa: E402

REC = struct.Struct('>IHHiiiHHHhII')
FIELDS = ('level', 'room', 'x', 'y', 'z', 'yaw', 'anim', 'frame', 'health', 'seed', 'items')
RING = 64
READ_EVERY = 40         # a tick takes two vertical blanks or more: each stays in the ring through three reads
FILL_EVERY = 8          # reads between window fills (a fill's worth of pairs is far more than 8 reads use)
STUCK = 20000           # the longest video between levels runs under 9000
STILL_SHOT = 600        # with no tick (a video between levels), a picture this often
LEVEL_NAMES = ['TITLE', 'GYM', 'Caves', 'Vilcabamba', 'Lost Valley', 'Qualopec', 'CUT1', "St. Francis' Folly",
               'Colosseum', 'Palace Midas', 'The Cistern', 'Tihocan', 'CUT2', 'City of Khamoon', 'Obelisk of Khamoon',
               'Sanctuary of the Scion', "Natla's Mines", 'CUT3', 'Atlantis', 'CUT4', 'The Great Pyramid']
HOST = os.environ.get('HOST_LOGIC', 'build/host/logic')


def level_name(level):
    return LEVEL_NAMES[level] if level < len(LEVEL_NAMES) else f'level {level}'


def host_run(replay, ticks, out):
    """The host's records after each tick, to the file out; its first and last ticks."""
    rel = os.path.relpath(replay, ROOT).replace('\\', '/')
    env = dict(os.environ, REPLAY=rel, REC='1', MSYS_NO_PATHCONV='1', WSLENV='REPLAY:REC')
    with open(out, 'w') as f:
        run = subprocess.run(['wsl.exe', '-d', 'Ubuntu-24.04', '--cd', ROOT.replace('\\', '/'), '--', 'qemu-ppc',
                              HOST, 'build/data', '-', str(ticks)], env=env, stdout=f, stderr=subprocess.PIPE, text=True)
    if run.returncode:
        sys.exit(f'host: {run.stderr.strip()}')
    first = last = None
    for t, rec in host_records(out):
        if first is None:
            first = (t, rec)
        last = t
    if first is None:
        sys.exit('host: no records')
    return first, last


def host_records(path):
    """(tick, record) in tick order, from the host's file."""
    with open(path) as f:
        for line in f:
            w = line.split()
            if w and w[0] == 'rec':
                # rec T level L room R pos X Y Z yaw A anim N frame F health H seed S items I [inv V]
                yield int(w[1]), (int(w[3]), int(w[5]), int(w[7]), int(w[8]), int(w[9]), int(w[11]), int(w[13]),
                                  int(w[15]), int(w[17]), int(w[19], 16), int(w[21], 16))


class Window:
    """A replay too long for the build: its pairs, pair p at slot p % WINDOW of
    kReplay, written from the master's cursor on (the build holds the first
    WINDOW)."""

    def __init__(self, path, base):
        _, _, runs = replay_file.read(path)
        self.pairs = [(n, sum(replay_file.BITS[k] for k in names)) for n, names in replay_file.pairs_of(runs)]
        self.base = base
        self.written = replay_file.WINDOW

    def fill(self, m, at):
        end = at + replay_file.WINDOW
        while self.written < end:
            slot = self.written % replay_file.WINDOW
            n = min(end - self.written, replay_file.WINDOW - slot, 512)
            data = b''.join(struct.pack('>HH', *(self.pairs[p] if p < len(self.pairs) else (0, 0)))
                            for p in range(self.written, self.written + n))
            m.send(f'poke {self.base + slot * 4:x} ' + ' '.join(f'{b:02x}' for b in data), 'ok poke', 30)
            self.written += n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('replay')
    ap.add_argument('build')
    ap.add_argument('--ticks', type=int, default=0, help='this many ticks only (0: the whole replay)')
    ap.add_argument('--make', action='store_true', help='build the replay disc in BUILD first')
    ap.add_argument('--name', default='replaycheck', help='its Mednafen (build/NAME_home): another name runs beside it')
    ap.add_argument('--after', type=int, default=3600, help='vertical blanks run on after the last tick')
    ap.add_argument('--shot-every', type=int, default=300, help='vertical blanks between pictures after the last tick')
    ap.add_argument('--save-at', type=int, default=0, help='a save state (to --state) once the game reaches this tick, '
                    'and the run stops there')
    ap.add_argument('--state', help='the save state --save-at writes')
    a = ap.parse_args()
    build = os.path.abspath(a.build)
    if a.make:
        def rel(p):
            return os.path.relpath(os.path.abspath(p), ROOT).replace(os.sep, '/')
        made = subprocess.run(['make', 'disc', f'BUILD={rel(a.build)}', f'REPLAY={rel(a.replay)}', '-j8'], cwd=ROOT,
                              capture_output=True, text=True)
        if made.returncode:
            sys.exit(f'make: {made.stdout[-2000:]}{made.stderr[-2000:]}')
    t0 = time.time()
    host_file = os.path.join(build, 'replaycheck_host.txt')
    (first_tick, first_rec), last = host_run(os.path.abspath(a.replay), a.ticks, host_file)
    print(f'host: ticks {first_tick}..{last} in {time.time() - t0:.0f} s', flush=True)

    syms = symbols(os.path.join(build, '0MAIN.elf'))
    ring = symbol(syms, '_gReplayRing')
    window = None
    replay_syms = syms.get('_kReplay', [])
    if replay_syms and replay_syms[0][1] == replay_file.WINDOW * 4:
        window = Window(a.replay, replay_syms[0][0])
        cursor = symbol(syms, '_gReplayMaster')
        print(f'the replay a window in the build: {len(window.pairs)} pairs', flush=True)
    level_id = symbol(syms, '_gLevelID')

    shots = os.path.join(build, 'replaycheck_shots')
    os.makedirs(shots, exist_ok=True)
    m = Mednafen(os.path.join(build, 'disc', 'tomb.cue'), a.name, fast=True)
    hosts = host_records(host_file)
    pending = {}            # tick: Counter of its records read
    final = first_tick      # ticks before it are settled
    newest = -1
    started = saved = False
    frame = reads = 0
    stats = Counter()
    part = None
    level = None
    since_tick = stills = 0
    t0 = time.time()
    try:
        def settle(upto):
            """The ticks before upto, left the ring: each compared with the host's."""
            nonlocal final, part, level
            for t in range(final, upto):
                c = pending.pop(t, None)
                ht, hrec = next(hosts)
                while ht < t:
                    ht, hrec = next(hosts)
                if not c:
                    stats['missed'] += 1
                    continue
                rec, n = c.most_common(1)[0]
                if n < 2:
                    stats['unsure'] += 1
                    continue
                stats['compared'] += 1
                if rec[0] != level:
                    level = rec[0]
                    print(f'  tick {t:7}: {level_name(level)} (vertical blank {frame}, {time.time() - t0:.0f} s)',
                          flush=True)
                if rec != hrec and part is None:
                    part = t
                    diffs = [f'{f} host {h} saturn {s}' for f, h, s in zip(FIELDS, hrec, rec) if h != s]
                    print(f'PART at tick {t}: ' + ', '.join(diffs), flush=True)
                elif rec != hrec:
                    stats['differ'] += 1
            final = max(final, upto)

        while final <= last:
            frame += READ_EVERY
            m.send(f'run_to_frame {frame}', 'done run_to_frame', 600)
            reads += 1
            if window and started and reads % FILL_EVERY == 0:
                at = m.word(cursor)
                if at > len(window.pairs):
                    sys.exit(f"the replay's cursor reads {at}: past its {len(window.pairs)} pairs")
                window.fill(m, at)
            data = m.read(ring, REC.size * RING)
            if not started:
                # nothing counted till the run's first tick is in the ring:
                # while the disc boots, its memory holds leftovers
                if REC.unpack_from(data, 0)[1:] != first_rec:
                    if frame > 20000:
                        sys.exit('the replay build never started as the host did')
                    continue
                started = True
                print(f'started at vertical blank {frame}', flush=True)
            got = newest
            for i in range(RING):
                r = REC.unpack_from(data, i * REC.size)
                # (the game ticks on past the replay's last: those settle the last ones)
                if final <= r[0] < final + 100000 and r[0] & 63 == i:
                    if r[0] <= last:
                        pending.setdefault(r[0], Counter())[r[1:]] += 1
                    newest = max(newest, r[0])
            since_tick = since_tick + READ_EVERY if newest == got else 0
            if since_tick and since_tick % STILL_SHOT == 0:
                # no tick: a video or a picture between levels, or a load
                shot = os.path.join(shots, f'still_{frame:08}.png').replace(os.sep, '/')
                m.send(f'screenshot {shot}', 'screenshot', 30)
                stills += 1
            settle(min(newest - RING + 1, last + 1))
            if a.save_at and newest >= a.save_at:
                state = os.path.abspath(a.state).replace(os.sep, '/')
                m.send(f'save_state {state}', 'ok save_state', 120)
                print(f'saved the state at tick {newest} (vertical blank {frame}): {state}', flush=True)
                saved = True
                break
            if part is not None and final > part + 300:
                break
            if since_tick >= STUCK:
                shot = os.path.join(shots, f'held_{frame}.png').replace(os.sep, '/')
                m.send(f'screenshot {shot}', 'screenshot', 30)
                print(f'HELD: no tick for {STUCK} vertical blanks after tick {newest} ({shot})', flush=True)
                break
            if frame % 36000 == 0:
                rate = frame / (time.time() - t0)
                print(f'  vertical blank {frame}: tick {newest} ({rate:.0f} vertical blanks a second)', flush=True)
        else:
            settle(last + 1)
            print(f'the replay played to its last tick {last} at vertical blank {frame} ({time.time() - t0:.0f} s); '
                  f'on {a.after} vertical blanks', flush=True)
            for k in range(a.after // a.shot_every):
                m.send(f'run_to_frame {frame + (k + 1) * a.shot_every}', 'done run_to_frame', 600)
                shot = os.path.join(shots, f'after_{k:03}.png').replace(os.sep, '/')
                m.send(f'screenshot {shot}', 'screenshot', 30)
                lvl, = struct.unpack('>i', m.read(level_id, 4))
                print(f'  +{(k + 1) * a.shot_every}: {level_name(lvl)}, picture {shot}', flush=True)
    finally:
        m.quit()
    print(f'compared {stats["compared"]} ticks; not read {stats["missed"]}; read once only {stats["unsure"]}; '
          f'{stills} pictures while no tick came, in {shots}')
    if part is not None:
        print(f'PARTED at tick {part} ({stats["differ"]} more ticks differ after it)')
        return 1
    if final <= last and not saved:
        return 1
    if saved:
        print("ALIKE to there: every tick compared matches the host's")
        return 0
    print('ALIKE: every tick compared matches the host\'s')
    return 0


if __name__ == '__main__':
    sys.exit(main())
