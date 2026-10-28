"""The game's frame trace (saturn/trace.h, HW_LOG builds) as a Chrome trace.

    trace.py capture CUE [--frame N] [--keys F:K,K,...] [--fps N] [--shots N] [--playback N] [--out NAME]
        boots CUE in a Mednafen of its own (its own automation and home
        directories, so a Mednafen the MCP server runs is left alone), taps
        the player's pad buttons K from frame F, runs to frame N, dumps the
        trace (build/NAME.bin) and converts it (build/NAME.json). The default
        keys page the HW_LOG overlay to nothing at all, which holds the bench
        on Base (Trace is at Detail unless the menu changes it). --fps N
        first counts the frames the game draws over N vblanks (from the
        frame the keys end) for a frame rate that a trace's few seconds can't
        give: frames take 2 or 3 vblanks, so a short window's rate moves with
        a handful of them. --shots N takes a screenshot at each of the N
        vblanks after too (build/NAME_1.png ...). --playback N then pages on
        to the playback and takes a screenshot every 4 vblanks for N
        (build/NAME_play/), which "video" reads as it would a Saturn's;
        --record N takes them without paging (the battery: keys
        1500:R,R,R,UP,A start it from the menu).
    trace.py convert DUMP [--out JSON]
        converts a dump of low work RAM (1 MB from 0x00200000)
    trace.py video VIDEO [--out JSON]
        reads a video of a Saturn's playback page (HW_LOG overlay: R from
        nothing at all; any recording that shows the whole screen at 30 fps
        or more, the picture anywhere in it), or a directory of its pictures.
        A battery's (menu: A on Battery) gives a trace for each split
        (JSON_c1 ...) and a table of them side by side.
    trace.py open JSON
        opens JSON in ui.perfetto.dev in the default browser

Open the JSON in ui.perfetto.dev or chrome://tracing. Tracks: the master's
stages, its interrupts, the slave's jobs, the SCU DSP's runs (as the slave
sees them start and end), VDP1 drawing each list, and the display (vblanks,
and each list on screen). Gaps are drawn as spans too: "idle" where a CPU is
in no stage, "wait for jobs" where the slave polls for rooms in a batch, so
that time no stage accounts for stands out next to the waits ("WAIT ...").

Also prints each stage's own time a frame.
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LWRAM = 0x00200000
LWRAM_BYTES = 0x100000
HEADER = 0x002FFF00
MAGIC = 0x54524331
LANES = ['master', 'irq', 'slave', 'dsp', 'dsp meshes']
SLAVE_LANES = (2, 3)     # written by the slave, on its own clock
CLOCK = 28636360        # SH-2 cycles a second at the 352-dot clock
TICK = 128              # cycles an FRT tick
FRAME_TICKS = 7457      # a 30 fps frame
END = 0x80

# tid: name, in the order shown
TRACK_NAMES = {1: 'Master SH-2', 2: 'Master interrupts', 3: 'Slave SH-2', 4: 'SCU DSP (rooms, slave)',
               7: "SCU DSP (master's meshes)", 5: 'VDP1', 6: 'Display'}
WAITS = {'TICK_WAIT', 'ROOMS_WAIT', 'FAR_WAIT', 'VDP1_WAIT', 'DSP_WAIT'}


def trace_ids():
    """TraceId names by value, from saturn/trace.h."""
    text = open(os.path.join(ROOT, 'saturn/trace.h')).read()
    body = text[text.index('enum TraceId'):]
    body = body[:body.index('};')]
    ids, value = {}, 0
    for m in re.finditer(r'^\s*TR_(\w+)(?:\s*=\s*(\w+))?\s*,', body, re.M):
        if m.group(2):
            value = int(m.group(2), 0)
        ids[value] = m.group(1)
        value += 1
    first_moment = next(v for v, n in ids.items() if n == 'PLOT')
    return ids, first_moment


def us(ticks):
    return ticks * TICK * 1e6 / CLOCK


def read_lanes(data):
    """Each lane's events, oldest first, as (16-bit master ticks, code, arg),
    and the vblank count at each lane's last."""
    header = HEADER - LWRAM
    magic, offset = struct.unpack_from('>II', data, header)
    if magic != MAGIC:
        sys.exit(f'no trace in the dump (magic {magic:#x}): an HW_LOG build in a level?')
    lanes, vblanks = [], []
    for i in range(len(LANES)):
        ring, size, nxt, vblank = struct.unpack_from('>IIII', data, header + 8 + i * 16)
        vblanks.append(vblank)
        count = min(nxt, size)
        base = ring - LWRAM
        events = []
        for n in range(nxt - count, nxt):
            e, = struct.unpack_from('>I', data, base + (n & (size - 1)) * 4)
            t = e >> 16
            if i in SLAVE_LANES:
                t = (t - offset) & 0xFFFF
            events.append((t, (e >> 8) & 0xFF, e & 0xFF))
        lanes.append(events)
    return lanes, vblanks, offset


