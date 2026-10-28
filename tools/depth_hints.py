"""saturn/depth_hints.txt to a C header: depth_hints.py IN OUT"""
import re
import sys

LINE = re.compile(r'(LVL_\w+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(item\s+(\d+)|static\s+(\d+):(\d+))\s+([+-]?\d+)$')


def main():
    src, out = sys.argv[1], sys.argv[2]
    hints = []
    for n, line in enumerate(open(src), 1):
        line = line.split('#')[0].strip()
        if not line:
            continue
        m = LINE.match(line)
        if not m:
            sys.exit(f'{src}:{n}: not "level room bx bz item N|static R:I buckets": {line}')
        level, room, bx, bz = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        if m.group(6) is not None:
            kind, owner, index = 'DEPTH_HINT_ITEM', 0, int(m.group(6))
        else:
            kind, owner, index = 'DEPTH_HINT_STATIC', int(m.group(7)), int(m.group(8))
        hints.append(f'    {{ {level}, {room}, {bx}, {bz}, {kind}, {owner}, {index}, {int(m.group(9))} }},')

    with open(out, 'w', newline='\n') as f:
        f.write(f'// Made from {src} by tools/depth_hints.py\n')
        f.write('static const DepthHint gDepthHints[] = {\n')
        f.write('\n'.join(hints) + ('\n' if hints else ''))
        f.write('};\n')


if __name__ == '__main__':
    main()
