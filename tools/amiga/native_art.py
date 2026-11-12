# SPDX-License-Identifier: MIT
"""Amiga battlefield art. Draw in native pixels, with no desktop atlas as input."""
import math
from random import Random

from PIL import Image, ImageDraw

# Soil (shade, body, light), road, water (deep, lit), foliage (shade, light).
WORLD_COLOURS = {
    "classic": ("#9d895f", "#bfad82", "#d6c49c", "#4b575a", "#203f50", "#4e8090", "#274d3d", "#70935b"),
    "desert": ("#b48153", "#d5a671", "#ecd09b", "#5a514c", "#294856", "#53868d", "#4a5336", "#a3a35f"),
    "jungle": ("#4c6549", "#6f8358", "#8a9d66", "#3f4d4a", "#1a4550", "#428587", "#1b4d35", "#699552"),
    "winter": ("#a2b8ca", "#cadce6", "#e6eff0", "#515d71", "#263f61", "#5d90ab", "#234b4b", "#77a39f"),
    "scifi": ("#35476a", "#4c6082", "#7089a8", "#36394e", "#16394e", "#32a3af", "#205963", "#6abfbc"),
    "volcanic": ("#493338", "#66474a", "#8c5c54", "#393740", "#6d302e", "#ed8341", "#54382b", "#ab7651"),
    "wasteland": ("#746344", "#9a875d", "#b8a374", "#4e4b45", "#384d4e", "#728474", "#424933", "#89905c"),
    "moon": ("#697186", "#929baa", "#b2bbca", "#485364", "#263549", "#536c89", "#3a5463", "#849ca6"),
    "swamp": ("#435943", "#65734d", "#85915c", "#414b46", "#2c4641", "#688571", "#234c39", "#6b9351"),
}

INK = "#141b26"
DEEP = "#303c4b"
METAL = "#566473"
STEEL = "#8595a3"
LIGHT = "#c1cdd0"
WHITE = "#f0e9d4"
BRASS = "#bd8657"
GOLD = "#efc26e"
RED = "#b94f50"
GLASS = "#4b8d99"
TEAM_COLOURS = (
    ("#885522", "#dd8833", "#ffbb66"),
    ("#225522", "#448844", "#77bb66"),
)


def colour(value):
    return tuple(bytes.fromhex(value.lstrip("#")))


def shade(rgb, amount):
    return tuple(max(0, min(255, c + amount)) for c in rgb)


