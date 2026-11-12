#!/usr/bin/env python3
"""Pack the native 16-colour Amiga artwork into planar graphics."""
from pathlib import Path
import json
import sys

from PIL import Image
from native_art import (battle_atlas, detail_original_structures, detail_original_walls, direction_atlas,
                        infantry, quiet_original_sand, structures_atlas, tank, terrain_atlas, turret, vehicle)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from amiga_palette import PALETTE, RGB, SHARED_PENS, TERRAIN_PENS, Nearest, tile_pens   # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "src" / "platform" / "amiga" / "classic_art.h"
# Structure atlas rows follow StructType, with a 64x48 slot for each state.
STRUCTURE_SIZES = (
    (1, 1), (2, 2), (2, 2), (3, 2), (3, 3), (4, 3), (3, 3),
    (2, 2), (3, 3), (2, 2), (3, 2), (3, 2), (1, 1), (1, 1),
    (2, 2), (2, 2), (2, 2), (3, 3), (1, 1), (2, 2),
)
CLASSIC_MAPS = ("dust-crossing", "perimeter-road", "switchback", "twin-boulevards")


def rgba_atlas(name, width, height, style):
    im = Image.open(style / name).convert("RGBA")
    if im.size != (width, height):
        raise ValueError(f"Unexpected dimensions for {name}")
    return im


def sprite_rows(atlas, stride, x, y, size, pens, masked=False, source_size=None):
    """pens: a Nearest (shared art) or, for terrain, None: the tile's own pens (tile_pens)."""
    source_size = source_size or size
    pad = (size - source_size) // 2
    pixels = {}
    for row in range(size):
        for col in range(size):
            if pad <= row < pad + source_size and pad <= col < pad + source_size:
                offset = ((y + row - pad) * stride + x + col - pad) * 4
                pixels[(row, col)] = tuple(atlas[offset:offset + 4])
    if pens is None:                                     # terrain: avoid the pen that changes team colour
        order = sorted(pixels)
        mapped = dict(zip(order, tile_pens([pixels[k] for k in order], RGB, TERRAIN_PENS)))
    rows = []
    for row in range(size):
        words = [0] * (5 if masked else 4)
        for col in range(size):
            px = pixels.get((row, col))
            if px is None or (masked and px[3] < 128):
                continue
            pen = mapped[(row, col)] if pens is None else pens(px)
            bit = 1 << (size - 1 - col)
            if masked:
                words[0] |= bit
            for plane in range(4):
                if pen & (1 << plane):
                    words[plane + int(masked)] |= bit
        rows.append(words)
    return rows


def c_values(values, width):
    return ", ".join(f"0x{value:0{width}x}" for value in values)


