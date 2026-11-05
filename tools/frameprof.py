"""A view's whole frame by function, both CPUs, from a unified trace, the overlay paged away.
python -I tools/frameprof.py DISC ELF NAME [--lists N] [--frame F] [--hold KEYS:N] [--labels] [--top N]
Prints cycles a list by function and CPU, and inside rooms.s, faces .s etc. by label with --labels."""
import argparse
import bisect
import collections
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
ap = argparse.ArgumentParser()
ap.add_argument('disc')
ap.add_argument('elf')
ap.add_argument('name')
ap.add_argument('--lists', type=int, default=4, help='vblanks traced are 2 x this')
ap.add_argument('--frame', type=int, default=2300)
ap.add_argument('--hold', default='none')
ap.add_argument('--top', type=int, default=34)
ap.add_argument('--asm', default='', help='SOURCE.s:SYMBOL[,...]: split that source by its labels (SYMBOL its first global)')
ap.add_argument('--fn', default='', help='each pc of the functions whose name holds this, by address, with counts')
ap.add_argument('--stall', type=float, default=0)
ap.add_argument('--script', default='', help='START:KEY[+KEY]:FRAMES,...: keys held from frame START as motion.py, tracing from --frame')
ap.add_argument('--lines', type=int, default=0, help='with --asm: each instruction run at least this many times a list, in order')
ap.add_argument('--src', default=ROOT, help='the checkout the sources are in')
args = ap.parse_args()
syms = []
for line in subprocess.run([BIN + 'nm', '-C', '-n', args.elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3 and p[1] in 'tT':
        syms.append((int(p[0], 16), p[2]))
starts = [a for a, _ in syms]
S = {}
for line in subprocess.run([BIN + 'nm', args.elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        S[p[2]] = int(p[0], 16)
path = f'{ROOT}/build/night/{args.name}.trace.txt'
os.makedirs(os.path.dirname(path), exist_ok=True)
m = Mednafen(f'{args.disc}/tomb.cue', 'fp' + args.name[-6:])
FIELDS = ['fps', 'update', 'render', 'flush', 'faces', 'late', 'frames', 'draw', 'endline', 'vertices', 'vdp1', 'wait', 'slavewait', 'uploads', 'roomv', 'slave', 'slaveidle', 'dma', 'rooms', 'meshes']
try:
    m.send('run_to_frame 1800', 'done run_to_frame', 900)
    for _ in range(120):
        if struct.unpack('>20I', m.read(S['_gStats'], 80))[6] > 60:
            break
        m.send('frame_advance 10', 'done frame_advance', 60)
    for _ in range(4):
        m.send('input R', 'ok input', 10)
        m.send('frame_advance 10', 'done frame_advance', 30)
        m.send('input_release R', 'ok input_release', 10)
        m.send('frame_advance 10', 'done frame_advance', 30)
    if args.hold != 'none':
        keys, n = args.hold.split(':')
        until = args.frame - 120
        keys = keys.split(',')
        m.send(f'run_to_frame {until - int(n)}', 'done run_to_frame', 900)
        m.send(f'input {keys[0]}', 'ok input', 10)
        m.send('frame_advance 8', 'done frame_advance', 30)
        for k in keys[1:]:
            m.send(f'input {k}', 'ok input', 10)
        m.send(f'run_to_frame {until}', 'done run_to_frame', 900)
        m.send('input L', 'ok input', 10)
        m.send('frame_advance 10', 'done frame_advance', 30)
        m.send('input_release L', 'ok input_release', 10)
        for k in reversed(keys):
            m.send(f'input_release {k}', 'ok input_release', 10)
    if args.script:
        first, rest = args.script.split(':', 1)
        f = int(first)
        m.send(f'run_to_frame {f}', 'done run_to_frame', 900)
        parts = rest.split(',')
        i = 0
        while f < args.frame:
            keys, n = parts[i % len(parts)].split(':')
            i += 1
            keys = [] if keys == '-' else keys.split('+')
            for k in keys:
                m.send(f'input {k}', 'ok input', 10)
            n = int(n)
            step = min(n, args.frame - f)
            m.send(f'frame_advance {step}', 'done frame_advance', 120)
            f += step
            if f < args.frame:
                for k in keys:
                    m.send(f'input_release {k}', 'ok input_release', 10)
    else:
        m.send(f'run_to_frame {args.frame}', 'done run_to_frame', 900)
    f0 = struct.unpack('>20I', m.read(S['_gStats'], 80))
    m.send(f'unified_trace {path}', 'ok unified_trace', 10)
    m.send('insn_trace_unified 3 800000000', 'ok insn_trace_unified', 10)
    m.send(f'frame_advance {args.lists * 2}', 'done frame_advance', 3600)
    m.send('insn_trace_stop', 'ok insn_trace_stop', 60)
    m.send('unified_trace_stop', 'ok unified_trace_stop', 60)
    f1 = struct.unpack('>20I', m.read(S['_gStats'], 80))
finally:
    m.quit()
    time.sleep(1)
lists = f1[6] - f0[6]
stats = dict(zip(FIELDS, f1))
print(f'{args.name}: lists traced {lists} rooms {stats["rooms"]} roomv {stats["roomv"]} meshes {stats["meshes"]} commands {stats["faces"]}')
lists = max(lists, 1)
cost = collections.defaultdict(int)
count = collections.defaultdict(int)
prev = {}
with open(path, errors='replace') as f:
    for line in f:
        p = line.split(None, 3)
        if len(p) < 3 or p[1] not in ('m', 's'):
            continue
        try:
            t, cpu, pc = int(p[0]), p[1], int(p[2], 16) & 0x07FFFFFF
        except ValueError:
            continue
        if cpu in prev:
            ppc, pt = prev[cpu]
            if 0 <= t - pt < 100000:
                cost[(cpu, ppc)] += t - pt
                count[(cpu, ppc)] += 1
        prev[cpu] = (pc, t)
byfn = collections.defaultdict(lambda: [0, 0])
total = collections.defaultdict(int)
for (cpu, pc), c in cost.items():
    i = bisect.bisect_right(starts, pc) - 1
    name = syms[i][1] if i >= 0 else '?'
    byfn[(cpu, name)][0] += c
    byfn[(cpu, name)][1] += count[(cpu, pc)]
    total[cpu] += c
MS = 28636.36
print(f'cycles a list: master {total["m"] // lists} ({total["m"] / lists / MS:.2f} ms) slave {total["s"] // lists} ({total["s"] / lists / MS:.2f} ms)')
for (cpu, name), (c, n) in sorted(byfn.items(), key=lambda kv: -kv[1][0])[:args.top]:
    print(f'  {cpu} {c / lists / MS:6.2f} ms {c // lists:8} cyc {n // lists:8} ins {c / max(n, 1):5.2f}  {name[:64]}')
if args.fn:
    rows = []
    for (cpu, pc), c in cost.items():
        i = bisect.bisect_right(starts, pc) - 1
        if i >= 0 and args.fn in syms[i][1]:
            rows.append((pc, cpu, c // lists, count[(cpu, pc)] // lists, syms[i][1][:24]))
    for r in sorted(rows):
        print(f'  {r[0]:08x} {r[1]} {r[2]:7} cyc {r[3]:6} runs {r[4]}')
for spec in [x for x in args.asm.split(',') if x]:
    src, sym = spec.split(':')
    base = next(a for a, n in syms if n == sym.lstrip('_'))
    lst = path + '.lst'
    r = subprocess.run([BIN + 'as', '-big', '-almn=' + lst, '-o', path + '.o', '--defsym', 'NOTHING=0', f'{args.src}/{src}'], capture_output=True, text=True)
    sections = [{}]
    label = ''
    for line in open(lst, errors='replace'):
        t = re.match(r'\s*\d+\s+(?:[0-9a-fA-F]{4}\s*)?(\.L\w+|_\w+):', line)
        q = re.match(r'\s*(\d+)\s+([0-9a-fA-F]{4})\s+([0-9a-fA-F]{4})\s*(.*)$', line)
        if re.search(r'\.section\s|^\s*\d+\s+\.text', line):
            sections.append({})
        if t:
            label = t.group(1)
        if q:
            sections[-1][int(q.group(2), 16)] = (label, q.group(4).split('!')[0].strip())
    rows = next(x for x in sections if any(v[0] == sym for v in x.values()))
    start = next((k for k, v in sorted(rows.items()) if v[0] == sym), None)
    if start is None:
        start = next(k for k, v in sorted(rows.items()) if v[0] == '_' + sym)
    offset = base - start
    blocks = collections.OrderedDict()
    firsts = {}
    for k, (lab, txt) in sorted(rows.items()):
        firsts.setdefault(lab, k)
    for (cpu, pc), c in cost.items():
        off = pc - offset
        if off in rows:
            lab = rows[off][0]
            b = blocks.setdefault(lab, [0, 0])
            b[0] += c
            b[1] += count[(cpu, pc)]
    if args.lines:
        for off in sorted(rows):
            pc = off + offset
            n = sum(count.get((cpu, pc), 0) for cpu in 'ms') // lists
            c = sum(cost.get((cpu, pc), 0) for cpu in 'ms') // lists
            if n >= args.lines and (not args.stall or c >= args.stall * n):
                print(f'{off:5x} {n:5} {c:6} {rows[off][1][:44]:44} {rows[off][0]}')
    print(f'{src} by label, a list:')
    for lab, (c, n) in sorted(blocks.items(), key=lambda kv: -kv[1][0])[:30]:
        runs = sum(count.get((cpu, firsts[lab] + offset), 0) for cpu in 'ms') // lists
        print(f'  {lab:18} {c / lists / MS:6.2f} ms {c // lists:8} cyc {n // lists:8} ins, runs {runs}')
    for f in (lst, path + '.o'):
        if os.path.exists(f):
            os.remove(f)
os.remove(path)
