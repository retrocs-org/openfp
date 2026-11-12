#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The Amiga client's profile faces: the desktop's ten (data/avatars.png, 24 x 24 each) in the screen's
palette, as four bitplanes of 24 x 24 (three bytes a row), into src/platform/amiga/faces_art.h.

    tools/amiga/gen_faces.py
"""
import os
import sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from amiga_palette import RGB, SHARED_PENS, Nearest   # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
NEAREST = Nearest(RGB, SHARED_PENS)  # Opaque outlines may share the backdrop's ink.


def pen_of(px):
    r, g, b, a = px
    if a < 128:
        return 0                                          # (see-through: the dark backdrop, INK)
    return NEAREST((r, g, b))


im = Image.open(os.path.join(ROOT, "data", "avatars.png")).convert("RGBA")
px = im.load()
faces = []
for f in range(im.size[0] // 24):
    planes = [[[0, 0, 0] for _ in range(24)] for _ in range(4)]
    for y in range(24):
        for x in range(24):
            p = pen_of(px[f * 24 + x, y])
            for pl in range(4):
                if p >> pl & 1:
                    planes[pl][y][x // 8] |= 0x80 >> (x % 8)
    faces.append(planes)
with open(os.path.join(ROOT, "src", "platform", "amiga", "faces_art.h"), "w") as out:
    out.write("/* SPDX-License-Identifier: MIT */\n/* made by tools/amiga/gen_faces.py from data/avatars.png */\n")
    out.write("#define FACES %d\nstatic const uint8_t faces_art[FACES][4][24][3] = {\n" % len(faces))
    for planes in faces:
        out.write("  {\n" + ",\n".join("    { " + ", ".join("{ 0x%02x, 0x%02x, 0x%02x }" % tuple(r) for r in pl) + " }" for pl in planes) + "\n  },\n")
    out.write("};\n")
print("faces_art.h: %d faces" % len(faces))

# (the names and numbers for everyone; the data for the file that holds it: amiga_headers.py)
import sys as _s, os as _o
_s.path.insert(0, _o.path.dirname(_o.path.abspath(__file__)))
from amiga_headers import ids_only as _ids_only   # noqa: E402
_ids_only(_o.path.join(_o.path.dirname(_o.path.abspath(__file__)), "..", "..", "src", "platform", "amiga", "faces_art.h"))
