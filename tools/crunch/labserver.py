"""The crunch lab's page served live: panel B's mixer (mixer.py) asks this
server to draw any mix of the techniques, which it does on demand (a few
seconds) and adds to the page.

    labserver.py [--port 8765]      then open http://localhost:8765/

Pages are the runs in build/crunch/runs (lab.py writes them). A mix is
drawn with its flicker measured against the original art's reference at
once; when the mix changes the art, the reference drawn with its own art
follows in the background (a minute or so) and its flicker is measured
again against that. Mixes stay in the run's page (run.json).
"""
import argparse
import json
import os
import threading
import time
import traceback
import urllib.parse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

import numpy as np
from PIL import Image

import mixer
from lab import Lab, heat, luminance, save_sheet, write_page
from medclient import LAB

RUNS = os.path.join(LAB, 'runs')


class Server:
    def __init__(self):
        self.labs = {}
        self.draw_lock = threading.Lock()   # one drawing at a time: one VDP1, and the CPU
        self.file_lock = threading.Lock()
        self.levels = {}                    # (run, level key) -> level
        self.status = {}                    # (run, name) -> dict(state, result, error)
        self.ref_locks = {}                 # reference sheet -> lock (one drawing of each)
        self.ref_cv = threading.Condition()
        self.ref_queue = []                 # mixes waiting for their own art's reference
        threading.Thread(target=self.ref_worker, daemon=True).start()

    def lab(self, run):
        if run not in self.labs:
            with open(os.path.join(RUNS, run, 'run.json')) as f:
                meta = json.load(f)
            self.labs[run] = Lab(meta['capture'], meta['path'], meta['frames'], run)
        return self.labs[run]

    def level(self, run, lab, mix, change):
        key = (run, mixer.key_of(mix, {r['id'] for r in mixer.ROWS
                                         if any('level' in o[2] for o in r['options'])} |
                                     ({'relief_light', 'relief_colours'} if mix.get('relief', 'off') != 'off' else set())))
        if key not in self.levels:
            if len(self.levels) > 6:
                self.levels.pop(next(iter(self.levels)))
            self.levels[key] = change(lab.base) if change else lab.base
        return self.levels[key]

    def request(self, run, mix):
        mix = mixer.normal(mix)
        name, title, kw, change, art = mixer.compile_mix(mix)
        name = 'live/' + name
        st = self.status.get((run, name))
        if st and st['state'] in ('queued', 'drawing', 'done'):
            return name
        drawn = self.stored(run, name)
        if drawn is not None:
            self.status[(run, name)] = dict(state='done', result=drawn)
            if drawn.get('flicker_pending'):
                threading.Thread(target=self.measure_later, args=(run, name, change, art, mix), daemon=True).start()
            return name
        self.status[(run, name)] = dict(state='queued')
        threading.Thread(target=self.draw, args=(run, name, title, kw, change, art, mix), daemon=True).start()
        return name

    def draw(self, run, name, title, kw, change, art, mix):
        st = self.status[(run, name)]
        try:
            with self.draw_lock:
                st['state'] = 'drawing'
                lab = self.lab(run)
                level = self.level(run, lab, mix, change)
                sheet = 'ref.mix-' + mixer.digest(art + mixer.recipe(mix)) if art else None
                ready = sheet and (sheet in lab.art_refs or os.path.exists(os.path.join(lab.out, sheet + '.png')))
                about = 'A mix: ' + (title if title != 'The game today' else 'nothing changed')
                r = lab.draw(name, 'Mix: ' + title, about, 'mix', kw, level, sheet if ready else None)
                r['mix'] = mix
                r['version'] = time.time()
                if sheet and not ready:
                    r['art'] = sheet[4:]
                    r['flicker_pending'] = True
                self.store(run, lab, name, r, art)
                st['result'] = r
                st['state'] = 'done'
            if sheet and not ready:
                self.queue_ref(run, lab, name, level, sheet)
        except Exception as e:
            traceback.print_exc()
            st['state'] = 'error'
            st['error'] = f'{type(e).__name__}: {e}'

    def measure_later(self, run, name, change, art, mix):
        """A mix drawn in an earlier session whose flicker still waits for
        its own art's reference."""
        with self.draw_lock:
            lab = self.lab(run)
            level = self.level(run, lab, mix, change)
        self.queue_ref(run, lab, name, level, 'ref.mix-' + mixer.digest(art + mixer.recipe(mix)))

    def queue_ref(self, *job):
        with self.ref_cv:
            self.ref_queue.append(job)
            self.ref_cv.notify()

    def ref_worker(self):
        """Draws the waiting references one at a time, the newest mix's
        first: the one on screen."""
        while True:
            with self.ref_cv:
                while not self.ref_queue:
                    self.ref_cv.wait()
                job = self.ref_queue.pop()
            try:
                self.fair_flicker(*job)
            except Exception:
                traceback.print_exc()

    def fair_flicker(self, run, lab, name, level, sheet):
        """The reference with the mix's own art, then the mix's flicker
        measured again against it."""
        with self.ref_locks.setdefault(sheet, threading.Lock()):
            own = lab.art_reference(sheet, level)
        frames = np.array(Image.open(os.path.join(lab.out, name + '.png')))
        lums = [luminance(frames[i * 224:(i + 1) * 224]) for i in range(len(lab.views))]
        flicker, imgs = [None], [heat(np.zeros_like(lums[0]), 64)]
        for i in range(1, len(lums)):
            f = np.abs((lums[i] - lums[i - 1]) - (own[i] - own[i - 1]))
            flicker.append(float(f.mean()))
            imgs.append(heat(f, 64))
        save_sheet(os.path.join(lab.out, name + '.flicker.png'), imgs)
        st = self.status[(run, name)]
        r = dict(st['result'], flicker=flicker, version=time.time())
        r.pop('flicker_pending', None)
        st['result'] = r
        self.store(run, lab, name, r, None)

    def stored(self, run, name):
        """A mix an earlier session drew into the run's page, or None."""
        try:
            with self.file_lock, open(os.path.join(RUNS, run, 'run.json')) as f:
                return json.load(f)['results'].get(name)
        except (OSError, KeyError, ValueError):
            return None

    def store(self, run, lab, name, r, art):
        with self.file_lock:
            path = os.path.join(lab.out, 'run.json')
            with open(path) as f:
                meta = json.load(f)
            meta['results'][name] = r
            if name not in meta['variants']:
                meta['variants'].append(name)
            if r.get('art'):
                meta['art_refs'] = sorted(set(meta.get('art_refs', [])) | {r['art']})
                if art:
                    meta.setdefault('art_titles', {})[r['art']] = mixer.describe(art)
            with open(path, 'w') as f:
                json.dump(meta, f)
            write_page(lab.out, meta)


