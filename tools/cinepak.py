"""The PlayStation's videos re-encoded as Cinepak for the Saturn (docs/ps1-fmv.md, "The other route").

The PS1 picture (ffmpeg's psxstr decode, the 120 picture lines) is encoded by ffmpeg's Cinepak encoder. The
decoder here does what saturn/cinepak.cpp does, pixel for pixel, and is checked against ffmpeg's own Cinepak
decoder (`check`). Written from the Cinepak bitstream as saturn/fmv.cpp reads it; ffmpeg is only run beside it.

    cinepak.py check TRACK [--clips NAME:START:COUNT:Q:STRIPS ...]   our decode vs ffmpeg's, and the strip rule
    cinepak.py benchframes TRACK OUT.h                              sequences packed for cpkbench
    cinepak.py encode TRACK OUTDIR [--settings q1s16 ...] [--videos NAME ...] [--jobs N]
                                                                    whole videos encoded: each frame's bytes and
                                                                    the luma PSNR against the PS1, to OUTDIR/*.json

TRACK is the PS1 disc's first track, raw (MODE2/2352). ffmpeg comes from --ffmpeg DIR or $FFMPEG_DIR.

The strip rule: each strip's vectors use only codebook entries that strip's own chunks sent in the same
frame. ffmpeg's encoder keeps it, and the Saturn's decoder relies on it: a CPU can take any strip without the
strips before it, and needs one codebook, not one per strip.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mdec  # noqa: E402

W, H, TOP = 320, 120, 44
OPAQUE = 0x80000000


# --- ffmpeg ---------------------------------------------------------------------------------------------

def ff(ffdir, args, data=None):
    return subprocess.run([os.path.join(ffdir, 'ffmpeg'), '-v', 'error', '-y'] + args, input=data,
                          capture_output=True, check=True).stdout


def ps1_frames(ffdir, raw, start, count):
    """The PS1 picture of frames start.. as (count, H, W, 3) uint8."""
    with tempfile.TemporaryDirectory() as t:
        path = os.path.join(t, 'v.str')
        with open(path, 'wb') as f:
            f.write(raw)
        data = ff(ffdir, ['-f', 'psxstr', '-i', path, '-an', '-vf',
                          f'select=between(n\\,{start}\\,{start + count - 1}),crop={W}:{H}:0:{TOP}', '-vsync', '0',
                          '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
    return np.frombuffer(data, np.uint8).reshape(-1, H, W, 3)[:count]


def encode(ffdir, pictures, q, strips, gop=12):
    """ffmpeg's Cinepak of (n, H, W, 3) pictures: (list of frame bytes, ffmpeg's own decode as (n, H, W, 3))."""
    n = len(pictures)
    with tempfile.TemporaryDirectory() as t:
        avi = os.path.join(t, 'c.avi')
        ff(ffdir, ['-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', '15', '-i', '-', '-c:v', 'cinepak',
                   '-q:v', str(q), '-max_strips', str(strips), '-max_extra_cb_iterations', '4', '-g', str(gop), avi],
           pictures.tobytes())
        sizes = [int(x) for x in subprocess.run(
            [os.path.join(ffdir, 'ffprobe'), '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'packet=size',
             '-of', 'csv=p=0', avi], capture_output=True, text=True, check=True).stdout.split()]
        data = ff(ffdir, ['-i', avi, '-c:v', 'copy', '-f', 'rawvideo', '-'])
        theirs = ff(ffdir, ['-i', avi, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
    assert len(data) == sum(sizes) and len(sizes) == n
    frames, p = [], 0
    for s in sizes:
        frames.append(data[p:p + s])
        p += s
    return frames, np.frombuffer(theirs, np.uint8).reshape(n, H, W, 3)


# --- decoding, as saturn/cinepak.cpp does -----------------------------------------------------------------

def be16(d, i):
    return (d[i] << 8) | d[i + 1]


def be24(d, i):
    return (d[i] << 16) | (d[i + 1] << 8) | d[i + 2]


def entry_rgb(e, grey):
    """A codebook entry's 4 pixels as (4, 3) ints: Cinepak's YUV, u/2 rounded toward zero as C does."""
    y = np.array(list(e[:4]), np.int32)
    if grey:
        r = g = b = y
    else:
        u = e[4] - 256 if e[4] > 127 else e[4]
        v = e[5] - 256 if e[5] > 127 else e[5]
        half = -((-u) // 2) if u < 0 else u // 2
        r, g, b = y + v * 2, y - half - v, y + u * 2
    return np.clip(np.stack([r, g, b], 1), 0, 255)


class Decoder:
    def __init__(self):
        self.pic = np.zeros((H, W, 3), np.int32)
        # per strip index, as the format has it (the strip rule makes the Saturn's single codebook equivalent)
        self.v4 = np.zeros((32, 256, 4, 3), np.int32)
        self.v1 = np.zeros((32, 256, 4, 3), np.int32)

    def codebook(self, s, cid, d, i, end, sent):
        book = self.v4 if (cid & 2) == 0 else self.v1
        n = 4 if cid & 4 else 6
        flag = mask = 0
        for k in range(256):
            if cid & 1:
                mask >>= 1
                if not mask:
                    if i + 4 > end:
                        return
                    flag, mask = struct.unpack_from('>I', d, i)[0], 0x80000000
                    i += 4
                if not flag & mask:
                    continue
            if i + n > end:
                return
            book[s, k] = entry_rgb(d[i:i + n], n == 4)
            sent.add(k)
            i += n

    def vectors(self, s, cid, d, i, end, top, height, used4, used1):
        flag = mask = 0
        for y in range(top, top + height, 4):
            for x in range(0, W, 4):
                if cid & 1:
                    mask >>= 1
                    if not mask:
                        if i + 4 > end:
                            return
                        flag, mask = struct.unpack_from('>I', d, i)[0], 0x80000000
                        i += 4
                    if not flag & mask:
                        continue
                v4 = False
                if not cid & 2:
                    mask >>= 1
                    if not mask:
                        if i + 4 > end:
                            return
                        flag, mask = struct.unpack_from('>I', d, i)[0], 0x80000000
                        i += 4
                    v4 = bool(flag & mask)
                if v4:
                    if i + 4 > end:
                        return
                    a, b, c, e = (self.v4[s, d[i + j]] for j in range(4))
                    used4.update(d[i:i + 4])
                    i += 4
                    self.pic[y, x:x + 2], self.pic[y, x + 2:x + 4] = a[0:2], b[0:2]
                    self.pic[y + 1, x:x + 2], self.pic[y + 1, x + 2:x + 4] = a[2:4], b[2:4]
                    self.pic[y + 2, x:x + 2], self.pic[y + 2, x + 2:x + 4] = c[0:2], e[0:2]
                    self.pic[y + 3, x:x + 2], self.pic[y + 3, x + 2:x + 4] = c[2:4], e[2:4]
                else:
                    if i >= end:
                        return
                    q = self.v1[s, d[i]]
                    used1.add(d[i])
                    i += 1
                    for r, (c0, c1) in enumerate(((0, 1), (0, 1), (2, 3), (2, 3))):
                        self.pic[y + r, x:x + 2] = q[c0]
                        self.pic[y + r, x + 2:x + 4] = q[c1]

    def frame(self, d):
        """Decodes one frame over the last; returns the strip rule's breaches (strip, which book, entries)."""
        flags, strips = d[0], be16(d, 8)
        i, y0, breaches = 10, 0, []
        for s in range(strips):
            end = i + be24(d, i + 1)
            top, bottom = be16(d, i + 4), be16(d, i + 8)
            if not top:
                top, bottom = y0, bottom + y0
            y0 = bottom
            if s > 0 and not flags & 1:
                self.v4[s], self.v1[s] = self.v4[s - 1], self.v1[s - 1]
            sent4, sent1, used4, used1 = set(), set(), set(), set()
            c = i + 12
            while c + 4 <= end:
                cid, cend = d[c], c + be24(d, c + 1)
                if 0x20 <= cid <= 0x27:
                    self.codebook(s, cid, d, c + 4, cend, sent4 if (cid & 2) == 0 else sent1)
                elif 0x30 <= cid <= 0x32:
                    self.vectors(s, cid, d, c + 4, cend, top, bottom - top, used4, used1)
                    break
                c = cend
            if used4 - sent4 or used1 - sent1:
                breaches.append((s, sorted(used4 - sent4)[:4], sorted(used1 - sent1)[:4]))
            i = end
        return breaches


