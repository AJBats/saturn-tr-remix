"""The game's frame trace (saturn/trace.h, HW_LOG builds) as a Chrome trace.

    trace.py capture CUE [--frame N] [--keys F:K,K,...] [--fps N] [--out NAME]
        boots CUE in a Mednafen of its own (its own automation and home
        directories, so a Mednafen the MCP server runs is left alone), taps
        the player's pad buttons K from frame F, runs to frame N, dumps the
        trace (build/NAME.bin) and converts it (build/NAME.json). The default
        keys page the HW_LOG overlay to the trace page with Trace detail on,
        which holds the bench on Base. --fps N first counts the frames the
        game draws over N vblanks (from the frame the keys end) for a frame
        rate that a trace's few seconds can't give: frames take 2 or 3
        vblanks, so a short window's rate moves with a handful of them.
    trace.py convert DUMP [--out JSON]
        converts a dump of low work RAM (1 MB from 0x00200000)
    trace.py open JSON
        opens JSON in ui.perfetto.dev in the default browser

Open the JSON in ui.perfetto.dev or chrome://tracing. Tracks: the master's
stages, its interrupts, the slave's jobs, the SCU DSP's runs (as the slave
sees them start and end), VDP1 drawing each list, and the display (vblanks,
and each list on screen). Gaps are drawn as spans too: "idle" where a CPU is
in no stage, "wait for jobs" where the slave polls for rooms in a batch, so
that time no stage accounts for stands out next to the waits ("WAIT ...").

Also prints each stage's own time a frame, as the overlay's trace page does.
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
LANES = ['master', 'irq', 'slave', 'dsp']
CLOCK = 28636360        # SH-2 cycles a second at the 352-dot clock
TICK = 128              # cycles an FRT tick
FRAME_TICKS = 7457      # a 30 fps frame
END = 0x80

TRACK_NAMES = {1: 'Master SH-2', 2: 'Master interrupts', 3: 'Slave SH-2', 4: 'SCU DSP', 5: 'VDP1', 6: 'Display'}
WAITS = {'TICK_WAIT', 'ROOMS_WAIT', 'FAR_WAIT', 'VDP1_WAIT'}


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
    """Each lane's events, oldest first, as (16-bit master ticks, code, arg)."""
    header = HEADER - LWRAM
    magic, offset = struct.unpack_from('>II', data, header)
    if magic != MAGIC:
        sys.exit(f'no trace in the dump (magic {magic:#x}): an HW_LOG build in a level?')
    lanes = []
    for i in range(len(LANES)):
        ring, size, nxt = struct.unpack_from('>III', data, header + 8 + i * 12)
        count = min(nxt, size)
        base = ring - LWRAM
        events = []
        for n in range(nxt - count, nxt):
            e, = struct.unpack_from('>I', data, base + (n & (size - 1)) * 4)
            t = e >> 16
            if i >= 2:      # the slave's counter
                t = (t - offset) & 0xFFFF
            events.append((t, (e >> 8) & 0xFF, e & 0xFF))
        lanes.append(events)
    return lanes, offset


def unwrap(lanes):
    """Ticks on one timeline: each lane's newest event placed against the
    master's newest (lanes end within a few frames of each other), then
    walked back by the 16-bit differences."""
    ref = lanes[0][-1][0] if lanes[0] else 0
    base = 1 << 24
    out = []
    for events in lanes:
        if not events:
            out.append([])
            continue
        t16 = events[-1][0]
        d = (t16 - ref) & 0xFFFF
        if d >= 0x8000:
            d -= 0x10000
        times = [base + ref + d]
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
    if name == 'JOB_SETUP':
        return 'job setup (to slave)' if arg_end else 'job setup (kept)'
    if name == 'DSP_RUN':
        return f'DSP run job {arg_begin}'
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
    lanes, offset = read_lanes(data)
    lanes = unwrap(lanes)
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

    tids = {0: 1, 1: 2, 2: 3, 3: 4}
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
    for tid, name in TRACK_NAMES.items():
        meta.append({'name': 'thread_name', 'ph': 'M', 'pid': 1, 'tid': tid, 'args': {'name': name}})
        meta.append({'name': 'thread_sort_index', 'ph': 'M', 'pid': 1, 'tid': tid, 'args': {'sort_index': tid}})
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


# from the bench's table: R to the trace page, R to the menu, up to its last
# option (Trace), right from Stages to Detail, L back to the trace page
DEFAULT_KEYS = '1500:R,R,R,R,UP,RIGHT,L'


def map_symbol(name):
    """A symbol's address from build/0MAIN.map."""
    for line in open(os.path.join(ROOT, 'build/0MAIN.map')):
        parts = line.split()
        if len(parts) == 2 and parts[1] == name and parts[0].startswith('0x'):
            return int(parts[0], 16)
    sys.exit(f'{name} not in build/0MAIN.map')


FRAMES_OFFSET = 24      # FrameStats.frames (system.h)


def capture(cue, frame, keys, name, fps_window=0):
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
                send('frame_advance 6', 'done frame_advance', 30)
                send(f'input_release {b}', 'ok input_release', 10)
                send('frame_advance 6', 'done frame_advance', 30)
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
        with open(dump, 'wb') as out:
            for at in range(LWRAM, LWRAM + LWRAM_BYTES, 0x10000):
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('capture')
    c.add_argument('cue')
    c.add_argument('--frame', type=int, default=1900)
    c.add_argument('--keys', default=DEFAULT_KEYS)
    c.add_argument('--fps', type=int, default=0)
    c.add_argument('--out', default='trace')
    v = sub.add_parser('convert')
    v.add_argument('dump')
    v.add_argument('--out')
    o = sub.add_parser('open')
    o.add_argument('json')
    args = ap.parse_args()

    if args.cmd == 'open':
        open_ui(args.json)
        return
    if args.cmd == 'capture':
        dump = capture(os.path.abspath(args.cue), args.frame, args.keys, args.out, args.fps)
        out = os.path.join(ROOT, 'build', args.out + '.json')
    else:
        dump = args.dump
        out = args.out or os.path.splitext(dump)[0] + '.json'
    with open(dump, 'rb') as f:
        convert(f.read(), out)


if __name__ == '__main__':
    main()
