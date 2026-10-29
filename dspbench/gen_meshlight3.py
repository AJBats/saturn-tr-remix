"""Writes meshlight3.dsp: page 5's meshes rotated and lit on the SCU DSP the
way Sonic R's program rotates its points, the ALU adding a product on every
instruction, with the lighting and the output's packing as dense.

usage: gen_meshlight3.py OUT.dsp

What comes out of a mesh is meshlight2.dsp's: each vertex
    x >> 14 << 16 | y >> 14 & 0xFFFF,  z >> 14 << 16 | t & 0x1F00
with t = ambient - ((n . l) * range >> 14). What changed:
  - the mesh's data comes laid out for the DSP (page_meshes.cpp
    buildDspData): groups of 4 vertices, x y z nx ny nz a longword each,
    so nothing is unpacked
  - the SH-2 writes each job's coefficients ready to use (4 m and 4 t for
    the rows, l * range * -4 and ambient << 16 | 0xFFFF for the light), so
    a job's setup is a copy
  - a group in two passes. The rows: per vertex four rows (x, y, z, the
    light) of four products, the translation (or the ambient) times 1
    first, as Sonic R takes its translation; a product taken and one added
    every instruction. The packing: per vertex two masks and two sums of
    x >> 14 times 65536 with y's half, an ALU operation every instruction
  - two banks of groups taking turns: while one group is worked, the other
    bank's output goes and the next group comes into it
  - no clamp of t: the SH-2 gives the DSP a mesh only when its light can't
    leave 0 .. 0x1FFF (t lies within ambient -+ range; TR1's lights keep
    it there, page 5's too)

The list (page_meshes.cpp MeshJob3), each job 18 longwords:
    0 its vertices' address / 4   1 its groups - 1 (-1 ends the list)
    2..17 its coefficients, as MD0 0..15 hold them
The list's end must still name vertices in high work RAM: the DSP fetches a
group past a job's last before it knows the list has ended (from address 0
the Saturn's transfer never ends; Mednafen reads zeros). The outputs follow
each other from where the SH-2 sets MD0 23, whole groups of 4 vertices
whatever the mesh's count.

Data RAM (* set by the SH-2: page_meshes.cpp meshLight3Load, start3):
  MD0  0..15  the job's coefficients, a row each in the order read:
              4t 4m 4m 4m (x), (y), (z), ambient' l' l' l' (the light)
       16 *65536  17 *0xFFFF  18 *0x1F00  19 the job's groups after the
       one being worked, - 1  20 *the record after the staged one / 4
       21 *where in MD3 the staged record is (20 or 40)  22 the job's
       vertices / 4 + 24  23 *the first output / 4  24 *the first job / 4
  MD1, MD2  0..23 a group's input, x y z nx ny nz a vertex; then its output
  MD3  0..15 the rows' sums >> 16, x y z t a vertex  16 *1
       20..37, 40..57 the next job's record, the places taking turns
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from dspasm import general  # noqa: E402

POINTS = 4          # vertices a group
DMA_AT = 34         # the rows' instruction the next group's transfer follows


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

    def text(self, labels):
        ops = ([self.alu] if self.alu else []) + self.x + self.y + ([self.d1] if self.d1 else [])
        line = ' | '.join(ops) if ops else 'nop'
        general(line.split('|'), 0, labels)     # the pairing rules: raises if broken
        return '        ' + line


def rows(bank):
    """The rows over the group in MD{bank}, a vertex after another: four
    rows of four products, each row's sum >> 16 to MD3 in turn. A product's
    factors load on instruction t, it is taken on t + 1 and added on t + 2."""
    mc = f'mc{bank}'
    slots = []
    for p in range(POINTS):
        for r in range(4):
            for j in range(4):
                d1 = 'mov #1,rx' if j == 0 else None
                if j == 3 and r < 2:
                    d1 = f'mov #{6 * p},ct{bank}'       # its x y z again
                elif j == 3 and r == 3:
                    d1 = 'mov #0,ct0'                   # the coefficients again
                slots.append((j, mc if j else None, d1))
    ins = [Ins() for _ in range(len(slots) + 2)]
    for t, (j, x, d1) in enumerate(slots):
        if x:
            ins[t].put('x', f'mov {x},x')
        ins[t].put('y', 'mov mc0,y')
        if d1:
            ins[t].put('d1', d1)
        ins[t + 1].x.insert(0, 'mov mul,p')
        ins[t + 1].y.insert(0, 'clr a' if j == 0 else 'mov alu,a')
        ins[t + 2].put('alu', 'ad2')
        if j == 3:
            ins[t + 2].put('d1', 'mov alh,mc3')
    return ins


def half(bank, other, labels):
    """A group in MD{bank}: rows, packing, its output sent; meanwhile the
    other bank takes the next group. Then the job's count."""
    mc = f'mc{bank}'
    ins = rows(bank)
    ins[DMA_AT].put('d1', f'mov #0,ct{other}')     # after its output has gone
    ins[-2].put('d1', 'mov #16,ct0')
    ins[-1].put('y', 'mov mc0,y')                  # RY = 65536 for the packing
    lines = [f'half{bank}:']
    for i, n in enumerate(ins):
        lines.append(n.text(labels))
        if i == DMA_AT:
            lines.append(f'        dma d0,mc{other},24            ; the next group')
    # the packing, a vertex in 4: y & 0xFFFF, x * 65536 | that, t & 0x1F00,
    # z * 65536 | that; x >> 16 taken a vertex early
    post = [Ins().put('d1', 'mov #0,ct3'),
            Ins().put('x', 'mov mc3,x').put('d1', f'mov #0,ct{bank}')]
    for k in range(POINTS):
        a = Ins()
        if k:
            a.put('alu', 'or').put('d1', f'mov all,{mc}')
        a.put('x', 'mov mc0,p').put('y', 'mov mc3,a')
        b = Ins().put('alu', 'and').put('y', 'mov alu,a')
        b.x += ['mov mul,p', 'mov mc3,x']
        if k == 0:
            b.put('d1', f'mov #half{other},top')           # where the count's jumps go
        elif k == POINTS - 1:
            b.put('d1', f'mov #0,ct{other}')               # the next group from its start
        c = Ins().put('alu', 'or').put('d1', f'mov all,{mc}')
        c.put('x', 'mov mc0,p').put('y', 'mov mc3,a')
        d = Ins().put('alu', 'and').put('y', 'mov alu,a')
        d.x.append('mov mul,p')
        if k < POINTS - 1:
            d.x.append('mov mc3,x')
            d.put('d1', 'mov #17,ct0')
        else:
            d.put('d1', 'mov #19,ct0')
        post += [a, b, c, d]
    e = Ins().put('alu', 'or').put('d1', f'mov all,{mc}')
    e.put('x', 'mov m3,p').put('y', 'mov m0,a')                 # 1; the groups left
    post.append(e)
    lines += [n.text(labels) for n in post]
    lines += ['        sub | mov all,mc0',
              f'        mov #0,ct{bank}',
              f'        dma {mc},d0,8              ; the output',
              '        jmp newjob,s                    ; the job\'s last group',
              '        mov #0,ct0',
              '        jmp setnext,z                   ; the next is its last',
              '        mov #0,ct3']
    if bank == 2:
        lines += ['        jmp half1',
                  '        nop']
    return lines


