"""The Saturn on the desk, driven from here.

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
        saves what the capture card shows right now (build/hw_shot.png)
    hw.py record SECONDS [--out MKV]
        records that long as OBS is set up to, moved from OBS's folder to
        build/hw_record.mkv; refuses if OBS is already recording
    hw.py card
        which side the SD mux holds the card on: pc or saturn
    hw.py card pc|saturn
        moves the card there and checks Windows saw it arrive or leave; refuses
        while the Saturn draws power, since it may be using the card

Exits with 1 if the plug doesn't answer or the reading isn't what it should
be. The plug is a Shelly Plug US Gen4 on the home network, found by its name.
OBS holds the capture card, so pictures come through its WebSocket server
(Tools > WebSocket Server Settings, on), with the password from OBS's config.
The SD mux is an SDWire, driven through Windows' own FTDI driver; the chip's
CBUS0 pin was set to general I/O once, which holds until someone changes it.
"""

import argparse
import base64
import ctypes
import hashlib
import json
import os
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


def fail(message):
    print(f'hw.py: {message}', file=sys.stderr)
    sys.exit(1)


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


def card_letters():
    """The drive letters of the card while the PC has it, None while it doesn't."""
    script = (f"$d = Get-Disk | Where-Object SerialNumber -eq '{CARD_READER}';"
              "if ($d.Size) { 'card ' + (@(Get-Partition -DiskNumber $d.Number -ErrorAction SilentlyContinue"
              " | Where-Object DriveLetter).DriveLetter -join ' ') }")
    words = subprocess.run(['powershell', '-NoProfile', '-Command', script],
                           capture_output=True, text=True).stdout.split()
    return words[1:] if words else None


def card(side):
    if side is None:
        # the pin reads low until first driven, with the card on the PC, so ask Windows
        print('pc' if card_letters() is not None else 'saturn')
        return
    watts = status()['apower']
    if watts >= OFF_WATTS:
        fail(f'the Saturn draws {watts} W: power it off first')
    to_pc = side == 'pc'
    mux_pin(int(to_pc))
    deadline = time.monotonic() + CARD_MOVE_S
    while (card_letters() is not None) != to_pc:
        if time.monotonic() > deadline:
            fail(f'the mux switched, but Windows {"never saw the card" if to_pc else "still has the card"}')
        time.sleep(0.25)
    print(side)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('power')
    p.add_argument('action', nargs='?', choices=['on', 'off', 'cycle'])
    p = sub.add_parser('shot')
    p.add_argument('--out', default='build/hw_shot.png')
    p = sub.add_parser('record')
    p.add_argument('seconds', type=float)
    p.add_argument('--out', default='build/hw_record.mkv')
    p = sub.add_parser('card')
    p.add_argument('side', nargs='?', choices=['pc', 'saturn'])
    args = ap.parse_args()
    if args.command == 'power':
        power(args.action)
    elif args.command == 'shot':
        shot(args.out)
    elif args.command == 'card':
        card(args.side)
    else:
        record(args.seconds, args.out)


if __name__ == '__main__':
    main()
