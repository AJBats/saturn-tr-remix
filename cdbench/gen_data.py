"""Test data for cdbench (cdbench/bench.cpp): STREAM.BIN for the data track (Mode 1, 2048 bytes a sector)
and FORM2.DAT for a Mode 2 Form 2 track (2324 bytes a sector), each SECTORS sectors of a pattern the
bench checks word by word: sector k's word i is k * 0x9E3779B1 + i * 0x85EBCA77 + salt, big-endian, the
salt 0 on the data track and 0x5A5A5A5A on the Form 2 track.

usage: gen_data.py OUTDIR
"""
import os
import sys

import numpy as np

SECTORS = 1500      # 10 seconds at 2x


def pattern(words, salt):
    k = np.arange(SECTORS, dtype=np.uint64)[:, None]
    i = np.arange(words, dtype=np.uint64)[None, :]
    w = (k * 0x9E3779B1 + i * 0x85EBCA77 + salt) & 0xFFFFFFFF
    return w.astype('>u4').tobytes()


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, 'STREAM.BIN'), 'wb') as f:
    f.write(pattern(512, 0))
with open(os.path.join(out, 'FORM2.DAT'), 'wb') as f:
    f.write(pattern(581, 0x5A5A5A5A))
