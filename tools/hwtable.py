"""Self-tracing discs of a tree's HEAD at many spots, each read on the Saturn: a line a spot.
python -I tools/hwtable.py WORKTREE TAG SPOT [SPOT ...]   (builds in WORKTREE; don't edit it meanwhile)
A SPOT: a name of tools/hwtest.sh, NAME:LOOK_KEY:FRAMES, or at,LEVEL,ID,room@x:y:z,yaw.
Appends to build/night/TAG_table.txt in the main checkout."""
import json
import os
import re
import subprocess
import sys

MAIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = os.path.dirname(os.path.abspath(__file__))
tree, tag = sys.argv[1:3]
spots = sys.argv[3:]
out = f'{MAIN}/build/night/{tag}_table.txt'
os.makedirs(os.path.dirname(out), exist_ok=True)


for item in spots:
    env_look = {}
    if item.startswith('at,'):
        # at,LEVEL,ID,room@x:y:z,yaw
        _, lv, ident, start, yaw = item.split(',')
        env_look = {'AT_LEVEL': lv, 'AT_ID': ident, 'AT_START': start, 'AT_YAW': yaw}
        spot, look = 'at', []
        at_label = lv + '_r' + start.split('@')[0]
    else:
        spot, *look = item.split(':')
        at_label = ''
        if look:
            env_look = {'HW_LOOK': look[0], 'HW_LOOK_FOR': look[1]}
    def run(cmd, cwd, timeout=1200):
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout, env=dict(os.environ, HW_TRACE_AT='600', **env_look))
    r = run(['sh', 'tools/hwtest.sh', spot], tree)
    if r.returncode:
        open(out, 'a').write(f'{spot}: build failed {r.returncode}\n')
        continue
    label = at_label or (spot + ('_' + look[0] + look[1] if look else ''))
    folder = f'build/disc_{tag}_{label}'
    run(['sh', 'tools/snapshot.sh', f'{tag}_{label}'], tree)
    run(['cp', 'build/0MAIN.elf', f'build/{tag}_{label}.elf'], tree)
    name = f'{tag}_{label}'
    r = subprocess.run([sys.executable, '-I', f'{S}/hwtrace.py', f'{tree}/{folder}', name], cwd=MAIN, capture_output=True, text=True, timeout=1200)
    text = r.stdout
    m = re.search(r'in (\d+) frames \(([\d.]+) fps\)', text)
    stages = dict((a.strip(), float(b)) for a, b in re.findall(r'^\s+([A-Za-z() 0-9]+?)\s+([\d.]+)\s*$', text, re.M))
    work_master = sum(v for k, v in stages.items() if k in ('draws', 'chunks', 'draw list', 'pose', 'flush', 'objects', 'feed', 'upload', 'view', 'join', 'render', 'plan', 'update', 'overlay', 'debug', 'lara', 'item', 'shimmers'))
    work_slave = sum(v for k, v in stages.items() if k in ('rooms', 'slave walk', 'slave shimmers', 'slave update', 'jobs', 'slave join', 'ahead'))
    line = (f'{label:30} fps {m.group(2) if m else "?":6} frames {m.group(1) if m else "?":3} master work {work_master:5.1f} WAIT slave {stages.get("WAIT slave", 0):4.1f} '
            f'vdp1wait {stages.get("WAIT vdp1", 0):4.1f} slave work {work_slave:5.1f} (rooms {stages.get("rooms", 0):4.1f} walk {stages.get("slave walk", 0):3.1f} upd {stages.get("slave update", 0):3.1f} '
            f'shim {stages.get("slave shimmers", 0):3.1f}+{stages.get("shimmers", 0):3.1f} ahead {stages.get("ahead", 0):4.1f}) idle {stages.get("(slave idle)", 0):3.1f} draws {stages.get("draws", 0):4.1f} chunks {stages.get("chunks", 0):4.1f}')
    open(out, 'a').write(line + '\n')
    open(f'{MAIN}/build/night/{name}.txt', 'w').write(text)
    print(line, flush=True)
