"""How VDP1 builds a frame, command by command: what each draws, what of it is still there at the end, and who
asked for it. For a study of overdraw and where it comes from.

    STEPWISE=1 BUILD=build/step tools/hwtest.sh SPOT
    python -I tools/stepwise.py capture NAME "TITLE" [--disc build/step] [--frame 2300] [--hold KEYS:FRAMES] [--freeze] [--boxes off]
    python -I tools/stepwise.py boxes OFF_NAME ON_NAME      what the solid boxes cut: into OFF_NAME's frame
    python -I tools/stepwise.py page OUT.html NAME ...

capture boots the disc in a private Mednafen, pages the overlay away, runs to the frame and takes the list VDP1 is
about to draw, with the marks a STEPWISE build keeps of who made which commands (renderer/faces.h). It then draws
the list here one command at a time with the Mednafen fork's own VDP1 code (tools/crunch/vdp1lib), so pixels are
exact; times are Mednafen's, which read 12-25% above the Saturn at today's scenes. build/stepwise/NAME/ holds the
result: frame.json (commands and owners), writes.bin (what each command wrote). page puts frames into one HTML
page from tools/stepwise.html. Its VDP1 library is its own, build/stepwise/vdp1lib.dll:
    wsl -d Ubuntu -- sh tools/crunch/build_vdp1lib.sh build/stepwise/vdp1lib.dll
"""
import argparse
import base64
import bisect
import ctypes
import json
import os
import re
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'crunch'))
from medclient import Mednafen, fwd     # noqa: E402
from vdp1 import Vdp1                   # noqa: E402
import trsat                            # noqa: E402

NM = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-nm.exe'
OUT = ROOT + '/build/stepwise'
W, H = 352, 224
MS = 1000 / 28636360
STEP_MARKS = 512
COMMAND_BYTES = 32
FACE_ROOM_BYTES = 112   # renderer/faces.h: clip at 76, room at 106
LEVELS = {'LVL_TR1_7A': 'LEVEL7A', 'LVL_TR1_3A': 'LEVEL3A', 'LVL_TR1_1': 'LEVEL1', 'LVL_TR1_2': 'LEVEL2'}
DATA = ROOT + '/external_resources/disc/files/DATA'
DRAW_TYPES = {}


def symbols(elf):
    syms = {}
    for line in subprocess.run([NM, '-C', elf], capture_output=True, text=True).stdout.splitlines():
        p = line.split(None, 2)
        if len(p) == 3:
            syms.setdefault(p[2], int(p[0], 16))
    return syms


def symbol(syms, name):
    return next(a for s, a in syms.items() if s == name or s.startswith(name + '('))


