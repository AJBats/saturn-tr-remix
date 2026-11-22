"""Lossless sound for the FMVs (docs/ps1-fmv.md): the retail Saturn's 16-bit PCM, every sample kept, in about
73% of the bytes, laid out for the sound CPU (a 68000 at 11.3 MHz with sound RAM's waits: about 27 cycles a
bit pulled from a bit stream) to decode with a table lookup a sample and no bit loops (saturn/fmvsound.68k).

Each channel is predicted from its last samples (fixed predictors of order 0-3, chosen per block of 128
samples, 16-bit wrapping arithmetic) and the residual zigzagged to u (0-65535). Each block codes u one of
four ways (its mode), chosen for the fewest bytes:
    0  u < 192: a byte; else the byte 192 or more, then u whole (2 bytes)
    1  u < 768: a 2-bit field u >> 8 and a byte; else the field 3, then u whole
    2  u < 3072: a 4-bit field u >> 8 and a byte; else a field of 12 or more, then u whole
    3  u whole (2 bytes)
Bytes go to a byte stream, fields to a field stream (from each byte's top: 4 fields of 2 bits, or 2 of 4).
Plain ("verbatim") blocks cost 16 bits a sample, so a block never costs more than the PCM it holds.

A chunk holds 1024 samples of each channel: the left's 8 blocks, then the right's:
    u16 bytes in the chunk (all of it, a multiple of 2)
    u16 bytes in the byte stream (n)
    byte stream: per block a header byte (order << 3 | mode), then its samples' bytes
    a pad byte if n is odd
    field stream: the rest
The predictors' last samples carry from block to block and chunk to chunk, from zeros at the start.

    lossless.py check RAW|CPK [...]                 round trip and size (s16be stereo files or retail FMVs)
    lossless.py benchclip RAW|CPK START SECONDS OUT.h   a clip packed for lossbench, its checksums
"""
import argparse
import struct
import sys

import numpy as np

BLOCK = 128
RING_SAMPLES = 32768        # each channel's ring in sound RAM (fmvsound.68k)
CHUNK_BLOCKS = 8
CHUNK = BLOCK * CHUNK_BLOCKS
# per mode: the field's bits, u's limit before an escape, a sample's bits plain and escaped
MODES = [(0, 192, 8, 24), (2, 768, 10, 18), (4, 3072, 12, 20)]


def predict(h, order):
    """The prediction from the last three samples h = (x1, x2, x3), 16-bit wrapped."""
    x1, x2, x3 = h
    return (0, x1, 2 * x1 - x2, 3 * x1 - 3 * x2 + x3)[order] & 0xFFFF


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def unzig(u):
    return (u >> 1) ^ -(u & 1)


def residuals_np(x, hist, order):
    """Residuals of block x (int64 array) after history hist (x1, x2, x3), as uint16 zigzag."""
    full = np.concatenate([np.array(hist[::-1], np.int64), x])
    x0 = full[3:]
    x1, x2, x3 = full[2:-1], full[1:-2], full[:-3]
    pred = (np.zeros_like(x0), x1, 2 * x1 - x2, 3 * x1 - 3 * x2 + x3)[order]
    r = ((x0 - pred + 0x8000) & 0xFFFF) - 0x8000
    return ((r << 1) ^ (r >> 15)) & 0xFFFF


def mode_bits(u, mode):
    if mode == 3:
        return 16 * len(u)
    _, limit, plain, escaped = MODES[mode]
    return int(np.where(u < limit, plain, escaped).sum())