HEADER = [line and '; ' + line for line in __doc__.split('\n')[:-1]]

START = """
        ; the first job's record, then its first group into MD1
start:  mov #23,ct0
        mov mc0,wa0
        mov m0,ra0
        mov #20,ct3
        dma d0,mc3,18
        mov #half1,top
sw:     jmp sw,t0
        nop
        mov #20,ct3
        mov m3,ra0
        mov #0,ct1
        dma d0,mc1,24
        jmp newjob
        nop
"""

JOBS = """
        ; a job begins: its record staged in MD3, its first group in the
        ; bank the next group takes; its coefficients copied, the record
        ; after its fetched, on to the next group (TOP)
newjob: mov #21,ct0
        mov m0,a | mov m0,ct3
        mov #60,pl
        xor | mov all,mc0               ; the other place, for the record after
        mov mc3,a | mov #24,pl
        add | mov all,mc0               ; its second group's address
        mov mc3,a | mov #0,pl
        or | mov #19,ct0
        jmp finish,s                    ; -1: the list's end
        mov all,mc0                     ; its groups - 1
        mov #0,ct0
        mvi 15,lop
        lps
        mov mc3,mc0                     ; its coefficients
        mvi 4095,lop                    ; for the BTMs to TOP
        mov #20,ct0
        mov m0,ra0 | mov m0,a
        mov #18,pl
        add | mov all,mc0
        mov m0,ct3
        dma d0,mc3,18                   ; the record after
        mov #19,ct0
        mov m0,a | mov #0,pl
        or | mov #22,ct0
        jmp onegroup,z                  ; one group: the next is the job after's
        nop
njw:    jmp njw,t0
        nop
        mov m0,ra0                      ; its second group, after the record
        mov #0,ct0
        mov #0,ct1
        btm
        mov #0,ct3

        ; a job of one group: the group after it is the next job's first
onegroup:
ogw:    jmp ogw,t0
        mov #21,ct0
        mov m0,ct3
        mov m3,ra0
        mov #0,ct0
        mov #0,ct1
        btm
        mov #0,ct3

        ; the next group is the job's last: the group after it, the next
        ; job's first
setnext:
        mov #21,ct0
        mov m0,ct3
        mov m3,ra0
        mov #0,ct3
        btm
        mov #0,ct0

        ; the list's end: the last output sent
finish:
fw:     jmp fw,t0
        nop
        end
"""


def main():
    labels = {'half1': 0, 'half2': 0}       # the TOP moves' 8-bit immediates
    body = half(1, 2, labels) + half(2, 1, labels)
    lines = HEADER + START.split('\n') + body + JOBS.split('\n')
    with open(sys.argv[1], 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
