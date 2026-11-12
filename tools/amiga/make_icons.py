#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Workbench icons for the Amiga release (tools/amiga/package_lha.sh): .info files in the classic OS 2.x/3.x
format (a DiskObject, two 4-colour planar images: normal and selected), drawn from the game's own art
(the Bison from data/styles/classic/tanks.png) in the Workbench's four colours, so they look right on any
Workbench from 2.0 on (NewIcons/GlowIcons tools convert them if you like).

    tools/amiga/make_icons.py OUTDIR       writes OpenFirePower.info (the drawer), OpenFirePower.info and
                                           OpenFirePowerOnline.info (the tools), Install.info, ReadMe.info
"""
import os
import struct
import sys

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
WB = ((0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB))   # the Workbench's pens 0..3
WBDISK, WBDRAWER, WBTOOL, WBPROJECT = 1, 2, 3, 4
GADGHCOMP, GADGIMAGE, GADGHIMAGE = 0x0000, 0x0004, 0x0002


def pen(rgb, alpha):
    if alpha < 128:
        return 0                                          # (the background: the window's grey)
    lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]    # (by brightness: a tank in four colours stays a tank)
    return 1 if lum < 70 else 3 if lum < 150 else 2


def tank_picture(size=(48, 34), angle=0, selected=False):
    """the Bison, turned a little, in the Workbench's colours; selected: the colours swapped, as WB does"""
    atlas = Image.open(os.path.join(ROOT, "data", "styles", "classic", "tanks.png")).convert("RGBA")
    cell = atlas.crop((angle * 32, 2 * 32, angle * 32 + 32, 3 * 32))
    cell = cell.crop(cell.getbbox()).resize((30, 32), Image.NEAREST)
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.paste(cell, ((size[0] - cell.size[0]) // 2, (size[1] - cell.size[1]) // 2), cell)
    px = canvas.load()
    rows = []
    for y in range(size[1]):
        row = []
        for x in range(size[0]):
            r, g, b, a = px[x, y]
            p = pen((r, g, b), a)
            if selected and p:
                p = {1: 2, 2: 1, 3: 3}[p]
            row.append(p)
        rows.append(row)
    return rows


def letter_picture(text, size=(40, 30), selected=False):
    """a page with a few lines on it (the ReadMe, the installer), its initial in blue"""
    rows = [[0] * size[0] for _ in range(size[1])]
    for y in range(2, size[1] - 2):
        for x in range(6, size[0] - 6):
            edge = y in (2, size[1] - 3) or x in (6, size[0] - 7)
            rows[y][x] = 1 if edge else 2
    for i, y in enumerate(range(8, size[1] - 6, 4)):
        for x in range(10, size[0] - 10 - (i % 2) * 6):
            rows[y][x] = 3 if text == "I" and i == 0 else 1
    if selected:
        rows = [[{0: 0, 1: 2, 2: 1, 3: 3}[p] for p in row] for row in rows]
    return rows


def image_struct(rows):
    h, w = len(rows), len(rows[0])
    words = (w + 15) // 16
    planes = b""
    for plane in range(2):
        for row in rows:
            bits = 0
            for x in range(words * 16):
                bits = bits << 1 | (x < w and (row[x] >> plane) & 1)
            planes += bits.to_bytes(words * 2, "big")
    head = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0)   # (ImageData, NextImage: "present" / none)
    return head + planes, w, h


def disk_object(kind, normal, selected, default_tool=b"", tooltypes=(), stack=0, drawer=False):
    img1, w, h = image_struct(normal)
    img2, _, _ = image_struct(selected)
    gadget = struct.pack(">IhhhhhhhIIIIIhI",
                         0, 0, 0, w, h + 1, GADGIMAGE | GADGHIMAGE, 3, 1,   # (RELVERIFY | GADGIMMEDIATE, BOOLGADGET)
                         1, 1, 0, 0, 0, 0, 1)                               # (render, select; UserData 1: OS 2.x)
    out = struct.pack(">HH", 0xE310, 1) + gadget
    out += struct.pack(">BBIIiiIII", kind, 0, 1 if default_tool else 0, 1 if tooltypes else 0,
                       -2147483648, -2147483648, 1 if drawer else 0, 0, stack)   # (NO_ICON_POSITION)
    if drawer:                                            # (DrawerData: where its window opens, and how big)
        out += struct.pack(">hhhhBBIIIIIIIhhHHH", 50, 30, 400, 160, 255, 255, 0, 0, 0, 0, 0, 0, 0,
                           90, 40, 0xFFFF, 0xFFFF, 1)   # (NewWindow: 48 bytes; Type 1: WBENCHSCREEN)
        out += struct.pack(">ii", 0, 0)                   # (dd_CurrentX, dd_CurrentY)
    out += img1 + img2
    if default_tool:
        out += struct.pack(">I", len(default_tool) + 1) + default_tool + b"\0"
    if tooltypes:
        out += struct.pack(">I", (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            out += struct.pack(">I", len(t) + 1) + t + b"\0"
    return out


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    tank, tank_sel = tank_picture(), tank_picture(selected=True)
    icons = {
        "OpenFirePower.drawer.info": disk_object(WBDRAWER, tank, tank_sel, drawer=True),
        "OpenFirePower.info": disk_object(WBTOOL, tank_picture(angle=4), tank_picture(angle=4, selected=True),
                                          stack=65536),
        "OpenFirePowerOnline.info": disk_object(WBTOOL, tank_picture(angle=8), tank_picture(angle=8, selected=True),
                                                stack=65536),
        "Install.info": disk_object(WBPROJECT, letter_picture("I"), letter_picture("I", selected=True),
                                    default_tool=b"C:Installer", tooltypes=(b"APPNAME=OpenFP",
                                    b"MINUSER=AVERAGE", b"DEFUSER=AVERAGE"), stack=40000),
        "ReadMe.info": disk_object(WBPROJECT, letter_picture("R"), letter_picture("R", selected=True),
                                   default_tool=b"SYS:Utilities/MultiView", stack=8192),
    }
    for name, data in icons.items():
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(data)
    print("icons: %s" % ", ".join(sorted(icons)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
