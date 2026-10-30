"""Writes roomrot.dsp: the rooms' vertices rotated on the SCU DSP for stage 2
(renderer/vertices.h), each room vertex's three sums as xform.s makes
them before its divide, for the CPUs to project (roomproject.s).

usage: gen_roomrot.py OUT.dsp

A vertex comes in as the room keeps it, a longword of bytes x y z g, and goes
out as three longwords, the rows' sums: t + m0 * 4x + m1 * 4y + m2 * 4z for
each row (m times 4 in the coefficients), wrapping at 32 bits as the
multiply-accumulate unit does. Groups of 4 vertices, in two passes:
  - the unpacking: each byte rotated to the bottom and masked, into MD3
  - the rows: per vertex three rows of four products, the translation times
    1 first; a product taken and one added every instruction (the bench's
    meshlight3.dsp rows), each row's sum into the worked bank
MD1 and MD2 take turns: while one group is worked, the other's output goes
and the next group comes into it.

The jobs (vertices.cpp RoomDspJob), a room each, 15 longwords:
    0 its vertices' address / 4   1 its output's / 4   2 its groups (0 ends
    the list)   3..14 its coefficients, t m0 m1 m2 a row, as MD0 3..14 hold
A room's last group reads and writes up to 3 vertices past its end: its
output has room for whole groups. Each room done, once its output has gone,
the rooms done so far go to a longword the SH-2 names, for a CPU to follow.

Data RAM (* set by the SH-2):
  MD0  0..14  the job   16 *the next job / 4   17 *the rooms done's longword / 4
       18 *the rooms done (0)
  MD1, MD2  0..3 a group's input, 4..15 its output
  MD3  0..11 a group's x y z, a vertex after another   12 *0xFF
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from dspasm import general  # noqa: E402

POINTS = 4          # vertices a group
ROWS = 3
COEFFICIENTS = 3    # MD0's first
DMA_AT = 14         # the rows' instruction the next group's transfer follows (its D1 free)


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


def unpack(bank):
    """The group's 4 longwords in MD{bank} 0..3 to x y z bytes in MD3 0..11:
    each rotated a byte left and masked by P (0xFF), the next vertex's
    longword loaded as the last byte goes."""
    mc = f'mc{bank}'
    ins = [Ins().put('y', f'mov {mc},a')]
    for p in range(POINTS):
        for k in range(3):
            ins.append(Ins().put('alu', 'rl8').put('y', 'mov alu,a'))
            last = Ins().put('alu', 'and').put('d1', 'mov all,mc3')
            if k == 2 and p < POINTS - 1:
                last.put('y', f'mov {mc},a')
            ins.append(last)
    return ins


def rows(bank):
    """The rows over the group's x y z in MD3, a vertex after another: three
    rows of four products, each row's sum to MD{bank} in turn. A product's
    factors load on instruction t, it is taken on t + 1 and added on t + 2."""
    slots = []
    for p in range(POINTS):
        for r in range(ROWS):
            for j in range(4):
                d1 = 'mov #1,rx' if j == 0 else None
                if j == 3 and r < ROWS - 1:
                    d1 = f'mov #{3 * p},ct3'                    # its x y z again
                elif j == 3:
                    d1 = f'mov #{COEFFICIENTS},ct0'             # the coefficients again
                slots.append((j, 'mc3' if j else None, d1))
    ins = [Ins() for _ in range(len(slots) + 2)]
    for t, (j, x, d1) in enumerate(slots):
        if x:
            ins[t].put('x', f'mov {x},x')
        ins[t].put('y', 'mov mc0,y')
        if d1:
            ins[t].put('d1', d1)
        ins[t + 1].x.insert(0, 'mov mul,p')
        ins[t + 1].y.insert(0, 'clr a' if j == 0 else 'mov alu,a')
        ins[t + 2].put('alu', 'add')
        if j == 3:
            ins[t + 2].put('d1', f'mov all,mc{bank}')
    return ins


def half(bank, other):
    """A group in MD{bank}: unpacked, its rows, its output sent; meanwhile the
    other bank takes the next group. Then the job's count."""
    lines = [f'half{bank}:',
             '        mov #12,ct3',
             '        mov m3,p                        ; 0xFF',
             '        mov #0,ct3',
             f'        mov #0,ct{bank}']
    lines += [n.text() for n in unpack(bank)]
    lines += ['        mov #0,ct3',
              f'        mov #{COEFFICIENTS},ct0']
    ins = rows(bank)
    ins[DMA_AT].put('d1', f'mov #0,ct{other}')
    for i, n in enumerate(ins):
        lines.append(n.text())
        if i == DMA_AT:
            lines.append(f'        dma d0,mc{other},4              ; the next group')
    lines += [f'        mov #4,ct{bank}',
              f'        dma mc{bank},d0,12             ; the output',
              '        mov #2,ct0',
              '        mov m0,a | mov #1,pl',
              '        sub | mov all,mc0               ; the groups left',
              '        jmp jobend,z',
              '        nop',
              f'        jmp half{other}',
              '        nop']
    return lines


HEADER = [line and '; ' + line for line in __doc__.split('\n')[:-1]]

JOBS = """
        ; a job: its record into MD0, its first group into MD1; none, the
        ; list's end
job:    mov #16,ct0
        mov m0,ra0
        mov m0,a | mov #15,pl
        add | mov all,mc0               ; the job after
        mov #0,ct0
        dma d0,mc0,15
jw:     jmp jw,t0
        nop
        mov #0,ct0
        mov mc0,ra0                     ; its vertices
        mov mc0,wa0                     ; its output
        mov m0,a | mov #0,pl
        or
        jmp done,z
        mov #0,ct1
        dma d0,mc1,4                    ; its first group
fw:     jmp fw,t0
        nop
        jmp half1
        nop

        ; a job's last group done: its output sent, the rooms done counted
        ; and sent, the next job
jobend:
ow:     jmp ow,t0
        mov #18,ct0
        mov m0,a | mov #1,pl
        add | mov all,mc0
        mov #17,ct0
        mov mc0,wa0
        dma mc0,d0,1
        jmp job
        nop
done:   endi                            ; the SCU's DSP end interrupt
"""


def main():
    body = half(1, 2) + half(2, 1)
    lines = HEADER + JOBS.split('\n') + body
    with open(sys.argv[1], 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
