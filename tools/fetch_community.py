#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the 'community' sound set from freely licensed OpenGameArt sounds.

    python3 tools/fetch_community.py              (downloads, needs internet)
    python3 tools/fetch_community.py --from DIR   (use files you downloaded)

The sounds are not used as they are: each is remixed for this game. The tool
cuts the useful part, resamples, layers it with our own synthesised sound,
shapes the envelope and makes loops seamless. The results go to
data/sounds/community/ together with CREDITS.txt, which names each original,
its author and licence, and what was changed, as CC-BY requires.

OGG files need ffmpeg to decode (macOS: brew install ffmpeg). WAV files don't.
"""
import argparse
import io
import math
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import wave
import zipfile

RATE = 22050
UA = {"User-Agent": "OpenFirePower asset fetcher (+https://github.com/)"}

# what we use, where it comes from, under which licence
ASSETS = [
    dict(key="engine", page="https://opengameart.org/content/engine-loop-heavy-vehicletank",
         file="engine_heavy_loop.wav", title="Engine-loop heavy vehicle/tank",
         author="Nayckron (from 'Car Engine Loop' by qubodup)", license="CC-BY 3.0 / GPL 3.0",
         changes="cut to a 1 s section, made seamless with a crossfade, low-passed, layered with our "
                 "synthesised cylinder thumps, resampled to 22 kHz mono"),
    dict(key="cannon", page="https://opengameart.org/content/cannon-fire",
         file="cannon_fire.ogg", title="Cannon fire", author="Thimras", license="CC0",
         changes="shortened to the shot and its first echo, layered with our synthesised low boom, "
                 "soft-clipped, resampled to 22 kHz mono"),
    dict(key="hit", page="https://opengameart.org/content/cannon-hit-wall",
         file="cannon_hit_wall.ogg", title="Cannon hit wall", author="Thimras", license="CC0",
         changes="only the first impact kept, high-passed for a metallic clank, short fade"),
    dict(key="splash", page="https://opengameart.org/content/battle-at-sea",
         file="cannon_miss.ogg", title="Battle at sea: cannon miss", author="Thimras", license="CC0",
         changes="the splash only, shortened and faded"),
    dict(key="bangs", page="https://opengameart.org/content/25-cc0-bang-firework-sfx",
         file="25-CC0-bang-sfx.zip", title="25 CC0 bang / firework SFX", author="rubberduck",
         license="CC0",
         changes="two bangs picked for big and small explosions, layered with our synthesised rumble "
                 "and debris crackle, re-enveloped"),
]


# ---- fetching ------------------------------------------------------------------------

def http(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return r.read()


def fetch(asset, cache):
    """the file, from --from, the cache, or the asset's page"""
    path = os.path.join(cache, asset["file"])
    if os.path.exists(path):
        return path
    try:
        page = http(asset["page"]).decode("utf-8", "replace")
        links = re.findall(r'href="([^"]*/sites/default/files/[^"]*%s)"' %
                           re.escape(asset["file"]).replace(r"\.", r"(?:_\d+)?\."), page)
        url = links[0] if links else "https://opengameart.org/sites/default/files/" + asset["file"]
        if url.startswith("/"):
            url = "https://opengameart.org" + url
        print(f"  downloading {url}")
        data = http(url)
    except Exception as e:                                   # noqa: BLE001
        print(f"  could not download {asset['file']}: {e}")
        return None
    with open(path, "wb") as f:
        f.write(data)
    return path


def decode(path):
    """-> list of floats, mono, RATE"""
    if path.lower().endswith(".wav"):
        try:
            return read_wav(path)
        except Exception:                                    # noqa: BLE001 (odd WAVs: try ffmpeg)
            pass
    if not shutil.which("ffmpeg"):
        print(f"  need ffmpeg to read {os.path.basename(path)} (macOS: brew install ffmpeg)")
        return None
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "x.wav")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-ac", "1", "-ar", str(RATE),
                        "-sample_fmt", "s16", out], check=True)
        return read_wav(out)


def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw == 1:
        vals = [(b - 128) / 128 for b in raw]
    elif sw == 2:
        vals = [v / 32768 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)]
    elif sw == 3:
        vals = [int.from_bytes(raw[i:i + 3], "little", signed=True) / 8388608 for i in range(0, len(raw), 3)]
    else:
        vals = [v / 2147483648 for v in struct.unpack("<%di" % (len(raw) // 4), raw)]
    mono = [sum(vals[i:i + ch]) / ch for i in range(0, len(vals) - ch + 1, ch)]
    return resample(mono, rate)


def resample(x, rate):
    if rate == RATE:
        return x
    step, out, pos = rate / RATE, [], 0.0
    while pos < len(x) - 1:
        i = int(pos)
        f = pos - i
        out.append(x[i] * (1 - f) + x[i + 1] * f)
        pos += step
    return out


def write_wav(path, x):
    peak = max(1e-6, max(abs(v) for v in x))
    k = 0.89 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v * k)) * 32000)) for v in x))


# ---- making them ours ------------------------------------------------------------------

def lowpass(x, k):
    y, out = 0.0, []
    for v in x:
        y += k * (v - y)
        out.append(y)
    return out


def highpass(x, k):
    return [a - b for a, b in zip(x, lowpass(x, k))]


def trim_start(x, thresh=0.04):
    peak = max(1e-6, max(abs(v) for v in x))
    for i, v in enumerate(x):
        if abs(v) > thresh * peak:
            return x[max(0, i - 40):]
    return x


