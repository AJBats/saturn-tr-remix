"""The FMVs for the Saturn, from the PlayStation's pictures and the retail Saturn's sound (docs/ps1-fmv.md,
"The Cinepak route: what gets built").

    fmvpack.py build PS1TRACK SATFMV OUTDIR [--videos NAME ...] [--jobs N]
    fmvpack.py check OUTDIR [--jobs N]

build: each video's PS1 picture (its flicker averaged out by ffmpeg's atadenoise, over the whole video) encoded
by ffmpeg's Cinepak at every setting of LADDER, each group of 12 frames on its own: so each group starts with a
keyframe, and groups from different settings join (ffmpeg's encoder places its own keyframes: after the
first, 17-21 frames on, then each 12, whatever -g, -keyint_min or -force_key_frames say); the retail Saturn CPK's sound (SATFMV/NAME.CPK, 16-bit PCM) packed by
tools/lossless.py; each 12-frame group's setting planned against a Form 2 drive (tools/fmvplan.py); the
stream laid out. Writes OUTDIR/FMV.TRK, the Form 2 track's data (tools/mkdisc.py --form2), and
OUTDIR/INDEX.BIN, its table for the data track. The encodes stay in OUTDIR/cache, so a second build only plans
and lays out again.

check: plays every stream on the PC as the Saturn will: each frame decoded (tools/cinepak.py's decoder) and
compared with ffmpeg's decode of the setting it came from, pixel for pixel; keyframes checked to code every
block (so groups from different settings join); the strip rule checked; the sound decoded and compared with
the retail PCM sample for sample; the drive simulated over the stream's real layout, sector by sector, with
nothing late. Also each video's luma PSNR against the PS1 picture.

A stream (all numbers big-endian), from a sector's start:
    header, 64 bytes: "TRSV", u16 version 1, u16 width, u16 height, u16 fps (15), u32 frames, u32 sound
        chunks, u16 samples a chunk (1024), u16 sample rate (22254), u16 preroll (ms), u16 0,
        u32 the largest frame's bytes, then zeros
    items, in the order the player needs them, each 4-byte aligned:
        u8 kind (1 a frame, 2 a sound chunk, 0 the end), u24 bytes, u32 its number (frame or chunk), the bytes
        (a frame: Cinepak, a keyframe each 12; a chunk: tools/lossless.py's), zeros to 4 bytes
The track: TRACK_LEAD sectors of zeros (a read from a track's first sectors lost 37 on the Saturn's
Satiator), then each stream from a sector's start. Its sectors carry 2324 bytes each (Mode 2 Form 2).

INDEX.BIN: "TRSI", u16 version 1, u16 videos, then per video 32 bytes: its name (8 bytes, zero-padded),
u32 first sector (from the track's start), u32 sectors, u32 frames, u16 width, u16 height, u32 sound chunks,
u32 0.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cinepak  # noqa: E402
import fmvplan  # noqa: E402
import lossless  # noqa: E402
import mdec  # noqa: E402

LADDER = ['q1s16', 'q1s8', 'q2s8', 'q4s8', 'q8s8', 'q16s8', 'q32s8']    # best first
# videos made from the retail Saturn disc's own pictures (for side-by-side tests, named with --videos): the
# CPK they come from and their lines. Its Cinepak can't be copied as it is: Sega's encoder lets a strip use
# codebook entries sent in earlier frames, which the Saturn decoder here doesn't keep.
RETAIL = {'CORERTL': ('CORELOGO', 200)}


def lines_of(name):
    if name in RETAIL:
        return RETAIL[name][1]
    return cinepak.PICTURE.get(name, (cinepak.H, cinepak.TOP))[0]


def source_cpk(satfmv, name):
    return os.path.join(satfmv, (RETAIL[name][0] if name in RETAIL else name) + '.CPK')
GOP = 12
FPS = 15
RATE = 22254
SECTOR = 2324
TRACK_LEAD = 150
DRIVE = 2324 * 150          # Form 2 at 2x
BUFFER = 1024 * 1024        # the RAM ring and the CD block's sectors
PREROLL = 1.0
HEADER = 64
ITEM = 8
KIND_END, KIND_FRAME, KIND_SOUND = 0, 1, 2


def ff(ffdir, tool='ffmpeg'):
    return os.path.join(ffdir, tool)


def item_bytes(payload):
    return ITEM + (payload + 3) // 4 * 4


# --- build ----------------------------------------------------------------------------------------------

def smoothed(ffdir, raw, name, cache):
    """The video's PS1 picture with its flicker averaged out (a RETAIL video's picture as it is), as raw RGB
    frames in the cache: its path."""
    path = os.path.join(cache, f'{name}.rgb')
    if not os.path.exists(path):
        if name in RETAIL:
            source = ['-i', raw, '-an']
        else:
            source = ['-f', 'psxstr', '-i', raw, '-an', '-vf', cinepak.picture_crop(name) + ',atadenoise']
        subprocess.run([ff(ffdir), '-v', 'error', '-y'] + source + ['-fps_mode', 'passthrough',
                        '-f', 'rawvideo', '-pix_fmt', 'rgb24', path + '.part'], check=True, capture_output=True)
        os.replace(path + '.part', path)
    return path


def encode(job):
    """One video at one setting, a group at a time: the groups joined in one AVI in the cache, its frames' bytes
    beside it."""
    import tempfile
    ffdir, rgb, name, lines, setting, cache = job
    q, strips = (int(x) for x in setting[1:].split('s'))
    avi = os.path.abspath(os.path.join(cache, f'{name}_{setting}.avi'))
    sizes_path = avi[:-4] + '.json'
    if os.path.exists(avi) and os.path.exists(sizes_path):
        return name, setting
    frame = cinepak.W * lines * 3
    pictures = np.memmap(rgb, np.uint8, 'r')
    n = len(pictures) // frame
    with tempfile.TemporaryDirectory() as t:
        parts = []
        for g in range(0, n, GOP):
            part = os.path.join(t, f'{g:06d}.avi')
            group = pictures[g * frame:min(n, g + GOP) * frame]
            subprocess.run([ff(ffdir), '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                            '-s', f'{cinepak.W}x{lines}', '-r', str(FPS), '-i', '-', '-c:v', 'cinepak',
                            '-q:v', str(q), '-max_strips', str(strips), '-max_extra_cb_iterations', '4',
                            '-g', str(GOP * 2), part], input=bytes(group), check=True, capture_output=True)
            parts.append(part)
        listing = os.path.join(t, 'parts.txt')
        with open(listing, 'w') as f:
            f.write(''.join(f"file '{os.path.basename(x)}'\n" for x in parts))
        subprocess.run([ff(ffdir), '-v', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', listing, '-c', 'copy',
                        avi + '.part.avi'], check=True, capture_output=True, cwd=t)
    sizes = [int(x) for x in subprocess.run(
        [ff(ffdir, 'ffprobe'), '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'packet=size',
         '-of', 'csv=p=0', avi + '.part.avi'], capture_output=True, text=True, check=True).stdout.split()]
    assert len(sizes) == n, f'{name} {setting}: {len(sizes)} frames joined of {n}'
    os.replace(avi + '.part.avi', avi)
    json.dump(sizes, open(sizes_path, 'w'))
    return name, setting


def frames_of(ffdir, avi):
    """An AVI's Cinepak frames, as bytes each."""
    sizes = json.load(open(avi[:-4] + '.json'))
    data = subprocess.run([ff(ffdir), '-v', 'error', '-i', avi, '-c:v', 'copy', '-f', 'rawvideo', '-'],
                          capture_output=True, check=True).stdout
    out, p = [], 0
    for s in sizes:
        out.append(data[p:p + s])
        p += s
    assert p == len(data), avi
    return out