def pixels32(pic):
    """(H, W, 3) RGB as the Saturn's 24-bit pixels, 0x80000000 | B << 16 | G << 8 | R."""
    p = pic.astype(np.uint32)
    return OPAQUE | (p[..., 2] << 16) | (p[..., 1] << 8) | p[..., 0]


def checksum(pic):
    """saturn/cinepak's bench checksum: sum of pixel * (index + 1), mod 2^32."""
    p = pixels32(pic).ravel().astype(np.uint64)
    return int((p * np.arange(1, p.size + 1, dtype=np.uint64)).sum() & 0xFFFFFFFF)


# --- commands -------------------------------------------------------------------------------------------

CHECK_CLIPS = ['CAFE:600:150:1:16', 'PRISON:250:150:1:8', 'CANYON:1400:150:1:16', 'CANYON:1400:150:4:8']


def cmd_check(args):
    vids = mdec.videos(args.track)
    bad = 0
    for spec in args.clips:
        name, start, count, q, strips = spec.split(':')
        start, count = int(start), int(count)
        frames, theirs = encode(args.ffmpeg, ps1_frames(args.ffmpeg, vids[name], start, count), int(q), int(strips))
        dec = Decoder()
        worst, breaches = 0, 0
        for k, f in enumerate(frames):
            b = dec.frame(f)
            breaches += len(b)
            if b and breaches <= 3:
                print(f'   {name} frame {k}: strip rule broken {b}')
            worst = max(worst, int(np.abs(dec.pic - theirs[k]).max()))
        kbs = sum(map(len, frames)) / count * 15 / 1024
        ok = worst == 0 and breaches == 0
        bad += not ok
        print(f'{spec:22s} {count} frames {kbs:6.1f} KB/s | vs ffmpeg: max diff {worst} | strip rule breaches '
              f'{breaches} | {"ok" if ok else "FAIL"}', flush=True)
    return 1 if bad else 0


