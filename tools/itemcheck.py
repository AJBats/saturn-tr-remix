"""Boots a disc in a Mednafen of our own, runs to each frame, prints the named items' type, flags and
status, and sector floors (s:room:sx:sz).
python -I tools/itemcheck.py DISC ELF NAME FRAME[,FRAME...] ITEM|s:room:sx:sz ..."""
import os
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + '/tools')
os.chdir(ROOT)
from capture import Mednafen  # noqa: E402

BIN = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-'
disc, elf, name, frames = sys.argv[1:5]
wanted = [int(a) for a in sys.argv[5:] if not a.startswith('s:')]
sectors = [tuple(int(v) for v in a[2:].split(':')) for a in sys.argv[5:] if a.startswith('s:')]
S = {}
for line in subprocess.run([BIN + 'nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        S[p[2]] = int(p[0], 16)
STATUS = ['none', 'active', 'inactive', 'invisible']


def report(m, fr):
    print('frame', fr, 'level', struct.unpack('>I', m.read(S['_gLevelID'], 4))[0])
    for i in wanted:
        raw = m.read(S['_items'] + i * 76, 76)
        flags = struct.unpack('>H', raw[26:28])[0]
        x, y, z = struct.unpack('>3i', raw[8:20])
        print(f'  item {i}: type {raw[52]} flags {flags:04x} status {STATUS[(flags >> 3) & 3]} active-list {(flags >> 2) & 1} '
              f'mask {(flags >> 9) & 31:02x} pos {x} {y} {z} state {raw[36]}')
    for room, sx, sz in sectors:
        r = m.read(S['_rooms'] + room * 64, 12)
        info, secs = struct.unpack('>II', r[4:12])
        zs = m.read(info + 21, 1)[0]
        sec = m.read(secs + (sx * zs + sz) * 8, 8)
        print(f'  room {room} sector {sx}:{sz}: floor {struct.unpack(">b", sec[5:6])[0]} ceiling {struct.unpack(">b", sec[7:8])[0]}')
    lara = struct.unpack('>I', m.read(S['_players'], 4))[0]
    if lara:
        lr = m.read(lara, 76)
        print('  lara pos %d %d %d health %d' % (struct.unpack('>3i', lr[8:20]) + struct.unpack('>h', lr[44:46])))


m = Mednafen(f'{disc}/tomb.cue', name)
try:
    if os.environ.get('HOLD'):      # FRAME:BUTTON:VBLANKS, the button held from that frame
        at, button, n = os.environ['HOLD'].split(':')
        m.send(f'run_to_frame {at}', 'done run_to_frame', 1200)
        for b in button.split('+'):
            m.send(f'input {b}', 'ok input', 10)
            m.send('frame_advance 30', 'done frame_advance', 60)
        m.send(f'frame_advance {n}', 'done frame_advance', 60)
        for b in button.split('+'):
            m.send(f'input_release {b}', 'ok input_release', 10)
    for fr in frames.split(','):
        m.send(f'run_to_frame {fr}', 'done run_to_frame', 1200)
        report(m, fr)
        if os.environ.get('SHOTS'):     # a picture at every frame, in a directory of the run's own
            d = f'{ROOT}/build/{name}_shots_{os.getpid()}'
            os.makedirs(d, exist_ok=True)
            m.send(f'screenshot {d}/{fr}.png', 'screenshot', 30)
            print(f'{d}/{fr}.png')
    shot = f'{ROOT}/build/{name}_shot.png'
    if os.path.exists(shot):
        os.replace(shot, shot + '.old')
    m.send(f'screenshot {shot}', 'screenshot', 30)
    print(shot)
finally:
    m.quit()
    time.sleep(1)
