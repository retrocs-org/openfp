#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the website's catalog of maps bundled with the public game."""
from base64 import b64encode
from json import dump
from pathlib import Path
from struct import pack
from zlib import compress, crc32

ROOT = Path(__file__).resolve().parents[1]
MAPS = ROOT / "data/maps"
GROUND = {
    "classic": "f0c889", "moon": "9a9aa2", "winter": "e8eef2", "desert": "d8b878",
    "jungle": "728848", "swamp": "697752", "volcanic": "3a3230", "wasteland": "8a7448",
}
COLORS = {
    "=": "646460", "~": "2f68b8", "I": "b5d9e8", "o": "22232b", "O": "302f37", "^": "5a5048", "T": "2c5a1e", "P": "2c5a1e",
    "#": "9a8f80", ":": "857a6c", "x": "4a4038",
}
SIZES = {"small": 64, "medium": 96, "large": 128, "huge": 192}
SIDE = {"player": "e9aa4b", "enemy": "30be8b"}
SIDE_LEN = 96


MODE_IDS = ["classic", "flag", "conquest", "hardpoint", "deathmatch", "killconfirmed", "annihilation", "warlord"]


def map_modes(fields, world):
    if fields.get("mode") in MODE_IDS:
        return [fields["mode"]]
    if world == "classic":
        return ["classic"]
    raw = fields.get("modes", "").strip()
    tokens = set(raw.replace(",", " ").split()) if raw else (
        {"annihilation"} if fields.get("arena") == "yes" else {"flag", "conquest", "deathmatch", "warlord"})
    if tokens & {"conquest", "hardpoint"}:
        tokens.update(("conquest", "hardpoint"))
    if tokens & {"deathmatch", "killconfirmed"}:
        tokens.update(("deathmatch", "killconfirmed"))
    return [mode for mode in MODE_IDS if mode in tokens]


def png(pixels):
    def chunk(kind, data):
        return pack(">I", len(data)) + kind + data + pack(">I", crc32(kind + data))

    rows = b"".join(b"\0" + b"".join(bytes.fromhex(pixel) for pixel in row) for row in pixels)
    header = pack(">IIBBBBB", SIDE_LEN, SIDE_LEN, 8, 2, 0, 0, 0)
    return b64encode(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"IDAT", compress(rows, 9)) + chunk(b"IEND", b"")).decode("ascii")


def entry(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    tiles = lines.index("tiles")
    headers = [line.split("=", 1) for line in lines[1:tiles] if "=" in line]
    fields = dict(headers)
    world = fields.get("biome", "classic")
    if world == "scifi":
        return None
    size = SIZES[fields.get("size", "large")]
    rows = lines[tiles + 1:tiles + 1 + size]
    background = GROUND[world]
    colors = dict(COLORS)
    if world == "moon":
        colors["~"] = COLORS["O"]  # legacy lunar water tiles become dry crater trenches
    pixels = []
    for sy in range(SIDE_LEN):
        y = sy * size // SIDE_LEN
        row = rows[y] if y < len(rows) else ""
        pixels.append([colors.get(row[x], background) if x < len(row) else background
                       for x in (sx * size // SIDE_LEN for sx in range(SIDE_LEN))])

    for key, value in headers:
        if key != "base":
            continue
        team, x, y = value.split()[:3]
        x, y = int(x) * SIDE_LEN // size, int(y) * SIDE_LEN // size
        for py in range(max(0, y - 2), min(SIDE_LEN, y + 3)):
            for px in range(max(0, x - 2), min(SIDE_LEN, x + 3)):
                pixels[py][px] = SIDE[team] if abs(px - x) < 2 and abs(py - y) < 2 else "161710"

    return {
        "name": fields["name"],
        "world": world,
        "modes": map_modes(fields, world),
        "time": fields.get("time", "day"),
        "size": size,
        "preview": png(pixels),
    }


def main():
    # The Classic subfolder is bundled. The Original subfolder holds private disk imports.
    maps = [entry(path) for path in sorted([*MAPS.glob("*.ofpmap"), *(MAPS / "classic").glob("*.ofpmap")])]
    maps = sorted((m for m in maps if m is not None), key=lambda m: (m["world"], m["name"].casefold()))
    with (ROOT / "public-website/assets/bundled-maps.json").open("w", encoding="utf-8") as out:
        dump(maps, out, ensure_ascii=False, separators=(",", ":"))
        out.write("\n")


if __name__ == "__main__":
    main()
