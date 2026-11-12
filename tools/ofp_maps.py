#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Turn the maps on YOUR OWN Fire Power disk into OpenFirePower maps.

    python3 tools/ofp_maps.py FirePower.adf

Writes data/maps/original/*.ofpmap: the levels' layouts only, which the project
ships (the graphics and sounds stay out of git, like the other imported material). In the game, choose
WORLD: ORIGINAL or REMASTER and then MAP on the game-type screen, or open them in the map
editor. (`ofp_adf.py theme` does this too, along with the graphics and sounds.)

How the conversion works:
  * An original map is 64 x 64 blocks of 64 x 64 pixels. Ours is 128 x 128
    tiles of 16 pixels, so each block becomes 2 x 2 tiles (half the
    original's size on screen).
  * Road shapes come from the original street blocks. Grey walls, pads
    and damaged structures are classified separately. Trees (blocks 2 and 4)
    become trees you hide under ("P"), as in the original.
  * Buildings and guns are mapped by block number to our structures: pillboxes
    become cannons, the big emplacement a heavy gun, the prison a POW camp,
    the bunkers, depots, huts, dish, first-aid pad and helipad their
    counterparts. All of them belong to the enemy.
  * The concrete barriers round the original's compounds (blocks 21, 41-43,
    47, 48, 50) become walls.
  * The player's garage stays at its original location. A second garage is
    placed well away from it so the remake can play versus without stamping
    two full bases over the original roads and buildings.
Identical maps on the disk (several levels share one) are written once.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ofp_adf as adfmod                                   # noqa: E402

MAP_W = MAP_H = 128

# block number -> (structure, width, height) placed at the block's top-left tile
STRUCTS = {
    9: ("cannon", 1, 1), 29: ("cannon", 1, 1), 31: ("cannon", 1, 1), 33: ("cannon", 1, 1),
    37: ("cannon", 1, 1), 38: ("cannon", 1, 1), 35: ("heavy", 2, 2),
    17: ("warehouse", 3, 2), 18: ("depot", 2, 2), 19: ("powcamp", 3, 3),
    23: ("barracks", 3, 2), 25: ("firstaid", 2, 2), 27: ("radar", 2, 2),
    14: ("helipad", 3, 3),
}
RUBBLE = {22, 28, 30, 39, 40, 44, 45, 46, 49}                   # blasted blocks: debris on the ground
RUINS = {24}                                                 # wrecked buildings
# the concrete barriers walling the compounds: these blocks only ever turn up
# in the closed rings round them (and 50, the corner pieces, has no picture)
WALLS = {21, 34, 36, 41, 42, 43, 47, 48, 50}
GARAGE = 20                                                  # the player's own garage: once per map


# Top-left, top-right, bottom-left, bottom-right, as in the desktop ADF importer.
# Only street blocks may supply asphalt. The grey areas around emplacements,
# on concrete walls and on landing pads are not part of the road network.
ROAD = {
    5: ".===", 6: "====", 7: "=.==", 8: "====", 10: "====", 11: "==.=",
    12: "====", 13: "===.", 15: "====", 16: "====",
}
FOLIAGE = {2: "PPPP", 3: ".T..", 4: "..P."}


def convert(grid, name):
    tiles = [["."] * MAP_W for _ in range(MAP_H)]
    structs = []
    for by, row in enumerate(grid):
        for bx, v in enumerate(row):
            i = v & 63
            cells = ("####" if i in WALLS else "::::" if i in RUBBLE else
                     "xxxx" if i in RUINS else ROAD.get(i, FOLIAGE.get(i, "....")))
            for k, ch in enumerate(cells):
                tiles[by * 2 + (k >> 1)][bx * 2 + (k & 1)] = ch
            if i in STRUCTS:
                structs.append((STRUCTS[i], bx * 2, by * 2))
    # Add the second garage where its 3 x 3 footprint and exit do the least damage.
    occupied = [[False] * MAP_W for _ in range(MAP_H)]
    for (_, w, h), sx, sy in structs:
        for y in range(sy, min(sy + max(h, 4), MAP_H)):
            for x in range(sx, min(sx + max(w, 4), MAP_W)):
                occupied[y][x] = True

    start = next(((x * 2, y * 2) for y, row in enumerate(grid)
                  for x, v in enumerate(row) if v & 63 == GARAGE), (3, 96))
    px, py = (max(1, min(MAP_W - 4, start[0])), max(1, min(MAP_H - 5, start[1])))

    def distance(x, y, bx, by):
        return (x - bx) ** 2 + (y - by) ** 2

    def garage_score(x, y):
        score = sum(100 * occupied[ty][tx] + 10 * (tiles[ty][tx] != ".")
                    for ty in range(y, y + 3) for tx in range(x, x + 3))
        score += 100 * occupied[y + 3][x + 1]
        if tiles[y + 3][x + 1] not in ".=":
            score += 10
        if tiles[y + 3][x + 1] != "=":
            score += 1
        return score

    ex, ey = min(((x, y) for y in range(8, MAP_H - 8) for x in range(8, MAP_W - 8)
                  if distance(x, y, px, py) >= 60 ** 2),
                 key=lambda pos: garage_score(*pos))
    lines = ["OFPMAP 1", f"name={name}", "modes=flag", "biome=classic", "ambient=off",
             "players=2", "bases=garages", f"base=player {px} {py}", f"base=enemy {ex} {ey}", "tiles"]
    lines += ["".join(r) for r in tiles]
    for (kind, _, _), x, y in structs:
        side = "player" if kind == "firstaid" and distance(x, y, px, py) < distance(x, y, ex, ey) else "enemy"
        lines.append(f"struct={kind} {side} {x} {y}")
    lines.append("end")
    return "\n".join(lines) + "\n"


def import_maps(adf, out, quiet=False):
    """convert every distinct map on the disk; returns how many were written"""
    files = adf.files()
    if "gr/tiles" not in files:
        raise SystemExit("this disk has no gr/tiles: not a Fire Power disk?")
    os.makedirs(out, exist_ok=True)
    seen, written = set(), 0
    for path in sorted(files):
        if not path.startswith("Map"):
            continue
        data = adf.read(files[path])
        if len(data) != adfmod.MAP_N * adfmod.MAP_N or data in seen:
            continue
        seen.add(data)
        grid = adfmod.read_map(data)
        base = path.replace("/", "_")
        title = "Original " + base.replace("Map", "").replace("_", " ")
        with open(os.path.join(out, base + ".ofpmap"), "w") as f:
            f.write(convert(grid, title))
        written += 1
        if not quiet:
            print(f"  {title}")
    return written


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("adf", help="your Fire Power disk image")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "maps", "original"))
    a = ap.parse_args()
    n = import_maps(adfmod.ADF(a.adf), a.out)
    print(f"{n} maps written to {os.path.abspath(a.out)}; pick WORLD: ORIGINAL or REMASTER, then MAP, on the game-type screen")


if __name__ == "__main__":
    main()
