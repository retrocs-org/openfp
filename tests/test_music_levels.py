#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('levels', ROOT / 'tools/amiga/music_levels.py')
levels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(levels)


class MusicLevels(unittest.TestCase):
    def test_hardware_limits_and_silence(self):
        self.assertEqual(levels.volume_trim([0] * 11025), 64)
        self.assertEqual(levels.volume_trim([10, -10] * 11025), 64)
        self.assertEqual(levels.volume_trim([40, -40] * 11025), 32)
        # A block straddling the intro may slightly change the fixed trim.
        trim = levels.volume_trim([0] * 11025 + [40, -40] * 11025)
        self.assertGreaterEqual(trim, 32)
        self.assertLessEqual(trim, 35)

    def test_shipped_table_matches_loops(self):
        self.assertEqual((ROOT / 'src/platform/amiga/music_levels.h').read_text(), levels.render())


if __name__ == '__main__':
    unittest.main()
