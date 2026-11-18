"""Boots a disc in a Mednafen of our own, presses buttons at frames, and takes
a picture at frames, printing the level and Lara's position, gun, its state
and her rounds with each (pictures in a directory of the run's own).
python -I tools/inputshot.py DISC ELF NAME PRESSES SHOTS
PRESSES: FRAME:BUTTON:VBLANKS,...  kill@FRAME (Lara's health poked to 0) and
         hp@FRAME:VALUE (to that)
SHOTS:   FRAME,FRAME,...
Buttons are Mednafen's (A action, B jump, C draw, START the ring, UP...)."""
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
disc, elf, name, presses, shots = sys.argv[1:6]
S = {}
for line in subprocess.run([BIN + 'nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        S[p[2]] = int(p[0], 16)
OUT = f'{ROOT}/build/{name}_shots_{os.getpid()}'
os.makedirs(OUT, exist_ok=True)

# ItemObj: pos +8, health +44, extraL +64; ExtraInfoLara: weaponState +2,
# weapon +10, head angles x, y +12, ammo[] +226 (int16 each)
events = []
for p in filter(None, presses.split(',')):
    if p.startswith('kill@'):
        events.append((int(p[5:]), 'kill', '0'))
        continue
    if p.startswith('hp@'):
        f, v = p[3:].split(':')
        events.append((int(f), 'kill', v))
        continue
    f, b, n = p.split(':')
    events.append((int(f), 'press', b))
    events.append((int(f) + int(n), 'release', b))
for f in filter(None, shots.split(',')):
    events.append((int(f), 'shot', ''))
events.sort(key=lambda e: (e[0], e[1] != 'release'))

m = Mednafen(f'{disc}/tomb.cue', name)
try:
    for fr, kind, b in events:
        m.send(f'run_to_frame {fr}', 'done run_to_frame', 1200)
        if kind == 'press':
            m.send(f'input {b}', 'ok input', 10)
        elif kind == 'release':
            m.send(f'input_release {b}', 'ok input_release', 10)
        elif kind == 'kill':
            lara = struct.unpack('>I', m.read(S['_players'], 4))[0]
            if lara:
                v = int(b) & 0xFFFF
                m.send('poke %08X %02X %02X' % (lara + 44, v >> 8, v & 0xFF), 'ok poke', 10)
        else:
            level = struct.unpack('>I', m.read(S['_gLevelID'], 4))[0]
            lara = struct.unpack('>I', m.read(S['_players'], 4))[0]
            info = ''
            if lara:
                lr = m.read(lara, 76)
                e = m.read(struct.unpack('>I', lr[64:68])[0], 240)
                info = 'lara %d %d %d health %d gun %d state %d rounds %d %d %d %d head %d %d' % (
                    struct.unpack('>3i', lr[8:20]) + struct.unpack('>h', lr[44:46]) + (e[10], e[2]) +
                    struct.unpack('>4h', e[226:234]) + struct.unpack('>2h', e[12:16]))
            m.send(f'screenshot {OUT}/{fr}.png', 'screenshot', 30)
            print(f'frame {fr} level {level} {info} -> {OUT}/{fr}.png', flush=True)
finally:
    m.quit()
    time.sleep(1)
