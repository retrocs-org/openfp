# SPDX-License-Identifier: MIT
"""The generated art and sound headers, written for two kinds of reader: the file that holds the data
(it includes the header plainly), and the rest of the Amiga client (src/platform/amiga/local/internal.h),
which wants only the names and numbers in it: with OFP_AMIGA_IDS_ONLY defined, the header gives its
#defines and enums and leaves the data out (local/art.c and amiga_sound.c, amiga_music.c have it)."""
import re


def ids_only(path):
    """wrap every `static const ...` definition in the header in #ifndef OFP_AMIGA_IDS_ONLY (once)"""
    text = open(path).read()
    if "OFP_AMIGA_IDS_ONLY" in text:
        return
    lines = text.split("\n")
    first = next((i for i, l in enumerate(lines) if l.startswith("static const")), None)
    if first is None:
        return
    last = max(i for i, l in enumerate(lines) if l.startswith("static const"))
    # the end of the last definition: its closing "};" (or the line itself, for one-liners)
    end = last
    if not lines[last].rstrip().endswith(";"):
        end = next(i for i in range(last, len(lines)) if lines[i].startswith("};"))
    lines.insert(end + 1, "#endif /* OFP_AMIGA_IDS_ONLY */")
    lines.insert(first, "#ifndef OFP_AMIGA_IDS_ONLY                 /* (the data: its own file's; the names above: everyone's) */")
    open(path, "w").write("\n".join(lines))
