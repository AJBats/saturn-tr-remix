"""Writes meshrot.dsp: the meshes' vertices rotated and lit on the SCU DSP for
stage 2 (renderer/vertices.h), from their data packed as the level
keeps it (int16 coordinates, byte normals) and unpacked here. It makes what
meshlight3.dsp (dspbench/gen_meshlight3.py) makes from data laid out for
it, a longword each.

usage: gen_meshrot.py OUT.dsp

What comes out of a mesh is meshlight3.dsp's: each vertex
    x >> 14 << 16 | y >> 14 & 0xFFFF,  z >> 14 << 16 | t & 0x1F00
with t = ambient - ((n . l) * range >> 14).

What goes in (vertices.cpp packMesh), a group of 4 vertices:
  - lit by its normals, 10 longwords: each vertex's normal as bytes nx ny
    nz and a spare, each plus 128 (4 longwords); then the vertices' int16
    x y z one after another (6 longwords)
  - else the 6 longwords of x y z alone
The +128 comes off in the light's first coefficient: ambient << 16 | 0xFFFF
less 128 (l'x + l'y + l'z).

A group, in turn, in one copy of the program (two would not fit):
  - the unpacking into MD1 0..23, x y z nx ny nz a vertex. A normal's bytes
    rotated to the bottom and masked; a longword's high half from its
    product with 1 (ALH); its low half from its product with 65536, the
    low 32 bits of that times 1 (ALH). A mesh unlit by its normals starts
    at its coordinates (TOP), its light's coefficients 0
  - the previous group's output sent from MD2 16..23
  - the rows (meshlight3.dsp's): per vertex four rows of four products,
    the translation (or the ambient) times 1 first, a product taken and one
    added every instruction, each row's sum >> 16 into MD3; halfway, the
    next group's transfer into MD2 (its place and length the job's)
  - the packing (meshlight3.dsp's) into MD2 16..23
  - BTM to the next group (LOP the groups left), else the next job
Neither transfer shares a bank with what runs beside it.

The jobs (vertices.cpp MeshDspJob), a mesh each, 21 longwords:
    0..15 its coefficients, 4t 4m 4m 4m a row and ambient' l' l' l'
    16 its groups' address / 4   17 its groups (0 ends the list)
    18 where a group begins (MESHROT_DSP_NORMALS or _COORDS)
    19 where a group lands in MD2 (0, or 4 without normals)   20 its longwords
The outputs follow each other from the address the SH-2 sets, whole groups
of 4 vertices, after 8 longwords of nothing (the first group sends the
output before it). Each job's last group fetches the group after it: the
data must go on 10 longwords in high work RAM.

Data RAM (* set by the SH-2):
  MD0  0..20 the job
  MD1  0..23 a group's x y z nx ny nz a vertex  24 *65536  25 *0xFFFF
       26 *0x1F00  27 *0xFF
  MD2  0..9 a group's input  10 *1  16..23 a group's output
  MD3  0..6, 9..17 the rows' sums >> 16, x y z t a vertex  7 the job's
       place in MD2  8 its longwords  20 *the next job / 4
       21 *the first output / 4 - 8
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from dspasm import general  # noqa: E402

POINTS = 4          # vertices a group
OUT_AT = 1          # the rows' instruction the output's transfer follows
IN_AT = 30          # and the next group's (its D1 free, after the output's has gone)
GAP = 7             # MD3: the job's place and length, between a vertex's z and t sums


class Ins:
    """A general instruction: its operations, a bus at a time."""

    def __init__(self):
        self.alu = None
        self.x = []
        self.y = []
        self.d1 = None

    def put(self, bus, op):
        if bus == 'alu':
            assert self.alu is None, (self.alu, op)
            self.alu = op
        elif bus == 'd1':
            assert self.d1 is None, (self.d1, op)
            self.d1 = op
        else:
            getattr(self, bus).append(op)
        return self

    def text(self):
        ops = ([self.alu] if self.alu else []) + self.x + self.y + ([self.d1] if self.d1 else [])
        line = ' | '.join(ops) if ops else 'nop'
        general(line.split('|'), 0, {})     # the pairing rules: raises if broken
        return '        ' + line


def normals():
    """MD2 0..3 to each vertex's nx ny nz in MD1: a longword into the
    accumulator, a byte at a time rotated to the bottom and masked by P."""
    ins = [Ins().put('y', 'mov mc2,a').put('x', 'mov m1,p').put('d1', 'mov #3,ct1')]     # P = 0xFF
    for p in range(POINTS):
        for c in range(3):
            a = Ins().put('alu', 'rl8').put('y', 'mov alu,a')
            if c == 0 and p:
                a.put('d1', f'mov #{6 * p + 3},ct1')
            b = Ins().put('alu', 'and').put('d1', 'mov all,mc1')
            if c == 2 and p < POINTS - 1:
                b.put('y', 'mov mc2,a')
            ins += [a, b]
    return ins


def coordinates():
    """MD2's 6 longwords of x y z to MD1, each vertex's at 6p. A longword w
    into P and RX (RY is 65536 throughout): its high half w * 1 >> 16 (AD2
    from 0, ALH); its low half from w * 65536, whose low 32 bits (ADD from
    0) go into P and come out >> 16 (AD2 from 0, ALH)."""
    ins = [Ins().put('x', 'mov mc2,p').put('x', 'mov mc2,x').put('y', 'clr a').put('d1', 'mov #0,ct1')]
    # the writes' places: a vertex's x y z, then a jump past its normal
    jumps = {(1, 'lo'): 6, (3, 'hi'): 12, (4, 'lo'): 18}
    for k in range(6):
        if (k, 'hi') in jumps:
            ins.append(Ins().put('d1', f'mov #{jumps[(k, "hi")]},ct1'))
        ins.append(Ins().put('alu', 'ad2').put('x', 'mov mul,p').put('d1', 'mov alh,mc1'))
        ins.append(Ins().put('alu', 'add').put('d1', 'mov all,pl'))
        if (k, 'lo') in jumps:
            ins.append(Ins().put('d1', f'mov #{jumps[(k, "lo")]},ct1'))
        lo = Ins().put('alu', 'ad2').put('d1', 'mov alh,mc1')
        if k < 5:
            lo.put('x', 'mov mc2,p').put('x', 'mov mc2,x')         # the next longword
        ins.append(lo)
    return ins


def rows():
    """meshlight3.dsp's rows over MD1's group: per vertex four rows of four
    products, each row's sum >> 16 to MD3 in turn. A product's factors load
    on instruction t, it is taken on t + 1 and added on t + 2."""
    slots = []
    for p in range(POINTS):
        for r in range(4):
            for j in range(4):
                d1 = 'mov #1,rx' if j == 0 else None
                if j == 3 and r < 2:
                    d1 = f'mov #{6 * p},ct1'                # its x y z again
                elif j == 3 and r == 3:
                    d1 = 'mov #0,ct0'                       # the coefficients again
                slots.append((j, 'mc1' if j else None, d1))
    ins = [Ins() for _ in range(len(slots) + 2)]
    for t, (j, x, d1) in enumerate(slots):
        if x:
            ins[t].put('x', f'mov {x},x')
        ins[t].put('y', 'mov mc0,y')
        if d1 and t:
            ins[t].put('d1', d1)
        ins[t + 1].x.insert(0, 'mov mul,p')
        ins[t + 1].y.insert(0, 'clr a' if j == 0 else 'mov alu,a')
        ins[t + 2].put('alu', 'ad2')
        if j == 3:
            ins[t + 2].put('d1', 'mov alh,mc3')
    # the first 1 from MD2 10, D1 taking the group's first places
    ins[0].put('x', 'mov m2,x').put('d1', 'mov #0,ct1')
    ins[1].put('d1', 'mov #16,ct2')                 # the output to send
    ins[2].put('d1', 'mov #0,ct3')                  # the sums
    ins[IN_AT].put('d1', 'mov mc3,ct2')             # the job's place in MD2, from the gap
    ins[-1].put('y', 'mov mc1,y')                   # RY = 65536 (MD1 24), for the packing on
    return ins


def packing():
    """meshlight3.dsp's packing, MD3's sums to MD2 16..23: per vertex two
    masks and two sums of x >> 14 times 65536 with y's half, then of z's with
    t & 0x1F00."""
    ins = [Ins().put('d1', 'mov #0,ct3'),
           Ins().put('x', 'mov mc3,x').put('d1', 'mov #16,ct2')]
    for k in range(POINTS):
        a = Ins().put('x', 'mov mc1,p').put('y', 'mov mc3,a')
        if k:
            a.put('alu', 'or').put('d1', 'mov all,mc2')
        b = Ins().put('alu', 'and').put('x', 'mov mul,p').put('x', 'mov mc3,x').put('y', 'mov alu,a')
        if k == 1:
            b.put('d1', f'mov #{GAP + 2},ct3')      # past the gap
        c = Ins().put('alu', 'or').put('x', 'mov mc1,p').put('y', 'mov mc3,a').put('d1', 'mov all,mc2')
        d = Ins().put('alu', 'and').put('x', 'mov mul,p').put('y', 'mov alu,a')
        if k < POINTS - 1:
            d.put('x', 'mov mc3,x').put('d1', 'mov #25,ct1')
        else:
            d.put('d1', f'mov #{GAP},ct3')          # for the place in MD2 (CT1 left at 27, the mask)
        ins += [a, b, c, d]
    ins.append(Ins().put('alu', 'or').put('d1', 'mov all,mc2'))
    return ins


HEADER = [line and '; ' + line for line in __doc__.split('\n')[:-1]]

JOBS = f"""
        ; a job: its record into MD0, its first group into MD2, on to its
        ; groups (TOP) with LOP their count less 1; none, the list's end
