"""The Saturn on the desk, driven from here. There's one, and any number of sessions wanting it; it's
for final checks and numbers nothing else can give, Mednafen is for everything else. Whatever changes
it (power on|off|cycle, card pc|saturn, deploy, record) takes --as NAME, your session's name, and
needs the lock held under that name, unless test does it all for you:

    hw.py test FOLDER --as NAME --why "what for" [--settle S] [--shots N] [--every S] [--out PREFIX] [--wait MIN]
        a whole run with nobody at the machine: waits up to --wait minutes for the lock (default 0: says
        who has it and stops), takes it, deploys, lets the game run --settle seconds after its first
        picture (the disc must get itself there: no pad), saves --shots pictures --every seconds apart
        (build/hw_test_1.png ...), lets the lock go
    hw.py lock
        who holds the Saturn, what for and until when
    hw.py lock NAME --why "what for" [--minutes N] [--wait MIN]
        takes it for N minutes (default 15; each command under it starts the time again, so a session
        that dies only blocks the others that long); held by another, says so and exits with 1
    hw.py unlock NAME
        lets it go; only the holder can
    hw.py power
        whether the Saturn's smart plug is on, and what it draws
    hw.py power off
        cuts the power; returns once the plug reads nothing drawn and the
        Saturn's power supply has drained
    hw.py power on
        restores it, and checks the Saturn starts drawing (it only does with
        its own power button pressed in)
    hw.py power cycle
        off, then on
    hw.py shot [--out PNG]
        saves what the capture card shows right now (build/hw_shot.png); needs no lock
    hw.py record SECONDS [--out MKV]
        records that long as OBS is set up to, moved from OBS's folder to
        build/hw_record.mkv; refuses if OBS is already recording
    hw.py card
        which side the SD mux holds the card on: pc or saturn
    hw.py card pc|saturn
        moves the card there and checks Windows saw it arrive or leave; refuses
        while the Saturn draws power, since it may be using the card. Windows
        writes out all it holds for the card before the card leaves the PC
    hw.py deploy [FOLDER] [--out PNG]
        puts a built disc (build/disc) on the card as the game it starts by
        itself, and starts it: cuts the power, moves the card to the PC, copies
        the tracks that differ in size or time and the cue as autoboot.cue,
        compares every file with what's on the card, redoing any that differ,
        moves the card back, powers on and saves the screen 30 s later. If the
        files won't come out right, the Saturn stays off and the card on the PC

Exits with 1 if the plug doesn't answer or the reading isn't what it should
be. The plug is a Shelly Plug US Gen4 on the home network, found by its name.
OBS holds the capture card, so pictures come through its WebSocket server
(Tools > WebSocket Server Settings, on), with the password from OBS's config.
The SD mux is an SDWire, driven through Windows' own FTDI driver; the chip's
CBUS0 pin was set to general I/O once, which holds until someone changes it.
Its card reader reports a write fault every 100 MB or so, which Windows
retries, so what's written through it has to be read back.
"""

import argparse
import base64
import contextlib
import ctypes
import glob
import hashlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

PLUG = 'shellyplugusg4-e8f60a7d5608.local'

OFF_WATTS = 1.0     # less than this is nothing drawn
ON_WATTS = 2.0      # more than this is the Saturn running
READING_S = 5       # how long the plug's reading gets to reach either
DRAIN_S = 5         # the power supply's capacitors, after the reading falls

OBS_CONFIG = os.path.expandvars(r'%APPDATA%\obs-studio\plugin_config\obs-websocket\config.json')
OBS_CAPTURE_KIND = 'dshow_input'    # OBS's Video Capture Device
RELEASE_S = 30      # how long a stopped recording's file gets to be let go

MUX_SERIAL = b'DP04I386'        # the SD mux's FTDI chip
CARD_READER = '000000264001'    # the serial Windows shows for the mux's card reader
FT_OPEN_BY_SERIAL = 1
FT_CBUS_BITBANG = 0x20
CARD_MOVE_S = 15    # how long Windows gets to see the card arrive or leave

GENERIC_READ_WRITE = 0xC0000000
FILE_SHARE_READ_WRITE = 3
OPEN_EXISTING = 3
FSCTL_LOCK_VOLUME = 0x00090018
FSCTL_DISMOUNT_VOLUME = 0x00090020

LOCK_FILE = os.path.expandvars(r'%LOCALAPPDATA%\tomb_hw_saturn.json')
LOCK_GATE = LOCK_FILE + '.gate'     # held for the moment of reading and writing LOCK_FILE
LOCK_MINUTES = 15
SATURN_WRITES_S = 20    # after power-on, how long the Satiator's menu may be writing to the card
BOOT_S = 30             # from power-on to the game's first picture
COPY_CHUNK = 1 << 20
COPY_TRIES = 3


