"""A test disc in Mednafen with keys scripted: each list's master, slave and VDP1 time, the worst ones with a picture.
python -I tools/motion.py DISC ELF NAME START_FRAME 'KEY[+KEY]:FRAMES,...' [REPEATS]
Overlay paged away first. Prints percentiles, and the worst lists by master, by slave and by VDP1."""
import os
import statistics
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + '/tools')
os.chdir(ROOT)
from capture import Mednafen  # noqa: E402

NM = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-nm'
TICK_MS = 128 / 28636.36
FIELDS = ['fps', 'update', 'render', 'flush', 'faces', 'late', 'frames', 'draw', 'endline', 'vertices', 'vdp1', 'wait', 'slavewait', 'uploads', 'roomv', 'slave', 'slaveidle', 'dma', 'rooms', 'meshes']
disc, elf, name, start, script = sys.argv[1:6]
start = int(start)
repeats = int(sys.argv[6]) if len(sys.argv) > 6 else 1
OUT = f'{ROOT}/build/night/motion'
os.makedirs(OUT, exist_ok=True)
S = {}
for line in subprocess.run([NM, elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        S[p[2]] = int(p[0], 16)


def stats(m):
    return dict(zip(FIELDS, struct.unpack('>20I', m.read(S['_gStats'], 80))))


m = Mednafen(f'{disc}/tomb.cue', 'mo' + name[-6:])
rows = []
try:
    m.send('run_to_frame 1800', 'done run_to_frame', 900)
    for _ in range(120):
        if stats(m)['frames'] > 60:
            break
        m.send('frame_advance 10', 'done frame_advance', 60)
    for _ in range(4):
        m.send('input R', 'ok input', 10)
        m.send('frame_advance 10', 'done frame_advance', 30)
        m.send('input_release R', 'ok input_release', 10)
        m.send('frame_advance 10', 'done frame_advance', 30)
    m.send(f'run_to_frame {start}', 'done run_to_frame', 900)
    seen = -1
    f = start
    t0 = stats(m)['frames']
    vb0 = f
    for _ in range(repeats):
        for part in script.split(','):
            keys, n = part.split(':')
            keys = [] if keys == '-' else keys.split('+')
            for k in keys:
                m.send(f'input {k}', 'ok input', 10)
            for _ in range(int(n) // 2):
                m.send('frame_advance 2', 'done frame_advance', 60)
                f += 2
                s = stats(m)
                if s['frames'] != seen:
                    seen = s['frames']
                    s['at'] = f
                    rows.append(s)
            for k in keys:
                m.send(f'input_release {k}', 'ok input_release', 10)
    t1 = stats(m)['frames']
    print(f'{name}: {t1 - t0} lists in {f - vb0} vblanks = {(t1 - t0) * 60 / max(f - vb0, 1):.1f} fps')
    ms = lambda v: v * TICK_MS
    rows = rows[2:]
    for key, get in (('master', lambda r: ms((r['update'] + r['render'] - r['wait']) & 0xFFFFFFFF)), ('slave', lambda r: ms(r['slave'])), ('vdp1', lambda r: ms(r['vdp1']))):
        v = sorted(get(r) for r in rows)
        print(f'  {key:7} median {v[len(v) // 2]:6.2f}  90% {v[len(v) * 9 // 10]:6.2f}  99% {v[len(v) * 99 // 100]:6.2f}  worst {v[-1]:6.2f}')
        worst = sorted(rows, key=get)[-3:]
        for r in worst:
            print(f'     at frame {r["at"]}: master {get(r):6.2f} | master {ms(r["update"] + r["render"] - r["wait"]):6.2f} slave {ms(r["slave"]):6.2f} vdp1 {ms(r["vdp1"]):6.2f} commands {r["faces"]} rooms {r["rooms"]} meshes {r["meshes"]} roomv {r["roomv"]} uploads {r["uploads"]}')
    print('  commands median', statistics.median(r['faces'] for r in rows), 'most', max(r['faces'] for r in rows), ' roomv most', max(r['roomv'] for r in rows), ' meshes most', max(r['meshes'] for r in rows), ' uploads most', max(r['uploads'] for r in rows))
    m.send(f'screenshot {OUT}/{name}_end.png', 'screenshot', 30)
    m.send('frame_advance 1', 'done frame_advance', 30)
finally:
    m.quit()
    time.sleep(1)
