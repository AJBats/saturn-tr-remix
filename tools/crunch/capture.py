"""Captures one VDP1 plot of the running game in a private Mednafen, for
checking the lab's VDP1 against Mednafen's.

    capture.py NAME [--frame N] [--cue CUE] [--elf ELF]

Boots CUE (default build/disc_crunch, the ledge start), runs to frame N, then
stops at plotStart (VDP1 idle, the list uploaded) and dumps VDP1 VRAM, frame
buffer 0 and both work RAMs; then stops at spriteEndHandler (the list drawn)
and dumps frame buffer 0 again. Mednafen's memory reads see frame buffer 0
only, so a plot drawn into buffer 1 is skipped for the next one. Writes
build/crunch/captures/NAME_{vram,fb_pre,fb_post,hwram,lwram}.bin.
"""
import argparse
import os
import subprocess
import sys

from medclient import LAB, ROOT, Mednafen, fwd

NM = os.path.join(ROOT, 'toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-nm')


def symbols(elf):
    syms = {}
    for line in subprocess.run([NM, '-C', elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            syms.setdefault(parts[2], int(parts[0], 16))
    return syms


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('name')
    ap.add_argument('--frame', type=int, default=1800)
    ap.add_argument('--cue', default=os.path.join(ROOT, 'build/disc_crunch/tomb.cue'))
    ap.add_argument('--elf', default=os.path.join(LAB, '0MAIN.elf'))
    args = ap.parse_args()

    syms = symbols(args.elf)
    plot = next(a for s, a in syms.items() if s.startswith('plotStart('))
    drawn = syms['spriteEndHandler']
    out_dir = os.path.join(LAB, 'captures')
    os.makedirs(out_dir, exist_ok=True)
    out = lambda kind: fwd(os.path.join(out_dir, f'{args.name}_{kind}.bin'))

    with Mednafen(fwd(args.cue), 'capture') as m:
        m.send(f'run_to_frame {args.frame}', 'done run_to_frame', 300)
        for attempt in range(4):
            m.send('breakpoint_clear', 'breakpoint_clear')
            m.send(f'breakpoint {plot:08X}', 'ok breakpoint')
            m.send('run', 'break pc=', 60)
            m.send(f'dump_region vdp1_vram {out("vram")}', 'ok dump_region')
            m.send(f'dump_mem_bin 05C80000 40000 {out("fb_pre")}', 'dump_mem_bin')
            m.send(f'dump_region wram_high {out("hwram")}', 'ok dump_region')
            m.send(f'dump_region wram_low {out("lwram")}', 'ok dump_region')
            m.send('breakpoint_clear', 'breakpoint_clear')
            m.send(f'breakpoint {drawn:08X}', 'ok breakpoint')
            reply = m.send('run', 'break pc=', 60)
            m.send(f'dump_mem_bin 05C80000 40000 {out("fb_post")}', 'dump_mem_bin')
            m.send('breakpoint_clear', 'breakpoint_clear')
            with open(out('fb_pre'), 'rb') as a, open(out('fb_post'), 'rb') as b:
                if a.read() != b.read():
                    print(f'{args.name}: plot captured ({reply})')
                    return
        sys.exit('frame buffer 0 never changed under a plot')


if __name__ == '__main__':
    main()
