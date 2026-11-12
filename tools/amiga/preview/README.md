# The Amiga client, previewed on the desktop

`src/platform/amiga/local.c` built for Linux or macOS against `shim.c`: just enough
of AmigaOS 3.x (planar bitmaps, the blitter calls the client makes, the palette,
Intuition's screen buffers, a few dos.library calls) to run it and save a picture
of the screen. For checking art and screens without an Amiga.

It is an art preview with no copper, chipset timing or performance measurement (the hardware sprites the
client sets are drawn over the picture, from their lists, as the display chips would).
Text uses font8x8 instead of topaz 8 (the same size), sound is silent, there are
no physical joysticks and no public server list. The timing shim is only for
advancing the preview; benchmark and stability results must be collected from
the native build on an Amiga. The native stability test is
unavailable in this art preview.

```sh
tools/amiga/preview/build.sh                                  # the local client (OpenFirePower)
tools/amiga/preview/build_online.sh                           # the online client (menus, worlds, editor)
```

Run it from a scratch folder (the online client reads and writes
`PROGDIR:OpenFirePower.cfg`, which on the desktop is a file of that name there):

```sh
OFP_SHOT=shot.ppm OFP_SHOT_MS=3000 tools/amiga/preview/amiga-preview
```

| Variable | |
|---|---|
| `OFP_SHOT` | where the picture goes (PPM; default `amiga-preview.ppm`) |
| `OFP_SHOT_MS` | how long it runs first (default 3000) |
| `OFP_KEYS` | keys to send: `ms:code`, a raw key (`0x44` Return down, `0xc4` up), or `ms:vN`, a character (`v13`: Return in the menus) |
| `OFP_NO_SPRITES` | `1`: the picture without the hardware sprites (radar dots) |
| `OFP_JOY` | a CD32 pad in port 1: `ms:bits,...` (the JPF_ bits from then on) |

A match in a world, from the online client: put `world=3` (BIOME_*: 3 is winter),
`map=4`, `players=1` and `map_size=1` in `PROGDIR:OpenFirePower.cfg`, then

```sh
OFP_KEYS="800:0x44,900:0xc4,1600:v13,2400:v13,3200:v13,4000:0x4d,4080:0xcd,4250:0x4d,4330:0xcd,4500:0x4d,4580:0xcd,4750:0x4d,4830:0xcd,5000:v13,6000:0x44,6100:0xc4,6700:0x44,6800:0xc4" \
OFP_SHOT=winter.ppm OFP_SHOT_MS=10800 ./amiga-preview-online
```

(the title, PLAY, LOCAL GAME, down to START GAME, then FIRE twice in the team setup).
