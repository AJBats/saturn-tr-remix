"""The Saturn on the desk, driven from here.

    hw.py power
        whether the Saturn's smart plug is on, and what it draws
    hw.py power off
        cuts the power; returns once the plug reads nothing drawn and the
        Saturn's power supply has drained
    hw.py power on
        restores it, and checks the Saturn starts drawing (it only does with
        its own power button pressed in)
    hw.py power cycle
        off, then on

Exits with 1 if the plug doesn't answer or the reading isn't what it should
be. The plug is a Shelly Plug US Gen4 on the home network, found by its name.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

PLUG = 'shellyplugusg4-e8f60a7d5608.local'

OFF_WATTS = 1.0     # less than this is nothing drawn
ON_WATTS = 2.0      # more than this is the Saturn running
READING_S = 5       # how long the plug's reading gets to reach either
DRAIN_S = 5         # the power supply's capacitors, after the reading falls


def fail(message):
    print(f'hw.py: {message}', file=sys.stderr)
    sys.exit(1)


def plug(method, **params):
    url = f'http://{PLUG}/rpc/{method}?{urllib.parse.urlencode(params)}'
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return json.load(r)
    except (urllib.error.URLError, OSError, ValueError) as e:
        fail(f'the plug at {PLUG} did not answer: {e}')


def status():
    return plug('Switch.GetStatus', id=0)


def wait_for_watts(reached):
    """The plug's reading once reached(watts) holds, None if it doesn't in time."""
    deadline = time.monotonic() + READING_S
    while True:
        watts = status()['apower']
        if reached(watts):
            return watts
        if time.monotonic() > deadline:
            return None
        time.sleep(0.25)


def power_off():
    plug('Switch.Set', id=0, on='false')
    watts = wait_for_watts(lambda w: w < OFF_WATTS)
    if watts is None:
        fail(f"switched off, but it still draws {status()['apower']} W")
    time.sleep(DRAIN_S)
    print(f'off: {watts} W, drained')


def power_on():
    plug('Switch.Set', id=0, on='true')
    watts = wait_for_watts(lambda w: w > ON_WATTS)
    if watts is None:
        fail("switched on, but nothing draws power: is the Saturn's button pressed in?")
    print(f'on: {watts} W')


def power(action):
    if action == 'off':
        power_off()
    elif action == 'on':
        power_on()
    elif action == 'cycle':
        power_off()
        power_on()
    else:
        s = status()
        print(f"{'on' if s['output'] else 'off'}: {s['apower']} W")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('power')
    p.add_argument('action', nargs='?', choices=['on', 'off', 'cycle'])
    args = ap.parse_args()
    power(args.action)


if __name__ == '__main__':
    main()
