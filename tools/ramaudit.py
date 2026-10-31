"""The game's RAM at each level, measured: a build of TRS as it ships (no
HW_LOG; the Makefile rebuilds for a change of flags) starting at the Gym,
booted in a Mednafen of its own
(tools/capture.py's), loads every level in turn (gNextLevel poked, its
video skipped) and, once one is playing (the game counting frames again),
reads what os.cpp took for it:

  high work RAM: the level's arena, from the program's bss to the master's
  stack (MASTER_STACK of it kept): its mesh store, its rooms' vertices, the
  renderer's tables, then the rooms' faces as they fit (osPlaceRoomFaces:
  a room's faces are where its RoomData points); the rest stay in low work
  RAM, where the slave reads them slower;
  low work RAM: the level's file, its room texture set, the colour tables'
  other versions and the inventory's item textures (osLoadLevel).

Before that, the program's own high work RAM from its ELF: code and data,
bss (its biggest buffers), the arena and the stack.

    ramaudit.py [--build build/ramaudit] [--top 12] [LEVEL ...]

The build is made first (make BUILD=... START_LEVEL=LVL_TR1_GYM disc).
LEVEL names as in the Makefile (GYM, LEVEL1, ..., CUT1, TITLE); all of them
by default.
"""
import argparse
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capture  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = os.path.join(ROOT, 'toolchain', 'gcc-14.2.0', 'sh2eb-elf', 'bin', 'sh2eb-elf-size')
KB = 1024
HWRAM_TOP = 0x06100000
LWRAM, LWRAM_BYTES = 0x00200000, 1024 * KB
MASTER_STACK = 8 * KB       # os.cpp
ROOM_SIZE, ROOM_QUADS = 64, 12  # openlara Room, its data.quads

# openlara LevelID: each level's id, by the name its files have
LEVEL_IDS = {name: i for i, name in enumerate(
    ['TITLE', 'GYM', 'LEVEL1', 'LEVEL2', 'LEVEL3A', 'LEVEL3B', 'CUT1', 'LEVEL4', 'LEVEL5', 'LEVEL6', 'LEVEL7A',
     'LEVEL7B', 'CUT2', 'LEVEL8A', 'LEVEL8B', 'LEVEL8C', 'LEVEL10A', 'CUT3', 'LEVEL10B', 'CUT4', 'LEVEL10C'])}
LEVELS = list(LEVEL_IDS)[1:] + ['TITLE']


def program(elf, syms, top):
    """Prints the program's sections and its biggest bss buffers; returns
    the arena's start and size."""
    out = subprocess.run([SIZE, '-A', elf], capture_output=True, text=True, check=True).stdout
    sections = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0].startswith('.') and int(parts[2]) >= 0x06000000:
            sections[parts[0]] = (int(parts[1]), int(parts[2]))
    code = sum(size for name, (size, _) in sections.items() if name != '.bss')
    bss = sections['.bss'][0]
    arena_start = (capture.symbol(syms, '__bss_end') + 15) & ~15
    arena = HWRAM_TOP - MASTER_STACK - arena_start
    print('High work RAM (the program):')
    print(f'  code and data   {code / KB:6.1f} KB   ({", ".join(sorted(sections))})')
    print(f'  bss             {bss / KB:6.1f} KB')
    print(f'  level arena     {arena / KB:6.1f} KB   (to the master\'s stack)')
    print(f'  master\'s stack  {MASTER_STACK / KB:6.1f} KB')
    big = sorted(((size, name) for name, entries in syms.items() for addr, size in entries
                  if sections['.bss'][1] <= addr < sections['.bss'][1] + bss and size), reverse=True)[:top]
    names = subprocess.run([SIZE.replace('-size', '-c++filt')] + [name for _, name in big],
                           capture_output=True, text=True, check=True).stdout.split('\n')
    print('  bss\'s biggest:')
    for (size, _), name in zip(big, names):
        print(f'    {size / KB:6.1f} KB  {name.lstrip("_")}')
    return arena_start, arena


