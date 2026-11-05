"""A trace's frame, lane by lane, in time order: python -I tools/tracelanes.py JSON [FRAME_INDEX=3] [MIN_MS=0.15]"""
import json
import sys

path = sys.argv[1]
which = int(sys.argv[2]) if len(sys.argv) > 2 else 3
least = float(sys.argv[3]) if len(sys.argv) > 3 else 0.15
data = json.load(open(path))
ev = data['traceEvents'] if isinstance(data, dict) else data
names = {}
for e in ev:
    if e.get('ph') == 'M' and e.get('name') == 'thread_name':
        names[(e['pid'], e['tid'])] = e['args']['name']
spans = [e for e in ev if e.get('ph') == 'X']
master_render = sorted((e for e in spans if e['name'].lower() in ('render',)), key=lambda e: e['ts'])
updates = sorted((e for e in spans if e['name'].lower() == 'update'), key=lambda e: e['ts'])
print('lanes', sorted(set(names.values())))
frames = [e for e in spans if 'frame' in e['name'].lower()]
ts = [e['ts'] for e in updates]
print('frame starts (ms):', [round((b - a) / 1000, 1) for a, b in zip(ts, ts[1:])])
t0, t1 = ts[which], ts[which + 1]
lanes = {}
for e in spans:
    if e['ts'] + e.get('dur', 0) >= t0 and e['ts'] < t1:
        lanes.setdefault(names.get((e['pid'], e['tid']), str(e['tid'])), []).append(e)
for lane, es in sorted(lanes.items()):
    print('==', lane)
    for e in sorted(es, key=lambda x: x['ts']):
        d = e.get('dur', 0) / 1000
        if d >= least:
            print(f"   {(e['ts'] - t0) / 1000:7.2f} .. {(e['ts'] + e.get('dur', 0) - t0) / 1000:7.2f}  {d:6.2f}  {e['name']}")
