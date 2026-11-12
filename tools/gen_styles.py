#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the built-in art: the UI theme and the map styles.

    python3 tools/gen_styles.py [data_dir]          (needs Pillow)

  data/themes/default/   HUD, selection-screen pictures, icons, fallback tanks
  data/styles/classic/   re-imagined 1987 look: sand, asphalt, bush clumps, bunkers
  data/styles/desert/    red dunes, dirt tracks, scrub, adobe, sandstone
  data/styles/jungle/    grass, mud tracks, rivers, dense canopy, tin roofs, sandbags
  data/styles/winter/    snow, ploughed roads, ice, pines, snowy roofs
  data/styles/volcanic/  basalt, ash tracks, lava, charred scrub, bunkers
  data/styles/wasteland/ rust dirt, broken asphalt, toxic pools, dead scrub, rusty tin
  data/styles/moon/      grey dust, lit lanes, crater ice, crystals, domes
  data/styles/swamp/     mud, mud tracks, murky water, dark canopy, wooden shacks

Everything is drawn by this script (original art, MIT). The classic style
follows the look of the Amiga release without copying any of its pixels.
Colours are Amiga 12-bit values so the AmigaOS backend can use the sheets.
Sheet layouts: docs/THEMES.md.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

TILES = ["sand", "road", "water", "rock", "tree", "stump",
         "wall", "rubble", "building", "ruins", "floor"]
AUTOTILE = {"road", "water", "wall", "building", "tree", "ruins"}
T = 16
N, E, S, W = 1, 2, 4, 8


def c12(r, g, b):
    return (r * 17, g * 17, b * 17)


BLACK = c12(0, 0, 0)

# ---- style definitions ----------------------------------------------------------------

STYLES = {
    "classic": dict(
        title="Remaster", biome="classic",
        ground=[c12(15, 12, 8), c12(11, 9, 6), c12(10, 7, 3), c12(15, 13, 10)], ground_w=[78, 14, 4, 4],
        ground_kind="speckle", shadow=c12(10, 7, 3),
        road_kind="asphalt", road=c12(5, 5, 5), road_dot=c12(8, 8, 8), road_edge=c12(8, 8, 8),
        water=[c12(2, 5, 10), c12(3, 7, 12), c12(1, 3, 8)], foam=c12(10, 13, 15), water_kind="water",
        tree_kind="bush", tree=[c12(0, 3, 1), c12(0, 5, 0), c12(3, 10, 0), c12(8, 14, 0)],
        roof_kind="bunker", roof=[c12(5, 3, 1), c12(8, 5, 2), c12(10, 7, 3), c12(13, 8, 3)],
        wall_kind="barrier", wall=[c12(4, 4, 4), c12(7, 7, 7), c12(10, 10, 10)],
        rock=[c12(8, 5, 2), c12(11, 7, 3), c12(13, 9, 5)], rock_cap=None,
        debris=[c12(5, 5, 5), c12(8, 8, 8), c12(5, 3, 1)],
        ruins=[c12(0, 0, 0), c12(5, 3, 1), c12(11, 0, 0), c12(13, 8, 3)],
        stump=[c12(5, 3, 1), c12(8, 5, 2)],
        teams=[(c12(13, 8, 3), c12(8, 5, 2), c12(15, 11, 6)), (c12(4, 8, 4), c12(2, 5, 2), c12(7, 11, 6))],
        camo=[c12(0, 3, 2), c12(5, 3, 1), c12(0, 3, 2), c12(7, 4, 1)], camo_bg=BLACK,
        st=dict(conc=[c12(5, 5, 5), c12(8, 8, 8), c12(11, 11, 11), c12(13, 13, 13)],
                roof=[c12(5, 3, 1), c12(8, 5, 2), c12(11, 7, 3), c12(13, 9, 4)],
                metal=[c12(4, 4, 4), c12(7, 7, 7), c12(10, 10, 10), c12(13, 13, 13)],
                dome=[c12(1, 3, 3), c12(2, 6, 5), c12(4, 10, 7), c12(10, 15, 12)],
                bags=[c12(7, 6, 3), c12(10, 9, 5), c12(12, 11, 7)], accent=c12(13, 1, 0),
                window=c12(1, 2, 3), light=c12(15, 15, 6), roof_kind="tin", finish=None),
    ),
    # the original's own look, redrawn: peach sand with stipple, edged asphalt,
    # round bright bushes, bronze bunkers and concrete barriers - with softer,
    # more modern shading than 1987 could afford
    "retro": dict(
        title="Classic", biome="classic", faithful=True,
        ground=[(246, 206, 146), (212, 160, 98), (255, 232, 196), (166, 116, 66)], ground_w=[88, 4, 6, 2],
        ground_kind="stipple", shadow=(176, 130, 78),
        road_kind="edged", road=(82, 82, 86), road_dot=(122, 122, 128), road_edge=(170, 170, 174),
        water=[(30, 70, 150), (46, 96, 180), (20, 50, 120)], foam=(190, 220, 245), water_kind="water",
        tree_kind="bush", tree=[(18, 50, 10), (46, 126, 22), (108, 196, 34), (190, 242, 92)],
        roof_kind="bunker", roof=[(84, 52, 22), (134, 84, 36), (178, 118, 54), (214, 156, 80)],
        wall_kind="barrier", wall=[(70, 70, 74), (116, 116, 120), (172, 172, 176)],
        rock=[(110, 70, 34), (158, 104, 52), (200, 144, 84)], rock_cap=None,
        debris=[(86, 86, 90), (132, 132, 136), (96, 60, 28)],
        ruins=[(20, 16, 12), (96, 60, 28), (190, 30, 10), (214, 156, 80)],
        stump=[(96, 60, 28), (140, 92, 44)],
        teams=[((214, 140, 60), (140, 86, 36), (246, 190, 110)), ((70, 136, 66), (36, 84, 36), (124, 186, 106))],
        camo=[(14, 52, 34), (96, 60, 20), (14, 52, 34), (124, 72, 20)], camo_bg=BLACK,
        st=dict(conc=[(84, 84, 88), (126, 126, 130), (168, 168, 172), (208, 208, 212)],
                roof=[(84, 52, 22), (138, 88, 38), (186, 124, 56), (224, 164, 86)],
                metal=[(64, 64, 68), (108, 108, 112), (156, 156, 160), (204, 204, 208)],
                dome=[(18, 50, 50), (34, 98, 84), (70, 160, 116), (170, 240, 200)],
                bags=[(118, 100, 52), (166, 146, 86), (204, 186, 120)], accent=(220, 20, 0),
                window=(20, 34, 50), light=(250, 240, 110), roof_kind="tin", finish=None),
    ),
    # a future world: steel deck plating, glowing energy lanes, crystal growths,
    # plasma pools, alloy blast walls and dark-glass domes
    "scifi": dict(
        title="Sci-Fi", biome="scifi",
        ground=[(58, 66, 78), (50, 57, 68), (72, 82, 96), (40, 46, 56)], ground_w=[70, 15, 10, 5],
        ground_kind="plate", shadow=(26, 30, 38),
        road_kind="energy", road=(24, 28, 38), road_dot=(36, 44, 58), road_edge=(80, 230, 255),
        water=[(120, 30, 150), (170, 60, 200), (80, 20, 110)], foam=(240, 170, 255), water_kind="water",
        tree_kind="crystal", tree=[(20, 40, 70), (40, 120, 180), (90, 210, 255), (210, 250, 255)],
        roof_kind="bunker", roof=[(22, 34, 44), (36, 60, 76), (60, 96, 118), (110, 160, 190)],
        wall_kind="concrete", wall=[(34, 38, 48), (70, 78, 94), (120, 132, 150)],
        rock=[(40, 26, 56), (70, 48, 96), (110, 84, 150)], rock_cap=None,
        debris=[(36, 40, 50), (80, 88, 104), (40, 140, 180)],
        ruins=[(14, 14, 20), (40, 44, 56), (255, 90, 30), (90, 210, 255)],
        stump=[(30, 60, 90), (60, 140, 190)],
        teams=[((150, 190, 225), (80, 110, 150), (220, 240, 255)), ((190, 60, 60), (110, 30, 36), (240, 120, 110))],
        camo=[(16, 20, 40), (40, 30, 70), (16, 20, 40), (20, 60, 80)], camo_bg=BLACK,
        st=dict(conc=[(34, 38, 48), (64, 72, 88), (104, 116, 134), (160, 176, 196)],
                roof=[(22, 34, 44), (40, 66, 84), (66, 104, 128), (120, 170, 200)],
                metal=[(40, 44, 56), (76, 84, 100), (124, 136, 156), (190, 204, 224)],
                dome=[(10, 20, 40), (30, 60, 110), (70, 140, 220), (200, 240, 255)],
                bags=[(40, 50, 64), (70, 84, 104), (110, 130, 156)], accent=(255, 60, 200),
                window=(60, 220, 255), light=(120, 255, 240), roof_kind="tin", finish=None),
    ),
    "desert": dict(
        title="Desert", biome="desert",
        ground=[c12(14, 9, 5), c12(13, 8, 4), c12(15, 11, 7), c12(11, 6, 3)], ground_w=[60, 20, 12, 8],
        ground_kind="dunes", shadow=c12(10, 5, 2),
        road_kind="dirt", road=c12(12, 10, 7), road_dot=c12(10, 8, 5), road_edge=c12(9, 6, 3),
        water=[c12(1, 8, 9), c12(3, 11, 11), c12(0, 6, 7)], foam=c12(11, 15, 14), water_kind="water",
        tree_kind="scrub", tree=[c12(4, 4, 1), c12(7, 7, 2), c12(10, 10, 4), c12(13, 12, 6)],
        roof_kind="adobe", roof=[c12(9, 5, 3), c12(12, 8, 5), c12(14, 10, 7), c12(15, 13, 10)],
        wall_kind="sandstone", wall=[c12(8, 4, 2), c12(12, 7, 4), c12(14, 10, 6)],
        rock=[c12(7, 3, 1), c12(10, 5, 2), c12(13, 7, 4)], rock_cap=None,
        debris=[c12(8, 4, 2), c12(12, 8, 5), c12(5, 3, 1)],
        ruins=[c12(1, 0, 0), c12(6, 3, 1), c12(12, 3, 0), c12(15, 9, 3)],
        stump=[c12(6, 4, 2), c12(9, 6, 3)],
        teams=[(c12(13, 11, 6), c12(9, 7, 3), c12(15, 14, 9)), (c12(6, 7, 3), c12(4, 5, 2), c12(9, 10, 5))],
        camo=[c12(10, 7, 4), c12(6, 4, 2), c12(12, 10, 6), c12(8, 5, 3)], camo_bg=c12(4, 2, 1),
        st=dict(conc=[c12(8, 5, 3), c12(11, 8, 5), c12(13, 10, 7), c12(15, 12, 9)],
                roof=[c12(8, 4, 2), c12(11, 6, 3), c12(13, 8, 5), c12(15, 11, 7)],
                metal=[c12(6, 4, 3), c12(9, 7, 5), c12(12, 10, 7), c12(14, 12, 10)],
                dome=[c12(9, 7, 5), c12(12, 10, 8), c12(14, 13, 11), c12(15, 15, 14)],
                bags=[c12(9, 7, 4), c12(12, 10, 6), c12(14, 12, 8)], accent=c12(1, 8, 9),
                window=c12(3, 1, 1), light=c12(15, 15, 8), roof_kind="clay", finish="sand"),
    ),
    "jungle": dict(
        title="Jungle", biome="jungle",
        ground=[c12(3, 7, 2), c12(2, 5, 1), c12(5, 8, 2), c12(5, 4, 1)], ground_w=[58, 22, 12, 8],
        ground_kind="grass", shadow=c12(1, 3, 1),
        road_kind="mud", road=c12(6, 4, 2), road_dot=c12(4, 3, 1), road_edge=c12(3, 2, 1),
        water=[c12(3, 5, 4), c12(4, 7, 5), c12(2, 4, 3)], foam=c12(8, 10, 7), water_kind="river",
        tree_kind="canopy", tree=[c12(0, 2, 0), c12(1, 4, 1), c12(2, 6, 1), c12(5, 10, 2)],
        roof_kind="tin", roof=[c12(4, 2, 1), c12(8, 4, 2), c12(7, 7, 7), c12(10, 10, 10)],
        wall_kind="sandbag", wall=[c12(5, 4, 2), c12(9, 8, 5), c12(12, 11, 7)],
        rock=[c12(4, 4, 4), c12(7, 7, 6), c12(9, 9, 8)], rock_cap=c12(3, 7, 2),
        debris=[c12(4, 3, 1), c12(7, 6, 4), c12(2, 2, 1)],
        ruins=[c12(0, 0, 0), c12(3, 2, 1), c12(9, 2, 0), c12(12, 7, 2)],
        stump=[c12(4, 2, 1), c12(7, 4, 2)],
        teams=[(c12(5, 8, 3), c12(3, 5, 2), c12(8, 11, 5)), (c12(9, 6, 3), c12(6, 4, 2), c12(12, 9, 5))],
        camo=[c12(1, 4, 1), c12(4, 3, 1), c12(2, 6, 2), c12(0, 2, 0)], camo_bg=c12(0, 1, 0),
        st=dict(conc=[c12(4, 4, 3), c12(6, 6, 5), c12(8, 8, 7), c12(10, 10, 9)],
                roof=[c12(4, 2, 1), c12(7, 4, 2), c12(9, 6, 3), c12(11, 8, 5)],
                metal=[c12(4, 2, 1), c12(7, 4, 2), c12(8, 7, 6), c12(10, 10, 9)],
                dome=[c12(1, 3, 2), c12(3, 6, 4), c12(6, 10, 6), c12(11, 15, 11)],
                bags=[c12(6, 5, 2), c12(8, 7, 4), c12(10, 9, 5)], accent=c12(12, 3, 0),
                window=c12(0, 1, 0), light=c12(14, 14, 6), roof_kind="wood", finish="moss"),
    ),
    "winter": dict(
        title="Winter", biome="winter",
        ground=[c12(14, 14, 15), c12(12, 13, 15), c12(15, 15, 15), c12(10, 11, 14)], ground_w=[62, 22, 10, 6],
        ground_kind="snow", shadow=c12(9, 10, 13),
        road_kind="plowed", road=c12(6, 6, 7), road_dot=c12(10, 10, 12), road_edge=c12(15, 15, 15),
        water=[c12(1, 3, 7), c12(2, 5, 9), c12(1, 2, 5)], foam=c12(13, 15, 15), water_kind="ice",
        tree_kind="pine", tree=[c12(0, 2, 2), c12(1, 4, 3), c12(2, 6, 4), c12(15, 15, 15)],
        roof_kind="snow", roof=[c12(6, 6, 8), c12(10, 11, 13), c12(13, 14, 15), c12(15, 15, 15)],
        wall_kind="snowcap", wall=[c12(4, 4, 5), c12(8, 8, 9), c12(15, 15, 15)],
        rock=[c12(4, 4, 5), c12(7, 7, 8), c12(10, 10, 11)], rock_cap=c12(15, 15, 15),
        debris=[c12(5, 5, 6), c12(8, 8, 9), c12(4, 3, 2)],
        ruins=[c12(1, 1, 1), c12(5, 5, 6), c12(10, 2, 0), c12(13, 13, 14)],
        stump=[c12(4, 3, 2), c12(7, 5, 3)],
        teams=[(c12(13, 13, 13), c12(8, 8, 9), c12(15, 15, 15)), (c12(5, 7, 5), c12(3, 4, 3), c12(8, 10, 8))],
        camo=[c12(13, 13, 14), c12(8, 9, 10), c12(15, 15, 15), c12(5, 6, 7)], camo_bg=c12(10, 11, 12),
        st=dict(conc=[c12(5, 5, 6), c12(8, 8, 9), c12(11, 11, 12), c12(13, 13, 14)],
                roof=[c12(8, 9, 11), c12(11, 12, 14), c12(13, 14, 15), c12(15, 15, 15)],
                metal=[c12(4, 4, 5), c12(7, 7, 8), c12(10, 10, 11), c12(13, 13, 14)],
                dome=[c12(7, 8, 10), c12(10, 11, 13), c12(13, 14, 15), c12(15, 15, 15)],
                bags=[c12(8, 8, 8), c12(11, 11, 11), c12(14, 14, 14)], accent=c12(13, 2, 0),
                window=c12(1, 2, 4), light=c12(15, 15, 9), roof_kind="snow", finish="snow"),
    ),
    "volcanic": dict(
        title="Volcano", biome="volcanic",
        ground=[c12(3, 3, 3), c12(2, 2, 2), c12(4, 4, 4), c12(5, 3, 2)], ground_w=[56, 24, 12, 8],
        ground_kind="stipple", shadow=c12(1, 1, 1),
        road_kind="dirt", road=c12(5, 4, 4), road_dot=c12(4, 3, 3), road_edge=c12(2, 2, 2),
        water=[c12(14, 4, 0), c12(15, 8, 1), c12(11, 2, 0)], foam=c12(15, 13, 4), water_kind="river",
        tree_kind="scrub", tree=[c12(1, 1, 1), c12(2, 2, 2), c12(4, 3, 2), c12(8, 3, 1)],
        roof_kind="bunker", roof=[c12(2, 2, 2), c12(4, 4, 4), c12(6, 6, 6), c12(9, 5, 2)],
        wall_kind="barrier", wall=[c12(2, 2, 2), c12(5, 5, 5), c12(9, 4, 1)],
        rock=[c12(2, 2, 2), c12(4, 4, 4), c12(6, 5, 5)], rock_cap=c12(10, 4, 1),
        debris=[c12(3, 3, 3), c12(6, 5, 4), c12(10, 3, 0)],
        ruins=[c12(0, 0, 0), c12(3, 3, 3), c12(12, 4, 0), c12(15, 9, 2)],
        stump=[c12(2, 1, 1), c12(5, 2, 1)],
        teams=[(c12(8, 8, 8), c12(5, 5, 5), c12(11, 11, 11)), (c12(9, 4, 2), c12(6, 2, 1), c12(12, 6, 3))],
        camo=[c12(3, 3, 3), c12(2, 2, 2), c12(5, 4, 4), c12(1, 1, 1)], camo_bg=c12(1, 1, 1),
        st=dict(conc=[c12(2, 2, 2), c12(4, 4, 4), c12(6, 6, 6), c12(8, 8, 8)],
                roof=[c12(2, 2, 2), c12(4, 3, 3), c12(6, 5, 4), c12(8, 7, 6)],
                metal=[c12(2, 2, 3), c12(4, 4, 5), c12(6, 6, 7), c12(9, 9, 10)],
                dome=[c12(3, 1, 0), c12(7, 2, 0), c12(11, 5, 1), c12(15, 10, 3)],
                bags=[c12(3, 3, 3), c12(5, 5, 5), c12(7, 7, 7)], accent=c12(15, 6, 0),
                window=c12(10, 3, 0), light=c12(15, 11, 3), roof_kind="bunker", finish="sand"),
    ),
    "wasteland": dict(
        title="Wasteland", biome="wasteland",
        ground=[c12(8, 5, 3), c12(7, 4, 2), c12(9, 7, 4), c12(6, 4, 3)], ground_w=[55, 25, 12, 8],
        ground_kind="speckle", shadow=c12(4, 2, 1),
        road_kind="asphalt", road=c12(4, 4, 4), road_dot=c12(6, 6, 5), road_edge=c12(7, 5, 3),
        water=[c12(5, 10, 2), c12(8, 13, 3), c12(3, 7, 1)], foam=c12(11, 15, 5), water_kind="river",
        tree_kind="scrub", tree=[c12(3, 2, 1), c12(5, 3, 1), c12(7, 5, 2), c12(9, 7, 3)],
        roof_kind="tin", roof=[c12(5, 2, 1), c12(9, 4, 1), c12(8, 6, 4), c12(11, 8, 5)],
        wall_kind="sandbag", wall=[c12(6, 4, 2), c12(9, 6, 3), c12(11, 8, 5)],
        rock=[c12(5, 3, 2), c12(7, 5, 3), c12(9, 7, 5)], rock_cap=c12(10, 6, 3),
        debris=[c12(5, 4, 3), c12(8, 6, 4), c12(3, 2, 1)],
        ruins=[c12(1, 1, 1), c12(5, 3, 2), c12(10, 3, 0), c12(12, 8, 4)],
        stump=[c12(4, 2, 1), c12(7, 4, 2)],
        teams=[(c12(9, 8, 6), c12(6, 5, 4), c12(12, 11, 9)), (c12(6, 7, 3), c12(4, 5, 2), c12(9, 10, 5))],
        camo=[c12(7, 4, 2), c12(5, 3, 2), c12(9, 6, 3), c12(3, 2, 1)], camo_bg=c12(2, 1, 0),
        st=dict(conc=[c12(5, 4, 3), c12(7, 6, 5), c12(9, 8, 6), c12(11, 10, 8)],
                roof=[c12(5, 2, 1), c12(8, 4, 2), c12(10, 6, 3), c12(12, 8, 5)],
                metal=[c12(5, 3, 2), c12(8, 4, 2), c12(9, 7, 5), c12(11, 9, 7)],
                dome=[c12(3, 5, 1), c12(5, 9, 2), c12(8, 13, 3), c12(12, 15, 7)],
                bags=[c12(7, 5, 3), c12(9, 7, 4), c12(11, 9, 6)], accent=c12(13, 4, 0),
                window=c12(2, 3, 1), light=c12(12, 15, 5), roof_kind="tin", finish="sand"),
    ),
    "moon": dict(
        title="Moon base", biome="moon",
        ground=[c12(9, 9, 9), c12(8, 8, 8), c12(11, 11, 11), c12(6, 6, 6)], ground_w=[58, 24, 10, 8],
        ground_kind="speckle", shadow=c12(4, 4, 5),
        road_kind="edged", road=c12(4, 4, 5), road_dot=c12(6, 6, 7), road_edge=c12(13, 13, 14),
        water=[c12(7, 9, 11), c12(9, 11, 13), c12(5, 7, 9)], foam=c12(14, 15, 15), water_kind="ice",
        tree_kind="crystal", tree=[c12(6, 7, 9), c12(9, 10, 12), c12(12, 13, 15), c12(15, 15, 15)],
        roof_kind="bunker", roof=[c12(7, 7, 8), c12(10, 10, 11), c12(12, 12, 13), c12(14, 14, 15)],
        wall_kind="barrier", wall=[c12(6, 6, 7), c12(10, 10, 11), c12(14, 14, 15)],
        rock=[c12(5, 5, 5), c12(8, 8, 8), c12(11, 11, 11)], rock_cap=c12(13, 13, 13),
        debris=[c12(6, 6, 6), c12(9, 9, 9), c12(4, 4, 4)],
        ruins=[c12(2, 2, 2), c12(6, 6, 7), c12(10, 3, 0), c12(12, 12, 13)],
        stump=[c12(6, 6, 7), c12(9, 9, 10)],
        teams=[(c12(13, 13, 13), c12(9, 9, 9), c12(15, 15, 15)), (c12(12, 10, 4), c12(9, 7, 2), c12(15, 13, 7))],
        camo=[c12(10, 10, 10), c12(7, 7, 7), c12(12, 12, 12), c12(5, 5, 5)], camo_bg=c12(4, 4, 4),
        st=dict(conc=[c12(6, 6, 7), c12(9, 9, 10), c12(11, 11, 12), c12(13, 13, 14)],
                roof=[c12(8, 8, 9), c12(11, 11, 12), c12(13, 13, 14), c12(15, 15, 15)],
                metal=[c12(5, 5, 6), c12(8, 8, 9), c12(11, 11, 12), c12(14, 14, 15)],
                dome=[c12(8, 9, 11), c12(11, 12, 14), c12(13, 14, 15), c12(15, 15, 15)],
                bags=[c12(8, 8, 8), c12(10, 10, 10), c12(12, 12, 12)], accent=c12(2, 10, 15),
                window=c12(1, 6, 10), light=c12(13, 15, 15), roof_kind="bunker", finish="sand"),
    ),
    "swamp": dict(
        title="Swamp", biome="swamp",
        ground=[c12(4, 5, 2), c12(3, 4, 2), c12(5, 5, 3), c12(4, 3, 2)], ground_w=[54, 26, 12, 8],
        ground_kind="grass", shadow=c12(2, 2, 1),
        road_kind="mud", road=c12(5, 4, 2), road_dot=c12(3, 3, 1), road_edge=c12(2, 2, 1),
        water=[c12(2, 4, 3), c12(3, 5, 3), c12(1, 3, 2)], foam=c12(6, 8, 5), water_kind="river",
        tree_kind="canopy", tree=[c12(1, 2, 1), c12(2, 3, 2), c12(3, 4, 2), c12(5, 6, 3)],
        roof_kind="wood", roof=[c12(4, 3, 1), c12(6, 4, 2), c12(5, 5, 4), c12(7, 7, 6)],
        wall_kind="sandbag", wall=[c12(4, 3, 2), c12(6, 5, 3), c12(8, 7, 4)],
        rock=[c12(3, 3, 3), c12(5, 5, 4), c12(7, 7, 6)], rock_cap=c12(4, 6, 3),
        debris=[c12(3, 3, 1), c12(5, 4, 2), c12(2, 2, 1)],
        ruins=[c12(0, 0, 0), c12(3, 2, 1), c12(8, 3, 0), c12(10, 7, 3)],
        stump=[c12(3, 2, 1), c12(5, 3, 2)],
        teams=[(c12(6, 7, 4), c12(4, 5, 2), c12(9, 10, 6)), (c12(8, 5, 3), c12(5, 3, 2), c12(11, 8, 5))],
        camo=[c12(2, 3, 1), c12(4, 3, 2), c12(3, 4, 2), c12(1, 2, 1)], camo_bg=c12(1, 1, 0),
        st=dict(conc=[c12(3, 3, 3), c12(5, 5, 4), c12(7, 7, 6), c12(9, 9, 8)],
                roof=[c12(4, 3, 1), c12(6, 4, 2), c12(8, 6, 3), c12(10, 8, 5)],
                metal=[c12(3, 3, 2), c12(5, 4, 3), c12(7, 6, 5), c12(9, 8, 7)],
                dome=[c12(1, 3, 2), c12(3, 5, 3), c12(5, 8, 4), c12(9, 12, 7)],
                bags=[c12(5, 4, 2), c12(7, 6, 3), c12(9, 8, 5)], accent=c12(11, 3, 0),
                window=c12(0, 1, 0), light=c12(12, 13, 5), roof_kind="wood", finish="moss"),
    ),
}