def rooms_placed(med, rooms, name):
    """The level's rooms (its packed file's count) and how many have their
    faces in high work RAM."""
    with open(os.path.join(ROOT, 'build', 'data', name + '.PKD'), 'rb') as f:
        count = struct.unpack('>6xH', f.read(8))[0]     # pack_level's header: 'SAT1', 0, rooms
    data = med.read(rooms, ROOM_SIZE * count)
    placed = sum(struct.unpack_from('>I', data, ROOM_SIZE * i + ROOM_QUADS)[0] >> 24 == 0x06 for i in range(count))
    return placed, count


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', default='build/ramaudit')
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('levels', nargs='*', default=LEVELS)
    args = ap.parse_args()
    for name in args.levels:
        if name not in LEVEL_IDS:
            sys.exit(f'{name}: not a level ({", ".join(LEVELS)})')

    made = subprocess.run(['make', f'BUILD={args.build}', 'START_LEVEL=LVL_TR1_GYM', 'disc', '-j8'], cwd=ROOT,
                          stdout=subprocess.DEVNULL)
    if made.returncode:
        sys.exit(f'make: exit {made.returncode}')
    elf = os.path.join(ROOT, args.build, '0MAIN.elf')
    syms = capture.symbols(elf)
    arena_start, arena = program(elf, syms, args.top)

    high_free = capture.symbol(syms, '__ZL9gHighFree')
    low_free = capture.symbol(syms, '__ZL8gLowFree')
    rooms = capture.symbol(syms, '_rooms')
    next_level = capture.symbol(syms, '_gNextLevel')
    level_id = capture.symbol(syms, '_gLevelID')
    skip = capture.symbol(syms, '__ZL10gSkipVideo')
    frames = capture.symbol(syms, '_gStats') + 24     # gStats.frames: the game's frames drawn

    print()
    print('Each level (KB):          arena used  left   rooms\' faces placed   low work RAM used  left')
    med = capture.Mednafen(os.path.join(ROOT, args.build, 'disc', 'tomb.cue'), name='ramaudit')
    try:
        med.send('deterministic', 'deterministic', 10)
        frame = 1800
        med.send(f'run_to_frame {frame}', 'done run_to_frame', 900)
        for name in args.levels:
            lid = LEVEL_IDS[name]
            if name != 'GYM' or struct.unpack('>I', med.read(level_id, 4))[0] != lid:
                med.send(f'poke {skip:08X} 01', 'ok poke', 10)
                med.send(f'poke {next_level:08X} 00 00 00 {lid:02X}', 'ok poke', 10)
                drawn = None
                for _ in range(40):
                    frame += 120
                    med.send(f'run_to_frame {frame}', 'done run_to_frame', 900)
                    now = struct.unpack('>I', med.read(frames, 4))[0]
                    if struct.unpack('>I', med.read(level_id, 4))[0] != lid:
                        continue
                    if drawn is not None and now > drawn:
                        break
                    drawn = now
                else:
                    print(f'  {name:8s} did not load', flush=True)
                    continue
            frame += 240
            med.send(f'run_to_frame {frame}', 'done run_to_frame', 900)
            high = struct.unpack('>I', med.read(high_free, 4))[0] - arena_start
            low = struct.unpack('>I', med.read(low_free, 4))[0] - LWRAM
            placed, count = rooms_placed(med, rooms, name)
            faces = f'{placed:3d} of {count:3d}'
            note = '' if placed == count else '  some in low work RAM'
            print(f'  {name:8s} (id {lid:2d})        {high / KB:6.1f} {(arena - high) / KB:6.1f}   {faces:>16s}   '
                  f'       {low / KB:6.1f} {(LWRAM_BYTES - low) / KB:6.1f}{note}', flush=True)
    finally:
        med.quit()


if __name__ == '__main__':
    main()
