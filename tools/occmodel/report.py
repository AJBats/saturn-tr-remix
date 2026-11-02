"""occmodel's lines as a table: each way against the baseline, for a band of views.

usage: python -I report.py BAND FILE ...   (BAND all, heavy or big)
"""
import sys
from collections import defaultdict

MS = 1 / 28636.36       # a cycle in ms


def main():
    band = sys.argv[1]
    rows = defaultdict(dict)
    extra = {}
    hot = {}
    for path in sys.argv[2:]:
        for line in open(path):
            p = line.split()
            if len(p) > 3 and p[1] in ('all', 'heavy', 'big'):
                d = {p[i]: float(p[i + 1]) for i in range(3, len(p) - 1, 2)}
                rows[(p[0], p[1])][p[2]] = d
            elif len(p) > 3 and p[1] == 'pvsdata':
                extra[p[0]] = {p[i]: float(p[i + 1]) for i in range(2, len(p) - 1, 2)}
            elif len(p) > 3 and p[1] == 'hotdata':
                hot[p[0]] = {p[i]: float(p[i + 1]) for i in range(2, len(p) - 1, 2)}
    ways = {}
    for (level, b), d in rows.items():
        if b != band:
            continue
        for way, r in d.items():
            ways.setdefault(way, []).append((level, r))
    print(f'{band} views, mean over the levels weighted by views; ms are Mednafen cycles of room work (faces + projection), VDP1 modelled')
    print(f'{"way":14}{"views":>7}{"rooms":>7}{"verts":>7}{"kept":>6}{"CPU ms":>8}{"VDP1 ms":>8}{"walk ms":>8}{"net CPU":>8}{"lost px":>8}{"views lost>0":>13}{"lost>=64":>9}{"worst":>7}{"lit lost":>9}')
    base = None
    for way in ways:
        t = defaultdict(float)
        n = 0
        worst = 0
        for level, r in ways[way]:
            n += r['views']
            for k in ('rooms', 'verts', 'kept', 'cycles', 'vdp1', 'test', 'lost'):
                t[k] += r[k] * r['views']
            t['lostviews'] += r['lostviews']
            t['lit'] += r.get('lit', 0) * r['views']
            t['lost64'] += r['lost64']
            worst = max(worst, r['worst'])
        if n == 0:
            continue
        for k in ('rooms', 'verts', 'kept', 'cycles', 'vdp1', 'test', 'lost'):
            t[k] /= n
        if way == 'base':
            base = dict(t)
        net = (t['cycles'] + t['test'] - base['cycles'] - base['test']) * MS if base else 0
        print(f'{way:14}{n:7.0f}{t["rooms"]:7.1f}{t["verts"]:7.0f}{t["kept"]:6.0f}{t["cycles"] * MS:8.2f}{t["vdp1"] * MS:8.2f}{t["test"] * MS:8.2f}{net:8.2f}{t["lost"]:8.1f}{t["lostviews"]:13.0f}{t["lost64"]:9.0f}{worst:7.0f}{t["lit"] / n:9.1f}')
    if hot:
        print('hot data, bytes:', ' '.join(f"{k.split('.')[0]} {v['bytes']:.0f}" for k, v in sorted(hot.items())))
    if extra:
        print('PVS data, bytes: rooms by cell / rooms by cell and octant (distinct sets + a 16-bit index)')
        for level, e in sorted(extra.items()):
            print(f"   {level:14} cells {e['cells']:6.0f} used {e['used']:6.0f}: {e['bytes']:8.0f} / {e['octbytes']:8.0f}")


main()
