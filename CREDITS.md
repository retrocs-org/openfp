# Credits

OpenFirePower's code is MIT (see LICENSE). The game's art, sounds and maps are its own
and MIT too. These are other people's work, each under its own licence:

## Libraries (in third_party/)

| What | Licence | From |
|---|---|---|
| libxmp-lite 4.7.4: plays the tracker music | MIT | https://github.com/libxmp/libxmp |
| stb_image and stb_vorbis | MIT or public domain | https://github.com/nothings/stb |
| dr_mp3 | MIT-0 or public domain | https://github.com/mackron/dr_libs |

## Soundtrack

The files in `data/music/original/` are the source downloads. The Amiga
versions in `data/music/amiga/` are adapted to 11,025 Hz signed 8-bit mono
with adjusted levels. The menu uses the complete loop cut. The world tracks
use excerpts with a short crossfade at the loop point. Each Amiga adaptation
is distributed under the same licence as its source.

Commando Team (Action) © 2012 Grégoire Lourme, with the loop edit © 2012
Iwan Gabovitch. [Composer's page](https://www.jamendo.com/artist/560/gregoire-lourme)

| Where | Original file | Track and source | Author | Licence |
|---|---|---|---|---|
| Main Menu | `menu.ogg` | [Commando Team (Action) [loop cut]](https://opengameart.org/content/commando-team-action-loop-cut) | Grégoire Lourme and Iwan Gabovitch (loop edit) | [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) |
| Classic | `classic.mp3` | [Shattered Reality – Epic Orchestral Dubstep](https://opengameart.org/content/shattered-reality-epic-orchestral-dubstep) | TheMightyRager | [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) |
| Moon Base | `moon.ogg` | [Sci-Fi Ambient – Crashed Ship](https://opengameart.org/content/sci-fi-ambient-crashed-ship) | Ted Kerr (Wolfgang_) | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| Winter | `winter.ogg` | [Cold Grounds](https://opengameart.org/content/cold-grounds) | Anipatok | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Desert | `desert.mp3` | [Desert Wasteland](https://opengameart.org/content/desert-wasteland) | tcarisland | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Jungle | `jungle.mp3` | [Chill Jungle Ambient](https://opengameart.org/content/chill-jungle-ambient) | Tausdei | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| Swamp | `swamp.ogg` | [Swamp Environment Audio](https://opengameart.org/content/swamp-environment-audio) | LokiF | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| Volcano | `volcanic.wav` | [Entering the Volcano (Ambience of a Fallen Age)](https://opengameart.org/content/ambience-of-a-fallen-age) | Umplix | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| Wasteland | `wasteland.mp3` | [Space Graveyard – Ambient Track](https://opengameart.org/content/space-graveyard-ambient-track) | TinyWorlds | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

## Credits music and art references

**Legion's Rally**, from **Oblivion Symphonic Variations**, by **Li-Hsin Huang
(Improbable Cadence)** plays during the scrolling credits.
[Source](https://opengameart.org/content/legions-rally).
Includes music from Oblivion Symphonic Variations, by Li-Hsin Huang, licensed
under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The page's licence selector says CC BY 3.0; its explicit author attribution
notice specifies CC BY 4.0, which is the notice followed here. The original MP3
is retained in `data/music/original/credits.mp3`; the Amiga adaptation uses the
complete track at 11,025 Hz signed 8-bit mono, normalized with a loop crossfade.
`tools/music_to_pcm.c` reproduces that conversion.

The following references inspired new pixel art drawn in code for OpenFirePower.
The original downloads are retained in `data/art/original/`, under their own
licences; those reference files are not used as runtime sprites.

| Use | Reference | Author | Licence |
|---|---|---|---|
| Volcano tank silhouettes | [2D Tanks Top Down](https://opengameart.org/content/2d-tanks-top-down) | Alucard | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| Moon Base tank silhouettes | [Top down painted tanks](https://opengameart.org/content/top-down-painted-tanks) | irmirx | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| Classic rounded tree canopy | [top-down tree](https://opengameart.org/content/top-down-tree) | C-MAN | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |

## Profile avatars

`data/avatars.png`: ten 24 x 24 portraits drawn by `tools/make_avatars.py` for OpenFirePower,
pixel by pixel in code. CC0 (no rights reserved).

## Sound effects

`data/sounds/original/laugh-evil-1.ogg` is **Evil Laugh** by AntumDeluge,
released under CC0. The Amiga skull uses a trimmed, mono 8-bit conversion.
[OpenGameArt](https://opengameart.org/content/evil-laugh)
