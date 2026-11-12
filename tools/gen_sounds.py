#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the built-in sound effects (original, synthesised, MIT).

    python3 tools/gen_sounds.py [data_dir]        (standard library only)

Writes data/sounds/default/*.wav, 22050 Hz mono 16-bit.
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 22050


def write(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        peak = max(1e-6, max(abs(s) for s in samples))
        scale = 0.9 / peak if peak > 0.9 else 1.0
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * scale)) * 32000)) for s in samples))


def lowpass(x, k):
    """one-pole low pass, k in 0..1 (smaller = darker)"""
    out, y = [], 0.0
    for s in x:
        y += k * (s - y)
        out.append(y)
    return out


def env(n, attack, decay_pow=1.0):
    a = max(1, int(attack * RATE))
    return [(i / a if i < a else (1 - (i - a) / max(1, n - a)) ** decay_pow) for i in range(n)]


def noise(n, rng):
    return [rng.uniform(-1, 1) for _ in range(n)]


def explosion(seconds, darkness, seed):
    rng = random.Random(seed)
    n = int(seconds * RATE)
    x = noise(n, rng)
    low = lowpass(x, darkness)
    lower = lowpass(low, darkness * 0.5)
    e = env(n, 0.004, 2.2)
    out = []
    for i in range(n):
        t = i / RATE
        rumble = math.sin(2 * math.pi * (55 - 25 * t / seconds) * t) * 0.5
        crack = x[i] * max(0.0, 1 - t / 0.04)               # the initial bang
        out.append((low[i] * 2.5 + lower[i] * 4 + rumble * (1 - t / seconds)) * e[i] + crack * 0.6)
    return out


def cannon(seed):
    rng = random.Random(seed)
    n = int(0.35 * RATE)
    x = noise(n, rng)
    low = lowpass(x, 0.12)
    e = env(n, 0.002, 3.0)
    return [(math.sin(2 * math.pi * (120 - 260 * i / n) * i / RATE) * 0.8 + low[i] * 3 +
             x[i] * max(0, 1 - i / (0.015 * RATE))) * e[i] for i in range(n)]


def mg(seed):
    rng = random.Random(seed)
    n = int(0.07 * RATE)
    x = noise(n, rng)
    band = lowpass(x, 0.5)
    e = env(n, 0.001, 4.0)
    return [(band[i] * 2 + math.sin(2 * math.pi * 180 * i / RATE) * 0.4) * e[i] for i in range(n)]


def rocket(seed):
    rng = random.Random(seed)
    n = int(0.7 * RATE)
    x = noise(n, rng)
    hi = [a - b for a, b in zip(x, lowpass(x, 0.15))]       # hissy band
    e = env(n, 0.06, 1.5)
    return [(hi[i] * 1.2 + math.sin(2 * math.pi * (300 + 500 * i / n) * i / RATE) * 0.15) * e[i] for i in range(n)]


def mortar(seed):
    rng = random.Random(seed)
    n = int(0.3 * RATE)
    x = lowpass(noise(n, rng), 0.08)
    e = env(n, 0.002, 2.0)
    return [(math.sin(2 * math.pi * 90 * i / RATE) * 0.9 + x[i] * 3) * e[i] for i in range(n)]


def hit(seed):
    rng = random.Random(seed)
    n = int(0.25 * RATE)
    e = env(n, 0.001, 3.0)
    parts = [(1130, 0.5), (1873, 0.35), (2640, 0.25), (720, 0.4)]
    return [(sum(a * math.sin(2 * math.pi * f * i / RATE) for f, a in parts) + rng.uniform(-0.3, 0.3) *
             max(0, 1 - i / (0.01 * RATE))) * e[i] for i in range(n)]


def splat(seed):
    rng = random.Random(seed)
    n = int(0.28 * RATE)
    x = lowpass(noise(n, rng), 0.3)
    e = env(n, 0.003, 1.6)
    return [(x[i] * 2.5 * (1 + math.sin(2 * math.pi * 30 * i / RATE)) +
             math.sin(2 * math.pi * (160 - 120 * i / n) * i / RATE) * 0.6) * e[i] for i in range(n)]


def splash_sfx(seed):
    """something heavy landing in water: a plop, then spray falling back"""
    rng = random.Random(seed)
    n = int(0.7 * RATE)
    x = noise(n, rng)
    hiss = [a - b for a, b in zip(x, lowpass(x, 0.3))]
    out = []
    for i in range(n):
        t = i / RATE
        plop = math.sin(2 * math.pi * (300 - 220 * min(1, t / 0.08)) * t) * max(0, 1 - t / 0.12)
        spray = hiss[i] * (min(1, t / 0.05)) * (1 - i / n) ** 1.6
        out.append(plop * 0.9 + spray * 0.8)
    return out


def pickup():
    out = []
    for f in (523, 659, 784, 1047):                           # C E G C
        n = int(0.08 * RATE)
        for i in range(n):
            sq = 1.0 if math.sin(2 * math.pi * f * i / RATE) > 0 else -1.0
            out.append(sq * 0.35 * (1 - i / n) ** 0.5)
    return out


def mine():
    n = int(0.12 * RATE)
    return [(math.sin(2 * math.pi * 900 * i / RATE) * (1 if i < n / 3 else 0) +
             math.sin(2 * math.pi * 600 * i / RATE) * (1 if i > n / 2 else 0)) * 0.4 for i in range(n)]


def heli_loop(seed):
    """rotor chop: noise pulses at 12 Hz over a low turbine hum; loops"""
    rng = random.Random(seed)
    chop = 12
    n = RATE // chop * 6                                     # whole number of chops
    x = lowpass(noise(n, rng), 0.25)
    out = []
    for i in range(n):
        ph = (i * chop / RATE) % 1.0
        pulse = max(0.0, 1 - ph * 5) ** 2
        out.append(x[i] * 3 * pulse + math.sin(2 * math.pi * 110 * i / RATE) * 0.15 +
                   math.sin(2 * math.pi * 1650 * i / RATE) * 0.03)
    return out


def engine_loop(base, seed):
    """diesel drone: a few harmonics of a low tone with rough noise; loops"""
    rng = random.Random(seed)
    n = int(round(RATE / base)) * int(base * 0.5)              # whole cycles, about half a second
    period = RATE / base
    x = lowpass(noise(n, rng), 0.1)
    out = []
    for i in range(n):
        p = 2 * math.pi * i / period
        firing = max(0.0, math.sin(p * 0.5)) ** 4              # cylinder thumps
        out.append(math.sin(p) * 0.5 + math.sin(2 * p) * 0.25 + math.sin(3 * p) * 0.12 +
                   x[i] * 2 * (0.4 + firing) + firing * 0.3)
    return out


def main():
    data = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "data")
    out = os.path.join(data, "sounds", "default")
    os.makedirs(out, exist_ok=True)
    sounds = {
        "explode": explosion(1.3, 0.06, 1),
        "explode_small": explosion(0.55, 0.12, 2),
        "cannon": cannon(3),
        "mg": mg(4),
        "rocket": rocket(5),
        "mortar": mortar(6),
        "hit": hit(7),
        "splat": splat(8),
        "pickup": pickup(),
        "mine": mine(),
        "splash": splash_sfx(11),
        "heli": heli_loop(9),
        "engine": engine_loop(48, 10),
    }
    for name, samples in sounds.items():
        write(os.path.join(out, name + ".wav"), samples)
    with open(os.path.join(out, "sounds.txt"), "w") as f:
        f.write("name=Default\nauthor=OpenFirePower contributors\nlicense=MIT\n")
    print(f"wrote {len(sounds)} sounds to {os.path.abspath(out)}")


if __name__ == "__main__":
    main()
