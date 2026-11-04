"""A private Mednafen for another game's disc, driven step by step: its own home
and automation folder, so nothing else's window or dialog is disturbed. The
emulator stays up between calls.

    spy.py start CUE
    spy.py run N              advance N frames
    spy.py tap BUTTON [N]     hold BUTTON N frames, then release
    spy.py hold BUTTON N      press BUTTON, advance N, leave it held
    spy.py rel BUTTON
    spy.py shot FILE [FRAMES] advance FRAMES (default 2), then a screenshot
    spy.py cmd "COMMAND" EXPECT
    spy.py regs FILE          VDP2's registers into FILE (vdp2dec.py reads it)
    spy.py dump REGION FILE
    spy.py pos               the rotation parameters' place and matrix (SPY_TABLE: VRAM offset, hex)
    spy.py quit

To hide a layer, poke the game's register shadow if it has one (SBL/SGL games
copy one to VDP2 each frame; poke itself can't reach VDP2): Sonic Jam's is at
060FF960, BGON at +20, PRISA at +F0.
"""
import os
import subprocess
import sys
import time

WORK = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(WORK, 'tools'))
NAME = os.environ.get('SPY_NAME', 'spy')
IPC = os.path.join(WORK, 'build', NAME + '_ipc')
ACTION = os.path.join(IPC, 'mednafen_action.txt')
ACK = os.path.join(IPC, 'mednafen_ack.txt')
SEQ = os.path.join(IPC, 'seq.txt')


def ack():
    try:
        with open(ACK) as f:
            return f.read().strip()
    except OSError:
        return ''


def send(cmd, expect, timeout=600):
    before = ack()
    seq = int(open(SEQ).read()) + 1 if os.path.exists(SEQ) else 1000
    with open(SEQ, 'w') as f:
        f.write(str(seq))
    tmp = ACTION + '.tmp'
    with open(tmp, 'w', newline='\n') as f:
        f.write(f'# spy {seq}{"." * (seq % 16)}\n{cmd}\n')
    for attempt in range(50):
        try:
            os.replace(tmp, ACTION)
            break
        except PermissionError:
            time.sleep(0.02 * (attempt + 1))
    deadline = time.time() + timeout
    while time.time() < deadline:
        reply = ack()
        if reply and reply != before and expect in reply:
            return reply
        time.sleep(0.03)
    sys.exit(f'{cmd}: no reply with "{expect}" (last: {ack()})')


def advance(n):
    return send(f'frame_advance {n}', 'done frame_advance')


def shot(path):
    path = os.path.abspath(path).replace(os.sep, '/')
    if os.path.exists(path):
        os.remove(path)
    send(f'screenshot {path}', 'screenshot')
    advance(1)
    for _ in range(100):
        if os.path.exists(path) and os.path.getsize(path):
            return path
        time.sleep(0.05)
    sys.exit('no screenshot')