def fail(message):
    print(f'hw.py: {message}', file=sys.stderr)
    sys.exit(1)


@contextlib.contextmanager
def lock_state():
    """The lock's record, to read and change; it's written back as the block ends."""
    import msvcrt

    with open(LOCK_GATE, 'a+') as gate:
        deadline = time.monotonic() + 10
        while True:
            try:
                gate.seek(0)
                msvcrt.locking(gate.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                if time.monotonic() > deadline:
                    fail('another hw.py has held the lock file for 10 s')
                time.sleep(0.05)
        try:
            try:
                with open(LOCK_FILE, encoding='utf-8') as f:
                    state = json.load(f)
            except (OSError, ValueError):
                state = {}
            yield state
            with open(LOCK_FILE + '.tmp', 'w', encoding='utf-8') as f:
                json.dump(state, f)
            os.replace(LOCK_FILE + '.tmp', LOCK_FILE)
        finally:
            gate.seek(0)
            msvcrt.locking(gate.fileno(), msvcrt.LK_UNLCK, 1)


def holder_of(state):
    """Who holds the lock, or None: a hold past its time is no hold."""
    return state.get('who') if state.get('until', 0) > time.time() else None


def held_text(state):
    left = (state['until'] - time.time()) / 60
    return f"{state['who']} holds the Saturn: {state.get('why') or 'no reason given'}; {left:.0f} min left unless it's refreshed"


def lock_take(name, why, minutes, wait):
    """Takes the lock, waiting up to wait minutes for it; False if it can't."""
    deadline = time.monotonic() + wait * 60
    told = None
    while True:
        with lock_state() as state:
            who = holder_of(state)
            if who is None or who == name:
                state.update(who=name, why=why or state.get('why', ''), minutes=minutes, since=time.time(),
                             until=time.time() + minutes * 60)
                return True
            text = held_text(state)
        if time.monotonic() >= deadline:
            print(text, file=sys.stderr)
            return False
        if who != told:
            print(f'{text}; waiting', flush=True)
            told = who
        time.sleep(5)


def lock_release(name):
    with lock_state() as state:
        who = holder_of(state)
        if who is None:
            print('the Saturn is free')
        elif who != name:
            fail(f'{held_text(state)}: only the holder can let it go')
        else:
            state['until'] = 0
            print('let go')


def lock_needed(name):
    """Stops unless name holds the lock; starts its time again if it does."""
    with lock_state() as state:
        who = holder_of(state)
        if who is None:
            fail('the Saturn has to be held first: hw.py lock NAME --why "what for"')
        if who != name:
            fail(held_text(state))
        state['until'] = time.time() + state.get('minutes', LOCK_MINUTES) * 60


def plug(method, **params):
    url = f'http://{PLUG}/rpc/{method}?{urllib.parse.urlencode(params)}'
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return json.load(r)
    except (urllib.error.URLError, OSError, ValueError) as e:
        fail(f'the plug at {PLUG} did not answer: {e}')


def status():
    return plug('Switch.GetStatus', id=0)


def wait_for_watts(reached):
    """The plug's reading once reached(watts) holds, None if it doesn't in time."""
    deadline = time.monotonic() + READING_S
    while True:
        watts = status()['apower']
        if reached(watts):
            return watts
        if time.monotonic() > deadline:
            return None
        time.sleep(0.25)


def power_off():
    plug('Switch.Set', id=0, on='false')
    watts = wait_for_watts(lambda w: w < OFF_WATTS)
    if watts is None:
        fail(f"switched off, but it still draws {status()['apower']} W")
    time.sleep(DRAIN_S)
    print(f'off: {watts} W, drained')


def power_on():
    plug('Switch.Set', id=0, on='true')
    watts = wait_for_watts(lambda w: w > ON_WATTS)
    if watts is None:
        fail("switched on, but nothing draws power: is the Saturn's button pressed in?")
    print(f'on: {watts} W')


def power(action):
    if action == 'off':
        power_off()
    elif action == 'on':
        power_on()
    elif action == 'cycle':
        power_off()
        power_on()
    else:
        s = status()
        print(f"{'on' if s['output'] else 'off'}: {s['apower']} W")


def b64_sha256(text):
    return base64.b64encode(hashlib.sha256(text.encode()).digest()).decode()


class Obs:
    """A connection to OBS's WebSocket server (protocol 5, JSON)."""

    def __init__(self):
        with open(OBS_CONFIG, encoding='utf-8') as f:
            config = json.load(f)
        try:
            self.sock = socket.create_connection(('127.0.0.1', config['server_port']), timeout=10)
        except OSError as e:
            fail(f'OBS does not answer (is it running, with its WebSocket server on?): {e}')
        self.file = self.sock.makefile('rb')
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            'GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
            f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n'
            'Sec-WebSocket-Protocol: obswebsocket.json\r\n\r\n').encode())
        status = self.file.readline()
        if b' 101 ' not in status:
            fail(f'OBS refused the WebSocket: {status!r}')
        while self.file.readline().strip():
            pass

        identify = {'rpcVersion': 1, 'eventSubscriptions': 0}
        auth = self.receive()['d'].get('authentication')
        if auth:
            secret = b64_sha256(config['server_password'] + auth['salt'])
            identify['authentication'] = b64_sha256(secret + auth['challenge'])
        self.send(1, identify)
        self.receive()

    def frame(self, opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        length = bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack('>H', n)
        masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        self.sock.sendall(bytes([0x80 | opcode]) + length + mask + masked)

    def send(self, op, data):
        self.frame(1, json.dumps({'op': op, 'd': data}).encode())

    def receive(self):
        message = b''
        while True:
            head = self.file.read(2)
            if len(head) < 2:
                fail('OBS closed the connection')
            size = head[1] & 0x7F
            if size == 126:
                size = struct.unpack('>H', self.file.read(2))[0]
            elif size == 127:
                size = struct.unpack('>Q', self.file.read(8))[0]
            payload = self.file.read(size)
            opcode = head[0] & 0x0F
            if opcode == 8:
                fail(f'OBS closed the connection: {payload[:2].hex()} {payload[2:].decode(errors="replace")}')
            if opcode == 9:
                self.frame(10, payload)
            elif opcode <= 2:
                message += payload
                if head[0] & 0x80:
                    return json.loads(message)

    def request(self, kind, **data):
        self.send(6, {'requestType': kind, 'requestId': kind, 'requestData': data})
        reply = self.receive()['d']
        if not reply['requestStatus']['result']:
            fail(f"OBS refused {kind}: {reply['requestStatus']}")
        return reply.get('responseData', {})


def shot(out):
    obs = Obs()
    inputs = obs.request('GetInputList', inputKind=OBS_CAPTURE_KIND)['inputs']
    if len(inputs) != 1:
        fail(f'expected one capture source in OBS, found {[i["inputName"] for i in inputs]}')
    image = obs.request('GetSourceScreenshot', sourceName=inputs[0]['inputName'], imageFormat='png')['imageData']
    with open(out, 'wb') as f:
        f.write(base64.b64decode(image.split(',', 1)[1]))
    print(out)


def record(seconds, out):
    obs = Obs()
    if obs.request('GetRecordStatus')['outputActive']:
        fail('OBS is already recording')
    obs.request('StartRecord')
    try:
        time.sleep(seconds)
    finally:
        # a new connection: a long wait doesn't answer the server's pings
        path = Obs().request('StopRecord')['outputPath']

    # OBS holds the file for a moment after it reports stopping
    deadline = time.monotonic() + RELEASE_S
    while True:
        try:
            os.replace(path, out)
            break
        except PermissionError:
            if time.monotonic() > deadline:
                fail(f'OBS still holds {path}')
            time.sleep(0.25)
    print(out)


def mux_pin(high):
    """Drives the mux's control pin: high puts the card on the PC, low on the Saturn."""
    ftd2xx = ctypes.WinDLL('ftd2xx.dll')
    handle = ctypes.c_void_p()
    if ftd2xx.FT_OpenEx(MUX_SERIAL, FT_OPEN_BY_SERIAL, ctypes.byref(handle)):
        fail('the SD mux is not on USB')
    try:
        if ftd2xx.FT_SetBitMode(handle, 0xF0 | high, FT_CBUS_BITBANG):
            fail('the SD mux refused the switch')
    finally:
        ftd2xx.FT_Close(handle)


def powershell(script):
    return subprocess.run(['powershell', '-NoProfile', '-Command', script],
                          capture_output=True, text=True).stdout.strip()


def card_side():
    """Where Windows sees the card: 'pc', 'saturn' (its reader is empty, as with an empty
    socket too), or None with the mux off USB."""
    return powershell(f"$d = Get-Disk | Where-Object SerialNumber -eq '{CARD_READER}';"
                      "if ($d) { if ($d.Size) { 'pc' } else { 'saturn' } }") or None


def card_letter():
    """The card's drive letter, empty while Windows hasn't mounted it."""
    return powershell(f"$d = Get-Disk | Where-Object SerialNumber -eq '{CARD_READER}';"
                      "if ($d.Size) { (Get-Partition -DiskNumber $d.Number -ErrorAction SilentlyContinue"
                      " | Where-Object DriveLetter).DriveLetter }")


def card_root():
    """The card's root folder, once Windows has mounted it."""
    deadline = time.monotonic() + CARD_MOVE_S
    while True:
        letter = card_letter()
        if letter and os.path.exists(f'{letter}:\\'):
            return f'{letter}:\\'
        if time.monotonic() > deadline:
            fail('Windows never mounted the card')
        time.sleep(0.25)


@contextlib.contextmanager
def card_released(letter):
    """Windows writes out all it holds for the card and forgets it, so anything read after
    this comes from the card itself; it can't mount the card again until the block ends."""
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.CreateFileW.restype = ctypes.c_void_p
    kernel32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                     ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    kernel32.FlushFileBuffers.argtypes = [ctypes.c_void_p]
    kernel32.DeviceIoControl.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
                                         ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32),
                                         ctypes.c_void_p]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]

    def control(code):
        returned = ctypes.c_uint32()
        return kernel32.DeviceIoControl(volume, code, None, 0, None, 0, ctypes.byref(returned), None)

    volume = kernel32.CreateFileW(f'\\\\.\\{letter}:', GENERIC_READ_WRITE, FILE_SHARE_READ_WRITE, None,
                                  OPEN_EXISTING, 0, None)
    if volume == ctypes.c_void_p(-1).value:
        fail(f'Windows would not open the card at {letter}: (error {ctypes.get_last_error()})')
    try:
        if not kernel32.FlushFileBuffers(volume):
            fail(f'Windows could not write out what it holds for the card (error {ctypes.get_last_error()})')
        # the lock refuses while something has a file open, which the flush has already made safe
        control(FSCTL_LOCK_VOLUME)
        if not control(FSCTL_DISMOUNT_VOLUME):
            fail(f'Windows would not let go of the card (error {ctypes.get_last_error()})')
        yield
    finally:
        kernel32.CloseHandle(volume)


