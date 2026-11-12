#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Import graphics from YOUR OWN copy of the original game into an
OpenFirePower theme. Nothing produced by this tool may be committed to the
repository; data/themes/classic/ is git-ignored. Needs Python 3.11+ and Pillow.

  ofp_import.py ilbm   FILE.iff OUT.png
        Decode an IFF ILBM picture (Amiga DPaint format; ByteRun1, EHB).

  ofp_import.py planar FILE OUT.png --width W --depth D [--height H]
                [--offset N] [--interleaved] [--palette SPEC]
        Decode raw Amiga bitplanes. Leave --height out to decode the rest of
        the file as one tall strip, which is how you hunt for graphics in an
        unknown data file: try widths 16/32/320 and depths 3-5 and look.
        SPEC: an .iff/.png to take colours from, or Amiga 12-bit values
        like 0x000,0xfff,0xf00 (default: grey ramp).

  ofp_import.py build  MAPPING.toml
        Cut tiles out of ripped PNGs into a map style (data/styles/...), and
        tank frames, selection art and icons into a UI theme (data/themes/...)
        (see docs/classic-mapping.example.toml).
"""
import argparse
import os
import struct
import sys

from PIL import Image

TILES = ["sand", "road", "water", "rock", "tree", "stump",
         "wall", "rubble", "building", "ruins", "floor"]
CLASSES = ["light", "medium", "heavy"]
ICONS = ["pow", "tank", "mine", "fuel", "armor", "left", "right", "speed"]
TILE, TANK_CELL, CARD_W, CARD_H, ICON = 16, 32, 176, 104, 8


# ---- Amiga picture formats ------------------------------------------------------

def amiga12(v):
    """0x0RGB Amiga colour register -> (r, g, b)"""
    return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17)


def unpack_byterun1(data, pos, size):
    out = bytearray()
    while len(out) < size and pos < len(data):
        n = data[pos] - 256 if data[pos] > 127 else data[pos]
        pos += 1
        if n >= 0:
            out += data[pos:pos + n + 1]
            pos += n + 1
        elif n != -128:
            out += bytes([data[pos]]) * (1 - n)
            pos += 1
    return bytes(out[:size]), pos


def read_ilbm(path):
    """returns (PIL RGB image, palette list)"""
    data = open(path, "rb").read()
    if data[:4] != b"FORM" or data[8:12] not in (b"ILBM", b"PBM "):
        raise SystemExit(f"{path}: not an IFF ILBM/PBM file")
    kind = data[8:12]
    pos, end = 12, 8 + struct.unpack(">I", data[4:8])[0]
    bmhd = cmap = body = None
    camg = 0
    while pos + 8 <= min(end, len(data)):
        cid, size = data[pos:pos + 4], struct.unpack(">I", data[pos + 4:pos + 8])[0]
        chunk = data[pos + 8:pos + 8 + size]
        if cid == b"BMHD":
            bmhd = struct.unpack(">HHhhBBBBHBBhh", chunk[:20])
        elif cid == b"CMAP":
            cmap = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk) - 2, 3)]
        elif cid == b"CAMG":
            camg = struct.unpack(">I", chunk[:4])[0]
        elif cid == b"BODY":
            body = chunk
        pos += 8 + size + (size & 1)
    if not bmhd or body is None:
        raise SystemExit(f"{path}: missing BMHD or BODY")
    w, h, _, _, planes, masking, compression = bmhd[:7]
    if camg & 0x800:
        raise SystemExit(f"{path}: HAM pictures are not supported")
    pal = list(cmap or [(i * 255 // max(1, (1 << planes) - 1),) * 3 for i in range(1 << planes)])
    if camg & 0x80 and len(pal) < 64:                     # Extra Half-Brite
        pal = pal[:32] + [(r // 2, g // 2, b // 2) for r, g, b in pal[:32]]
    pal += [(0, 0, 0)] * (256 - len(pal))

    img = Image.new("RGB", (w, h))
    px = img.load()
    if kind == b"PBM ":                                   # chunky, one byte per pixel
        row_bytes = w + (w & 1)
        raw = unpack_byterun1(body, 0, row_bytes * h)[0] if compression == 1 else body
        for y in range(h):
            for x in range(w):
                px[x, y] = pal[raw[y * row_bytes + x]]
        return img, pal

    row_bytes = ((w + 15) // 16) * 2
    nplanes = planes + (1 if masking == 1 else 0)
    if compression == 1:
        raw, _ = unpack_byterun1(body, 0, row_bytes * nplanes * h)
    else:
        raw = body
    for y in range(h):
        base = y * row_bytes * nplanes
        for x in range(w):
            byte, bit = x >> 3, 7 - (x & 7)
            idx = 0
            for p in range(planes):
                if raw[base + p * row_bytes + byte] >> bit & 1:
                    idx |= 1 << p
            px[x, y] = pal[idx]
    return img, pal


def decode_planar(data, width, height, depth, interleaved, palette):
    row_bytes = ((width + 15) // 16) * 2
    plane_size = row_bytes * height
    img = Image.new("RGB", (width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            byte, bit = x >> 3, 7 - (x & 7)
            idx = 0
            for p in range(depth):
                off = (y * row_bytes * depth + p * row_bytes + byte) if interleaved \
                    else (p * plane_size + y * row_bytes + byte)
                if off < len(data) and data[off] >> bit & 1:
                    idx |= 1 << p
            px[x, y] = palette[idx % len(palette)]
    return img


def parse_palette(spec, depth):
    n = 1 << depth
    if not spec:
        return [(i * 255 // (n - 1),) * 3 for i in range(n)]
    if os.path.isfile(spec):
        if spec.lower().endswith((".iff", ".ilbm", ".lbm")):
            return read_ilbm(spec)[1]
        im = Image.open(spec).convert("P") if Image.open(spec).mode != "P" else Image.open(spec)
        raw = im.getpalette()
        return [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    return [amiga12(int(v, 16)) for v in spec.split(",")]


def cmd_ilbm(a):
    img, _ = read_ilbm(a.file)
    img.save(a.out)
    print(f"{a.out}: {img.size[0]}x{img.size[1]}")


def cmd_planar(a):
    data = open(a.file, "rb").read()[a.offset:]
    row_bytes = ((a.width + 15) // 16) * 2
    height = a.height or max(1, len(data) // (row_bytes * a.depth))
    img = decode_planar(data, a.width, height, a.depth, a.interleaved, parse_palette(a.palette, a.depth))
    if a.zoom > 1:
        img = img.resize((img.size[0] * a.zoom, img.size[1] * a.zoom), Image.NEAREST)
    img.save(a.out)
    print(f"{a.out}: {a.width}x{height}, {a.depth} planes, "
          f"{'interleaved' if a.interleaved else 'plane after plane'}")


# ---- theme builder ------------------------------------------------------------------

class Sources:
    def __init__(self, cfg, base):
        self.files = {k: os.path.join(base, v) for k, v in cfg.get("sources", {}).items()}
        self.keys = {k: tuple(v) for k, v in cfg.get("transparent", {}).items()}
        self.cache = {}

    def get(self, key):
        if key not in self.cache:
            if key not in self.files:
                raise SystemExit(f"mapping refers to unknown source '{key}'")
            im = Image.open(self.files[key]).convert("RGBA")
            if key in self.keys:                          # colour key -> transparent
                kr, kg, kb = self.keys[key][:3]
                px = im.load()
                for y in range(im.size[1]):
                    for x in range(im.size[0]):
                        if px[x, y][:3] == (kr, kg, kb):
                            px[x, y] = (0, 0, 0, 0)
            self.cache[key] = im
        return self.cache[key]

    def cut(self, spec):
        key, x, y, w, h = spec[:5]
        return self.get(key).crop((x, y, x + w, y + h))


def fit(img, w, h, stretch):
    """resize a cut-out into a theme cell: stretch (tiles, cards) or centre (sprites)"""
    if stretch:
        return img.resize((w, h), Image.NEAREST)
    if img.size[0] > w or img.size[1] > h:
        f = min(w / img.size[0], h / img.size[1])
        img = img.resize((max(1, int(img.size[0] * f)), max(1, int(img.size[1] * f))), Image.NEAREST)
    cell = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cell.paste(img, ((w - img.size[0]) // 2, (h - img.size[1]) // 2))
    return cell


def strip(src, spec, cell_w, cell_h, stretch, scale=1.0):
    """[source, x, y, w, h, count, step_x?, step_y?] -> list of cells"""
    key, x, y, w, h = spec[:5]
    count = spec[5] if len(spec) > 5 else 1
    sx = spec[6] if len(spec) > 6 else w
    sy = spec[7] if len(spec) > 7 else 0
    cells = []
    for i in range(count):
        im = src.cut([key, x + i * sx, y + i * sy, w, h])
        if scale != 1.0:
            im = im.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.NEAREST)
        cells.append(fit(im, cell_w, cell_h, stretch))
    return cells


def cmd_build(a):
    import tomllib
    with open(a.mapping, "rb") as f:
        cfg = tomllib.load(f)
    base = os.path.dirname(os.path.abspath(a.mapping))
    out = os.path.join(base, cfg.get("out", "classic"))
    style_out = os.path.join(base, cfg.get("style_out", os.path.join(os.path.dirname(out), "..", "styles", "imported")))
    src = Sources(cfg, base)
    os.makedirs(out, exist_ok=True)
    wrote = []

    if "tiles" in cfg:
        sheet = Image.new("RGBA", (TILE * 16, TILE * len(TILES)))
        default = os.path.join(os.path.dirname(__file__), "..", "data", "styles",
                               cfg.get("biome", "classic"), "tiles.png")   # unmapped rows
        if os.path.exists(default):                         # unmapped rows keep the default art
            sheet.paste(Image.open(default).convert("RGBA"))
        for row, name in enumerate(TILES):
            spec = cfg["tiles"].get(name)
            if not spec:
                continue
            cells = strip(src, spec, TILE, TILE, True)
            for col in range(16):                          # 1 cell -> all columns; 16 -> per mask
                sheet.paste(cells[col % len(cells)], (col * TILE, row * TILE))
        os.makedirs(style_out, exist_ok=True)
        sheet.save(os.path.join(style_out, "tiles.png"))
        with open(os.path.join(style_out, "style.txt"), "w") as f:
            f.write(f"name={cfg.get('name', 'Imported')}\nbiome={cfg.get('biome', 'classic')}\nimported=1\n")
        wrote.append(f"map style {style_out}")

    if "structures" in cfg:                           # multi-tile buildings, into the style
        footprints = {"tower": (1, 1), "bunker": (2, 2), "depot": (2, 2), "barracks": (3, 2),
                      "dome": (3, 3), "hq": (4, 3), "garage": (3, 3), "firstaid": (2, 2),
                      "powcamp": (3, 3), "radar": (2, 2), "power": (3, 2), "warehouse": (3, 2),
                      "mg": (1, 1), "cannon": (1, 1), "rocket": (2, 2), "heavy": (2, 2), "flag": (2, 2),
                      "helipad": (3, 3), "twin": (1, 1), "mortar": (2, 2)}
        base = os.path.join(os.path.dirname(__file__), "..", "data", "styles", cfg.get("biome", "classic"),
                            "structures.png")
        sheet = Image.open(base).convert("RGBA") if os.path.exists(base) else Image.new("RGBA", (192, 960))
        for row, (name, (fw, fh)) in enumerate(footprints.items()):
            for col, key in enumerate((name, name + "_damaged")):
                spec = cfg["structures"].get(key) or (cfg["structures"].get(name) if col else None)
                if spec:
                    sheet.paste(Image.new("RGBA", (64, 48)), (col * 64, row * 48))
                    sheet.paste(fit(src.cut(spec), fw * 16, fh * 16, False), (col * 64, row * 48))
        os.makedirs(style_out, exist_ok=True)
        sheet.save(os.path.join(style_out, "structures.png"))
        wrote.append("structures.png")

    if "tanks" in cfg:
        sheet = Image.new("RGBA", (TANK_CELL * 32, TANK_CELL * 6), (0, 0, 0, 0))
        for ci, cls in enumerate(CLASSES):
            for team in range(2):
                spec = cfg["tanks"].get(f"{cls}_{team}") or cfg["tanks"].get(f"{cls}_0") \
                       or cfg["tanks"].get("all")
                if not spec:
                    continue
                frames = strip(src, spec, TANK_CELL, TANK_CELL, False, cfg.get("tank_scale", 1.0))
                n = len(frames)
                for f in range(32):                        # spread n directions over 32
                    sheet.paste(frames[round(f * n / 32) % n], (f * TANK_CELL, (ci * 2 + team) * TANK_CELL))
        sheet.save(os.path.join(out, "tanks.png"))
        wrote.append("tanks.png")

    if "cards" in cfg:
        sheet = Image.new("RGBA", (CARD_W * 3, CARD_H), (0, 0, 0, 255))
        for ci, cls in enumerate(CLASSES):
            if cls in cfg["cards"]:
                sheet.paste(fit(src.cut(cfg["cards"][cls]), CARD_W, CARD_H, True), (ci * CARD_W, 0))
        sheet.save(os.path.join(out, "cards.png"))
        wrote.append("cards.png")

    if "icons" in cfg:
        sheet = Image.new("RGBA", (ICON * len(ICONS), ICON), (0, 0, 0, 0))
        for i, name in enumerate(ICONS):
            if name in cfg["icons"]:
                sheet.paste(fit(src.cut(cfg["icons"][name]), ICON, ICON, True), (i * ICON, 0))
        sheet.save(os.path.join(out, "ui.png"))
        wrote.append("ui.png")

    if "backdrop" in cfg:                                 # any size, tiled behind the HUD
        src.cut(cfg["backdrop"]).save(os.path.join(out, "backdrop.png"))
        wrote.append("backdrop.png")

    with open(os.path.join(out, "theme.txt"), "w") as f:
        f.write(f"name={cfg.get('name', 'Classic')}\nimported=1\n")
        for k, v in cfg.get("colors", {}).items():       # HUD colours, rrggbb
            f.write(f"{k}={v.lstrip('#')}\n")
    print(f"wrote {', '.join(wrote) or 'nothing'} to {out}")
    print("missing files fall back to the default theme; press F4 in game to switch")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("ilbm")
    p.add_argument("file"); p.add_argument("out")
    p.set_defaults(fn=cmd_ilbm)
    p = sub.add_parser("planar")
    p.add_argument("file"); p.add_argument("out")
    p.add_argument("--width", type=int, required=True)
    p.add_argument("--height", type=int, default=0)
    p.add_argument("--depth", type=int, default=5)
    p.add_argument("--offset", type=lambda v: int(v, 0), default=0)
    p.add_argument("--interleaved", action="store_true")
    p.add_argument("--palette", default="")
    p.add_argument("--zoom", type=int, default=1)
    p.set_defaults(fn=cmd_planar)
    p = sub.add_parser("build")
    p.add_argument("mapping")
    p.set_defaults(fn=cmd_build)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
