"""Every level loaded and run a while in Mednafen from one disc: frames moving, stats, a picture, no crash.
python -I tools/levelsmoke.py DISC ELF NAME [FIRST_ID LAST_ID]"""
import bisect
import os
import re
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + '/tools')
os.chdir(ROOT)
from capture import Mednafen  # noqa: E402

BIN = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-'
TICK_MS = 128 / 28636.36
disc, elf, name = sys.argv[1:4]
first = int(sys.argv[4]) if len(sys.argv) > 4 else 1
last = int(sys.argv[5]) if len(sys.argv) > 5 else 20
OUT = f'{ROOT}/build/night/{name}'
os.makedirs(OUT, exist_ok=True)
S = {}
for line in subprocess.run([BIN + 'nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        S[p[2]] = int(p[0], 16)
syms = sorted((int(a, 16), n) for a, t, n in (l.split(None, 2) for l in subprocess.run([BIN + 'nm', '-C', elf], capture_output=True, text=True).stdout.splitlines() if len(l.split(None, 2)) == 3) if t in 'tT')
starts = [a for a, _ in syms]


def named(pc):
    i = bisect.bisect_right(starts, pc & 0x07FFFFFF) - 1
    return f'{pc:08x} {syms[i][1][:50]}+{(pc & 0x07FFFFFF) - syms[i][0]:x}' if i >= 0 else f'{pc:08x}'


def word(m, a):
    return struct.unpack('>I', m.read(a, 4))[0]


def stats(m):
    return struct.unpack('>20I', m.read(S['_gStats'], 80))


m = Mednafen(f'{disc}/tomb.cue', 'ls' + name[-6:])
frame = 2600
try:
    m.send(f'run_to_frame {frame}', 'done run_to_frame', 1200)
    for level in range(first, last + 1):
        m.send(f'poke {S["_gNextLevel"]:x} 0 0 0 {level:x}', 'ok poke', 10)
        f0 = stats(m)[6]
        moved = False
        for step in range(60):
            frame += 120
            m.send(f'run_to_frame {frame}', 'done run_to_frame', 600)
            cur = word(m, S['_gLevelID'])
            f = stats(m)[6]
            if cur == level and f > f0 + 30:
                moved = True
                break
        frame += 240
        m.send(f'run_to_frame {frame}', 'done run_to_frame', 600)
        s = stats(m)
        regs = re.search(r'PC=([0-9A-Fa-f]{8})', m.send('dump_regs', 'R0=', 10))
        pc = int(regs.group(1), 16) if regs else 0
        sr = re.search(r'PC=([0-9A-Fa-f]{8})', m.send('dump_slave_regs', 'R0=', 10))
        spc = int(sr.group(1), 16) if sr else 0
        path = f'{OUT}/level_{level:02}.png'
        if os.path.exists(path):
            os.replace(path, path + '.old')
        m.send(f'screenshot {path}', 'screenshot', 30)
        print(f'level {level:2}: {"ok " if moved else "NOT MOVING"} now {word(m, S["_gLevelID"]):2} frames {s[6]:6} cpu {s[2] * TICK_MS:5.1f} slave {s[15] * TICK_MS:5.1f} vdp1 {s[10] * TICK_MS:5.1f} rooms {s[18]:2} roomv {s[14]:4} high left? master {named(pc)[:40]} slave {named(spc)[:40]}', flush=True)
        if 'crash' in named(pc) or 'crash' in named(spc):
            print('  CRASHED')
            break
finally:
    m.quit()
    time.sleep(1)
