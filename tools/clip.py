"""Records a clip of a disc in a Mednafen of our own: the game's picture every
other frame (a new one each 30 Hz tick) from FIRST to LAST, packed into one
sheet, and with --sound the game's sound over the same frames, for the
decisions pages (side by side, a build against another).
python -I tools/clip.py DISC ELF NAME EVENTS FIRST LAST [--sound]
EVENTS: FRAME:BUTTON:VBLANKS,...  (Mednafen's buttons: A action, B jump, C draw,
        START the ring, UP...), kill@FRAME:ITEM[:VALUE] (that item's health to 0,
        or to VALUE: a creature dies by a hit, so 1 and a shot kills it),
        hp@FRAME:VALUE (Lara's health to that)
Writes build/clips/NAME.jpg (frames left to right, top to bottom), NAME.wav
and NAME.json (frame size, count, columns, frames per second)."""
import json
import os
import struct
import subprocess
import sys
import time
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + '/tools')
os.chdir(ROOT)
from capture import Mednafen  # noqa: E402
from PIL import Image  # noqa: E402

BIN = ROOT + '/toolchain/gcc-14.2.0/sh2eb-elf/bin/sh2eb-elf-'
ITEM_SIZE = 76          # sizeof(ItemObj): items[] is MAX_ITEMS (256) of them
ITEM_HEALTH = 44
COLUMNS = 10

args = [a for a in sys.argv[1:] if not a.startswith('--')]
sound = '--sound' in sys.argv
disc, elf, name, events_arg, first, last = args[:6]
first, last = int(first), int(last)
S = {}
for line in subprocess.run([BIN + 'nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        S[p[2]] = int(p[0], 16)
OUT = f'{ROOT}/build/clips'
FRAMES = f'{OUT}/{name}_frames'
os.makedirs(FRAMES, exist_ok=True)
for f in os.listdir(FRAMES):
    os.remove(os.path.join(FRAMES, f))
raw_wav = f'{OUT}/{name}_whole.wav'

events = []
for p in filter(None, events_arg.split(',')):
    if p.startswith('kill@'):
        f, item, *value = p[5:].split(':')
        events.append((int(f), 'poke', (S['_items'] + int(item) * ITEM_SIZE + ITEM_HEALTH, int(value[0]) if value else 0)))
    elif p.startswith('hp@'):
        f, v = p[3:].split(':')
        events.append((int(f), 'hp', int(v)))
    else:
        f, b, n = p.split(':')
        events.append((int(f), 'press', b))
        events.append((int(f) + int(n), 'release', b))
for f in range(first, last + 1, 2):
    events.append((f, 'shot', f))
events.sort(key=lambda e: (e[0], e[1] != 'release', e[1] == 'shot'))

m = Mednafen(f'{disc}/tomb.cue', name, sound=raw_wav if sound else None)
end_frame = last
try:
    for fr, kind, arg in events:
        m.send(f'run_to_frame {fr}', 'done run_to_frame', 1200)
        if kind == 'press':
            m.send(f'input {arg}', 'ok input', 10)
        elif kind == 'release':
            m.send(f'input_release {arg}', 'ok input_release', 10)
        elif kind == 'poke':
            addr, v = arg
            m.send('poke %08X %02X %02X' % (addr, (v >> 8) & 0xFF, v & 0xFF), 'ok poke', 10)
        elif kind == 'hp':
            lara = m.word(S['_players'])
            if lara:
                m.send('poke %08X %02X %02X' % (lara + ITEM_HEALTH, (arg >> 8) & 0xFF, arg & 0xFF), 'ok poke', 10)
        else:
            m.send(f'screenshot {FRAMES}/{arg:06d}.png', 'screenshot', 30)
    # the last picture is taken in the next frame: run past it, then stop the sound there
    end_frame = last + 2
    m.send(f'run_to_frame {end_frame}', 'done run_to_frame', 60)
finally:
    m.quit()
    time.sleep(1)

shots = sorted(f for f in os.listdir(FRAMES) if f.endswith('.png'))
if not shots:
    sys.exit('no pictures were taken')
images = [Image.open(os.path.join(FRAMES, f)).convert('RGB') for f in shots]
w, h = images[0].size
rows = (len(images) + COLUMNS - 1) // COLUMNS
sheet = Image.new('RGB', (w * COLUMNS, h * rows))
for i, im in enumerate(images):
    sheet.paste(im.resize((w, h)), ((i % COLUMNS) * w, (i // COLUMNS) * h))
sheet.save(f'{OUT}/{name}.jpg', quality=80)
meta = {'width': w, 'height': h, 'frames': len(images), 'columns': COLUMNS, 'fps': 30, 'first': first}

if sound:
    # the recording runs from boot to the quit at end_frame: its samples a frame,
    # from its length, place FIRST..LAST in it
    with wave.open(raw_wav, 'rb') as src:
        rate, ch, width, total = src.getframerate(), src.getnchannels(), src.getsampwidth(), src.getnframes()
        per_frame = total / end_frame
        start = int(first * per_frame)
        count = int((last + 2 - first) * per_frame)
        src.setpos(start)
        data = src.readframes(count)
    with wave.open(f'{OUT}/{name}.wav', 'wb') as dst:
        dst.setnchannels(ch)
        dst.setsampwidth(width)
        dst.setframerate(rate)
        dst.writeframes(data)
    os.remove(raw_wav)
    meta['sound'] = {'rate': rate, 'channels': ch, 'samples_per_frame': round(per_frame, 3)}
json.dump(meta, open(f'{OUT}/{name}.json', 'w'), indent=1)
print(f'{name}: {len(images)} pictures {w}x{h} -> {OUT}/{name}.jpg' + (f', sound {OUT}/{name}.wav' if sound else ''))