def material_detail(atlas, cell_width, cell_height, terrain=False, protected=()):
    """Add tonal steps for the eight-bitplane edition, keeping hard pixel edges."""
    result = atlas.copy()
    src, dst = atlas.load(), result.load()
    protected = set(protected)
    for y in range(atlas.height):
        for x in range(atlas.width):
            r, g, b, alpha = src[x, y]
            if not alpha or (r, g, b) in protected:
                continue
            u, v = x % cell_width, y % cell_height
            grain = ((x * 31 ^ y * 57 ^ x * y * 13) & 7) - 3
            if terrain:
                ridge = (3 * u + v + (x // cell_width) * 7) & 15
                light = (5 - abs(ridge - 6)) if y < cell_height else (3 - abs(ridge - 7)) // 2
            else:
                light = 8 - (u * 7 // cell_width + v * 14 // cell_height)
                if max(r, g, b) < 52:
                    light //= 2
            dst[x, y] = (*shade((r, g, b), (light + grain) // 2 * 2), alpha)
    return result

def mix(a, b):
    return tuple((x + y) // 2 for x, y in zip(a, b))


def ground_colours(name):
    return tuple(map(colour, WORLD_COLOURS[name]))


def world_palette(name, count):
    colours = ground_colours(name)
    if count == 8:
        return list(colours)
    dirt, soil, sun, road, deep, water, leaf, tips = colours
    return [*colours, mix(dirt, soil), mix(soil, sun), shade(road, -17), shade(road, 18),
            mix(deep, water), shade(water, 30), mix(leaf, tips), shade(leaf, -16),
            shade(tips, 24), mix(dirt, road), shade(sun, 18), mix(sun, road)]


def links(column, x, y, lo, hi):
    """A central strip with arms to each linked edge, in N/E/S/W order."""
    across = column & 8 if x < lo else column & 2 if x > hi else True
    along = column & 1 if y < lo else column & 4 if y > hi else True
    return across and along


def canopy_tone(x, y, column, variant, winter):
    """Light a connected grove from its outer edges, with a quiet interior."""
    left = max(0, 6 - x) if not column & 8 else 0
    right = max(0, x - 9) if not column & 2 else 0
    top = max(0, 6 - y) if not column & 1 else 0
    bottom = max(0, y - 9) if not column & 4 else 0
    grain = ((x * 37 ^ y * 59 ^ x * y * 11 ^ variant * 31) & 15) - 7
    if column:
        if (left + right) ** 2 + (top + bottom) ** 2 > 32 + grain * 0.6:
            return None
    elif (x - 7.5) ** 2 + (y - 7.5) ** 2 > 57 + grain * 0.85:
        return None
    # Move the patches of foliage between variants and fade them at the joins.
    seam = min(x, 15-x, y, 15-y, 3) / 3
    patches = ((4, 6, 4), (10, 9, 4), (8, 3, 3)) if variant == 0 else ((7, 10, 5), (11, 5, 4), (3, 7, 3))
    lobes = max(radius - math.hypot(x-cx, y-cy) for cx, cy, radius in patches)
    tone = 7.2 + top * 1.15 + left * 0.35 - bottom * 1.1 - right * 0.8
    return tone + (max(-1, lobes) * (1.3 if winter else 1.0) + grain * 0.18) * seam


def large_crown_tone(x, y):
    """An irregular rounded crown filling a two-by-two tile footprint."""
    grain = ((x * 37 ^ y * 59 ^ x * y * 11) & 15) - 7
    lobes = ((15,10,8.3),(9,15,8.2),(22,15,8.3),(15,22,8.8))
    edge = min(math.hypot(x-cx,y-cy)-r for cx,cy,r in lobes)
    if edge > grain * .035: return None
    mound = min(math.hypot(x-cx,y-cy)/r for cx,cy,r in lobes)
    # Warm top-down tree reference, with foliage in irregular clusters.
    light = 16 - y * .39 - x * .16 + (1-mound)*8
    return light + (grain*.3 if (x+y)%3==0 else 0)


def terrain_tile(name, kind, col):
    dirt, soil, sun, road, deep, water, leaf, tips = ground_colours(name)
    im = Image.new("RGB", (16, 16), soil)
    draw = ImageDraw.Draw(im)
    rng = Random(1949 + 37 * col + kind * 349)
    for i in range(20):
        x, y = rng.randrange(16), rng.randrange(16)
        draw.line((x, y, min(15, x + (i % 3 == 0)), y), fill=mix(soil, sun) if i < 13 else dirt)
    if kind == 0:
        ground_detail(draw, rng, name, col, dirt, soil, sun, leaf)

    if kind == 1:  # laid road, with edge and centre markings
        for y in range(16):
            for x in range(16):
                if links(col, x, y, 1, 14):
                    im.putpixel((x, y), shade(road, 18 if x < 8 and y < 8 else -17))
                if links(col, x, y, 2, 13):
                    im.putpixel((x, y), road)
        if col == 5:
            draw.line((7, 4, 7, 10), fill=mix(sun, road))
        elif col == 10:
            draw.line((4, 7, 10, 7), fill=mix(sun, road))
        for _ in range(9):
            x, y = rng.randrange(4, 12), rng.randrange(4, 12)
            if im.getpixel((x, y)) == road:
                draw.point((x, y), fill=shade(road, -17))
    elif kind == 2:  # pools and connected waterways
        for y in range(16):
            for x in range(16):
                shore = min(y if not col & 1 else 16, 15-x if not col & 2 else 16,
                            15-y if not col & 4 else 16, x if not col & 8 else 16)
                exposed_x = min(x if not col & 8 else 16, 15-x if not col & 2 else 16)
                exposed_y = min(y if not col & 1 else 16, 15-y if not col & 4 else 16)
                if exposed_x + exposed_y < 3:
                    continue
                if shore == 0 or exposed_x + exposed_y == 3:
                    im.putpixel((x, y), shade(water, 30))
                else:
                    im.putpixel((x, y), water if shore == 1 else deep)
        for x, y, length in ((3, 4, 4), (9, 7, 3), (5, 11, 5), (11, 14, 2)):
            for p in range(x, x + length):
                if im.getpixel((p, y)) == deep:
                    draw.point((p, y), fill=water if p == x + 1 else mix(deep, water))
        for x, y in ((2, 6), (7, 9), (12, 12)):
            if im.getpixel((x, y)) == deep:
                draw.line((x - 1, y, x, y), fill=mix(deep, water))
    elif kind == 3:  # rock, with three readable facets
        draw.polygon([(2, 5), (6, 2), (12, 3), (15, 9), (12, 14), (4, 14), (1, 10)], fill=DEEP)
        draw.polygon([(2, 6), (6, 3), (12, 4), (9, 7), (4, 10)], fill=mix(sun, road))
        draw.polygon([(4, 10), (9, 7), (12, 4), (14, 9), (11, 13), (5, 13)], fill=road)
        draw.line((5, 5, 8, 4), fill=sun)
    elif kind == 4:  # low shrubs, distinct from overhead cover
        draw.ellipse((2, 5, 14, 14), fill=shade(leaf, -16))
        draw.polygon([(1, 10), (3, 6), (6, 6), (8, 3), (12, 4), (14, 8), (12, 12), (5, 13)], fill=leaf)
        draw.line((3, 8, 6, 7, 8, 5, 11, 6), fill=tips, width=2)
        draw.line((7, 10, 10, 8, 13, 8), fill=mix(leaf, tips))
        if name == "winter":
            draw.line((5, 6, 8, 4, 11, 5), fill=sun)
    elif kind == 6:  # connected masonry
        for y in range(16):
            for x in range(16):
                if not links(col, x, y, 1, 14):
                    continue
                if not links(col, x, y, 3, 12):
                    pen = DEEP
                elif y % 5 == 2:
                    pen = METAL
                elif (x + 4 * (y // 5)) % 8 == 0:
                    pen = road
                else:
                    pen = STEEL if y % 5 == 3 else mix(sun, road)
                if links(col, x, y, 4, 11) and (x + y * 3) % 11 == 0:
                    pen = LIGHT if y % 5 == 3 else METAL
                im.putpixel((x, y), colour(pen) if isinstance(pen, str) else pen)
    elif kind in (5, 7, 9):  # broken shrubs, wall debris, ruined buildings
        for i in range(9):
            x, y = rng.randrange(2, 14), rng.randrange(2, 14)
            draw.line((x, y, x + (i % 3), y), fill=(leaf if kind == 5 else road))
            if i % 2:
                draw.point((x, y - 1), fill=sun)
    elif kind in (8, 10):
        draw.rectangle((0, 0, 15, 15), fill=road)
        draw.line((0, 0, 15, 0), fill=shade(road, 18))
        draw.line((0, 15, 15, 15), fill=shade(road, -17))
        draw.line((0, 0, 0, 15), fill=shade(road, -17))
    return im


def ground_detail(draw, rng, name, col, dirt, soil, sun, leaf):
    x, y = rng.randrange(2, 9), rng.randrange(4, 10)
    if name == "winter":
        # A low drift with a cool underside and a broken, sunlit crest.
        draw.line((x, y+2, x+2, y+1, x+5, y+1, x+6, y), fill=dirt)
        draw.line((x, y+1, x+2, y, x+5, y, x+6, y-1), fill=sun)
        draw.line((x+2, y+3, x+4, y+3), fill=mix(dirt, soil))
    elif name in ("jungle", "swamp") and col % 2 == 0:
        draw.line((x, y+4, x+3, y, x+6, y+2), fill=dirt)
        draw.line((x+3, y+3, x+3, y, x+4, y-1), fill=leaf)
        draw.point((x+3, y), fill=sun)
    elif name == "scifi":
        draw.line((0, 15, 15, 15, 15, 0), fill=dirt)
        draw.line((1, 0, 12, 0), fill=mix(soil, sun))
        for p in ((2, 2), (13, 2), (2, 13), (13, 13)):
            draw.point(p, fill=sun)
    elif name in ("volcanic", "moon") and col % 3 == 0:
        draw.arc((x, y-2, x+6, y+4), 180, 350, fill=dirt)
        draw.arc((x, y-2, x+6, y+4), 0, 140, fill=sun)
    elif col % 3 != 0:
        draw.line((x, y, x+2, y-1, x+5, y-1), fill=dirt)
        draw.line((x+1, y+1, x+3, y, x+5, y), fill=mix(soil, sun))
        draw.point((x+6, y+2), fill=dirt)


def terrain_atlas(name):
    atlas = Image.new("RGB", (256, 176))
    for kind in range(11):
        for col in range(16):
            atlas.paste(terrain_tile(name, kind, col), (col * 16, kind * 16))
    return atlas.convert("RGBA")


def sprite():
    return Image.new("RGBA", (32, 32))


def tank(team, launcher=False, world="classic", cls=1):
    im = sprite()
    d = ImageDraw.Draw(im)
    dark, body, light = TEAM_COLOURS[team]
    for x in (6, 20):
        d.rounded_rectangle((x, 7, x + 5, 26), radius=1, fill=DEEP, outline=INK)
        for y in range(9, 26, 3):
            d.line((x + 1, y, x + 4, y), fill=STEEL if y == 9 else METAL)
        edge = x + (1 if x == 6 else 4)
        for y in (11, 17, 23):
            d.line((edge, y, edge, y + 1), fill=LIGHT if y == 11 else STEEL)
    d.polygon([(10, 8), (21, 8), (23, 12), (23, 22), (20, 26), (11, 26), (8, 23), (8, 12)], fill=INK)
    d.polygon([(11, 9), (20, 9), (22, 12), (22, 21), (19, 25), (12, 25), (9, 22), (9, 12)], fill=body)
    d.line((10, 13, 10, 20, 12, 23, 18, 23), fill=dark)
    d.line((11, 10, 20, 10, 21, 12), fill=light)
    d.rectangle((13, 21, 19, 24), fill=dark)
    for x in (14, 16, 18):
        d.line((x, 22, x, 23), fill=INK)
    if not launcher:
        d.rectangle((14, 3, 17, 15), fill=INK)
        d.line((15, 4, 15, 13), fill=STEEL)
    d.rounded_rectangle((11, 12, 20, 20), radius=2, fill=dark, outline=INK)
    d.rounded_rectangle((12, 12, 19, 18), radius=2, fill=body)
    d.line((13, 13, 17, 13), fill=light)
    d.rectangle((15, 15, 17, 17), fill=dark)
    d.line((15, 15, 17, 15), fill=light)
    d.point((17, 16), fill=INK)
    d.point((11, 21), fill=light)
    d.point((20, 21), fill=light)
    d.point((11, 11), fill=WHITE)
    d.point((20, 11), fill=WHITE)
    if launcher:
        d.rounded_rectangle((10, 5, 21, 20), radius=2, fill=dark, outline=INK)
        d.rectangle((12, 6, 19, 18), fill=body)
        d.rectangle((12, 6, 19, 9), fill=DEEP, outline=INK)
        for x in (13, 16, 18):
            d.line((x, 7, x, 8), fill=STEEL)
            d.line((x, 10, x, 16), fill=light)
        d.line((11, 17, 20, 17), fill=METAL)
        d.point((14, 19), fill=GLASS)
        d.point((17, 19), fill=GLASS)
    if world == "volcanic" and not launcher:
        d.rounded_rectangle((9,9,22,23), radius=4, fill=dark, outline=INK)
        d.ellipse((11,10,20,19), fill=body, outline=light)
        for x,y in ((10,11),(21,11),(10,21),(21,21)): d.point((x,y),fill=LIGHT)
        d.rectangle((14,3,17,14), fill=INK); d.line((15,4,15,13),fill=STEEL)
        d.rectangle((12,21,19,23), fill=DEEP)
    elif world == "moon" and not launcher:
        d.polygon(((9,23),(10,10),(15,7),(21,10),(22,23)),fill=dark,outline=INK)
        d.polygon(((11,15),(15,10),(20,15),(19,21),(12,21)),fill=body,outline=light)
        for x in (10,20): d.line((x,14,x,20),fill=STEEL)
        for x in ((13,17) if cls==2 else (15,)):
            d.rectangle((x-1,3,x+1,14),fill=INK); d.line((x,4,x,13),fill=LIGHT)
        d.line((13,22,18,22),fill=GLASS)
    if cls == 0:
        im = im.resize((28,28), Image.Resampling.NEAREST)
        out = sprite(); out.paste(im,(2,2)); im=out
    elif cls == 2:
        im = im.resize((36,36), Image.Resampling.NEAREST).crop((2,2,34,34))
    return im


def vehicle(kind, variant):
    im = sprite()
    d = ImageDraw.Draw(im)
    dark, body, light = TEAM_COLOURS[variant % 2]
    if variant == 2:
        dark, body, light = DEEP, METAL, BRASS
    width = (5, 5, 4, 6, 5, 5)[kind]
    half_length = (11, 12, 6, 10, 10, 7)[kind]
    top, bottom = 16 - half_length, 15 + half_length
    for x in (15 - width - 1, 16 + width):
        for y in (top + 2, bottom - 3):
            d.rectangle((x, y, x + 1, y + 3), fill=INK)
    d.rounded_rectangle((15 - width, top, 16 + width, bottom), radius=2, fill=dark, outline=INK)
    d.rounded_rectangle((16 - width, top + 1, 15 + width, bottom - 2), radius=1, fill=body)
    d.rectangle((17 - width, top + 4, 14 + width, top + 6), fill=GLASS if variant != 2 else DEEP)
    d.line((17 - width, top + 2, 14 + width, top + 2), fill=light)
    for y in range(top + 8, bottom - 2, 3):
        d.line((17 - width, y, 14 + width, y), fill=dark)
    if kind == 1:
        d.rounded_rectangle((12, top + 9, 19, 23), radius=3, fill=STEEL, outline=DEEP)
        d.line((14, top + 11, 14, 21), fill=LIGHT)
    if kind == 5:
        d.line((15, top+8, 15, top-2), fill=INK, width=2)
    if variant == 2:
        d.line((17-width, 17, 16+width, bottom-3), fill=INK)
    return im


def turret(kind):
    im = sprite()
    d = ImageDraw.Draw(im)
    width = 5 if kind in (0, 1, 4) else 7
    d.ellipse((15 - width, 11, 16 + width, 21), fill=INK)
    d.ellipse((16 - width, 11, 15 + width, 19), fill=METAL)
    d.arc((16 - width, 12, 15 + width, 18), 170, 300, fill=LIGHT)
    barrels = (13, 17) if kind in (2, 4) else (15,)
    for x in barrels:
        d.rectangle((x, 6 + (kind == 5) * 3, x + 2, 16), fill=DEEP)
        d.line((x, 7 + (kind == 5) * 3, x, 14), fill=STEEL)
    d.rectangle((14, 15, 17, 17), fill=BRASS)
    return im


def infantry():
    atlas = Image.new("RGBA", (192, 36))
    uniforms = ((STEEL, WHITE, LIGHT), ("#337744", "#77bb55", "#bbee77"), ("#997744", "#ee9955", "#ffdd55"))
    for row, (shadow, jacket, highlight) in enumerate(uniforms):
        for direction in range(8):
            for step in range(2):
                im = Image.new("RGBA", (12, 12))
                d = ImageDraw.Draw(im)
                d.line((5, 7, 4, 9 + step), fill=INK, width=2)
                d.line((6, 7, 7, 10 - step), fill=INK, width=2)
                d.line((4, 7, 4, 8 + step), fill=shadow)
                d.line((7, 7, 7, 9 - step), fill=jacket)
                d.rounded_rectangle((3, 4, 8, 8), radius=1, fill=jacket, outline=INK)
                d.line((4, 5, 4, 7, 6, 7), fill=shadow)
                d.point((6, 5), fill=highlight)
                d.rectangle((4, 2, 7, 4), fill=shadow, outline=INK)
                d.line((5, 2, 6, 2), fill=highlight)
                d.line((5, 3, 6, 3), fill=BRASS)
                if row:
                    d.line((8, 6, 9, 2, 9, 1), fill=INK)
                    d.point((9, 2), fill=STEEL)
                else:
                    d.point((3 if step else 8, 5), fill=WHITE)
                atlas.paste(im.rotate(-45 * direction, center=(6, 6)), ((2 * direction + step) * 12, row * 12))
    return atlas


def original_building_overlay(kind, palette):
    """Draw the Original world's fittings in native pixels, lit from the upper left."""
    layer = Image.new("RGBA", (64, 48))
    draw = ImageDraw.Draw(layer)
    ink, shadow, iron, steel, bronze, copper, gold, white, green, lime = (
        palette[i] for i in (6, 12, 4, 5, 3, 8, 11, 10, 14, 13)
    )

    def plate(box, face, light, dark):
        x, y, right, bottom = box
        draw.rectangle(box, fill=face, outline=ink)
        draw.line((x + 1, bottom - 1, x + 1, y + 1, right - 1, y + 1), fill=light)
        draw.line((x + 2, bottom - 1, right - 1, bottom - 1, right - 1, y + 2), fill=dark)

    def rivet(x, y):
        draw.line((x, y, x, y + 1), fill=shadow)
        draw.point((x, y), fill=steel)

    def grille(x, y, width, height):
        plate((x, y, x + width - 1, y + height - 1), shadow, steel, iron)
        for row in range(y + 3, y + height - 2, 2):
            draw.line((x + 2, row, x + width - 3, row), fill=ink)

    def pipe(points, light=steel):
        draw.line([(x + 1, y + 1) for x, y in points], fill=ink, width=3)
        draw.line(points, fill=iron, width=3)
        draw.line(points, fill=light)

    def vessel(x, y, width, height, face=iron):
        draw.ellipse((x + 2, y + 2, x + width + 2, y + height + 2), fill=shadow, outline=ink)
        draw.ellipse((x, y, x + width, y + height), fill=face, outline=ink)
        draw.arc((x + 1, y + 1, x + width - 1, y + height - 1), 160, 295, fill=steel)
        draw.arc((x + 2, y + 2, x + width - 1, y + height - 1), 0, 102, fill=shadow)

    def ribbed_roof(x, y, count, spacing, height):
        for i in range(count):
            left = x + i * spacing
            plate((left, y, left + spacing, y + height), bronze, copper, shadow)
            draw.line((left + 2, y + 5, left + 3, y + 3, left + spacing - 2, y + 3), fill=copper)
            draw.line((left + 2, y + height - 3, left + spacing - 2, y + height - 3), fill=iron)
            draw.point((left + spacing - 2, y + height - 2), fill=steel)
        draw.line((x, y + height // 2, x + count * spacing, y + height // 2), fill=ink, width=2)
        draw.line((x, y + height // 2, x + count * spacing, y + height // 2), fill=iron)

    if kind == 0:  # watchtower window and footplate
        plate((2, 2, 13, 13), bronze, copper, shadow)
        plate((4, 4, 11, 9), shadow, steel, iron)
        draw.point((6, 6), fill=gold)
        draw.line((5, 11, 10, 11), fill=iron)
    elif kind == 1:  # reinforced bunker and its embrasure
        plate((7, 6, 28, 27), iron, steel, shadow)
        plate((10, 8, 24, 18), bronze, copper, shadow)
        grille(12, 11, 10, 6)
        plate((10, 21, 23, 24), ink, steel, shadow)
        for x, y in ((9, 8), (25, 8), (9, 24), (25, 24)):
            rivet(x, y)
    elif kind == 2:  # fuel depot: two round tanks, valves and a pipe manifold
        plate((1, 1, 29, 29), iron, steel, shadow)
        pipe(((5, 24), (5, 15), (18, 15), (18, 5), (26, 5)), copper)
        vessel(3, 3, 13, 13)
        vessel(14, 13, 13, 13)
        for x, y in ((9, 8), (20, 18)):
            plate((x - 2, y - 2, x + 3, y + 3), shadow, steel, iron)
            draw.point((x, y), fill=gold)
        grille(23, 3, 5, 8)
        rivet(5, 26)
    elif kind in (3, 11):  # barracks and warehouse: raised bays over ribbed siding
        plate((1, 2, 45, 27), bronze, copper, shadow)
        ribbed_roof(3, 4, 5 if kind == 3 else 4, 8 if kind == 3 else 10, 18)
        draw.line((4, 25, 42, 25), fill=iron)
        for x in range(6, 44, 8):
            draw.rectangle((x, 23, x + 2, 26), fill=green)
            draw.point((x + 1, 23), fill=lime)
        if kind == 11:
            grille(5, 17, 8, 6)
            grille(34, 17, 8, 6)
    elif kind == 4:  # bio-dome: steel ribs across green glazing
        draw.arc((3, 3, 43, 43), 150, 316, fill=steel, width=2)
        draw.arc((5, 5, 42, 42), 0, 147, fill=shadow, width=2)
        for inset in (0, 5):
            draw.arc((10 + inset, 6, 38 - inset, 39), 80, 275, fill=iron)
            draw.arc((7, 11 + inset, 40, 35 - inset), 177, 353, fill=steel)
        grille(18, 39, 10, 6)
        for x, y in ((11, 11), (35, 12), (12, 33), (34, 34)):
            rivet(x, y)
    elif kind == 5:  # command HQ: deck, landing markings, aerial and vent stacks
        plate((0, 1, 60, 45), iron, steel, shadow)
        plate((3, 4, 57, 41), iron, steel, shadow)
        draw.line((29, 6, 29, 39), fill=shadow)
        draw.line((30, 6, 30, 39), fill=steel)
        draw.ellipse((6, 12, 25, 33), outline=shadow, width=2)
        draw.ellipse((7, 12, 24, 31), outline=gold)
        draw.line((12, 17, 12, 27), fill=gold)
        draw.line((19, 17, 19, 27), fill=gold)
        draw.line((12, 22, 19, 22), fill=gold)
        grille(34, 8, 8, 9)
        grille(34, 26, 8, 9)
        pipe(((44, 35), (51, 35), (51, 22)))
        vessel(43, 9, 12, 12)
        draw.line((49, 15, 55, 10), fill=ink)
        draw.point((55, 10), fill=gold)
        plate((47, 36, 56, 40), bronze, copper, shadow)
        for x in range(7, 56, 7):
            rivet(x, 7)
            rivet(x, 38)
    elif kind == 6:  # garage: reinforced roof and segmented doors flanking the open bay
        plate((0, 0, 46, 32), bronze, copper, shadow)
        plate((4, 4, 41, 28), iron, steel, shadow)
        for y in (7, 17):
            for x in (7, 18, 29):
                plate((x, y, x + 10, y + 8), iron, steel, shadow)
        for x, y in ((8, 9), (36, 9), (8, 23), (36, 23)):
            draw.line((x + 1, y + 1, 23, 17), fill=shadow, width=2)
            draw.line((x, y, 22, 16), fill=steel)
        plate((19, 12, 26, 20), shadow, steel, iron)
        for x in (1, 32):
            plate((x, 33, x + 14, 47), shadow, steel, iron)
            for slat in range(x + 3, x + 13, 3):
                draw.line((slat, 36, slat, 45), fill=iron)
                draw.point((slat, 36), fill=steel)
        for x in (14, 32):
            draw.point((x, 35), fill=gold)
    elif kind == 7:  # first aid: enamel sign, service hatch and lit door
        plate((1, 1, 29, 29), steel, white, shadow)
        plate((5, 4, 25, 21), white, white, iron)
        draw.rectangle((13, 7, 17, 18), fill=copper)
        draw.rectangle((9, 11, 21, 15), fill=copper)
        draw.line((13, 8, 13, 10), fill=bronze)
        grille(4, 23, 7, 5)
        plate((13, 23, 23, 29), iron, steel, shadow)
        draw.line((16, 25, 20, 25), fill=ink)
        draw.point((26, 25), fill=gold)
    elif kind == 8:  # prisoner compound: ribbed huts and a floodlit watch post
        for x, y in ((5, 6), (5, 27), (26, 27)):
            ribbed_roof(x, y, 3, 5, 12)
        plate((26, 7, 39, 20), iron, steel, shadow)
        grille(28, 10, 9, 7)
        for x, y in ((4, 3), (42, 3), (4, 43), (42, 43)):
            plate((x - 1, y - 1, x + 2, y + 2), bronze, copper, shadow)
            draw.point((x, y), fill=gold)
    elif kind == 9:  # radar: open lattice, raised rim and feed cable
        plate((1, 1, 29, 29), iron, steel, shadow)
        pipe(((20, 18), (24, 22), (24, 25), (20, 25)), bronze)
        vessel(3, 2, 21, 21, bronze)
        draw.arc((4, 3, 23, 22), 168, 296, fill=copper)
        draw.ellipse((5, 4, 22, 21), fill=shadow, outline=ink)
        for y, x in ((6, 9), (9, 6), (12, 6), (15, 6), (18, 8)):
            draw.line((x, y, 27 - x, y), fill=iron)
        for x, y in ((9, 6), (16, 5), (20, 8), (21, 14), (17, 18), (9, 18), (6, 13), (7, 9)):
            draw.line((13, 12, x, y), fill=steel)
        plate((11, 10, 15, 14), bronze, copper, shadow)
        plate((17, 23, 24, 28), bronze, copper, shadow)
        draw.point((21, 25), fill=gold)
    elif kind == 10:  # power station: turbine grilles and connecting bus
        plate((1, 5, 45, 29), iron, steel, shadow)
        pipe(((6, 24), (34, 24), (38, 20), (38, 9)), copper)
        for x in (3, 21):
            vessel(x, 2, 15, 16)
            draw.ellipse((x + 3, 5, x + 12, 14), fill=ink, outline=shadow)
            for y in range(7, 14, 2):
                draw.line((x + 5, y, x + 10, y), fill=steel)
        grille(39, 10, 6, 12)
        for x in (8, 16, 26, 34):
            rivet(x, 26)
    elif kind in (12, 13, 18):  # light gun mounts, below the moving gun
        if kind == 13:
            plate((1, 1, 13, 13), bronze, copper, shadow)
            grille(4, 4, 8, 8)
        else:
            vessel(2, 2, 11, 11)
        for x in (4, 11):
            rivet(x, 10)
    elif kind in (14, 15, 19):  # armoured gun beds and mortar pit
        if kind == 19:
            vessel(2, 2, 26, 26, bronze)
            draw.ellipse((8, 8, 23, 23), fill=ink, outline=steel)
        else:
            plate((1, 1, 29, 29), bronze, copper, shadow)
            plate((5, 5, 25, 25), iron, steel, shadow)
            grille(9, 8, 14, 15)
        for x, y in ((6, 8), (24, 8), (6, 22), (24, 22)):
            rivet(x, y)
    elif kind == 16:  # flag bunker: interlocking plates around the hatch
        plate((1, 1, 29, 29), iron, steel, shadow)
        for x in (3, 21):
            plate((x, 5, x + 6, 23), iron, steel, shadow)
            rivet(x + 2, 8)
            rivet(x + 2, 20)
        plate((9, 5, 21, 24), shadow, steel, ink)
        draw.line((12, 7, 12, 20), fill=iron)
        draw.rectangle((15, 10, 19, 15), fill=copper)
        draw.point((16, 11), fill=gold)
        for x in range(7, 25, 4):
            draw.line((x, 27, x + 1, 26), fill=gold)
    elif kind == 17:  # landing pad: segmented rim and lights outside the target
        draw.arc((3, 3, 43, 43), 160, 305, fill=steel, width=2)
        draw.arc((4, 4, 43, 43), 0, 140, fill=shadow, width=2)
        for x, y in ((12, 9), (33, 9), (8, 30), (37, 31), (16, 39), (28, 40)):
            plate((x, y, x + 3, y + 3), iron, steel, shadow)
            draw.point((x + 1, y + 2), fill=gold)
    return layer


def detail_original_structures(im, palette):
    """Apply the detail to both conditions, retaining the footprint and battle damage."""
    pixels = im.load()
    for kind in range(20):                        # the atlas rows follow StructType
        layer = original_building_overlay(kind, palette).load()
        for y in range(48):
            row = kind * 48 + y
            for x in range(64):
                paint = layer[x, y]
                if not paint[3]:
                    continue
                intact = pixels[x, row]
                damaged = pixels[x + 64, row]
                if intact[3] >= 128:
                    pixels[x, row] = (*paint[:3], intact[3])
                if damaged == intact and damaged[3] >= 128:
                    pixels[x + 64, row] = (*paint[:3], damaged[3])
    return im


def quiet_original_sand(im):
    """Soften sand in the retro atlas, including the backing around rocks and foliage."""
    sand = (246, 206, 146)
    grain = {(212, 160, 98), (166, 116, 66), (176, 130, 78)}
    bright_grain = (255, 232, 196)
    rng = Random(1987)
    pixels = im.load()
    for y in range(im.height):
        for x in range(im.width):
            rgb, alpha = pixels[x, y][:3], pixels[x, y][3]
            if rgb == bright_grain or (rgb in grain and rng.randrange(3) != 0):
                pixels[x, y] = (*sand, alpha)
            elif rgb in grain:
                pixels[x, y] = (176, 130, 78, alpha)
    return im


def detail_original_walls(im, palette):
    """Add grooves and wear within the retro wall faces, leaving their edges in place."""
    pixels = im.load()
    for col in range(16):
        for y in range(16):
            for x in range(16):
                pos = col * 16 + x, 6 * 16 + y
                if pixels[pos][:3] != (116, 116, 120):
                    continue
                if y % 6 == 4:
                    pen = 12
                elif (x + (y // 6) * 4) % 8 == 3:
                    pen = 12
                elif (x * 3 + y * 11) % 19 == 0:
                    pen = 5
                else:
                    continue
                pixels[pos] = (*palette[pen], 255)
    return im


def battle_atlas(palette):
    """Impacts, blasts, embers, gate pieces and the respawn skull, in four frames each."""
    atlas = Image.new("RGBA", (128, 192))
    ink, dark, metal, steel, rust, orange, gold, hot = [palette[i] for i in (0, 12, 4, 5, 3, 8, 11, 10)]
    for row, sizes in enumerate(((4, 6, 7, 8), (6, 9, 12, 13))):
        for phase, radius in enumerate(sizes):
            im = sprite()
            d = ImageDraw.Draw(im)
            rng = Random(1967 + row)
            # Jagged silhouette, hot lobes and sparks travelling out from the impact.
            edge = []
            for ray in range(16):
                a = math.tau * ray / 16
                r = radius * rng.uniform(0.60, 1.02)
                edge.append((round(16 + r * math.cos(a)), round(16 + r * math.sin(a))))
            d.polygon(edge, fill=(rust, orange, rust, dark)[phase])
            for y in range(16 - radius, 17 + radius):
                for x in range(16 - radius, 17 + radius):
                    if not im.getpixel((x, y))[3]:
                        continue
                    level = math.hypot(x - 16, y - 16) / radius + rng.uniform(-0.26, 0.26)
                    if phase < 3:
                        pen = hot if level < (0.4, 0.26, 0.12)[phase] else gold if level < 0.51 else orange if level < 0.72 else rust
                    else:
                        pen = orange if level < 0.23 else metal if rng.randrange(4) == 0 else dark
                    d.point((x, y), fill=pen)
            sparks = Random(613 + row)
            for i in range(12 + row * 12):
                a = sparks.uniform(0, math.tau)
                r = (radius + 2) * sparks.uniform(0.75, 1.0)
                dx, dy = math.cos(a), math.sin(a)
                x, y = round(16 + r * dx), round(16 + r * dy)
                pen = (gold, orange, hot)[i % 3] if phase < 3 else (rust, dark, orange)[i % 3]
                if phase in (1, 2) and i % 3 == 0:
                    d.line((round(x - 2 * dx), round(y - 2 * dy), x, y), fill=rust)
                d.point((x, y), fill=pen)
            atlas.paste(im, (phase * 32, row * 32))

    for phase, tongue in enumerate((-2, 0, 1, -1)):
        im = sprite()
        d = ImageDraw.Draw(im)
        d.ellipse((12, 16, 20, 19), fill=dark)
        d.polygon(((13, 17), (14, 14), (15 + tongue, 11 + (phase & 1)), (17, 15),
                   (19, 14), (18, 18), (15, 18)), fill=orange)
        d.line((15, 17, 16, 15, 17, 17), fill=gold)
        d.point((16, 17), fill=hot)
        d.point((19 - phase, 11 - phase), fill=rust if phase & 1 else orange)
        atlas.paste(im, (phase * 32, 64))

    for part, half in enumerate((6, 4, 2, 4)):
        im = sprite()
        d = ImageDraw.Draw(im)
        d.rectangle((12, 16 - half, 19, 16 + half), fill=ink)
        d.rectangle((12, 17 - half, 19, 15 + half), fill=metal)
        d.line((12, 17 - half, 19, 17 - half), fill=steel)
        if part == 2:
            for x in range(12, 20):
                d.point((x, 16), fill=hot if x < 16 else orange)
        else:
            d.line((19, 17 - half, 19, 15 + half), fill=dark)
            d.rectangle((14, 15, 17, 17), fill=dark)
            d.line((14, 15, 17, 15), fill=steel)
            d.point((16, 16), fill=gold if part in (0, 3) else metal)
        atlas.paste(im, (part * 32, 96))
        atlas.paste(im.transpose(Image.Transpose.TRANSPOSE), (part * 32, 128))
    for phase, gap in enumerate((0, 2, 4, 2)):
        im = sprite()
        d = ImageDraw.Draw(im)
        d.polygon(((3, 20), (4, 11), (7, 5), (12, 2), (20, 2), (25, 5),
                   (28, 11), (29, 20), (25, 23), (7, 23)), fill=palette[14], outline=ink)
        d.polygon(((6, 13), (8, 7), (12, 4), (17, 3), (19, 4), (16, 7), (11, 8)),
                  fill=palette[9])
        d.line((5, 15, 9, 10, 22, 10, 27, 14), fill=dark, width=2)
        d.polygon(((8, 22), (8, 13), (12, 10), (21, 10), (24, 13), (24, 22),
                   (20, 23), (19, 26), (12, 26), (11, 23)), fill=rust, outline=ink)
        d.polygon(((9, 21), (9, 14), (12, 11), (20, 11), (23, 14), (23, 20),
                   (20, 22), (18, 24), (13, 24), (11, 22)), fill=hot)
        d.polygon(((9, 16), (13, 15), (15, 17), (14, 19), (10, 19)), fill=ink)
        d.polygon(((17, 17), (19, 15), (23, 16), (22, 19), (18, 19)), fill=ink)
        d.polygon(((14, 21), (16, 18), (18, 21)), fill=dark)
        d.line((11, 23, 13, 26 + gap, 18, 26 + gap, 21, 23), fill=rust, width=2)
        d.line((13, 25 + gap, 18, 25 + gap, 20, 23 + gap // 2), fill=hot)
        for x in (13, 16, 18):
            d.point((x, 24), fill=hot)
        atlas.paste(im, (phase * 32, 160))
    return atlas


def direction_atlas(rows):
    """Keep the renderer's direction indexing, with the first frame facing north."""
    atlas = Image.new("RGBA", (1024, len(rows) * 32))
    for row, (im, count) in enumerate(rows):
        for i in range(count):
            atlas.paste(im.rotate(-360 * i / count, center=(16, 16)), (i * (32 // count) * 32, row * 32))
    return atlas


def structures_atlas(sizes):
    atlas = Image.new("RGBA", (192, 960))
    for kind, (width, height) in enumerate(sizes):
        w, h = width * 16, height * 16
        intact = building(kind, w, h)
        atlas.paste(intact, (0, kind * 48))
        wear = intact.copy()
        d = ImageDraw.Draw(wear)
        d.line((w//4, h//4, w//2, h//3, w//2-2, h//2, 3*w//4, 2*h//3), fill=INK)
        d.polygon([(w//3, h//2), (w//3+4, h//2-3), (w//3+6, h//2+2)], fill=DEEP)
        atlas.paste(wear, (64, kind * 48))
    return atlas


def building(kind, w, h):
    im = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(im)
    if kind == 17:  # helipad
        d.rectangle((1, 1, w-2, h-2), fill=DEEP, outline=INK)
        d.rectangle((5, 5, w-6, h-6), outline=STEEL)
        d.ellipse((10, 10, w-11, h-11), outline=GOLD)
        d.line((19, 16, 19, 31), fill=LIGHT, width=3)
        d.line((28, 16, 28, 31), fill=LIGHT, width=3)
        d.line((19, 23, 28, 23), fill=LIGHT, width=3)
        for x in (7, w-10):
            for y in (7, h-9):
                d.rectangle((x, y, x+2, y+1), fill=METAL)
                d.point((x+1, y), fill=GOLD)
        return im
    if kind in (0, 12, 13, 14, 15, 18, 19):  # gun emplacements and watch tower
        d.polygon([(1, 4), (4, 1), (w-5, 1), (w-2, 4), (w-2, h-5), (w-5, h-2),
                   (4, h-2), (1, h-5)], fill=DEEP, outline=INK)
        d.rectangle((4, 4, w-5, h-5), fill=METAL)
        d.line((3, h-6, 3, 3, w-5, 3), fill=STEEL)
        for x, y in ((4, 4), (w-5, 4), (4, h-5), (w-5, h-5)):
            d.point((x, y), fill=GOLD)
        for x in range(6, w-5, 3):
            d.line((x, h-7, x+1, h-7), fill=STEEL)
            d.point((x, h-6), fill=DEEP)
        if kind == 0:
            d.rectangle((5, 4, 10, 11), fill=INK, outline=LIGHT)
            d.line((6, 6, 9, 6), fill=GLASS)
        return im
    roof = {2: "#8e755a", 3: "#637e69", 4: "#607f8f", 5: "#5d7280", 7: "#8b9b98",
            8: "#746756", 9: "#5d7b89", 10: "#82664f", 11: "#747f88", 16: "#8f6454"}.get(kind, METAL)
    d.rounded_rectangle((2, 4, w-1, h-1), radius=2, fill=INK)
    d.rounded_rectangle((0, 1, w-3, h-3), radius=2, fill=DEEP, outline=INK)
    d.rounded_rectangle((1, 1, w-5, h-8), radius=2, fill=roof, outline=STEEL)
    d.line((3, 3, w-7, 3), fill=LIGHT)
    d.line((2, h-9, w-4, h-9), fill=INK)
    if kind == 6:  # garage apron and open bay
        d.rectangle((1, h-16, w-3, h-2), fill=DEEP, outline=INK)
        d.rectangle((w//2-7, h-16, w//2+6, h-2), fill=INK)
        d.rectangle((w//2-5, h-12, w//2+4, h-3), fill=METAL)
        for x in range(4, w-4, 5):
            if abs(x-w//2) > 8:
                d.line((x, h-14, x, h-4), fill=STEEL)
        for x in range(w//2-6, w//2+6, 4):
            d.line((x, h-3, x+1, h-4), fill=GOLD)
        for x in (w//2-10, w//2+8):
            d.rectangle((x, h-19, x+2, h-17), fill=GOLD)
    else:
        d.rectangle((w//2-4, h-10, w//2+2, h-3), fill=INK)
        d.line((w//2-3, h-8, w//2-3, h-4), fill=STEEL)
    if kind in (4, 9):
        d.ellipse((5, 5, w-8, h-11), fill=DEEP, outline=INK)
        d.ellipse((6, 5, w-9, h-13), fill=roof, outline=LIGHT)
        d.arc((8, 7, w-11, h-15), 175, 290, fill=WHITE)
        d.line((w//2, 6, w//2, h-14), fill=STEEL)
        if kind == 9:
            d.line((w//2, h//2, w-9, 4), fill=INK, width=2)
            d.point((w-9, 4), fill=GOLD)
    elif kind in (2, 10):
        for x in range(5, w-12, 10):
            d.rounded_rectangle((x, 6, x+7, h-12), radius=3, fill=BRASS, outline=INK)
            d.line((x+2, 8, x+2, h-15), fill=GOLD)
            d.line((x+2, h-14, x+5, h-14), fill=DEEP)
    elif kind == 7:
        d.rectangle((w//2-8, 6, w//2+6, h-12), fill=WHITE, outline=DEEP)
        d.rectangle((w//2-3, 8, w//2+1, h-14), fill=RED)
        d.rectangle((w//2-6, 12, w//2+4, 16), fill=RED)
    elif kind in (3, 11):
        for x in range(5, w-11, 10):
            d.rectangle((x, 6, x+6, h-13), fill=DEEP, outline=INK)
            d.line((x+1, 7, x+1, h-14), fill=GLASS if kind == 3 else BRASS)
            d.line((x+1, 7, x+5, 7), fill=LIGHT if kind == 3 else GOLD)
    else:
        for x in range(5, w-7, 8):
            d.line((x, 5, x, h-18 if kind == 6 else h-12), fill=DEEP)
            d.line((x+1, 6, x+1, h-19 if kind == 6 else h-13), fill=STEEL)
        if kind in (1, 5, 8, 16):
            d.rectangle((w//2-5, 6, w//2+4, 10), fill=INK)
            d.line((w//2-3, 7, w//2+2, 7), fill=GOLD if kind in (8, 16) else GLASS)
        if kind in (5, 6, 8, 16):
            y = h // 2
            d.rectangle((w//2-8, y-2, w//2+7, y+3), fill=DEEP, outline=INK)
            d.line((w//2-7, y-1, w//2+6, y-1), fill=LIGHT)
            for x in range(w//2-5, w//2+7, 3):
                d.line((x, y, x, y+2), fill=STEEL)
    # Fasteners on the flashing, and a broken line of vents on the lower fascia.
    for x in range(5, w-7, 6):
        d.point((x, 3), fill=DEEP)
        d.point((x+1, 3), fill=WHITE)
    if kind != 6:
        for x in range(5, w-6, 3):
            if abs(x - w//2) > 5:
                d.line((x, h-7, x, h-5), fill=METAL)
                d.point((x, h-7), fill=STEEL)
    return im
