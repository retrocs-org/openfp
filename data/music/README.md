# Soundtrack

`original/` contains the original OpenGameArt downloads for the eight worlds
and the main menu. The desktop plays them through the game's audio mixer.

`amiga/` contains the Amiga playback loops as 11,025 Hz signed mono 8-bit PCM.
The menu uses the original 22.59-second loop cut. The world tracks use about
40 seconds each with a short crossfade at the loop point. The game reads
these files from `PROGDIR:music/` as they play.

## Custom desktop tracks

Put a tracker module named `classic`, `moon`, `winter`, `desert`, `jungle`,
`swamp`, `volcanic`, `wasteland` or `menu` in the `music` folder beside
`settings.ini`. The supported extensions are `.mod`, `.xm`, `.s3m` and `.it`.
Those modules take priority over the included tracks.

## Credits

See [CREDITS.md](../../CREDITS.md) for the authors, sources and licences.
`credits.txt` supplies track titles and credits to the desktop client.