# ---- helpers ----------------------------------------------------------------------------

def pick(rng, cols, weights):
    return rng.choices(cols, weights)[0]


def new_tile():
    return Image.new("RGBA", (T, T))


def open_sides(mask):
    return {"n": not mask & N, "e": not mask & E, "s": not mask & S, "w": not mask & W}


def edge_dist(x, y, o):
    d = 99
    if o["n"]: d = min(d, y)
    if o["s"]: d = min(d, T - 1 - y)
    if o["w"]: d = min(d, x)
    if o["e"]: d = min(d, T - 1 - x)
    return d


def ground_px(P, rng, x, y):
    c = pick(rng, P["ground"], P["ground_w"])
    k = P["ground_kind"]
    if k == "dunes":                     # ripples periodic in 16 px: tiles join seamlessly
        v = (y + 2.2 * math.sin(2 * math.pi * x / 16)) % 8
        if v < 1.0:
            c = P["ground"][3]
        elif v < 2.0:
            c = P["ground"][2]
    elif k == "grass":
        if rng.random() < 0.10:
            c = P["ground"][2]           # blade highlights
    elif k == "snow":
        if rng.random() < 0.015:
            c = (255, 255, 255)
    elif k == "plate":                    # steel deck: a panel per tile, seams and rivets
        tx, ty = x % 16, y % 16
        if tx == 0 or ty == 0:
            c = P["ground"][3]
        elif tx == 15 or ty == 15:
            c = P["ground"][2]
        elif (tx, ty) in ((2, 2), (13, 2), (2, 13), (13, 13)):
            c = P["ground"][2]
        elif rng.random() < 0.5:
            c = P["ground"][0]
    elif k == "stipple":                  # the original's dithered sand, with a soft tile-seamless swell
        swell = math.sin(2 * math.pi * x / 16) * math.cos(2 * math.pi * y / 16)
        r = rng.random()
        if r < 0.20 + 0.07 * swell:
            c = P["ground"][1]
        elif r > 0.972:
            c = P["ground"][3]
        elif r > 0.94:
            c = P["ground"][2]
    return c


