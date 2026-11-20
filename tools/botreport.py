"""The playtest bot's whole run as a QA report: the host built, every level's
route played chained (the inventory carried), and per level what it got
against Stella's 100% totals, the steps that failed on the way (the bot
recovers from most), and where the run stopped.

    python -I tools/botreport.py [--out build/bot/report.txt]

The replay of the run goes to build/bot/CHAIN.rpl (make disc REPLAY= plays
it on the Saturn; tools/retailrun.py plays it on retail).
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# route, level name, Stella's totals (kills, pickups, secrets)
LEVELS = [
    ('LEVEL1.route', 'Caves', (14, 7, 3)),
    ('LEVEL2.route', 'Vilcabamba', (29, 13, 3)),
    ('LEVEL3A.route', 'Lost Valley', (13, 16, 5)),
    ('LEVEL3B.route', 'Tomb of Qualopec', (8, 8, 3)),     # 7 and the mummy
]


def wsl(cmd):
    return subprocess.run(['wsl.exe', '-d', 'Ubuntu-24.04', '--cd', ROOT.replace('\\', '/'), '--', 'sh', '-c', cmd],
                          capture_output=True, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build/bot/report.txt'))
    args = ap.parse_args()
    build = wsl('sh host/build.sh')
    if build.returncode:
        sys.exit('host build failed:\n' + build.stderr)
    routes = ','.join('host/routes/' + r for r, _, _ in LEVELS)
    run = wsl(f'sh build/bot/run.sh {routes} build/bot/CHAIN.rpl')
    out = run.stdout + run.stderr
    open(os.path.join(ROOT, 'build/bot/CHAIN.out'), 'w').write(out)

    finished = re.findall(r'last level finished: kills (\d+), pickups (\d+), secrets (\d+), (\d+) s', out)
    # each level's stats are printed when it ends (and once more at the run's end)
    ends = []
    for f in finished:
        if not ends or ends[-1] != f:
            ends.append(f)
    lines = []
    lines.append('level              kills    pickups  secrets  time')
    for i, (route, name, (k, p, s)) in enumerate(LEVELS):
        if i < len(ends):
            gk, gp, gs, t = map(int, ends[i])
            mark = '' if (gk, gp, gs) >= (k, p, s) else '  <- short'
            lines.append(f'{name:18} {gk:2}/{k:<4}  {gp:2}/{p:<4}  {gs}/{s:<5}  {t // 60}:{t % 60:02}{mark}')
        else:
            lines.append(f'{name:18} not finished')
    result = re.search(r'ROUTE (DONE|FAILED): (\d+) ticks \((\d+) s of play\)', out)
    if result:
        lines.append(f'run {result.group(1).lower()}: {result.group(2)} ticks ({int(result.group(3)) // 60} min of play)')
    fails = [l for l in out.splitlines() if l.startswith('FAIL')]
    lines.append(f'{len(fails)} steps failed and were recovered or ended the run:')
    lines += ['  ' + l for l in fails]
    stop = [l for l in out.splitlines() if re.match(r'line +\d+ .* FAIL ', l)]
    if stop:
        lines.append('stopped at: ' + stop[-1].strip())
    text = '\n'.join(lines) + '\n'
    open(args.out, 'w').write(text)
    print(text, end='')
    return 0 if result and result.group(1) == 'DONE' else 1


if __name__ == '__main__':
    sys.exit(main())