def sound_of(ffdir, cpk, frames):
    """The retail sound, packed: its chunks (bytes each), the PCM cut or padded with silence to cover the frames."""
    pcm = lossless.load(cpk, ffdir) if os.path.exists(cpk) else np.zeros((0, 2), np.int16)
    chunks = -(-frames * RATE // FPS // lossless.CHUNK)
    want = chunks * lossless.CHUNK
    full = np.zeros((want, 2), np.int16)
    n = min(want, len(pcm))
    full[:n] = pcm[:n]
    enc = lossless.encode(full)
    out, p = [], 0
    while p < len(enc):
        size = struct.unpack_from('>H', enc, p)[0]
        out.append(enc[p:p + size])
        p += size
    return out, full


def layout(frames, chunks):
    """The stream's items in order: (deadline, kind, number, payload)."""
    items = [(PREROLL + k / FPS, KIND_FRAME, k, f) for k, f in enumerate(frames)]
    t = lossless.CHUNK / RATE
    items += [(PREROLL + j * t - fmvplan.SOUND_LEAD, KIND_SOUND, j, c) for j, c in enumerate(chunks)]
    items.sort(key=lambda x: (x[0], x[1]))
    return items


def stream_bytes(width, height, frames, chunks):
    items = layout(frames, chunks)
    head = struct.pack('>4sHHHHIIHHHHI', b'TRSV', 1, width, height, FPS, len(frames), len(chunks),
                       lossless.CHUNK, RATE, int(PREROLL * 1000), 0, max(len(f) for f in frames))
    out = bytearray(head.ljust(HEADER, b'\0'))
    for _, kind, number, payload in items:
        out += struct.pack('>I', kind << 24 | len(payload)) + struct.pack('>I', number) + payload
        out += bytes(-len(payload) % 4)
    out += struct.pack('>II', KIND_END << 24, 0)
    return bytes(out)


def plan_video(sizes, chunk_sizes):
    """Each group's setting (an index into LADDER), planned with the stream's real item sizes."""
    fmvplan.PRE = PREROLL
    fmvplan.FRAME_OVERHEAD = 0
    fmvplan.SOUND_OVERHEAD = 0
    fmvplan.SETTINGS[:] = LADDER
    items = {s: [item_bytes(b) for b in sizes[s]] for s in LADDER}
    sound = [item_bytes(b) for b in chunk_sizes]
    level = fmvplan.plan(items, sound, DRIVE, BUFFER - HEADER)
    if level is None:
        sys.exit('fmvpack: late even all at the lowest setting')
    return level


def cmd_build(args):
    vids = mdec.videos(args.track)
    names = args.videos or sorted(vids)
    cache = os.path.join(args.out, 'cache')
    os.makedirs(cache, exist_ok=True)
    raws = {}
    for name in names:
        if name in RETAIL:
            raws[name] = source_cpk(args.satfmv, name)
            continue
        raws[name] = os.path.join(cache, f'{name}.str')
        if not os.path.exists(raws[name]) or os.path.getsize(raws[name]) != len(vids[name]):
            with open(raws[name], 'wb') as f:
                f.write(vids[name])
    with ProcessPoolExecutor(args.jobs) as pool:
        rgbs = dict(zip(names, pool.map(smoothed, [args.ffmpeg] * len(names), [raws[n] for n in names], names,
                                        [cache] * len(names))))
        jobs = [(args.ffmpeg, rgbs[n], n, lines_of(n), s, cache) for n in names for s in LADDER]
        jobs.sort(key=lambda j: -os.path.getsize(j[1]))
        for name, setting in pool.map(encode, jobs):
            pass

    track = bytearray(TRACK_LEAD * SECTOR)
    index, report = [], {}
    for name in names:
        sizes = {s: json.load(open(os.path.join(cache, f'{name}_{s}.json'))) for s in LADDER}
        n = len(sizes[LADDER[0]])
        assert all(len(v) == n for v in sizes.values()), name
        chunks, _ = sound_of(args.ffmpeg, source_cpk(args.satfmv, name), n)
        level = plan_video(sizes, [len(c) for c in chunks])
        frames_at = {}
        frames = []
        for k in range(n):
            s = LADDER[level[k // GOP]]
            if s not in frames_at:
                frames_at[s] = frames_of(args.ffmpeg, os.path.join(cache, f'{name}_{s}.avi'))
            frames.append(frames_at[s][k])
        lines = lines_of(name)
        data = stream_bytes(cinepak.W, lines, frames, chunks)
        first = len(track) // SECTOR
        sectors = -(-len(data) // SECTOR)
        track += data + bytes(sectors * SECTOR - len(data))
        index.append(struct.pack('>8sIIIHHII', name.encode(), first, sectors, n, cinepak.W, lines, len(chunks), 0))
        shares = [sum(1 for g in level if g == i) / len(level) for i in range(len(LADDER))]
        kbs = sectors * SECTOR / (n / FPS) / 1024
        report[name] = {'groups': [LADDER[g] for g in level], 'stream_kbs': kbs}
        print(f'{name:9s} {n:5d} frames, {sectors:6d} sectors ({kbs:5.1f} KB/s): ' +
              ' '.join(f'{s} {v * 100:.0f}%' for s, v in zip(LADDER, shares) if v), flush=True)

    with open(os.path.join(args.out, 'FMV.TRK'), 'wb') as f:
        f.write(track)
    with open(os.path.join(args.out, 'INDEX.BIN'), 'wb') as f:
        f.write(struct.pack('>4sHH', b'TRSI', 1, len(index)) + b''.join(index))
    json.dump(report, open(os.path.join(args.out, 'plan.json'), 'w'), indent=1)
    print(f'FMV.TRK: {len(track) // SECTOR} sectors ({len(track) / 1e6:.1f} MB); INDEX.BIN: {len(index)} videos')
    return 0


# --- check ----------------------------------------------------------------------------------------------

def read_index(out):
    data = open(os.path.join(out, 'INDEX.BIN'), 'rb').read()
    magic, version, count = struct.unpack_from('>4sHH', data, 0)
    assert magic == b'TRSI' and version == 1
    out = []
    for i in range(count):
        name, first, sectors, frames, w, h, chunks, _ = struct.unpack_from('>8sIIIHHII', data, 8 + 32 * i)
        out.append((name.rstrip(b'\0').decode(), first, sectors, frames, w, h, chunks))
    return out


def parse(data):
    """A stream's header fields and its items: (offset of the item's end, kind, number, payload)."""
    head = struct.unpack_from('>4sHHHHIIHHHHI', data, 0)
    assert head[0] == b'TRSV' and head[1] == 1, 'not a stream'
    items, p = [], HEADER
    while True:
        word, number = struct.unpack_from('>II', data, p)
        kind, size = word >> 24, word & 0xFFFFFF
        if kind == KIND_END:
            break
        payload = data[p + ITEM:p + ITEM + size]
        p += ITEM + (size + 3) // 4 * 4
        items.append((p, kind, number, payload))
    return head, items


def keyframe_whole(frame, height):
    """Whether a frame codes every block: a keyframe whose strips, all with every block coded, fill the height."""
    if frame[0] != 0:
        return False
    strips, d, y = cinepak.be16(frame, 8), 10, 0
    for _ in range(strips):
        end = d + cinepak.be24(frame, d + 1)
        top, bottom = cinepak.be16(frame, d + 4), cinepak.be16(frame, d + 8)
        y += bottom - top if top else bottom
        c, vectors = d + 12, None
        while c + 4 <= end:
            if 0x30 <= frame[c] <= 0x32:
                vectors = frame[c]
                break
            c += cinepak.be24(frame, c + 1)
        if vectors not in (0x30, 0x32):
            return False
        d = end
    return y == height


def drive_late(items, deadlines):
    """Items late when the drive reads the stream from its first sector into BUFFER, sector by sector (fmvplan's
    model over the real byte layout): (how many are late, the buffer's fullest)."""
    ends = [e for e, _, _, _ in items]
    assert all(a <= b for a, b in zip(deadlines, deadlines[1:])), 'the stream is not in deadline order'
    arrived, freed, step, i, late, fullest = 0, 0, 0, 0, 0, 0
    n = len(items)
    while i < n:
        t = step / 150
        while i < n and deadlines[i] <= t:
            if arrived < ends[i]:
                late += 1
            freed = ends[i]
            i += 1
        if i < n and arrived < ends[-1] and arrived - freed + SECTOR <= BUFFER:
            arrived += SECTOR
            fullest = max(fullest, arrived - freed)
        step += 1
    return late, fullest


def check_video(job):
    ffdir, out, name, first, sectors, frames_n, w, h, chunks_n, groups, satfmv = job
    with open(os.path.join(out, 'FMV.TRK'), 'rb') as f:
        f.seek(first * SECTOR)
        data = f.read(sectors * SECTOR)
    head, items = parse(data)
    problems = []
    if (head[2], head[3], head[5], head[6]) != (w, h, frames_n, chunks_n):
        problems.append('header and index differ')

    # the planned frames, as ffmpeg decodes each setting's encode
    cache = os.path.join(out, 'cache')
    pipes = {}
    for s in sorted(set(groups)):
        pipes[s] = subprocess.Popen([ff(ffdir), '-v', 'error', '-i', os.path.join(cache, f'{name}_{s}.avi'),
                                     '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
    if name in RETAIL:
        source = ['-i', source_cpk(satfmv, name), '-an']
    else:
        source = ['-f', 'psxstr', '-i', os.path.join(cache, f'{name}.str'), '-an', '-vf', cinepak.picture_crop(name)]
    ps1 = subprocess.Popen([ff(ffdir), '-v', 'error'] + source + ['-fps_mode', 'passthrough', '-f', 'rawvideo',
                            '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
    cinepak.H = h       # the decoder's picture
    dec = cinepak.Decoder()
    dec.pic = np.zeros((h, w, 3), np.int32)
    frame_bytes = w * h * 3
    mse, frames_seen, chunks, mismatched, breaches, not_whole = 0.0, 0, [], 0, 0, 0
    deadlines = []
    for end, kind, number, payload in items:
        if kind == KIND_FRAME:
            deadlines.append(PREROLL + number / FPS)
            if number != frames_seen:
                problems.append(f'frame {number} out of order')
            setting = groups[number // GOP]
            if number % GOP == 0 and not keyframe_whole(payload, h):
                not_whole += 1
            breaches += len(dec.frame(payload))
            ref = {s: np.frombuffer(p.stdout.read(frame_bytes), np.uint8) for s, p in pipes.items()}
            want = ref[setting].reshape(h, w, 3)
            if not np.array_equal(dec.pic, want):
                mismatched += 1
            src = np.frombuffer(ps1.stdout.read(frame_bytes), np.uint8).reshape(h, w, 3)
            y = lambda a: a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114  # noqa: E731
            mse += float(np.mean((y(dec.pic.astype(np.float64)) - y(src.astype(np.float64))) ** 2))
            frames_seen += 1
        else:
            deadlines.append(PREROLL + number * lossless.CHUNK / RATE - fmvplan.SOUND_LEAD)
            chunks.append(payload)
    for p in list(pipes.values()) + [ps1]:
        p.stdout.close()
        p.wait()

    sound = lossless.decode(b''.join(chunks))
    _, want = sound_of(ffdir, source_cpk(satfmv, name), frames_n)
    sound_ok = sound.shape == want.shape and np.array_equal(sound, want)
    late, fullest = drive_late(items, deadlines)
    if frames_seen != frames_n:
        problems.append(f'{frames_seen} frames of {frames_n}')
    if mismatched:
        problems.append(f'{mismatched} frames differ from ffmpeg')
    if breaches:
        problems.append(f'strip rule broken {breaches} times')
    if not_whole:
        problems.append(f'{not_whole} group starts are not whole keyframes')
    if not sound_ok:
        problems.append('sound differs from the retail PCM')
    if late:
        problems.append(f'{late} items late')
    psnr = 10 * np.log10(255 ** 2 / (mse / max(1, frames_seen)))
    return name, frames_seen, len(chunks), psnr, fullest, problems


def cmd_check(args):
    plan = json.load(open(os.path.join(args.out, 'plan.json')))
    jobs = [(args.ffmpeg, args.out, name, first, sectors, frames, w, h, chunks, plan[name]['groups'], args.satfmv)
            for name, first, sectors, frames, w, h, chunks in read_index(args.out)]
    bad = 0
    with ProcessPoolExecutor(args.jobs) as pool:
        for name, frames, chunks, psnr, fullest, problems in pool.map(check_video, jobs):
            bad += bool(problems)
            print(f'{name:9s} {frames:5d} frames, {chunks:4d} sound chunks, luma {psnr:5.2f} dB against the PS1, '
                  f'buffer at most {fullest // 1024} KB: {"ok" if not problems else "; ".join(problems)}', flush=True)
    print('all ok' if not bad else f'{bad} videos with problems')
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('track')
    b.add_argument('satfmv')
    b.add_argument('out')
    b.add_argument('--videos', nargs='+')
    c = sub.add_parser('check')
    c.add_argument('out')
    c.add_argument('satfmv')
    for p in (b, c):
        p.add_argument('--jobs', type=int, default=8)
        p.add_argument('--ffmpeg', default=os.environ.get('FFMPEG_DIR', ''))
    args = ap.parse_args()
    return {'build': cmd_build, 'check': cmd_check}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
