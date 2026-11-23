"""A self-tracing disc (HW_TRACE_AT) on the Saturn: played, its playback recorded, read: a stage table of real frames.
python -I tools/hwtrace.py FOLDER TAG [SECONDS=6]    Leaves build/night/TAG.mkv, TAG.json; prints the table.

No fixed waits: the disc is deployed and booted, then a picture taken each second until the trace's playback page is
up (its blue border), and only one pass of the playback recorded (8 vertical blanks a frame: 6 s covers the 7 frames
the rings hold at a busy level; a 127-frame ring needs about 22)."""
import os
import subprocess
import sys
import time

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
folder, tag = sys.argv[1:3]
seconds = sys.argv[3] if len(sys.argv) > 3 else '6'
os.makedirs('build/night', exist_ok=True)
out = f'build/night/{tag}'
NAME = 'hwtrace'
PAGE_WAIT = 150     # seconds from the game's boot for the playback page to come up


def hw(*a):
    r = subprocess.run([sys.executable, '-I', 'tools/hw.py'] + list(a), capture_output=True, text=True)
    print(r.stdout[-300:], r.stderr[-300:], flush=True)
    return r.returncode


def off():
    """It's a 30 year old machine: never left on after a capture."""
    hw('lock', NAME, '--why', 'switching it off')
    hw('power', 'off', '--as', NAME)
    hw('unlock', NAME)


def playback_up(path):
    """The playback page's blue border on both sides of the picture."""
    im = Image.open(path).convert('RGB').resize((192, 108))
    px = im.load()
    hits = sum(1 for y in range(20, 90) for x in (28, 29, 30, 162, 163)
               if px[x, y][2] > 180 and px[x, y][0] < 90 and px[x, y][1] > 80)
    return hits / (70 * 5) > 0.5


t0 = time.time()
if hw('test', folder, '--as', NAME, '--why', 'a trace', '--out', out, '--settle', '0', '--shots', '0', '--wait', '20'):
    off()
    sys.exit(1)
hw('lock', NAME, '--why', "waiting for the trace's playback, then recording it")
up = False
while time.time() - t0 < PAGE_WAIT + 120:
    if hw('shot', '--out', out + '_poll.png') == 0 and playback_up(out + '_poll.png'):
        up = True
        break
    time.sleep(1)
print(f'playback page {"up" if up else "never seen"} at {time.time() - t0:.0f} s', flush=True)
code = hw('record', seconds, '--out', out + '.mkv', '--as', NAME) if up else 1
off()
print(f'off at {time.time() - t0:.0f} s', flush=True)
if code:
    sys.exit(code)
r = subprocess.run([sys.executable, '-I', 'tools/trace.py', 'video', out + '.mkv', '--out', out + '.json'], capture_output=True, text=True)
print(r.stdout[-3500:], r.stderr[-800:])
