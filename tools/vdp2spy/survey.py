"""Boots a disc in a private Mednafen, tapping START now and then, and prints each
new VDP2 set-up it comes across: the display mode, layers and VRAM cycle words.
A survey of what shipped games really do on the Saturn.

    SPY_NAME=name survey.py CUE [frames] [every] [buttons]

buttons: those to tap in turn (default START). A shot of each set-up goes to
build/NAME_NNNN.png.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spy

COLORS = {0: '16', 1: '256', 2: '2048', 3: '32K', 4: '16M'}


def read_regs():
    path = os.path.join(spy.IPC, 'regs.bin').replace(os.sep, '/')
    spy.send(f'dump_vdp2_regs {path}', 'dump_vdp2_regs')
    time.sleep(0.03)
    w = struct.unpack('>256H', open(path, 'rb').read()[:512])
    return {spy.REGNAMES[o]: w[o // 2] for o in spy.REGNAMES}


def describe(r):
    h = r['TVMD'] & 7
    width = {0: 320, 1: 352, 2: 640, 3: 704, 4: 320, 5: 352, 6: 640, 7: 704}[h]
    lines = {0: 224, 1: 240, 2: 256}.get((r['TVMD'] >> 4) & 3, 0)
    bg = r['BGON']
    a, b = r['CHCTLA'], r['CHCTLB']
    layers = []
    if bg & 1:
        layers.append(f"N0 {COLORS.get((a >> 4) & 7)}{' bmp' if a & 2 else ''}")
    if bg & 2:
        layers.append(f"N1 {COLORS.get((a >> 12) & 3)}{' bmp' if a & 0x200 else ''}")
    if bg & 4:
        layers.append(f"N2 {'256' if b & 2 else '16'}")
    if bg & 8:
        layers.append(f"N3 {'256' if b & 0x20 else '16'}")
    if bg & 16:
        layers.append(f"R0 {COLORS.get((b >> 12) & 7)}{' bmp' if b & 0x200 else ''}")
    if bg & 32:
        layers.append('R1')
    rm = r['RAMCTL']
    return (f"{width}x{lines} TVMD {r['TVMD']:04X} BGON {bg:04X} [{', '.join(layers)}] RAMCTL {rm:04X} "
            f"A0 {r['CYCA0L']:04X} {r['CYCA0U']:04X} A1 {r['CYCA1L']:04X} {r['CYCA1U']:04X} "
            f"B0 {r['CYCB0L']:04X} {r['CYCB0U']:04X} B1 {r['CYCB1L']:04X} {r['CYCB1U']:04X} "
            f"RPMD {r['RPMD']:X} KTCTL {r['KTCTL']:04X} SPCTL {r['SPCTL']:04X} CCCTL {r['CCCTL']:04X}")


def main():
    cue = sys.argv[1]
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    every = int(sys.argv[3]) if len(sys.argv) > 3 else 120
    buttons = sys.argv[4].split(',') if len(sys.argv) > 4 else ['START']
    import capture
    os.makedirs(spy.IPC, exist_ok=True)
    capture.Mednafen(cue, spy.NAME)
    seen = set()
    press = 0
    for at in range(0, frames, every):
        spy.advance(every - 8)
        b = buttons[press % len(buttons)]
        press += 1
        spy.send(f'input {b}', 'input')
        spy.advance(6)
        spy.send(f'input_release {b}', 'input_release')
        spy.advance(2)
        r = read_regs()
        if not r['TVMD'] & 0x8000:
            continue
        d = describe(r)
        if d not in seen:
            seen.add(d)
            shot = os.path.join(spy.WORK, 'build', f'{spy.NAME}_{at:05d}.png')
            spy.shot(shot)
            print(f'frame {at:5d}: {d}', flush=True)
    spy.send('quit', 'quit', 10)


if __name__ == '__main__':
    main()
