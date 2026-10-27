"""Captures a depth-order test case from a running Mednafen (tools/depthcheck.py).

    depthcapture.py STATE CASE [--layers camera|one]

Loads save state STATE (made with build/depth_cases/0MAIN.elf's binary), sets
the depth layers option, and dumps high work RAM twice for build/depth_cases:
CASE_faces.bin in flush_c just after roomsFinish returns, both ordering
tables full, and CASE_cmds.bin at the next flush_c, that frame's commands
still in gCmds. Leaves the emulator paused with no breakpoints.

It talks to the Mednafen the MCP server launched, through the same command
and reply files (build/mcp_ipc): run it only while no MCP tool call is in
flight.
"""
import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IPC = os.path.join(ROOT, 'build/mcp_ipc')
CASES = os.path.join(ROOT, 'build/depth_cases')
OBJDUMP = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-objdump')
NM = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-nm')

ACTION = os.path.join(IPC, 'mednafen_action.txt')
ACK = os.path.join(IPC, 'mednafen_ack.txt')


def ack():
    try:
        with open(ACK) as f:
            return f.read().strip()
    except OSError:
        return ''


def send(cmd, expect, timeout=30):
    """Sends one command and returns the reply holding expect."""
    before = ack()
    tmp = ACTION + '.tmp'
    with open(tmp, 'w', newline='\n') as f:
        f.write(f'# depthcapture {time.time_ns()}\n{cmd}\n')
    for attempt in range(50):
        try:
            os.replace(tmp, ACTION)
            break
        except PermissionError:     # Mednafen is reading it
            time.sleep(0.02 * (attempt + 1))
    else:
        os.replace(tmp, ACTION)
    deadline = time.time() + timeout
    while time.time() < deadline:
        reply = ack()
        if reply != before and reply:
            if expect in reply:
                return reply
            if 'error' in reply.split(' cycle=')[0]:
                sys.exit(f'{cmd}: {reply}')
        time.sleep(0.02)
    sys.exit(f'{cmd}: no reply with "{expect}" in {timeout}s (last: {ack()})')


def addresses(elf):
    """flush_c, the break just after its call of roomsFinish returns, gDepthLayers, gSlaveOT."""
    syms = {}
    for line in subprocess.run([NM, '-C', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            syms.setdefault(parts[2], int(parts[0], 16))
    flush = syms['flush_c()']
    dis = subprocess.run([OBJDUMP, '-d', f'--start-address={flush:#x}', f'--stop-address={flush + 0x80:#x}', elf],
                         capture_output=True, text=True, check=True).stdout
    lines = dis.splitlines()
    at = next(i for i, l in enumerate(lines) if 'roomsFinish' in l)
    jsr = next(l for l in lines[at:] if re.search(r'\tjsr\t', l))
    ret = int(jsr.split(':')[0], 16) + 4
    # Mednafen stops at a breakpoint as its instruction is fetched, two
    # instructions ahead of the one executing: this one stops right after
    # the instruction at the return address.
    return flush, ret + 6, ret, syms['gDepthLayers'], syms['gSlaveOT']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('state', help='save state (.mcs)')
    ap.add_argument('case', help='name for the dumps in build/depth_cases')
    ap.add_argument('--layers', choices=('camera', 'one'), default='camera', help='depth layers option')
    args = ap.parse_args()

    elf = os.path.join(CASES, '0MAIN.elf')
    flush, after_rooms, ret, layers, slave_ot = addresses(elf)
    state = os.path.abspath(args.state).replace('\\', '/')
    out = lambda kind: os.path.join(CASES, f'{args.case}_{kind}.bin').replace('\\', '/')

    send('pause', 'ok pause')
    send('breakpoint_clear', 'breakpoint_clear')
    send(f'load_state {state}', 'ok load_state')
    send(f'poke {layers + 3:08X} {1 if args.layers == "camera" else 0:02X}', 'ok poke')
    send('frame_advance 3', 'done frame_advance', 60)

    send(f'breakpoint {after_rooms:08X}', 'breakpoint')
    reply = send('run', 'break pc=', 60)
    if f'PR={ret:08X}' not in reply or f'R0={slave_ot:08X}' not in reply:
        sys.exit(f'stopped elsewhere than after roomsFinish: {reply}')
    send(f'dump_region wram_high {out("faces")}', 'dump_region')
    send('breakpoint_clear', 'breakpoint_clear')

    send(f'breakpoint {flush:08X}', 'breakpoint')
    send('run', 'break pc=', 60)
    send(f'dump_region wram_high {out("cmds")}', 'dump_region')
    send('breakpoint_clear', 'breakpoint_clear')
    print(f'{args.case}: {out("faces")}, {out("cmds")}')


if __name__ == '__main__':
    main()