def encode(pcm):
    """s16 stereo (n, 2) array, n a multiple of CHUNK: the chunks, as bytes."""
    hist = [[0, 0, 0], [0, 0, 0]]
    out = bytearray()
    for c0 in range(0, len(pcm), CHUNK):
        bs, fs = bytearray(), bytearray()
        for ch in (0, 1):
            for b in range(c0, c0 + CHUNK, BLOCK):
                x = pcm[b:b + BLOCK, ch].astype(np.int64)
                best = None
                for order in range(4):
                    u = residuals_np(x, hist[ch], order)
                    for mode in range(4):
                        n = mode_bits(u, mode)
                        if best is None or n < best[0]:
                            best = (n, order, mode, u)
                _, order, mode, u = best
                bs.append(order << 3 | mode)
                fields = []
                for v in u.tolist():
                    if mode == 3:
                        bs += struct.pack('>H', v)
                        continue
                    width, limit, _, _ = MODES[mode]
                    if mode == 0:
                        if v < limit:
                            bs.append(v)
                        else:
                            bs.append(0xFF)
                            bs += struct.pack('>H', v)
                        continue
                    if v < limit:
                        fields.append(v >> 8)
                        bs.append(v & 0xFF)
                    else:
                        fields.append((1 << width) - 1)
                        bs += struct.pack('>H', v)
                if mode == 1:
                    for i in range(0, BLOCK, 4):
                        f = fields[i:i + 4]
                        fs.append(f[0] << 6 | f[1] << 4 | f[2] << 2 | f[3])
                elif mode == 2:
                    for i in range(0, BLOCK, 2):
                        fs.append(fields[i] << 4 | fields[i + 1])
                h = x.tolist()
                hist[ch] = [h[-1], h[-2], h[-3]]
        body = struct.pack('>H', len(bs)) + bytes(bs) + (b'\0' if len(bs) & 1 else b'') + bytes(fs)
        if len(body) & 1:
            body += b'\0'
        out += struct.pack('>H', len(body) + 2) + body
    return bytes(out)


def decode(data):
    """The chunks back to s16 stereo (n, 2), as fmvsound.68k decodes them."""
    hist = [[0, 0, 0], [0, 0, 0]]
    chans = [[], []]
    p = 0
    while p < len(data):
        total, nbytes = struct.unpack_from('>HH', data, p)
        bp = p + 4
        fp = p + 4 + nbytes + (nbytes & 1)
        for ch in (0, 1):
            for _ in range(CHUNK_BLOCKS):
                head = data[bp]
                bp += 1
                order, mode = head >> 3, head & 3
                for i in range(BLOCK):
                    if mode == 3:
                        u = struct.unpack_from('>H', data, bp)[0]
                        bp += 2
                    elif mode == 0:
                        u = data[bp]
                        bp += 1
                        if u >= 192:
                            u = struct.unpack_from('>H', data, bp)[0]
                            bp += 2
                    else:
                        if mode == 1:
                            f = (data[fp] >> (6 - 2 * (i & 3))) & 3
                            if i & 3 == 3:
                                fp += 1
                            esc = f == 3
                        else:
                            f = (data[fp] >> (4 - 4 * (i & 1))) & 15
                            if i & 1:
                                fp += 1
                            esc = f >= 12
                        if esc:
                            u = struct.unpack_from('>H', data, bp)[0]
                            bp += 2
                        else:
                            u = f << 8 | data[bp]
                            bp += 1
                    x = (predict(hist[ch], order) + unzig(u)) & 0xFFFF
                    hist[ch] = [x, hist[ch][0], hist[ch][1]]
                    chans[ch].append(s16(x))
        p += total
    return np.array(chans, np.int16).T


