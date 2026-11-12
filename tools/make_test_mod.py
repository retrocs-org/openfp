#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# A small ProTracker module (M.K., 4 channels) for testing the game's music: two
# generated samples (a soft square bass, a triangle lead) and a short looping tune.
# Our own, CC0. Usage: make_test_mod.py OUT.mod [TITLE]
import math, struct, sys

out = sys.argv[1] if len(sys.argv) > 1 else "test.mod"
title = (sys.argv[2] if len(sys.argv) > 2 else "OFP test tune")[:20]

def sample(kind, n=512):
    s = bytearray()
    for i in range(n):
        ph = i / n * 4                                   # 4 cycles in the sample: a clean loop
        if kind == "square":
            v = 70 if (ph % 1) < 0.5 else -70
        else:                                            # triangle
            p = ph % 1
            v = int(110 * (4 * p - 1 if p < 0.5 else 3 - 4 * p))
        s.append(v & 0xFF)
    return bytes(s)

samples = [("bass", sample("square"), 48), ("lead", sample("triangle"), 40)]

# ProTracker periods, octaves 1-3 (C-1 .. B-3)
periods = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
           428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
           214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113]
def note(n):                                             # "C-2" -> period
    names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    return periods[(int(n[2]) - 1) * 12 + names.index(n[:2])]

def cell(period=0, smp=0, eff=0, arg=0):
    return struct.pack(">BBBB", (smp & 0xF0) | (period >> 8), period & 0xFF, ((smp & 0x0F) << 4) | eff, arg)

bass = ["C-1", None, "C-1", None, "G-1", None, "A#1", None] * 2 + ["F-1", None, "F-1", None, "C-2", None, "D#1", None] * 2
lead = ["C-3", None, None, "D#3", None, None, "G-3", None, "F-3", None, "D#3", None, "C-3", None, None, None,
        "A#2", None, None, "C-3", None, None, "D#3", None, "D-3", None, "C-3", None, "A#2", None, None, None]

def pattern(transpose_lead=False):
    rows = []
    for r in range(64):
        b = bass[r % len(bass)]; l = lead[r % len(lead)]
        c0 = cell(note(b), 1) if b else cell()
        c1 = cell(note(l), 2) if l else cell()
        c2 = cell(0, 0, 0xF, 6) if r == 0 else cell()  # speed 6
        rows.append(c0 + c1 + c2 + cell())
    return b"".join(rows)

hdr = title.encode().ljust(20, b"\0")
for i in range(31):
    if i < len(samples):
        name, data, vol = samples[i]
        hdr += name.encode().ljust(22, b"\0") + struct.pack(">HBBHH", len(data) // 2, 0, vol, 0, len(data) // 2)
    else:
        hdr += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
order = [0, 1]
hdr += bytes([len(order), 127]) + bytes(order + [0] * (128 - len(order))) + b"M.K."
body = pattern() + pattern(True)
data = b"".join(s[1] for s in samples)
open(out, "wb").write(hdr + body + data)
print(f"{out}: {len(hdr + body + data)} bytes")
