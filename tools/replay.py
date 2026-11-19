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
"""
import sys

KEYS = {'UP': 'IK_UP', 'DOWN': 'IK_DOWN', 'LEFT': 'IK_LEFT', 'RIGHT': 'IK_RIGHT', 'A': 'IK_A', 'B': 'IK_B',
        'C': 'IK_C', 'X': 'IK_X', 'Y': 'IK_Y', 'Z': 'IK_Z', 'L': 'IK_L', 'R': 'IK_R', 'START': 'IK_SELECT'}


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


def main():
    level, new_game, runs = read(sys.argv[1])
    pairs = []
    for ticks, names in runs:
        while ticks > 0:
            n = min(ticks, 65535)
            pairs.append(f'{n}, {"|".join(KEYS[k] for k in names) or "0"}')
            ticks -= n
    with open(sys.argv[2], 'w', newline='\n') as f:
        f.write(f'// tools/replay.py from {sys.argv[1]}\n')
        f.write(f'#define REPLAY_LEVEL "{level}"\n')
        f.write(f'#define REPLAY_NEW_GAME {int(new_game)}\n')
        f.write('static const uint16 kReplay[] = {\n')
        for p in pairs:
            f.write(f'    {p},\n')
        f.write('    0, 0\n};\n')


if __name__ == '__main__':
    main()
