"""saturn/depth_hints.txt to a C header: depth_hints.py IN OUT

parse(path) gives the hints as tuples in render.cpp DepthHint's order, the
level by name: the lab packs its level's (tools/labscene.py)."""
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


def parse(src):
    """(level, room, bx, bz, kind, owner, index, buckets, node, behind room,
    polygon, behind) each, kind 0 an item, 1 a static."""
    hints = []
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
        room, bx, bz = (ANYWHERE, 0, 0) if anywhere else (int(room), int(bx), int(bz))
        thing = (0, 0, int(item)) if item is not None else (1, int(owner), int(static))
        if buckets is not None:
            if int(buckets) == 0:
                sys.exit(f'{src}:{n}: 0 buckets: {line}')
            hints.append((level, room, bx, bz, *thing, int(buckets), 0, 0, 0, 0))
            continue
        own = (int(node), polygon(kind + index)) if node is not None else (WHOLE, 0)
        for face in faces.split():
            hints.append((level, room, bx, bz, *thing, 0, own[0], int(behind_room), own[1], polygon(face)))
    return hints


def main():
    src, out = sys.argv[1], sys.argv[2]
    kinds = ('DEPTH_HINT_ITEM', 'DEPTH_HINT_STATIC')
    hints = [f'    {{ {h[0]}, {h[1]}, {h[2]}, {h[3]}, {kinds[h[4]]}, {h[5]}, {h[6]}, {h[7]}, {h[8]}, {h[9]}, '
             f'{h[10]:#x}, {h[11]:#x} }},' for h in parse(src)]

    with open(out, 'w', newline='\n') as f:
        f.write(f'// Made from {src} by tools/depth_hints.py\n')
        f.write('static const DepthHint gDepthHints[] = {\n')
        # with none, one for no level: an empty table lets GCC prove the
        # active hints null and turn their uses into traps, which need abort
        f.write('\n'.join(hints) + '\n' if hints else '    { 0xFF },\n')
        f.write('};\n')


if __name__ == '__main__':
    main()