def card(side):
    if side is None:
        # the pin reads low until first driven, with the card on the PC, so ask Windows
        where = card_side()
        if where is None:
            fail('the SD mux is not on USB')
        print(where)
        return
    watts = status()['apower']
    if watts >= OFF_WATTS:
        fail(f'the Saturn draws {watts} W: power it off first')
    letter = card_letter() if side == 'saturn' else ''
    if letter:
        with card_released(letter):
            mux_pin(0)
    else:
        mux_pin(int(side == 'pc'))
    deadline = time.monotonic() + CARD_MOVE_S
    while card_side() != side:
        if time.monotonic() > deadline:
            fail(f'the mux switched, but Windows {"never saw the card" if side == "pc" else "still has the card"}')
        time.sleep(0.25)
    print(side)


def unchanged(source, target):
    """Whether the card's file looks the same as the source's, by size and time."""
    try:
        a, b = os.stat(source), os.stat(target)
    except OSError:
        return False
    return a.st_size == b.st_size and abs(a.st_mtime - b.st_mtime) < 2


def copy_to_card(source, target):
    """A file of the same size goes over the old one's own space, which leaves the card's
    directory and allocation tables unwritten. A failure is left for the compare to find."""
    try:
        keep_space = os.path.exists(target) and os.path.getsize(target) == os.path.getsize(source)
        with open(source, 'rb') as a, open(target, 'r+b' if keep_space else 'wb') as b:
            shutil.copyfileobj(a, b, COPY_CHUNK)
            b.flush()
            os.fsync(b.fileno())
        stat = os.stat(source)
        os.utime(target, ns=(stat.st_atime_ns, stat.st_mtime_ns))
    except OSError as e:
        print(f'{target}: {e}')


