"""The DSP rotation body the streaming programs share (gen_stream.py,
gen_serial.py): 8 points rotated by a 3x4 matrix, a pass a row, wholly
unrolled, the multiplier taking a product and the ALU adding one every
instruction.

A point's row is four products: the translation first (by 1, X set by an
immediate on the D1 bus, as Sonic R does), then x, y and z. The rows lie in
MD0 from 4 as t m0 m1 m2, so a row's coefficients are read in order and the
next row's follow; a point's last load takes the counter back to its row's
first. Each row's sum >> 16 goes to MD3 as the next point's second load is
made, and A is cleared then for that point's first sum.
"""

POINTS = 8
COEFFS = 4      # MD0 index of the first row


def rotate_body(bank, out_base, insert_after=None, insert=()):
    """The body's lines for points in data RAM bank (1 or 2) from 0, results
    to MD3 from out_base (x'0..x'7 y'0..y'7 z'0..z'7), with ct0 at COEFFS and
    the bank's counter at 0 on entry. The lines of insert go in after load
    number insert_after (jumps and DMA: the pipeline's registers hold)."""
    mc = f'mc{bank}'
    ct = f'ct{bank}'
    slots = []
    for row in range(3):
        base = COEFFS + row * 4
        for k in range(POINTS):
            last = k == POINTS - 1
            slots.append({'x': None, 'd1': 'mov #1,rx'})
            slots.append({'x': mc, 'd1': None})
            slots.append({'x': mc, 'd1': None})
            if not last:
                d1 = f'mov #{base},ct0'
            elif row < 2:
                d1 = f'mov #0,{ct}'     # the points again; ct0 is at the next row
            else:
                d1 = None
            slots.append({'x': mc, 'd1': d1, 'end': True})
    count = len(slots) + 2
    ins = [{'alu': None, 'x': [], 'y': [], 'd1': None} for _ in range(count)]

    def d1(i, op):
        assert ins[i]['d1'] is None, (i, op, ins[i]['d1'])
        ins[i]['d1'] = op

    for t, s in enumerate(slots):
        if s['x']:
            ins[t]['x'].append(f"mov {s['x']},x")
        ins[t]['y'].append('mov mc0,y')
        if s['d1']:
            d1(t, s['d1'])
        ins[t + 1]['x'].insert(0, 'mov mul,p')
        acc = ins[t + 2]
        acc['alu'] = 'ad2'
        if s.get('end'):
            acc['y'].insert(0, 'clr a')
            d1(t + 2, 'mov alh,mc3')
        else:
            acc['y'].insert(0, 'mov alu,a')
    ins[1]['y'].insert(0, 'clr a')      # the first point's first sum from 0
    d1(1, f'mov #{out_base},ct3')       # before the first store
    lines = []
    for i, n in enumerate(ins):
        ops = ([n['alu']] if n['alu'] else []) + n['x'] + n['y'] + ([n['d1']] if n['d1'] else [])
        lines.append('        ' + (' | '.join(ops) if ops else 'nop'))
        if insert_after is not None and i == insert_after:
            lines += insert
    return lines