def unwrap(lanes, vblanks):
    """Ticks on one timeline: each lane's newest event placed against the
    master's newest, by the vblanks between them (a lane may have stopped
    long before) and then to the tick by its 16-bit time, then walked back by
    the 16-bit differences."""
    ref = lanes[0][-1][0] if lanes[0] else 0
    base = 1 << 24
    out = []
    for events, vblank in zip(lanes, vblanks):
        if not events:
            out.append([])
            continue
        t16 = events[-1][0]
        guess = -int((vblanks[0] - vblank) * FRAME_TICKS / 2)
        d = (t16 - ref - guess) & 0xFFFF
        if d >= 0x8000:
            d -= 0x10000
        times = [base + ref + guess + d]
        for i in range(len(events) - 1, 0, -1):
            times.append(times[-1] - ((events[i][0] - events[i - 1][0]) & 0xFFFF))
        times.reverse()
        out.append([(t, c, a) for t, (_, c, a) in zip(times, events)])
    return out


def span_name(name, arg_begin, arg_end):
    if name == 'OBJECTS':
        return f'objects room {arg_begin}'
    if name == 'ITEM':
        return f'item type {arg_begin}'
    if name in ('XFORM', 'FACES', 'SLAVE_MESH'):
        return f'{name.lower().replace("_", " ")} job {arg_begin}'
    if name == 'DSP_RUN':
        return f'DSP run job {arg_begin}'
    if name == 'MESHES_FINISH':
        return f'meshes finish ({arg_begin})'
    if name == 'DSP_MESHES':
        return f'DSP meshes from slot {arg_begin}'
    if name in WAITS:
        return 'WAIT ' + name[:-5].lower().replace('_', ' ')
    return name.lower().replace('_', ' ')


def pair_spans(events, ids, first_moment):
    """Spans (begin, end, depth, id, arg begin, arg end) and moments (t, id, arg)."""
    spans, moments, stack = [], [], []
    for t, code, arg in events:
        ident = code & ~END
        if ident >= first_moment:
            moments.append((t, ident, arg))
        elif code & END:
            found = next((i for i in range(len(stack) - 1, -1, -1) if stack[i][1] == ident), None)
            if found is None:
                continue
            while len(stack) > found:
                b, bid, barg = stack.pop()
                spans.append((b, t, len(stack), bid, barg, arg if bid == ident else 0))
        else:
            stack.append((t, ident, arg))
    return spans, moments


def gaps(spans, start, end, depth, name, parent=None):
    """Spans of time at depth not covered by spans at that depth (in parent)."""
    lo, hi = (parent[0], parent[1]) if parent else (start, end)
    inside = sorted((s[0], s[1]) for s in spans if s[2] == depth and lo <= s[0] and s[1] <= hi)
    out, t = [], lo
    for b, e in inside:
        if b > t:
            out.append((t, b, name))
        t = max(t, e)
    if hi > t:
        out.append((t, hi, name))
    return out