def same_bytes(source, target):
    try:
        with open(source, 'rb') as a, open(target, 'rb') as b:
            while True:
                chunk = a.read(COPY_CHUNK)
                if chunk != b.read(COPY_CHUNK):
                    return False
                if not chunk:
                    return True
    except OSError:
        return False


def disc_files(folder):
    """What goes on the card, as {name on the card: source}."""
    cues = glob.glob(os.path.join(glob.escape(folder), '*.cue'))
    if len(cues) != 1:
        fail(f'{folder} should hold one cue, and holds {len(cues)}')
    with open(cues[0]) as f:
        tracks = re.findall(r'FILE "([^"]+)"', f.read())
    files = {'autoboot.cue': cues[0], **{t: os.path.join(folder, t) for t in tracks}}
    missing = [t for t in tracks if not os.path.exists(files[t])]
    if not tracks or missing:
        fail(f'{cues[0]} names {len(tracks)} tracks; not in the folder: {missing}')
    return files


def deploy(folder, out):
    files = disc_files(folder)
    if card_side() is None:
        fail('the SD mux is not on USB')

    s = status()
    if s['apower'] >= OFF_WATTS and not s['aenergy']['by_minute'][1]:
        print('on for under two minutes: leaving the Satiator time to finish writing')
        time.sleep(SATURN_WRITES_S)
    power_off()
    card('pc')
    root = card_root()
    if not os.path.exists(root + 'menu.bin'):
        fail(f'{root} has no menu.bin at its root, so it is not the card to write to')

    copying = [name for name, source in files.items() if not unchanged(source, root + name)]
    for _ in range(COPY_TRIES):
        start = time.monotonic()
        for name in copying:
            copy_to_card(files[name], root + name)
        print(f'copied {len(copying)} of {len(files)} files in {time.monotonic() - start:.0f} s')

        with card_released(root[0]):
            pass
        root = card_root()
        start = time.monotonic()
        copying = [name for name, source in files.items() if not same_bytes(source, root + name)]
        if not copying:
            print(f'all {len(files)} files on the card are equal to {folder} ({time.monotonic() - start:.0f} s)')
            break
        print(f'different on the card: {" ".join(copying)}')
    else:
        fail(f'the card still differs after {COPY_TRIES} tries: the Saturn stays off and the card on the PC')

    card('saturn')
    power_on()
    time.sleep(BOOT_S)
    shot(out)


