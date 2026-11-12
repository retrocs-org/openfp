# Map styles and UI themes

OpenFirePower keeps two kinds of art apart.

**Map styles** live in `data/styles/<name>/` and are the terrain of one kind of map. You pick one per game with **Up/Down** on the selection screen. A style also names its biome, which changes how the map generator mixes the terrain, and it can bring its own tank camouflage and backdrop.

**The UI theme** lives in `data/themes/<name>/` and holds:
* the riveted HUD colours
* the selection-screen pictures and icons
* the fallback tanks and backdrop

`default` is built in. For art development, launch with `--theme NAME` to preview a different asset set.

## Built-in map styles

| Style     | Biome   | Look | Generator |
|-----------|---------|------|-----------|
| `classic` | classic | Re-imagined 1987 Amiga look: pale sand, asphalt with kerbs, bush clumps, brown vented bunkers, concrete walls | Many roads, clumps and compounds; almost no water |
| `desert`  | desert  | Rippled red dunes, rutted dirt tracks, dry scrub, adobe roofs, sandstone walls | Rocky, sparse, a small oasis |
| `jungle`  | jungle  | Grass, mud tracks with puddles, murky rivers, dense canopy, rusty tin roofs, sandbag walls | Dense canopy, two meandering rivers |
| `winter`  | winter  | Snow, ploughed roads with snow berms, icy lakes, snowy pines, snow-covered roofs | Frozen lakes, pine forests, snow-capped rocks |

All four are drawn by `tools/gen_styles.py`. The art is original and MIT-licensed. The classic style follows the look of the Amiga release but copies none of its pixels, and every colour is an Amiga 12-bit value. Edit the script and rerun it, or paint over the PNGs.

## Bases, buildings and guns

Every map has two big **home bases** (34×26 tiles) in opposite corners, each a walled camp with its own roads and gates.

**Your camp:**
* three indestructible **tank garages** side by side, one per tank type, as in the original. Your tank waits in its own garage with its nose just out of the door, and comes back there after being destroyed.
* an apron and a main road to the wide south gate, an avenue to the east gate, and lanes around the back
* HQ, bio-dome, flag bunker, power plant, radar, two barracks, warehouses, a bunker, first aid and two fuel depots
* your own guns on the walls

**The enemy fortress** is the same size:
* a single garage, with the flag bunker deep inside between two heavy guns
* mortars, a rocket launcher and a helipad
* three barracks
* twin auto-cannons inside the gate and cannons on the road out

Everything else on the map belongs to the enemy:
* walled compounds with towers or guns in the corners
* outposts, POW camps and lone gun nests

The generator keeps hostile guns away from the player's base.

Destroying a manned building (barracks, HQ, bunkers, power plant, warehouse, dome, radar, towers, turrets) sends its survivors running away from you. After a few seconds they regroup and start shooting. Shoot them or run them over.

A shot at any tile of a building damages the whole building. At half health it shows its damaged picture. When it falls, the entire footprint goes up in a chain of explosions and turns into one ruin.

| Building | Tiles | Hits | Points |
|---|---|---|---|
| Watch tower | 1×1 | 2 | 25 |
| Bunker | 2×2 | 6 | 50 |
| Fuel depot (big explosion) | 2×2 | 3 | 75 |
| Barracks | 3×2 | 8 | 100 |
| Bio-dome | 3×3 | 10 | 150 |
| Command HQ | 4×3 | 16 | 250 |
| Tank garage | 3×3 | indestructible | |
| First-aid station | 2×2 | 5 | |
| POW camp | 3×3 | 6 | 100 |
| Radar station | 2×2 | 5 | 125 |
| Power plant | 3×2 | 10 | 200 |
| Warehouse | 3×2 | 8 | 100 |
| Flag bunker (holds the flag) | 2×2 | 12 | 300 |
| Helipad (drivable; its helicopter answers campers) | 3×3 | indestructible | |

**Gun turrets** turn towards the nearest enemy tank they can see. A wall, building, rock or bush in the way blocks their line of fire. As in the original, they can only point and fire along the 8 compass directions, so a tank approaching between those lines is safe from them. Mortars lob at wherever you are, and rockets home in. None of them is a perfect shot: each has some spread. Their shots hurt tanks, not the scenery, and nobody's guns hit their own side.

