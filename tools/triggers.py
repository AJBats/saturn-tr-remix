"""Lists a level's floor triggers that do more than activate items: flip maps,
effects, level ends, flows and so on, with the room and sector they're on.

usage: python tools/triggers.py <DATA dir> <LEVEL> [action ...]

Actions are names such as FLIP, FLIP_ON, EFFECT (all but ACTIVATE_OBJECT,
ACTIVATE_CAMERA and SOUNDTRACK by default). A sector is printed as
room:x:z, which tools/pack_level.py takes as LARA_START.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trsat

TRIGGER_TYPES = ["ACTIVATE", "PAD", "SWITCH", "KEY", "PICKUP", "OBJECT", "ANTIPAD", "COMBAT", "DUMMY"]
ACTIONS = ["ACTIVATE_OBJECT", "ACTIVATE_CAMERA", "FLOW", "FLIP", "FLIP_ON", "FLIP_OFF", "CAMERA_TARGET",
           "END", "SOUNDTRACK", "EFFECT", "SECRET", "CLEAR_BODIES", "FLYBY", "CUTSCENE"]
FLOOR_TRIGGER = 4


def sector_triggers(floors, index):
    """(trigger type, [(action, args)]) of the trigger in a sector's floor data."""
    if index == 0:
        return None
    while index < len(floors):
        func = floors[index]
        kind = func & 0x1F
        if kind in (1, 2, 3):           # portal, floor and ceiling slants: one argument
            index += 2
        elif kind == FLOOR_TRIGGER:
            index += 2                  # the trigger's timer/mask word
            cmds = []
            while True:
                cmd = floors[index]
                index += 1
                action = (cmd >> 10) & 0x1F
                cmds.append((ACTIONS[action] if action < len(ACTIONS) else str(action), cmd & 0x3FF))
                if action == 1:         # cameras take a second word
                    cmd = floors[index]
                    index += 1
                if cmd & 0x8000:
                    break
            return TRIGGER_TYPES[(func >> 8) & 0x7F], cmds
        else:
            index += 1
        if func & 0x8000:
            break
    return None


def main():
    data_dir, name = sys.argv[1:3]
    wanted = set(sys.argv[3:]) or set(ACTIONS) - {"ACTIVATE_OBJECT", "ACTIVATE_CAMERA", "SOUNDTRACK"}
    lvl = trsat.load(data_dir, name)
    for ri, room in enumerate(lvl.rooms):
        for i, sector in enumerate(room.sectors):
            trig = sector_triggers(lvl.floors, sector[0])
            if not trig:
                continue
            kind, cmds = trig
            if any(a in wanted for a, _ in cmds):
                sx, sz = divmod(i, room.z_sectors)
                desc = " ".join(f"{a}({v})" for a, v in cmds)
                alt = f" alt {room.alternate}" if room.alternate >= 0 else ""
                print(f"{ri}:{sx}:{sz} {kind:8s} {desc}{alt}")


if __name__ == "__main__":
    main()
