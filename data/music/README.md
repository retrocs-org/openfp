# Soundtrack

`original/` contains the original OpenGameArt downloads for the eight worlds,
the main menu and credits. The desktop prepares only the selected track in a
background loader, keeping the playing track and its next transition in memory.
The audio callback does not open files, decode recordings or free track buffers.
Muted music is prepared when it is enabled again.

Desktop recordings receive a fixed adjustment measured over active 400 ms blocks,
with a peak limit to leave room for effects. Quiet intros and pauses stay intact;
there is no constantly changing volume or modification of the original downloads.
Custom tracker modules retain their authored levels.

`amiga/` contains the Amiga playback loops as 11,025 Hz signed mono 8-bit PCM.
The menu uses the original 22.59-second loop cut. The world tracks use about
40 seconds each with a short crossfade at the loop point. Through the Snow
keeps its complete seamless loop. The game reads
these files from `PROGDIR:music/` as they play.

## Map soundtracks

In either map editor, open **Options > Soundtrack**. Choices stay with the map,
including network matches and replays. Old maps keep their existing default track.

| World | Soundtracks |
|---|---|
| Classic | Shattered Reality |
| Moon Base | Crashed Ship; Space Atmosphere |
| Winter | Cold Grounds; Through the Snow |
| Desert | Desert Wasteland; Lost in the Desert; Desert of Dreams Mix |
| Jungle | Chill Jungle Ambient; Urban Jungle 2061; Jungle |
| Swamp | Swamp Environment Audio |
| Volcano | Entering the Volcano; Iron Wasteland |
| Wasteland | Space Graveyard; Cannon Tube; A New Beginning |

## Reproducing the new Amiga loops

Build `tools/music_to_pcm.c` with a host C compiler. It accepts MP3, Ogg Vorbis,
and 16-bit PCM WAV sources. Cannon Tube uses the author's
medium loop; Through the Snow uses the original seamless loop.

```sh
cc -O2 tools/music_to_pcm.c -lm -o /tmp/ofp-music-to-pcm
/tmp/ofp-music-to-pcm data/music/original/cannon-tube-loop.ogg data/music/amiga/cannon-tube.pcm 40
/tmp/ofp-music-to-pcm data/music/original/snow-theme.ogg data/music/amiga/snow-theme.pcm --seamless
/tmp/ofp-music-to-pcm data/music/original/space-atmosphere.mp3 data/music/amiga/space-atmosphere.pcm 40
/tmp/ofp-music-to-pcm data/music/original/new-beginning.ogg data/music/amiga/new-beginning.pcm 40
/tmp/ofp-music-to-pcm data/music/original/lost-in-the-desert.mp3 data/music/amiga/lost-in-the-desert.pcm 40
/tmp/ofp-music-to-pcm data/music/original/desert-of-dreams-mix.ogg data/music/amiga/desert-of-dreams-mix.pcm 40
/tmp/ofp-music-to-pcm data/music/original/urban-jungle-2061.mp3 data/music/amiga/urban-jungle-2061.pcm 40
/tmp/ofp-music-to-pcm data/music/original/jungle-pro-sensory.wav data/music/amiga/jungle-pro-sensory.pcm 40
/tmp/ofp-music-to-pcm data/music/original/iron-wasteland.ogg data/music/amiga/iron-wasteland.pcm 40
```

The Amiga streams the selected loop through its existing small audio buffers;
the added tracks do not increase its audio buffer allocation. Louder loops have
per-track hardware volume trims, while quieter ambience keeps its maximum range.
This adds no per-sample processing. Regenerate the small volume table after
changing any Amiga loop:

```sh
python3 tools/amiga/music_levels.py
python3 tools/amiga/music_levels.py --check
```

The Amiga build updates this table automatically when its inputs change.

## Custom desktop tracks

Put a tracker module named `classic`, `moon`, `winter`, `desert`, `jungle`,
`swamp`, `volcanic`, `wasteland` or `menu` in the `music` folder beside
`settings.ini`. The supported extensions are `.mod`, `.xm`, `.s3m` and `.it`.
Those modules take priority over the included tracks.

## Credits

See [CREDITS.md](../../CREDITS.md) for the authors, sources and licences.
`credits.txt` supplies track titles and credits to the desktop client.