| Turret | Tiles | Range | Fires | Damage | Hits | Points |
|---|---|---|---|---|---|---|
| Machine-gun nest | 1×1 | 140 px | a steady stream of tracers, wide spread | 1 | 4 | 75 |
| Twin auto-cannon | 1×1 | 160 px | bursts of 4, then a pause | 2 | 5 | 100 |
| Cannon | 1×1 | 170 px | single aimed shells | 4 | 6 | 100 |
| Rocket launcher | 2×2 | 220 px | pairs of slow homing rockets | 6 | 8 | 175 |
| Heavy gun | 2×2 | 240 px | single heavy shells, very accurate | 8 | 12 | 225 |
| Mortar | 2×2 | 260 px | lobs shells **over walls** at where you were; splash | 7 | 7 | 175 |

Tanks have armour: 30, 50 or 80 by class. At zero the tank explodes and costs a life, and after three seconds a fresh one rolls out of the garage. With no lives left it's game over.

Building stats are in `struct_info` (`src/core/world.c`); turret stats are in `turret_info` (`src/core/game.c`).

## Sheet layouts

**Map style** (`data/styles/<name>/`)

| File | Size | Layout |
|------|------|--------|
| `tiles.png` (required) | 256 × 176 | 16 columns × 11 rows of 16×16. Row = tile type: sand, road, water, rock, tree, stump, wall, rubble, building, ruins, floor. For road, water, wall, building, tree and ruins the column is the neighbour mask (N=1, E=2, S=4, W=8; a bit is set where the neighbour is the same kind), so edges join up. For the other types the 16 columns are random variants. |
| `structures.png` | 192 × 960 | Multi-tile buildings. One 64×48 slot per type and state. Rows: tower (1×1 tiles), bunker (2×2), depot (2×2), barracks (3×2), dome (3×3), HQ (4×3), garage (3×3), first aid (2×2), POW camp (3×3), radar (2×2), power plant (3×2), warehouse (3×2), MG nest (1×1), cannon (1×1), rocket launcher (2×2), heavy gun (2×2), flag bunker (2×2), helipad (3×3, drivable), twin auto-cannon (1×1), mortar pit (2×2). For the garage, the top two rows are its roof; they are drawn again over the tank inside, so the tank is hidden until it rolls out. The bottom row is the front wall with the open door in the middle. The tank drives through the middle column. Turrets only show their base here; the gun is in `turrets.png`. Column 0 is intact; column 1 is damaged, shown from half health; column 2 is the remains after it falls: wall stubs, corner posts and debris, drawn over the ruins tiles, with glowing embers added by the game. Each picture covers its footprint from the slot's top-left corner, transparent where the ground shows through. Optional: imported styles borrow the sheet of their biome. |
| `vehicles.png` | 1024 × 576 | Parked vehicles, 32 directions (columns) of 32×32, facing north first, clockwise. Three rows per kind: our colours, the enemy's, burnt-out wreck. Kinds: truck, fuel tanker, jeep, APC, half-track, towed gun. Optional: imported styles borrow their biome's. |
| `turrets.png` | 1024 × 192 | Rotating gun heads: rows MG, cannon, rocket, heavy, twin, mortar; 32 directions clockwise from north; 32×32 cells, centred on the turret. Optional, like structures. |
| `tanks.png` | 1024 × 192 | Optional; tank camouflage for this terrain, same layout as in the UI theme. |
| `backdrop.png` | any | Optional; tiled behind the play field. |
| `style.txt` | | `name=` (shown in the chooser) and `biome=classic\|desert\|jungle\|winter`. `water=waves|murky|ice` picks how the water is animated. `bollards=1` makes the game draw pairs of square posts on the ground beside walls, as on the original's concrete barriers. |

**UI theme** (`data/themes/<name>/`). Missing files come from `default`.

