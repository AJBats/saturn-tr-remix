"""saturn/depth_hints.txt to a C header: depth_hints.py IN OUT

parse() is also the packer's (tools/pack_level.py), which keeps the faces a
hint names apart when it simplifies a mesh (tools/diet.py)."""
import re
import sys

WHERE = r'(?:(\d+)\s+(\d+)\s+(\d+)|(\*))'
THING = r'(?:item\s+(\d+)|static\s+(\d+):(\d+))'
FACES = r'((?:[qt]\d+\s*)+)'
LINE = re.compile(rf'(LVL_\w+)\s+{WHERE}\s+{THING}\s+'
                  rf'(?:([+-]?\d+)|(?:m(\d+)\s+([qt])(\d+)\s+)?behind\s+room\s+(\d+)\s+{FACES})$')
TRIANGLE = 0x8000
ANYWHERE = 0xFF
WHOLE = 0xFF


def polygon(face):
    return int(face[1:]) | (TRIANGLE if face[0] == 't' else 0)


def level_id(name):
    """A level file's name as the game's LevelID names it: LEVEL3A is
    LVL_TR1_3A, CUT1 LVL_TR1_CUT_1, GYM LVL_TR1_GYM."""
    if name.startswith('LEVEL'):
        return 'LVL_TR1_' + name[5:]
    if name.startswith('CUT'):
        return 'LVL_TR1_CUT_' + name[3:]
    return 'LVL_TR1_' + name


def parse(src):
    """src's hints, a dict each: level, room (ANYWHERE: wherever Lara
    stands), bx, bz, kind ('item' or 'static'), owner (a static's room),
    index, buckets (0 for faces drawn before a room's), node (WHOLE: every
    face of the thing), polygon, behind_room and behind (a polygon each)."""
    for n, line in enumerate(open(src), 1):
        line = line.split('#')[0].strip()
        if not line:
            continue
        m = LINE.match(line)
        if not m:
            sys.exit(f'{src}:{n}: not "level WHERE THING buckets" or "level WHERE THING [mN qN|tN] '
                     f'behind room R qN|tN...": {line}')
        (level, room, bx, bz, anywhere, item, owner, static, buckets,
         node, kind, index, behind_room, faces) = m.groups()
        hint = dict(level=level, room=ANYWHERE if anywhere else int(room), bx=0 if anywhere else int(bx),
                    bz=0 if anywhere else int(bz), kind='item' if item is not None else 'static',
                    owner=0 if item is not None else int(owner), index=int(item if item is not None else static),
                    buckets=0, node=WHOLE, polygon=0, behind_room=0, behind=[])
        if buckets is not None:
            if int(buckets) == 0:
                sys.exit(f'{src}:{n}: 0 buckets: {line}')
            hint['buckets'] = int(buckets)
        else:
            if node is not None:
                hint['node'], hint['polygon'] = int(node), polygon(kind + index)
            hint['behind_room'] = int(behind_room)
            hint['behind'] = [polygon(face) for face in faces.split()]
        yield hint


def main():
    src, out = sys.argv[1], sys.argv[2]
    hints = []
    for h in parse(src):
        where = f"{h['level']}, {h['room']}, {h['bx']}, {h['bz']}"
        if h['kind'] == 'item':
            thing = f"DEPTH_HINT_ITEM, 0, {h['index']}"
        else:
            thing = f"DEPTH_HINT_STATIC, {h['owner']}, {h['index']}"
        if h['buckets']:
            hints.append(f"    {{ {where}, {thing}, {h['buckets']}, 0, 0, 0, 0 }},")
            continue
        for face in h['behind']:
            hints.append(f"    {{ {where}, {thing}, 0, {h['node']}, {h['behind_room']}, "
                         f"{h['polygon']:#x}, {face:#x} }},")

    with open(out, 'w', newline='\n') as f:
        f.write(f'// Made from {src} by tools/depth_hints.py\n')
        f.write('static const DepthHint gDepthHints[] = {\n')
        # with none, one for no level: an empty table lets GCC prove the
        # active hints null and turn their uses into traps, which need abort
        f.write('\n'.join(hints) + '\n' if hints else '    { 0xFF },\n')
        f.write('};\n')


if __name__ == '__main__':
    main()
