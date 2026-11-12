# Maps

A game is played either on a freshly generated battlefield ("RANDOM") or on a
map file. Each map file also sets the battle's **time of day** (day, dusk or
night). For a random battlefield you pick it on the game-type screen. On the game-type screen, pick the **WORLD** (classic, desert,
jungle, winter), then the **MAP**: RANDOM, or one of that world's map files.
A picture beside the menu shows the map with both camps and flags. For
RANDOM it shows the very battlefield you'll get.

## Where maps come from

| Folder | What | In git |
|---|---|---|
| `data/maps/` | maps that come with the game | yes |
| `data/maps/original/` | made from **your own** Fire Power disk by `tools/ofp_maps.py` | no |
| `…/OpenFirePower/maps/` (next to `settings.ini`) | yours, saved by the map editor | — |

## Classic world and disk imports

The Classic world comes with four new 1 vs 1 maps in the 1987 style:
Dust Crossing, Perimeter Road, Switchback and Twin Boulevards. They use
continuous two-tile roads linking two walled bases. Each base has one garage
and a destructible building that conceals its flag. Four-tile gates open
for their own team. Opponents can destroy a gate to breach the wall.

The ADF import tools remain available separately.

**With the tools** (also brings the disk's graphics and sounds):

```
python3 tools/ofp_adf.py theme FirePower.adf     # graphics, sounds and the maps
python3 tools/ofp_maps.py FirePower.adf          # just the maps
```

Disk imports are separate from the four authored Classic world levels.

This reads the maps on your disk and writes them as `.ofpmap` files. Identical
maps (several levels share one) are written once; a typical disk gives six.

**Size:** an original map is 64 × 64 blocks of 64 pixels. Each block becomes
2 × 2 of our 16-pixel tiles, so the maps keep their layout at half the
original's scale.

**Ground:** original street blocks supply the road shapes. Walls, landing
pads and rubble are classified separately. The two sizes of tree remain
canopy.

**Buildings and guns:** mapped by block number to remake structures, at their
imported positions where the footprints fit. First-aid pads belong to the
nearer garage's team.

**Home bases:** the original player's garage (block 20) anchors the first
spawn. The importer adds one garage at least 60 tiles away for the second
side, favoring clear ground with a road outside the door. These maps use
`bases=garages` and keep the surrounding terrain and structures instead of
generating full compounds. Each side's flag is outside its garage.

## Desert island maps

**Saltwater Pass** (medium) and **Twin Harbors** (large) support Capture the Flag
and Conquest. Both maps have multiple paved crossings between islands.

## The map editor

Choose **Map Editor** to start a blank Large map in the Classic world. To edit an
existing map, press Escape and choose **File > Open**.

The sidebar groups tools into Ground, Roads, Nature, and Base. Press Tab or
use the category arrows to switch lists, then scroll to see more tools.
Use **Map** to open the overview and click a tile, or move the marker and
press Fire, to jump there.

| Action | macOS | AmigaOS |
|---|---|---|
| Choose a category | Tab | Tab |
| Choose a tool | Q and E | F3 and F4 |
| Open the overview | F6 | F7 |
| Move or paint | Player 1 movement and Fire | Player 1 movement and Fire |
| Pan or erase | Right-drag to pan, right-click to erase | Same |
| Editor menu | Escape | Escape or Player 1 Mine |

Use **Options** to set the world, weather, time, size, game mode, and
player count. Small supports 2 players, Medium 2 or 4, Large 4 or 6, and
Huge 8. A new Large map defaults to 4 players. Resizing erases the draft
after confirmation. World, weather, and time changes preserve the layout.
Choosing an enhanced world while the draft uses Classic rules switches it to
Capture the Flag. The Classic world and Classic rules both disable weather.
The toolbar's **Time** and **Weather** buttons control the editor previews.
The saved map keeps the settings from **Options**.

Open **Set Area**, select **Team 1** or **Team 2**, and drag on the overview
to draw an area. Drag inside a rectangle to move it or drag an edge or corner
to resize it. The editors assign buildings and guns to the team that contains
their footprint. Guns outside both areas are hostile to both teams.
Each garage and its exit must fit inside its team's area. Place one garage
per player, split evenly. Flag bases are available in Classic and Capture the
Flag. An imported map with built-in bases keeps its existing area layout.

Use **Details** to enter a title, description, author and optional contact.
**File > Save** stores these fields in the map. **Network > Upload Map to
Gallery** makes them public. **Test Run Map** plays the draft. AmigaOS uses
two tanks in a local test, including on maps designed for eight players.

## Editor recovery

Both editors save a dirty map every 30 seconds to three rotating recovery files.
These include unfinished maps that do not yet have all required garages.
After an interruption, reopening the editor offers **Restore Draft** or
**Discard Draft**. Restoring creates an unsaved map; manually saved maps stay
untouched. A corrupt latest draft falls back to an earlier valid generation.

Successful manual saves and an explicit exit that discards edits clear recovery
files. Desktop stores them in the user maps folder; Amiga uses `maps/` beside
its executable. Test runs, normal saves and gallery uploads still require the
usual playable-map checks. Recovery files cannot bypass those checks.

## The file format (`.ofpmap`)

Plain text, so maps are easy to share, diff and fix by hand:

```
OFPMAP 1
name=River Delta
description=Fight for the river crossings.
author=Map Maker
contact=@mapmaker
biome=jungle
base=player 7 94          top-left tile of each home base (34 x 26 tiles)
base=enemy 86 6
time=night                day, dusk or night (day when left out)
tiles                     then 128 lines of 128 characters:
....==~~^^TT,#:x          . sand  = road  ~ water  ^ rock  T bush
                          , flattened bush  # wall  : rubble  x ruins
struct=heavy enemy 40 52  one line per building or gun: type, side, x, y
end
```

* **Structure types:** `tower bunker depot barracks dome hq firstaid powcamp
  radar power warehouse helipad mg twin cannon rocket heavy mortar`
* **Sides:** `enemy`, `player`, or `independent` for a turret hostile to both sides.
* **Bases** are rebuilt from their positions when the map loads, so their
  insides aren't stored.
* **Loading is forgiving:** a building that doesn't fit is left out, and
  unknown lines are ignored.

Player-built bases use `bases=custom`, `mode=`, `players=`, and one
`compound=` rectangle per side. Their garages, flag bases and other buildings
are stored as individual `struct=` lines. The loader checks the areas, garage
counts and exits. Maps that use the earlier base formats still load.

The core reads and writes the format in memory (`mapfile_load`,
`mapfile_save` in `src/core/mapfile.c`), with no file access. That keeps it
usable unchanged on the Amiga and on the dedicated server, which sends
the map to clients as part of the match setup.


## The map's own weather and extras

Two optional lines after `time=`:

```
weather=storm            # enhanced modes: clear, thunderstorm, changing, rain, snow, snowstorm, fog, sandstorm, cloudy, eruption, or meteor
extras=pickups,hill      # or none, pickups, hill
```

A map that specifies weather uses it in the match. Winter supports Snow and Snowstorm. Winter offers Clear, Snow and Snowstorm, without rain or thunderstorms. The Graphics menu controls weather visibility on each client.

`roads=preserve` keeps painted road tiles where generated camps leave open ground.
Use it when laying out roads through a camp's gates or along its garage apron.

## Conquest's capture points

```
points=64,64 43,41 82,84     # up to three, in tiles
```

Without the line, conquest places them itself: the middle, and between it and each camp.

## Maps made for a mode

```
modes=flag,warlord           # flag, conquest, deathmatch, annihilation, warlord
arena=yes                    # an arena: no camps at all ...
spawns=64,22 94,34 106,64    # ... tanks come in at these (up to 8; the one furthest from the others)
```

LOCAL GAME's MAP offers the maps made for the mode chosen (and a random battlefield, which
suits it too: in the deathmatches it's an arena). A map without `modes=` has camps, so it
fits every mode but annihilation. In team deathmatch each side's spawn points are in its
own half of the arena (the camp's top left, the fortress's bottom right). The bundled ones:

* annihilation, 8 arenas: Scrapyard, Salt Pan, Ice Floe, Jungle Pit, Rust Belt,
  Crater Field, Glacier Bowl, Neon Pit
* team deathmatch and warlord, 6 arenas in halves: Close Quarters, Sandtrap, Reactor Core, Mangrove,
  Snowblind, Foundry
* conquest, 6: Crossfire, Three Rivers, Frostline, Highlands, Data Nodes, Monsoon Front
* capture the flag, 18: the camp maps, and the long ones with the camps in
  far corners (Long Haul, Border Run, Deep Jungle)

`tools/make_mode_maps.c` makes the deathmatch maps and the arenas.

## In the editor

P places a capture point at the cursor (P on one takes it away; three at most), O a spawn
point (eight at most; coloured by the half it's in, for the team modes), N makes the map an
arena (no camps; it needs two spawn points) or gives it camps again, L says which modes
it's made for (the modes= line). F1 lists every key.

## Made for so many

```
players=2                    # 1 vs 1 (4: 2 vs 2; 6: 3 vs 3). Without it: as many as fit
```

A map made for fewer than fit holds only those: FILL fills up to them, the team screen
splits them evenly, and MAP offers it only when everyone fits. The bundled ones for smaller
matches: 1 vs 1 Duel at Dawn, Two Dunes, Cold Standoff (capture the flag, camps closer),
Duel Ring (annihilation); 2 vs 2 Small Hours, Rustwater (flag), Outposts, Three Wells
(conquest), Pit Stop, Ice Box (deathmatches), Four Corners, Ember Ring (annihilation);
3 vs 3 Half Moon (deathmatches). The Classic world is 1 vs 1.

## Sizes

```
size=small                   # small 64 x 64, medium 96, large 128 (the original's), huge 192
```

Before the tiles; without it, LARGE (the original levels are LARGE too). The tiles are then that many rows of that many characters. In the map
editor, Z changes a map's size (growing: sand to the new edge; shrinking: bases that
no longer fit go to their corners); G makes a random map of its size.

## Trees overhead

`P` in the tiles: trees you drive under and hide beneath, as in the original (the original
levels' trees come in as these; `tools/add_groves.py` adds groves to map files). Shots pass
through and they stay; the computer's tanks don't see you under them unless they're close.

## Starting a map

The editor (MAIN MENU -> MAP EDITOR) opens the map chosen under LOCAL GAME's MAP for editing,
or, with MAP on RANDOM, a new empty map of LOCAL GAME's world: sand, the border and the two
bases. G (NEW RANDOM MAP) makes a random battlefield to start from instead; C (CLEAR) empties
a map again, its bases kept. Changing the world (B, or WORLD in START's menu) changes how it
looks at once.


### Enhanced surface tiles

`I` is driveable ice (Winter), `o` an oil spill (enhanced terrestrial worlds except Winter), and `O` a dry, impassable crater trench (Moon Base). `~` remains open water, unavailable on Moon Base. Ice and oil reduce braking and steering grip. Classic permits none of these three new surfaces. Both editors filter their palettes by world and replace incompatible surfaces when applying a different world.
