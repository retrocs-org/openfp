#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Integration checks: OFP_MATCH=/path/to/ofp_match python3 tests/test_ai_tournament.py."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tournament', ROOT / 'tools/ai_tournament.py')
tournament = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tournament)
ENGINE = os.environ.get('OFP_MATCH')
CTF = ROOT / 'data/maps/ctf-duel-at-dawn.ofpmap'
CLASSIC = ROOT / 'data/maps/classic/dust-crossing.ofpmap'


class CatalogTests(unittest.TestCase):
    def test_mode_restrictions(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'map.ofpmap'
            for header, expected in [
                ('biome=classic\nmodes=flag,conquest', ['flag']),
                ('biome=jungle\nmode=hardpoint\nmodes=flag,conquest', ['hardpoint']),
                ('biome=winter\nmodes=conquest', ['conquest', 'hardpoint']),
                ('biome=desert\nmodes=deathmatch warlord', ['deathmatch', 'killconfirmed', 'warlord']),
                ('biome=moon\narena=yes', ['annihilation']),
                ('biome=scifi\nmodes=flag', []),
            ]:
                with self.subTest(header=header):
                    path.write_text('ofpmap 1\n' + header + '\ntiles\n')
                    self.assertEqual(tournament.map_goals(path), expected)


@unittest.skipUnless(ENGINE, 'set OFP_MATCH to the freshly built match tool')
class EvaluationTests(unittest.TestCase):
    def evaluate(self, map_path=CTF, profile='server', extra=()):
        run = subprocess.run([ENGINE, 'evaluate', '--map', str(map_path), '--minutes', '1',
                              '--profile', profile, '--side', '4', '--seed', '7919', *extra],
                             capture_output=True, text=True, check=True)
        return json.loads(run.stdout)

    def test_deterministic_statistics(self):
        first = self.evaluate()
        self.assertEqual(first, self.evaluate())
        self.assertLessEqual(first['side'] * 2, 8)
        for stats in first['teams']:
            self.assertLessEqual(stats['alive_ticks'], first['ticks'] * first['side'])
            for key in ['home_ticks', 'enemy_base_ticks', 'repair_ticks', 'attack_ticks', 'carrying_ticks']:
                self.assertLessEqual(stats[key], stats['alive_ticks'])

    def test_classic_does_not_gain_enhanced_bot_flags(self):
        server = self.evaluate(CLASSIC)
        local = self.evaluate(CLASSIC, 'local')
        self.assertEqual(server['checksum'], local['checksum'])
        self.assertEqual(server['teams'], local['teams'])

    def test_recorded_inputs_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            replay = Path(tmp) / 'test.ofprep'
            self.evaluate(profile='local', extra=['--out', str(replay)])
            checked = subprocess.run([ENGINE, 'verify', str(replay)], capture_output=True, text=True, check=True)
            self.assertIn('checksums match', checked.stdout)

    def test_bad_limits_rejected(self):
        for option, value in [('--seed', '999999999999999999999'), ('--seed', '-1'),
                              ('--minutes', '31'), ('--side', '5'), ('--level', 'invalid')]:
            with self.subTest(option=option, value=value):
                result = subprocess.run([ENGINE, 'evaluate', '--map', str(CTF), option, value], capture_output=True)
                self.assertEqual(result.returncode, 2)

    def test_parallel_runner_snapshots_and_reports(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'run'
            subprocess.run([sys.executable, str(ROOT / 'tools/ai_tournament.py'), '--engine', ENGINE,
                            '--map', str(CTF), '--minutes', '1', '--matches', '2', '--jobs', '2',
                            '--out', str(output)], check=True, capture_output=True)
            rows = [json.loads(line) for line in (output / 'results.jsonl').read_text().splitlines()]
            self.assertEqual(len(rows), 2)
            self.assertEqual({row['status'] for row in rows}, {'ok'})
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['engine_sha256'], tournament.digest(output / 'engine/ofp_match'))
            self.assertTrue((output / manifest['maps'][0]['snapshot']).is_file())
            self.assertIn('home_pct', (output / 'matches.csv').read_text())
            self.assertIn('Completed: 2 matches.', (output / 'report.md').read_text())


class ShutdownTests(unittest.TestCase):
    def run_sleeper(self, budget=False):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            fake = folder / 'slow-engine'
            fake.write_text('#!/usr/bin/env python3\nimport time\ntime.sleep(30)\n')
            fake.chmod(0o755)
            output = folder / 'results'
            run = subprocess.run([sys.executable, str(ROOT / 'tools/ai_tournament.py'), '--engine', str(fake),
                                  '--map', str(CTF), '--matches', '1', '--jobs', '1',
                                  '--timeout', '30' if budget else '.05', '--hours', '.0001' if budget else '1',
                                  '--out', str(output)], capture_output=True, text=True, timeout=8)
            rows = [json.loads(line) for line in (output / 'results.jsonl').read_text().splitlines()]
            self.assertEqual(len(rows), 1)
            self.assertFalse(list((output / 'replays').glob('*.ofprep')))
            return run.returncode, rows[0]['status']

    def test_per_match_timeout_is_failure(self):
        self.assertEqual(self.run_sleeper(), (1, 'timeout'))

    def test_time_budget_stops_cleanly(self):
        self.assertEqual(self.run_sleeper(budget=True), (0, 'budget'))


if __name__ == '__main__':
    unittest.main()
