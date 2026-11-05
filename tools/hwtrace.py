"""A self-tracing disc (HW_TRACE_AT) on the Saturn: played, its playback recorded, read: a stage table of real frames.
python -I tools/hwtrace.py FOLDER TAG [SETTLE=70] [SECONDS=30]    Leaves build/night/TAG.mkv, TAG.json; prints the table."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
folder, tag = sys.argv[1:3]
settle = sys.argv[3] if len(sys.argv) > 3 else '70'
seconds = sys.argv[4] if len(sys.argv) > 4 else '30'
os.makedirs('build/night', exist_ok=True)
out = f'build/night/{tag}'


def hw(*a):
    r = subprocess.run([sys.executable, '-I', 'tools/hw.py'] + list(a), capture_output=True, text=True)
    print(r.stdout[-300:], r.stderr[-300:], flush=True)
    return r.returncode


def off():
    """It's a 30 year old machine: never left on after a capture."""
    hw('lock', 'hwtrace', '--why', 'switching it off')
    hw('power', 'off', '--as', 'hwtrace')
    hw('unlock', 'hwtrace')


if hw('test', folder, '--as', 'hwtrace', '--why', 'a trace', '--out', out, '--settle', settle, '--shots', '1', '--wait', '20'):
    off()
    sys.exit(1)
hw('lock', 'hwtrace', '--why', 'recording the trace')
code = hw('record', seconds, '--out', out + '.mkv', '--as', 'hwtrace')
off()
if code:
    sys.exit(code)
r = subprocess.run([sys.executable, '-I', 'tools/trace.py', 'video', out + '.mkv', '--out', out + '.json'], capture_output=True, text=True)
print(r.stdout[-3500:], r.stderr[-800:])
