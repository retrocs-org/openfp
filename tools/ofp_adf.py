#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extract graphics, maps and sounds from YOUR OWN Fire Power (Amiga, 1987)
disk image, and build an OpenFirePower "classic" theme from them.
Needs Python 3.8+ and Pillow. Nothing it writes may be committed: the
results are copyrighted by the original authors (data/themes/classic/ is
git-ignored).

  ofp_adf.py ls      FirePower.adf            list files on the disk
  ofp_adf.py unpack  FirePower.adf DIR        copy every file out, as is
  ofp_adf.py extract FirePower.adf DIR        decode everything to PNG/WAV/CSV
  ofp_adf.py theme   FirePower.adf [--remix]  build the "original" map style, its maps and
                                              the "classic" UI theme; --remix also
                                              makes original-desert/-jungle/-winter

Formats handled (worked out from the 1987 release):
  * AmigaDOS OFS/FFS disk images (.adf, 880 KB)
  * gr/* brush files: a chain of records, each a 16-byte big-endian header
      width, height, depth, planes_stored, mask_plane, last_plane,
      plane_pick, plane_on_off
    followed by planes_stored bitplanes of ((width+15)//16)*2 bytes x height,
    one plane after the other. Colour planes go to the hardware planes set
    in plane_pick; planes not picked are filled from plane_on_off (that is
    how a 1-plane sand tile or a 4-plane tank lands in the right colours).
    mask_plane (if >= 0) is the transparency mask.
  * gr/* full screens: 352x232, 5 planes, followed by a 32-entry 12-bit palette
  * Map*/t*: 64x64 bytes, column-major; low 6 bits = 64x64 px block from
    gr/tiles, bit 6 = a per-block flag (kept in the CSV)
  * sounds/*: IFF 8SVX or raw signed 8-bit samples
"""
import argparse
import json
import random
import os
import struct
import sys
import wave

try:
    from PIL import Image
except ModuleNotFoundError as error:
    if error.name != "PIL":
        raise
    Image = None

BS = 512


# ---- AmigaDOS disk image ---------------------------------------------------------

class ADF:
    T_HEADER, ST_ROOT, ST_DIR, ST_FILE = 2, 1, 2, -3

    def __init__(self, path):
        self.data = open(path, "rb").read()
        if len(self.data) not in (901120, 1802240):
            print(f"warning: {path}: unusual size {len(self.data)}, trying anyway", file=sys.stderr)
        if self.data[:3] != b"DOS":
            raise SystemExit(f"{path}: not an AmigaDOS disk (custom track loader?). "
                             "Rip the graphics from the running game instead, see docs/THEMES.md")
        self.ffs = self.data[3] & 1
        self.root = len(self.data) // BS // 2

    def block(self, n):
        if not 0 < n < len(self.data) // BS:
            raise ValueError(f"block {n} out of range")
        return self.data[n * BS:(n + 1) * BS]

    @staticmethod
    def u32(b, i):
        return struct.unpack_from(">I", b, i * 4)[0]

    @staticmethod
    def s32(b, i):
        return struct.unpack_from(">i", b, i * 4)[0]

    @staticmethod
    def name(b):
        n = min(b[BS - 80], 30)
        return b[BS - 79:BS - 79 + n].decode("latin-1")

    def _entries(self, dir_block):
        b = self.block(dir_block)
        seen = set()
        for i in range(6, 6 + 72):                     # hash table
            h = self.u32(b, i)
            while h and h not in seen:
                seen.add(h)
                e = self.block(h)
                yield h, e
                h = self.u32(e, BS // 4 - 4)           # hash chain

    def walk(self, block=None, prefix=""):
        """yields (path, header_block, is_dir, size), sorted per directory"""
        items = []
        for h, e in self._entries(block or self.root):
            items.append((self.name(e), h, self.s32(e, BS // 4 - 1), self.u32(e, BS // 4 - 47)))
        for name, h, sec, size in sorted(items, key=lambda t: t[0].lower()):
            if sec == self.ST_DIR:
                yield prefix + name + "/", h, True, 0
                yield from self.walk(h, prefix + name + "/")
            else:
                yield prefix + name, h, False, size

    def read(self, header):
        e = self.block(header)
        size = self.u32(e, BS // 4 - 47)
        out = bytearray()
        if self.ffs:
            b = e
            while True:
                for i in range(BS // 4 - 51, 5, -1):   # data block table, reversed
                    p = self.u32(b, i)
                    if p:
                        out += self.block(p)
                ext = self.u32(b, BS // 4 - 2)
                if not ext:
                    break
                b = self.block(ext)
        else:
            p, guard = self.u32(e, 4), 0
            while p and guard < 2000:
                db = self.block(p)
                n = self.u32(db, 3)
                out += db[24:24 + n]
                p = self.u32(db, 4)
                guard += 1
        return bytes(out[:size])

    def files(self):
        return {path: h for path, h, is_dir, _ in self.walk() if not is_dir}


# ---- graphics -------------------------------------------------------------------------

def amiga12(v):
    return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17)


SCREEN_W, SCREEN_H, SCREEN_D = 352, 232, 5
SCREEN_SIZE = (SCREEN_W // 8) * SCREEN_H * SCREEN_D + 64


def is_screen(data):
    return len(data) == SCREEN_SIZE


def screen_palette(data):
    return [amiga12(v) for v in struct.unpack(">32H", data[-64:])]


def decode_screen(data):
    pal = screen_palette(data)
    rb, ph = SCREEN_W // 8, (SCREEN_W // 8) * SCREEN_H
    img = Image.new("RGB", (SCREEN_W, SCREEN_H))
    px = img.load()
    for y in range(SCREEN_H):
        for x in range(SCREEN_W):
            byte, bit, idx = y * rb + (x >> 3), 7 - (x & 7), 0
            for p in range(SCREEN_D):
                if data[p * ph + byte] >> bit & 1:
                    idx |= 1 << p
            px[x, y] = pal[idx]
    return img


def brush_records(data):
    """list of (header tuple, plane data), or None if the file isn't a brush chain"""
    recs, pos = [], 0
    while pos + 16 <= len(data):
        h = struct.unpack_from(">8h", data, pos)
        w, hgt, depth, planes = h[:4]
        if not (0 < w <= 1024 and 0 < hgt <= 1024 and 0 < planes <= 8 and 0 <= depth <= planes):
            return None
        size = ((w + 15) // 16) * 2 * hgt * planes
        if pos + 16 + size > len(data):
            return None
        recs.append((h, data[pos + 16:pos + 16 + size]))
        pos += 16 + size
    return recs if recs and pos == len(data) else None


def decode_brush(h, data):
    """-> 2D list of palette indices, -1 = transparent, -2 = shadow (mask-only brush)"""
    w, hgt, depth, planes, mask, _last, pick, onoff = h
    rb = ((w + 15) // 16) * 2
    hw_planes = [b for b in range(8) if pick >> b & 1]
    base = onoff & ~pick & 0xFF

    def bit(p, x, y):
        return data[p * rb * hgt + y * rb + (x >> 3)] >> (7 - (x & 7)) & 1

    rows = []
    for y in range(hgt):
        row = []
        for x in range(w):
            if depth == 0:                              # shadow silhouette
                row.append(-2 if bit(0, x, y) else -1)
                continue
            if 0 <= mask < planes and not bit(mask, x, y):
                row.append(-1)
                continue
            idx = base
            for i in range(min(depth, len(hw_planes))):
                if bit(i, x, y):
                    idx |= 1 << hw_planes[i]
            row.append(idx)
        rows.append(row)
    return rows


def render(indices, pal, shadow=(0, 0, 0, 110)):
    if Image is None:
        raise SystemExit("Pillow is required to extract Fire Power graphics.")
    hgt, w = len(indices), len(indices[0])
    img = Image.new("RGBA", (w, hgt), (0, 0, 0, 0))
    px = img.load()
    for y, row in enumerate(indices):
        for x, i in enumerate(row):
            if i >= 0:
                px[x, y] = pal[i % len(pal)] + (255,)
            elif i == -2:
                px[x, y] = shadow
    return img


def sheet(images, gap=1):
    """frames side by side; returns (image, list of [x, y, w, h])"""
    w = sum(im.size[0] for im in images) + gap * (len(images) - 1)
    h = max(im.size[1] for im in images)
    out, rects, x = Image.new("RGBA", (w, h), (0, 0, 0, 0)), [], 0
    for im in images:
        out.paste(im, (x, 0))
        rects.append([x, 0, im.size[0], im.size[1]])
        x += im.size[0] + gap
    return out, rects


def write_gpl(path, name, pal):
    with open(path, "w") as f:
        f.write(f"GIMP Palette\nName: {name}\nColumns: 8\n#\n")
        for i, (r, g, b) in enumerate(pal):
            f.write(f"{r:3d} {g:3d} {b:3d}\tcolour {i} (Amiga ${r // 17:x}{g // 17:x}{b // 17:x})\n")


# ---- sounds -------------------------------------------------------------------------

def sound_to_wav(data, path, default_rate=8363):
    rate, samples = default_rate, None
    if data[:4] == b"FORM" and data[8:12] == b"8SVX":
        pos = 12
        while pos + 8 <= len(data):
            cid, size = data[pos:pos + 4], struct.unpack_from(">I", data, pos + 4)[0]
            if cid == b"VHDR" and size >= 14:
                rate = struct.unpack_from(">H", data, pos + 8 + 12)[0] or default_rate
            elif cid == b"BODY":
                samples = data[pos + 8:pos + 8 + size]
            pos += 8 + size + (size & 1)
    if samples is None:                                 # raw signed 8-bit
        samples = data
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(bytes((b + 128) & 255 for b in samples))   # WAV 8-bit is unsigned
    return rate


# ---- maps -------------------------------------------------------------------------------

MAP_N, BLOCK = 64, 64


def read_map(data):
    """[y][x] of raw bytes; the files are stored column by column"""
    return [[data[x * MAP_N + y] for x in range(MAP_N)] for y in range(MAP_N)]


def render_map(grid, blocks, scale=4):
    size = BLOCK // scale
    small = [b.resize((size, size), Image.BOX) for b in blocks]
    out = Image.new("RGBA", (MAP_N * size, MAP_N * size), (255, 0, 255, 255))
    for y, row in enumerate(grid):
        for x, v in enumerate(row):
            if (v & 63) < len(small):
                out.paste(small[v & 63], (x * size, y * size))
    return out


# ---- commands ---------------------------------------------------------------------------

def game_palette(adf, files):
    for name in ("gr/oneplayer", "gr/loadscreen"):
        if name in files:
            d = adf.read(files[name])
            if is_screen(d):
                return screen_palette(d), name
    print("warning: no in-game palette found, using a grey ramp", file=sys.stderr)
    return [(i * 8, i * 8, i * 8) for i in range(32)], None


def cmd_ls(a):
    adf = ADF(a.adf)
    print(f"{'FFS' if adf.ffs else 'OFS'} disk")
    for path, _, is_dir, size in adf.walk():
        print(f"{'':>8}  {path}" if is_dir else f"{size:8d}  {path}")


def cmd_unpack(a):
    adf = ADF(a.adf)
    n = 0
    for path, h in adf.files().items():
        dst = os.path.join(a.out, *path.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(adf.read(h))
        n += 1
    print(f"unpacked {n} files to {a.out}")


def extract_all(adf, out, quiet=False):
    files = adf.files()
    pal, pal_src = game_palette(adf, files)
    manifest = {"palette_source": pal_src, "graphics": {}, "screens": {}, "maps": {}, "sounds": {}}
    for d in ("gfx", "screens", "maps", "sounds", "text", "raw"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    write_gpl(os.path.join(out, "palette.gpl"), "Fire Power in-game", pal)

    blocks = None
    for path in sorted(files):
        data = adf.read(files[path])
        base = path.replace("/", "_")
        if path.startswith("gr/") and is_screen(data):
            decode_screen(data).save(os.path.join(out, "screens", base + ".png"))
            write_gpl(os.path.join(out, "screens", base + ".gpl"), path, screen_palette(data))
            manifest["screens"][path] = {"size": [SCREEN_W, SCREEN_H]}
            continue
        recs = brush_records(data) if path.startswith("gr/") else None
        if recs:
            frames = [render(decode_brush(h, d), pal) for h, d in recs]
            img, rects = sheet(frames)
            img.save(os.path.join(out, "gfx", base + ".png"))
            manifest["graphics"][path] = {
                "frames": [{"rect": r, "header": list(h)} for r, (h, _) in zip(rects, recs)]}
            if path == "gr/tiles":
                blocks = frames
                grid = Image.new("RGBA", (BLOCK * 10, BLOCK * ((len(frames) + 9) // 10)), (0, 0, 0, 0))
                for i, b in enumerate(frames):
                    grid.paste(b, ((i % 10) * BLOCK, (i // 10) * BLOCK))
                grid.save(os.path.join(out, "gfx", "gr_tiles_grid.png"))
            continue
        if path.startswith("sounds/"):
            rate = sound_to_wav(data, os.path.join(out, "sounds", base + ".wav"))
            manifest["sounds"][path] = {"rate": rate}
            continue
        if path.startswith("Map") and len(data) == MAP_N * MAP_N:
            grid_ = read_map(data)
            with open(os.path.join(out, "maps", base + ".csv"), "w") as f:
                f.write("\n".join(",".join(str(v) for v in row) for row in grid_) + "\n")
            manifest["maps"][path] = {"size": [MAP_N, MAP_N], "block_px": BLOCK}
            continue
        if path.startswith("Ins/") or path.startswith("s/"):
            with open(os.path.join(out, "text", base + ".txt"), "w", encoding="latin-1") as f:
                f.write(data.decode("latin-1"))
            continue
        with open(os.path.join(out, "raw", base), "wb") as f:
            f.write(data)

    if blocks:                                       # map previews need the blocks
        for path in manifest["maps"]:
            grid_ = read_map(adf.read(files[path]))
            render_map(grid_, blocks).save(os.path.join(out, "maps", path.replace("/", "_") + ".png"))
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    if not quiet:
        print(f"{len(manifest['graphics'])} sprite/tile files, {len(manifest['screens'])} screens, "
              f"{len(manifest['maps'])} maps, {len(manifest['sounds'])} sounds -> {out}")
    return files, pal, manifest


def cmd_extract(a):
    extract_all(ADF(a.adf), a.out)


# ---- classic theme ------------------------------------------------------------------------
# OpenFirePower draws 16x16 tiles; the original draws 64x64 blocks. We cut
# matching 16x16 pieces out of the blocks (see docs/THEMES.md for the layout).

TILE_ROWS = ["sand", "road", "water", "rock", "tree", "stump",
             "wall", "rubble", "building", "ruins", "floor"]
N, E, S, W = 1, 2, 4, 8


def crop(img, x, y, w=16, h=16):
    return img.crop((x, y, x + w, y + h))


def on_sand(piece, sand):
    out = sand.copy()
    out.alpha_composite(piece)
    return out


# the game's buildings, from the 64x64 blocks, shrunk onto our footprints
# (structures.png layout: 64x48 slot per type, column 0 intact, 1 damaged)
STRUCT_SOURCES = [("tower", 29, 1, 1), ("bunker", 20, 2, 2), ("depot", 18, 2, 2),
                  ("barracks", 23, 3, 2), ("dome", 27, 3, 3), ("hq", 35, 4, 3)]


def nearest(pal, rgb):
    return min(pal, key=lambda p: (p[0] - rgb[0]) ** 2 + (p[1] - rgb[1]) ** 2 + (p[2] - rgb[2]) ** 2)


def original_structures(blocks, pal, biome="classic"):
    """the original's buildings where it has one; the rest (garage, turrets,
    POW camp, ...) from the matching built-in style"""
    sand = {pal[1], pal[3]}
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "styles", biome, "structures.png")
    sheet = Image.open(base).convert("RGBA") if os.path.exists(base) else Image.new("RGBA", (192, 960))
    for row, (_name, block, tw, th) in enumerate(STRUCT_SOURCES):
        src = blocks[block].copy()
        px = src.load()
        for y in range(64):                       # sand becomes transparent
            for x in range(64):
                if px[x, y][:3] in sand:
                    px[x, y] = (0, 0, 0, 0)
        box = src.getbbox() or (0, 0, 64, 64)
        obj = src.crop(box)
        w, h = tw * 16, th * 16
        f = min(w / obj.size[0], h / obj.size[1], 1.0)
        obj = obj.resize((max(1, round(obj.size[0] * f)), max(1, round(obj.size[1] * f))), Image.BOX)
        op = obj.load()
        for y in range(obj.size[1]):              # back onto the game's palette, hard edges
            for x in range(obj.size[0]):
                r, g, b, a = op[x, y]
                op[x, y] = nearest(pal, (r, g, b)) + (255,) if a >= 128 else (0, 0, 0, 0)
        cell = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        cell.paste(obj, ((w - obj.size[0]) // 2, (h - obj.size[1]) // 2))
        sheet.paste(Image.new("RGBA", (192, 48)), (0, row * 48))
        sheet.paste(cell, (0, row * 48))
        broken = cell.copy()                      # damaged: scorched dither
        bp = broken.load()
        for y in range(h):
            for x in range(w):
                if bp[x, y][3] and (x * 7 + y * 13) % 5 == 0:
                    bp[x, y] = (24, 18, 12, 255)
        sheet.paste(broken, (64, row * 48))
        try:                                              # what's left of it afterwards
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            import gen_styles
            sheet.paste(gen_styles.remains(cell, random.Random(row * 31 + 5),
                                           gen_styles.STYLES[biome]["st"], gen_styles.STYLES[biome]["ruins"]),
                        (128, row * 48))
        except Exception as e:                            # keep the built-in remains then
            print("note: remains not made from the original buildings:", e)
    return sheet


def autotile_window(mask, lo, hi):
    """pick a 16x16 window inside an object spanning lo..hi (inclusive) on
    both axes, so open sides of the mask land on the object's border"""
    def axis(open_lo, open_hi):
        if open_lo and open_hi:
            return None
        if open_lo:
            return lo
        if open_hi:
            return hi - 15
        return (lo + hi) // 2 - 8
    x = axis(not mask & W, not mask & E)
    y = axis(not mask & N, not mask & S)
    return x, y


# Re-colourings of the original 32-colour palette for the --remix styles.
# Index -> Amiga 12-bit colour; indices not listed keep their colour.
REMIX = {
    "desert": ("desert", {1: 0xe95, 3: 0xc63, 11: 0xbb6, 12: 0x884, 13: 0x542, 19: 0xbb6,
                          7: 0x975, 15: 0x975, 17: 0x975, 6: 0xb97, 14: 0xb97, 16: 0xb97,
                          28: 0xdb6, 29: 0xa84, 30: 0x863, 31: 0x531}),
    "jungle": ("jungle", {1: 0x372, 3: 0x251, 11: 0x6b2, 12: 0x281, 13: 0x041, 19: 0x6b2,
                          7: 0x642, 15: 0x642, 17: 0x642, 6: 0x863, 14: 0x863, 16: 0x863,
                          28: 0x683, 29: 0x562, 30: 0x441, 31: 0x231}),
    "winter": ("winter", {1: 0xeef, 3: 0xbcd, 11: 0xdfe, 12: 0x264, 13: 0x032, 19: 0xdfe,
                          7: 0x667, 15: 0x667, 17: 0x667, 6: 0xaab, 14: 0xaab, 16: 0xaab,
                          2: 0x89a, 9: 0x89a, 20: 0x9ab, 21: 0x789, 22: 0x567, 23: 0x345,
                          24: 0x9ab, 25: 0x789, 26: 0x567,
                          28: 0xddd, 29: 0xaaa, 30: 0x777, 31: 0x444}),
}


def remap(pal, table):
    out = list(pal)
    for i, v in table.items():
        out[i] = amiga12(v)
    return out


def build_style(adf, files, pal, out, title, biome):
    """map style from the original: tiles, tanks and backdrop"""
    blocks = [render(decode_brush(h, d), pal) for h, d in brush_records(adf.read(files["gr/tiles"]))]
    sand_block = Image.new("RGBA", (64, 64), pal[1] + (255,))
    sand_block.alpha_composite(blocks[0])
    blocks = [on_sand(b, sand_block) for b in blocks]
    sand_cells = [crop(sand_block, (i % 4) * 16, (i // 4) * 16) for i in range(16)]
    os.makedirs(out, exist_ok=True)

    tiles = Image.new("RGBA", (16 * 16, 16 * len(TILE_ROWS)), (0, 0, 0, 0))
    default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "styles", biome, "tiles.png")
    if os.path.exists(default):                  # rows the original lacks (water, rock)
        tiles.paste(Image.open(default).convert("RGBA"))

    def put(row, col, img):
        tiles.paste(img.resize((16, 16), Image.NEAREST) if img.size != (16, 16) else img,
                    (col * 16, TILE_ROWS.index(row) * 16))

    asphalt, light, black = pal[7], pal[6], pal[0]
    road_src = blocks[15]                        # straight east-west road, rows 15..48
    for m in range(16):
        put("sand", m, sand_cells[m])
        # road: original asphalt with the original edge colours on open sides
        cell = crop(road_src, 8 + (m * 5) % 32, 24).copy()
        px = cell.load()
        for i in range(16):
            if not m & N: px[i, 0], px[i, 1] = light + (255,), black + (255,)
            if not m & S: px[i, 14], px[i, 15] = light + (255,), black + (255,)
            if not m & W: px[0, i], px[1, i] = black + (255,), light + (255,)
            if not m & E: px[14, i], px[15, i] = black + (255,), light + (255,)
        put("road", m, cell)
        # bushes: windows into the big bush so clumps keep its outline
        bx = autotile_window(m, 12, 59)[0]
        by = autotile_window(m, 9, 53)[1]
        if bx is None or by is None:
            small = crop(blocks[3], 34, 3, 28, 24)          # lone small bush, shrunk
            put("tree", m, small)
        else:
            put("tree", m, crop(blocks[2], bx, by))
        # buildings: windows into the brown bunker block
        bx = autotile_window(m, 8, 54)[0]
        by = autotile_window(m, 8, 54)[1]
        put("building", m, crop(blocks[17], 8, 8, 47, 47) if bx is None or by is None
            else crop(blocks[17], bx, by))
        put("wall", m, crop(blocks[29], 17, 18, 28, 28))      # pillbox blocks
        put("rubble", m, crop(blocks[22], 4 + (m % 4) * 12, 4 + (m // 4) * 12))
        put("ruins", m, crop(blocks[43 + (m & 1)], 4 + (m % 4) * 10, 8 + (m // 4) * 10))
        put("stump", m, crop(blocks[30], 23, 25))
    tiles.save(os.path.join(out, "tiles.png"))
    original_structures(blocks, pal, biome).save(os.path.join(out, "structures.png"))

    # tanks: 16 directions from the original spread over our 32
    CELL = 32
    tanks = Image.new("RGBA", (CELL * 32, CELL * 6), (0, 0, 0, 0))
    for team, name in enumerate(["gr/tank0", "gr/Scorp"]):
        frames = [render(decode_brush(h, d), pal) for h, d in brush_records(adf.read(files[name]))]
        n = len(frames)
        for cls in range(3):
            for f in range(32):
                fr = frames[round(f * n / 32) % n]
                tanks.paste(fr, (f * CELL + (CELL - fr.size[0]) // 2,
                                 (cls * 2 + team) * CELL + (CELL - fr.size[1]) // 2))
    tanks.save(os.path.join(out, "tanks.png"))
    if "gr/loadscreen" in files:                   # camouflage backdrop, recoloured too
        ld = adf.read(files["gr/loadscreen"])
        orig = screen_palette(ld)
        lut = {orig[i]: pal[i] for i in range(32)}
        img = decode_screen(ld)
        px = img.load()
        for y in range(img.size[1]):
            for x in range(img.size[0]):
                px[x, y] = lut.get(px[x, y], px[x, y])
        img.convert("RGBA").save(os.path.join(out, "backdrop.png"))
    with open(os.path.join(out, "style.txt"), "w") as f:
        f.write(f"name={title}\nbiome={biome}\nimported=1\n")
        if title == "Original (disk)":                 # the disk's own looks: as faithful as it gets
            f.write("faithful=1\n")
    print(f"map style '{title}' written to {out}")


def build_ui_theme(adf, files, pal, out):
    """selection pictures, tank names and HUD colours from the original"""
    os.makedirs(out, exist_ok=True)
    # selection art: the three tank pictures from gr/pick, without their stats panel
    cards = Image.new("RGBA", (176 * 3, 104), (0, 0, 0, 255))
    picks = [render(decode_brush(h, d), pal) for h, d in brush_records(adf.read(files["gr/pick"]))]
    order = {0: 0, 2: 1, 1: 2}                    # pick order: light, heavy, medium
    for src_i, cls in order.items():
        if src_i < len(picks):
            art = picks[src_i].crop((2, 36, 156, 140))
            cards.paste(art, (cls * 176 + (176 - art.size[0]) // 2, 0))
    cards.save(os.path.join(out, "cards.png"))

    if "gr/loadscreen" in files:
        decode_screen(adf.read(files["gr/loadscreen"])).convert("RGBA").save(os.path.join(out, "backdrop.png"))

    def hx(i):
        return "%02x%02x%02x" % pal[i]
    with open(os.path.join(out, "theme.txt"), "w") as f:
        f.write("name=Classic (imported from your Fire Power disk)\nimported=1\n")
        f.write(f"panel={hx(7)}\nframe={hx(28)}\nframe_dark={hx(30)}\nrivet={hx(28)}\n"
                f"label={hx(4)}\nvalue={hx(5)}\nbar_fuel={hx(6)}\nbar_armor={hx(12)}\n"
                f"team0={hx(28)}\nteam1={hx(12)}\nbackdrop=000000\n")
        f.write("tank_light=MARC XJ1\ntank_medium=SCORPION\ntank_heavy=SHADOW 6\n")
    print(f"UI theme 'classic' written to {out}  (press F4 in game to switch)")


def build_theme(adf, remix):
    files = adf.files()
    need = ["gr/tiles", "gr/tank0", "gr/Scorp", "gr/pick"]
    missing = [n for n in need if n not in files]
    if missing:
        raise SystemExit(f"this disk lacks {', '.join(missing)}; use 'extract' and "
                         "tools/ofp_import.py build with your own mapping instead")
    data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
    pal, _ = game_palette(adf, files)
    build_style(adf, files, pal, os.path.join(data, "styles", "original"), "Original (disk)", "classic")   # the faithful world, in the disk's own looks
    if remix:
        for name, (biome, table) in REMIX.items():
            build_style(adf, files, remap(pal, table), os.path.join(data, "styles", "original-" + name),
                        "Orig. " + name.capitalize(), biome)
    build_ui_theme(adf, files, pal, os.path.join(data, "themes", "classic"))
    build_sounds(adf, files, os.path.join(data, "sounds", "original"))
    import ofp_maps                                    # and the original's levels, as map files
    n = ofp_maps.import_maps(adf, os.path.join(data, "maps", "original"), quiet=True)
    print(f"{n} original maps written to {os.path.join(data, 'maps', 'original')}  (WORLD: ORIGINAL or REMASTER, then MAP)")
    print("pick the terrain with Up/Down on the selection screen")


# the disk's own effects, under the names the game asks for (see tools/gen_sounds.py)
SOUND_MAP = {"sounds/boom2": "explode", "sounds/TankBlast": "cannon", "sounds/Heli": "heli",
             "sounds/Splat": "splat", "sounds/drip": "hit"}


def build_sounds(adf, files, out):
    os.makedirs(out, exist_ok=True)
    got = []
    for src, name in SOUND_MAP.items():
        if src in files:
            sound_to_wav(adf.read(files[src]), os.path.join(out, name + ".wav"))
            got.append(name)
    if "explode" in got:                                   # a shorter bang for small hits
        import shutil
        shutil.copy(os.path.join(out, "explode.wav"), os.path.join(out, "explode_small.wav"))
    with open(os.path.join(out, "sounds.txt"), "w") as f:
        f.write("name=Original (your disk)\nimported=1\n")
    print(f"reference sounds written to {out}  ({', '.join(got)})")


def cmd_theme(a):
    build_theme(ADF(a.adf), a.remix)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("ls"); p.add_argument("adf"); p.set_defaults(fn=cmd_ls)
    p = sub.add_parser("unpack"); p.add_argument("adf"); p.add_argument("out"); p.set_defaults(fn=cmd_unpack)
    p = sub.add_parser("extract"); p.add_argument("adf"); p.add_argument("out"); p.set_defaults(fn=cmd_extract)
    p = sub.add_parser("theme"); p.add_argument("adf"); p.add_argument("--remix", action="store_true")
    p.set_defaults(fn=cmd_theme)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