def fill_ground(img, rng, P, detail=True):
    px = img.load()
    for y in range(T):
        for x in range(T):
            px[x, y] = ground_px(P, rng, x, y)
    if not detail:
        return
    k = P["ground_kind"]
    if k == "grass":                     # blades, and now and then a flower or leaf
        for _ in range(7):
            x, y = rng.randrange(T), rng.randrange(T - 2)
            px[x, y] = px[x, y + 1] = P["shadow"]
            if y > 0: px[x, y - 1] = P["ground"][2]
        if rng.random() < 0.35:
            x, y = rng.randrange(1, T - 1), rng.randrange(1, T - 1)
            px[x, y] = rng.choice([c12(15, 14, 4), c12(14, 6, 9), c12(8, 5, 2)])
        return
    for _ in range(rng.choice((0, 1, 1, 2))):   # pebbles: lit top-left, shadow bottom-right
        x, y = rng.randrange(1, T - 3), rng.randrange(1, T - 3)
        w = rng.choice((1, 2))
        base = P["ground"][3] if k != "snow" else c12(11, 12, 14)
        for yy in range(2):
            for xx in range(w + 1):
                px[x + xx, y + yy] = base
        px[x, y] = P["ground"][2] if k != "snow" else c12(15, 15, 15)
        for xx in range(w + 2):
            px[x + xx, y + 2] = P["shadow"]
        px[x + w + 1, y + 1] = P["shadow"]


# ---- tiles ----------------------------------------------------------------------------

