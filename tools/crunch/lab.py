"""The crunch lab: draws a camera move through captured rooms with each idea
(variants.py), exactly as VDP1 would (Mednafen's drawing code), beside a
filtered reference, and writes a page to compare them.

    lab.py [--capture ledge0] [--path dolly|creep|pan|strafe|still]
           [--frames 60] [--variants game,exact,ripmap+exact,...] [--fresh]
           [--tag NAME] [--page]

Variants are ingredients (variants.INGREDIENTS) or combinations of them
joined by '+'. A run adds its variants to the page's earlier ones (same
camera move) unless --fresh. labserver.py draws any mix of the techniques
on demand (mixer.py) into the same page.

Output: build/crunch/runs/<capture>-<path>[-<tag>]/index.html, with each
variant's frames, flicker and error pictures as sprite sheets.

Setting up (once, or after the game's renderer changes):
    wsl -d Ubuntu -- sh tools/crunch/build_vdp1lib.sh   the VDP1 library
    capture.py NAME           a frame of the game in a private Mednafen
    check_vdp1.py NAME        vdp1lib draws its list as Mednafen did
    check_rooms.py NAME       rooms.py builds the game's room commands

Measures, per variant and frame:
  vdp1_ms   VDP1's drawing time (calibrated costs, the rooms alone)
  flicker   mean |change of the frame - change of the reference| (0..255
            luminance): what moves on screen that the scene doesn't. A
            variant that changes the art is held to the reference drawn with
            its own art (the ideal of that art), not the original's
  error     mean |frame - reference|: everything that differs, still or not
  corner    99th percentile of the on-screen room corners' distance (px)
            from where an exact projection puts them (whole pixels alone:
            up to 1.41)
  detail    the frame's fine detail (mean |Laplacian|) over the
            reference's: above 1 crunchier, below 1 softer than ideal
  form      its middle-sized detail (difference of 1 and 3 px blurs) over
            the reference's: below 1 blobby or washed out
  error_mul mean |frame - the reference shaded as the PS1 multiplies|
  wobble    95th percentile of the frame-to-frame change (px) of that
            distance: corners hopping (whole pixels alone: about 1 px)
"""
import argparse
import json
import os
import time

import numpy as np
from PIL import Image
from scipy import ndimage

from capdecode import Capture
from check_rooms import view_of
from gamedata import Level
from medclient import LAB
from ref import Reference
from rooms import Options, RoomDrawer, RoomJob, TextureStore, View, build_commands
from variants import INGREDIENTS, compose
import texstudy
import textures
from vdp1 import Vdp1, rgb

VDP1_HZ = 28636364      # the 352-wide display's clock, VDP1's cycle counter's
FULL = (0, 0, 352, 224)


def forward(view):
    return view.rot[2].astype(np.float64) / 16384.0


def right(view):
    return view.rot[0].astype(np.float64) / 16384.0


PATHS = {
    'dolly': ('Forward 12 units a frame (about Lara\'s walk)',
              lambda v, i: View(np.round(np.array(v.pos) + forward(v) * 12 * i), v.angle_x, v.angle_y, FULL)),
    'creep': ('Forward 3 units a frame (a camera settling)',
              lambda v, i: View(np.round(np.array(v.pos) + forward(v) * 3 * i), v.angle_x, v.angle_y, FULL)),
    'pan': ('Turning 24 angle units (0.13 degrees) a frame',
            lambda v, i: View(v.pos, v.angle_x, (v.angle_y + 24 * i) & 0xFFFF, FULL)),
    'strafe': ('Sideways 8 units a frame',
               lambda v, i: View(np.round(np.array(v.pos) + right(v) * 8 * i), v.angle_x, v.angle_y, FULL)),
    'still': ('Not moving (flicker should read 0)', lambda v, i: View(v.pos, v.angle_x, v.angle_y, FULL)),
}


def luminance(img):
    return img.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)


def heat(values, scale):
    """0.. -> black, magenta, yellow, white."""
    t = np.clip(values / scale, 0, 1)
    r = np.clip(t * 3, 0, 1)
    g = np.clip(t * 3 - 1, 0, 1)
    b = np.clip(np.where(t < 1 / 3, t * 3, 2 - t * 3), 0, 1) * 0.8 + np.clip(t * 3 - 2, 0, 1) * 0.2
    return (np.stack([r, g, b], axis=-1) * 255).astype(np.uint8)


