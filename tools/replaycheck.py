"""A tick replay run on the host and on a replay build in Mednafen, the game
after each tick set side by side: the first tick where they part, or that
they ran alike.

    python -I tools/replaycheck.py REPLAY BUILD [--ticks N]

REPLAY is the replay file (tools/replay.py), BUILD the folder `make disc
BUILD=BUILD REPLAY=REPLAY` built (--make builds it first). The host
(build/host/logic, host/build.sh) prints its records with REC=1; the replay
build keeps the last 64 in gReplayRing (saturn/main.cpp), read every 40
vertical blanks.
"""
import argparse
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from capture import Mednafen, symbols, symbol  # noqa: E402

REC = struct.Struct('>IHHiiiHHHhII')
FIELDS = ('level', 'room', 'x', 'y', 'z', 'yaw', 'anim', 'frame', 'health', 'seed', 'items')
RING = 64


def host_records(replay, ticks):
    rel = os.path.relpath(replay, ROOT).replace('\\', '/')
    env = dict(os.environ, REPLAY=rel, REC='1', MSYS_NO_PATHCONV='1', WSLENV='REPLAY:REC')
    out = subprocess.run(['wsl.exe', '-d', 'Ubuntu-24.04', '--cd', ROOT.replace('\\', '/'), '--', 'qemu-ppc',
                          'build/host/logic', 'build/data', '-', str(ticks)],
                         env=env, capture_output=True, text=True)
    if out.returncode:
        sys.exit(f'host: {out.stderr.strip()}')
    recs = {}
    for line in out.stdout.splitlines():
        w = line.split()
        if w and w[0] == 'rec':
            # rec T level L room R pos X Y Z yaw A anim N frame F health H seed S items I
            recs[int(w[1])] = (int(w[3]), int(w[5]), int(w[7]), int(w[8]), int(w[9]), int(w[11]), int(w[13]),
                               int(w[15]), int(w[17]), int(w[19], 16), int(w[21], 16))
    return recs


def saturn_records(build, last):
    syms = symbols(os.path.join(build, '0MAIN.elf'))
    ring = symbol(syms, '_gReplayRing')
    m = Mednafen(os.path.join(build, 'disc', 'tomb.cue'), 'replaycheck')
    recs, frame, idle = {}, 0, 0
    try:
        while last not in recs and idle < 60:
            frame += 40
            m.send(f'run_to_frame {frame}', 'done run_to_frame', 600)
            data = m.read(ring, REC.size * RING)
            got = len(recs)
            for i in range(RING):
                r = REC.unpack_from(data, i * REC.size)
                if r[1]:    # a level: written (a replay starts in one)
                    recs.setdefault(r[0], r[1:])
            idle = idle + 1 if len(recs) == got else 0
    finally:
        m.quit()
    return recs, frame


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('replay')
    ap.add_argument('build')
    ap.add_argument('--ticks', type=int, default=0, help='this many ticks only (0: the whole replay)')
    ap.add_argument('--make', action='store_true', help='build the replay disc in BUILD first')
    a = ap.parse_args()
    if a.make:
        def rel(p):
            return os.path.relpath(os.path.abspath(p), ROOT).replace(os.sep, '/')
        made = subprocess.run(['make', 'disc', f'BUILD={rel(a.build)}', f'REPLAY={rel(a.replay)}', '-j8'], cwd=ROOT,
                              capture_output=True, text=True)
        if made.returncode:
            sys.exit(f'make: {made.stdout[-2000:]}{made.stderr[-2000:]}')
    host = host_records(os.path.abspath(a.replay), a.ticks)
    if not host:
        sys.exit('host: no records')
    last = max(host)
    saturn, frames = saturn_records(os.path.abspath(a.build), last)
    seen = sorted(t for t in saturn if t in host)
    print(f'host {len(host)} ticks; Mednafen {len(saturn)} ticks seen in {frames} vertical blanks, {len(seen)} of them the host\'s')
    missed = [t for t in range(last + 1) if t not in saturn]
    if missed:
        print(f'not seen on Mednafen: {len(missed)} ticks, first {missed[:10]}')
    parted = [t for t in seen if saturn[t] != host[t]]
    for t in parted[:5]:
        diffs = [f'{n} host {h} saturn {s}' for n, h, s in zip(FIELDS, host[t], saturn[t]) if h != s]
        print(f'PART at tick {t}: ' + ', '.join(diffs))
    if parted:
        later = [t for t in seen if t > parted[0]]
        print(f'{len(parted)} of {len(seen)} ticks differ; {len(later) - len(parted) + 1} after the first are alike')
        return 1
    print(f'ALIKE: every tick seen matches the host\'s ({seen[0]}..{seen[-1]})' if seen else 'nothing to compare')
    return 0


if __name__ == '__main__':
    sys.exit(main())
