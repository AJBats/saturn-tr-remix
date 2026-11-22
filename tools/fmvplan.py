"""Will each FMV stream from the Saturn's 2x drive, and at what quality? (docs/ps1-fmv.md, "The other route")

Reads what tools/cinepak.py encode wrote (DIR/NAME_SETTING.json: every frame's bytes at a setting) and the
sound's chunk sizes (DIR/NAME_sound.json: tools/lossless.py's encoding of the retail PCM). For each drive
(Mode 1, 307200 bytes a second; Mode 2 Form 2, 348600) and sound (the retail 16-bit PCM, 89016 bytes a
second; the same, lossless) it plans each video:

A player is simulated sector by sector (1/150 s). The stream holds everything in the order it is needed:
each frame PREROLL seconds after the start plus its time, each sound chunk SOUND_LEAD before its first
sample plays. The drive reads a sector whenever the buffer (the RAM ring and the CD block's own sectors
together) has room; a frame's or chunk's bytes leave the buffer at its deadline, and must all be in by then.

Every group of 12 frames starts with a keyframe, so groups from different settings join freely. All groups
start at the best setting; while something is late, the largest group not yet at the lowest setting in the
seconds before it steps down one setting. The result is each video's share of frames at each setting.

    fmvplan.py DIR [--preroll S] [--buffer KB ...]
"""
import argparse
import glob
import json
import os
import sys

RATES = {'Mode 1': 2048 * 150, 'Form 2': 2324 * 150}
SETTINGS = ['q1s16', 'q1s8', 'q2s8', 'q4s8']     # the ladder, best first (--ladder)
FPS = 15
GOP = 12
CHUNK_S = 1024 / 22254
SOUND_LEAD = 0.5            # seconds of decoded sound ahead of the play position
FRAME_OVERHEAD = 16         # a frame's header in the stream
SOUND_OVERHEAD = 8
LOOKBACK = 6.0              # seconds before a late deadline whose groups may step down


def first_late(deadlines, ends, rate, buffer):
    """Index of the first item not all in by its deadline, or None. Items are back to back in the stream:
    item i ends at byte ends[i]; it leaves the buffer at deadlines[i] (ascending)."""
    sector = rate / 150
    arrived = 0.0
    freed = 0           # bytes of items past their deadline
    i, n = 0, len(deadlines)
    step = 0
    while i < n:
        t = step / 150
        # what is due now leaves the buffer, all in or late
        while i < n and deadlines[i] <= t:
            if arrived < ends[i]:
                return i
            freed = ends[i]
            i += 1
        if arrived < ends[-1] and arrived - freed + sector <= buffer:
            arrived += sector
        step += 1
    return None


def build(frames, sound):
    """The stream's items in order: (deadline, bytes, frame index or -1 for sound)."""
    out = [(PRE + k / FPS, s + FRAME_OVERHEAD, k) for k, s in enumerate(frames)]
    out += [(PRE + j * CHUNK_S - SOUND_LEAD, s + SOUND_OVERHEAD, -1) for j, s in enumerate(sound)]
    out.sort()
    return out


def plan(sizes, sound, rate, buffer):
    """Per group, the index of its setting; None if late even all at the lowest."""
    frames_n = len(sizes[SETTINGS[0]])
    groups = (frames_n + GOP - 1) // GOP
    level = [0] * groups
    while True:
        frames = [sizes[SETTINGS[level[k // GOP]]][k] for k in range(frames_n)]
        stream = build(frames, sound)
        deadlines = [d for d, _, _ in stream]
        ends, total = [], 0
        for _, b, _ in stream:
            total += b
            ends.append(total)
        late = first_late(deadlines, ends, rate, buffer)
        if late is None:
            return level
        t = deadlines[late]
        # the groups shown in the LOOKBACK seconds up to t that can still step down: the largest goes
        lo = max(0, int((t - PRE - LOOKBACK) * FPS) // GOP)
        hi = min(groups - 1, int((t - PRE) * FPS) // GOP)
        cands = [g for g in range(lo, hi + 1) if level[g] < len(SETTINGS) - 1]
        if not cands:
            return None
        g = max(cands, key=lambda g: sum(sizes[SETTINGS[level[g]]][g * GOP:(g + 1) * GOP]))
        level[g] += 1


def main():
    global PRE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dir')
    ap.add_argument('--preroll', type=float, default=1.0)
    ap.add_argument('--buffer', type=int, nargs='+', default=[1024], help='KB of buffer')
    ap.add_argument('--ladder', nargs='+', default=SETTINGS, help='settings, best first')
    args = ap.parse_args()
    PRE = args.preroll
    SETTINGS[:] = args.ladder

    videos = {}
    for path in glob.glob(os.path.join(args.dir, '*_*q*s*.json')):
        j = json.load(open(path))
        videos.setdefault(j['name'], {})[j['setting']] = j
    sounds = {}
    for p in glob.glob(os.path.join(args.dir, '*_sound.json')):
        j = json.load(open(p))
        sounds[j['name']] = j

    print(f'preroll {PRE} s; sound decoded {SOUND_LEAD} s ahead; shares of frames at ' + ' / '.join(SETTINGS))
    for buffer_kb in args.buffer:
        for drive, rate in RATES.items():
            for kind in ('PCM', 'lossless'):
                print(f'\n{drive}, {kind} sound, {buffer_kb} KB buffer')
                all_frames, all_counts = 0, [0] * len(SETTINGS)
                for name in sorted(videos):
                    sizes = {s: videos[name][s]['sizes'] for s in SETTINGS}
                    frames_n = len(sizes[SETTINGS[0]])
                    chunks = -(-frames_n * 22254 // FPS // 1024)
                    snd = sounds[name]['sizes']
                    sound = [4096] * chunks if kind == 'PCM' else (snd * 2)[:chunks]
                    level = plan(sizes, sound, rate, buffer_kb * 1024)
                    if level is None:
                        print(f'  {name:9s} late even all at {SETTINGS[-1]}')
                        continue
                    counts = [0] * len(SETTINGS)
                    for k in range(frames_n):
                        counts[level[k // GOP]] += 1
                    video = sum(sizes[SETTINGS[level[k // GOP]]][k] for k in range(frames_n))
                    kbs = video / (frames_n / FPS) / 1024
                    all_frames += frames_n
                    all_counts = [a + c for a, c in zip(all_counts, counts)]
                    print(f'  {name:9s} video {kbs:5.0f} KB/s  ' +
                          ' / '.join(f'{c / frames_n * 100:3.0f}%' for c in counts))
                print(f'  {"all":9s} {"":16s}' + ' / '.join(f'{c / all_frames * 100:3.0f}%' for c in all_counts))
    return 0


if __name__ == '__main__':
    sys.exit(main())