# Sequences for cpkbench (all of them in the Saturn's 1 MB of high work RAM): each the heaviest run of a
# keyframe and the frames after it, from a clip encoded at a setting. The noise sequence is the worst a frame
# can be: every block V4, both codebooks full, in 32 strips.
BENCH = [('CANYON q1 16s', 'CANYON', 1400, 150, 1, 16),
         ('CANYON q2 8s', 'CANYON', 1400, 150, 2, 8),
         ('CAFE q1 16s', 'CAFE', 600, 150, 1, 16)]
GOP = 12
RUN = 6
NOISE_FRAMES = 2


def heaviest_run(frames):
    best = max(range(0, len(frames) - RUN + 1, GOP), key=lambda s: sum(map(len, frames[s:s + RUN])))
    return best, frames[best:best + RUN]


def cmd_benchframes(args):
    vids = mdec.videos(args.track)
    seqs = []
    for label, name, start, count, q, strips in BENCH:
        frames, _ = encode(args.ffmpeg, ps1_frames(args.ffmpeg, vids[name], start, count), q, strips)
        mean = sum(map(len, frames)) / count * 15 / 1024
        at, run = heaviest_run(frames)
        seqs.append((label, run, mean, f'{name} frames {start + at}-{start + at + RUN - 1}'))
    rng = np.random.default_rng(1)
    noise = rng.integers(0, 256, (NOISE_FRAMES, H, W, 3), dtype=np.uint8)
    frames, _ = encode(args.ffmpeg, noise, 1, 32, gop=1)
    seqs.append(('noise q1 32s', frames, sum(map(len, frames)) / len(frames) * 15 / 1024, 'random pixels, all keyframes'))

    blob, table, entries = bytearray(), [], []
    for label, run, mean, what in seqs:
        dec, sums = Decoder(), []
        for f in run:
            assert not dec.frame(f), f'{label}: strip rule broken'
            sums.append(checksum(dec.pic))
            table.append((len(blob), len(f), max(be16(f, 8), 0)))
            blob += f + bytes(-len(f) % 4)
        total = sum(sums) & 0xFFFFFFFF
        entries.append((label, len(table) - len(run), len(run), round(mean), sum(map(len, run)), total, what))
        print(f'{label:14s} {what:28s} clip mean {mean:6.1f} KB/s | this run {sum(map(len, run)) / len(run) * 15 / 1024:6.1f}'
              f' KB/s, largest frame {max(map(len, run))} B | checksum {total:08X}')

    with open(args.out, 'w') as f:
        f.write('// Written by tools/cinepak.py benchframes: Cinepak sequences for cpkbench, their checksums from the\n'
                '// PC decoder (checked against ffmpeg\'s). Not for the repository.\n\n')
        f.write(f'#define CPK_FRAMES {len(table)}\n#define CPK_SEQS {len(entries)}\n\n')
        f.write('static const uint8_t kCpkData[] __attribute__((aligned(4))) = {\n')
        for i in range(0, len(blob), 24):
            f.write('    ' + ','.join(str(b) for b in blob[i:i + 24]) + ',\n')
        f.write('};\n\n// offset, bytes, strips\nstatic const uint32_t kCpkFrame[CPK_FRAMES][3] = {\n')
        for t in table:
            f.write(f'    {{ {t[0]}, {t[1]}, {t[2]} }},\n')
        f.write('};\n\nstruct CpkSeq { const char* name; int32_t first, count, clipKbs, bytes; uint32_t checksum; };\n')
        f.write('static const CpkSeq kCpkSeq[CPK_SEQS] = {\n')
        for e in entries:
            f.write(f'    {{ "{e[0]}", {e[1]}, {e[2]}, {e[3]}, {e[4]}, 0x{e[5]:08X} }},  // {e[6]}\n')
        f.write('};\n')
    print(f'{args.out}: {len(table)} frames, {len(blob)} bytes')
    return 0


