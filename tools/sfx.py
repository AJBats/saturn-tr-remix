"""The level's sound effects, as the game loads them into sound RAM (the .SFX
file) and the offsets table that describes them (saturn/sound.h).

The Saturn's samples are 16-bit PCM, made from the same recordings as the
PlayStation's (SPU ADPCM at 11025 Hz), from 70-170 samples into the
PlayStation's, and shortened: cut at the end, or with stretches cut out of
the middle. Some are other recordings. Most are at 5512.5 Hz (every other
sample), and the original plays them so; but by its lists (LoadSoundFX
06036ec0, S_SoundPlaySample 0603c478), variant 0 of each sound in
SLOW_SOUNDS, a sample made at 2500 Hz, it plays at 2971.6 Hz (1.19 times as
fast as recorded), and of each in FAST_SOUNDS, made at 11025 Hz, at 11025
Hz. The offsets table carries each sample's rate (saturn/sound.h).

A Saturn sample is replaced by the PlayStation's it was made from, as 8-bit
PCM at 11025 Hz: from 5512.5 Hz that takes the same room for the same
length; from 2500 Hz, 2.2 times the room (while sound RAM lasts: fit()),
played 1.19 times as fast, as the original plays it. It is found among the
PlayStation samples the sound map gives the same sound (the same sample
index when it gives none), seen at the Saturn sample's rate, and accepted
when the Saturn sample, in 256-sample chunks, is found in it in order (an
edit of it): chunks holding at least CHAIN_MIN of the sample's energy each
correlate CHUNK_MIN or better with the PlayStation sample, at positions
that only move forward. Another recording of a sound has chunks that
correlate as well only by chance, and at scattered positions. A sample not
found keeps the Saturn data, as do the fast ones (16-bit at the
PlayStation's rate already).

The 8-bit sample starts where the Saturn one does in the PlayStation's and
is as long or, with sound RAM to spare, longer, up to the PlayStation's end
(restore()). It plays at the Saturn sample's level, normalised to the 8-bit
range with TPDF dither and first-order noise shaping; the offsets table
carries the attenuation that undoes the normalisation, in SCSP total level
steps.

The original's aliases (ALIASES) give a sound's variant another's sample
when the level has both sounds (its own is a placeholder): the offsets
table gives it a copy of the other's.
"""
import math
import os
import random
import re
import struct

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOUND_RAM_SIZE = 0x80000
SPARE_MIN = 8 * 1024        # sound RAM kept free when restoring cut sounds
RESTORE_MIN = 256           # samples (23 ms) a sound is lengthened by at least
LENGTH_MAX = 0x10000        # samples a slot plays (SCSP loop end is 16 bits)

CHUNK = 256                 # Saturn samples
CHUNK_MIN = 0.8
CHAIN_MIN = 0.5
LAG_TOLERANCE = 64          # PlayStation samples a chunk may sit before the one ahead of it
HEAD_MIN = 0.5              # the correlation of a chunk before the run that places the start
LEVEL_RANGE = 100           # energy ratio (20 dB) a chunk and its match may differ by
TL_STEP_DB = 0.375
FADE = 64                   # samples faded out where a sample is cut short

ADPCM_FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]

PS1_RATE = 11025.0
NORMAL_MADE = 5512.5        # Hz, the rate most Saturn samples were made at
SLOW_MADE = 2500.0          # the slow sounds'

# The original's lists (0MAIN 0606324a, 06063246): the sounds whose variant 0
# plays at 2971.6 Hz, and at 11025 Hz.
SLOW_SOUNDS = frozenset((3, 11, 12, 13, 14, 15, 16, 18, 60, 63, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
                         82, 83, 85, 86, 87, 88, 89, 90, 91, 92, 93, 100, 101, 102, 104, 105, 106, 107, 129, 130,
                         132, 133, 135, 144, 145, 148, 149, 150, 154, 163, 164, 165, 166, 168))
FAST_SOUNDS = frozenset((58,))

# The original's aliases (0MAIN 060631d8): sound, variant -> the sound,
# variant whose sample it plays.
ALIASES = ((14, 0, 14, 1), (117, 0, 153, 0), (170, 0, 70, 0), (99, 0, 70, 0), (136, 0, 70, 0),
           (172, 0, 82, 0), (64, 0, 63, 0))


def sound_defines():
    """SOUND_* constants of saturn/sound.h."""
    src = open(os.path.join(ROOT, "saturn", "sound.h")).read()
    return {k: int(v, 0) for k, v in re.findall(r"#define (SOUND_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", src)}


