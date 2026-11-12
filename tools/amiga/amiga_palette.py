# SPDX-License-Identifier: MIT
"""The Amiga client's 16-colour screen, shared by the art generators (gen_classic_art.py, gen_world_art.py,
gen_faces.py), and the conversion they use.

    pen 0          INK, the backdrop and outlines        pens 8..15  the sides, white, gold, the HUD's
    pens 1, 2      the world's ground: sand and dark earth in the classic world, each world its own
                   (world_art.h's world_ground_pens; the 68000 build has the classic world only)
    pens 3..7      brown, greys, blood red (6), blue

Pens 1 and 2 change with the world, so sprites and buildings (shared by every world) never use them.
Colours are compared in CIELAB, as eyes see them, not as RGB numbers.
"""
import numpy as np

PALETTE = (0x112, 0xFC8, 0x531, 0x974, 0x555, 0x999, 0xC22, 0x58B,
           0xE95, 0x7B5, 0xFFE, 0xFD5, 0x333, 0xBE7, 0x374, 0x224)
WORLD_PENS = (1, 2)
FORTRESS_PEN = 9
BLOOD_PEN = 6
TERRAIN_PENS = tuple(p for p in range(16) if p not in (FORTRESS_PEN, BLOOD_PEN))
SHARED_PENS = tuple(p for p in range(16) if p not in (*WORLD_PENS, BLOOD_PEN))


def rgb12(c):
    return tuple((c >> s & 15) * 17 for s in (8, 4, 0))


RGB = [rgb12(c) for c in PALETTE]


def lab(a):
    a = np.asarray(a, dtype=np.float64) / 255.0
    a = np.where(a > 0.04045, ((a + 0.055) / 1.055) ** 2.4, a / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = a @ m.T / np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


class Nearest:
    """rgb -> the nearest of pens (in a palette of rgb tuples), remembered."""
    def __init__(self, palette, pens):
        self.pens = list(pens)
        self.lab = lab(np.array([palette[p] for p in self.pens]))
        self.cache = {}

    def __call__(self, rgb):
        rgb = tuple(rgb[:3])
        if rgb not in self.cache:
            d = ((self.lab - lab(np.array(rgb))) ** 2).sum(-1)
            self.cache[rgb] = self.pens[int(d.argmin())]
        return self.cache[rgb]


def tile_pens(pixels, palette, pens, apart=10.0, slack=2.2):
    """An 8x8 tile's colours (a list of rgb, row by row) to pens: each the nearest, except that two clearly
    different colours (more than `apart` apart) don't end up as one pen while another is nearly as close:
    a tile's lines and grain survive (the planks' edges, the bricks')."""
    pens = list(pens)
    pal = lab(np.array([palette[p] for p in pens]))
    colours, counts = [], {}
    for c in pixels:
        c = tuple(c[:3])
        if c not in counts:
            colours.append(c)
        counts[c] = counts.get(c, 0) + 1
    L = {c: lab(np.array(c)) for c in colours}
    taken, assign = {}, {}
    for c in sorted(colours, key=lambda c: -counts[c]):
        d = ((pal - L[c]) ** 2).sum(-1)
        best, choice = d.min(), None
        for k in np.argsort(d):
            if d[k] > best * slack * slack + 4:
                break
            owner = taken.get(int(k))
            if owner is None or np.sqrt(((L[owner] - L[c]) ** 2).sum()) < apart:
                choice = int(k)
                break
        if choice is None:
            choice = int(d.argmin())
        taken.setdefault(choice, c)
        assign[c] = pens[choice]
    return [assign[tuple(c[:3])] for c in pixels]


def pick(samples, palette, fixed_pens, count, rounds=30):
    """count new 12-bit colours for samples {rgb: weight}, beside the fixed pens (a weighted k-means in CIELAB;
    each new colour is a real colour of the art, rounded to the Amiga's 12 bits)."""
    cols = np.array(list(samples.keys()), dtype=np.float64)
    w = np.array(list(samples.values()), dtype=np.float64)
    X, F = lab(cols), lab(np.array([palette[p] for p in fixed_pens]))
    centres = []
    for _ in range(count):
        pool = np.concatenate([F] + ([np.array(centres)] if centres else []))
        d = ((X[:, None] - pool[None]) ** 2).sum(-1).min(1)
        centres.append(X[int(np.argmax(d * w))])
    centres = np.array(centres)
    for _ in range(rounds):
        pool = np.concatenate([F, centres])
        near = ((X[:, None] - pool[None]) ** 2).sum(-1).argmin(1)
        for j in range(count):
            mine = near == len(F) + j
            if mine.any():
                centres[j] = (X[mine] * w[mine, None]).sum(0) / w[mine].sum()
    out = []
    for c in centres:
        r, g, b = cols[int(((X - c) ** 2).sum(-1).argmin())]
        out.append(int(round(r / 17)) << 8 | int(round(g / 17)) << 4 | int(round(b / 17)))
    return out