# --- whole videos ---------------------------------------------------------------------------------------

PICTURE = {'CORELOGO': (208, 0)}    # lines and first line of the PS1 frame's picture; the rest are 120 from 44


def picture_crop(name):
    lines, top = PICTURE.get(name, (H, TOP))
    return f'crop={W}:{lines}:0:{top}'


def encode_video(job):
    """One video at one setting: (name, setting, frame sizes, PSNR of Y, U, V against the PS1's picture)."""
    import json
    import re
    ffdir, raw_path, name, setting, outdir = job
    denoise = setting.startswith('d')       # dq1s16: the PS1 picture's flicker from frame to frame smoothed first
    q, strips = (int(x) for x in setting.lstrip('d')[1:].split('s'))
    avi = os.path.join(outdir, f'{name}_{setting}.avi')
    crop = picture_crop(name)
    filters = crop + (',atadenoise' if denoise else '')
    subprocess.run([os.path.join(ffdir, 'ffmpeg'), '-v', 'error', '-y', '-f', 'psxstr', '-i', raw_path, '-an',
                    '-vf', filters, '-fps_mode', 'passthrough', '-c:v', 'cinepak', '-q:v', str(q), '-max_strips',
                    str(strips), '-max_extra_cb_iterations', '4', '-g', '12', avi], check=True, capture_output=True)
    sizes = [int(x) for x in subprocess.run(
        [os.path.join(ffdir, 'ffprobe'), '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'packet=size',
         '-of', 'csv=p=0', avi], capture_output=True, text=True, check=True).stdout.split()]
    log = subprocess.run([os.path.join(ffdir, 'ffmpeg'), '-v', 'info', '-i', avi, '-f', 'psxstr', '-i', raw_path,
                          '-lavfi', f'[1:v]{crop}[ref];[0:v][ref]psnr', '-f', 'null', '-'],
                         capture_output=True, text=True).stderr
    m = re.search(r'PSNR y:([\d.inf]+) u:([\d.inf]+) v:([\d.inf]+)', log)
    psnr = [float(x) for x in m.groups()] if m else None
    with open(os.path.join(outdir, f'{name}_{setting}.json'), 'w') as f:
        json.dump({'name': name, 'setting': setting, 'sizes': sizes, 'psnr': psnr}, f)
    os.remove(avi)
    return name, setting, sizes, psnr


def cmd_encode(args):
    from concurrent.futures import ProcessPoolExecutor
    vids = mdec.videos(args.track)
    os.makedirs(args.out, exist_ok=True)
    jobs = []
    for name in args.videos or sorted(vids):
        raw_path = os.path.join(args.out, f'{name}.str')
        with open(raw_path, 'wb') as f:
            f.write(vids[name])
        for setting in args.settings:
            if not os.path.exists(os.path.join(args.out, f'{name}_{setting}.json')):
                jobs.append((args.ffmpeg, raw_path, name, setting, args.out))
    # the longest first, so the pool ends together
    jobs.sort(key=lambda j: -os.path.getsize(j[1]))
    with ProcessPoolExecutor(args.jobs) as pool:
        for name, setting, sizes, psnr in pool.map(encode_video, jobs):
            kbs = sum(sizes) / len(sizes) * 15 / 1024
            print(f'{name:9s} {setting:6s} {len(sizes):5d} frames {kbs:6.1f} KB/s  PSNR {psnr}', flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check')
    c.add_argument('track')
    c.add_argument('--clips', nargs='+', default=CHECK_CLIPS)
    b = sub.add_parser('benchframes')
    b.add_argument('track')
    b.add_argument('out')
    e = sub.add_parser('encode')
    e.add_argument('track')
    e.add_argument('out')
    e.add_argument('--settings', nargs='+', default=['q1s16', 'q1s8', 'q2s8'])
    e.add_argument('--videos', nargs='+')
    e.add_argument('--jobs', type=int, default=4)
    for p in (c, b, e):
        p.add_argument('--ffmpeg', default=os.environ.get('FFMPEG_DIR', ''))
    args = ap.parse_args()
    return {'check': cmd_check, 'benchframes': cmd_benchframes, 'encode': cmd_encode}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