def capture(name, title, disc, frame, hold, level, boxes='on', freeze=False):
    out = f'{OUT}/{name}'
    os.makedirs(out, exist_ok=True)
    elf = f'{ROOT}/{disc}/0MAIN.elf'
    syms = symbols(elf)
    tmp = f'{out}/tmp.bin'
    with Mednafen(f'{ROOT}/{disc}/disc/tomb.cue', f'st{name}') as m:
        def dump(addr, size):
            m.send(f'dump_mem_bin {addr:08x} {size:x} {fwd(tmp)}', 'dump_mem_bin', 30)
            return open(tmp, 'rb').read()

        m.send('deterministic', 'deterministic', 10)
        m.send('run_to_frame 1800', 'done run_to_frame', 900)
        for _ in range(4):
            m.send('input R', 'ok input', 10)
            m.send('frame_advance 10', 'done frame_advance', 30)
            m.send('input_release R', 'ok input_release', 10)
            m.send('frame_advance 10', 'done frame_advance', 30)
        if hold:
            keys, frames = hold.split(':')
            # frozen, the held look's camera stays (debug.cpp) well before the frame: it's the same with the boxes off
            until = frame - 120 if freeze else frame
            m.send(f'run_to_frame {until - int(frames)}', 'done run_to_frame', 900)
            keys = keys.split(',')
            m.send(f'input {keys[0]}', 'ok input', 10)
            if len(keys) > 1:
                m.send('frame_advance 8', 'done frame_advance', 30)     # the look's key before its direction
            for k in keys[1:]:
                m.send(f'input {k}', 'ok input', 10)
            if freeze:
                m.send(f'run_to_frame {until}', 'done run_to_frame', 900)
                m.send('input L', 'ok input', 10)
                m.send('frame_advance 10', 'done frame_advance', 30)
                m.send('input_release L', 'ok input_release', 10)
                for k in reversed(keys):
                    m.send(f'input_release {k}', 'ok input_release', 10)
        if boxes == 'off':
            m.send(f'run_to_frame {frame - 60}', 'done run_to_frame', 900)
            m.send(f'poke {syms["render::gBoxesOn"]:08x} 00', 'poke', 10)
        m.send(f'run_to_frame {frame}', 'done run_to_frame', 900)
        m.send(f'breakpoint {symbol(syms, "plotStart"):08X}', 'ok breakpoint', 10)
        m.send('run', 'break pc=', 120)
        vram = dump(0x25c00000, 0x80000)
        marks = dump(syms['render::gStepMarks'], STEP_MARKS * 8 * 2)
        uploaded = struct.unpack('>II', dump(syms['render::gStepUploaded'], 8))
        store_at = struct.unpack('>I', dump(syms['gMeshStore'], 4))[0]
        header = struct.unpack('>8I', dump(store_at, 32))
        mesh_count = header[1]
        counts = struct.unpack('>2i', dump(syms['render::gBoxCounts'], 8))
        seen = dump(syms['render::gBoxes'], 2 * 6 * 40)
        rects = []
        for k in range(2):
            rects.append([struct.unpack_from('>4hi', seen, (6 * k + i) * 40) for i in range(counts[k])])
        index = struct.unpack(f'>{mesh_count}H', dump(store_at + header[4], mesh_count * 2))
        rooms_at = struct.unpack('>I', dump(syms['render::gFrameRooms'], 4))[0]
        room_count = struct.unpack('>I', dump(syms['render::gFrameRoomCount'], 4))[0]
        jobs = dump(rooms_at, FACE_ROOM_BYTES * room_count) if room_count else b''
        windows = [[struct.unpack_from('>H', jobs, FACE_ROOM_BYTES * i + 106)[0], *struct.unpack_from('>4i', jobs, FACE_ROOM_BYTES * i + 76)]
                   for i in range(room_count)]
        m.send('breakpoint_clear', 'breakpoint', 10)
        shot = f'{out}/shot.png'
        if os.path.exists(shot):
            os.remove(shot)
        m.send(f'screenshot {fwd(shot)}', 'screenshot', 30)
    cmds_at = syms['render::gFrameCmds']
    lists = []
    for k in (0, 1):
        at = marks[k * STEP_MARKS * 8:(k + 1) * STEP_MARKS * 8]
        got = []
        slot_above = 1 << 30
        for j in range(uploaded[k] - 1, max(uploaded[k] - STEP_MARKS, -1), -1):
            cmd, who = struct.unpack_from('>II', at, (j % STEP_MARKS) * 8)
            slot = ((cmd & 0x07FFFFFF) - (cmds_at & 0x07FFFFFF)) // COMMAND_BYTES
            if slot > slot_above or slot < 0:
                break
            got.append((slot, who))
            slot_above = slot
        lists.append(got[::-1])
    open(f'{out}/vram.bin', 'wb').write(vram)
    json.dump({'title': title, 'lists': lists, 'index': index, 'level': level, 'frame': frame, 'rects': rects, 'windows': windows},
              open(f'{out}/capture.json', 'w'))
    os.remove(tmp)
    print(f'{name}: {len(lists[0])} rooms, {len(lists[1])} draws marked', flush=True)
    analyze(name)


def item_names():
    s = open(ROOT + '/openlara/common.h', encoding='latin-1').read()
    body = s[s.index('#define ITEM_TYPES(E)'):]
    body = body[:body.index('\n\n')]
    return re.findall(r'E\(\s*(\w+)\s*\)', body)