def vertex_errors(drawer, jobs, verts):
    """(room, vertex) -> (dx, dy): each on-frame room vertex's screen
    position less its exact projection (pixel x covers x .. x + 1)."""
    x, y, z, g, clip, s = verts.arrays()
    out = {}
    for job in jobs:
        base = drawer.room_vertices.get(job.room.index)
        if base is None:
            continue
        v4 = job.room.points.astype(np.float64)
        view = v4 @ job.R.T.astype(np.float64) + job.T.astype(np.float64)
        n = len(v4)
        ex = 176 + 256.0 * view[:, 0] / view[:, 2]
        ey = 112 + 256.0 * view[:, 1] / view[:, 2]
        ok = (clip[base:base + n] & 0x21) == 0
        for i in np.nonzero(ok)[0]:
            out[(job.room.index, int(i))] = (x[base + i] - ex[i], y[base + i] - ey[i])
    return out


def form(lum):
    """Mean |difference of Gaussians (1 and 3 px)|: the frame's middle-sized
    detail (cracks, edges, lumps: what reads as the surface)."""
    return float(np.abs(ndimage.gaussian_filter(lum, 1.0) - ndimage.gaussian_filter(lum, 3.0)).mean())


def laplacian(lum):
    """Mean |4-neighbour Laplacian|: how much fine detail (or noise) a frame has."""
    return float(np.abs(4 * lum[1:-1, 1:-1] - lum[:-2, 1:-1] - lum[2:, 1:-1] - lum[1:-1, :-2] - lum[1:-1, 2:]).mean())


def view_key(v):
    return dict(pos=list(map(int, v.pos)), ax=v.angle_x, ay=v.angle_y)


def art_key(name):
    """The ingredients of a variant that change the art (what its ideal
    picture shows), joined; '' for none."""
    return '+'.join(p for p in name.split('+') if 'level' in INGREDIENTS[p] and INGREDIENTS[p]['group'] != 'geometry')


def texture_pixels(level, jobs):
    """Pixels each room texture draws in the game's frame for jobs."""
    opts = Options()
    faces, verts = RoomDrawer(level, opts).faces(jobs)
    cl = build_commands(level, faces, verts, opts, TextureStore(level, opts))
    v = Vdp1()
    v.load_vram(cl.finish())
    v.fb[:] = 0
    _, log = v.plot(log_max=8192)
    order = []
    buckets = {}
    for f in faces:
        buckets.setdefault(f.bucket, []).append(f)
    for b in sorted(buckets, reverse=True):
        order += list(reversed(buckets[b]))
    px = {}
    for c in log:
        i = c.addr // 4 - 3     # the three clip and coordinate commands first
        if 0 <= i < len(order) and order[i].tex is not None:
            px[order[i].tex] = px.get(order[i].tex, 0) + c.pixels
    return px