def fade(x, seconds, power=2.0):
    n = len(x)
    return [v * (1 - i / n) ** power for i, v in enumerate(x[:int(seconds * RATE)])][:int(seconds * RATE)]


def mix(a, b, kb):
    n = max(len(a), len(b))
    a = a + [0.0] * (n - len(a))
    b = b + [0.0] * (n - len(b))
    return [p + q * kb for p, q in zip(a, b)]


def soft_clip(x, drive=1.6):
    return [math.tanh(v * drive) for v in x]


def boom(seconds, f0=95, f1=38):
    n = int(seconds * RATE)
    out, ph = [], 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * i / n
        ph += 2 * math.pi * f / RATE
        out.append(math.sin(ph) * (1 - i / n) ** 2.5)
    return out


def rumble(seconds, seed=1):
    rng = random.Random(seed)
    n = int(seconds * RATE)
    x = lowpass(lowpass([rng.uniform(-1, 1) for _ in range(n)], 0.04), 0.06)
    return [v * 6 * (1 - i / n) ** 1.5 for i, v in enumerate(x)]


def thumps(n, rate_hz=24):
    period = RATE / rate_hz
    return [max(0.0, math.sin(math.pi * (i % period) / period)) ** 8 for i in range(n)]


def make_engine(x):
    x = trim_start(x)
    mid = len(x) // 2
    seg = x[max(0, mid - RATE // 2): mid + RATE // 2 + RATE // 10]
    if len(seg) < RATE:
        seg = x[:RATE + RATE // 10]
    body, tail = seg[:RATE], seg[RATE:]
    cf = len(tail)
    for i in range(cf):                                   # crossfade the end into the start
        t = i / max(1, cf)
        body[i] = body[i] * t + tail[i] * (1 - t)
    body = lowpass(body, 0.35)
    th = lowpass(thumps(len(body)), 0.2)
    return mix(body, th, 0.18 * max(abs(v) for v in body))


def make_cannon(x):
    x = trim_start(x)[:int(0.9 * RATE)]
    x = fade(x, 0.9, 1.6)
    peak = max(abs(v) for v in x) or 1
    return soft_clip(mix([v / peak for v in x], boom(0.5), 0.7))


def make_hit(x):
    x = trim_start(x)[:int(0.35 * RATE)]
    return fade(highpass(x, 0.08), 0.35, 2.5)


def make_splash(x):
    x = trim_start(x)
    peak_i = max(range(len(x)), key=lambda i: abs(x[i]))
    x = x[max(0, peak_i - RATE // 20):]
    return fade(x, 0.8, 1.8)


def make_explosions(bangs):
    bangs = sorted(bangs, key=len)
    big, small = bangs[-1], bangs[len(bangs) // 3]
    big = fade(trim_start(big), 1.4, 1.2)
    pb = max(abs(v) for v in big) or 1
    big = soft_clip(mix([v / pb for v in big], rumble(1.4, 3), 0.8), 1.3)
    small = fade(trim_start(small), 0.6, 1.8)
    ps = max(abs(v) for v in small) or 1
    small = soft_clip(mix([v / ps for v in small], rumble(0.6, 4), 0.5), 1.3)
    return big, small


def unzip_sounds(path):
    out = []
    with zipfile.ZipFile(path) as z, tempfile.TemporaryDirectory() as tmp:
        for name in z.namelist():
            if name.lower().endswith((".wav", ".ogg")) and not name.startswith("__MACOSX"):
                p = os.path.join(tmp, os.path.basename(name))
                with open(p, "wb") as f:
                    f.write(z.read(name))
                x = decode(p)
                if x:
                    out.append(x)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="src", help="folder with the original files already downloaded")
    ap.add_argument("--data", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data"))
    a = ap.parse_args()
    out = os.path.join(a.data, "sounds", "community")
    cache = a.src or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "rip", "community")
    os.makedirs(out, exist_ok=True)
    os.makedirs(cache, exist_ok=True)

    made, credits = [], []
    for asset in ASSETS:
        print(f"{asset['title']} ({asset['license']}, {asset['author']})")
        path = fetch(asset, cache)
        if not path:
            continue
        try:
            if asset["key"] == "bangs":
                bangs = unzip_sounds(path)
                if not bangs:
                    continue
                big, small = make_explosions(bangs)
                write_wav(os.path.join(out, "explode.wav"), big)
                write_wav(os.path.join(out, "explode_small.wav"), small)
                made += ["explode", "explode_small"]
            else:
                x = decode(path)
                if not x:
                    continue
                y = {"engine": make_engine, "cannon": make_cannon, "hit": make_hit,
                     "splash": make_splash}[asset["key"]](x)
                write_wav(os.path.join(out, asset["key"] + ".wav"), y)
                made.append(asset["key"])
        except Exception as e:                               # noqa: BLE001
            print(f"  skipped: {e}")
            continue
        credits.append(asset)

    with open(os.path.join(out, "sounds.txt"), "w") as f:
        f.write("name=Community (OpenGameArt, remixed)\nlicense=see CREDITS.txt\n")
    with open(os.path.join(out, "CREDITS.txt"), "w") as f:
        f.write("Sounds in this folder are remixes of freely licensed sounds from OpenGameArt.org.\n"
                "Anything not listed here comes from OpenFirePower's own synthesised set.\n\n")
        for c in credits:
            f.write(f"{c['title']}\n  by {c['author']}\n  {c['page']}\n  licence: {c['license']}\n"
                    f"  changes: {c['changes']}\n\n")
    print(f"\nsound set 'community' in {os.path.abspath(out)}: {', '.join(made) or 'nothing'}")


if __name__ == "__main__":
    main()
