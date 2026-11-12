#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The Amiga client's music: the desktop's six synthesised songs (src/platform/sdl2/music.c's Song table:
the same chords, bass, tune and drums) written out as small ProTracker modules (M.K., 31 samples) with
generated samples, for src/platform/amiga/modplay.c to play on Paula. Ours, MIT like the code.

Three channels: bass, tune, drums (the fourth stays empty: the game's sound effects have it).
Writes src/platform/amiga/music_mods.h, and with --mods DIR the .mod files too (to listen to anywhere).

    tools/amiga/gen_music.py [--mods DIR]
"""
import math
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")

MINOR = (0, 2, 3, 5, 7, 8, 10)
PHRYGIAN = (0, 1, 4, 5, 7, 8, 10)
DORIAN = (0, 2, 3, 5, 7, 9, 10)
AEOLIAN = (0, 2, 3, 5, 7, 8, 10)
# name, bpm, root, scale, chords, bass, lead, drums, wave (0 pulse, 1 triangle, 2 bell): as music.c has them
SONGS = (
    ("classic", 112, 45, MINOR, (0, 5, 3, 4, 0, 5, 6, 4),
     "0-0-4-0-0-0-4-7-", "0-2-4-=-2-1-0---4-4-5-4-2---0---", "k-h-s-hkk-h-s-hh", 0),
    ("desert", 96, 50, PHRYGIAN, (0, 1, 0, 6, 0, 1, 5, 0),
     "0---0-4-0---7-4-", "4-=-5-4-1-=-0---4-5-6-5-4-1-=---", "k--hs--hk-k-s--t", 1),
    ("jungle", 120, 43, DORIAN, (0, 0, 3, 3, 4, 3, 0, 6),
     "0--00--40--00-7-", "------0-2-4-----5-4-2-0---------", "k-tkt-s-k-ttk-st", 1),
    ("winter", 84, 52, AEOLIAN, (0, 5, 2, 6, 0, 3, 4, 4),
     "0-------4-------", "4---=---2---0---5---4---=-------", "k-------s-------", 2),
    ("scifi", 128, 40, MINOR, (0, 0, 5, 5, 3, 3, 4, 6),
     "0-07-0-70-07-0-7", "0247024702470247424742474247424-", "k-hhs-hhk-hhs-hk", 0),
    ("menu", 100, 45, MINOR, (0, 3, 4, 0, 5, 3, 6, 4),
     "0---4---0---4---", "0---2---4---=---2---1---0-------", "k-------s-------", 1),
)

# samples: 1 bass, 2 tune pulse, 3 tune triangle, 4 tune bell, 5 kick, 6 snare, 7 hat, 8 tom
BASS, PULSE, TRIANGLE, BELL, KICK, SNARE, HAT, TOM = range(1, 9)
LEAD_CYCLE, BASS_CYCLE = 32, 64          # bytes a cycle: at C-2 (period 428, 8363 Hz) MIDI 60 and 48


def wave(kind, cycle, cycles):
    out = bytearray()
    for i in range(cycle * cycles):
        p = (i % cycle) / cycle
        if kind == "pulse":
            v = 52 if p < 0.3 else -52
        elif kind == "square":
            v = 60 if p < 0.5 else -60
            v = int(v * 0.8 + 18 * math.sin(2 * math.pi * p))          # (softer edges: a rounder bass)
        elif kind == "triangle":
            v = int(100 * (4 * p - 1 if p < 0.5 else 3 - 4 * p))
        else:                                                            # bell: two sines
            v = int(70 * math.sin(2 * math.pi * p) + 30 * math.sin(2 * math.pi * 3 * p))
        out.append(v & 255)
    return bytes(out)


def drum(kind, rate=16574):              # (played at C-3, period 214: 16574 Hz)
    out, noise = bytearray(), 0x12345
    n = {"kick": 2600, "snare": 2400, "hat": 600, "tom": 2600}[kind]
    phase = 0.0
    for i in range(n):
        t = i / rate
        noise = (noise * 1103515245 + 12345) & 0x7fffffff
        white = ((noise >> 16) & 255) - 128
        if kind in ("kick", "tom"):
            f = (140 if kind == "kick" else 220) * math.exp(-t * 18) + (45 if kind == "kick" else 90)
            phase += 2 * math.pi * f / rate
            v = 120 * math.sin(phase) * math.exp(-t * 14)
        elif kind == "snare":
            v = (white * 0.7 + 40 * math.sin(2 * math.pi * 190 * t)) * math.exp(-t * 22)
        else:
            v = white * 0.8 * math.exp(-t * 60)
        out.append(int(max(-127, min(127, v))) & 255)
    return bytes(out)


SAMPLES = (                             # name, data, volume, loop (start, length in bytes; length 0: none)
    ("bass", wave("square", BASS_CYCLE, 2), 44, True),
    ("pulse", wave("pulse", LEAD_CYCLE, 2), 34, True),
    ("triangle", wave("triangle", LEAD_CYCLE, 2), 40, True),
    ("bell", wave("bell", LEAD_CYCLE, 2), 40, True),
    ("kick", drum("kick"), 64, False),
    ("snare", drum("snare"), 40, False),
    ("hat", drum("hat"), 22, False),
    ("tom", drum("tom"), 50, False),
)


def scale_note(scale, root, degree):
    octave = degree // 7
    return root + octave * 12 + scale[degree - octave * 7]


def period(midi, ref):
    """the ProTracker period playing a sample tuned so C-2 is MIDI ref (octaves folded into C-1..B-3)"""
    while midi < ref - 12:
        midi += 12
    while midi > ref + 23:
        midi -= 12
    return int(round(428 * 2 ** ((ref - midi) / 12)))


def module(song):
    name, bpm, root, scale, chords, bass, lead, drums, wave_kind = song
    speed = max(3, min(15, round(750 / bpm)))               # (16 steps a bar at 50 ticks a second: 750 / bpm a step)
    lead_sample = (PULSE, TRIANGLE, BELL)[wave_kind]
    rows = []                                               # 256 steps: 16 bars of 16
    for stp in range(256):
        bar, in_bar = stp // 16, stp % 16
        chord = chords[(bar // 2) % 8]
        cells = [(0, 0, 0, 0)] * 4
        b, l, d = bass[in_bar], lead[stp % 32], drums[in_bar]
        if b.isdigit():
            k = int(b)
            cells[0] = (BASS, period(scale_note(scale, root, chord + k) - 12, 48), 0, 0)
        else:
            cells[0] = (0, 0, 0xA, 0x03)                    # (rests: the bass fades, as the synthesiser's)
        if l.isdigit():
            cells[1] = (lead_sample, period(scale_note(scale, root, chord + int(l)) + 12, 60), 0, 0)
        elif l == "-":
            cells[1] = (0, 0, 0xA, 0x04)                    # ("=": held; "-": the tune fades)
        drum_sample = {"k": KICK, "s": SNARE, "h": HAT, "t": TOM}.get(d)
        if drum_sample:
            cells[2] = (drum_sample, 214, 0, 0)
        if stp == 0:
            cells[3] = (0, 0, 0xF, speed)                   # (the speed, once)
        rows.append(cells)
    out = bytearray(name.encode()[:20].ljust(20, b"\0"))
    for i in range(31):
        if i < len(SAMPLES):
            sname, data, vol, loop = SAMPLES[i]
            words = len(data) // 2
            out += sname.encode()[:22].ljust(22, b"\0")
            out += struct.pack(">HBBHH", words, 0, vol, 0 if loop else 0, words if loop else 1)
        else:
            out += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    patterns = 4
    out += bytes([patterns, 127]) + bytes(range(patterns)) + bytes(128 - patterns) + b"M.K."
    for r in rows:
        for sample, per, fx, arg in r:
            out += bytes([(sample & 0xF0) | (per >> 8), per & 255, ((sample & 15) << 4) | fx, arg])
    for sname, data, vol, loop in SAMPLES:
        out += data[:len(data) // 2 * 2]
    return bytes(out)


def main():
    mods = [module(s) for s in SONGS]
    if "--mods" in sys.argv:
        d = sys.argv[sys.argv.index("--mods") + 1]
        os.makedirs(d, exist_ok=True)
        for s, m in zip(SONGS, mods):
            with open(os.path.join(d, s[0] + ".mod"), "wb") as f:
                f.write(m)
    lines = ["/* SPDX-License-Identifier: MIT */",
             "/* Generated by tools/amiga/gen_music.py: the desktop's six songs as ProTracker modules */",
             "#ifndef OFP_AMIGA_MUSIC_MODS_H", "#define OFP_AMIGA_MUSIC_MODS_H", "",
             "enum { MUSIC_CLASSIC, MUSIC_DESERT, MUSIC_JUNGLE, MUSIC_WINTER, MUSIC_SCIFI, MUSIC_MENU, MUSIC_SONGS };"]
    for s, m in zip(SONGS, mods):
        lines.append("static const unsigned char music_%s[%d] = {" % (s[0], len(m)))
        for i in range(0, len(m), 24):
            lines.append("    " + ", ".join("%d" % b for b in m[i:i + 24]) + ",")
        lines.append("};")
    lines.append("static const unsigned char *const music_mods[MUSIC_SONGS] = { %s };" %
                 ", ".join("music_" + s[0] for s in SONGS))
    lines.append("static const unsigned long music_sizes[MUSIC_SONGS] = { %s };" %
                 ", ".join("sizeof music_" + s[0] for s in SONGS))
    lines += ["#endif", ""]
    with open(os.path.join(ROOT, "src", "platform", "amiga", "music_mods.h"), "w") as f:
        f.write("\n".join(lines))
    print("music_mods.h: %d songs, %d bytes" % (len(mods), sum(len(m) for m in mods)))


if __name__ == "__main__":
    main()

# (the names and numbers for everyone; the data for the file that holds it: amiga_headers.py)
import sys as _s, os as _o
_s.path.insert(0, _o.path.dirname(_o.path.abspath(__file__)))
from amiga_headers import ids_only as _ids_only   # noqa: E402
_ids_only(_o.path.join(_o.path.dirname(_o.path.abspath(__file__)), "..", "..", "src", "platform", "amiga", "music_mods.h"))
