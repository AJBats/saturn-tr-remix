"""Moves issue N to the top of OPEN_ISSUES.md's Closed section with a closing line,
and marks it closed in the burn-down order.
python -I tools/close_issue.py N "Closed 2026-11-17: what fixed it, commit." """
import os
import re
import sys

PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'OPEN_ISSUES.md')
n, line = int(sys.argv[1]), sys.argv[2]
t = open(PATH, encoding='utf-8').read()
open_part, closed = t.split('\n## Closed\n', 1)
m = re.search(rf'^### {n}\. .*?(?=^### |^## |\Z)', open_part, re.M | re.S)
if not m:
    sys.exit(f'issue {n} not found in the open part')
entry = m.group(0).rstrip('\n')
open_part = open_part[:m.start()] + open_part[m.end():]
head, rest = entry.split('\n', 1)
entry = f'{head}\n*{line}*\n\n{rest}\n'
# the burn-down row that lists it ("| order | N. title | fix |", or "N, M. title"
# for several) gets "closed" in its fix column once none of its issues is open
k = 0
for row in re.finditer(r'^(\| \d+ \| ((?:\d+, )*\d+)\. [^|]*\| )([^|]*)\|$', open_part, re.M):
    listed = [int(v) for v in row.group(2).split(', ')]
    if n in listed and '(closed)' not in row.group(3) and \
            not any(re.search(rf'^### {m}\. ', open_part, re.M) for m in listed):
        open_part = open_part[:row.start()] + f'{row.group(1)}{row.group(3)}(closed) |' + open_part[row.end():]
        k = 1
        break
closed = '\n' + entry + closed if closed.startswith('\n') else '\n' + entry + '\n' + closed
open(PATH, 'w', encoding='utf-8', newline='\n').write(open_part + '\n## Closed\n' + closed)
print(f'issue {n} closed; burn-down row marked: {bool(k)}')