def convert(data, out_path):
    ids, first_moment = trace_ids()
    lanes, vblanks, offset = read_lanes(data)
    lanes = unwrap(lanes, vblanks)
    if not lanes[0]:
        sys.exit('the master lane is empty')

    # the time every lane covers
    start = max(l[0][0] for l in lanes[:3] if l)
    end = min(l[-1][0] for l in lanes[:3] if l)
    t0 = start

    events = []

    def x(tid, b, e, name, cat, cname=None, args=None):
        b, e = max(b, start), min(e, end)
        if e <= b:
            return
        ev = {'name': name, 'cat': cat, 'ph': 'X', 'pid': 1, 'tid': tid, 'ts': round(us(b - t0), 3),
              'dur': round(us(e - b), 3)}
        if cname:
            ev['cname'] = cname
        if args:
            ev['args'] = args
        events.append(ev)

    def moment(tid, t, name, args=None):
        if start <= t <= end:
            ev = {'name': name, 'ph': 'i', 's': 't', 'pid': 1, 'tid': tid, 'ts': round(us(t - t0), 3)}
            if args:
                ev['args'] = args
            events.append(ev)

    tids = {0: 1, 1: 2, 2: 3, 3: 4, 4: 7}
    paired = [pair_spans(l, ids, first_moment) for l in lanes]
    selfs = {}      # own ticks by id name
    for lane, (spans, moments) in enumerate(paired):
        tid = tids[lane]
        for b, e, depth, ident, a0, a1 in spans:
            name = ids.get(ident, f'id{ident}')
            label = span_name(name, a0, a1)
            cat = 'wait' if name in WAITS else 'work'
            x(tid, b, e, label, cat, 'terrible' if name in WAITS else None,
              {'ticks': e - b, 'pct of 30fps frame': round((e - b) * 100 / FRAME_TICKS, 2)})
            inner = sum(s[1] - s[0] for s in spans if s[2] == depth + 1 and b <= s[0] and s[1] <= e)
            if b >= start and e <= end:
                selfs[name] = selfs.get(name, 0) + (e - b) - inner
        if lane in (0, 2):
            for b, e, name in gaps(spans, start, end, 0, 'idle'):
                x(tid, b, e, name, 'idle', 'grey')
                selfs['(idle)' if lane == 0 else '(slave idle)'] = selfs.get('(idle)' if lane == 0 else '(slave idle)', 0) + (min(e, end) - max(b, start))
        if lane == 2:
            for s in spans:
                if ids.get(s[3]) == 'BATCH':
                    for b, e, name in gaps(spans, start, end, 1, 'wait for jobs', s):
                        x(tid, b, e, name, 'idle', 'bad')

    # VDP1: from each plot to the end of its drawing; the display: vblanks,
    # buffer changes asked for, and each list from its showing to the next
    moments_all = sorted(m for _, ms in paired for m in ms)
    plot = None
    shown = None
    for t, ident, arg in moments_all:
        name = ids[ident]
        if name == 'PLOT':
            plot = (t, arg)
        elif name == 'DRAWN' and plot:
            x(5, plot[0], t, f'VDP1 draws list {plot[1]}', 'vdp1', None,
              {'pct of 30fps frame': round((t - plot[0]) * 100 / FRAME_TICKS, 2)})
            plot = None
        elif name == 'CHANGE':
            moment(6, t, 'change asked')
        elif name == 'SHOWN':
            if shown:
                x(6, shown[0], t, f'list {shown[1]} on screen', 'display')
            shown = (t, arg)
    irq_spans = paired[1][0]
    vin = sorted(s[0] for s in irq_spans if ids.get(s[3]) == 'IRQ_VBLANK_IN')
    vout = sorted(s[0] for s in irq_spans if ids.get(s[3]) == 'IRQ_VBLANK_OUT')
    for b in vin:
        e = next((o for o in vout if o > b), None)
        if e:
            x(6, b, e, 'vblank', 'display', 'grey')

    meta = []
    for order, (tid, name) in enumerate(TRACK_NAMES.items()):
        meta.append({'name': 'thread_name', 'ph': 'M', 'pid': 1, 'tid': tid, 'args': {'name': name}})
        meta.append({'name': 'thread_sort_index', 'ph': 'M', 'pid': 1, 'tid': tid, 'args': {'sort_index': order}})
    meta.append({'name': 'process_name', 'ph': 'M', 'pid': 1, 'args': {'name': 'Saturn'}})
    with open(out_path, 'w') as f:
        json.dump({'traceEvents': meta + events, 'displayTimeUnit': 'ms'}, f)

    # the stages, as the overlay's trace page adds them up: own time, percent
    # of the time
    frames = sum(1 for s in paired[0][0] if ids.get(s[3]) == 'TICK_WAIT' and start <= s[1] <= end)
    window = end - start
    print(f'{out_path}: {us(window) / 1000:.1f} ms, {frames} frames ({frames * 1e6 / us(window):.1f} fps), '
          f'slave clock offset {offset:#06x}')
    print('own time, % of the time (100 = all of it):')
    for name, ticks in sorted(selfs.items(), key=lambda kv: -kv[1]):
        print(f'  {name:16s} {ticks * 100 / window:6.1f}')


# from the bench's table: R past the frame, stages and menu pages to nothing
DEFAULT_KEYS = '1500:R,R,R,R'


def map_symbol(name):
    """A symbol's address from build/0MAIN.map."""
    for line in open(os.path.join(ROOT, 'build/0MAIN.map')):
        parts = line.split()
        if len(parts) == 2 and parts[1] == name and parts[0].startswith('0x'):
            return int(parts[0], 16)
    sys.exit(f'{name} not in build/0MAIN.map')


FRAMES_OFFSET = 24      # FrameStats.frames (system.h)


