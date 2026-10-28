"""saturn/depth_hints.txt to a C header: depth_hints.py IN OUT"""
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


def main():
    src, out = sys.argv[1], sys.argv[2]
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
        if item is not None:
            thing = f'DEPTH_HINT_ITEM, 0, {int(item)}'
        else:
            thing = f'DEPTH_HINT_STATIC, {int(owner)}, {int(static)}'
        where = f'{level}, {room}, {bx}, {bz}'
        if buckets is not None:
            if int(buckets) == 0:
                sys.exit(f'{src}:{n}: 0 buckets: {line}')
            hints.append(f'    {{ {where}, {thing}, {int(buckets)}, 0, 0, 0, 0 }},')
            continue
        own = (int(node), polygon(kind + index)) if node is not None else (WHOLE, 0)
        for face in faces.split():
            hints.append(f'    {{ {where}, {thing}, 0, {own[0]}, {int(behind_room)}, '
                         f'{own[1]:#x}, {polygon(face):#x} }},')

    with open(out, 'w', newline='\n') as f:
        f.write(f'// Made from {src} by tools/depth_hints.py\n')
        f.write('static const DepthHint gDepthHints[] = {\n')
        # with none, one for no level: an empty table lets GCC prove the
        # active hints null and turn their uses into traps, which need abort
        f.write('\n'.join(hints) + '\n' if hints else '    { 0xFF },\n')
        f.write('};\n')


if __name__ == '__main__':
    main()