def owners_of(cap):
    """A function from a command's slot to who made it, and a mesh store record's name."""
    lvl = trsat.load(DATA, cap['level'])
    names = item_names()
    by_record = {}
    index = cap['index']
    for m in lvl.models:
        name = names[m['type']] if m['type'] < len(names) else f'model {m["type"]}'
        for mesh in range(m['start'], m['start'] + m['count']):
            if mesh < len(index):
                by_record.setdefault(index[mesh], name)
    for s in lvl.static_meshes:
        mesh = s['mesh']
        if mesh < len(index):
            by_record.setdefault(index[mesh], f'static {s["id"]}')
    rooms, draws = cap['lists']
    room_slots = [r[0] for r in rooms]
    draw_slots = [d[0] for d in draws]

    def owner(slot):
        if slot == 0:
            return 'the frame', 'frame'
        if draw_slots and slot >= draw_slots[0]:
            who = draws[bisect.bisect_right(draw_slots, slot) - 1][1]
            kind = who >> 24
            if kind == 3:
                return 'fills past the walk', 'fill'
            mesh = who & 0xFFFF
            dtype = (who >> 16) & 0xFF
            name = by_record.get(mesh)
            if name is None:
                return f'draw type {dtype}', 'sprite'
            if name.startswith('static'):
                return name, 'static'
            if name.startswith('LARA'):
                return 'LARA', 'lara'
            return name, 'creature' if name.split()[0] in CREATURES else 'object'
        if room_slots and slot >= room_slots[0]:
            who = rooms[bisect.bisect_right(room_slots, slot) - 1][1]
            return f'room {who & 0xFFFF}', 'room'
        return 'the frame', 'frame'

    return owner


CREATURES = {'WOLF', 'BEAR', 'BAT', 'CROCODILE_LAND', 'CROCODILE_WATER', 'LION_MALE', 'LION_FEMALE', 'PUMA', 'GORILLA', 'RAT_LAND', 'RAT_WATER',
             'REX', 'TREX', 'RAPTOR', 'MUTANT_1', 'MUTANT_2', 'MUTANT_3', 'CENTAUR', 'MUMMY', 'LARSON', 'PIERRE', 'SKATEBOARD', 'SKATER', 'COWBOY', 'MR_T',
             'NATLA', 'GIANT_MUTANT', 'DOPPELGANGER'}


def final_frame(commands, runs, run_at, colours, drop, box):
    """What the frame ends as, a colour per pixel and a mask of those written, with the commands in drop left out
    and a rectangle of colour 0 under the rest."""
    col = np.zeros((H, W), np.uint16)
    written = np.zeros((H, W), bool)
    if box:
        written[box[1]:box[3] + 1, box[0]:box[2] + 1] = True
    at = 0
    for i, c in enumerate(commands):
        n = c['written']
        if i not in drop:
            k = at
            for r in range(run_at[i], run_at[i + 1]):
                y, x, m = runs[r * 3], runs[r * 3 + 1], runs[r * 3 + 2]
                col[y, x:x + m] = colours[k:k + m]
                written[y, x:x + m] = True
                k += m
        at += n
    # 0x8000 and colour 0 both show black
    return np.where(col & 0x8000, col & 0x7FFF, 0), written


def black_lists(v, commands, runs, run_at, colours):
    """Two lists to set against today's. 'run': the black commands before the first with colour as one fill behind
    everything. 'all': every black command so but Lara's and the objects', whose black is their own (her shadow, her
    dark parts, a bridge's underside) and not the fog's; one over something bright would let it show, which
    final_frame's difference finds. The fill's box is round the pixels they write; its time, VDP1's own for it."""
    black = []
    at = 0
    for c in commands:
        n = c['written']
        black.append(n > 0 and not np.any(colours[at:at + n] & 0x7FFF))
        at += n
    first_lit = next((i for i, c in enumerate(commands) if c['written'] and not black[i]), len(commands))
    out = {}
    now, now_written = final_frame(commands, runs, run_at, colours, set(), None)
    for mode, drop in (('run', [i for i in range(first_lit) if black[i]]),
                       ('all', [i for i, b in enumerate(black) if b and commands[i]['cat'] not in ('lara', 'object')])):
        box = None
        if drop:
            spans = [(runs[r * 3], runs[r * 3 + 1], runs[r * 3 + 2]) for i in drop for r in range(run_at[i], run_at[i + 1])]
            box = [min(s[1] for s in spans), min(s[0] for s in spans), max(s[1] + s[2] for s in spans) - 1, max(s[0] for s in spans)]
        cycles = 0
        if box:
            at = 0x3FFF0
            keep = v.vram[at:at + 0x10].copy()
            v.vram[at:at + 0x10] = [4, 0, 0xC0, 0, 0, 0, box[0], box[1], box[2], box[1], box[2], box[3], box[0], box[3], 0, 0]
            cycles = v.step(at, ctypes.c_int32(-1))[1].cycles
            v.vram[at:at + 0x10] = keep
        col, written = final_frame(commands, runs, run_at, colours, set(drop), box)
        out[mode] = {'drop': drop, 'box': box, 'cycles': int(cycles), 'differ': int(((col != now) & now_written).sum()),
                     'never': int((~written).sum())}
    return out