def add_sprite_bank(lines, name, atlas, source_rows, directions, palettes,
                    stride=1024, cell=32, frames=32):
    bounds = []
    lines.append(f"static const uint32_t classic_{name}[{len(source_rows)}][{directions}][32][5] = {{")
    for i, source_y in enumerate(source_rows):
        lines.append("    {")
        bounds.append([])
        for direction in range(directions):
            rows = sprite_rows(atlas, stride, direction * (frames // directions) * cell,
                               source_y * cell, 32, palettes[i % len(palettes)], True, cell)
            occupied = [(x, y) for y, row in enumerate(rows) for x in range(32)
                        if row[0] & (1 << (31 - x))]
            xs, ys = zip(*occupied)
            bounds[-1].append((min(xs), min(ys), max(xs) + 1, max(ys) + 1))
            lines.append("        {")
            lines.extend("            { " + c_values(row, 8) + " }," for row in rows)
            lines.append("        },")
        lines.append("    },")
    lines += ["};", "", "/* Opaque bounds, with exclusive right and bottom edges. */",
              f"static const uint8_t classic_{name}_bounds[{len(source_rows)}][{directions}][4] = {{"]
    for row in bounds:
        lines.append("    {")
        lines.extend("        { " + ", ".join(map(str, rect)) + " }," for rect in row)
        lines.append("    },")
    lines += ["};", ""]


def add_ground(lines, tiles):
    for kind in range(12):
        source_row = 4 if kind == 11 else kind
        for col in range(16):
            # Store each 16px tile as four byte-aligned 8px quadrants.
            for quadrant in range(4):
                x = col * 16 + (quadrant & 1) * 8
                y = source_row * 16 + (quadrant >> 1) * 8
                rows = sprite_rows(tiles, 256, x, y, 8, None)
                planes = ", ".join("{ " + c_values(plane, 2) + " }" for plane in zip(*rows))
                lines.append("    { " + planes + " },")
    lines += ["};", ""]

def add_structures(lines, structures, prefix):
    lines.append(f"static const uint8_t {prefix}_structures[20][2][48][5][8] = {{")
    for kind, (width, height) in enumerate(STRUCTURE_SIZES):
        lines.append("    {")
        for state in range(2):
            lines.append("        {")
            for y in range(height * 2):
                for x in range(width * 2):
                    rows = sprite_rows(structures, 192, state * 64 + x * 8,
                                       kind * 48 + y * 8, 8, SHARED, True)
                    planes = ", ".join("{ " + c_values(plane, 2) + " }" for plane in zip(*rows))
                    lines.append(f"            [{y * 8 + x}] = {{ {planes} }},")
            lines.append("        },")
        lines.append("    },")
    lines += ["};", ""]

SHARED = Nearest(RGB, SHARED_PENS)                 # sprites, buildings: every world's pens


def original_terrain():
    style = ROOT / "data/styles/retro"
    lines = ["/* Generated from the desktop Original artwork by tools/amiga/gen_classic_art.py. */",
             "static const uint8_t original_tiles[768][4][8] = {"]
    tiles = quiet_original_sand(rgba_atlas("tiles.png", 256, 176, style))
    detail_original_walls(tiles, RGB)
    add_ground(lines, tiles.tobytes())
    structures = detail_original_structures(rgba_atlas("structures.png", 192, 960, style), RGB)
    add_structures(lines, structures.tobytes(), "original")
    OUTPUT.with_name("original_terrain.h").write_text("\n".join(lines))


def main():
    tiles = terrain_atlas("classic").tobytes()
    tanks = direction_atlas([(tank(team), 32) for team in range(2)]).tobytes()
    lines = [
        "/* SPDX-License-Identifier: MIT */",
        "/* Generated by tools/amiga/gen_classic_art.py from native_art.py. */",
        "#ifndef OFP_AMIGA_CLASSIC_ART_H",
        "#define OFP_AMIGA_CLASSIC_ART_H",
        "",
        f"static const uint16_t classic_palette[16] = {{ {c_values(PALETTE, 3)} }};",
        "",
        "static const uint8_t classic_tiles[768][4][8] = {",
    ]
    add_ground(lines, tiles)
    # Include both steel shades so tread links and the barrel stay separate from the hull.
    tank_pens = (Nearest(RGB, (0, 3, 4, 5, 8, 10, 11, 12)),
                 Nearest(RGB, (0, 4, 5, 9, 10, 12, 13, 14)))
    add_sprite_bank(lines, "tanks", tanks, (0, 1), 32, tank_pens)
    rockets = direction_atlas([(tank(team, launcher=True), 16) for team in range(2)]).tobytes()
    lines.append("#ifdef OFP_AMIGA_ONLINE")
    add_sprite_bank(lines, "rocket_tanks", rockets, (0, 1), 16, tank_pens)
    lines += ["#endif", ""]
    vehicles = direction_atlas([(vehicle(kind, team), 8) for kind in range(6) for team in range(3)]).tobytes()
    add_sprite_bank(lines, "vehicles", vehicles, range(18), 8, (SHARED,))
    turrets = direction_atlas([(turret(kind), 8) for kind in range(6)]).tobytes()
    add_sprite_bank(lines, "turrets", turrets, range(6), 8, (SHARED,))
    men = infantry().tobytes()
    add_sprite_bank(lines, "men", men, range(3), 16, (SHARED,), stride=192, cell=12, frames=16)
    battle = battle_atlas(RGB).tobytes()
    add_sprite_bank(lines, "battle", battle, range(6), 4, (SHARED,), stride=128, frames=4)
    structures = structures_atlas(STRUCTURE_SIZES).tobytes()
    add_structures(lines, structures, "classic")
    lines += ["#endif", ""]
    OUTPUT.write_text("\n".join(lines))

    lines = ["/* Generated from data/maps/classic by tools/amiga/gen_classic_art.py. */"]
    for name in CLASSIC_MAPS:
        symbol = name.replace("-", "_")
        lines.append(f"static const char classic_{symbol}[] =")
        source = ROOT / "data" / "maps" / "classic" / f"{name}.ofpmap"
        lines.extend("    " + json.dumps(line) for line in source.read_text().splitlines(keepends=True))
        lines[-1] += ";"
    OUTPUT.with_name("classic_maps.h").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    import sys

    if sys.argv[1:] == ["--original-terrain"]:
        original_terrain()
    else:
        main()
        original_terrain()
