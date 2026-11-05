"""The lab's gauntlet in a worktree, its verdict in a line a scene. python -I tools/labcheck.py [WORKTREE] [scene ...]
`first`'s 12 commands off (8446 pixels) are old: it prints NOT CLEAN: 5 for that alone.
Prints every exact/off line with something off, and the times that matter."""
import os
import re
import subprocess
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
scenes = sys.argv[2:]
r = subprocess.run([sys.executable, '-I', 'tools/gauntlet.py', '--jobs', '4'] + scenes, cwd=root, capture_output=True, text=True)
bad = 0
for line in r.stdout.splitlines():
    m = re.match(r'== (\w+) (.*)', line)
    if not m:
        continue
    scene, status = m.groups()
    path = os.path.join(root, 'build', f'lab_{scene}', 'run.txt')
    text = open(path, errors='replace').read() if os.path.exists(path) else ''
    offs = []
    for row in text.splitlines():
        for k in re.finditer(r'(\d+) (?:off|differ)\b', row):
            if int(k.group(1)):
                offs.append(row.strip()[:110])
                break
    px = re.findall(r'(\d+) pixels differ', text)
    print(f'{scene:12} {status:10} off-lines {len(offs)} pixels {px}')
    for o in offs[:6]:
        print('    ' + o)
    bad += len(offs) + (status != 'exit 0')
print('ALL CLEAN' if not bad else f'NOT CLEAN: {bad}')
sys.exit(1 if bad else 0)