| File | Size | Layout |
|------|------|--------|
| `tanks.png` | 1024 × 192 | 32 columns (directions clockwise from north) × 6 rows of 32×32. Row = class × 2 + team. |
| `cards.png` | 528 × 104 | Three 176×104 selection pictures: light, medium, heavy. |
| `ui.png` | 80 × 8 | 8×8 white icons, tinted when drawn: pow, tank, mine, fuel, armor, left, right, speed, up, down. |
| `backdrop.png` | any | Tiled background, used when the map style has none. |
| `men.png` | 192 × 36 | 12×12 cells. Row 0 is freed prisoners, row 1 enemy soldiers, row 2 our own soldiers (optional: without it they are drawn as row 1, tinted khaki). Built-in soldiers wear armbands in their side's colour: red for the enemy, blue for ours. 16 columns: 8 directions (N, NE, E, ...) × 2 walking frames. |
| `heli.png` | 1280 × 40 | Helicopter body in 32 directions, 40×40 cells. The rotor and the shadow are drawn by the game. |
| `theme.txt` | | See the list below. |

`theme.txt` keys:
* `name=`
* HUD colours as `rrggbb`: `panel`, `frame`, `frame_dark`, `rivet`, `label`, `value`, `bar_fuel`, `bar_armor`, `team0`, `team1`
* optional tank names: `tank_light=`, `tank_medium=`, `tank_heavy=`

## Sound effects

The SDL client loads WAV effects from `data/sounds/default/`.
* one WAV per effect: `explode`, `explode_small`, `cannon`, `mg`, `rocket`, `mortar`, `hit`, `splat`, `pickup`, `mine`, `heli`, `engine`, `splash`
* `heli` and `engine` loop

`tools/gen_sounds.py` generates these effects. `ofp_adf.py theme` also exports reference samples from your disk to `data/sounds/original/`.

## Your original disk: the "Original" style and remixes

The original graphics are copyrighted, so nothing derived from them ships with OpenFirePower. These directories are all git-ignored:
* `data/styles/original*/`
* `data/themes/classic/`

From your own Amiga disk you can build them locally:

    python3 tools/ofp_adf.py theme FirePower.adf            # Original style + classic UI theme
    python3 tools/ofp_adf.py theme FirePower.adf --remix    # + Orig. Desert / Jungle / Winter

The Original styles also get the game's own buildings (watch post, bunkers, barracks, bio-dome, command bunker), shrunk onto our footprints. `--remix` recolours the original 32-colour palette, turning sand to snow and bushes to pines, for your own use. Water and rocks, which the original lacks, come from the matching built-in style.

`extract` dumps everything:
* sprite sheets as PNG, with a `manifest.json`
* full screens with their `.gpl` palettes
* maps as CSV plus a rendered preview
* sounds as WAV
* the instruction texts

    python3 tools/ofp_adf.py extract FirePower.adf rip/

The format notes are at the top of `tools/ofp_adf.py`.

### Other versions, or doing it by hand

* **AmigaDOS disks:** `pip install amitools`, then `xdftool disk.adf unpack rip/`.
* **IFF pictures:** `python3 tools/ofp_import.py ilbm file.iff out.png`.
* **Unknown data:** decode raw bitplanes with `python3 tools/ofp_import.py planar FILE out.png --width 320 --depth 4`. Try other widths, depths and `--interleaved` until the graphics appear.
* **Packed data, or a track-loader disk:** use clean 1× low-resolution captures from your own copy of the game.

Then copy `docs/classic-mapping.example.toml` next to your PNGs and fill in the coordinates:

    python3 tools/ofp_import.py build rip/mapping.toml

Tiles go to a map style and everything else to a UI theme.

`team_camp=r,g,b` and `team_fortress=r,g,b` in style.txt: the sides' colours on screen (the
minimap, frames, scores, flags), best the tanks' own; without them, blue and red.


## World vehicle artwork

Volcano and Moon Base have their own tank silhouettes and shared simulation
profiles (`world_tank_stats`), used by all clients and the dedicated server.
`tools/gen_styles.py` draws the desktop sprites. The native Amiga banks are
created by `tools/amiga/gen_vehicle_art.py` using the existing 16-colour palette;
only the current world's bank is copied into Chip RAM. Classic tree canopies
use rounded, shaded clusters, and still cover only the portion of a tank beneath
them. Source art references, authors and licences are listed in `CREDITS.md`.