job:    mov #20,ct3
        mov m3,ra0
        mov m3,a | mov #21,pl
        add | mov all,mc3               ; the job after
        mov #0,ct0
        dma d0,mc0,21
jw:     jmp jw,t0
        nop
        mov #16,ct0
        mov mc0,ra0                     ; its groups
        mov m0,a | mov #0,pl
        or | mov mc0,lop
        jmp done,z
        mov mc0,top                     ; where a group begins
        mov #{GAP},ct3
        mov mc0,mc3                     ; where it lands in MD2
        mov mc0,mc3                     ; its longwords
        mov #{GAP},ct3
        mov mc3,ct2
        dma d0,mc2,mc3                  ; the first group
gw:     jmp gw,t0
        mov #0,ct0                      ; the coefficients
        mov #{GAP},ct3
        btm
        mov m3,ct2

        ; the last output sent
done:   mov #16,ct2
        dma mc2,d0,8
ew:     jmp ew,t0
        nop
        endi                            ; the SCU's DSP end interrupt

        ; the outputs from where the SH-2 says; RY 65536 from here on; the
        ; first job
start:  mov #21,ct3
        mov m3,wa0
        mov #24,ct1
        mov mc1,y
        jmp job
        mov #27,ct1
"""


def main():
    lines = HEADER + ['', 'normals:']
    lines += [n.text() for n in normals()]
    lines += ['coords:']
    lines += [n.text() for n in coordinates()]
    for i, n in enumerate(rows()):
        lines.append(n.text())
        if i == OUT_AT:
            lines.append('        dma mc2,d0,8                    ; the previous group\'s output')
        elif i == IN_AT:
            lines.append('        dma d0,mc2,mc3                  ; the next group')
    lines += [n.text() for n in packing()]
    lines += ['        btm                             ; the next group',
              '        mov m3,ct2                      ; where it begins in MD2']
    lines += JOBS.split('\n')
    with open(sys.argv[1], 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