def tile_road(mask, rng, P):
    img, o = new_tile(), open_sides(mask)
    fill_ground(img, rng, P)
    px = img.load()
    kind = P["road_kind"]
    horiz = bool(mask & (E | W)) and not mask & (N | S)
    vert = bool(mask & (N | S)) and not mask & (E | W)
    for y in range(T):
        for x in range(T):
            d = edge_dist(x, y, o)
            c = P["road_dot"] if rng.random() < 0.03 else P["road"]
            if kind == "asphalt":
                if d == 0: c = P["road_edge"]
                elif d == 1: c = BLACK
            elif kind == "edged":                  # black outline, a light kerb line, then a soft
                if d == 0: c = BLACK               # inner shadow: the original's road, rounder
                elif d == 1: c = P["road_edge"]
                elif d == 2: c = tuple(max(0, v - 18) for v in P["road"])
                elif d == 3 and rng.random() < 0.5: c = tuple(max(0, v - 9) for v in P["road"])
            elif kind == "energy":                 # a dark lane with glowing rails and a dashed centre
                if d == 0: c = P["road_edge"]
                elif d == 1: c = tuple(v // 3 for v in P["road_edge"])
                elif (horiz and y in (7, 8) and x % 8 < 4) or (vert and x in (7, 8) and y % 8 < 4):
                    c = tuple(v // 2 for v in P["road_edge"])
            elif kind == "dirt":
                if (horiz and y in (4, 11)) or (vert and x in (4, 11)):
                    c = P["road_dot"]                       # wheel ruts
                if d == 0:
                    c = px[x, y] if rng.random() < 0.6 else P["road_edge"]
                elif d == 1 and rng.random() < 0.4:
                    c = P["road_edge"]
            elif kind == "mud":
                if rng.random() < 0.06: c = P["road_edge"]  # puddles
                if (horiz and y in (5, 10)) or (vert and x in (5, 10)):
                    c = P["road_edge"]
                if d == 0: c = P["shadow"]
                elif d == 1 and rng.random() < 0.5: c = P["road_edge"]
            elif kind == "plowed":
                if d <= 1:
                    c = P["road_edge"]                      # snow berm
                elif d == 2:
                    c = P["shadow"]
            px[x, y] = c
    if kind in ("asphalt", "plowed", "edged"):  # wear: a patch and the odd crack
        if rng.random() < 0.3:
            x0, y0 = rng.randrange(3, 9), rng.randrange(3, 9)
            for y in range(y0, y0 + rng.randrange(3, 6)):
                for x in range(x0, x0 + rng.randrange(3, 7)):
                    if px[x, y] == P["road"]:
                        px[x, y] = P["road_dot"] if kind == "plowed" else \
                            (tuple(max(0, v - 10) for v in P["road"]) if kind == "edged" else c12(6, 6, 6))
        if rng.random() < 0.45:
            x, y = rng.randrange(3, 13), rng.randrange(3, 13)
            for _ in range(rng.randrange(3, 7)):
                if 2 < x < 13 and 2 < y < 13:
                    px[x, y] = (44, 44, 46) if kind == "edged" else c12(3, 3, 3)
                x += rng.choice((-1, 0, 1))
                y += 1
    return img


def tile_water(mask, rng, P):
    img, o = new_tile(), open_sides(mask)
    px = img.load()
    kind = P["water_kind"]
    for y in range(T):
        for x in range(T):
            c = pick(rng, P["water"], [70, 15, 15])
            if kind != "ice" and (x + 2 * y) % 9 == 0 and y % 4 == 1:
                c = P["foam"]
            if kind == "ice" and (x * 3 + y * 5) % 23 == 0:
                c = P["foam"]                               # floating floes
            d = edge_dist(x, y, o)
            if d == 0:
                c = ground_px(P, rng, x, y)
            elif d == 1:
                c = P["foam"] if kind == "ice" else BLACK
            elif d == 2 and rng.random() < (0.8 if kind == "ice" else 0.4):
                c = P["foam"]
            px[x, y] = c
    return img


def tile_barrier(mask, rng, P):
    """concrete barrier slab like the original's; the game draws the square
    bollards on the sand beside it (style.txt: bollards=1)"""
    img, o = new_tile(), open_sides(mask)
    px = img.load()
    lo, mid, hi = P["wall"]
    for y in range(T):
        for x in range(T):
            c = mid if rng.random() > 0.04 else lo
            if rng.random() < 0.015: c = hi
            if o["n"] and y <= 1: c = hi if y == 1 else BLACK
            if o["w"] and x <= 1: c = hi if x == 1 else BLACK
            if o["s"] and y >= T - 2: c = lo if y == T - 2 else BLACK
            if o["e"] and x >= T - 2: c = lo if x == T - 2 else BLACK
            px[x, y] = c
    return img


def tile_wall(mask, rng, P):
    if P["wall_kind"] == "barrier":
        return tile_barrier(mask, rng, P)
    img, o = new_tile(), open_sides(mask)
    px = img.load()
    lo, mid, hi = P["wall"]
    kind = P["wall_kind"]
    for y in range(T):
        for x in range(T):
            if kind == "sandbag":                           # rows of rounded bags
                row, off = y // 5, (4 if (y // 5) % 2 else 0)
                bx, by = (x + off) % 8, y % 5
                edge = by == 4 or bx == 7
                c = lo if edge else (hi if by == 0 or bx == 0 else mid)
            else:
                off = 4 if (y // 8) % 2 else 0
                joint = y % 8 == 7 or (x + off) % 8 == 7
                c = lo if joint else (hi if y % 8 == 0 or (x + off) % 8 == 0 else mid)
                if not joint and (y % 8 == 6 or (x + off) % 8 == 6):
                    c = lo if kind != "sandstone" else mid      # brick bottom/right shadow
                if kind == "sandstone" and not joint and rng.random() < 0.15:
                    c = hi
            if not joint_ok(kind) and rng.random() < 0.04:
                c = lo
            d = edge_dist(x, y, o)
            if d == 0:
                c = BLACK
            elif d == 1:
                c = hi if ((o["n"] and y == 1) or (o["w"] and x == 1)) else lo
            if kind == "snowcap" and y <= 3 and d != 0 and (y <= 1 or rng.random() < 0.5):
                c = hi                                      # snow on top
            px[x, y] = c
    return img


def joint_ok(kind):
    return kind == "sandbag"


def tile_building(mask, rng, P):
    img, o = new_tile(), open_sides(mask)
    px = img.load()
    r0, r1, r2, r3 = P["roof"]
    kind = P["roof_kind"]
    for y in range(T):
        for x in range(T):
            if kind == "bunker":                            # panels with vents
                vent = (x % 8 in (2, 3, 4, 5)) and (y % 8 in (2, 5))
                c = r0 if vent else (r2 if y % 8 == 0 else r1)
            elif kind == "adobe":
                c = r2 if rng.random() > 0.08 else r1
                if (x % 16 in (5, 6)) and (y % 16 in (5, 6)):
                    c = r0                                  # roof hatch
            elif kind == "tin":                             # corrugated, rusty
                c = r3 if x % 3 == 0 else r2
                if rng.random() < 0.18:
                    c = r1 if rng.random() < 0.7 else r0
            else:                                           # snow on a roof
                c = r3 if rng.random() > 0.1 else r2
                if y % 16 in (7, 8):
                    c = r2                                  # ridge
            d = edge_dist(x, y, o)
            if d == 0:
                c = BLACK
            elif d == 1:
                hi = (o["n"] and y == 1) or (o["w"] and x == 1)
                c = r3 if hi else r0
            elif d == 2 and kind in ("bunker", "adobe"):
                c = r2 if ((o["n"] and y == 2) or (o["w"] and x == 2)) else r1
            px[x, y] = c
    return img


_LEAVES = {}


def leaf_texture(P, big):
    key = (P["tree"][2], big)
    if key in _LEAVES:
        return _LEAVES[key]
    rng = random.Random(4242 + big)
    img = Image.new("RGBA", (T, T), P["tree"][1])
    px = img.load()
    for _ in range(9 if big else 14):
        cx, cy = rng.uniform(0, 16), rng.uniform(0, 16)
        r = rng.uniform(2.6, 4.2) if big else rng.uniform(1.8, 3.2)
        for y in range(T):
            for x in range(T):
                dx = (x - cx + 8) % 16 - 8
                dy = (y - cy + 8) % 16 - 8
                d = math.hypot(dx, dy)
                if d > r:
                    continue
                if d > r - 0.8 and dx + dy > 0:
                    px[x, y] = P["tree"][0]
                elif dx + dy < -r * 0.5:
                    px[x, y] = P["tree"][3]
                elif big and abs(dx - dy) < 0.6:
                    px[x, y] = P["tree"][3]                 # frond rib
                else:
                    px[x, y] = P["tree"][2]
    _LEAVES[key] = px
    return px


def tile_tree(mask, rng, P):
    kind = P["tree_kind"]
    img = new_tile()
    fill_ground(img, rng, P)
    px = img.load()
    if kind == "pine":
        return tile_pine(img, rng, P)
    if kind == "crystal":
        return tile_crystal(img, rng, P)
    base_r = {"bush": 6.4, "scrub": 5.0, "canopy": 7.0}[kind]
    lim = {"bush": 6.6, "scrub": 5.2, "canopy": 7.4}[kind]
    wob = [rng.uniform(-1.4, 0.6) for _ in range(16)]
    if kind == "scrub":
        mask = 0 if rng.random() < 0.5 else mask           # scrub mostly stays in tufts

    def inside(x, y, shrink=0.0):
        cx = cy = 7.5
        a = math.atan2(y - cy, x - cx)
        r = base_r + wob[int((a + math.pi) / (2 * math.pi) * 16) % 16] - shrink
        if math.hypot(x - cx, y - cy) <= r:
            return True
        l = lim - shrink
        if mask & N and y <= cy and abs(x - cx) <= l: return True
        if mask & S and y >= cy and abs(x - cx) <= l: return True
        if mask & W and x <= cx and abs(y - cy) <= l: return True
        if mask & E and x >= cx and abs(y - cy) <= l: return True
        if mask & N and mask & E and x >= cx and y <= cy: return True
        if mask & N and mask & W and x <= cx and y <= cy: return True
        if mask & S and mask & E and x >= cx and y >= cy: return True
        if mask & S and mask & W and x <= cx and y >= cy: return True
        return False

    for y in range(T):
        for x in range(T):
            if inside(x - 2, y - 2) and not inside(x, y):
                px[x, y] = P["shadow"]
    leaves = leaf_texture(P, kind == "canopy")
    for y in range(T):
        for x in range(T):
            if not inside(x, y):
                continue
            if not inside(x, y, 1.0) or not inside(x + 1, y + 1, 0.5):
                px[x, y] = P["tree"][0]
                continue
            c = leaves[x, y]
            if kind == "scrub" and rng.random() < 0.25:
                c = ground_px(P, rng, x, y)                 # sparse, dry
            if not inside(x - 2, y - 2, 1.0) and c != P["tree"][0]:
                c = P["tree"][3]
            px[x, y] = c
    return img


def tile_crystal(img, rng, P):
    """a cluster of glowing crystal shards: a light face and a dark face each"""
    d = ImageDraw.Draw(img)
    dark, mid, lite, glow = P["tree"]
    shards = [(7.5 + rng.uniform(-3, 3), 7.5 + rng.uniform(-3, 3), rng.uniform(2.2, 4.2)) for _ in range(rng.randrange(3, 6))]
    for cx, cy, r in shards:                                   # shadows first
        d.polygon([(cx + 1.5, cy - r + 1.5), (cx + r * 0.6 + 1.5, cy + 1.5), (cx + 1.5, cy + r * 0.5 + 1.5),
                   (cx - r * 0.6 + 1.5, cy + 1.5)], fill=P["shadow"])
    for cx, cy, r in sorted(shards, key=lambda q: q[1]):
        top, right, bot, left = (cx, cy - r), (cx + r * 0.6, cy), (cx, cy + r * 0.5), (cx - r * 0.6, cy)
        d.polygon([top, right, bot, left], fill=dark)
        d.polygon([top, (cx, cy), bot, left], fill=lite)      # the sunlit face
        d.polygon([top, right, bot, (cx, cy)], fill=mid)
        d.point((round(cx - 1), round(cy - r + 2)), fill=glow)
    return img


def tile_pine(img, rng, P):
    """top-down pine: 8 spiky boughs, snow on the sunny side"""
    px = img.load()
    dark, mid, lite, snow = P["tree"]

    def r_at(a):
        return 4.6 + 2.4 * abs(math.cos(4 * a))

    def inside(x, y, shrink=0.0):
        dx, dy = x - 7.5, y - 7.5
        return math.hypot(dx, dy) <= r_at(math.atan2(dy, dx)) - shrink

    for y in range(T):
        for x in range(T):
            if inside(x - 2, y - 2) and not inside(x, y):
                px[x, y] = P["shadow"]
    for y in range(T):
        for x in range(T):
            if not inside(x, y):
                continue
            dx, dy = x - 7.5, y - 7.5
            d = math.hypot(dx, dy)
            if not inside(x, y, 1.0):
                c = dark
            elif d < 1.5:
                c = dark
            else:
                lit = (dx + dy) < -1.5
                c = mid if not lit else lite
                if lit and rng.random() < 0.55:
                    c = snow
                elif rng.random() < 0.12:
                    c = dark
            px[x, y] = c
    return img


def blob(img, rng, cx, cy, rad, cols, outline, cap=None):
    px = img.load()
    for y in range(T):
        for x in range(T):
            d = math.hypot(x - cx, y - cy)
            if d <= rad:
                if d > rad - 1:
                    px[x, y] = outline
                else:
                    shade = -(x - cx) - (y - cy)
                    i = 2 if shade > rad * 0.45 else 0 if shade < -rad * 0.35 else 1
                    if rng.random() < 0.12:
                        i = max(0, min(2, i + rng.choice((-1, 1))))
                    c = cols[i]
                    if cap and shade > rad * 0.2 and rng.random() < 0.8:
                        c = cap
                    px[x, y] = c


def tile_rock(rng, P):
    img = new_tile()
    fill_ground(img, rng, P)
    px = img.load()
    for y in range(T):
        for x in range(T):
            if math.hypot(x - 9, y - 9.5) < 6.4:
                px[x, y] = P["shadow"]
    blob(img, rng, 7.0, 7.5, 6.8, P["rock"], BLACK, P["rock_cap"])
    return img


def tile_stump(rng, P):
    """a bush flattened under tank tracks: crushed leaves pressed into the ground"""
    img = new_tile()
    fill_ground(img, rng, P, detail=False)
    px = img.load()
    dark, mid, lite = P["tree"][0], P["tree"][1], P["tree"][2]
    for _ in range(16):                                  # flattened twigs and leaf mats
        x, y = rng.randrange(1, 14), rng.randrange(1, 15)
        ln = rng.randrange(2, 6)
        dx = rng.choice((1, 1, 0))
        for k in range(ln):
            xx, yy = x + k * dx, y + (k if not dx else 0) // 2
            if 0 <= xx < T and 0 <= yy < T:
                px[xx, yy] = rng.choice((dark, mid, mid, lite))
    for _ in range(10):
        x, y = rng.randrange(T), rng.randrange(T)
        px[x, y] = mid
    return img


def tile_rubble(rng, P):
    img = new_tile()
    fill_ground(img, rng, P)
    px = img.load()
    for _ in range(10):
        cx, cy, r = rng.uniform(1, 14), rng.uniform(1, 14), rng.uniform(0.8, 2.2)
        col = rng.choice(P["debris"])
        for y in range(T):
            for x in range(T):
                if abs(x - cx) + abs(y - cy) <= r:
                    px[x, y] = col
                elif abs(x - cx - 1) + abs(y - cy - 1) <= r and px[x, y] in P["ground"]:
                    px[x, y] = P["shadow"]
    return img


def tile_ruins(mask, rng, P):
    """what is left of a building: scorched floor, broken wall stubs and
    debris; open sides fray out into the surrounding ground"""
    img, o = new_tile(), open_sides(mask)
    fill_ground(img, rng, P, detail=False)
    px = img.load()
    k = P["ruins"]
    deb = P["debris"]
    for y in range(T):
        for x in range(T):
            d = edge_dist(x, y, o)
            if d <= 1 and rng.random() < 0.6:
                continue                                  # ragged edge, ground shows
            r = rng.random()
            if r < 0.05:
                c = k[3]                                  # glowing embers
            elif r < 0.10:
                c = k[2]
            elif r < 0.30:
                c = deb[1]
            else:
                c = k[0] if (x + y) % 2 == 0 else k[1]   # scorched floor
            px[x, y] = c
    for _ in range(3):                                    # concrete chunks with light and shadow
        x, y = rng.randrange(2, 12), rng.randrange(2, 12)
        w, h = rng.randrange(2, 4), rng.randrange(1, 3)
        for yy in range(h):
            for xx in range(w):
                px[x + xx, y + yy] = deb[2] if yy == 0 else deb[1]
        for xx in range(w + 1):
            px[x + xx, y + h] = BLACK
    if o["n"]:                                            # stub of the old outer wall
        for x in range(2, 14):
            if rng.random() < 0.55:
                px[x, 2] = deb[2]
                px[x, 3] = deb[0]
    if o["w"]:
        for y in range(2, 14):
            if rng.random() < 0.55:
                px[2, y] = deb[2]
                px[3, y] = deb[0]
    if rng.random() < 0.4:                                # twisted metal
        x, y = rng.randrange(4, 11), rng.randrange(4, 11)
        for i in range(4):
            px[x + i, y + (i % 2)] = c12(7, 7, 8)
    return img


def tile_floor(rng, P):
    """concrete slabs, used under garages"""
    img = new_tile()
    px = img.load()
    lo, mid, hi, top = P["st"]["conc"]
    for y in range(T):
        for x in range(T):
            c = mid if rng.random() > 0.08 else lo
            if x % 8 == 0 or y % 8 == 0:
                c = lo
            elif x % 8 == 1 or y % 8 == 1:
                c = hi
            px[x, y] = c
    return img


def build_tiles(P):
    sheet = Image.new("RGBA", (T * 16, T * len(TILES)))
    for row, name in enumerate(TILES):
        for col in range(16):
            rng = random.Random(row * 1000 + col)
            if name in AUTOTILE:
                fn = {"road": tile_road, "water": tile_water, "wall": tile_wall,
                      "building": tile_building, "tree": tile_tree, "ruins": tile_ruins}[name]
                img = fn(col, rng, P)
            elif name == "sand":
                img = new_tile()
                fill_ground(img, rng, P)
            else:
                img = {"rock": tile_rock, "stump": tile_stump, "rubble": tile_rubble,
                       "floor": tile_floor}[name](rng, P)
            sheet.paste(img, (col * T, row * T))
    return sheet


# ---- structures --------------------------------------------------------------------------
# One slot of 64x48 per type and state: rows tower, bunker, depot, barracks,
# dome, hq; column 0 intact, column 1 damaged. Each picture covers its
# footprint (w*16 x h*16) from the slot's top-left corner.
STRUCTS = [("tower", 1, 1), ("bunker", 2, 2), ("depot", 2, 2), ("barracks", 3, 2),
           ("dome", 3, 3), ("hq", 4, 3), ("garage", 3, 3), ("firstaid", 2, 2), ("powcamp", 3, 3),
           ("radar", 2, 2), ("power", 3, 2), ("warehouse", 3, 2),
           ("mg", 1, 1), ("cannon", 1, 1), ("rocket", 2, 2), ("heavy", 2, 2), ("flag", 2, 2),
           ("helipad", 3, 3), ("twin", 1, 1), ("mortar", 2, 2)]
# must match StructType in src/core/world.h (row = type - 1)
SLOT_W, SLOT_H = 64, 48


def shade_rect(d, box, cols, bevel=1):
    """filled box lit from the top-left: cols = [dark, mid, light, highlight]"""
    x0, y0, x1, y1 = box
    d.rectangle(box, fill=cols[1])
    for i in range(bevel):
        d.line([(x0 + i, y0 + i), (x1 - i, y0 + i)], fill=cols[2])
        d.line([(x0 + i, y0 + i), (x0 + i, y1 - i)], fill=cols[2])
        d.line([(x0 + i, y1 - i), (x1 - i, y1 - i)], fill=cols[0])
        d.line([(x1 - i, y0 + i), (x1 - i, y1 - i)], fill=cols[0])
    d.point((x0, y0), fill=cols[3])


def shade_disc(img, cx, cy, r, cols, rng=None, noise=0.0):
    """sphere-like shading: highlight top-left, dark rim bottom-right"""
    px = img.load()
    for y in range(int(cy - r - 1), int(cy + r + 2)):
        for x in range(int(cx - r - 1), int(cx + r + 2)):
            dx, dy = x - cx, y - cy
            dd = math.hypot(dx, dy)
            if dd > r or not (0 <= x < img.size[0] and 0 <= y < img.size[1]):
                continue
            light = -(dx + dy) / (2 * r) + 0.5 * (1 - dd / r)
            if noise and rng.random() < noise:
                light += rng.uniform(-0.2, 0.2)
            i = 3 if light > 0.55 else 2 if light > 0.15 else 1 if light > -0.3 else 0
            px[x, y] = cols[i]


def draw_tower(img, d, S, rng):
    m, r = S["metal"], S["roof"]
    for i in range(4):                               # long shadow of the legs
        d.point((12 + i, 13 + i), fill=(0, 0, 0, 90))
        d.point((13 + i, 12 + i), fill=(0, 0, 0, 60))
    shade_rect(d, (1, 1, 13, 13), m)                 # platform
    for x in range(2, 13, 2):                        # railing posts
        d.point((x, 2), fill=m[3])
        d.point((2, x), fill=m[3])
        d.point((x, 12), fill=m[0])
        d.point((12, x), fill=m[0])
    for y in range(4, 11):                           # pyramid roof, four lit faces
        for x in range(4, 11):
            dx, dy = x - 7, y - 7
            if abs(dx) > abs(dy):
                c = r[1] if dx > 0 else r[2]
            else:
                c = r[0] if dy > 0 else r[3]
            d.point((x, y), fill=c)
    d.point((7, 7), fill=m[3])
    d.rectangle((2, 2, 3, 3), fill=S["light"])       # searchlight
    d.point((12, 12), fill=S["accent"])


def draw_bunker(img, d, S, rng):
    c, b = S["conc"], S["bags"]
    for i in range(5, 28, 4):                        # sandbag wall along north and west
        d.ellipse((i, 1, i + 4, 4), fill=b[1], outline=b[0])
        d.ellipse((1, i, 4, i + 4), fill=b[1], outline=b[0])
        d.point((i + 1, 2), fill=b[2])
        d.point((2, i + 1), fill=b[2])
    d.rounded_rectangle((5, 5, 28, 28), radius=5, fill=c[1])
    d.rounded_rectangle((5, 5, 28, 28), radius=5, outline=c[0])
    d.arc((5, 5, 15, 15), 180, 270, fill=c[3])
    d.line([(10, 5), (23, 5)], fill=c[2])
    d.line([(5, 10), (5, 23)], fill=c[2])
    shade_rect(d, (10, 10, 23, 23), c, bevel=2)      # raised roof slab
    d.rectangle((9, 25, 24, 26), fill=S["window"])   # firing slit
    d.line([(9, 24), (24, 24)], fill=c[0])
    d.ellipse((14, 13, 19, 18), fill=c[0])           # hatch
    d.ellipse((15, 14, 18, 17), fill=S["metal"][2])
    d.point((15, 14), fill=S["metal"][3])
    for x in (12, 21):                               # vents
        d.rectangle((x, 20, x + 1, 21), fill=c[0])


def draw_depot(img, d, S, rng):
    c, m = S["conc"], S["metal"]
    shade_rect(d, (1, 1, 29, 29), c)                 # concrete pad
    for x in range(4, 28, 6):
        d.line([(x, 2), (x, 28)], fill=c[0])
    d.rectangle((14, 9, 16, 21), fill=m[0])          # pipe between the tanks
    d.line([(14, 9), (14, 21)], fill=m[2])
    for cx, cy in ((9, 9), (21, 21)):
        shade_disc(img, cx, cy + 1, 7.5, [(0, 0, 0), m[0], m[0], m[0]])   # drop shadow
        shade_disc(img, cx, cy, 7, [m[0], m[1], m[2], m[3]])
        d.ellipse((cx - 2, cy - 2, cx + 2, cy + 2), fill=m[1], outline=m[0])
        d.line([(cx + 4, cy - 5), (cx + 4, cy + 5)], fill=m[0])          # ladder
        d.point((cx - 3, cy - 4), fill=S["accent"])                     # hazard marking
        d.point((cx - 4, cy - 3), fill=S["accent"])
    shade_rect(d, (22, 4, 27, 9), S["roof"])         # pump house
    d.point((24, 6), fill=S["light"])


def draw_barracks(img, d, S, rng):
    r, kind = S["roof"], S["roof_kind"]
    x0, y0, x1, y1 = 1, 3, 44, 26
    mid = (y0 + y1) // 2
    for y in range(y0, y1 + 1):                      # gabled roof, lit north half
        for x in range(x0, x1 + 1):
            north = y < mid
            c = r[2] if north else r[1]
            if kind == "tin" and x % 3 == 0:
                c = r[3] if north else r[0]
            elif kind == "wood" and (y - y0) % 3 == 0:
                c = r[0]
            elif kind == "clay" and (x + (y // 2) * 2) % 4 == 0:
                c = r[3] if north else r[0]
            elif kind == "snow":
                c = r[3] if north else (r[1] if x % 3 == 0 else r[2])
                if north and (y - y0) % 4 == 3 and rng.random() < 0.6:
                    c = r[2]
            d.point((x, y), fill=c)
    d.line([(x0, mid), (x1, mid)], fill=r[3])        # ridge
    d.line([(x0, mid + 1), (x1, mid + 1)], fill=r[0])
    d.rectangle((x0, y0, x1, y1), outline=(0, 0, 0))
    for x in (8, 22, 36):                            # roof vents on the ridge
        shade_rect(d, (x, mid - 2, x + 3, mid + 1), S["metal"])
    shade_rect(d, (18, y1 + 1, 26, y1 + 4), S["conc"])   # door canopy
    d.rectangle((20, y1 + 2, 24, y1 + 3), fill=S["window"])
    for x in (4, 12, 32, 40):                        # windows under the eaves
        d.rectangle((x, y1 + 1, x + 2, y1 + 2), fill=S["window"])
        d.point((x, y1 + 1), fill=S["light"])


def draw_dome(img, d, S, rng):
    c, g = S["conc"], S["dome"]
    d.regular_polygon((22, 22, 21), 8, rotation=22.5, fill=c[1], outline=c[0])   # base ring
    d.regular_polygon((22, 22, 20), 8, rotation=22.5, outline=c[2])
    shade_disc(img, 22, 22, 17, g)
    px = img.load()
    for a in range(0, 180, 30):                      # geodesic frame
        ang = math.radians(a)
        for t in range(-17, 18):
            x, y = 22 + t * math.cos(ang), 22 + t * math.sin(ang)
            if math.hypot(x - 22, y - 22) < 16.5:
                px[int(round(x)), int(round(y))] = g[0]
    for rr in (6, 11):
        d.ellipse((22 - rr, 22 - rr, 22 + rr, 22 + rr), outline=g[0])
    d.ellipse((11, 11, 16, 15), fill=g[3])           # reflection
    shade_rect(d, (18, 38, 26, 44), c)               # airlock
    d.rectangle((20, 42, 24, 44), fill=S["window"])
    d.point((41, 5), fill=S["accent"])


def draw_hq(img, d, S, rng):
    c, m = S["conc"], S["metal"]
    shade_rect(d, (1, 1, 60, 44), c, bevel=2)        # parapet
    d.rectangle((4, 4, 57, 41), fill=c[1])
    d.line([(4, 4), (57, 4)], fill=c[0])
    d.line([(4, 4), (4, 41)], fill=c[0])
    for x in range(4, 58, 9):                        # roof sections
        d.line([(x, 5), (x, 41)], fill=c[2])
    d.ellipse((6, 15, 25, 34), fill=c[0])            # helipad
    d.ellipse((7, 16, 24, 33), outline=S["light"])
    for y in range(20, 30):
        d.point((12, y), fill=S["light"])
        d.point((18, y), fill=S["light"])
    for x in range(12, 19):
        d.point((x, 25), fill=S["light"])
    shade_disc(img, 47, 14, 7, m)                    # radar dish
    d.ellipse((44, 11, 50, 17), outline=m[0])
    d.line([(47, 14), (52, 9)], fill=m[0])
    d.point((52, 9), fill=S["accent"])
    for x, y in ((31, 8), (31, 18), (40, 30), (50, 30)):   # air-conditioning units
        shade_rect(d, (x, y, x + 6, y + 6), m)
        d.ellipse((x + 1, y + 1, x + 5, y + 5), outline=m[0])
        d.point((x + 3, y + 3), fill=m[0])
    d.line([(30, 34), (30, 25)], fill=m[3])          # antenna with a light
    d.point((30, 24), fill=S["accent"])
    d.rectangle((52, 38, 56, 41), fill=S["accent"])  # flag on the roof
    d.point((52, 37), fill=m[3])


def hazard(d, x0, y0, x1, y1, S):
    """yellow and black warning stripes"""
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            d.point((x, y), fill=S["light"] if (x + y) % 4 < 2 else (20, 20, 20))


def draw_garage(img, d, S, rng):
    """closed tank garage: a framed steel roof over the top two rows (drawn
    over the tank inside), and the front wall with the open door below"""
    c, m, r = S["conc"], S["metal"], S["roof"]
    shade_rect(d, (0, 0, 47, 33), r, bevel=3)          # roof frame
    shade_rect(d, (5, 5, 42, 29), m, bevel=1)          # steel roof panel
    d.rectangle((7, 7, 40, 27), fill=m[1])
    for p0, p1 in (((8, 8), (39, 26)), ((39, 8), (8, 26)), ((23, 8), (23, 26)), ((8, 17), (39, 17))):
        d.line([p0, p1], fill=m[0])                    # cross bracing
    for x, y in ((8, 8), (21, 8), (35, 8), (8, 15), (35, 15), (8, 23), (21, 23), (35, 23), (21, 15)):
        shade_rect(d, (x, y, x + 4, y + 4), m)         # bolted nodes
    for x in range(3, 45, 6):                          # vent slots in the frame
        d.line([(x, 1), (x + 2, 1)], fill=r[0])
        d.line([(x, 32), (x + 2, 32)], fill=r[0])
    # front wall with the door
    shade_rect(d, (0, 34, 47, 47), [m[0], m[1], m[1], m[2]], bevel=1)
    for x in range(2, 46, 3):
        d.line([(x, 36), (x, 46)], fill=m[0])
    for y in range(34, 48):                            # dark doorway, lighter towards the opening
        k = (y - 34) / 13
        col = tuple(int(10 + 50 * k) for _ in range(3))
        d.line([(16, y), (31, y)], fill=col)
    d.line([(15, 34), (15, 47)], fill=m[3])            # door frame
    d.line([(32, 34), (32, 47)], fill=m[0])
    d.line([(15, 34), (32, 34)], fill=(0, 0, 0))
    hazard(d, 16, 46, 31, 47, S)                       # threshold stripes
    d.point((14, 36), fill=S["light"]); d.point((33, 36), fill=S["light"])


def draw_firstaid(img, d, S, rng):
    c = S["conc"]
    shade_rect(d, (1, 1, 29, 29), [c[1], c[3], (250, 250, 250), (255, 255, 255)], bevel=2)
    d.rectangle((12, 6, 18, 24), fill=(220, 30, 30))   # red cross
    d.rectangle((6, 12, 24, 18), fill=(220, 30, 30))
    d.rectangle((13, 7, 17, 23), fill=(240, 60, 50))
    d.rectangle((7, 13, 23, 17), fill=(240, 60, 50))
    d.rectangle((24, 24, 28, 28), fill=S["window"])
    d.point((25, 25), fill=S["light"])


def draw_powcamp(img, d, S, rng):
    b, r = S["bags"], S["roof"]
    for y in range(2, 46):                            # trampled yard
        for x in range(2, 46):
            if rng.random() < 0.85:
                d.point((x, y), fill=b[0] if rng.random() < 0.3 else b[1])
    for x in range(1, 47, 2):                         # wire fence on posts
        d.point((x, 1), fill=(60, 60, 60)); d.point((x, 46), fill=(60, 60, 60))
        d.point((1, x), fill=(60, 60, 60)); d.point((46, x), fill=(60, 60, 60))
    for x in range(1, 47, 6):
        for p in ((x, 0), (x, 46), (0, x), (46, x)):
            d.rectangle((p[0], p[1], p[0] + 1, p[1] + 1), fill=(30, 24, 16))
    for hx, hy in ((6, 6), (6, 26), (26, 26)):        # huts with pitched roofs
        for y in range(hy, hy + 14):
            for x in range(hx, hx + 16):
                north = y < hy + 7
                c = r[2] if north else r[1]
                if (x - hx) % 3 == 0:
                    c = r[0]
                d.point((x, y), fill=c)
        d.rectangle((hx, hy, hx + 15, hy + 13), outline=(0, 0, 0))
        d.line([(hx, hy + 7), (hx + 15, hy + 7)], fill=r[3])
    shade_rect(d, (30, 6, 40, 16), S["metal"])        # guard post
    d.rectangle((33, 9, 37, 13), fill=S["roof"][2])
    d.point((31, 7), fill=S["light"])


def draw_radar(img, d, S, rng):
    c, m = S["conc"], S["metal"]
    shade_rect(d, (1, 1, 29, 29), c)
    shade_rect(d, (3, 20, 11, 28), S["roof"])         # control hut
    d.point((5, 22), fill=S["light"])
    for p in ((8, 8), (24, 8), (16, 24)):             # struts to the dish
        d.line([p, (16, 14)], fill=m[0])
    d.ellipse((5, 3, 27, 23), fill=m[3])              # big dish, seen from above at an angle
    d.ellipse((7, 5, 26, 22), fill=m[2])
    d.ellipse((10, 8, 22, 19), fill=m[1])
    d.arc((5, 3, 27, 23), 20, 160, fill=m[0])
    d.line([(16, 13), (21, 8)], fill=m[0])            # feed horn
    d.point((21, 8), fill=S["accent"])


def draw_power(img, d, S, rng):
    c, m = S["conc"], S["metal"]
    shade_rect(d, (1, 6, 46, 30), c, bevel=2)          # turbine hall
    for x in range(4, 45, 5):
        d.line([(x, 9), (x, 27)], fill=c[2])
    for cx in (11, 27):                               # two chimneys / cooling towers
        shade_disc(img, cx, 12, 9, [c[0], c[1], c[2], c[3]])
        d.ellipse((cx - 5, 7, cx + 5, 17), fill=(20, 18, 16))
        d.ellipse((cx - 3, 9, cx + 3, 15), fill=(50, 44, 38))
    for x in (37, 42):                                # transformers
        shade_rect(d, (x, 14, x + 3, 22), m)
        hazard(d, x, 23, x + 3, 24, S)
    d.line([(36, 18), (30, 18)], fill=m[0])
    d.point((44, 9), fill=S["accent"])


def draw_warehouse(img, d, S, rng):
    r, c = S["roof"], S["conc"]
    for y in range(2, 25):                            # long roof with skylights
        for x in range(1, 47):
            col = r[2] if y < 13 else r[1]
            if x % 2 == 0:
                col = r[3] if y < 13 else r[0]
            d.point((x, y), fill=col)
    for x in range(5, 44, 9):
        d.rectangle((x, 5, x + 4, 20), fill=S["window"])
        d.line([(x, 5), (x + 4, 5)], fill=S["light"])
    d.rectangle((1, 2, 46, 24), outline=(0, 0, 0))
    shade_rect(d, (15, 25, 31, 30), c)                # loading dock, open door
    d.rectangle((18, 25, 28, 27), fill=(0, 0, 0))
    for x, y in ((3, 27), (8, 27), (37, 26), (42, 28)):     # crates
        shade_rect(d, (x, y, x + 3, y + 3), S["bags"] + [S["bags"][2]])
        d.line([(x, y), (x + 3, y + 3)], fill=S["bags"][0])


def draw_mg(img, d, S, rng):
    b = S["bags"]
    for i in range(12):                               # ring of sandbags
        a = i * math.pi / 6
        x, y = 7.5 + math.cos(a) * 5.5, 7.5 + math.sin(a) * 5.5
        d.ellipse((x - 2, y - 1.5, x + 2, y + 1.5), fill=b[1], outline=b[0])
        d.point((round(x) - 1, round(y) - 1), fill=b[2])
    d.ellipse((4, 4, 11, 11), fill=S["conc"][0])


def draw_cannon(img, d, S, rng):
    """square armoured pad, framed like the original's gun emplacements"""
    r, c = S["roof"], S["conc"]
    shade_rect(d, (0, 0, 14, 14), r, bevel=2)
    d.rectangle((3, 3, 11, 11), fill=c[0])
    for i in range(3, 12, 2):                          # riveted inner rim
        d.point((i, 3), fill=c[2]); d.point((3, i), fill=c[2])
        d.point((i, 11), fill=(0, 0, 0)); d.point((11, i), fill=(0, 0, 0))


def draw_rocket(img, d, S, rng):
    c = S["conc"]
    shade_rect(d, (1, 1, 29, 29), c, bevel=2)
    d.ellipse((7, 7, 23, 23), fill=c[0])               # blast-scorched launch ring
    for i in range(16):
        a = i * math.pi / 8
        d.point((15 + math.cos(a) * 9, 15 + math.sin(a) * 9), fill=S["light"] if i % 2 else (30, 30, 30))
    for x, y in ((3, 3), (26, 3), (3, 26), (26, 26)):  # bollards
        shade_rect(d, (x, y, x + 2, y + 2), S["metal"])


def draw_heavy(img, d, S, rng):
    """big square emplacement: bronze frame, sloped armour, a ring for the gun"""
    r, c, m = S["roof"], S["conc"], S["metal"]
    shade_rect(d, (0, 0, 29, 29), r, bevel=3)
    for i in range(4):                                 # sloped inner walls
        d.rectangle((4 + i, 4 + i, 25 - i, 25 - i), outline=[r[3], r[2], r[1], r[0]][i])
    d.rectangle((8, 8, 21, 21), fill=c[0])
    for y in range(9, 21, 2):                          # mesh floor
        for x in range(9 + (y % 4 == 1), 21, 2):
            d.point((x, y), fill=c[1])
    d.ellipse((9, 9, 20, 20), outline=m[0])


def draw_flaghouse(img, d, S, rng):
    """armoured strongroom with the flag on its roof"""
    c, m = S["conc"], S["metal"]
    shade_rect(d, (1, 1, 29, 29), m, bevel=2)
    for i in range(4, 28, 4):                         # heavy riveted plates
        d.line([(i, 3), (i, 27)], fill=m[0])
        d.point((i + 2, 4), fill=m[3])
    shade_rect(d, (8, 8, 22, 22), c, bevel=2)
    hazard(d, 3, 26, 27, 27, S)                       # blast door frame
    d.rectangle((11, 24, 19, 28), fill=(0, 0, 0))
    d.line([(15, 9), (15, 20)], fill=(40, 36, 30))    # flag pole and cloth
    d.rectangle((16, 9, 21, 13), fill=S["accent"])
    d.point((17, 10), fill=(255, 255, 255))
    d.ellipse((13, 19, 17, 22), fill=c[0])


def draw_helipad(img, d, S, rng):
    """round landing pad with a target ring; the corners stay ground"""
    c, m = S["conc"], S["metal"]
    road = [c12(3, 3, 3), c12(5, 5, 5), c12(6, 6, 6), c12(8, 8, 8)]
    d.ellipse((1, 1, 46, 46), fill=(0, 0, 0))
    d.ellipse((2, 2, 45, 45), fill=road[3])
    d.ellipse((3, 3, 44, 44), fill=road[1])
    px = img.load()
    for _ in range(60):                               # asphalt grain
        x, y = rng.randrange(6, 42), rng.randrange(6, 42)
        if math.hypot(x - 23.5, y - 23.5) < 19 and px[x, y][3]:
            px[x, y] = road[2] + (255,)
    d.ellipse((12, 12, 35, 35), outline=(210, 210, 200))          # target ring and cross
    d.line([(23, 8), (23, 39)], fill=(210, 210, 200))
    d.line([(8, 23), (39, 23)], fill=(210, 210, 200))
    for x, y in ((23, 4), (23, 43), (4, 23), (43, 23)):           # edge lights
        d.point((x, y), fill=S["light"])
    d.arc((3, 3, 44, 44), 190, 260, fill=road[3])


def draw_twin(img, d, S, rng):
    """low concrete ring with ammunition boxes"""
    c, b = S["conc"], S["bags"]
    d.ellipse((0, 0, 15, 15), fill=(0, 0, 0))
    d.ellipse((1, 1, 14, 14), fill=c[1])
    d.arc((1, 1, 14, 14), 180, 270, fill=c[3])
    d.ellipse((4, 4, 11, 11), fill=c[0])
    d.rectangle((11, 12, 14, 14), fill=b[1]); d.point((11, 12), fill=b[2])


def draw_mortar(img, d, S, rng):
    """sunken pit ringed with sandbags, shells stacked at the side"""
    c, b = S["conc"], S["bags"]
    for i in range(18):
        a = i * math.pi / 9
        x, y = 15 + math.cos(a) * 12, 15 + math.sin(a) * 12
        d.ellipse((x - 3, y - 2, x + 3, y + 2), fill=b[1], outline=b[0])
        d.point((round(x) - 1, round(y) - 1), fill=b[2])
    d.ellipse((6, 6, 24, 24), fill=c[0])
    d.ellipse((8, 9, 22, 23), fill=(30, 26, 22))
    for k in range(3):                                # shells
        d.rectangle((22 + k * 2, 24, 23 + k * 2, 27), fill=S["metal"][2])
        d.point((22 + k * 2, 24), fill=S["accent"])


DRAW = {"tower": draw_tower, "bunker": draw_bunker, "depot": draw_depot,
        "barracks": draw_barracks, "dome": draw_dome, "hq": draw_hq,
        "garage": draw_garage, "firstaid": draw_firstaid, "powcamp": draw_powcamp,
        "radar": draw_radar, "power": draw_power, "warehouse": draw_warehouse,
        "mg": draw_mg, "cannon": draw_cannon, "rocket": draw_rocket, "heavy": draw_heavy,
        "flag": draw_flaghouse, "helipad": draw_helipad, "twin": draw_twin, "mortar": draw_mortar}


# ---- turret heads: 4 rows (mg, cannon, rocket, heavy) x 32 directions, 32x32 ------------

def turret_master(kind, S):
    m, c = S["metal"], S["conc"]
    size = CELL * SS
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cc = size / 2

    def P(u, v):
        return (cc + u * SS, cc - v * SS)

    def rect(u0, v0, u1, v1, col):
        x0, y0 = P(u0, v1)
        x1, y1 = P(u1, v0)
        d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=col)

    def circ(r, col, v=0.0):
        x0, y0 = P(-r, v + r)
        x1, y1 = P(r, v - r)
        d.ellipse([x0, y0, x1, y1], fill=col)

    gun = (52, 50, 46)
    if kind == "mg":
        rect(-1.4, 1, -0.6, 8, gun)                   # twin barrels
        rect(0.6, 1, 1.4, 8, gun)
        circ(3.2, m[2])
        rect(-3.2, -1, 3.2, 0.5, m[1])               # gun shield
        circ(1.2, m[0])
    elif kind == "cannon":
        rect(-0.8, 1, 0.8, 10, gun)
        rect(-1.2, 8.5, 1.2, 10, gun)
        circ(4.6, m[1])
        circ(3.6, m[2], 0.4)
        circ(1.3, m[0], -1.2)
    elif kind == "rocket":
        rect(-4.5, -3.5, 4.5, 4.5, m[1])             # launcher box
        rect(-4.5, 3.5, 4.5, 4.5, m[2])
        for u in (-3, -1, 1, 3):                     # four tubes at the front
            x0, y0 = P(u - 0.8, 5.5)
            x1, y1 = P(u + 0.8, 3.9)
            d.ellipse([x0, y0, x1, y1], fill=(20, 20, 20))
        rect(-1, -5.5, 1, -3.5, m[0])
    elif kind == "twin":
        rect(-2.2, 1, -1.2, 9, gun)                  # two auto-cannon barrels
        rect(1.2, 1, 2.2, 9, gun)
        rect(-3.6, -3, 3.6, 2.5, m[1])               # boxy mount
        rect(-3.6, 1.5, 3.6, 2.5, m[2])
        rect(-1, -2, 1, 0, m[0])
    elif kind == "mortar":
        circ(3.2, (40, 38, 34))                      # baseplate
        rect(-1.5, -1, 1.5, 6, gun)                  # short fat tube
        x0, y0 = P(-1.5, 6.5)
        x1, y1 = P(1.5, 5)
        d.ellipse([x0, y0, x1, y1], fill=(10, 10, 10))
        rect(-3.5, -3, -2.5, 0, m[2])                # bipod legs
        rect(2.5, -3, 3.5, 0, m[2])
    else:                                            # heavy gun
        rect(-1.3, 2, 1.3, 14, gun)
        rect(-1.9, 12, 1.9, 14, gun)                 # muzzle brake
        d.regular_polygon((cc, cc + SS, 6.2 * SS), 6, fill=m[1])
        d.regular_polygon((cc, cc + SS * 0.6, 4.8 * SS), 6, fill=m[2])
        circ(1.5, m[0], -2)
        rect(2.5, -3, 4, -1, m[3])
    return img


def build_turrets(P):
    S = P["st"]
    sheet = Image.new("RGBA", (CELL * 32, CELL * 6), (0, 0, 0, 0))
    for row, kind in enumerate(("mg", "cannon", "rocket", "heavy", "twin", "mortar")):
        master = turret_master(kind, S)
        for f in range(32):
            rot = master.rotate(-f * 360 / 32, resample=Image.NEAREST)
            sheet.paste(outline_and_shadow(majority_downsample(rot, SS)), (f * CELL, row * CELL))
    return sheet


def finish(img, S, rng):
    """style weathering on top of the drawing: sand drifts, moss or snow"""
    kind = S["finish"]
    if not kind:
        return
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            if p[3] < 255 or p[:3] == (0, 0, 0):
                continue
            if kind == "snow" and (y < h * 0.4 or rng.random() < 0.1) and rng.random() < 0.18:
                px[x, y] = c12(15, 15, 15) + (255,)
            elif kind == "moss" and rng.random() < 0.07:
                px[x, y] = rng.choice([c12(2, 6, 1), c12(3, 8, 2)]) + (255,)
            elif kind == "sand" and y > h * 0.7 and rng.random() < 0.18:
                px[x, y] = c12(14, 10, 6) + (255,)


def damage(img, rng, S):
    """scorched, holed and broken version of a structure"""
    out = img.copy()
    px = out.load()
    w, h = out.size
    solid = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] == 255]
    if not solid:
        return out
    for _ in range(max(2, len(solid) // 120)):       # scorch marks
        cx, cy = rng.choice(solid)
        r = rng.uniform(2, 4.5)
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if 0 <= x < w and 0 <= y < h and px[x, y][3] == 255 and math.hypot(x - cx, y - cy) <= r:
                    if (x + y) % 2 == 0 or math.hypot(x - cx, y - cy) < r * 0.5:
                        px[x, y] = (24, 18, 12, 255)
    for _ in range(max(1, len(solid) // 300)):       # holes with glowing edges
        cx, cy = rng.choice(solid)
        r = rng.uniform(1.2, 2.5)
        for y in range(int(cy - r) - 1, int(cy + r) + 2):
            for x in range(int(cx - r) - 1, int(cx + r) + 2):
                if not (0 <= x < w and 0 <= y < h) or px[x, y][3] < 255:
                    continue
                dd = math.hypot(x - cx, y - cy)
                if dd <= r:
                    px[x, y] = (0, 0, 0, 255)
                elif dd <= r + 1 and rng.random() < 0.6:
                    px[x, y] = rng.choice([S["accent"], c12(15, 9, 2)]) + (255,)
    return out


def outline_shadow_struct(img):
    """black outline plus a soft drop shadow, both inside the footprint"""
    w, h = img.size
    src = img.load()
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    o = out.load()
    for y in range(h):
        for x in range(w):
            if x >= 2 and y >= 2 and src[x - 2, y - 2][3] == 255 and not src[x, y][3]:
                o[x, y] = (0, 0, 0, 100)
    for y in range(h):
        for x in range(w):
            a = src[x, y][3]
            if a == 255:
                o[x, y] = src[x, y]
            elif a:                                   # drawn shadows keep their alpha
                o[x, y] = src[x, y]
            elif any(0 <= x + dx < w and 0 <= y + dy < h and src[x + dx, y + dy][3] == 255
                     for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                o[x, y] = (0, 0, 0, 255)
    return out


def remains(intact, rng, S, ruins):
    """what is left standing after the blast, as in the original: broken
    stubs of the outer walls, the corner posts, a burnt floor and chunks of
    the building lying about. The game adds the glowing embers on top."""
    w, h = intact.size
    src = intact.load()
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    o = out.load()
    solid = [(x, y) for y in range(h) for x in range(w) if src[x, y][3] == 255 and src[x, y][:3] != (0, 0, 0)]
    if not solid:
        return out
    xs, ys = [p[0] for p in solid], [p[1] for p in solid]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    seg = rng.randrange(1000)

    def scorch(c, k):
        return (int(c[0] * k), int(c[1] * k * 0.92), int(c[2] * k * 0.85), 255)

    for y in range(y0 + 2, y1 - 1):                   # patches of burnt floor (ruins tile shows between)
        for x in range(x0 + 2, x1 - 1):
            if rng.random() < 0.35:
                o[x, y] = (ruins[0] if rng.random() < 0.6 else ruins[1]) + (255,)
    for (x, y) in solid:                              # wall stubs: broken runs along the outline
        edge = min(x - x0, x1 - x, y - y0, y1 - y)
        if edge > 2:
            continue
        along = x if min(y - y0, y1 - y) <= 2 else y
        if ((along + seg) // 4) % 3 != 0 or edge == 0 and rng.random() < 0.3:
            o[x, y] = scorch(src[x, y], 0.8 - 0.1 * edge)
    for cx, cy in ((x0, y0), (x1 - 3, y0), (x0, y1 - 3), (x1 - 3, y1 - 3)):   # corner posts
        for y in range(max(0, cy), min(h, cy + 4)):
            for x in range(max(0, cx), min(w, cx + 4)):
                if src[x, y][3] == 255:
                    o[x, y] = scorch(src[x, y], 0.85)
    for _ in range(max(5, len(solid) // 45)):         # chunks thrown about
        sx, sy = rng.choice(solid)
        c = scorch(src[sx, sy], 0.9)
        cx, cy = rng.randrange(max(0, x0), max(1, min(w - 3, x1 - 1))), rng.randrange(max(0, y0), max(1, min(h - 3, y1 - 1)))
        cw, ch = rng.randrange(2, 5), rng.randrange(2, 4)
        for y in range(cy, min(h, cy + ch)):
            for x in range(cx, min(w, cx + cw)):
                o[x, y] = c if y > cy else (min(255, c[0] + 40), min(255, c[1] + 40), min(255, c[2] + 40), 255)
    edge_px = []                                      # black outline round what stands
    for y in range(h):
        for x in range(w):
            if o[x, y][3] == 255 and o[x, y][:3] not in (ruins[0], ruins[1]):
                for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and o[nx, ny][3] == 0:
                        edge_px.append((nx, ny))
    for p in edge_px:
        o[p] = (0, 0, 0, 255)
    return out


def build_structures(P):
    S = P["st"]
    sheet = Image.new("RGBA", (SLOT_W * 3, SLOT_H * len(STRUCTS)), (0, 0, 0, 0))
    for row, (name, tw, th) in enumerate(STRUCTS):
        rng = random.Random(row * 99 + 7)
        img = Image.new("RGBA", (tw * 16, th * 16), (0, 0, 0, 0))
        DRAW[name](img, ImageDraw.Draw(img), S, rng)
        finish(img, S, rng)
        intact = outline_shadow_struct(img)
        broken = damage(intact, random.Random(row * 13 + 1), S)
        sheet.paste(intact, (0, row * SLOT_H))
        sheet.paste(broken, (SLOT_W, row * SLOT_H))
        sheet.paste(remains(intact, random.Random(row * 31 + 5), S, P["ruins"]), (SLOT_W * 2, row * SLOT_H))
    return sheet


# ---- tank sprites ----------------------------------------------------------------------
CELL, SS = 32, 4
SHAPES = [
    dict(track=(4.0, 6.0, 7.0), hull=[(-4, -6), (4, -6), (4, 4), (2, 7), (-2, 7), (-4, 4)],
         turret=("circle", 2.6, -0.5), barrel=(0.6, 1.5, 10.0)),
    dict(track=(4.5, 7.0, 7.5), hull=[(-4.5, -6.5), (4.5, -6.5), (4.5, 6.5), (-4.5, 6.5)],
         turret=("rect", 3.0, -0.5), barrel=(0.7, 2.0, 11.0)),
    dict(track=(5.0, 8.0, 8.0), hull=[(-5, -7), (5, -7), (5, 7), (-5, 7)],
         turret=("rect", 3.8, -1.0), barrel=(0.9, 2.5, 12.5)),
]


def tank_master(cls, colors, world="classic"):
    sh, (body, dark, lite) = SHAPES[cls], colors
    track, tread, gun = (40, 40, 36), (90, 90, 84), (60, 56, 50)
    size = CELL * SS
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2

    def P(u, v):
        return (c + u * SS, c - v * SS)

    def rect(u0, v0, u1, v1, col):
        x0, y0 = P(u0, v1)
        x1, y1 = P(u1, v0)
        d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=col)

    ti, to, tl = sh["track"]
    for sgn in (-1, 1):
        a, b = sorted((sgn * ti, sgn * to))
        rect(a, -tl, b, tl, track)
        v = -tl
        while v < tl:
            rect(a, v, b, v + 0.5, tread)
            v += 2
    d.polygon([P(u, v) for u, v in sh["hull"]], fill=body)
    hull_left = min(u for u, _ in sh["hull"])
    hull_mask = Image.new("1", img.size, 0)
    ImageDraw.Draw(hull_mask).polygon([P(u, v) for u, v in sh["hull"]], fill=1)
    crng = random.Random(cls * 17 + body[0])
    blotch = Image.new("RGBA", img.size, (0, 0, 0, 0))
    bd = ImageDraw.Draw(blotch)
    for _ in range(5):                               # camouflage blotches
        u, v = crng.uniform(-5, 5), crng.uniform(-7, 7)
        r = crng.uniform(1.2, 2.2)
        x0, y0 = P(u - r, v + r)
        x1, y1 = P(u + r, v - r)
        bd.ellipse([x0, y0, x1, y1], fill=dark)
    img.paste(blotch, (0, 0), Image.composite(blotch, Image.new("RGBA", img.size, (0, 0, 0, 0)), hull_mask))
    rect(hull_left, -6, hull_left + 1, 6, lite)
    rect(-hull_left - 1, -6, -hull_left, 6, dark)
    for v in (-5.5, -4.5, -3.5):                     # engine grille at the back
        rect(-2.5, v, 2.5, v + 0.5, dark)
    rect(hull_left + 0.5, 1, hull_left + 1.8, 4, lite)   # storage boxes on the fenders
    rect(-hull_left - 1.8, 1, -hull_left - 0.5, 4, dark)
    kind, r, tv = sh["turret"]
    bw, bv0, bv1 = sh["barrel"]
    rect(-bw, bv0, bw, bv1, gun)
    if cls == 2:
        rect(-bw - 0.6, bv1 - 1.5, bw + 0.6, bv1, gun)
    if kind == "circle":
        x0, y0 = P(-r, tv + r)
        x1, y1 = P(r, tv - r)
        d.ellipse([x0, y0, x1, y1], fill=lite)
    else:
        rect(-r, tv - r, r, tv + r, lite)
        rect(-r, tv - r, -r + 1, tv + r, dark)
    rect(-0.8, tv - 0.8, 0.8, tv + 0.8, dark)
    rect(-r + 0.5, tv + r - 1.2, -r + 1.5, tv + r - 0.4, (230, 230, 220))   # commander's periscope
    if world == "volcanic":
        # Rounded riveted cast armour and linked tracks, inspired by Alucard.
        x0, y0 = P(-5, 6); x1, y1 = P(5, -4)
        d.rounded_rectangle((x0, y0, x1, y1), radius=3*SS, fill=dark, outline=lite, width=SS)
        x0, y0 = P(-3.5, 4.5); x1, y1 = P(3.5, -2.5)
        d.ellipse((x0,y0,x1,y1), fill=body, outline=lite, width=SS)
        for u,v in ((-4,5),(4,5),(-4,-3),(4,-3)):
            rect(u-.4,v-.4,u+.4,v+.4,(28,26,24))
        rect(-bw, 1, bw, bv1, gun)
        rect(-bw+.3, 1, bw, bv1, lite)
        rect(-3,-8,3,-6,dark)
    elif world == "moon":
        # Painted-tank reference: angular rover decks, coils and split armour.
        d.polygon([P(-6,-7),P(-5,6),P(0,9),P(5,6),P(6,-7)], fill=dark, outline=lite, width=SS)
        d.polygon([P(-4,0),P(0,5),P(4,0),P(3,-4),P(-3,-4)], fill=body, outline=lite, width=SS)
        for u in (-5,4):
            rect(u,-5,u+1,3,(90,160,190)); rect(u,-4,u+1,-3,(215,235,235))
        for u in ((-2,2) if cls == 2 else (0,)):
            rect(u-0.6,0,u+0.6,11,(50,65,70)); rect(u-.3,1,u+.3,10,(210,230,225))
        rect(-2,-5,2,-4,(80,200,210))
    return img


def majority_downsample(img, f):
    src = img.load()
    w, h = img.size[0] // f, img.size[1] // f
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    dst = out.load()
    for y in range(h):
        for x in range(w):
            counts = {}
            for yy in range(f):
                for xx in range(f):
                    p = src[x * f + xx, y * f + yy]
                    if p[3] < 128:
                        p = (0, 0, 0, 0)
                    counts[p] = counts.get(p, 0) + 1
            best = max(counts.items(), key=lambda kv: kv[1])[0]
            opaque = [kv for kv in counts.items() if kv[0][3]]
            if best[3] == 0 and opaque and sum(n for _, n in opaque) >= f * f // 2:
                best = max(opaque, key=lambda kv: kv[1])[0]
            dst[x, y] = best
    return out


def outline_and_shadow(img):
    w, h = img.size
    src = img.load()
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    o = out.load()
    for y in range(h):
        for x in range(w):
            if x >= 2 and y >= 2 and src[x - 2, y - 2][3]:
                o[x, y] = (0, 0, 0, 100)
    for y in range(h):
        for x in range(w):
            if src[x, y][3]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if 0 <= x + dx < w and 0 <= y + dy < h and src[x + dx, y + dy][3]:
                    o[x, y] = (0, 0, 0, 255)
                    break
    for y in range(h):
        for x in range(w):
            if src[x, y][3]:
                o[x, y] = src[x, y]
    return out


def build_tanks(teams, world="classic"):
    sheet = Image.new("RGBA", (CELL * 32, CELL * 6), (0, 0, 0, 0))
    for cls in range(3):
        for team in range(2):
            master = tank_master(cls, teams[team], world)
            for f in range(32):
                rot = master.rotate(-f * 360 / 32, resample=Image.NEAREST)
                sheet.paste(outline_and_shadow(majority_downsample(rot, SS)),
                            (f * CELL, (cls * 2 + team) * CELL))
    return sheet


def build_backdrop(colors, bg, seed=1987):
    img = Image.new("RGBA", (64, 64), bg + (255,))
    px = img.load()
    rng = random.Random(seed)
    for _ in range(26):
        cx, cy = rng.uniform(0, 64), rng.uniform(0, 64)
        rx, ry, rot = rng.uniform(3, 7), rng.uniform(2, 5), rng.uniform(0, math.pi)
        col = rng.choice(colors)
        for y in range(64):
            for x in range(64):
                dx = (x - cx + 32) % 64 - 32
                dy = (y - cy + 32) % 64 - 32
                u = dx * math.cos(rot) + dy * math.sin(rot)
                v = -dx * math.sin(rot) + dy * math.cos(rot)
                if (u / rx) ** 2 + (v / ry) ** 2 <= 1 + 0.25 * math.sin(3 * math.atan2(v, u) + cx):
                    px[x, y] = col
    return img


# ---- selection-screen pictures (UI theme) ---------------------------------------------------
CW, CH = 176, 104
SIDE = [
    dict(hull=[(30, 70), (136, 70), (150, 60), (128, 54), (40, 54), (24, 62)],
         turret=[(66, 54), (106, 54), (100, 44), (74, 44)], gun=(100, 47, 160, 2), wheels=4),
    dict(hull=[(24, 74), (142, 74), (156, 60), (134, 50), (36, 50), (18, 62)],
         turret=[(58, 50), (114, 50), (106, 36), (70, 34), (60, 40)], gun=(108, 41, 168, 3), wheels=5),
    dict(hull=[(18, 78), (150, 78), (162, 62), (150, 46), (24, 46), (12, 60)],
         turret=[(48, 46), (122, 46), (122, 28), (56, 28), (48, 36)], gun=(120, 35, 172, 4), wheels=6),
]
SEPIA = dict(body=c12(10, 6, 3), dark=c12(7, 4, 2), lite=c12(13, 9, 5), deep=c12(4, 2, 1),
             steel=c12(6, 4, 2), steel_hi=c12(10, 7, 4), steel_lo=c12(3, 2, 1))


def draw_side_tank(img, cls, sh, col, yoff, grain=0.22):
    rng = random.Random(cls * 31 + 5)
    body, dark, lite, deep = col["body"], col["dark"], col["lite"], col["deep"]
    steel, steel_hi, steel_lo = col["steel"], col["steel_hi"], col["steel_lo"]
    d = ImageDraw.Draw(img)
    px = img.load()
    hull = [(x, y + yoff) for x, y in sh["hull"]]
    tur = [(x, y + yoff) for x, y in sh["turret"]]
    gx, gy, gl, gw = sh["gun"]
    gy += yoff
    left, right = min(x for x, _ in hull), max(x for x, _ in hull)
    top, bottom = min(y for _, y in hull), max(y for _, y in hull)
    track_top, track_bot = bottom - 4, bottom + 12

    def layer_mask(fn):
        m = Image.new("1", (CW, CH), 0)
        fn(ImageDraw.Draw(m))
        return m

    d.rounded_rectangle([left + 6, track_top, right - 6, track_bot], radius=7, fill=steel_lo)
    for x in range(left + 8, right - 6, 3):
        d.point((x, track_bot), fill=steel)
        d.point((x, track_top), fill=steel)
    n = sh["wheels"]
    x0, x1 = left + 16, right - 16
    for i in range(n):
        wx = round(x0 + (x1 - x0) * i / (n - 1))
        d.ellipse([wx - 6, track_top + 2, wx + 6, track_top + 14], fill=steel)
        d.arc([wx - 6, track_top + 2, wx + 6, track_top + 14], 180, 300, fill=steel_hi)
        d.ellipse([wx - 2, track_top + 6, wx + 2, track_top + 10], fill=steel_hi)
    d.rectangle([gx, gy, gl, gy + gw], fill=steel)
    d.line([(gx, gy), (gl, gy)], fill=steel_hi)
    d.line([(gx, gy + gw), (gl, gy + gw)], fill=steel_lo)
    if cls == 2:
        d.rectangle([gl - 6, gy - 2, gl, gy + gw + 2], fill=steel)
    else:
        d.rectangle([gx + (gl - gx) // 2, gy - 1, gx + (gl - gx) // 2 + 5, gy + gw + 1], fill=steel)
    hm = layer_mask(lambda m: m.polygon(hull, fill=1)).load()
    tm = layer_mask(lambda m: m.polygon(tur, fill=1)).load()
    mid = top + (bottom - top) * 0.55
    for y in range(CH):
        for x in range(CW):
            if hm[x, y]:
                c = lite if y <= top + 1 else ((dark if (x + y) % 2 else body) if y > mid else body)
                if y > bottom - 3:
                    c = dark
                px[x, y] = c
    ty_base = max(y for _, y in tur)
    for x in range(left + 8, right - 8):
        if hm[x, round(mid)]:
            px[x, round(mid)] = deep
        if hm[x, ty_base + 1] and min(t[0] for t in tur) <= x <= max(t[0] for t in tur):
            px[x, ty_base + 1] = dark
    ttop = min(y for _, y in tur)
    for y in range(CH):
        for x in range(CW):
            if tm[x, y]:
                c = lite if y <= ttop + 1 else body
                if y >= ty_base - 3:
                    c = dark if (x + y) % 2 else body
                px[x, y] = c
    tx0, tx1 = min(x for x, _ in tur), max(x for x, _ in tur)
    hx = tx0 + (tx1 - tx0) // 3
    d.rectangle([hx, ttop - 3, hx + 8, ttop - 1], fill=dark)
    d.line([(tx0 + 4, ttop), (tx0 + 2, ttop - 18)], fill=steel_lo)
    full = layer_mask(lambda m: (m.polygon(hull, fill=1), m.polygon(tur, fill=1),
                                 m.rectangle([gx, gy - 2, gl, gy + gw + 2], fill=1),
                                 m.rounded_rectangle([left + 6, track_top, right - 6, track_bot], radius=7, fill=1),
                                 m.rectangle([hx, ttop - 3, hx + 8, ttop - 1], fill=1)))
    fm = full.load()
    ramp = [deep, steel_lo, dark, body, lite]
    for y in range(CH):
        for x in range(CW):
            if fm[x, y] and rng.random() < grain:
                p = px[x, y][:3]
                if p in ramp:
                    px[x, y] = ramp[max(0, min(4, ramp.index(p) + rng.choice((-1, 1))))]
    for y in range(1, CH - 1):
        for x in range(1, CW - 1):
            if not fm[x, y] and (fm[x + 1, y] or fm[x - 1, y] or fm[x, y + 1] or fm[x, y - 1]):
                px[x, y] = BLACK + (255,)


def build_cards():
    sheet = Image.new("RGBA", (CW * 3, CH), (0, 0, 0, 255))
    for cls in range(3):
        img = Image.new("RGBA", (CW, CH), (0, 0, 0, 255))
        d = ImageDraw.Draw(img)
        rng = random.Random(9 + cls)
        for y in range(92, CH):
            for x in range(8, CW - 8):
                if rng.random() < 0.18 * (1 - abs(x - CW / 2) / (CW / 2)):
                    d.point((x, y), fill=c12(3, 2, 1))
        draw_side_tank(img, cls, SIDE[cls], SEPIA, 14 - (4 if cls == 2 else 0))
        sheet.paste(img, (cls * CW, 0))
    return sheet


ICONS = {
    "pow":   ["...##...", "...##...", ".######.", "#.####.#", "..####..", "..#..#..", ".##..##.", "........"],
    "tank":  ["........", "...##...", "..####.#", "########", "########", ".######.", "#.#.#.#.", "........"],
    "mine":  ["........", "..#..#..", "...##...", ".######.", "########", ".######.", "...##...", "........"],
    "fuel":  [".####...", ".#..##..", "######..", "#.##.#..", "#.##.#..", "#.##.#..", "######..", "........"],
    "armor": ["########", "#......#", "#.####.#", "#.####.#", ".#.##.#.", "..#..#..", "...##...", "........"],
    "left":  ["....#...", "...##...", "..###...", ".####...", "..###...", "...##...", "....#...", "........"],
    "right": ["...#....", "...##...", "...###..", "...####.", "...###..", "...##...", "...#....", "........"],
    "speed": ["........", "..####..", ".#....#.", "#..#...#", "#...#..#", "#...#..#", ".#....#.", "........"],
    "up":    ["...#....", "..###...", ".#####..", "#######.", "........", "........", "........", "........"],
    "down":  ["........", "........", "........", "........", "#######.", ".#####..", "..###...", "...#...."],
}
ICON_ORDER = ["pow", "tank", "mine", "fuel", "armor", "left", "right", "speed", "up", "down"]


def build_ui():
    sheet = Image.new("RGBA", (8 * len(ICON_ORDER), 8), (0, 0, 0, 0))
    px = sheet.load()
    for i, n in enumerate(ICON_ORDER):
        for y, row in enumerate(ICONS[n]):
            for x, ch in enumerate(row):
                if ch == "#":
                    px[i * 8 + x, y] = (255, 255, 255, 255)
    return sheet


# ---- parked vehicles: 6 kinds x (our colours, theirs, burnt wreck), 32 directions -------
VEHICLES = ["truck", "tanker", "jeep", "apc", "halftrack", "gun"]     # = PropKind order


def vehicle_master(kind, colors, wreck=False):
    """top-down soft-skinned and light vehicles, facing north, at SS x"""
    body, dark, lite = colors
    tire, glass, metal = (34, 34, 32), (120, 150, 170), (80, 80, 76)
    tarp = tuple(min(255, int(c * 1.15) + 12) for c in body)
    size = CELL * SS
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2

    def B(u0, v0, u1, v1, col):
        x0, y0 = c + u0 * SS, c - v1 * SS
        x1, y1 = c + u1 * SS, c - v0 * SS
        d.rectangle([min(x0, x1), min(y0, y1), max(x0, x1) - 1, max(y0, y1) - 1], fill=col)

    def wheels(vs, w=5.4):
        for v in vs:
            B(-w - 0.4, v - 1.3, -w + 1.0, v + 1.3, tire)
            B(w - 1.0, v - 1.3, w + 0.4, v + 1.3, tire)

    if kind == "truck":
        wheels((7.5, -3, -7.5))
        B(-4.6, 4.5, 4.6, 10.5, body)                  # cab
        B(-4.6, 9.5, 4.6, 10.5, dark)                  # bumper
        B(-3.6, 6.0, 3.6, 8.2, glass)                  # windscreen seen from above
        B(-4.8, -10.5, 4.8, 4.0, tarp)                 # canvas over the cargo bed
        for v in (-8, -4.5, -1, 2.5):                  # tarp ribs
            B(-4.8, v, 4.8, v + 0.6, dark)
        B(-4.8, -10.5, -4.0, 4.0, lite)
    elif kind == "tanker":
        wheels((8.5, -2, -6, -9.5))
        B(-4.6, 6.0, 4.6, 11.5, body)
        B(-3.6, 7.5, 3.6, 9.6, glass)
        B(-4.4, -11.5, 4.4, 5.0, metal)                # the tank barrel
        B(-4.4, -11.5, -2.6, 5.0, (150, 150, 144))     # sheen on the round tank
        B(2.8, -11.5, 4.4, 5.0, (52, 52, 50))
        B(-1.2, -4.0, 1.2, -1.5, dark)                 # filler hatch
        B(-4.4, -6.5, 4.4, -6.0, (200, 40, 30))        # hazard band
    elif kind == "jeep":
        wheels((4, -4), 4.2)
        B(-3.6, -6.0, 3.6, 6.0, body)
        B(-3.0, 3.0, 3.0, 4.2, glass)                  # windscreen
        B(-2.6, -2.0, -0.6, 1.5, dark)                 # seats
        B(0.6, -2.0, 2.6, 1.5, dark)
        B(-1.4, -6.8, 1.4, -5.6, tire)                 # spare wheel
        B(-3.6, 5.2, 3.6, 6.0, lite)
    elif kind == "apc":
        wheels((7, 3, -1, -5), 6.0)
        d.polygon([(c - 5.6 * SS, c - 6 * SS), (c - 3.5 * SS, c - 10 * SS), (c + 3.5 * SS, c - 10 * SS),
                   (c + 5.6 * SS, c - 6 * SS), (c + 5.6 * SS, c + 9.5 * SS), (c - 5.6 * SS, c + 9.5 * SS)], fill=body)
        B(-5.6, -9.5, -4.6, 6.0, lite)
        B(4.6, -9.5, 5.6, 6.0, dark)
        B(-2.0, 0.5, 2.0, 4.5, dark)                   # hatches
        B(-1.5, -5.5, 1.5, -2.5, dark)
        B(-0.4, 4.0, 0.4, 8.5, metal)                  # roof gun
    elif kind == "halftrack":
        wheels((6.5,), 5.0)
        B(-5.6, -10, -3.8, 2, tire)                    # rear tracks
        B(3.8, -10, 5.6, 2, tire)
        B(-4.0, 3.5, 4.0, 10.0, body)                  # engine bay and cab
        B(-3.2, 3.8, 3.2, 5.2, glass)
        B(-4.6, -10.0, 4.6, 3.0, body)                 # open troop bay
        B(-3.6, -9.0, 3.6, 2.0, dark)
        for v in (-7, -4, -1):
            B(-3.2, v, -1.6, v + 1.6, metal)           # seats
            B(1.6, v, 3.2, v + 1.6, metal)
    else:                                              # towed gun
        B(-5.8, -1.5, -4.0, 1.5, tire)
        B(4.0, -1.5, 5.8, 1.5, tire)
        B(-4.2, -0.8, 4.2, 0.8, metal)                 # axle
        d.polygon([(c - 4.5 * SS, c + 0.5 * SS), (c + 4.5 * SS, c + 0.5 * SS),
                   (c + 3.5 * SS, c - 2.5 * SS), (c - 3.5 * SS, c - 2.5 * SS)], fill=body)   # shield
        B(-0.6, 2.0, 0.6, 11.5, (52, 52, 48))          # barrel
        B(-1.0, 10.0, 1.0, 11.5, (52, 52, 48))
        d.line([(c - 0.5 * SS, c + 1 * SS), (c - 3 * SS, c + 9 * SS)], fill=dark, width=SS)   # trail legs
        d.line([(c + 0.5 * SS, c + 1 * SS), (c + 3 * SS, c + 9 * SS)], fill=dark, width=SS)
    if wreck:                                          # burnt out: dark, scorched, holed
        rng = random.Random(len(kind) * 7)
        px = img.load()
        for y in range(size):
            for x in range(size):
                p = px[x, y]
                if p[3] < 128:
                    continue
                k = 0.32 if rng.random() > 0.18 else 0.18
                q = (int(p[0] * k), int(p[1] * k * 0.95), int(p[2] * k * 0.9), 255)
                if rng.random() < 0.03:
                    q = (150, 60, 20, 255)
                px[x, y] = q
    return img


def build_vehicles(P):
    sheet = Image.new("RGBA", (CELL * 32, CELL * 3 * len(VEHICLES)), (0, 0, 0, 0))
    for k, kind in enumerate(VEHICLES):
        masters = [vehicle_master(kind, P["teams"][0]), vehicle_master(kind, P["teams"][1]),
                   vehicle_master(kind, P["teams"][1], wreck=True)]
        for row, m in enumerate(masters):
            for f in range(32):
                rot = m.rotate(-f * 360 / 32, resample=Image.NEAREST)
                sheet.paste(outline_and_shadow(majority_downsample(rot, SS)), (f * CELL, (k * 3 + row) * CELL))
    return sheet


# ---- people and helicopters (UI theme: the same on every terrain) ---------------------------
MAN = 12


def man_master(kind, frame):
    """top-down soldier facing north at 4x: helmet, shoulders, stepping legs"""
    size = MAN * SS
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2
    if kind == "pow":
        shirt, dark, head = (220, 216, 200), (150, 146, 130), (230, 180, 140)
    elif kind == "friend":                                              # our men: sand khaki, tan helmet
        shirt, dark, head = (204, 164, 96), (150, 112, 60), (176, 132, 70)
    else:
        shirt, dark, head = (90, 120, 60), (50, 70, 34), (60, 80, 40)       # helmet
    step = 2 if frame else -2
    for side, sgn in ((-1.4, 1), (1.4, -1)):                           # legs
        x0 = c + side * SS - SS * 0.6
        y0 = c + (step * sgn) * SS * 0.5
        d.rectangle([x0, y0, x0 + SS * 1.2, y0 + SS * 2.6], fill=(50, 44, 36))
    d.ellipse([c - 3.2 * SS, c - 1.4 * SS, c + 3.2 * SS, c + 1.6 * SS], fill=shirt)   # shoulders
    d.ellipse([c - 3.2 * SS, c - 0.2 * SS, c + 3.2 * SS, c + 1.6 * SS], fill=dark)
    if kind == "pow":                                                   # waving arms
        up = -3.5 if frame else -2.0
        d.rectangle([c - 3.4 * SS, c + up * SS, c - 2.4 * SS, c], fill=shirt)
        d.rectangle([c + 2.4 * SS, c + (-2.0 if frame else -3.5) * SS, c + 3.4 * SS, c], fill=shirt)
    else:                                                               # rifle forward
        d.rectangle([c + 0.6 * SS, c - 4.5 * SS, c + 1.4 * SS, c], fill=(30, 30, 30))
        # an armband in the side's colour, as on the flags: blue for ours, red for theirs
        band = (60, 130, 255) if kind == "friend" else (230, 40, 30)
        d.rectangle([c - 3.4 * SS, c - 0.6 * SS, c - 1.8 * SS, c + 1.0 * SS], fill=band)
        d.rectangle([c + 1.8 * SS, c - 0.6 * SS, c + 3.4 * SS, c + 1.0 * SS], fill=band)
    d.ellipse([c - 1.6 * SS, c - 1.8 * SS, c + 1.6 * SS, c + 1.4 * SS], fill=head)
    return img


def build_men():
    sheet = Image.new("RGBA", (MAN * 16, MAN * 3), (0, 0, 0, 0))
    for row, kind in enumerate(("pow", "soldier", "friend")):
        for dirn in range(8):
            for frame in range(2):
                rot = man_master(kind, frame).rotate(-dirn * 45, resample=Image.NEAREST)
                spr = majority_downsample(rot, SS)
                px = spr.load()                                         # thin dark outline
                out = spr.copy()
                op = out.load()
                for y in range(MAN):
                    for x in range(MAN):
                        if px[x, y][3] == 0 and any(0 <= x + dx < MAN and 0 <= y + dy < MAN and px[x + dx, y + dy][3]
                                                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                            op[x, y] = (0, 0, 0, 200)
                sheet.paste(out, ((dirn * 2 + frame) * MAN, row * MAN))
    return sheet


HELI = 40


def heli_master():
    size = HELI * SS
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2
    body, dark, lite = (70, 96, 54), (40, 58, 30), (110, 140, 84)

    def P(u, v):
        return (c + u * SS, c - v * SS)

    def B(u0, v0, u1, v1):              # box from two corners, any order
        (x0, y0), (x1, y1) = P(u0, v0), P(u1, v1)
        return [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]

    tail = [P(-0.9, -2), P(0.9, -2), P(0.6, -15), P(-0.6, -15)]           # tail boom
    d.polygon(tail, fill=dark)
    d.rectangle(B(-3, -14.5, 3, -13.5), fill=dark)                 # tail stabiliser
    d.rectangle(B(3, -12, 3.6, -16), fill=(30, 30, 30))            # tail rotor
    for u in (-4.6, 4.6):                                                 # skids
        d.rectangle(B(u - 0.4, 6, u + 0.4, -6), fill=(36, 36, 36))
    d.ellipse(B(-3.8, 8, 3.8, -5), fill=body)                      # fuselage
    d.ellipse(B(-3.8, 8, -0.5, -5), fill=lite)
    d.ellipse(B(-2.6, 8, 2.6, 3.5), fill=(120, 170, 190))          # canopy glass
    d.ellipse(B(-1.8, 7.6, -0.2, 5.4), fill=(210, 235, 240))
    d.rectangle(B(-5.5, 0.8, 5.5, -0.8), fill=dark)                # weapon pylons
    for u in (-5.2, 5.2):
        d.rectangle(B(u - 0.6, 2.5, u + 0.6, -1.5), fill=(40, 40, 40))
    d.ellipse(B(-1.2, 1.2, 1.2, -1.2), fill=(30, 30, 30))          # rotor hub
    return img


def build_heli():
    sheet = Image.new("RGBA", (HELI * 32, HELI), (0, 0, 0, 0))
    master = heli_master()
    for f in range(32):
        rot = master.rotate(-f * 360 / 32, resample=Image.NEAREST)
        spr = majority_downsample(rot, SS)
        px = spr.load()
        out = spr.copy()
        op = out.load()
        for y in range(HELI):
            for x in range(HELI):
                if px[x, y][3] == 0 and any(0 <= x + dx < HELI and 0 <= y + dy < HELI and px[x + dx, y + dy][3]
                                            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    op[x, y] = (0, 0, 0, 255)
        sheet.paste(out, (f * HELI, 0))
    return sheet


# ---- output ----------------------------------------------------------------------------------

def write_ui_theme(out):
    os.makedirs(out, exist_ok=True)
    classic = STYLES["classic"]
    build_cards().save(os.path.join(out, "cards.png"))
    build_ui().save(os.path.join(out, "ui.png"))
    build_men().save(os.path.join(out, "men.png"))
    build_heli().save(os.path.join(out, "heli.png"))
    build_tanks(classic["teams"]).save(os.path.join(out, "tanks.png"))
    build_backdrop(classic["camo"], classic["camo_bg"]).save(os.path.join(out, "backdrop.png"))
    with open(os.path.join(out, "theme.txt"), "w") as f:
        f.write("name=Default\nauthor=OpenFirePower contributors\nlicense=MIT\n"
                "panel=555555\nframe=dd8833\nframe_dark=773311\nrivet=dd8833\nlabel=bb0000\n"
                "value=ffff00\nbar_fuel=888888\nbar_armor=33aa00\nteam0=dd8833\nteam1=33aa00\n")
    print("wrote theme default ->", os.path.abspath(out))


def write_style(out, name, P):
    os.makedirs(out, exist_ok=True)
    build_tiles(P).save(os.path.join(out, "tiles.png"))
    build_structures(P).save(os.path.join(out, "structures.png"))
    build_turrets(P).save(os.path.join(out, "turrets.png"))
    build_vehicles(P).save(os.path.join(out, "vehicles.png"))
    build_tanks(P["teams"], name).save(os.path.join(out, "tanks.png"))
    build_backdrop(P["camo"], P["camo_bg"], seed=sum(map(ord, name))).save(os.path.join(out, "backdrop.png"))
    with open(os.path.join(out, "style.txt"), "w") as f:
        f.write(f"name={P['title']}\nbiome={P['biome']}\nauthor=OpenFirePower contributors\nlicense=MIT\n")
        if P["wall_kind"] == "barrier":
            f.write("bollards=1\n")
        if "teams" in P:                                  # the sides' colours on screen: the tanks' own (lighter)
            for side, key in ((0, "team_camp"), (1, "team_fortress")):
                c = P["teams"][side][2]
                f.write("%s=%d,%d,%d\n" % (key, c[0], c[1], c[2]))
        f.write("water=%s\n" % {"ice": "ice", "river": "murky"}.get(P["water_kind"], "waves"))
        if P.get("faithful"):
            f.write("faithful=1\n")
    print("wrote style", name, "->", os.path.abspath(out))


def main():
    data = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "data")
    write_ui_theme(os.path.join(data, "themes", "default"))
    for name, P in STYLES.items():
        write_style(os.path.join(data, "styles", name), name, P)


if __name__ == "__main__":
    main()