def capture(cue, frame, keys, name, fps_window=0, shots=0, playback=0, record=0):
    # the binary the MCP server picks (mednafen_bot.py): the calibrated fork
    exe = None
    for root in (ROOT, os.path.join(ROOT, '..', 'TombRaiderSaturn')):
        for rel in ('mednafen_debug.exe', 'src/mednafen.exe', 'mednafen_gcc494.exe'):
            path = os.path.join(root, 'tools/mednafen', rel)
            if not exe and os.path.exists(path):
                exe = path
    if not exe:
        sys.exit('no Mednafen in tools/mednafen (a submodule: a worktree may not have it)')
    print(f'Mednafen: {os.path.normpath(exe)}')
    ipc = os.path.join(ROOT, 'build/trace_ipc')
    home = os.path.join(ROOT, 'build/trace_home')
    os.makedirs(ipc, exist_ok=True)
    if not os.path.exists(os.path.join(home, 'mednafen.cfg')):
        sys.exit(f'{home} needs a mednafen.cfg and firmware/ (copy build/mednafen_home, calibrated)')
    action, ack_file = os.path.join(ipc, 'mednafen_action.txt'), os.path.join(ipc, 'mednafen_ack.txt')
    for f in (action, ack_file):
        if os.path.exists(f):
            os.remove(f)

    def ack():
        try:
            with open(ack_file) as f:
                return f.read().strip()
        except OSError:
            return ''

    seq = [0]

    def send(cmd, expect, timeout=600):
        before = ack()
        seq[0] += 1
        tmp = action + '.tmp'
        with open(tmp, 'w', newline='\n') as f:
            f.write(f'# trace {seq[0]}{"." * (seq[0] % 16)}\n{cmd}\n')
        for attempt in range(50):
            try:
                os.replace(tmp, action)
                break
            except PermissionError:
                time.sleep(0.02 * (attempt + 1))
        deadline = time.time() + timeout
        while time.time() < deadline:
            reply = ack()
            if reply and reply != before and expect in reply:
                return reply
            time.sleep(0.05)
        sys.exit(f'{cmd}: no reply with "{expect}" (last: {ack()})')

    env = dict(os.environ, MEDNAFEN_HOME=home)
    proc = subprocess.Popen([exe, '--sound', '0', '-cd.image_memcache', '1', '--automation', ipc, cue],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    try:
        deadline = time.time() + 60
        while 'ready' not in ack():
            if proc.poll() is not None or time.time() > deadline:
                sys.exit('Mednafen did not start')
            time.sleep(0.2)
        send('deterministic', 'deterministic', 10)
        if keys:
            at, buttons = keys.split(':')
            send(f'run_to_frame {at}', 'done run_to_frame')
            for b in buttons.split(','):
                send(f'input {b}', 'ok input', 10)
                send('frame_advance 10', 'done frame_advance', 30)
                send(f'input_release {b}', 'ok input_release', 10)
                send('frame_advance 10', 'done frame_advance', 30)
        if fps_window:
            def word(address):
                reply = send(f'dump_mem {address:x} 4', 'mem ', 10)
                hexes = [t for t in reply.split()[2:] if '=' not in t]
                return int(''.join(hexes[:4]), 16)

            def counters():
                return word(map_symbol('gVblankCount')), word(map_symbol('gStats') + FRAMES_OFFSET)
            at = int(send('status', 'status', 10).split('frame=')[1].split()[0])
            vb0, fr0 = counters()
            send(f'run_to_frame {at + fps_window}', 'done run_to_frame')
            vb1, fr1 = counters()
            print(f'fps over {vb1 - vb0} vblanks: {(fr1 - fr0) * 60 / (vb1 - vb0):.2f} ({fr1 - fr0} frames)')
            frame = max(frame, at + fps_window + 10)
        send(f'run_to_frame {frame}', 'done run_to_frame')
        dump = os.path.join(ROOT, 'build', name + '.bin')
        part = os.path.join(ipc, 'part.bin')
        # low work RAM (the trace), and high (build/NAME_hwram.bin: stack
        # depth and the like)
        for base, path in ((LWRAM, dump), (0x06000000, os.path.join(ROOT, 'build', name + '_hwram.bin'))):
            with open(path, 'wb') as out:
                for at in range(base, base + LWRAM_BYTES, 0x10000):
                    if os.path.exists(part):
                        os.remove(part)
                    send(f'dump_mem_bin {at:x} 10000 {part.replace(os.sep, "/")}', 'dump_mem_bin', 30)
                    for _ in range(50):
                        if os.path.exists(part) and os.path.getsize(part) == 0x10000:
                            break
                        time.sleep(0.05)
                    with open(part, 'rb') as f:
                        out.write(f.read())
        send('frame_advance 1', 'done frame_advance', 30)
        shot = os.path.join(ROOT, 'build', name + '.png')
        if os.path.exists(shot):
            os.remove(shot)
        print(send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30))
        time.sleep(0.5)
        # the vblanks after, each its own (two builds' pictures can be
        # matched though their frames fall on different vblanks)
        for i in range(1, shots + 1):
            send('frame_advance 1', 'done frame_advance', 30)
            shot = os.path.join(ROOT, 'build', f'{name}_{i}.png')
            send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30)
            time.sleep(0.2)
        if playback or record:
            folder = os.path.join(ROOT, 'build', name + '_play')
            os.makedirs(folder, exist_ok=True)
            for old in os.listdir(folder):
                os.remove(os.path.join(folder, old))
            if playback:
                send('input R', 'ok input', 10)
                send('frame_advance 10', 'done frame_advance', 30)
                send('input_release R', 'ok input_release', 10)
            for i in range(0, playback or record, 4):
                send('frame_advance 4', 'done frame_advance', 30)
                shot = os.path.join(folder, f'{i:05d}.png')
                send(f'screenshot {shot.replace(os.sep, "/")}', 'screenshot', 30)
            time.sleep(0.5)
    finally:
        try:
            send('quit', 'quit', 5)
        except SystemExit:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            print('Mednafen still running after quit', file=sys.stderr)
    return dump


