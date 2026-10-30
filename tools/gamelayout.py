"""The game's structures as its compiler lays them out: a probe of offsetof
and sizeof expressions compiled with the game's flags (sh2eb-elf-g++ -S),
each value read back from the array it emits.

    layout({'name': 'C++ expression', ...}) -> {'name': value, ...}

The expressions see the game's headers (openlara/common.h, saturn/); a
capture's dumps are laid out by the program built from them.
"""
import os
import re
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin')
CXX = os.path.join(BIN, 'sh2eb-elf-g++')
FLAGS = ['-m2', '-mb', '-Os', '-D__SATURN__', '-Isaturn', '-Iopenlara', '-Ibuild/gen', '-fno-rtti', '-fno-exceptions',
         '-Wno-invalid-offsetof', '-S', '-o', '-']


def layout(exprs):
    names = list(exprs)
    src = ['#include "common.h"', '#include <stddef.h>', 'extern "C" const int kLayout[] = {']
    src += [f'    (int)({exprs[n]}),' for n in names]
    src += ['};']
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, 'probe.cpp')
        with open(probe, 'w') as f:
            f.write('\n'.join(src) + '\n')
        env = dict(os.environ, PATH=BIN + os.pathsep + os.environ['PATH'])    # cc1 loads its DLLs from it
        asm = subprocess.run([CXX, *FLAGS, probe], cwd=ROOT, capture_output=True, text=True, env=env)
    if asm.returncode:
        raise RuntimeError(asm.stderr)
    body = asm.stdout.split('_kLayout:', 1)[1]
    values = [int(v, 0) for v in re.findall(r'^\s*\.long\s+(-?\w+)', body, re.M)[:len(names)]]
    if len(values) != len(names):
        raise RuntimeError(f'{len(values)} values for {len(names)} expressions')
    return dict(zip(names, values))


if __name__ == '__main__':
    print(layout({'ItemObj': 'sizeof(ItemObj)', 'pos': 'offsetof(ItemObj, pos)', 'Model': 'sizeof(Model)'}))
