"""Makes depth-order test cases at random places: depthcorpus.py BASE_STATE COUNT [--seed N]

From BASE_STATE (a save state in LEVEL1 made with build/depth_cases/0MAIN.elf's
binary), puts Lara on a random flat, open floor sector of a random dry room,
facing a random way, lets the camera settle, saves the state as
build/depth_cases/auto_SEED_K.mcs and captures it (tools/depthcapture.py) with
the room depth option at Farthest. Score them with
depthcheck.py auto_SEED_* (a shell glob of the names).

Same caveat as depthcapture.py: no MCP tool call may be in flight.
"""
import argparse
import os
import random
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import depthcapture as cap
import depthcheck
import trsat

SETTLE_FRAMES = 90
ITEM_ROOM, ITEM_X, ITEM_Y, ITEM_Z, ITEM_YAW = 4, 8, 12, 16, 0x16


def symbols(elf):
    out = {}
    for line in subprocess.run([cap.NM, '-C', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            out.setdefault(parts[2], int(parts[0], 16))
    return out


def spots(lvl, rng):
    """Endless random (room, x, y, z, yaw) on flat, open, dry floor."""
    flipped = {r.alternate for r in lvl.rooms if r.alternate >= 0}
    rooms = [r for r in lvl.rooms if r.index not in flipped]   # shown only after a flip
    while True:
        r = rng.choice(rooms)
        if r.flags & 1:
            continue    # water
        sx, sz = rng.randrange(r.x_sectors), rng.randrange(r.z_sectors)
        floor_index, _, below, floor, _, ceiling = r.sectors[sx * r.z_sectors + sz]
        if floor == -127 or below != 255 or floor_index and depthcheck.door(lvl, floor_index) is not None:
            continue
        if floor_index and lvl.floors[floor_index] & 0x1F == 2:
            continue    # sloped
        if (floor - ceiling) * 256 < 1024:
            continue    # too low for Lara
        x = r.x + sx * 1024 + rng.randrange(256, 768)
        z = r.z + sz * 1024 + rng.randrange(256, 768)
        yield r.index, x, floor * 256, z, rng.randrange(0x10000)


def poke(addr, fmt, value):
    data = struct.pack('>' + fmt, value)
    cap.send(f'poke {addr:08X} ' + ' '.join(f'{b:02X}' for b in data), 'ok poke')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('base', help='save state in LEVEL1')
    ap.add_argument('count', type=int)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--spots', help='a file of spots instead of random ones: NAME: room R X Y Z yaw HEX per line '
                                    '(as this script writes them)')
    ap.add_argument('--layers', choices=('camera', 'one'), default='camera', help='depth layers option')
    ap.add_argument('--prefix', default='auto', help="the cases' names")
    args = ap.parse_args()

    elf = os.path.join(cap.CASES, '0MAIN.elf')
    sym = symbols(elf)
    lvl = trsat.load(os.path.join(cap.ROOT, 'external_resources/disc/files/DATA'), 'LEVEL1')
    rng = random.Random(args.seed)
    base = os.path.abspath(args.base).replace('\\', '/')
    ram_path = os.path.join(cap.IPC, 'depthcorpus_ram.bin').replace('\\', '/')

    cap.send('pause', 'ok pause')
    cap.send('breakpoint_clear', 'breakpoint_clear')
    cap.send(f'load_state {base}', 'ok load_state')
    cap.send(f'dump_region wram_high {ram_path}', 'dump_region')
    ram = open(ram_path, 'rb').read()
    lara = struct.unpack_from('>I', ram, sym['players'] - 0x06000000)[0]

    if args.spots:
        listed = [l.split() for l in open(args.spots) if l.strip()]
        chosen = [(int(p[2]), int(p[3]), int(p[4]), int(p[5]), int(p[7], 16)) for p in listed]
        names = [p[0].rstrip(':') for p in listed]
    else:
        chosen = list(zip(range(args.count), spots(lvl, rng)))
        chosen = [c for _, c in chosen]
        names = [f'{args.seed}_{k}' for k in range(len(chosen))]

    made = []
    for name, (room, x, y, z, yaw) in zip(names, chosen):
        name = f'{args.prefix}_{name}'
        state = os.path.join(cap.CASES, name + '.mcs').replace('\\', '/')
        cap.send(f'load_state {base}', 'ok load_state')
        poke(lara + ITEM_ROOM, 'I', sym['rooms'] + room * depthcheck.ROOM_SIZE)
        poke(lara + ITEM_X, 'i', x)
        poke(lara + ITEM_Y, 'i', y)
        poke(lara + ITEM_Z, 'i', z)
        poke(lara + ITEM_YAW, 'H', yaw)
        cap.send(f'frame_advance {SETTLE_FRAMES}', 'done frame_advance', 120)
        cap.send(f'save_state {state}', 'ok save_state')
        sys.argv = ['depthcapture.py', state, name, '--layers', args.layers]
        cap.main()
        made.append(f'{name}: room {room} {x} {y} {z} yaw {yaw:04x}')
    with open(os.path.join(cap.CASES, f'{args.prefix}_{args.seed}.txt'), 'w') as f:
        f.write('\n'.join(made) + '\n')
    print('\n'.join(made))


if __name__ == '__main__':
    main()