REGNAMES = {
    0x00: 'TVMD', 0x02: 'EXTEN', 0x04: 'TVSTAT', 0x06: 'VRSIZE', 0x08: 'HCNT', 0x0A: 'VCNT',
    0x0E: 'RAMCTL', 0x10: 'CYCA0L', 0x12: 'CYCA0U', 0x14: 'CYCA1L', 0x16: 'CYCA1U',
    0x18: 'CYCB0L', 0x1A: 'CYCB0U', 0x1C: 'CYCB1L', 0x1E: 'CYCB1U', 0x20: 'BGON', 0x22: 'MZCTL',
    0x24: 'SFSEL', 0x26: 'SFCODE', 0x28: 'CHCTLA', 0x2A: 'CHCTLB', 0x2C: 'BMPNA', 0x2E: 'BMPNB',
    0x30: 'PNCN0', 0x32: 'PNCN1', 0x34: 'PNCN2', 0x36: 'PNCN3', 0x38: 'PNCR', 0x3A: 'PLSZ',
    0x3C: 'MPOFN', 0x3E: 'MPOFR', 0x40: 'MPABN0', 0x42: 'MPCDN0', 0x44: 'MPABN1', 0x46: 'MPCDN1',
    0x48: 'MPABN2', 0x4A: 'MPCDN2', 0x4C: 'MPABN3', 0x4E: 'MPCDN3', 0x50: 'MPABRA', 0x52: 'MPCDRA',
    0x54: 'MPEFRA', 0x56: 'MPGHRA', 0x58: 'MPIJRA', 0x5A: 'MPKLRA', 0x5C: 'MPMNRA', 0x5E: 'MPOPRA',
    0x60: 'MPABRB', 0x62: 'MPCDRB', 0x64: 'MPEFRB', 0x66: 'MPGHRB', 0x68: 'MPIJRB', 0x6A: 'MPKLRB',
    0x6C: 'MPMNRB', 0x6E: 'MPOPRB',
    0x70: 'SCXIN0', 0x72: 'SCXDN0', 0x74: 'SCYIN0', 0x76: 'SCYDN0', 0x78: 'ZMXIN0', 0x7A: 'ZMXDN0',
    0x7C: 'ZMYIN0', 0x7E: 'ZMYDN0', 0x80: 'SCXIN1', 0x82: 'SCXDN1', 0x84: 'SCYIN1', 0x86: 'SCYDN1',
    0x88: 'ZMXIN1', 0x8A: 'ZMXDN1', 0x8C: 'ZMYIN1', 0x8E: 'ZMYDN1', 0x90: 'SCXN2', 0x92: 'SCYN2',
    0x94: 'SCXN3', 0x96: 'SCYN3', 0x98: 'ZMCTL', 0x9A: 'SCRCTL', 0x9C: 'VCSTAU', 0x9E: 'VCSTAL',
    0xA0: 'LSTA0U', 0xA2: 'LSTA0L', 0xA4: 'LSTA1U', 0xA6: 'LSTA1L', 0xA8: 'LCTAU', 0xAA: 'LCTAL',
    0xAC: 'BKTAU', 0xAE: 'BKTAL', 0xB0: 'RPMD', 0xB2: 'RPRCTL', 0xB4: 'KTCTL', 0xB6: 'KTAOF',
    0xB8: 'OVPNRA', 0xBA: 'OVPNRB', 0xBC: 'RPTAU', 0xBE: 'RPTAL', 0xC0: 'WPSX0', 0xC2: 'WPSY0',
    0xC4: 'WPEX0', 0xC6: 'WPEY0', 0xC8: 'WPSX1', 0xCA: 'WPSY1', 0xCC: 'WPEX1', 0xCE: 'WPEY1',
    0xD0: 'WCTLA', 0xD2: 'WCTLB', 0xD4: 'WCTLC', 0xD6: 'WCTLD', 0xD8: 'LWTA0U', 0xDA: 'LWTA0L',
    0xDC: 'LWTA1U', 0xDE: 'LWTA1L', 0xE0: 'SPCTL', 0xE2: 'SDCTL', 0xE4: 'CRAOFA', 0xE6: 'CRAOFB',
    0xE8: 'LNCLEN', 0xEA: 'SFPRMD', 0xEC: 'CCCTL', 0xEE: 'SFCCMD', 0xF0: 'PRISA', 0xF2: 'PRISB',
    0xF4: 'PRISC', 0xF6: 'PRISD', 0xF8: 'PRINA', 0xFA: 'PRINB', 0xFC: 'PRIR', 0x100: 'CCRSA',
    0x102: 'CCRSB', 0x104: 'CCRSC', 0x106: 'CCRSD', 0x108: 'CCRNA', 0x10A: 'CCRNB', 0x10C: 'CCRR',
    0x10E: 'CCRLB', 0x110: 'CLOFEN', 0x112: 'CLOFSL', 0x114: 'COAR', 0x116: 'COAG', 0x118: 'COAB',
    0x11A: 'COBR', 0x11C: 'COBG', 0x11E: 'COBB',
}


def main():
    op = sys.argv[1]
    if op == 'start':
        import capture
        os.makedirs(IPC, exist_ok=True)
        m = capture.Mednafen(sys.argv[2], NAME)
        print('started pid', m.proc.pid, ack())
    elif op == 'run':
        print(advance(int(sys.argv[2])))
    elif op == 'tap':
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 6
        send(f'input {sys.argv[2]}', 'input')
        advance(n)
        send(f'input_release {sys.argv[2]}', 'input_release')
        print(advance(1))
    elif op == 'hold':
        send(f'input {sys.argv[2]}', 'input')
        print(advance(int(sys.argv[3])))
    elif op == 'rel':
        print(send(f'input_release {sys.argv[2]}', 'input_release'))
    elif op == 'shot':
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 2
        if n:
            advance(n)
        print(shot(sys.argv[2]), ack())
    elif op == 'cmd':
        print(send(sys.argv[2], sys.argv[3]))
    elif op == 'regs':
        path = os.path.abspath(sys.argv[2]).replace(os.sep, '/')
        send(f'dump_vdp2_regs {path}', 'dump_vdp2_regs')
        time.sleep(0.2)
        data = open(path, 'rb').read()
        print(len(data), 'bytes')
        if len(sys.argv) > 3:
            print(data[:64].hex())
    elif op == 'dump':
        path = os.path.abspath(sys.argv[3]).replace(os.sep, '/')
        print(send(f'dump_region {sys.argv[2]} {path}', 'dump_region'))
    elif op == 'pos':
        import struct
        tmp = os.path.join(IPC, 'rp.bin').replace(os.sep, '/')
        table = 0x25E00000 + int(os.environ.get('SPY_TABLE', '5ff20'), 16)
        send(f'dump_mem_bin {table:x} 100 {tmp}', 'dump_mem_bin', 30)
        time.sleep(0.05)
        d = open(tmp, 'rb').read()
        for label, b in (('A', 0), ('B', 0x80)):
            m = [x / 65536 for x in struct.unpack('>6i', d[b + 0x1C:b + 0x34])]
            mx, my = [x / 65536 for x in struct.unpack('>2i', d[b + 0x44:b + 0x4C])]
            print(label, 'M', round(mx, 1), round(my, 1), 'matrix', ' '.join(f'{x:.3f}' for x in m))
    elif op == 'quit':
        print(send('quit', 'quit', 10))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