def open_ui(path):
    """Opens the trace in ui.perfetto.dev in the default browser: the UI may
    fetch a trace from 127.0.0.1:9001 (as Perfetto's own open_trace_in_ui
    does), so it is served from there until the UI has it."""
    import http.server
    import webbrowser

    path = os.path.abspath(path)
    name = os.path.basename(path)
    served = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.split('?')[0] != '/' + name:
                self.send_error(404)
                return
            with open(path, 'rb') as f:
                body = f.read()
            self.send_response(200)
            self.send_header('Access-Control-Allow-Origin', 'https://ui.perfetto.dev')
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            served.append(name)

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(('127.0.0.1', 9001), Handler)
    server.timeout = 1
    webbrowser.open(f'https://ui.perfetto.dev/#!/?url=http://127.0.0.1:9001/{name}')
    deadline = time.time() + 120
    while not served and time.time() < deadline:
        server.handle_request()
    server.server_close()
    print(f'{name}: ' + ('opened in Perfetto' if served else 'Perfetto never asked for it (2 minutes)'))


# The playback page (trace.h tracePlayFrame): 26 rows of 42 characters, a
# character in from the 352x224 screen's edges, on the playback's back colour.
PAGE_ROWS, PAGE_COLUMNS = 26, 42
PAGE_CELLS = 5
CHECK_DIGITS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
PAGE_HEADER = re.compile(r'F(\d{3})/(\d{3}) S(\d{7}) L(\d{3}) E(\d{3}) D(\d{3})(?: C(\d{2}))? *$')
OPEN_END = 999      # a span whose end the page couldn't hold


def trace_letters():
    """TRACE_LETTERS from saturn/trace.h: an id's letter at its index."""
    text = open(os.path.join(ROOT, 'saturn/trace.h')).read()
    return re.search(r'#define TRACE_LETTERS\s+"([^"]+)"', text).group(1)


def font_glyphs(chars):
    """saturn/font.cpp's 8 x 8 glyphs of chars, a row of 64 pixels each."""
    import numpy as np
    text = open(os.path.join(ROOT, 'saturn/font.cpp')).read()
    body = text[text.index('gFont8'):]
    rows = re.findall(r'\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){7}0x[0-9A-Fa-f]{2})\s*\}', body)
    glyphs = []
    for c in chars:
        lines = [int(v, 16) for v in rows[ord(c) - 32].split(',')]
        glyphs.append([(lines[y] >> x) & 1 for y in range(8) for x in range(8)])
    return np.array(glyphs, dtype=bool)


def row_checked(text):
    total = sum((i + 1) * ord(c) for i, c in enumerate(text[:PAGE_COLUMNS - 1]))
    return CHECK_DIGITS[total % 36] == text[PAGE_COLUMNS - 1]


