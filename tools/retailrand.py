"""Who draws retail's control random numbers in the game tick that plays the host's tick T,
for a seed that parts: from a retailrun.py state of the same replay (one that plays the
same up to it), held before that tick, a breakpoint on Random_GetControl; each draw's
caller (PR) and the registers printed, for TICKS ticks. The host's side: RNG=1 with a
HOST_DEBUG=1 build, its callers named by powerpc-linux-gnu-addr2line.

    python -I tools/retailrand.py REPLAY HOSTRUN STATE T [TICKS]

HOSTRUN is the host's records for the replay (retailrun.py's OUT_hostrun.txt).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retailrun as rr  # noqa: E402
from capture import Mednafen  # noqa: E402

RANDOM_GET_CONTROL = 0x060168F8
SEED = 0x06060D34


def main():
    replay, hostrun, state, t = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    ticks = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    _, keys = rr.read_replay(replay)
    keys = keys[:t + 2000]
    host = rr.Records(rr.HOST_FIELDS)
    for line in open(hostrun):
        w = line.split()
        if w and w[0] == 'rec':
            if int(w[1]) >= len(keys):
                break
            host[int(w[1])] = dict(level=int(w[3]), inv=int(w[23]))
    kept, script_entries = rr.split_inventory(keys, host)
    i = kept.index(t)
    code, vars_off = rr.assemble('rrand')
    stub_vars = rr.STUB + vars_off
    m = Mednafen(rr.CUE, 'rrand', fast=True)
    try:
        m.send('deterministic', 'deterministic', 10)
        m.send(f'load_state {os.path.abspath(state).replace(os.sep, "/")}', 'ok load_state', 120)
        script = rr.Script(script_entries, vars_off)
        script.base = m.word(stub_vars + 36)
        script.write(m)
        window = rr.Window(keys, kept)
        window.fill(m, m.word(stub_vars + 4))
        rr.poke(m, stub_vars + 16, struct.pack('>I', i))       # held before game tick i
        while m.word(stub_vars + 4) < i:
            m.send('frame_advance 30', 'done frame_advance', 600)
            window.fill(m, m.word(stub_vars + 4))
            script.follow(m)
        m.send('frame_advance 2', 'done frame_advance', 60)
        print(f'held before game tick {i} (host {t}), seed {m.word(SEED):08x}', flush=True)
        for k in range(ticks):
            m.send(f'breakpoint {RANDOM_GET_CONTROL:08X}', 'breakpoint', 10)
            rr.poke(m, stub_vars + 16, struct.pack('>I', i + k + 1))     # through this tick, held before the next
            hits = 0
            while hits < 50:
                try:
                    m.send('run', 'break pc=', 8)
                except SystemExit:      # no draw for 8 s: held before the next tick
                    m.send('frame_advance 1', 'done frame_advance', 30)
                    break
                regs = m.send('dump_regs', 'R0=', 10)
                pr = re.search(r'PR=([0-9A-Fa-f]{8})', regs).group(1)
                print(f'  host tick {t + k}: Random_GetControl from {pr} (seed {m.word(SEED):08x}) '
                      f'{regs.strip()[:200]}', flush=True)
                hits += 1
            m.send('breakpoint_clear', 'breakpoint', 10)
            print(f'  host tick {t + k}: {hits} draws', flush=True)
    finally:
        m.quit()


if __name__ == '__main__':
    main()
