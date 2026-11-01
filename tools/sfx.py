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


class Sfx:
    """The .SFX file of a level and its offsets table."""

    def __init__(self, lvl, ps1):
        self.defs = sound_defines()
        self.budget = SOUND_RAM_SIZE - self.defs["SOUND_SAMPLES_ADDR"]
        self.saturn = [lvl.samples[i] for i in range(len(lvl.samples))]
        self.ids = sample_ids(lvl.sound_map, lvl.sound_infos)
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

    def source(self, i):
        """Sample i's data, its key (which data it is), and its entry's flags."""
        d = self.defs
        k = self.aliases.get(i, i)
        if k in self.ps1_of:
            j = self.ps1_of[k]
            data = bytes(self.ps1[j])
            data = data[:len(data) // ADPCM_BLOCK * ADPCM_BLOCK]
            return data, ("ps1", j), (d["SOUND_FORMAT_ADPCM"] << d["SOUND_FORMAT_SHIFT"] |
                                      d["SOUND_RATE_FAST"] << d["SOUND_RATE_SHIFT"])
        data = bytes(self.saturn[k])[:2 * LENGTH_MAX]
        return data, ("saturn", k), (d["SOUND_FORMAT_PCM16"] << d["SOUND_FORMAT_SHIFT"] |
                                     self.rate_of(k) << d["SOUND_RATE_SHIFT"])

    def build(self):
        """(file data, offsets table: two words a sample)"""
        data, entries, placed = bytearray(), [], {}
        self.counts = {"ps1": 0, "saturn": 0, "none": 0}
        for i in range(len(self.saturn)):
            if i not in self.ids:
                entries += [0, 0]
                self.counts["none"] += 1
                continue
            sample, key, flags = self.source(i)
            self.counts[key[0]] += 1
            if key not in placed:
                placed[key] = len(data)
                data += sample
                while len(data) & 3:
                    data.append(0)
            entries += [placed[key] | flags, len(sample)]
        if len(data) > self.budget:
            raise ValueError(f"{len(data)} bytes of sound samples, {self.budget} fit")
        assert len(data) <= self.defs["SOUND_OFFSET_MASK"]
        return bytes(data), entries