def tile_strip(images, size=128):
    """Textures side by side, each scaled by whole steps to about size."""
    tiles = []
    for img in images:
        k = max(1, size // max(img.shape[:2]))
        t = np.kron(img, np.ones((k, k, 1), np.uint8))
        canvas = np.full((size, size, 3), 24, np.uint8)
        canvas[:min(size, t.shape[0]), :min(size, t.shape[1])] = t[:size, :size]
        tiles.append(np.pad(canvas, ((0, 0), (0, 4), (0, 0)), constant_values=255))
    return np.concatenate(tiles, axis=1)


def texture_measures(level, share, top):
    """The textures' grain, lonely texels and runs (texstudy), weighted by
    their screen share, and a strip of the top ones."""
    acc = {}
    total = 0
    for tex, n in share.items():
        t = level.textures[tex]
        st = texstudy.texture_stats(t.texels(), textures.to_rgb(t.colors), t.transparent)
        if st is None:
            continue
        for k in ('grain', 'lonely', 'run', 'contrast', 'step'):
            acc[k] = acc.get(k, 0.0) + st[k] * n
        total += n
    stats = {k: v / total for k, v in acc.items()}
    imgs = []
    for tex in top:
        t = level.textures[tex]
        imgs.append((textures.to_rgb(t.colors)[t.texels()] * 255 / 31).astype(np.uint8))
    return stats, tile_strip(imgs)


def save_sheet(path, frames):
    Image.fromarray(np.concatenate(frames, axis=0)).save(path, optimize=True)


class Lab:
    """One camera move through a capture's rooms: its views, references and
    the game's texture shares. draw() draws and measures a variant."""

    def __init__(self, capture, path, frames, run):
        self.capture, self.path = capture, path
        self.cap = Capture(capture)
        self.base = Level('LEVEL1')
        view0, _ = view_of(self.cap)
        self.room_order = [r for r, *_ in self.cap.jobs()]
        self.path_title, path_fn = PATHS[path]
        self.views = [path_fn(view0, i) for i in range(frames)]
        self.run = run
        self.out = os.path.join(LAB, 'runs', run)
        os.makedirs(self.out, exist_ok=True)
        self.vdp1 = Vdp1()
        self.prev = self.load_run()

        t0 = time.time()
        self.refs = self.reference('ref', self.base)
        self.refs_mul = self.reference('ref_mul', self.base, 'multiply')
        print(f'references: {time.time() - t0:.1f} s', flush=True)
        self.ref_l = [luminance(r) for r in self.refs]
        self.ref_mul_l = [luminance(r) for r in self.refs_mul]
        self.ref_detail = float(np.mean([laplacian(lum) for lum in self.ref_l]))
        self.ref_form = float(np.mean([form(lum) for lum in self.ref_l]))
        self.art_refs = {}      # sheet name -> luminances

        self.share = texture_pixels(self.base, self.jobs_for(self.base, self.views[0]))
        self.top = sorted(self.share, key=lambda k: -self.share[k])[:12]
        sonic = texstudy.sonic_r_tiles()
        self.sonic_stats = sonic[0] if sonic else None
        if sonic:
            Image.fromarray(tile_strip(sonic[1])).save(os.path.join(self.out, 'sonicr.tiles.png'))

    def load_run(self):
        """The last run's page data, if it drew the same views."""
        try:
            with open(os.path.join(self.out, 'run.json')) as f:
                prev = json.load(f)
            if prev['views'] == [view_key(v) for v in self.views]:
                return prev
        except (OSError, KeyError, ValueError):
            pass
        return None

    def jobs_for(self, level, view):
        return [RoomJob.from_view(view, level.rooms[r], FULL) for r in self.room_order]

    def reference(self, sheet, level, shading='add'):
        """Reference frames for level, from the run's sheet if an earlier
        run drew it for these views, else rendered and saved."""
        path = os.path.join(self.out, sheet + '.png')
        if self.prev is not None and os.path.exists(path):
            img = np.array(Image.open(path))
            if img.shape[0] == 224 * len(self.views):
                return [img[i * 224:(i + 1) * 224] for i in range(len(self.views))]
        ref = Reference(level)
        frames = [ref.render(self.jobs_for(level, v), shading) for v in self.views]
        save_sheet(path, frames)
        return frames

    def art_reference(self, sheet, level):
        """Luminances of the reference drawn with level's art."""
        if sheet not in self.art_refs:
            t0 = time.time()
            self.art_refs[sheet] = [luminance(f) for f in self.reference(sheet, level)]
            print(f'reference {sheet}: {time.time() - t0:.1f} s', flush=True)
        return self.art_refs[sheet]

    def draw(self, name, title, about, group, kw, level, ref_sheet=None):
        """Draws the move with level and Options(**kw); writes the variant's
        sheets (name.png, .flicker.png, .error.png, .tiles.png) and returns
        its measures. ref_sheet names the reference of its own art (None:
        the original art's); its flicker is measured against that."""
        own_l = self.art_reference(ref_sheet, level) if ref_sheet else self.ref_l
        opts = Options(**kw)
        drawer = RoomDrawer(level, opts)
        store = TextureStore(level, opts)
        r = dict(title=title, about=about, group=group, vdp1_ms=[], flicker=[], error=[], error_mul=[],
                 wobble=[], corner=[], detail=[], form=[], commands=[], texture_kb=[])
        frames, flick_imgs, err_imgs = [], [], []
        last_err = None
        t1 = time.time()
        for i, view in enumerate(self.views):
            jobs = self.jobs_for(level, view)
            faces, verts = drawer.faces(jobs)
            errs = vertex_errors(drawer, jobs, verts)
            r['corner'].append(float(np.percentile([np.hypot(*e) for e in errs.values()], 99)))
            both = [k for k in errs if last_err and k in last_err]
            r['wobble'].append(float(np.percentile([np.hypot(errs[k][0] - last_err[k][0],
                                                             errs[k][1] - last_err[k][1]) for k in both], 95))
                               if both else None)
            last_err = errs
            cl = build_commands(level, faces, verts, opts, store)
            self.vdp1.load_vram(cl.finish())
            self.vdp1.fb[:] = 0
            totals, _ = self.vdp1.plot()
            img = rgb(self.vdp1.fb).copy()
            frames.append(img)
            r['vdp1_ms'].append(totals.cycles * 1000.0 / VDP1_HZ)
            r['commands'].append(len(cl.cmds))
            r['texture_kb'].append(cl.texture_bytes / 1024.0)
            lum = luminance(img)
            e = np.abs(lum - self.ref_l[i])
            r['error'].append(float(e.mean()))
            r['error_mul'].append(float(np.abs(lum - self.ref_mul_l[i]).mean()))
            r['detail'].append(laplacian(lum) / self.ref_detail)
            r['form'].append(form(lum) / self.ref_form)
            err_imgs.append(heat(e, 96))
            if i == 0:
                f = np.zeros_like(lum)
                r['flicker'].append(None)
            else:
                f = np.abs((lum - luminance(frames[i - 1])) - (own_l[i] - own_l[i - 1]))
                r['flicker'].append(float(f.mean()))
            flick_imgs.append(heat(f, 64))
        base = os.path.join(self.out, name)
        os.makedirs(os.path.dirname(base), exist_ok=True)
        save_sheet(base + '.png', frames)
        save_sheet(base + '.flicker.png', flick_imgs)
        save_sheet(base + '.error.png', err_imgs)
        tstats, strip = texture_measures(level, self.share, self.top)
        r.update({'tex_' + k: v for k, v in tstats.items()})
        Image.fromarray(strip).save(base + '.tiles.png')
        r['art'] = ref_sheet[4:] if ref_sheet else ''
        m = lambda k: float(np.mean([v for v in r[k] if v is not None]))
        print(f'{name:28s} {time.time() - t1:5.1f} s  vdp1 {m("vdp1_ms"):5.2f} ms  cmds {m("commands"):5.0f}  '
              f'tex {m("texture_kb"):4.0f} KB  flicker {m("flicker"):5.2f}  error {m("error"):5.2f} '
              f'(x {m("error_mul"):5.2f})  detail {m("detail"):4.2f}  form {m("form"):4.2f}  '
              f'corner {m("corner"):4.2f}  wobble {m("wobble"):4.2f}', flush=True)
        return r

    def meta(self, results, order):
        art_keys = sorted({r['art'] for r in results.values() if r.get('art')})
        return dict(run=self.run, capture=self.capture, path=self.path, path_title=self.path_title,
                    frames=len(self.views), art_refs=art_keys, sonic_r=self.sonic_stats, width=352, height=224,
                    variants=[n for n in order if n in results], results=results,
                    views=[view_key(v) for v in self.views])

    def save(self, results, order):
        meta = self.meta(results, order)
        with open(os.path.join(self.out, 'run.json'), 'w') as f:
            json.dump(meta, f)
        write_page(self.out, meta)
        return meta


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--capture', default='ledge0')
    ap.add_argument('--path', default='dolly', choices=sorted(PATHS))
    ap.add_argument('--frames', type=int, default=60)
    ap.add_argument('--variants', default=','.join(INGREDIENTS),
                    help='ingredients, or combinations of them joined by +')
    ap.add_argument('--fresh', action='store_true', help='drop the variants an earlier run left in the page')
    ap.add_argument('--tag', default='', help='a separate page: runs/<capture>-<path>-<tag>')
    ap.add_argument('--page', action='store_true', help='only rewrite the page from the results of the last run')
    args = ap.parse_args()
    run = f'{args.capture}-{args.path}' + (f'-{args.tag}' if args.tag else '')

    if args.page:
        out = os.path.join(LAB, 'runs', run)
        with open(os.path.join(out, 'run.json')) as f:
            write_page(out, json.load(f))
        return

    lab = Lab(args.capture, args.path, args.frames, run)
    old = {} if args.fresh or lab.prev is None else lab.prev['results']
    names = args.variants.split(',')
    results = {}
    for name in names:
        title, about, group, kw, change = compose(name)
        level = change(lab.base) if change else lab.base
        key = art_key(name)
        results[name] = lab.draw(name, title, about, group, kw, level, 'ref.' + key if key else None)
    for k, v in old.items():
        if k not in results and os.path.exists(os.path.join(lab.out, k + '.png')):
            results[k] = v
    lab.save(results, list(old) + [n for n in names if n not in old])


def write_page(out, meta):
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'viewer.html'), encoding='utf-8') as f:
        page = f.read()
    with open(os.path.join(out, 'index.html'), 'w', encoding='utf-8') as f:
        f.write(page.replace('/*RUN_JSON*/null', json.dumps(meta)))
    print(os.path.join(out, 'index.html'))


if __name__ == '__main__':
    main()
