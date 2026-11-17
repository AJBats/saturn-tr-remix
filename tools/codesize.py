"""The game as it ships (no HW_LOG, no testing flags), built in build/size,
and what it takes of high work RAM: code and data, bss, and the arena left
for the level (to the master's stack), whose last part holds the rooms'
faces at the Cistern; every byte here is one fewer for them.

    codesize.py                   build and print the totals
    codesize.py --save NAME       also keep its symbols' sizes (build/size/NAME.syms)
    codesize.py --diff NAME       and each symbol grown, shrunk, added or gone since NAME
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'toolchain', 'gcc-14.2.0', 'sh2eb-elf', 'bin', 'sh2eb-elf-')
BUILD = 'build/size'
HWRAM_TOP = 0x06100000
MASTER_STACK = 8 * 1024     # os.cpp


def sizes(elf):
    """Each symbol's size, by its demangled name and kind (T code, R rodata, D data, B bss)."""
    out = subprocess.run([BIN + 'nm', '-S', '-C', elf], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[1] != '' and len(parts[2]) == 1:
            key = f'{parts[2].upper()} {parts[3]}'
            syms[key] = syms.get(key, 0) + int(parts[1], 16)
    return syms


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--save')
    ap.add_argument('--diff')
    args = ap.parse_args()

    made = subprocess.run(['make', f'BUILD={BUILD}', f'{BUILD}/0MAIN.elf', '-j8'], cwd=ROOT,
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if made.returncode:
        sys.exit(made.stderr[-2000:] + f'\nmake: exit {made.returncode}')
    elf = os.path.join(ROOT, BUILD, '0MAIN.elf')

    sections = {}
    for line in subprocess.run([BIN + 'size', '-A', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0].startswith('.') and int(parts[2]) >= 0x06000000:
            sections[parts[0]] = int(parts[1])
    end = None
    for line in subprocess.run([BIN + 'nm', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        if line.endswith(' __bss_end'):
            end = int(line.split()[0], 16)
    code = sum(size for name, size in sections.items() if name != '.bss')
    arena = HWRAM_TOP - MASTER_STACK - ((end + 15) & ~15)
    print(f'code and data {code:7d}  ({", ".join(f"{n} {s}" for n, s in sorted(sections.items()) if n != ".bss")})')
    print(f'bss           {sections.get(".bss", 0):7d}')
    print(f'level arena   {arena:7d}  (bss ends {end:08x})')

    syms = sizes(elf)
    if args.save:
        with open(os.path.join(ROOT, BUILD, args.save + '.syms'), 'w', encoding='utf-8') as f:
            for key, size in sorted(syms.items()):
                f.write(f'{size} {key}\n')
    if args.diff:
        old = {}
        with open(os.path.join(ROOT, BUILD, args.diff + '.syms'), encoding='utf-8') as f:
            for line in f:
                size, key = line.rstrip('\n').split(' ', 1)
                old[key] = int(size)
        changes = sorted(((syms.get(k, 0) - old.get(k, 0), k) for k in set(syms) | set(old)
                          if syms.get(k, 0) != old.get(k, 0)), key=lambda c: -abs(c[0]))
        total = {}
        for delta, key in changes:
            total[key[0]] = total.get(key[0], 0) + delta
        print(f'since {args.diff}: ' + ', '.join(f'{kind} {delta:+d}' for kind, delta in sorted(total.items())) +
              '   (T code, R rodata, D data, B bss)')
        for delta, key in changes:
            note = ' (new)' if key not in old else ' (gone)' if key not in syms else ''
            print(f'  {delta:+7d}  {key}{note}')


if __name__ == '__main__':
    main()
