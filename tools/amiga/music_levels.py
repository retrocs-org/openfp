#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate Amiga track volume trims without changing the source recordings."""
import argparse
import array
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def volume_trim(samples, rate=11025):
    block = rate * 2 // 5
    powers = [(sum(v * v for v in samples[i:i + block]), len(samples[i:i + block]))
              for i in range(0, len(samples), block)]
    if not powers or not max(abs(v) for v in samples):
        return 64
    gate = max(power / count for power, count in powers) * .01
    active = [(power, count) for power, count in powers if power / count >= gate]
    rms = math.sqrt(sum(power for power, _ in active) / sum(count for _, count in active))
    # Keep quieter ambience at full scale. Trim louder loops with Paula's volume
    # register, so there is no per-sample processing or extra Chip RAM allocation.
    return min(64, max(1, round(64 * 20 / rms)))


def render():
    catalog = (ROOT / 'src/common/soundtrack.h').read_text()
    entries = re.findall(r'X\((\w+), "([^"\n]+)",', catalog)
    lines = ['/* SPDX-License-Identifier: MIT',
             ' * Track trims from tools/amiga/music_levels.py; regenerate after changing PCM loops. */',
             '#ifndef OFP_AMIGA_MUSIC_LEVELS_H', '#define OFP_AMIGA_MUSIC_LEVELS_H',
             'static const unsigned char amiga_music_level[SOUNDTRACK_COUNT] = {']
    for track, file in entries:
        path = ROOT / 'data/music/amiga' / (Path(file).stem + '.pcm')
        samples = array.array('b', path.read_bytes())
        lines.append(f'    [{track}] = {volume_trim(samples)},')
    return '\n'.join(lines + ['};', '#endif', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = ROOT / 'src/platform/amiga/music_levels.h'
    text = render()
    if args.check:
        if not output.is_file() or output.read_text() != text:
            parser.exit(1, 'Amiga music levels need regeneration.\n')
    else:
        output.write_text(text)


if __name__ == '__main__':
    main()