def adpcm_decode(data):
    """PlayStation SPU ADPCM to 16-bit samples."""
    out = []
    old = older = 0
    for b in range(0, len(data) - 15, 16):
        hdr = data[b]
        shift = hdr & 15
        f = (hdr >> 4) & 7
        f0, f1 = ADPCM_FILTERS[f] if f < 5 else (0, 0)
        if shift > 12:
            shift = 9
        for i in range(28):
            byte = data[b + 2 + i // 2]
            n = (byte >> 4) if i & 1 else (byte & 15)
            if n >= 8:
                n -= 16
            s = (n << 12) >> shift
            s += (old * f0 + older * f1 + 32) >> 6
            s = max(-32768, min(32767, s))
            older, old = old, s
            out.append(s)
    return np.array(out, float)


def sample_ids(sound_map, sound_infos):
    """{sample index: [(sound ID, variant)]}"""
    ids = {}
    for sid, m in enumerate(sound_map):
        if 0 <= m < len(sound_infos):
            first, _, _, flags = sound_infos[m]
            for k in range((flags >> 2) & 15):
                ids.setdefault(first + k, []).append((sid, k))
    return ids


class Source:
    """The PlayStation samples, decoded as needed, each split in its two
    5512.5 Hz phases for comparing with the Saturn's."""

    def __init__(self, samples):
        self.raw = samples
        self.cache = {}

    def __len__(self):
        return len(self.raw)

    def pcm(self, j):
        if j not in self.cache:
            self.cache[j] = adpcm_decode(self.raw[j])
        return self.cache[j]


class Seen:
    """A PlayStation sample p at the rate a Saturn sample was made at: its
    phases, each with its first sample's place in p, and the PlayStation
    samples a Saturn one spans (scale)."""

    def __init__(self, p, made):
        if made == NORMAL_MADE:
            self.phases = [(p[0::2], 0), (p[1::2], 1)]
            self.scale = 2
        else:
            # band-limited to the lower rate, by FFT
            n = len(p)
            m = max(1, int(round(n * made / PS1_RATE)))
            spectrum = np.fft.rfft(p)
            kept = np.zeros(m // 2 + 1, complex)
            keep = min(len(spectrum), len(kept))
            kept[:keep] = spectrum[:keep]
            self.phases = [(np.fft.irfft(kept, m) * (m / n), 0)]
            self.scale = PS1_RATE / made

    def place(self, i, s0):
        """Where the Saturn sample starts in p (but for the phase's place),
        if its sample s0 lies at sample i of a phase."""
        return int(round((i - s0) * self.scale))

    def segment(self, start, n):
        """n samples at the Saturn rate from the PlayStation's sample start."""
        if self.scale == 2:
            d, offset = self.phases[start & 1]
            return d[(start - offset) // 2:][:n]
        d = self.phases[0][0]
        k = int(round(start / self.scale))
        return d[max(k, 0):max(k + n, 0)]


def chunk_matches(x, seen):
    """For each CHUNK of the Saturn sample x, its energy and the best
    normalised correlation with the PlayStation sample (seen at x's rate), at
    the start the chunk gives the Saturn sample in it (PlayStation samples)."""
    out = []
    for s0 in range(0, len(x), CHUNK):
        seg = x[s0:s0 + CHUNK]
        if s0 and len(seg) < CHUNK // 4:
            break
        n = len(seg)
        e = float(np.dot(seg, seg))
        best = (0.0, 0)
        for d, ph in seen.phases:
            if len(d) < n:
                continue
            lags = len(d) - n + 1
            size = 1 << (len(d) + n - 1).bit_length()
            c = np.fft.irfft(np.fft.rfft(d, size) * np.conj(np.fft.rfft(seg, size)), size)[:lags]
            sq = np.concatenate([[0.0], np.cumsum(d * d)])
            ed = sq[n:n + lags] - sq[:lags]
            k = c / (np.sqrt(ed * e) + 1e-9)
            k[(ed < e / LEVEL_RANGE) | (ed > e * LEVEL_RANGE)] = 0  # near-silence correlates with anything
            i = int(np.argmax(k))
            if k[i] > best[0]:
                best = (float(k[i]), seen.place(i, s0) + ph)
        out.append((e, best[0], best[1]))
    return out


def find_in(x, p, made=NORMAL_MADE):
    """(share of x's energy found in order in p, where x starts in p, gain
    from p to x) or None; x made at made Hz."""
    seen = Seen(p, made)
    ch = chunk_matches(x, seen)
    if not ch:
        return None
    emax = max(e for e, _, _ in ch)
    if emax <= 0:
        return None
    loud = [k for k, (e, _, _) in enumerate(ch) if e >= emax * 0.01]
    total = sum(ch[k][0] for k in loud)
    good = [k for k in loud if ch[k][1] >= CHUNK_MIN]
    # the heaviest run of chunks whose starts don't go back
    best = {}
    for a, k in enumerate(good):
        run = (ch[k][0], [k])
        for k2 in good[:a]:
            if ch[k][2] >= ch[k2][2] - LAG_TOLERANCE and best[k2][0] + ch[k][0] > run[0]:
                run = (best[k2][0] + ch[k][0], best[k2][1] + [k])
        best[k] = run
    if not best:
        return None
    weight, ks = max(best.values(), key=lambda r: r[0])
    # the start: where the run starts or, found before it, an earlier chunk
    # (a quiet or partly silent start correlates less)
    start = ch[ks[0]][2]
    for e, c, lag in ch[:ks[0]]:
        if e > 0 and c >= HEAD_MIN and 0 <= lag <= start + LAG_TOLERANCE:
            start = lag
            break
    if start < 0:
        return None
    # level: the Saturn chunks' energy against the PlayStation's they match
    es = ep = 0.0
    for k in ks:
        s0 = k * CHUNK
        seg = x[s0:s0 + CHUNK]
        d = seen.segment(ch[k][2] + int(round(s0 * seen.scale)), len(seg))
        es += float(np.dot(seg, seg))
        ep += float(np.dot(d, d))
    gain = math.sqrt(es / ep) if ep > 0 else 1.0
    return weight / total, start, gain


def find_source(x, made, candidates, src):
    """The best accepted (share, PlayStation index, start, gain) or None."""
    found = None
    for j in candidates:
        r = find_in(x, src.pcm(j), made)
        if r and r[0] >= CHAIN_MIN and (found is None or r[0] > found[0]):
            found = (r[0], j, r[1], r[2])
    return found


def sound_end(p):
    """Length of p without its trailing silence (below an 8-bit step)."""
    peak = np.max(np.abs(p)) if len(p) else 0
    loud = np.nonzero(np.abs(p) > peak / 256)[0]
    return int(loud[-1]) + 1 if len(loud) else 0


def to_pcm8(p, gain, steps_max, seed):
    """8-bit samples of p at gain (the SCSP plays a byte as its value * 256),
    scaled up to the 8-bit range by whole TL steps; returns (bytes, steps)."""
    y = p * gain
    peak = float(np.max(np.abs(y))) if len(y) else 0.0
    steps = 0
    if peak > 0:
        steps = min(steps_max, max(0, math.floor(20 * math.log10(127 * 256 / peak) / TL_STEP_DB)))
    k = 10 ** (steps * TL_STEP_DB / 20) / 256
    if peak * k > 127:
        k = 127 / peak      # louder than 16 bits holds at the Saturn's level: a little quieter
    rnd = random.Random(seed)
    err = 0.0
    q = bytearray()
    for v in y * k:
        v -= err * 0.5
        o = max(-128, min(127, int(round(v + rnd.random() - rnd.random()))))
        err = o - v
        q.append(o & 0xFF)
    return bytes(q), steps


class Sfx:
    """The .SFX file of a level and its offsets table."""

    def __init__(self, lvl, ps1):
        defs = sound_defines()
        self.flag_pcm8 = defs["SOUND_PCM8"]
        self.rate_shift = defs["SOUND_RATE_SHIFT"]
        self.rate_normal = defs["SOUND_RATE_NORMAL"]
        self.rate_slow = defs["SOUND_RATE_SLOW"]
        self.rate_fast = defs["SOUND_RATE_FAST"]
        self.rate_raised = defs["SOUND_RATE_RAISED"]
        self.atten_shift = defs["SOUND_ATTEN_SHIFT"]
        self.atten_max = defs["SOUND_ATTEN_MASK"]
        self.offset_mask = defs["SOUND_OFFSET_MASK"]
        self.budget = SOUND_RAM_SIZE - defs["SOUND_SAMPLES_ADDR"]
        self.saturn = [lvl.samples[i] for i in range(len(lvl.samples))]
        self.ids = sample_ids(lvl.sound_map, lvl.sound_infos)
        self.rates = [self.rate_of(i) for i in range(len(self.saturn))]
        self.aliases = self.find_aliases(lvl)
        self.matches = {}   # Saturn index: (share, PlayStation index, start, gain)
        self.ps1_ids = {}
        self.src = None
        if ps1 is not None and ps1.samples:
            self.src = Source(ps1.samples)
            self.ps1_ids = sample_ids(ps1.sound_map, ps1.sound_infos)
            self.match(lvl, ps1)
        # matched samples' lengths (8-bit samples), at first the Saturn's
        self.lengths = {i: self.length_for(i, self.spans(i)) for i in self.matches}
        self.kept = []      # slow samples matched but kept, for room
        self.restored = []
        self.fit()

    def rate_of(self, i):
        """The original's rate for sample i (SOUND_RATE_*)."""
        firsts = {sid for sid, k in self.ids.get(i, []) if k == 0}
        if firsts & FAST_SOUNDS:
            return self.rate_fast
        if firsts & SLOW_SOUNDS:
            return self.rate_slow
        return self.rate_normal

    def made(self, i):
        """The rate sample i was made at (Hz)."""
        return SLOW_MADE if self.rates[i] == self.rate_slow else NORMAL_MADE

    def spans(self, i):
        """The PlayStation samples the Saturn sample i spans."""
        return int(round(len(self.saturn[i]) // 2 * PS1_RATE / self.made(i)))

    def find_aliases(self, lvl):
        """{sample: the sample it plays}, for the aliases whose sounds are both
        in the level (as the original's loader takes them)."""
        def sample(sid, k):
            m = lvl.sound_map[sid] if sid < len(lvl.sound_map) else -1
            if not 0 <= m < len(lvl.sound_infos):
                return None
            i = lvl.sound_infos[m][0] + k
            return i if i < len(self.saturn) else None
        out = {}
        for sid, k, tid, tk in ALIASES:
            s, t = sample(sid, k), sample(tid, tk)
            if s is not None and t is not None:
                out[s] = t
        return out

    def match(self, lvl, ps1):
        by_id = {}
        for j, uses in self.ps1_ids.items():
            for u in uses:
                by_id.setdefault(u, set()).add(j)
        for i, data in enumerate(self.saturn):
            if len(data) < 2 * CHUNK // 4 or self.rates[i] == self.rate_fast or i in self.aliases:
                continue    # placeholders; the fast ones are at the PlayStation's rate
            x = np.array(struct.unpack(f">{len(data) // 2}h", data), float)
            candidates = sorted(set().union(*(by_id.get(u, set()) for u in self.ids.get(i, []))))
            if not candidates:
                candidates = [i]
            found = find_source(x, self.made(i), [j for j in candidates if j < len(self.src)], self.src)
            if found:
                self.matches[i] = found

    def full_length(self, i):
        """The PlayStation sample from where the Saturn one starts to its end."""
        _, j, start, _ = self.matches[i]
        return sound_end(self.src.pcm(j)) - start

    def length_for(self, i, saturn_length):
        return min(saturn_length, self.full_length(i), LENGTH_MAX)

    def size(self, lengths=None):
        lengths = self.lengths if lengths is None else lengths
        total = 0
        for i in range(len(self.saturn)):
            k = self.aliases.get(i, i)
            total += ((lengths[k] if k in lengths else len(self.saturn[k])) + 3) & ~3
        return total

    def fit(self):
        """Keeps the Saturn data of matched slow samples, the costliest first,
        till the rest fit with SPARE_MIN of sound RAM free."""
        slow = [i for i in self.lengths if self.rates[i] == self.rate_slow]
        slow.sort(key=lambda i: len(self.saturn[i]) - self.lengths[i])
        for i in slow:
            if self.size() <= self.budget - SPARE_MIN:
                break
            del self.lengths[i]
            del self.matches[i]
            self.kept.append(i)

    def restore(self):
        """Lengthens the matched samples the Saturn cut short, the most cut
        first, while SPARE_MIN of sound RAM stays free."""
        order = sorted(self.lengths, key=lambda i: -self.full_length(i) / max(1, self.lengths[i]))
        for i in order:
            full = min(self.full_length(i), LENGTH_MAX)
            room = self.budget - SPARE_MIN - self.size()
            if room <= 0:
                break
            new = min(full, (self.lengths[i] + room) & ~3)
            if new >= self.lengths[i] + RESTORE_MIN:
                self.restored.append((i, self.lengths[i], new))
                self.lengths[i] = new

    def sample(self, i):
        """Sample i's bytes and its entry's flags."""
        if i not in self.lengths:
            return self.saturn[i], self.rates[i] << self.rate_shift
        _, j, start, gain = self.matches[i]
        p = self.src.pcm(j)
        n = self.lengths[i]
        seg = p[start:start + n].copy()
        if start + n < sound_end(p):
            fade = min(FADE, n)
            seg[n - fade:] *= np.linspace(1, 0, fade + 1)[1:]
        pcm, steps = to_pcm8(seg, gain, self.atten_max, seed=i)
        rate = self.rate_raised if self.rates[i] == self.rate_slow else self.rate_fast
        return pcm, self.flag_pcm8 | (rate << self.rate_shift) | (steps << self.atten_shift)

    def build(self):
        """(file data, offsets table entries including the size entry)"""
        data, entries = bytearray(), []
        for i in range(len(self.saturn)):
            sample, flags = self.sample(self.aliases.get(i, i))
            entries.append(len(data) | flags)
            data += sample
            while len(data) & 3:
                data.append(0)
        entries.append(len(data))
        assert len(data) <= self.offset_mask
        return bytes(data), entries
