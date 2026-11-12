#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# The profile avatars: ten little soldier portraits, 24 x 24 each, drawn here pixel by
# pixel (no one else's art: CC0, made for OpenFirePower). Writes data/avatars.png, ten
# side by side (240 x 24), and a big preview if asked:
#   tools/make_avatars.py [--preview out.png]
import os, sys
from PIL import Image

S = 24
OUT = os.path.join(os.path.dirname(__file__), '..', 'data', 'avatars.png')

def rgb(h): return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (255,)

INK = rgb('14120e')
SKIN = [rgb('f1c7a0'), rgb('e0a878'), rgb('c08458'), rgb('8d5a36'), rgb('5e3a22')]
SHADE = lambda c: tuple(max(0, int(v * 0.78)) for v in c[:3]) + (255,)
LIGHT = lambda c: tuple(min(255, int(v * 1.18 + 12)) for v in c[:3]) + (255,)

class P:
    def __init__(s): s.im = Image.new('RGBA', (S, S), (0, 0, 0, 0)); s.px = s.im.load()
    def set(s, x, y, c):
        if 0 <= x < S and 0 <= y < S: s.px[x, y] = c
    def rect(s, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1): s.set(x, y, c)
    def ellipse(s, cx, cy, rx, ry, c, y_from=-99, y_to=99):
        for y in range(S):
            for x in range(S):
                if ((x + .5 - cx) / rx) ** 2 + ((y + .5 - cy) / ry) ** 2 <= 1 and y_from <= y <= y_to: s.set(x, y, c)
    def outline(s):                                   # a dark rim round everything drawn
        src = s.im.copy().load()
        for y in range(S):
            for x in range(S):
                if src[x, y][3] == 0 and any(0 <= x + dx < S and 0 <= y + dy < S and src[x + dx, y + dy][3] and src[x + dx, y + dy][:3] != INK[:3]
                                             for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    s.set(x, y, INK)

def face(p, skin, eyes='dot'):
    p.ellipse(12, 13, 5.2, 6.0, skin)                 # head
    p.ellipse(12, 14.5, 4.6, 4.6, skin)
    p.rect(9, 18, 15, 19, SHADE(skin))                # chin shade
    p.set(6, 13, skin); p.set(17, 13, skin)           # ears
    p.set(6, 14, SHADE(skin)); p.set(17, 14, SHADE(skin))
    if eyes == 'dot':
        p.set(10, 13, INK); p.set(14, 13, INK)
    p.rect(11, 17, 13, 17, SHADE(skin))               # mouth
    p.set(12, 15, SHADE(skin))                        # nose

def body(p, uniform, collar=None):
    p.ellipse(12, 24.5, 9.5, 5.5, uniform)            # shoulders
    p.rect(10, 19, 14, 20, uniform)                   # neck in the collar
    if collar: p.rect(10, 20, 14, 20, collar)

def avatar(i):
    p = P()
    skin = SKIN[[0, 2, 4, 1, 3, 0, 2, 3, 1, 4][i]]
    if i == 0:     # a steel helmet, olive
        body(p, rgb('556b2f'), rgb('3e4f22')); face(p, skin)
        p.ellipse(12, 10, 7, 5.2, rgb('6b7d3a'), y_to=10); p.rect(5, 10, 18, 11, rgb('4e5c29'))
        p.set(9, 7, LIGHT(rgb('6b7d3a'))); p.set(10, 6, LIGHT(rgb('6b7d3a')))
        p.rect(7, 12, 7, 16, rgb('3a3a2a'))           # strap
    elif i == 1:   # a red beret, a moustache
        body(p, rgb('7a5230'), rgb('5c3c22')); face(p, skin)
        p.ellipse(11, 8.5, 6.5, 3.2, rgb('b3262a')); p.rect(6, 9, 17, 10, rgb('8e1c20'))
        p.set(16, 9, rgb('e8c23a'))                   # badge
        p.rect(10, 16, 14, 16, INK)                   # moustache
    elif i == 2:   # a field cap, freckles
        body(p, rgb('4a5a6a'), rgb('34404c')); face(p, skin)
        p.rect(7, 6, 17, 9, rgb('5b6e46')); p.rect(6, 9, 20, 10, rgb('46563a'))   # the cap and its peak
        p.set(9, 15, SHADE(skin)); p.set(15, 15, SHADE(skin))
    elif i == 3:   # a tank helmet, goggles up
        body(p, rgb('3b2f25'), rgb('2a211a')); face(p, skin)
        p.ellipse(12, 10, 6.8, 5.4, rgb('5a4130'), y_to=11)
        p.rect(5, 10, 6, 16, rgb('5a4130')); p.rect(17, 10, 18, 16, rgb('5a4130'))   # ear flaps
        p.rect(8, 8, 16, 9, rgb('2c2c34')); p.rect(9, 8, 10, 9, rgb('8fd4ff')); p.rect(14, 8, 15, 9, rgb('8fd4ff'))
    elif i == 4:   # a headset, short hair
        body(p, rgb('2f4f4f'), rgb('233b3b')); face(p, skin)
        p.ellipse(12, 8.5, 5.6, 3.0, rgb('2a1d14'))
        p.rect(5, 12, 6, 15, rgb('202020')); p.rect(17, 12, 18, 15, rgb('202020')); p.rect(6, 6, 17, 6, rgb('303030'))
        p.rect(15, 16, 17, 16, rgb('303030')); p.set(14, 16, rgb('e04040'))         # the microphone
    elif i == 5:   # a bandana, a scar
        body(p, rgb('6b4f2a'), rgb('4f3a1f')); face(p, skin)
        p.ellipse(12, 9, 6.0, 3.4, rgb('2e6b3a')); p.rect(6, 9, 18, 10, rgb('245530'))
        p.set(19, 11, rgb('2e6b3a')); p.set(19, 12, rgb('245530'))                  # the knot
        p.set(14, 12, rgb('b0504a')); p.set(15, 13, rgb('b0504a')); p.set(14, 14, rgb('b0504a'))
    elif i == 6:   # a winter hood
        body(p, rgb('dcdcd4'), rgb('b8b8ae'))
        p.ellipse(12, 12, 8.2, 8.6, rgb('cfcfc6'))
        face(p, skin); p.ellipse(12, 6.5, 6, 2.4, rgb('e8e8e0'))
        p.rect(4, 12, 5, 19, rgb('b8b8ae')); p.rect(18, 12, 19, 19, rgb('b8b8ae'))
    elif i == 7:   # a sci-fi visor
        body(p, rgb('30304a'), rgb('202036')); face(p, skin)
        p.ellipse(12, 10, 7.0, 5.6, rgb('4a4a6e'), y_to=11)
        p.rect(6, 11, 18, 13, rgb('1a1a2a')); p.rect(7, 12, 17, 12, rgb('40e0d0'))
        p.set(12, 5, rgb('ff6060'))
    elif i == 8:   # a mohawk, a cigar
        body(p, rgb('5a2e2e'), rgb('3e1f1f')); face(p, skin)
        p.rect(11, 3, 13, 9, rgb('d4402a')); p.rect(10, 5, 14, 8, rgb('b83420'))
        p.rect(14, 17, 17, 17, rgb('7a4a28')); p.set(18, 17, rgb('ff9040'))
    else:          # a peaked officer's cap
        body(p, rgb('2a3a5a'), rgb('d4b040')); face(p, skin)
        p.ellipse(12, 7.5, 7.2, 3.0, rgb('2a3a5a')); p.rect(7, 9, 17, 10, rgb('1e2a40'))
        p.rect(6, 10, 18, 10, INK); p.set(12, 7, rgb('e8c23a')); p.set(11, 7, rgb('e8c23a'))
    p.outline()
    return p.im

def main():
    sheet = Image.new('RGBA', (S * 10, S), (0, 0, 0, 0))
    for i in range(10):
        sheet.paste(avatar(i), (i * S, 0))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT)
    print('wrote', os.path.normpath(OUT))
    if '--preview' in sys.argv:
        pv = Image.new('RGBA', (S * 10 * 6 + 54, S * 6 + 12), (40, 40, 34, 255))
        for i in range(10):
            pv.paste(avatar(i).resize((S * 6, S * 6), Image.NEAREST), (6 + i * (S * 6 + 4), 6), avatar(i).resize((S * 6, S * 6), Image.NEAREST))
        pv.save(sys.argv[sys.argv.index('--preview') + 1])

main()
