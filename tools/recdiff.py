"""Two REC files (host/host.cpp's REC=1 lines, tools/retailrun.py's) compared tick by tick
on Lara and the control seed: the first tick that differs and its neighbours.
python -I tools/recdiff.py A B [CONTEXT]"""
import sys

FIELDS = ('room', 'pos', 'yaw', 'anim', 'frame', 'health', 'seed')


def load(path):
    recs = {}
    for line in open(path):
        w = line.split()
        if not w or w[0] != 'rec':
            continue
        t = int(w[1])
        d = {}
        i = 2
        while i < len(w):
            k = w[i]
            if k == 'pos':
                d[k] = tuple(w[i + 1:i + 4])
                i += 4
            else:
                d[k] = w[i + 1]
                i += 2
        recs[t] = d
    return recs


a, b = load(sys.argv[1]), load(sys.argv[2])
ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 3
common = sorted(set(a) & set(b))
first = None
for t in common:
    if any(a[t].get(f) != b[t].get(f) for f in FIELDS):
        first = t
        break
print(f'{len(common)} ticks in both ({common[0]}..{common[-1]})')
if first is None:
    print('ALIKE on Lara and the seed')
    sys.exit(0)
print(f'first difference at tick {first}:')
for t in range(first - ctx, first + ctx + 1):
    if t in a and t in b:
        for name, r in (('A', a[t]), ('B', b[t])):
            print(f'  {t} {name}: ' + ' '.join(f'{f} {" ".join(r[f]) if f == "pos" else r.get(f)}' for f in FIELDS))
sys.exit(1)