def load(raw, ffdir=''):
    """s16 stereo from a raw s16be file, or from a retail Saturn FMV (.CPK) through ffmpeg."""
    if raw.upper().endswith('.CPK'):
        import os
        import subprocess
        data = subprocess.run([os.path.join(ffdir, 'ffmpeg'), '-v', 'error', '-i', raw, '-vn', '-f', 's16be',
                               '-acodec', 'pcm_s16be', '-'], capture_output=True, check=True).stdout
    else:
        data = open(raw, 'rb').read()
    a = np.frombuffer(data, '>i2').reshape(-1, 2)
    return a[:len(a) // CHUNK * CHUNK]


def checksum(samples):
    """lossbench's: sum of each 16-bit sample (as unsigned) times its index + 1, mod 2^32."""
    s = samples.astype(np.int64).ravel() & 0xFFFF
    return int((s * np.arange(1, s.size + 1, dtype=np.int64)).sum() & 0xFFFFFFFF)


def cmd_check(args):
    tot_pcm = tot_enc = 0
    for raw in args.raw:
        pcm = load(raw)
        enc = encode(pcm)
        ok = np.array_equal(decode(enc), pcm) if args.decode else None
        tot_pcm += pcm.size * 2
        tot_enc += len(enc)
        print(f'{raw}: {len(pcm) / 22254:6.1f} s, {len(enc) / (pcm.size * 2) * 100:5.1f}% '
              f'({len(enc) / len(pcm) * 22254 / 1024:5.1f} KB/s){"" if ok is None else "  round trip ok" if ok else "  ROUND TRIP FAILED"}',
              flush=True)
    print(f'all: {tot_enc / tot_pcm * 100:.1f}%')
    return 0


def cmd_benchclip(args):
    pcm = load(args.raw, args.ffmpeg)
    rate = 22254
    start = int(args.start * rate) // CHUNK * CHUNK
    n = int(args.seconds * rate) // CHUNK * CHUNK
    clip = pcm[start:start + n]
    enc = encode(clip)
    assert np.array_equal(decode(enc), clip)
    chunks, p = [], 0
    while p < len(enc):
        chunks.append(p)
        p += struct.unpack_from('>H', enc, p)[0]
    # the speed test decodes the first SPEED_CHUNKS chunks: each channel's samples, left then right
    speed = min(len(chunks), args.speed_chunks)
    part = clip[:speed * CHUNK]
    sums = (checksum(part[:, 0]), checksum(part[:, 1]))
    # the rings after the whole clip has played: each of their places holds the last sample decoded there
    last = np.arange(n - RING_SAMPLES, n)
    rings = np.zeros((RING_SAMPLES, 2), np.int16)
    rings[last % RING_SAMPLES] = clip[last]
    play_sums = (checksum(rings[:, 0]), checksum(rings[:, 1]))
    with open(args.out, 'w') as f:
        f.write('// Written by tools/lossless.py benchclip: a clip of FMV sound for lossbench. Not for the repository.\n')
        f.write(f'#define CLIP_CHUNKS {len(chunks)}\n#define CLIP_BYTES {len(enc)}\n')
        f.write(f'#define SPEED_CHUNKS {speed}\n#define SPEED_SUM_LEFT 0x{sums[0]:08X}u\n#define SPEED_SUM_RIGHT 0x{sums[1]:08X}u\n')
        f.write(f'#define PLAY_SUM_LEFT 0x{play_sums[0]:08X}u\n#define PLAY_SUM_RIGHT 0x{play_sums[1]:08X}u\n')
        f.write('static const uint8_t kClip[] __attribute__((aligned(4))) = {\n')
        for i in range(0, len(enc), 24):
            f.write('    ' + ','.join(str(b) for b in enc[i:i + 24]) + ',\n')
        f.write('};\n')
    print(f'{args.out}: {n / rate:.2f} s from {start / rate:.2f} s, {len(chunks)} chunks, {len(enc)} bytes = '
          f'{len(enc) / (clip.size * 2) * 100:.1f}% ({len(enc) / n * rate / 1024:.1f} KB/s); speed test {speed} chunks')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check')
    c.add_argument('raw', nargs='+')
    c.add_argument('--no-decode', dest='decode', action='store_false')
    b = sub.add_parser('benchclip')
    b.add_argument('raw')
    b.add_argument('start', type=float)
    b.add_argument('seconds', type=float)
    b.add_argument('out')
    b.add_argument('--speed-chunks', type=int, default=32)
    b.add_argument('--ffmpeg', default=__import__('os').environ.get('FFMPEG_DIR', ''))
    args = ap.parse_args()
    return {'check': cmd_check, 'benchclip': cmd_benchclip}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
