"""The Saturn build's items in Mednafen, as host/host.cpp --items prints them,
for comparing the host's logic with the Saturn's at a level's start.

python -I host/satitems.py DISC_DIR ELF [FRAME] > items.txt     [HOLD=KEY:START:LENGTH]

Boots DISC_DIR/tomb.cue (a START_LEVEL disc) in a Mednafen of its own (name
logicchk; settings seeded from build/logicchk_home), waits for the level's
first frames, and prints every item. The Mednafen is the main checkout's
tools/mednafen build, or MEDNAFEN_EXE."""
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.environ.get('TRS_MAIN', r'C:\Users\albat\Projects\TombRaiderSaturn')
sys.path.insert(0, os.path.join(MAIN, 'tools'))
from capture import Mednafen  # noqa: E402

EXE = os.environ.get('MEDNAFEN_EXE', os.path.join(MAIN, 'tools/mednafen/src/mednafen.exe'))
BIN = os.path.join(MAIN, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-')
ITEM_SIZE = 76      # host --items prints sizeof(ItemObj) and the pointers' offsets: room 4, extra 64,
ROOM_SIZE = 64      # next 68, active 72


def symbols(elf):
    s = {}
    for line in subprocess.run([BIN + 'nm', elf], capture_output=True, text=True).stdout.splitlines():
        p = line.split(None, 2)
        if len(p) == 3:
            s[p[2]] = int(p[0], 16)
    return s


def start(cue, name):
    """capture.Mednafen's protocol on our own folders and the main checkout's executable."""
    m = Mednafen.__new__(Mednafen)
    m.ipc = os.path.join(HERE, f'build/{name}_ipc')
    home = os.path.join(HERE, f'build/{name}_home')
    os.makedirs(m.ipc, exist_ok=True)
    m.action = os.path.join(m.ipc, 'mednafen_action.txt')
    m.ack_file = os.path.join(m.ipc, 'mednafen_ack.txt')
    for f in (m.action, m.ack_file):
        if os.path.exists(f):
            os.remove(f)
    m.seq = 0
    stderr = open(os.path.join(HERE, f'build/{name}_stderr.txt'), 'w')
    m.proc = subprocess.Popen([EXE, '--sound', '0', '-cd.image_memcache', '1', '--automation', m.ipc, cue],
                              stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=stderr,
                              env=dict(os.environ, MEDNAFEN_HOME=home))
    deadline = time.time() + 60
    while 'ready' not in m.ack():
        if m.proc.poll() is not None or time.time() > deadline:
            sys.exit('Mednafen did not start: see build/%s_stderr.txt' % name)
        time.sleep(0.2)
    return m


def main():
    disc, elf = sys.argv[1:3]
    frame = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    s = symbols(elf)
    items, rooms, players = s['_items'], s['_rooms'], s['_players']
    m = start(os.path.join(disc, 'tomb.cue'), 'logicchk')
    try:
        # the level is up once Lara is placed
        f = 600
        lara_at = 0
        while f < 6000:
            m.send(f'run_to_frame {f}', 'done run_to_frame', 600)
            if struct.unpack('>I', m.read(players, 4))[0]:
                lara_at = f
                break
            f += 10
        # HOLD=KEY:START:LENGTH (vblanks after Lara is placed): a key held a while
        if os.environ.get('HOLD'):
            key, at, length = os.environ['HOLD'].split(':')
            m.send(f'run_to_frame {lara_at + int(at)}', 'done run_to_frame', 600)
            m.send(f'input {key}', 'ok input', 10)
            m.send(f'frame_advance {length}', 'done frame_advance', 600)
            m.send(f'input_release {key}', 'ok input_release', 10)
        if frame:
            m.send(f'run_to_frame {lara_at + frame}', 'done run_to_frame', 600)
        raw = m.read(items, ITEM_SIZE * 256)
        print(f'# Lara placed by vblank {lara_at}, read at {lara_at + frame}')
        for i in range(256):
            it = bytearray(raw[i * ITEM_SIZE:(i + 1) * ITEM_SIZE])

            def ptr(off):
                return struct.unpack('>I', it[off:off + 4])[0]

            def index(p):
                return -1 if p == 0 else (p - items) // ITEM_SIZE

            room = -1 if ptr(4) == 0 else (ptr(4) - rooms) // ROOM_SIZE
            extra = 1 if ptr(64) else 0
            nxt, act = index(ptr(68)), index(ptr(72))
            for off in (0, 4, 64, 68, 72):
                it[off:off + 4] = bytes(4)
            print(f'item {i:3}: room {room:3} extra {extra} next {nxt:3} active {act:3} raw {it.hex()}')
    finally:
        m.quit()
        time.sleep(1)


if __name__ == '__main__':
    main()
