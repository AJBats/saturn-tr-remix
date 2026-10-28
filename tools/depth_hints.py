"""saturn/depth_hints.txt to a C header: depth_hints.py IN OUT"""
import re
import sys

LINE = re.compile(r'(LVL_\w+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(item\s+(\d+)|static\s+(\d+):(\d+))\s+'
                  r'(?:([+-]?\d+)|m(\d+)\s+([qt])(\d+)\s+behind\s+room\s+(\d+)\s+([qt])(\d+))$')
TRIANGLE = 0x8000


def polygon(kind, index):
    return int(index) | (TRIANGLE if kind == 't' else 0)


def main():
    src, out = sys.argv[1], sys.argv[2]
    hints = []
    for n, line in enumerate(open(src), 1):
        line = line.split('#')[0].strip()
        if not line:
            continue
        m = LINE.match(line)
        if not m:
            sys.exit(f'{src}:{n}: not "level room bx bz THING buckets" or '
                     f'"level room bx bz THING mN qN|tN behind room R qN|tN": {line}')
        level, room, bx, bz = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        if m.group(6) is not None:
            kind, owner, index = 'DEPTH_HINT_ITEM', 0, int(m.group(6))
        else:
            kind, owner, index = 'DEPTH_HINT_STATIC', int(m.group(7)), int(m.group(8))
        if m.group(9) is not None:
            buckets = int(m.group(9))
            if buckets == 0:
                sys.exit(f'{src}:{n}: 0 buckets: {line}')
            face = '0, 0, 0, 0'
        else:
            buckets = 0
            face = (f'{int(m.group(10))}, {int(m.group(13))}, '
                    f'{polygon(m.group(11), m.group(12)):#x}, {polygon(m.group(14), m.group(15)):#x}')
        hints.append(f'    {{ {level}, {room}, {bx}, {bz}, {kind}, {owner}, {index}, {buckets}, {face} }},')

    with open(out, 'w', newline='\n') as f:
        f.write(f'// Made from {src} by tools/depth_hints.py\n')
        f.write('static const DepthHint gDepthHints[] = {\n')
        f.write('\n'.join(hints) + ('\n' if hints else ''))
        f.write('};\n')


if __name__ == '__main__':
    main()
