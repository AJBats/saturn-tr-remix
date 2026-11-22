"""The PlayStation's videos (FMV/*.FMV on Tomb Raider's PS1 disc) decoded on the PC: the reference the
Saturn's decoder must match bit for bit (docs/ps1-fmv.md).

Written from psx-spx (Martin Korth, problemkaputt.de/psx-spx.htm): "CDROM File Video STR Streaming and BS
Picture Compression" (STR sectors, BS v2 headers, DC values, the AC Huffman codes), "MDEC Decompression"
(dequantising, the fast IDCT, YUV to RGB, the quant table and zigzag) and "CDROM XA Audio ADPCM Compression".
ffmpeg is only run beside it, as an oracle (`check`); none of its code was read.

The fixed-point steps here are the Saturn's: the decoder in saturn/ must give the same numbers.

    mdec.py check TRACK [--every N] [--videos NAME ...] [--ffmpeg DIR]   pictures and sound against ffmpeg's
    mdec.py frame TRACK NAME N OUT.png                                     one frame, as RGB
    mdec.py info TRACK                                                     frames, sizes, rows that are black

TRACK is the PS1 disc's first track, raw (MODE2/2352).
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

RAW = 2352
USER = 24                       # Mode 2: sync 12 + header 4 + subheader 8
SUBMODE = 18                    # subheader byte 2: bit 2 = audio
STR_HEADER = 32
STR_DATA = 2016
XA_GROUPS = 18
XA_GROUP = 128

# --- reading the disc -------------------------------------------------------


def _user(f, lba):
    f.seek(lba * RAW + USER)
    return f.read(2048)


def _records(f, lba, size):
    out = []
    for s in range((size + 2047) // 2048):
        d = _user(f, lba + s)
        i = 0
        while i < 2048 and d[i]:
            n = d[i]
            ext, ln = struct.unpack_from('<I', d, i + 2)[0], struct.unpack_from('<I', d, i + 10)[0]
            name = d[i + 33:i + 33 + d[i + 32]].decode('ascii', 'replace').split(';')[0]
            out.append((name, ext, ln, d[i + 25]))
            i += n
    return out


def videos(track):
    """{NAME: raw sectors} for every FMV/*.FMV on the raw track."""
    with open(track, 'rb') as f:
        pvd = _user(f, 16)
        if pvd[1:6] != b'CD001':
            raise SystemExit(f'{track}: no ISO9660 volume (is it the raw first track?)')
        root = pvd[156:190]
        top = _records(f, struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0])
        fmv = [r for r in top if r[0] == 'FMV']
        if not fmv:
            raise SystemExit(f'{track}: no FMV directory')
        out = {}
        for name, lba, ln, flags in _records(f, fmv[0][1], fmv[0][2]):
            if flags & 2 or not name.endswith('.FMV'):
                continue
            f.seek(lba * RAW)
            out[name[:-4]] = f.read(((ln + 2047) // 2048) * RAW)
        return out


def demux(raw):
    """(frames, xa): each frame's BS data (cut to its STR frame size), in frame order, and the sound
    sectors' 18 XA groups each (2304 bytes)."""
    parts = {}
    sizes = {}
    xa = []
    for off in range(0, len(raw) - RAW + 1, RAW):
        if raw[off + SUBMODE] & 4:
            xa.append(raw[off + USER:off + USER + XA_GROUPS * XA_GROUP])
            continue
        h = raw[off + USER:off + USER + STR_HEADER]
        if h[:4] != b'\x60\x01\x01\x80':
            continue
        sec, count, frame, size = struct.unpack_from('<HHII', h, 4)
        parts.setdefault(frame, {})[sec] = raw[off + USER + STR_HEADER:off + USER + STR_HEADER + STR_DATA]
        sizes[frame] = (count, size)
    frames = []
    for n in sorted(parts):
        count, size = sizes[n]
        if sorted(parts[n]) != list(range(count)):
            raise ValueError(f'frame {n}: sectors {sorted(parts[n])} of {count}')
        frames.append(b''.join(parts[n][s] for s in range(count))[:size])
    return frames, xa


# --- the bitstream (psx-spx: BS v2) -----------------------------------------

# AC codes: (prefix, MDEC values the x bits index); a trailing s bit negates the level
AC_CODES = [
    ('11', [0x0001]),
    ('011', [0x0401]),
    ('010', [0x0002, 0x0801]),
    ('0011', [0x1001, 0x0C01]),
    ('00101', [0x0003]),
    ('00100', [0x3401, 0x0006, 0x3001, 0x2C01, 0x0C02, 0x0403, 0x0005, 0x2801]),
    ('0001', [0x1C01, 0x1801, 0x0402, 0x1401]),
    ('00001', [0x0802, 0x2401, 0x0004, 0x2001]),
    ('0000001', [0x4001, 0x1402, 0x0007, 0x0803, 0x0404, 0x3C01, 0x3801, 0x1002]),
    ('00000001', [0x000B, 0x2002, 0x1003, 0x000A, 0x0804, 0x1C02, 0x5401, 0x5001,
                  0x0009, 0x4C01, 0x4801, 0x0405, 0x0C03, 0x0008, 0x1802, 0x4401]),
    ('000000001', [0x2802, 0x2402, 0x1403, 0x0C04, 0x0805, 0x0407, 0x0406, 0x000F,
                   0x000E, 0x000D, 0x000C, 0x6801, 0x6401, 0x6001, 0x5C01, 0x5801]),
    ('0000000001', [0x001F, 0x001E, 0x001D, 0x001C, 0x001B, 0x001A, 0x0019, 0x0018,
                    0x0017, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010]),
    ('00000000001', [0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
                     0x0020, 0x040E, 0x040D, 0x040C, 0x040B, 0x040A, 0x0409, 0x0408]),
    ('000000000001', [0x0412, 0x0411, 0x0410, 0x040F, 0x1803, 0x4002, 0x3C02, 0x3802,
                      0x3402, 0x3002, 0x2C02, 0x7C01, 0x7801, 0x7401, 0x7001, 0x6C01]),
]
PEEK = 17                       # the longest AC code with its sign bit
EOB, ESCAPE = -1, -2


def _ac_table():
    """Indexed by the next 17 bits: length | run << 5 | level << 11 (level signed), or EOB / ESCAPE."""
    table = [0] * (1 << PEEK)

    def fill(bits, value):
        n = len(bits)
        base = int(bits, 2) << (PEEK - n)
        for i in range(1 << (PEEK - n)):
            table[base + i] = value

    fill('10', EOB)
    fill('000001', ESCAPE)
    for prefix, values in AC_CODES:
        xbits = (len(values) - 1).bit_length()
        for i, v in enumerate(values):
            for s in (0, 1):
                code = prefix + (format(i, f'0{xbits}b') if xbits else '') + str(s)
                run, level = v >> 10, v & 0x3FF
                level = -level if s else level
                fill(code, len(code) | run << 5 | level << 11)
    return table


AC_TABLE = _ac_table()

# the 64 quant table bytes for STR movies, in zigzag order (psx-spx, "set_quant_table")
QUANT = bytes([
    0x02, 0x10, 0x10, 0x13, 0x10, 0x13, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x1A, 0x18, 0x1A, 0x1B,
    0x1B, 0x1B, 0x1A, 0x1A, 0x1A, 0x1A, 0x1B, 0x1B, 0x1B, 0x1D, 0x1D, 0x1D, 0x22, 0x22, 0x22, 0x1D,
    0x1D, 0x1D, 0x1B, 0x1B, 0x1D, 0x1D, 0x20, 0x20, 0x22, 0x22, 0x25, 0x26, 0x25, 0x23, 0x23, 0x22,
    0x23, 0x26, 0x26, 0x28, 0x28, 0x28, 0x30, 0x30, 0x2E, 0x2E, 0x38, 0x38, 0x3A, 0x45, 0x45, 0x53])
# zigzag[raster] = scan index (psx-spx); ZAGZIG[scan] = raster position
ZIGZAG = [0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42,
          3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
          10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
          21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63]
ZAGZIG = [0] * 64
for _r, _k in enumerate(ZIGZAG):
    ZAGZIG[_k] = _r


def signed10(v):
    return v - 0x400 if v & 0x200 else v


class BadFrame(Exception):
    pass


def parse(data, max_blocks):
    """A BS v2 frame's blocks: (quant scale, coefficients [n, 64] int32 in raster order, dequantised and
    saturated, bits read). Stops at the end-of-frame code or after max_blocks."""
    if len(data) < 8:
        raise BadFrame('short')
    _, ident, quant, version = struct.unpack_from('<HHHH', data, 0)
    if ident != 0x3800 or version != 2 or not 0 < quant < 64:
        raise BadFrame(f'header {ident:04x} v{version} quant {quant}')
    words = struct.unpack_from(f'<{(len(data) - 8) // 2}H', data, 8)
    nwords = len(words)
    table = AC_TABLE
    scale = [QUANT[k] * quant for k in range(64)]
    qdc = QUANT[0]
    out = np.zeros((max_blocks, 64), np.int32)
    buf = cnt = wi = 0
    nblocks = 0
    while nblocks < max_blocks:
        while cnt < 32:
            buf = (buf << 16) | (words[wi] if wi < nwords else 0)
            wi += 1
            cnt += 16
        dc = (buf >> (cnt - 10)) & 0x3FF
        if dc == 0x1FF and nblocks % 6 == 0:
            break
        cnt -= 10
        blk = out[nblocks]
        v = signed10(dc) * qdc
        blk[0] = -0x400 if v < -0x400 else 0x3FF if v > 0x3FF else v
        k = 0
        while True:
            if cnt < 22:
                buf = ((buf & ((1 << cnt) - 1)) << 16) | (words[wi] if wi < nwords else 0)
                wi += 1
                cnt += 16
                if cnt < 22:
                    buf = (buf << 16) | (words[wi] if wi < nwords else 0)
                    wi += 1
                    cnt += 16
            e = table[(buf >> (cnt - PEEK)) & 0x1FFFF]
            if e == EOB:
                cnt -= 2
                break
            if e == ESCAPE:
                raw16 = (buf >> (cnt - 22)) & 0xFFFF
                cnt -= 22
                run, level = raw16 >> 10, signed10(raw16 & 0x3FF)
            elif e == 0:
                raise BadFrame(f'block {nblocks}: no code')
            else:
                cnt -= e & 31
                run, level = (e >> 5) & 63, e >> 11
            k += run + 1
            if k > 63:
                raise BadFrame(f'block {nblocks}: past 64 values')
            v = (level * scale[k] + 4) >> 3
            blk[ZAGZIG[k]] = -0x400 if v < -0x400 else 0x3FF if v > 0x3FF else v
        nblocks += 1
        buf &= (1 << cnt) - 1
    return quant, out[:nblocks], (wi * 16 - cnt)


# --- the inverse DCT (psx-spx's fast_idct_core, in the Saturn's fixed point) --

# scalefactor[0..7] = cos(k * 90'/8), times sqrt(2) for k > 0 (psx-spx)
SCALEFACTOR = [1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100, 0.275899379]
PRE_BITS = 16                   # the prescale table's fraction
FRAC = 6                        # pixels carry 6 fraction bits through the two passes
K_BITS = 12                     # the passes' constants
PRESCALE = np.array([round(SCALEFACTOR[r % 8] * SCALEFACTOR[r // 8] / 8 * (1 << PRE_BITS)) for r in range(64)],
                    np.int64)
K1414 = round(1.414213562 * (1 << K_BITS))
K1847 = round(1.847759065 * (1 << K_BITS))
K2613 = round(2.613125930 * (1 << K_BITS))
K1082 = round(1.082392200 * (1 << K_BITS))


def _wrap(a):
    """32-bit wrap, as the SH-2's registers do."""
    return ((a + (1 << 31)) & 0xFFFFFFFF) - (1 << 31)


def _pass(v):
    """One pass over [n, 8, 8] int64: v[:, j, i] is input j of vector i; returns out[:, i, j]."""
    s = [v[:, j, :] for j in range(8)]

    def m(k, x):
        return _wrap(x * k) >> K_BITS

    z10, z11 = s[0] + s[4], s[0] - s[4]
    z13, z12 = s[2] + s[6], s[2] - s[6]
    z12 = m(K1414, z12) - z13
    t0, t3, t1, t2 = z10 + z13, z10 - z13, z11 + z12, z11 - z12
    z13, z10 = s[3] + s[5], s[3] - s[5]
    z11, z12 = s[1] + s[7], s[1] - s[7]
    z5 = m(K1847, z12 - z10)
    t7 = z11 + z13
    t6 = m(K2613, z10) + z5 - t7
    t5 = m(K1414, z11 - z13) - t6
    t4 = m(K1082, z12) - z5 + t5
    out = np.empty_like(v)
    for j, x in enumerate((t0 + t7, t1 + t6, t2 + t5, t3 - t4, t3 + t4, t2 - t5, t1 - t6, t0 - t7)):
        out[:, :, j] = x
    return out


def idct(coeffs):
    """[n, 64] coefficients (raster) -> [n, 8, 8] pixels, signed, rounded (rows, columns)."""
    v = (coeffs.astype(np.int64) * PRESCALE + (1 << (PRE_BITS - FRAC - 1))) >> (PRE_BITS - FRAC)
    v = v.reshape(-1, 8, 8)
    v = _pass(_pass(v))
    return (v + (1 << (FRAC - 1))) >> FRAC


def idct_float(coeffs):
    """The same IDCT in floating point, from its definition: the yardstick for the fixed point."""
    c = np.array([[(np.sqrt(0.5) if u == 0 else 1.0) * np.cos((2 * x + 1) * u * np.pi / 16) / 2
                   for u in range(8)] for x in range(8)])
    b = coeffs.reshape(-1, 8, 8).astype(np.float64)
    # psx-spx's blocks feed dequantised values straight to an IDCT whose DC gain is 1/8 per value after
    # its prescale; the orthonormal IDCT's is 1/8 too, with the AC terms' sqrt(2)s inside c
    return np.einsum('xu,nvu,yv->nyx', c, b, c)


# --- the picture -------------------------------------------------------------

def assemble(blocks, width, height):
    """Six pixel blocks a macroblock (Cr, Cb, Y1..Y4), macroblocks column by column: Y, Cb, Cr planes (int)."""
    mbw, mbh = (width + 15) // 16, (height + 15) // 16
    y = np.zeros((mbh * 16, mbw * 16), np.int32)
    cb = np.zeros((mbh * 8, mbw * 8), np.int32)
    cr = np.zeros((mbh * 8, mbw * 8), np.int32)
    for mb in range(len(blocks) // 6):
        col, row = divmod(mb, mbh)
        b = blocks[mb * 6:mb * 6 + 6]
        cr[row * 8:row * 8 + 8, col * 8:col * 8 + 8] = b[0]
        cb[row * 8:row * 8 + 8, col * 8:col * 8 + 8] = b[1]
        y0, x0 = row * 16, col * 16
        y[y0:y0 + 8, x0:x0 + 8] = b[2]
        y[y0:y0 + 8, x0 + 8:x0 + 16] = b[3]
        y[y0 + 8:y0 + 16, x0:x0 + 8] = b[4]
        y[y0 + 8:y0 + 16, x0 + 8:x0 + 16] = b[5]
    return y[:height, :width], cb[:height // 2, :width // 2], cr[:height // 2, :width // 2]


# YUV to RGB (psx-spx): R = Y + 1.402 Cr, G = Y - 0.3437 Cb - 0.7143 Cr, B = Y + 1.772 Cb, clamped -128..127
C_BITS = 12
CR_R = round(1.402 * (1 << C_BITS))
CR_G = round(0.7143 * (1 << C_BITS))
CB_G = round(0.3437 * (1 << C_BITS))
CB_B = round(1.772 * (1 << C_BITS))


def rgb(y, cb, cr):
    """Planes -> [h, w, 3] uint8, a chroma value colouring each 2x2 square."""
    half = 1 << (C_BITS - 1)
    r = (cr * CR_R + half) >> C_BITS
    g = (-cb * CB_G - cr * CR_G + half) >> C_BITS
    b = (cb * CB_B + half) >> C_BITS
    up = lambda p: np.repeat(np.repeat(p, 2, 0), 2, 1)
    out = np.stack([y + up(r), y + up(g), y + up(b)], -1)
    return (np.clip(out, -128, 127) + 128).astype(np.uint8)


def decode(frame, width, height, fixed=True):
    """A frame's Y, Cb, Cr planes (signed ints), and what parse() reports."""
    mbs = ((width + 15) // 16) * ((height + 15) // 16)
    quant, coeffs, bits = parse(frame, mbs * 6)
    if len(coeffs) != mbs * 6:
        raise BadFrame(f'{len(coeffs)} blocks, not {mbs * 6}')
    pix = idct(coeffs) if fixed else np.rint(idct_float(coeffs)).astype(np.int32)
    return assemble(pix, width, height), (quant, coeffs, bits)


# --- XA sound (psx-spx) -----------------------------------------------------

XA_F0 = (0, 60, 115, 98)
XA_F1 = (0, 0, -52, -55)


def xa_decode(sectors):
    """Stereo XA sectors (18 groups of 128 bytes each) -> [n, 2] int16."""
    out = []
    old = [0, 0]
    older = [0, 0]
    for sec in sectors:
        for g in range(XA_GROUPS):
            grp = sec[g * XA_GROUP:(g + 1) * XA_GROUP]
            chans = ([], [])
            for unit in range(8):                   # units alternate left, right
                ch = unit & 1
                head = grp[4 + unit]
                shift = 12 - (head & 15 if head & 15 <= 12 else 9)     # 13..15 act as 9
                f0, f1 = XA_F0[(head >> 4) & 3], XA_F1[(head >> 4) & 3]
                o, oo = old[ch], older[ch]
                for j in range(28):
                    t = (grp[16 + (unit >> 1) + j * 4] >> ((unit & 1) * 4)) & 15
                    t = t - 16 if t & 8 else t
                    s = (t << shift) + ((o * f0 + oo * f1 + 32) >> 6)
                    s = -0x8000 if s < -0x8000 else 0x7FFF if s > 0x7FFF else s
                    chans[ch].append(s)
                    oo, o = o, s
                old[ch], older[ch] = o, oo
            out.append(np.stack([np.array(chans[0]), np.array(chans[1])], -1))
    return np.concatenate(out).astype(np.int16)


# --- ffmpeg, the oracle -----------------------------------------------------

def ffmpeg_planes(ffdir, raw, width, height):
    """ffmpeg's decode of a raw STR stream: an iterator of (Y, Cb, Cr) uint8 planes."""
    with tempfile.NamedTemporaryFile(suffix='.str', delete=False) as t:
        t.write(raw)
    try:
        p = subprocess.Popen([os.path.join(ffdir, 'ffmpeg'), '-v', 'error', '-f', 'psxstr', '-i', t.name, '-an',
                              '-f', 'rawvideo', '-pix_fmt', 'yuvj420p', '-vsync', '0', '-'], stdout=subprocess.PIPE)
        ny, nc = width * height, (width // 2) * (height // 2)
        while True:
            b = p.stdout.read(ny + 2 * nc)
            if len(b) < ny + 2 * nc:
                break
            a = np.frombuffer(b, np.uint8)
            yield (a[:ny].reshape(height, width), a[ny:ny + nc].reshape(height // 2, width // 2),
                   a[ny + nc:].reshape(height // 2, width // 2))
        p.wait()
    finally:
        os.remove(t.name)


def ffmpeg_audio(ffdir, raw):
    with tempfile.NamedTemporaryFile(suffix='.str', delete=False) as t:
        t.write(raw)
    try:
        out = subprocess.run([os.path.join(ffdir, 'ffmpeg'), '-v', 'error', '-f', 'psxstr', '-i', t.name, '-vn',
                              '-f', 's16le', '-acodec', 'pcm_s16le', '-'], capture_output=True).stdout
    finally:
        os.remove(t.name)
    return np.frombuffer(out, np.int16).reshape(-1, 2)


def frame_size(raw):
    for off in range(0, len(raw) - RAW + 1, RAW):
        h = raw[off + USER:off + USER + STR_HEADER]
        if h[:4] == b'\x60\x01\x01\x80':
            return struct.unpack_from('<HH', h, 16)
    raise ValueError('no video sectors')


# --- commands -----------------------------------------------------------------

def cmd_check(args):
    vids = videos(args.track)
    names = args.videos or sorted(vids)
    worst_all = 0
    for name in names:
        raw = vids[name]
        width, height = frame_size(raw)
        frames, xa = demux(raw)
        stats = []
        fixed_vs_float = 0
        for i, ff in enumerate(ffmpeg_planes(args.ffmpeg, raw, width, height)):
            if i >= len(frames):
                break
            if i % args.every:
                continue
            (y, cb, cr), (quant, coeffs, bits) = decode(frames[i], width, height)
            ours = (np.clip(y + 128, 0, 255), np.clip(cb + 128, 0, 255), np.clip(cr + 128, 0, 255))
            d = [np.abs(o.astype(np.int32) - f.astype(np.int32)) for o, f in zip(ours, ff)]
            stats.append((i, max(int(x.max()) for x in d), float(np.mean(np.concatenate([x.ravel() for x in d])))))
            fl = np.rint(idct_float(coeffs)).astype(np.int32)
            fixed_vs_float = max(fixed_vs_float, int(np.abs(idct(coeffs).reshape(-1, 64) - fl.reshape(-1, 64)).max()))
        worst = max(s[1] for s in stats)
        worst_all = max(worst_all, worst)
        mean = sum(s[2] for s in stats) / len(stats)
        wi = max(stats, key=lambda s: s[1])[0]
        pcm = xa_decode(xa)
        ffa = ffmpeg_audio(args.ffmpeg, raw)
        n = min(len(pcm), len(ffa))
        sound = 'same' if n and np.array_equal(pcm[:n], ffa[:n]) else \
            f'DIFFERS ({int(np.sum(pcm[:n] != ffa[:n]))} of {n * 2} samples)'
        print(f'{name:9s} {width}x{height} frames {len(frames):5d} checked {len(stats):4d} | vs ffmpeg: max {worst} '
              f'(frame {wi}) mean {mean:.3f} | fixed vs float IDCT max {fixed_vs_float} | sound {len(pcm)} '
              f'samples, ffmpeg {len(ffa)}: {sound}', flush=True)
    return 0


def cmd_frame(args):
    raw = videos(args.track)[args.name]
    width, height = frame_size(raw)
    frames, _ = demux(raw)
    (y, cb, cr), _ = decode(frames[args.n], width, height)
    from PIL import Image
    Image.fromarray(rgb(y, cb, cr)).save(args.out)
    return 0


def cmd_info(args):
    for name, raw in sorted(videos(args.track).items()):
        width, height = frame_size(raw)
        frames, xa = demux(raw)
        mbh = (height + 15) // 16
        live = np.zeros(mbh, bool)
        for i in range(0, len(frames), max(1, args.every)):
            (y, _, _), _ = decode(frames[i], width, height)
            live |= (y + 128).reshape(mbh, 16, -1).max(axis=(1, 2)) > args.black
        rows = ''.join('#' if v else '.' for v in live)
        print(f'{name:9s} {width}x{height} frames {len(frames):5d} bytes {sum(map(len, frames)):9d} '
              f'max {max(map(len, frames)):6d} xa sectors {len(xa):5d} | macroblock rows {rows}', flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check')
    c.add_argument('track')
    c.add_argument('--every', type=int, default=25)
    c.add_argument('--videos', nargs='*')
    c.add_argument('--ffmpeg', default=os.environ.get('FFMPEG_DIR', ''))
    f = sub.add_parser('frame')
    f.add_argument('track')
    f.add_argument('name')
    f.add_argument('n', type=int)
    f.add_argument('out')
    i = sub.add_parser('info')
    i.add_argument('track')
    i.add_argument('--every', type=int, default=10)
    i.add_argument('--black', type=int, default=24)
    args = ap.parse_args()
    return {'check': cmd_check, 'frame': cmd_frame, 'info': cmd_info}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
