#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Add groves of trees overhead (canopy, `P` in the tiles: drive under them and you're hidden, as in
the original) to map files: on open sand only, away from the bases, the buildings, the spawn points
and the capture points. Deterministic (seeded by the map's name); a map that has groves already is
left as it is.

    tools/add_groves.py data/maps/*.ofpmap
"""
import random
import sys

PER_WORLD = {"classic": 7, "desert": 4, "jungle": 10, "winter": 6, "scifi": 3, "volcanic": 2,
             "wasteland": 3, "moon": 2, "swamp": 9}
BASE_W, BASE_H = 36, 28                     # a base's ground, tiles (a little more than it covers)


def groves(path):
    text = open(path).read()
    lines = text.split("\n")
    if "tiles" not in lines:
        return 0
    t0 = lines.index("tiles") + 1
    head = lines[:t0 - 1]
    rows = []
    i = t0
    while i < len(lines) and lines[i] and "=" not in lines[i][:1] and not lines[i].startswith("struct="):
        rows.append(list(lines[i]))
        i += 1
    rest = lines[i:]
    if any("P" in r for r in rows):
        return 0                            # (has some already)
    h, w = len(rows), max(len(r) for r in rows) if rows else 0
    meta = {k: v for k, v in (l.split("=", 1) for l in head if "=" in l)}
    world, arena = meta.get("biome", "classic"), meta.get("arena") == "yes"
    keep = []                               # (x0, y0, x1, y1): where nothing may grow
    for l in head:
        if l.startswith("base="):
            _, x, y = l[5:].split()[:3]
            x, y = int(x), int(y)
            keep.append((x - 4, y - 4, x + BASE_W + 4, y + BASE_H + 4))
        if l.startswith("spawns=") or l.startswith("points="):
            nums = [int(v) for v in l.split("=", 1)[1].replace(",", " ").split()]
            for k in range(0, len(nums) - 1, 2):
                keep.append((nums[k] - 4, nums[k + 1] - 4, nums[k] + 5, nums[k + 1] + 5))
    for l in rest:
        if l.startswith("struct="):
            p = l[7:].split()
            x, y = int(p[-2]), int(p[-1])
            keep.append((x - 3, y - 3, x + 7, y + 7))

    def free(x, y):
        if x < 3 or y < 3 or x >= w - 3 or y >= h - 3 or x >= len(rows[y]) or rows[y][x] != ".":
            return False
        return not any(a <= x <= c and b <= y <= d for a, b, c, d in keep)

    rnd = random.Random(meta.get("name", path))
    want = max(1, PER_WORLD.get(world, 5) * (w * h) // (128 * 128) // (2 if arena else 1))
    made = tries = 0
    while made < want and tries < 400:
        tries += 1
        cx, cy = rnd.randrange(6, max(7, w - 6)), rnd.randrange(6, max(7, h - 6))
        if not free(cx, cy):
            continue
        cells = 0
        for _ in range(rnd.randint(2, 4)):  # a few round patches, merged: a grove
            bx, by, rad = cx + rnd.randint(-2, 2), cy + rnd.randint(-2, 2), rnd.randint(1, 2)
            for dy in range(-rad, rad + 1):
                for dx in range(-rad, rad + 1):
                    if dx * dx + dy * dy <= rad * rad + 1 and free(bx + dx, by + dy):
                        rows[by + dy][bx + dx] = "P"
                        cells += 1
        made += cells > 0
    out = head + ["tiles"] + ["".join(r) for r in rows] + rest
    open(path, "w").write("\n".join(out))
    return made


if __name__ == "__main__":
    total = 0
    for p in sys.argv[1:]:
        n = groves(p)
        total += n
        print(f"{p}: {n} groves")
    print(f"{total} groves in all")