def analyze(name):
    out = f'{OUT}/{name}'
    cap = json.load(open(f'{out}/capture.json'))
    owner_of = owners_of(cap)
    v = Vdp1(dll=OUT + '/vdp1lib.dll')
    v.load_vram(open(f'{out}/vram.bin', 'rb').read())
    v.reset()
    fb = v.fb
    S1, S2 = 0xFC1F, 0x83E0
    fb[:] = 0
    ret = ctypes.c_int32(-1)
    addr = 0
    commands = []
    owners = {}
    runs, run_at, colors = [], [0], []
    last = -np.ones((H, W), np.int32)
    count = np.zeros((H, W), np.int32)
    guard = 0
    while addr >= 0 and guard < 0x10000:
        guard += 1
        word = int(v.vram[addr])
        if word & 0x8000:
            break
        kind = word & 0xF
        before = fb.copy()
        mask = None
        if not word & 0x4000 and kind < 8:
            for sentinel in (S1, S2):
                fb[:] = sentinel
                v.step(addr, ctypes.c_int32(ret.value))
                got = fb[:H, :W] != sentinel
                mask = got if mask is None else mask | got
            fb[:] = before
        nxt, cost = v.step(addr, ret)
        at = addr
        addr = nxt
        if word & 0x4000 or kind >= 8:
            continue
        slot = (cost.addr * 8 - VRAM_FRAME_CMDS) // COMMAND_BYTES
        label, category = owner_of(slot)
        n = len(commands)
        written = 0
        ys, xs = np.nonzero(mask)
        if len(ys):
            written = len(ys)
            last[ys, xs] = n
            count[ys, xs] += 1
            colors.append(fb[:H, :W][ys, xs].astype('<u2'))
            # spans along each row, from nonzero's row order
            start = 0
            for i in range(1, len(ys) + 1):
                if i == len(ys) or ys[i] != ys[start] or xs[i] != xs[i - 1] + 1:
                    runs.extend((int(ys[start]), int(xs[start]), i - start))
                    start = i
        run_at.append(len(runs) // 3)
        c = [int(x) for x in v.vram[at:at + 0x10]]
        corners = [c[6 + i] - 65536 if c[6 + i] & 0x8000 else c[6 + i] for i in range(8)]
        size = c[5]
        commands.append({'owner': owners.setdefault(label, len(owners)), 'cat': category, 'kind': kind, 'cycles': cost.cycles, 'lines': cost.lines,
                         'plotted': cost.pixels, 'texels': cost.texels, 'written': written, 'corners': corners, 'tw': (size >> 8) * 8, 'th': size & 255,
                         'mode': c[2], 'slot': slot})
    seen = np.bincount(last[last >= 0], minlength=len(commands))
    for i, c in enumerate(commands):
        c['seen'] = int(seen[i])
    labels = sorted(owners, key=owners.get)
    table = [dict(name=n, cat=None, commands=0, written=0, seen=0, cycles=0, plotted=0) for n in labels]
    for c in commands:
        t = table[c['owner']]
        t['cat'] = c['cat']
        t['commands'] += 1
        t['written'] += c['written']
        t['seen'] += c['seen']
        t['cycles'] += c['cycles']
        t['plotted'] += c['plotted']
    colours = np.concatenate(colors) if colors else np.zeros(0, '<u2')
    black = black_lists(v, commands, runs, run_at, colours)
    open(f'{out}/writes.bin', 'wb').write(colours.tobytes())
    open(f'{out}/runs.bin', 'wb').write(np.array(runs, '<u2').tobytes())
    open(f'{out}/run_at.bin', 'wb').write(np.array(run_at, '<u4').tobytes())
    total = sum(c['cycles'] for c in commands)
    never = int((last < 0).sum())
    hist = np.bincount(count.ravel(), minlength=9).tolist()
    # the time on what doesn't show: a command's cycles by the share of its pixels gone by the end
    wasted = sum(c['cycles'] * (1 - c['seen'] / c['written']) if c['written'] else c['cycles'] for c in commands)
    gone = sum(c['cycles'] for c in commands if c['written'] and not c['seen'])
    frame = {'title': cap['title'], 'name': name, 'width': W, 'height': H, 'commands': commands, 'owners': table, 'never': never, 'hist': hist,
             'cycles': total, 'wasted': wasted, 'gone': gone, 'ms': MS, 'plottedTotal': sum(c['plotted'] for c in commands), 'black': black}
    json.dump(frame, open(f'{out}/frame.json', 'w'))
    report(frame)


def report(f):
    cmds = f['commands']
    screens = sum(c['written'] for c in cmds) / (W * H)
    print(f'{f["title"]}: {len(cmds)} commands, VDP1 {f["cycles"] * MS:.2f} ms, {sum(c["written"] for c in cmds)} pixels written = {screens:.2f} screens, '
          f'{f["never"]} never written')
    print(f'  time on pixels gone by the end: {f["wasted"] * MS:.2f} ms ({f["wasted"] * 100 / f["cycles"]:.0f}%); commands wholly gone: '
          f'{sum(1 for c in cmds if c["written"] and not c["seen"])} = {f["gone"] * MS:.2f} ms; drawing nothing on the frame: '
          f'{sum(1 for c in cmds if not c["written"])} = {sum(c["cycles"] for c in cmds if not c["written"]) * MS:.2f} ms')
    cats = {}
    for c in cmds:
        s = cats.setdefault(c['cat'], [0, 0, 0, 0])
        s[0] += 1
        s[1] += c['cycles']
        s[2] += c['written']
        s[3] += c['seen']
    for k, s in sorted(cats.items(), key=lambda a: -a[1][1]):
        print(f'  {k:9s} {s[0]:5d} commands {s[1] * MS:7.2f} ms  written {s[2]:7d}  seen {s[3]:6d} ({s[3] * 100 / max(s[2], 1):3.0f}%)')
    for mode, label in (('run', 'black before the first colour'), ('all', 'all black but Lara and objects')):
        b = f['black'][mode]
        gone = sum(cmds[i]['cycles'] for i in b['drop'])
        print(f'  {label}: {len(b["drop"])} commands {gone * MS:.2f} ms -> one fill {b["box"]} {b["cycles"] * MS:.2f} ms; list {f["cycles"] * MS:.2f} -> '
              f'{(f["cycles"] - gone + b["cycles"]) * MS:.2f} ms; {b["differ"]} pixels look different; never written {f["never"]} -> {b["never"]}')
    print('  by owner, dearest:')
    for o in sorted(f['owners'], key=lambda o: -o['cycles'])[:14]:
        print(f'    {o["name"][:24]:24s} {o["commands"]:4d} cmds {o["cycles"] * MS:6.2f} ms  written {o["written"]:6d}  seen {o["seen"]:6d} ({o["seen"] * 100 / max(o["written"], 1):3.0f}%)')
    h = f['hist']
    print('  pixels by times written:', ' '.join(f'{i}x:{h[i] * 100 / (W * H):.0f}%' for i in range(min(9, len(h)))), f'9+x:{sum(h[9:]) * 100 / (W * H):.0f}%' if len(h) > 9 else '')


def boxes(off_name, on_name):
    """The room commands of a frame with the boxes off that the same frame with them on hasn't: what they cut. Every
    one should leave no pixel at the end of the off frame; 'seen' adds up any that do."""
    off = json.load(open(f'{OUT}/{off_name}/frame.json'))
    on = json.load(open(f'{OUT}/{on_name}/frame.json'))
    cap = json.load(open(f'{OUT}/{on_name}/capture.json'))
    rects = max(cap['rects'], key=len)

    def key(f, c):
        return f['owners'][c['owner']]['name'], tuple(sorted(zip(c['corners'][0::2], c['corners'][1::2])))

    left = {}
    for c in on['commands']:
        if c['cat'] == 'room':
            k = key(on, c)
            left[k] = left.get(k, 0) + 1
    drop, why = [], []
    for i, c in enumerate(off['commands']):
        if c['cat'] != 'room':
            continue
        k = key(off, c)
        if left.get(k):
            left[k] -= 1
            continue
        xs, ys = c['corners'][0::2], c['corners'][1::2]
        inside = -1
        for n, r in enumerate(rects):
            if min(xs) >= r[0] and min(ys) >= r[1] and max(xs) <= r[2] and max(ys) <= r[3]:
                inside = n
                break
        drop.append(i)
        why.append(inside)
    cmds = off['commands']
    rooms_off = {off['owners'][c['owner']]['name'] for c in cmds if c['cat'] == 'room'}
    rooms_on = {on['owners'][c['owner']]['name'] for c in on['commands'] if c['cat'] == 'room'}
    hidden = [i for i, c in enumerate(cmds) if c['cat'] == 'room' and c['written'] and not c['seen']]
    out = {'rects': rects, 'drop': drop, 'why': why, 'rooms': sorted(rooms_off - rooms_on), 'extra': sum(left.values()),
           'seen': sum(cmds[i]['seen'] for i in drop), 'cycles': sum(cmds[i]['cycles'] for i in drop),
           'hidden': len(hidden), 'hiddenCycles': sum(cmds[i]['cycles'] for i in hidden),
           'onCycles': on['cycles'], 'onCommands': len(on['commands']), 'offCommands': len(cmds)}
    off['boxes'] = out
    off['black']['boxes'] = {'drop': drop, 'box': None, 'cycles': 0}
    json.dump(off, open(f'{OUT}/{off_name}/frame.json', 'w'))
    on['boxRects'] = rects
    json.dump(on, open(f'{OUT}/{on_name}/frame.json', 'w'))
    print(f'{off["title"]}: {len(rects)} rectangles {[r[:4] for r in rects]}')
    print(f'  cut {len(drop)} of {len(cmds)} commands, {out["cycles"] * MS:.2f} ms ({sum(1 for w in why if w >= 0)} inside a rectangle, {sum(1 for w in why if w < 0)} of rooms skipped through a doorway: {out["rooms"]})')
    print(f'  pixels of them still seen at the end of the off frame: {out["seen"]}; on the on frame but not the off: {out["extra"]} room commands')
    print(f'  list off {off["cycles"] * MS:.2f} ms, on {on["cycles"] * MS:.2f} ms, {len(on["commands"])} commands; room faces wholly hidden in the off frame: {len(hidden)}, {out["hiddenCycles"] * MS:.2f} ms')


def page(out_path, names):
    frames = []
    for name in names:
        d = f'{OUT}/{name}'
        f = json.load(open(f'{d}/frame.json'))
        for key in ('writes', 'runs', 'run_at'):
            f[key] = base64.b64encode(open(f'{d}/{key}.bin', 'rb').read()).decode()
        frames.append(f)
    html = open(ROOT + '/tools/stepwise.html', encoding='utf-8').read()
    html = html.replace('/*FRAMES*/null', json.dumps(frames, separators=(',', ':')))
    if any('boxes' in f for f in frames):
        html = html.replace('<title>VDP1 Stepwise</title>', '<title>Solid Box Stepwise</title>')
        html = html.replace('<h1>VDP1 Stepwise</h1>', '<h1>Solid Box Stepwise</h1>')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as o:
        o.write(html)
    print(out_path, os.path.getsize(out_path) >> 10, 'KB')


def vram_frame_cmds():
    s = open(ROOT + '/renderer/types.h', encoding='latin-1').read()
    m = re.search(r'#define VRAM_FRAME_CMDS\s+(\S+)', s)
    return int(m.group(1), 0)


VRAM_FRAME_CMDS = vram_frame_cmds()


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='what', required=True)
    c = sub.add_parser('capture')
    c.add_argument('name')
    c.add_argument('title')
    c.add_argument('--disc', default='build/step')
    c.add_argument('--frame', type=int, default=2300)
    c.add_argument('--hold', default='')
    c.add_argument('--level', default='LEVEL7A')
    c.add_argument('--boxes', default='on')
    c.add_argument('--freeze', action='store_true')
    b = sub.add_parser('boxes')
    b.add_argument('off')
    b.add_argument('on')
    a = sub.add_parser('analyze')
    a.add_argument('name')
    p = sub.add_parser('page')
    p.add_argument('out')
    p.add_argument('names', nargs='+')
    args = ap.parse_args()
    if args.what == 'capture':
        capture(args.name, args.title, args.disc, args.frame, args.hold, args.level, args.boxes, args.freeze)
    elif args.what == 'boxes':
        boxes(args.off, args.on)
    elif args.what == 'analyze':
        analyze(args.name)
    else:
        page(args.out, args.names)


if __name__ == '__main__':
    main()
