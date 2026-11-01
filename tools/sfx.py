"""The level's sound effects, as the game loads them into sound RAM (the .SFX
file) and the offsets table that describes them (saturn/sound.h).

Every sample a sound plays is the PlayStation's, as the PlayStation keeps
it: SPU ADPCM at 11025 Hz, whole, at its own level, which the sound CPU
decodes as it plays (saturn/sound.68k). A Saturn sample's is the one the
PlayStation level gives the same sound's same variant (its sound map and
sound infos). A sample the PlayStation has none for (the cutscenes have no
PlayStation level) keeps the original's: 16-bit PCM at 5512.5 Hz, or for
the sounds of SLOW_SOUNDS, at 2500 Hz, the rate they were made at (the
original played them 1.19 times as fast), and for FAST_SOUNDS, 11025 Hz.
Samples no sound plays take no room.

Decoding takes the sound CPU 100 to 330 cycles a sample by the block's
filter (FILTER_CYCLES), so it keeps up with only about three voices of the
costliest; each ADPCM sample's mean goes in its entry, for the game to
keep the voices decoding at once within what it can (saturn/sound.cpp).
Sound RAM left over takes samples decoded here instead, as 16-bit PCM at
11025 Hz (the same samples, which the slot plays straight): first those
that play on and on (the camera's underwater sound, the sound sources',
looped ones), then the costliest to decode, while they fit.

The original's aliases (ALIASES) give a sound's variant another's sample
when the level has both sounds (its own is a placeholder). Samples played
for more than one sound are stored once.
"""
import os
import re

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOUND_RAM_SIZE = 0x80000

ADPCM_FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]
ADPCM_BLOCK = 16            # bytes, 28 samples

# The sound CPU's cycles a sample by the block's filter (saturn/
# soundbench.68k over blocks of one filter, Mednafen = the Saturn when no
# slot reads sound RAM), and its own a block besides (sound.68k decode and
# its voice's turn)
FILTER_CYCLES = (100, 252, 324, 328, 324)
BLOCK_CYCLES = 500
SND_UNDERWATER = 60         # openlara common.h: the camera plays it under water all along

# The original's lists (0MAIN 0606324a, 06063246): the sounds whose variant 0
# it plays at 2971.6 Hz (made at 2500 Hz), and at 11025 Hz.
SLOW_SOUNDS = frozenset((3, 11, 12, 13, 14, 15, 16, 18, 60, 63, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
                         82, 83, 85, 86, 87, 88, 89, 90, 91, 92, 93, 100, 101, 102, 104, 105, 106, 107, 129, 130,
                         132, 133, 135, 144, 145, 148, 149, 150, 154, 163, 164, 165, 166, 168))
FAST_SOUNDS = frozenset((58,))

# The original's aliases (0MAIN 060631d8): sound, variant -> the sound,
# variant whose sample it plays.
ALIASES = ((14, 0, 14, 1), (117, 0, 153, 0), (170, 0, 70, 0), (99, 0, 70, 0), (136, 0, 70, 0),
           (172, 0, 82, 0), (64, 0, 63, 0))

LENGTH_MAX = 0x10000        # samples a slot plays straight (SCSP loop end is 16 bits)


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


def decode_cycles(data):
    """The sound CPU's mean cycles a sample decoding ADPCM data."""
    blocks = len(data) // ADPCM_BLOCK
    total = sum(FILTER_CYCLES[f if f < 5 else 0] * 28 + BLOCK_CYCLES
                for f in ((data[b * ADPCM_BLOCK] >> 4) & 7 for b in range(blocks)))
    return total // max(1, 28 * blocks)


def pcm16(data):
    """ADPCM data decoded to 16-bit PCM (big-endian), as the sound CPU would."""
    return adpcm_decode(data).astype(">i2").tobytes()


