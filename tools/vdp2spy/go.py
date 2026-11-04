"""Boots a disc in a private Mednafen and taps a button every so often until a
frame, as survey.py did, leaving the emulator up and paused there.

    SPY_NAME=name go.py CUE FRAME [EVERY] [BUTTONS]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spy

cue, frame = sys.argv[1], int(sys.argv[2])
every = int(sys.argv[3]) if len(sys.argv) > 3 else 150
buttons = sys.argv[4].split(',') if len(sys.argv) > 4 else ['START']
import capture
os.makedirs(spy.IPC, exist_ok=True)
capture.Mednafen(cue, spy.NAME)
press = 0
for at in range(0, frame, every):
    spy.advance(every - 8)
    b = buttons[press % len(buttons)]
    press += 1
    spy.send(f'input {b}', 'input')
    spy.advance(6)
    spy.send(f'input_release {b}', 'input_release')
    spy.advance(2)
print(spy.ack())