def find_page(img):
    """The page's characters' rectangle in a picture (left, top, right,
    bottom), as the black between the bands of back colour round it, or None."""
    import numpy as np
    b, g, r = (img[..., i].astype(np.int16) for i in range(3))
    back = (b > 150) & (r < 100) & (g > 40) & (g < 220) & (b - r > 100)
    if back.sum() < 2000:
        return None
    # the band round the page: the blue blob with the biggest box (a desktop
    # has blue elsewhere)
    import cv2
    count, _, stats, _ = cv2.connectedComponentsWithStats(back.astype(np.uint8), connectivity=8)
    if count < 2:
        return None
    best = max(range(1, count), key=lambda i: stats[i, cv2.CC_STAT_WIDTH] * stats[i, cv2.CC_STAT_HEIGHT])
    x0, y0 = stats[best, cv2.CC_STAT_LEFT], stats[best, cv2.CC_STAT_TOP]
    x1, y1 = x0 + stats[best, cv2.CC_STAT_WIDTH] - 1, y0 + stats[best, cv2.CC_STAT_HEIGHT] - 1

    def run(fractions):
        best, start = (0, 0, 0), None
        for i, f in enumerate(list(fractions) + [1.0]):
            if f < 0.5 and start is None:
                start = i
            elif f >= 0.5 and start is not None:
                if i - start > best[0]:
                    best = (i - start, start, i)
                start = None
        return best[1], best[2]

    h, w = y1 - y0, x1 - x0
    left, right = run(back[y0 + h * 2 // 5:y0 + h * 3 // 5, x0:x1 + 1].mean(axis=0))
    top, bottom = run(back[y0:y1 + 1, x0 + w * 2 // 5:x0 + w * 3 // 5].mean(axis=1))
    if right - left < 168 or bottom - top < 104:
        return None
    return x0 + left, y0 + top, x0 + right, y0 + bottom


class PageReader:
    """The rows of text on a page's picture, matched against the font."""

    def __init__(self):
        import numpy as np
        letters = trace_letters().replace('.', '')
        self.chars = sorted(set(letters + CHECK_DIGITS + 'FSLED/ '))
        self.glyphs = font_glyphs(self.chars).astype(np.float32)
        self.rect = None

        # what each column may hold, the rest barred: digits look too much
        # like some letters to be told apart anywhere
        def bars(allowed):
            return np.array([[0 if c in a else 1000 for c in self.chars] for a in allowed], np.float32)
        digits = '0123456789'
        cell = [letters + ' '] + [digits + ' '] * 6 + [' ']
        body = cell * PAGE_CELLS + [' '] * (PAGE_COLUMNS - 1 - PAGE_CELLS * 8) + [CHECK_DIGITS]
        header = (['F'] + [digits] * 3 + ['/'] + [digits] * 3 + [' ', 'S'] + [digits] * 7 + [' ', 'L'] +
                  [digits] * 3 + [' ', 'E'] + [digits] * 3 + [' ', 'D'] + [digits] * 3 +
                  [' ', 'C '] + [digits + ' '] * 2)   # the battery's split, if any
        header += [' '] * (PAGE_COLUMNS - 1 - len(header)) + [CHECK_DIGITS]
        self.barred = bars(body)
        self.header_barred = bars(header)

    def read(self, img):
        import cv2
        import numpy as np
        if self.rect is None:
            self.rect = find_page(img)
            if self.rect is None:
                return None
        left, top, right, bottom = self.rect
        grey = cv2.cvtColor(img[top:bottom, left:right], cv2.COLOR_BGR2GRAY).astype(np.float32)
        # each of the Saturn's pixels as its mean brightness, black 0 to white
        # 1, against the glyphs' (a soft picture's edges stay grey, where a
        # threshold would make them either)
        small = cv2.resize(grey, (PAGE_COLUMNS * 8, PAGE_ROWS * 8), interpolation=cv2.INTER_AREA)
        lo, hi = np.percentile(small, 2), np.percentile(small, 98)
        lit = np.clip((small - lo) / max(hi - lo, 1), 0, 1)
        cells = lit.reshape(PAGE_ROWS, 8, PAGE_COLUMNS, 8).transpose(0, 2, 1, 3).reshape(-1, 64)
        misses = ((cells[:, None, :] - self.glyphs[None, :, :]) ** 2).sum(axis=2).reshape(PAGE_ROWS, PAGE_COLUMNS, -1)
        misses = misses + self.barred[None, :, :]
        misses[0] = misses[0] - self.barred + self.header_barred
        best = misses.argmin(axis=2)
        return [''.join(self.chars[i] for i in row) for row in best]


def video_frames(path):
    """The pictures of a video, or of a directory's images in name order."""
    import cv2
    if os.path.isdir(path):
        for name in sorted(os.listdir(path)):
            img = cv2.imread(os.path.join(path, name))
            if img is not None:
                yield img
        return
    video = cv2.VideoCapture(path)
    if not video.isOpened():
        sys.exit(f'{path}: not a video OpenCV can read')
    while True:
        ok, img = video.read()
        if not ok:
            break
        yield img


def read_pages(path):
    """Each frame's page: {(tag, index): (header numbers, [(letter, begin,
    end)])}, from the pictures that hold it whole (header and every row
    checked), the reading most of them agree on; and each tag's frame count.
    A page without a tag (the playback page's own) has tag 0."""
    import collections
    reader = PageReader()
    readings = collections.defaultdict(collections.Counter)
    totals = {}
    pictures = torn = 0
    for img in video_frames(path):
        pictures += 1
        rows = reader.read(img)
        if rows is None or not row_checked(rows[0]):
            reader.rect = None  # the page found again: it may have moved
            continue
        m = PAGE_HEADER.match(rows[0][:PAGE_COLUMNS - 1])
        if not m:
            continue
        if not all(row_checked(r) for r in rows[1:]):
            torn += 1
            continue
        index, frames, start, length, count, dropped = (int(v) for v in m.groups()[:6])
        tag = int(m.group(7) or 0)
        events = []
        for r in rows[1:]:
            for c in range(PAGE_CELLS):
                cell = r[c * 8:c * 8 + 7]
                if cell[0] == ' ':
                    break
                events.append((cell[0], int(cell[1:4]), int(cell[4:7])))
        if len(events) != count:
            torn += 1
            continue
        totals[tag] = frames
        readings[(tag, index)][((start, length, count, dropped), tuple(events))] += 1
    pages = {k: c.most_common(1)[0][0] for k, c in readings.items()}
    print(f'{path}: {pictures} pictures, {len(pages)} frames read of {sum(totals.values())}, '
          f'{torn} pictures torn or misread')
    return pages, totals


def nest(spans):
    """Spans (begin, end, name) with their depths, outer first."""
    out, stack = [], []
    for b, e, name in sorted(spans, key=lambda s: (s[0], -s[1])):
        while stack and stack[-1] <= b:
            stack.pop()
        out.append((b, e, len(stack), name))
        stack.append(e)
    return out


def uncovered(spans, lo, hi):
    """The time from lo to hi in no span."""
    out, t = [], lo
    for b, e in sorted((max(b, lo), min(e, hi)) for b, e in spans if e > lo and b < hi):
        if b > t:
            out.append((t, b))
        t = max(t, e)
    if hi > t:
        out.append((t, hi))
    return out


def split_names():
    """The battery's splits by tag (saturn/debug.cpp kSplits), from 1."""
    text = open(os.path.join(ROOT, 'saturn/debug.cpp')).read()
    if 'kSplits[] = {' not in text:
        return {}   # a build without the battery
    body = text[text.index('kSplits[] = {'):]
    body = body[:body.index('};')]
    return {i + 1: name for i, name in enumerate(re.findall(r'\{ "([^"]+)",', body))}


def page_trace(pages, out_path, title):
    """One playback's pages ({index: page}) as a Chrome trace at out_path.
    Returns its summary: frames, ms read, each label's own ms, and each piece's
    ms (its spans' mean) by label and by the master's item order."""
    import collections
    ids, _ = trace_ids()
    letters = trace_letters()
    by_letter = {c: i for i, c in enumerate(letters) if c != '.'}

    lanes = {1: [], 2: [], 3: [], 4: [], 7: []}
    vdp1, shown, vblanks, windows = [], [], [], []
    opened = set()      # spans whose end the page couldn't hold
    for index in sorted(pages):
        (start, length, count, dropped), events = pages[index]
        windows.append((start, start + length, index, dropped))
        for letter, b, e in events:
            ident = by_letter[letter]
            name = ids[ident]
            if e == OPEN_END:
                opened.add((start + b, name))
            span = (start + b, start + e, name)
            if name == 'PLOT':
                vdp1.append(span)
            elif name == 'SHOWN':
                shown.append(start + b)
            elif name == 'IRQ_VBLANK_IN':
                vblanks.append(start + b)
                lanes[2].append(span)
            elif ident < 32:
                lanes[1].append(span)
            elif ident < 40:
                lanes[3].append(span)
            elif name == 'DSP_RUN':
                lanes[4].append(span)
            else:
                lanes[7].append(span)

    def ms_us(tenths):
        return round(tenths * 100, 1)

    events = []

    def x(tid, b, e, name, cat, cname=None):
        ev = {'name': name, 'cat': cat, 'ph': 'X', 'pid': 1, 'tid': tid, 'ts': ms_us(b), 'dur': ms_us(e - b)}
        if cname:
            ev['cname'] = cname
        events.append(ev)

    def label(name):
        if name in WAITS:
            return 'WAIT ' + name[:-5].lower().replace('_', ' ')
        return name.lower().replace('_', ' ')

    selfs, window = collections.Counter(), sum(e - b for b, e, _, _ in windows)
    pieces = collections.defaultdict(list)
    for tid, spans in lanes.items():
        nested = nest(spans)
        for b, e, depth, name in nested:
            wait = name in WAITS
            x(tid, b, e, label(name), 'wait' if wait else 'work', 'terrible' if wait else None)
            inner = sum(s[1] - s[0] for s in nested if s[2] == depth + 1 and b <= s[0] and s[1] <= e)
            selfs[label(name)] += (e - b) - inner
            if (b, name) not in opened:
                pieces[label(name)].append(e - b)
        if tid in (1, 3):
            top = [(b, e) for b, e, depth, _ in nested if depth == 0]
            for lo, hi, _, _ in windows:
                for b, e in uncovered(top, lo, hi):
                    x(tid, b, e, 'idle', 'idle', 'grey')
                    selfs['(idle)' if tid == 1 else '(slave idle)'] += e - b
        if tid == 3:
            for b0, e0, depth, name in nested:
                if name == 'BATCH':
                    inside = [(b, e) for b, e, d, _ in nested if d == depth + 1]
                    for b, e in uncovered(inside, b0, e0):
                        x(tid, b, e, 'wait for jobs', 'idle', 'bad')
                        selfs['(wait for jobs)'] += e - b
    for b, e, name in vdp1:
        x(5, b, e, 'VDP1 draws', 'vdp1')
        if (b, name) not in opened:
            pieces['VDP1 draws'].append(e - b)
    shown.sort()
    for b, e in zip(shown, shown[1:]):
        x(6, b, e, 'list on screen', 'display')
    for t in vblanks:
        events.append({'name': 'vblank', 'ph': 'i', 's': 't', 'pid': 1, 'tid': 6, 'ts': ms_us(t)})
    for lo, hi, index, dropped in windows:
        x(8, lo, hi, f'frame {index}' + (f' ({dropped} events left out)' if dropped else ''), 'frame')

    # the master's items in the order it draws them each frame
    items = collections.defaultdict(list)
    for lo, hi, _, _ in windows:
        mine = sorted((b, e) for b, e, name in lanes[1] if name == 'ITEM' and lo <= b < hi)
        for k, (b, e) in enumerate(mine):
            items[k].append(e - b)

    names = dict(TRACK_NAMES)
    names[8] = 'Frames'
    meta = []
    for order, (tid, name) in enumerate(names.items()):
        meta.append({'name': 'thread_name', 'ph': 'M', 'pid': 1, 'tid': tid, 'args': {'name': name}})
        meta.append({'name': 'thread_sort_index', 'ph': 'M', 'pid': 1, 'tid': tid, 'args': {'sort_index': order}})
    meta.append({'name': 'process_name', 'ph': 'M', 'pid': 1, 'args': {'name': title}})
    with open(out_path, 'w') as f:
        json.dump({'traceEvents': meta + events, 'displayTimeUnit': 'ms'}, f)

    frames = len(windows)
    print(f'{out_path}: {window / 10:.1f} ms in {frames} frames ({frames * 10000 / window:.1f} fps), '
          f'{sum(w[3] for w in windows)} events left out, {len(opened)} spans past 99.9 ms')
    return {'frames': frames, 'frame': window / frames / 10,
            'own': {k: v / frames / 10 for k, v in selfs.items()},
            'pieces': {k: sum(v) / len(v) / 10 for k, v in pieces.items()},
            'items': {k: sum(v) / len(v) / 10 for k, v in items.items() if len(v) * 2 >= frames}}


# the battery's table: each split's own time a frame of these, then the mean
# of one piece of work, by kind and by the master's item order
TABLE_OWN = ['update', 'view', 'queue', 'lara', 'objects', 'item', 'static', 'mesh', 'meshes finish', 'flush',
             'upload', 'overlay', 'WAIT rooms', 'WAIT far', 'WAIT vdp1', 'WAIT dsp', 'WAIT tick', '(idle)',
             'xform', 'faces', 'slave mesh', 'slave item', 'slave flush', '(wait for jobs)',
             '(slave idle)', 'dsp run', 'dsp meshes']
TABLE_PIECES = ['lara', 'static', 'mesh', 'xform', 'faces', 'slave mesh', 'slave item', 'slave flush',
                'dsp run', 'dsp meshes', 'VDP1 draws']


def battery_table(summaries, names):
    tags = sorted(summaries)
    width = 15
    head = '%-22s' % '' + ''.join(f'{names.get(t, str(t))[:width - 1]:>{width}}' for t in tags)
    print('\nms a frame (own time; a CPU or the DSP adds up to its busy time)')
    print(head)
    print('%-22s' % 'frame' + ''.join(f'{summaries[t]["frame"]:{width}.2f}' for t in tags))
    for k in TABLE_OWN:
        if any(summaries[t]['own'].get(k, 0) >= 0.005 for t in tags):
            print('%-22s' % k + ''.join(f'{summaries[t]["own"].get(k, 0):{width}.2f}' for t in tags))
    print('\nms one piece of work takes (the mean of its spans)')
    print(head)
    for k in TABLE_PIECES:
        if any(k in summaries[t]['pieces'] for t in tags):
            print('%-22s' % k + ''.join(
                f'{summaries[t]["pieces"][k]:{width}.2f}' if k in summaries[t]['pieces'] else ' ' * (width - 1) + '-'
                for t in tags))
    count = max(len(summaries[t]['items']) for t in tags)
    for i in range(count):
        print('%-22s' % f'master item #{i + 1}' + ''.join(
            f'{summaries[t]["items"][i]:{width}.2f}' if i in summaries[t]['items'] else ' ' * (width - 1) + '-'
            for t in tags))


def video(path, out_path):
    pages, totals = read_pages(path)
    if not pages:
        sys.exit('no page read whole: is the playback page on, and the video sharp enough?')
    names = split_names()
    summaries = {}
    for tag in sorted(totals):
        mine = {i: p for (t, i), p in pages.items() if t == tag}
        missing = [i for i in range(totals[tag]) if i not in mine]
        if missing:
            print(f'{names.get(tag, "playback")}: frames not read: {missing}')
        if not mine:
            continue
        if tag:
            base, ext = os.path.splitext(out_path)
            name = f'{base}_c{tag}{ext}'
            title = f'Saturn battery: {names.get(tag, tag)}'
        else:
            name, title = out_path, 'Saturn (from the playback video)'
        summaries[tag] = page_trace(mine, name, title)
    if len(summaries) > 1 or 0 not in summaries:
        battery_table(summaries, names)
    else:
        s = summaries[0]
        print('own time, ms a frame:')
        for k, v in sorted(s['own'].items(), key=lambda kv: -kv[1]):
            print(f'  {k:16s} {v:6.2f}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('capture')
    c.add_argument('cue')
    c.add_argument('--frame', type=int, default=1900)
    c.add_argument('--keys', default=DEFAULT_KEYS)
    c.add_argument('--fps', type=int, default=0)
    c.add_argument('--shots', type=int, default=0)
    c.add_argument('--playback', type=int, default=0)
    c.add_argument('--record', type=int, default=0)
    c.add_argument('--out', default='trace')
    v = sub.add_parser('convert')
    v.add_argument('dump')
    v.add_argument('--out')
    d = sub.add_parser('video')
    d.add_argument('video')
    d.add_argument('--out')
    o = sub.add_parser('open')
    o.add_argument('json')
    args = ap.parse_args()

    if args.cmd == 'open':
        open_ui(args.json)
        return
    if args.cmd == 'video':
        video(args.video, args.out or os.path.splitext(args.video.rstrip('/\\'))[0] + '.json')
        return
    if args.cmd == 'capture':
        dump = capture(os.path.abspath(args.cue), args.frame, args.keys, args.out, args.fps, args.shots,
                       args.playback, args.record)
        out = os.path.join(ROOT, 'build', args.out + '.json')
    else:
        dump = args.dump
        out = args.out or os.path.splitext(dump)[0] + '.json'
    with open(dump, 'rb') as f:
        convert(f.read(), out)


if __name__ == '__main__':
    main()
