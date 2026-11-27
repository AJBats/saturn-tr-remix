"""A retail run's differences from the host (tools/retailrun.py's OUT.txt beside OUT_host.txt),
sorted by what they do to play, not by whether a byte differs.

    python -I tools/retaildiff.py OUT.txt [OUT2.txt ...] [--list N]

Several OUT files (a run and its resumptions from states) are read as one, a later
one's record of a tick over an earlier one's.

Each tick is alike, or differs in the random seed only, in the level's statistics only,
or in Lara (place, room, heading, animation, health). A run of ticks that differ is an
episode, sorted into a tier by the worst it does:

  tier 1  nothing seen: the seed (random numbers drawn in another order or number), or a
          statistic a known difference explains (KNOWN)
  tier 2  small drift: Lara less than DRIFT apart, in the same room, health within one
          hit, and the two meet again
  tier 3  play differs: Lara in another room or DRIFT or more apart, an extra or a missing
          hit (HIT or more health), one dead and the other not, a statistic that differs
          (a kill, a pickup, a secret), or the two never meet again

Then, per level, what each finished it with (kills, pickups, secrets) and whether retail
finished it at all. The first ticks of a level (retail's record is the new level's, the
host's the old one's last) and the tick a ring stay ends (retail uses the ring's choice
inside the tick that opened it) are left out, as retailrun.py leaves them.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from retailrun import RETAIL_LEVEL  # noqa: E402

DRIFT = 256             # a quarter of a block
HIT = 100               # the smallest hit that counts (a wolf's bite is 50: two)
LEVEL_NAMES = ['TITLE', 'GYM', 'Caves', 'Vilcabamba', 'Lost Valley', 'Qualopec', 'CUT1', "St. Francis' Folly",
               'Colosseum', 'Palace Midas', 'The Cistern', 'Tihocan', 'CUT2', 'City of Khamoon', 'Obelisk of Khamoon',
               'Sanctuary of the Scion', "Natla's Mines", 'CUT3', 'Atlantis', 'CUT4', 'The Great Pyramid']
LARA = ('room', 'x', 'y', 'z', 'yaw', 'anim', 'frame', 'health')
# Differences we chose, by the host's level: (level, statistic, test(retail, host), why)
KNOWN = [
    (20, 'secrets', lambda r, h: r | 4 == h and not r & 4,
     "secret 3's trigger: the packer renumbers it to the third secret, as TRX does (retail counts the first again)"),
]


def name(level):
    return LEVEL_NAMES[level] if 0 <= level < len(LEVEL_NAMES) else f'level {level}'


def records(path):
    out = {}
    for line in open(path):
        w = line.split()
        if not w or w[0] != 'rec':
            continue
        r = dict(level=int(w[3]), room=int(w[5]), x=int(w[7]), y=int(w[8]), z=int(w[9]), yaw=int(w[11]),
                 anim=int(w[13]), frame=int(w[15]), health=int(w[17]), seed=int(w[19], 16))
        if len(w) > 25 and w[20] == 'kills':
            r.update(kills=int(w[21]), pickups=int(w[23]), secrets=int(w[25], 16))
        out[int(w[1])] = r
    return out


def inventory_ticks(path):
    """The host's ticks with the inventory open after them (its full run's records)."""
    inv = set()
    if os.path.exists(path):
        for line in open(path):
            w = line.split()
            if len(w) > 23 and w[0] == 'rec' and w[23] == '1':
                inv.add(int(w[1]))
    return inv


def known(level, stat, r, h):
    return next((why for lv, st, test, why in KNOWN if lv == level and st == stat and test(r, h)), None)


def tick_kind(r, h):
    """What differs at a tick: None (alike), 'seed', 'stats', 'known' or 'lara', and the details."""
    lara = [k for k in LARA if r[k] != h[k]]
    if lara:
        return 'lara', lara
    stats = [k for k in ('kills', 'pickups', 'secrets') if k in r and k in h and r[k] != h[k]]
    if stats:
        whys = [known(h['level'], k, r[k], h[k]) for k in stats]
        if all(whys):
            return 'known', whys
        return 'stats', [k for k, y in zip(stats, whys) if not y]
    if r['seed'] != h['seed']:
        return 'seed', []
    return None, []


class Episode:
    def __init__(self, t, level):
        self.start = self.end = t
        self.level = level
        self.kinds = set()
        self.apart = 0
        self.rooms = set()
        self.health = 0
        self.dead = False
        self.fields = set()
        self.stats = set()
        self.whys = set()
        self.met = False        # the two alike again after it

    def add(self, t, kind, details, r, h):
        self.end = t
        self.kinds.add(kind)
        if kind == 'lara':
            self.fields.update(details)
            self.apart = max(self.apart, max(abs(r['x'] - h['x']), abs(r['y'] - h['y']), abs(r['z'] - h['z'])))
            if r['room'] != h['room']:
                self.rooms.add((r['room'], h['room']))
            self.health = max(self.health, abs(r['health'] - h['health']))
            self.dead |= (r['health'] <= 0) != (h['health'] <= 0)
        elif kind == 'stats':
            self.stats.update(details)
        elif kind == 'known':
            self.whys.update(details)

    def tier(self):
        if self.rooms or self.apart >= DRIFT or self.health >= HIT or self.dead or self.stats or \
                ('lara' in self.kinds and not self.met):
            return 3
        if 'lara' in self.kinds:
            return 2
        return 1

    def describe(self):
        what = []
        if self.rooms:
            what.append('rooms ' + ', '.join(f'{a} (retail) / {b} (host)' for a, b in sorted(self.rooms)[:3]))
        if 'lara' in self.kinds:
            what.append(f'Lara up to {self.apart} apart' + (f', health up to {self.health} apart' if self.health else '') +
                        (' (' + ', '.join(sorted(self.fields)) + ')'))
        if self.dead:
            what.append('one of them dead')
        if self.stats:
            what.append('statistics differ: ' + ', '.join(sorted(self.stats)))
        if self.whys:
            what.append('known: ' + '; '.join(sorted(self.whys)))
        if self.kinds == {'seed'}:
            what.append('the random seed only')
        if not self.met:
            what.append('never alike again')
        return '; '.join(what)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('outs', nargs='+')
    ap.add_argument('--list', type=int, default=40, help='episodes of tiers 2 and 3 listed')
    a = ap.parse_args()
    retail, host, inv = {}, {}, set()
    for out in a.outs:
        base = os.path.splitext(out)[0]
        retail.update(records(out))
        host.update(records(base + '_host.txt'))
        inv |= inventory_ticks(base + '_hostrun.txt')
    ticks = sorted(t for t in retail if t in host)
    if not ticks:
        sys.exit('no ticks to compare')
    episodes, cur = [], None
    counts = {None: 0, 'seed': 0, 'stats': 0, 'known': 0, 'lara': 0}
    levels = {}     # the host's level: [first tick, last tick, retail's last record, host's last record]
    for t in ticks:
        r, h = retail[t], host[t]
        if r['level'] != RETAIL_LEVEL.get(h['level']):
            continue        # retail's first record of a level
        lv = levels.setdefault(h['level'], [t, t, r, h])
        lv[1], lv[2], lv[3] = t, r, h
        if t in inv or (t - 1) in inv:
            kind, details = (('seed', []) if r['seed'] != h['seed'] else (None, []))
        else:
            kind, details = tick_kind(r, h)
        counts[kind] += 1
        if kind is None:
            if cur:
                cur.met = True
                episodes.append(cur)
                cur = None
            continue
        if cur is None or cur.level != h['level']:
            if cur:
                episodes.append(cur)
            cur = Episode(t, h['level'])
        cur.add(t, kind, details, r, h)
    if cur:
        episodes.append(cur)

    n = len(ticks)
    print(f'{n} ticks compared (host ticks {ticks[0]}..{ticks[-1]}): {counts[None]} alike, {counts["seed"]} the seed only, '
          f'{counts["known"]} a known difference only, {counts["stats"]} the statistics, {counts["lara"]} Lara')
    by_tier = {1: [], 2: [], 3: []}
    for e in episodes:
        by_tier[e.tier()].append(e)
    print(f'{len(episodes)} episodes: tier 1 (nothing seen) {len(by_tier[1])}, tier 2 (small drift) {len(by_tier[2])}, '
          f'tier 3 (play differs) {len(by_tier[3])}')
    print()
    print('level                    ticks            kills     pickups   secrets   episodes 1/2/3')
    finished_hosts = sorted(levels)
    for i, lv in enumerate(finished_hosts):
        first, last, r, h = levels[lv]
        done = i + 1 < len(finished_hosts)      # retail went on to the next level
        tiers = [sum(1 for e in by_tier[k] if e.level == lv) for k in (1, 2, 3)]

        def stat(k):
            if k not in r:
                return '-'
            if r[k] == h[k]:
                return f'{h[k]:x}' if k == 'secrets' else str(h[k])
            mark = '' if known(lv, k, r[k], h[k]) else '!'
            return (f'{r[k]:x}/{h[k]:x}' if k == 'secrets' else f'{r[k]}/{h[k]}') + mark
        print(f'{name(lv):24} {first:7}..{last:<7} {stat("kills"):9} {stat("pickups"):9} {stat("secrets"):9} '
              f'{tiers[0]}/{tiers[1]}/{tiers[2]}' + ('' if done else '   (retail\'s last level: run ended in it)'))
    print('(retail/host where they differ; ! = not a known difference)')
    for tier in (3, 2):
        if not by_tier[tier]:
            continue
        print()
        print(f'tier {tier} episodes' + (f' (first {a.list})' if len(by_tier[tier]) > a.list else '') + ':')
        for e in by_tier[tier][:a.list]:
            print(f'  {name(e.level):24} ticks {e.start}..{e.end} ({e.end - e.start + 1}): {e.describe()}')
    if by_tier[1]:
        seeds = [e for e in by_tier[1] if e.kinds == {'seed'}]
        print()
        print(f'tier 1: {len(seeds)} episodes of the seed alone (longest {max((e.end - e.start + 1 for e in seeds), default=0)} '
              f'ticks), {len(by_tier[1]) - len(seeds)} of known differences')
    return 1 if by_tier[3] else 0


if __name__ == '__main__':
    sys.exit(main())