def test(folder, name, why, settle, shots, every, out, wait):
    disc_files(folder)      # a bad folder is refused before anyone waits for the Saturn
    if not lock_take(name, why, LOCK_MINUTES, wait):
        sys.exit(1)
    try:
        deploy(folder, f'{out}_boot.png')
        lock_needed(name)
        time.sleep(settle)
        for i in range(shots):
            if i:
                time.sleep(every)
            shot(f'{out}_{i + 1}.png')
    finally:
        lock_release(name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('test')
    p.add_argument('folder')
    p.add_argument('--as', dest='name', required=True)
    p.add_argument('--why', default='')
    p.add_argument('--settle', type=float, default=30)
    p.add_argument('--shots', type=int, default=3)
    p.add_argument('--every', type=float, default=5)
    p.add_argument('--out', default='build/hw_test')
    p.add_argument('--wait', type=float, default=0)
    p = sub.add_parser('lock')
    p.add_argument('name', nargs='?')
    p.add_argument('--why', default='')
    p.add_argument('--minutes', type=float, default=LOCK_MINUTES)
    p.add_argument('--wait', type=float, default=0)
    p = sub.add_parser('unlock')
    p.add_argument('name')
    p = sub.add_parser('power')
    p.add_argument('action', nargs='?', choices=['on', 'off', 'cycle'])
    p.add_argument('--as', dest='name')
    p = sub.add_parser('shot')
    p.add_argument('--out', default='build/hw_shot.png')
    p = sub.add_parser('record')
    p.add_argument('seconds', type=float)
    p.add_argument('--out', default='build/hw_record.mkv')
    p.add_argument('--as', dest='name')
    p = sub.add_parser('card')
    p.add_argument('side', nargs='?', choices=['pc', 'saturn'])
    p.add_argument('--as', dest='name')
    p = sub.add_parser('deploy')
    p.add_argument('folder', nargs='?', default='build/disc')
    p.add_argument('--out', default='build/hw_shot.png')
    p.add_argument('--as', dest='name')
    args = ap.parse_args()
    if args.command == 'lock':
        if args.name:
            if not lock_take(args.name, args.why, args.minutes, args.wait):
                sys.exit(1)
        with lock_state() as state:
            print(held_text(state) if holder_of(state) else 'the Saturn is free')
        return
    if args.command == 'unlock':
        lock_release(args.name)
        return
    if args.command == 'test':
        test(args.folder, args.name, args.why, args.settle, args.shots, args.every, args.out, args.wait)
        return
    changes = args.command in ('deploy', 'record') or (args.command == 'power' and args.action) or \
        (args.command == 'card' and args.side)
    if changes:
        lock_needed(args.name)
    if args.command == 'power':
        power(args.action)
    elif args.command == 'shot':
        shot(args.out)
    elif args.command == 'card':
        card(args.side)
    elif args.command == 'deploy':
        deploy(args.folder, args.out)
    else:
        record(args.seconds, args.out)


if __name__ == '__main__':
    main()