SERVER = Server()


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=RUNS, **kw)

    def log_message(self, fmt, *args):
        pass

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def reply(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(url.query))
        if url.path == '/':
            runs = sorted(d for d in os.listdir(RUNS) if os.path.exists(os.path.join(RUNS, d, 'index.html')))
            items = ''.join(f'<li><a href="/{d}/">{d}</a></li>' for d in runs)
            body = (f'<!doctype html><meta charset="utf-8"><title>Crunch Lab</title>'
                    f'<body style="font:15px system-ui;margin:24px"><h1>Crunch Lab</h1><ul>{items}</ul>').encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif url.path == '/api/rows':
            presets = {}
            try:
                with open(os.path.join(RUNS, q.get('run', ''), 'run.json')) as f:
                    meta = json.load(f)
                for n in meta['variants']:
                    p = mixer.preset(n)
                    # only variants drawn as the mixer would draw them now
                    current = (not mixer.uses_art_filters(p or {})
                               or meta['results'][n].get('art_version') == mixer.art.ART_VERSION)
                    if not n.startswith('live/') and p is not None and current:
                        presets[n] = p
            except (OSError, ValueError, KeyError):
                pass
            self.reply(dict(rows=mixer.rows_for_page(), presets=presets))
        elif url.path == '/api/status':
            st = SERVER.status.get((q['run'], q['name']))
            self.reply(st or dict(state='unknown'))
        else:
            super().do_GET()

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == '/api/mix':
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            name = SERVER.request(body['run'], body['mix'])
            self.reply(dict(name=name, **{k: v for k, v in SERVER.status[(body['run'], name)].items()}))
        else:
            self.send_error(404)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=8765)
    args = ap.parse_args()
    print(f'crunch lab: http://localhost:{args.port}/', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
