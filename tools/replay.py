"""A tick replay (the pad a tick at a time) as the header a replay build
compiles in (make REPLAY=FILE, saturn/main.cpp TICK_REPLAY).

    python tools/replay.py FILE OUT.h

FILE, as the host reads it too (host/host.cpp REPLAY):

    # comment
    start LEVEL1 newgame        the level, and as a new game starts it or
                                with every gun (kit), as make START_LEVEL
    30 -                        30 ticks of no key
    12 UP+A                     12 ticks of these held

Keys: UP DOWN LEFT RIGHT A B C X Y Z L R START, as the pad's buttons.

A replay of more than WINDOW_OVER pairs (a whole chained bot run) doesn't fit
in the build: it gets a window of WINDOW pairs instead, its first ones, which
tools/replaycheck.py keeps written ahead in Mednafen (no Saturn plays it).
"""
import sys

KEYS = {'UP': 'IK_UP', 'DOWN': 'IK_DOWN', 'LEFT': 'IK_LEFT', 'RIGHT': 'IK_RIGHT', 'A': 'IK_A', 'B': 'IK_B',
        'C': 'IK_C', 'X': 'IK_X', 'Y': 'IK_Y', 'Z': 'IK_Z', 'L': 'IK_L', 'R': 'IK_R', 'START': 'IK_SELECT'}
# their bits (openlara/common.h InputKey), for the driver writing a window's pairs
BITS = {'UP': 1 << 0, 'RIGHT': 1 << 1, 'DOWN': 1 << 2, 'LEFT': 1 << 3, 'A': 1 << 4, 'B': 1 << 5, 'C': 1 << 6,
        'X': 1 << 7, 'Y': 1 << 8, 'Z': 1 << 9, 'L': 1 << 10, 'R': 1 << 11, 'START': 1 << 15}
WINDOW_OVER = 8192
WINDOW = 512


def read(path):
    """(level, new game, [(ticks, [key names])])"""
    level, new_game, runs = None, False, []
    for n, line in enumerate(open(path), 1):
        words = line.split('#', 1)[0].split()
        if not words:
            continue
        if words[0] == 'start':
            if len(words) != 3 or words[2] not in ('newgame', 'kit'):
                sys.exit(f'{path}:{n}: start LEVEL newgame|kit')
            level, new_game = words[1], words[2] == 'newgame'
            continue
        if len(words) != 2 or not words[0].isdigit():
            sys.exit(f'{path}:{n}: TICKS KEYS')
        names = [] if words[1] == '-' else words[1].split('+')
        for k in names:
            if k not in KEYS:
                sys.exit(f'{path}:{n}: no key {k}')
        runs.append((int(words[0]), names))
    if level is None:
        sys.exit(f'{path}: no start line')
    return level, new_game, runs


def pairs_of(runs):
    """The build's pairs: (ticks, key names), at most 65535 ticks each."""
    pairs = []
    for ticks, names in runs:
        while ticks > 0:
            n = min(ticks, 65535)
            pairs.append((n, names))
            ticks -= n
    return pairs


def main():
    level, new_game, runs = read(sys.argv[1])
    pairs = pairs_of(runs)
    window = len(pairs) > WINDOW_OVER
    with open(sys.argv[2], 'w', newline='\n') as f:
        f.write(f'// tools/replay.py from {sys.argv[1]}\n')
        f.write(f'#define REPLAY_LEVEL "{level}"\n')
        f.write(f'#define REPLAY_NEW_GAME {int(new_game)}\n')
        if window:
            f.write(f'#define REPLAY_WINDOW {WINDOW}\n')
            f.write('uint16 kReplay[REPLAY_WINDOW * 2] = {\n')
        else:
            f.write('static const uint16 kReplay[] = {\n')
        for n, names in pairs[:WINDOW] if window else pairs:
            f.write(f'    {n}, {"|".join(KEYS[k] for k in names) or "0"},\n')
        f.write('};\n' if window else '    0, 0\n};\n')
    if window:
        print(f'{sys.argv[1]}: {len(pairs)} pairs, a window of {WINDOW} in the build, '
              f'kept written by tools/replaycheck.py in Mednafen')


if __name__ == '__main__':
    main()
