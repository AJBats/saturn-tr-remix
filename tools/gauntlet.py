"""The lab's scenes, all of them at once: each built on a disc of its own
(make lab LAB=build/lab_SCENE LAB_SCENE=SCENE) and run in a Mednafen of its
own (lab.py), a few at a time; then each one's results, in order.

    gauntlet.py [--jobs 4] [SCENE ...]      (the eight by default)

A scene's results go to build/lab_SCENE/run.txt; a scene whose build or
run fails is said to, with its exit code. Mednafen's times are emulated
cycles, the same however many run beside each other.
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENES = ['ledge', 'wolves', 'wolves_aim', 'pool', 'first', 'hints', 'under_water', 'fill']


def run(scene):
    lab = f'build/lab_{scene}'
    os.makedirs(os.path.join(ROOT, lab), exist_ok=True)
    out = os.path.join(ROOT, lab, 'run.txt')
    with open(out, 'w') as f:
        made = subprocess.run(['make', 'lab', f'LAB={lab}', f'LAB_SCENE={scene}', '-j4'], cwd=ROOT,
                              stdout=subprocess.DEVNULL, stderr=f)
        if made.returncode:
            return scene, f'build exit {made.returncode}', out
        ran = subprocess.run([sys.executable, '-I', 'tools/lab.py', '--scene', scene,
                              '--cue', os.path.join(ROOT, lab, 'disc', 'tomb.cue')],
                             cwd=ROOT, stdout=f, stderr=subprocess.STDOUT)
    return scene, f'exit {ran.returncode}', out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('scenes', nargs='*', default=SCENES)
    args = ap.parse_args()
    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(run, args.scenes))
    for scene, status, out in results:
        print(f'== {scene} {status}')
        with open(out) as f:
            lines = f.read().splitlines()
        print(lines[0] if lines else '(no output)')


if __name__ == '__main__':
    main()