class Sfx:
    """The .SFX file of a level and its offsets table."""

    def __init__(self, lvl, ps1):
        self.defs = sound_defines()
        self.budget = SOUND_RAM_SIZE - self.defs["SOUND_SAMPLES_ADDR"]
        self.saturn = [lvl.samples[i] for i in range(len(lvl.samples))]
        self.ids = sample_ids(lvl.sound_map, lvl.sound_infos)
        # sounds that play on and on
        self.lasting = {SND_UNDERWATER} | {s[3] for s in lvl.sound_sources}
        for sid, m in enumerate(lvl.sound_map):
            if 0 <= m < len(lvl.sound_infos) and lvl.sound_infos[m][3] & 3 == 3:
                self.lasting.add(sid)
        self.aliases = self.find_aliases(lvl)
        self.ps1 = ps1.samples if ps1 is not None and ps1.samples else []
        # Saturn sample: the PlayStation's for the same sound and variant
        self.ps1_of = {}
        if self.ps1:
            by_id = {u: j for j, uses in sample_ids(ps1.sound_map, ps1.sound_infos).items() for u in uses}
            for i, uses in self.ids.items():
                for u in uses:
                    j = by_id.get(u)
                    if j is not None and j < len(self.ps1) and len(self.ps1[j]) >= ADPCM_BLOCK:
                        self.ps1_of[i] = j
                        break

    def rate_of(self, i):
        """The rate the original's sample i plays at here (SOUND_RATE_*)."""
        firsts = {sid for sid, k in self.ids.get(i, []) if k == 0}
        if firsts & FAST_SOUNDS:
            return self.defs["SOUND_RATE_FAST"]
        if firsts & SLOW_SOUNDS:
            return self.defs["SOUND_RATE_SLOW"]
        return self.defs["SOUND_RATE_NORMAL"]

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

    def adpcm(self, j):
        data = bytes(self.ps1[j])
        return data[:len(data) // ADPCM_BLOCK * ADPCM_BLOCK]

    def source(self, i, decoded):
        """Sample i's data, its key (which data it is), its entry's flags and
        its decoding's cycles a sample (0: none)."""
        d = self.defs
        k = self.aliases.get(i, i)
        if k in self.ps1_of:
            j = self.ps1_of[k]
            if j in decoded:
                return pcm16(self.adpcm(j)), ("decoded", j), (d["SOUND_FORMAT_PCM16"] << d["SOUND_FORMAT_SHIFT"] |
                                                            d["SOUND_RATE_FAST"] << d["SOUND_RATE_SHIFT"]), 0
            data = self.adpcm(j)
            return data, ("ps1", j), (d["SOUND_FORMAT_ADPCM"] << d["SOUND_FORMAT_SHIFT"] |
                                      d["SOUND_RATE_FAST"] << d["SOUND_RATE_SHIFT"]), decode_cycles(data)
        data = bytes(self.saturn[k])[:2 * LENGTH_MAX]
        return data, ("saturn", k), (d["SOUND_FORMAT_PCM16"] << d["SOUND_FORMAT_SHIFT"] |
                                     self.rate_of(k) << d["SOUND_RATE_SHIFT"]), 0

    def layout(self, decoded):
        """(file data, offsets table) with the PlayStation samples decoded
        that are in decoded."""
        data, entries, placed = bytearray(), [], {}
        self.counts = {"ps1": 0, "decoded": 0, "saturn": 0, "none": 0}
        for i in range(len(self.saturn)):
            if i not in self.ids:
                entries += [0, 0]
                self.counts["none"] += 1
                continue
            sample, key, flags, cycles = self.source(i, decoded)
            self.counts[key[0]] += 1
            if key not in placed:
                placed[key] = len(data)
                data += sample
                while len(data) & 3:
                    data.append(0)
            entries += [placed[key] | flags, len(sample) | (cycles >> 1) << self.defs["SOUND_COST_SHIFT"]]
        return data, entries

    def build(self):
        """(file data, offsets table: two words a sample)"""
        played = sorted({self.ps1_of[self.aliases.get(i, i)] for i in self.ids
                         if self.aliases.get(i, i) in self.ps1_of})
        lasting = {self.ps1_of[k] for k, uses in self.ids.items() if k in self.ps1_of and
                   any(sid in self.lasting for sid, _ in uses)}
        # decoded here while room lasts, those a slot can play straight
        # (LENGTH_MAX samples at most)
        def samples(j):
            return len(self.adpcm(j)) // ADPCM_BLOCK * 28
        order = sorted((j for j in played if samples(j) <= LENGTH_MAX),
                       key=lambda j: (j not in lasting, -decode_cycles(self.adpcm(j)), samples(j)))
        decoded = set()
        data, entries = self.layout(decoded)
        size = len(data)
        for j in order:
            extra = ((2 * samples(j) + 3) & ~3) - ((len(self.adpcm(j)) + 3) & ~3)
            if size + extra <= self.budget:
                decoded.add(j)
                size += extra
        data, entries = self.layout(decoded)
        if len(data) > self.budget:
            raise ValueError(f"{len(data)} bytes of sound samples, {self.budget} fit")
        assert len(data) <= self.defs["SOUND_OFFSET_MASK"]
        return bytes(data), entries
