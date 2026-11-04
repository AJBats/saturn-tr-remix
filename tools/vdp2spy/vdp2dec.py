"""Decodes a Mednafen VDP2 register dump (dump_vdp2_regs: 256 big-endian words)
and, given the VRAM dump, the rotation parameter tables and coefficient table.

    vdp2dec.py REGS.bin [VRAM.bin]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spy

COLORS = {0: '16', 1: '256', 2: '2048', 3: '32K', 4: '16M'}


def regs(path):
    w = struct.unpack('>256H', open(path, 'rb').read()[:512])
    return {spy.REGNAMES[o]: w[o // 2] for o in spy.REGNAMES}, w


def sx(x, b):
    x &= (1 << b) - 1
    return x - (1 << b) if x >> (b - 1) else x


def bits(v, hi, lo):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


def main():
    r, raw = regs(sys.argv[1])
    print('all registers:')
    names = list(spy.REGNAMES.items())
    for i in range(0, len(names), 8):
        print('  ' + ' '.join(f'{n}={raw[o // 2]:04X}' for o, n in names[i:i + 8]))
    bg = r['BGON']
    print(f"BGON {bg:04X}: shown " + ' '.join(n for b, n in enumerate(['NBG0', 'NBG1', 'NBG2', 'NBG3', 'RBG0', 'RBG1']) if bg >> b & 1)
          + ' | transparent-code-off (TPON) ' + ' '.join(n for b, n in enumerate(['N0', 'N1', 'N2', 'N3', 'R0']) if bg >> (b + 8) & 1))
    a, b = r['CHCTLA'], r['CHCTLB']
    print(f"NBG0: {COLORS.get(bits(a,6,4))} col, bitmap {bits(a,1,1)}, cell {'2x2' if a & 1 else '1x1'}, bmsize {bits(a,3,2)}")
    print(f"NBG1: {COLORS.get(bits(a,13,12))} col, bitmap {bits(a,9,9)}, cell {'2x2' if a >> 8 & 1 else '1x1'}, bmsize {bits(a,11,10)}")
    print(f"NBG2: {'256' if b & 2 else '16'} col, cell {'2x2' if b & 1 else '1x1'}")
    print(f"NBG3: {'256' if b >> 5 & 1 else '16'} col, cell {'2x2' if b >> 4 & 1 else '1x1'}")
    print(f"RBG0: {COLORS.get(bits(b,14,12))} col, bitmap {bits(b,9,9)}, bmsize {bits(b,10,10)}, cell {'2x2' if b >> 8 & 1 else '1x1'}")
    print(f"PLSZ {r['PLSZ']:04X}: N0 {bits(r['PLSZ'],1,0)} N1 {bits(r['PLSZ'],3,2)} N2 {bits(r['PLSZ'],5,4)} N3 {bits(r['PLSZ'],7,6)} RA plane {bits(r['PLSZ'],9,8)} over {bits(r['PLSZ'],11,10)} RB plane {bits(r['PLSZ'],13,12)} over {bits(r['PLSZ'],15,14)}")
    rm = r['RAMCTL']
    print(f"RAMCTL {rm:04X}: CRAM mode {bits(rm,13,12)}, coef in CRAM {rm >> 15 & 1}, VRAM A split {rm >> 8 & 1}, B split {rm >> 9 & 1}, rotation banks A0 {bits(rm,1,0)} A1 {bits(rm,3,2)} B0 {bits(rm,5,4)} B1 {bits(rm,7,6)} (0 none, 1 coef, 2 names, 3 chars)")
    print(f"cycles A0 {r['CYCA0L']:04X} {r['CYCA0U']:04X}  A1 {r['CYCA1L']:04X} {r['CYCA1U']:04X}  B0 {r['CYCB0L']:04X} {r['CYCB0U']:04X}  B1 {r['CYCB1L']:04X} {r['CYCB1U']:04X}")
    print(f"RPMD {r['RPMD']:04X} (0 A only, 1 B only, 2 by coef's transparent bit, 3 by rotation window)  RPRCTL {r['RPRCTL']:04X} RPTA {r['RPTAU']:04X}{r['RPTAL']:04X} (table at VRAM {((r['RPTAU'] & 7) << 16 | r['RPTAL']) * 2:06X})")
    print(f"KTCTL {r['KTCTL']:04X} KTAOF {r['KTAOF']:04X} OVPNRA {r['OVPNRA']:04X} OVPNRB {r['OVPNRB']:04X} MPOFR {r['MPOFR']:04X} PNCR {r['PNCR']:04X}")
    print(f"map RA: {r['MPABRA']:04X} {r['MPCDRA']:04X} {r['MPEFRA']:04X} {r['MPGHRA']:04X} {r['MPIJRA']:04X} {r['MPKLRA']:04X} {r['MPMNRA']:04X} {r['MPOPRA']:04X}")
    print(f"map RB: {r['MPABRB']:04X} {r['MPCDRB']:04X} {r['MPEFRB']:04X} {r['MPGHRB']:04X} {r['MPIJRB']:04X} {r['MPKLRB']:04X} {r['MPMNRB']:04X} {r['MPOPRB']:04X}")
    print(f"PRI: SA {r['PRISA']:04X} SB {r['PRISB']:04X} SC {r['PRISC']:04X} SD {r['PRISD']:04X} | N0 {r['PRINA'] & 7} N1 {r['PRINA'] >> 8 & 7} N2 {r['PRINB'] & 7} N3 {r['PRINB'] >> 8 & 7} R0 {r['PRIR'] & 7}")
    print(f"SPCTL {r['SPCTL']:04X}: sprite type {r['SPCTL'] & 15}, mixed {r['SPCTL'] >> 5 & 1}, SPWINEN {r['SPCTL'] >> 4 & 1}, CC cond {bits(r['SPCTL'],13,12)} num {bits(r['SPCTL'],10,8)} | SDCTL {r['SDCTL']:04X} (shadow) | SFPRMD {r['SFPRMD']:04X} SFSEL {r['SFSEL']:04X} SFCODE {r['SFCODE']:04X} SFCCMD {r['SFCCMD']:04X}")
    print(f"CCCTL {r['CCCTL']:04X} (N0 {r['CCCTL'] & 1} N1 {r['CCCTL'] >> 1 & 1} N2 {r['CCCTL'] >> 2 & 1} N3 {r['CCCTL'] >> 3 & 1} R0 {r['CCCTL'] >> 4 & 1} LCC {r['CCCTL'] >> 5 & 1} SP {r['CCCTL'] >> 6 & 1} mode {r['CCCTL'] >> 8 & 1} extended {r['CCCTL'] >> 10 & 1} gradation {r['CCCTL'] >> 15 & 1}/{r['CCCTL'] >> 12 & 7} ) "
          f"rates SA {r['CCRSA']:04X} SB {r['CCRSB']:04X} SC {r['CCRSC']:04X} SD {r['CCRSD']:04X} N {r['CCRNA']:04X} {r['CCRNB']:04X} R {r['CCRR']:04X} LB {r['CCRLB']:04X}")
    print(f"LNCLEN {r['LNCLEN']:04X} LCTA {r['LCTAU']:04X}{r['LCTAL']:04X} BKTA {r['BKTAU']:04X}{r['BKTAL']:04X} CLOFEN {r['CLOFEN']:04X} CLOFSL {r['CLOFSL']:04X} COA {r['COAR']:03X} {r['COAG']:03X} {r['COAB']:03X} COB {r['COBR']:03X} {r['COBG']:03X} {r['COBB']:03X}")
    print(f"windows: W0 ({r['WPSX0']},{r['WPSY0']})-({r['WPEX0']},{r['WPEY0']}) W1 ({r['WPSX1']},{r['WPSY1']})-({r['WPEX1']},{r['WPEY1']}) WCTL {r['WCTLA']:04X} {r['WCTLB']:04X} {r['WCTLC']:04X} {r['WCTLD']:04X} (R0 in low byte of C) LWTA {r['LWTA0U']:04X}{r['LWTA0L']:04X} {r['LWTA1U']:04X}{r['LWTA1L']:04X}")
    print(f"NBG scroll: N0 {r['SCXIN0']}.{r['SCXDN0']:04X},{r['SCYIN0']}.{r['SCYDN0']:04X} zoom {r['ZMXIN0']}.{r['ZMXDN0']:04X} | N1 {r['SCXIN1']}.{r['SCXDN1']:04X},{r['SCYIN1']}.{r['SCYDN1']:04X} zoom {r['ZMXIN1']}.{r['ZMXDN1']:04X} | N2 {r['SCXN2']},{r['SCYN2']} N3 {r['SCXN3']},{r['SCYN3']} MZCTL {r['MZCTL']:04X} ZMCTL {r['ZMCTL']:04X} SCRCTL {r['SCRCTL']:04X}")
    print(f"TVMD {r['TVMD']:04X} EXTEN {r['EXTEN']:04X} VRSIZE {r['VRSIZE']:04X} CRAOF A {r['CRAOFA']:04X} B {r['CRAOFB']:04X}")
    if len(sys.argv) > 2:
        vram = open(sys.argv[2], 'rb').read()
        at = ((r['RPTAU'] & 7) << 16 | r['RPTAL']) * 2 & 0x7FFFF
        names_ = ['Xst', 'Yst', 'Zst', 'DXst', 'DYst', 'DX', 'DY', 'A', 'B', 'C', 'D', 'E', 'F']
        for label, base in (('A', at & ~0x80), ('B', at | 0x80)):
            vals = struct.unpack('>25i', vram[base:base + 100])[:13]
            print(f'params {label} @{base:06X}:', ' '.join(f'{n}={v / 65536:.4f}' for n, v in zip(names_, vals)))
            px, py, pz, _ = struct.unpack('>4h', vram[base + 0x34:base + 0x3C])
            cx, cy, cz, _ = struct.unpack('>4h', vram[base + 0x3C:base + 0x44])
            mx, my, kx, ky = struct.unpack('>4i', vram[base + 0x44:base + 0x54])
            print(f'    P=({px},{py},{pz}) C=({cx},{cy},{cz}) M=({mx / 65536:.3f},{my / 65536:.3f}) k=({kx / 65536:.4f},{ky / 65536:.4f})')
            ktaof = (r['KTAOF'] >> (0 if label == 'A' else 8)) & 7
            raw_ = struct.unpack('>3I', vram[base + 0x54:base + 0x60])
            ka, dk = raw_[0] >> 6, sx(raw_[1] >> 6, 20)
            print(f'    coefficients: KAst {ka:08X} dKAst {dk} dKAx {sx(raw_[2] >> 6, 20)} table offset {ktaof}; by line:')
            acc = (ktaof << 26) + ka
            for y in range(224):
                at_ = ((acc >> 10) << 1) & 0x3FFFF
                c = struct.unpack('>I', vram[at_ * 2:at_ * 2 + 4])[0]
                if y % 32 == 0:
                    print(f'      y {y:3d} @{at_ * 2:05X} {c:08X} transparent {c >> 31} line colour {(c >> 24) & 0x7F:3d} k {sx(c & 0xFFFFFF, 24) / 65536:8.3f}')
                acc = (acc + dk) & 0xFFFFFFFF


if __name__ == '__main__':
    main()
