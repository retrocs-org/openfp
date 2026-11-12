# SPDX-License-Identifier: MIT
"""Offline package tests. Compiler commands write fixture text, never game binaries."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from release_assets import package_name, checksum
from release_build import catalog, Project
from release_common import config, ReleaseError

ROOT = Path(__file__).resolve().parents[1]


class AssetTests(unittest.TestCase):
    def test_names_and_release_inventory(self):
        matrix = [
            ('client', 'macos', 'universal', 'zip'), ('client', 'macos', 'universal', 'dmg'),
            ('client', 'windows', 'x86_64', 'zip'), ('client', 'windows', 'x86_64', 'msi'),
            ('client', 'amigaos', 'm68k', 'lha'),
        ] + [(role, 'linux', arch, 'tar.gz') for role in ('client', 'server') for arch in ('x86_64', 'aarch64')]
        cfg = config(ROOT, ROOT / 'examples/release-config.json')
        names = {name for target in catalog(ROOT, cfg, '0.1').values() for name, _ in target['files'] if not name.endswith('.readme')}
        expected = {f'openfp-{role}-{platform}-{arch}-0.1.{ext}' for role, platform, arch, ext in matrix}
        self.assertEqual(names, expected)
        for role, platform, arch, ext in matrix:
            self.assertIn(package_name(role, platform, arch, '0.1', ext), expected)
        for version in ('../bad', '0.1-dev', '0.1\n', ''):
            with self.assertRaises(ValueError): package_name('client', 'macos', 'universal', version, 'zip')
        with self.assertRaises(ValueError): package_name('server', 'windows', 'x86_64', '0.1', 'zip')

    def test_checksums_work_after_moving_downloads(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'package files'; folder.mkdir()
            asset = folder / 'openfp-client-macos-universal-0.1.zip'
            asset.write_bytes(b'fixture archive bytes')
            sidecar = checksum(asset)
            self.assertEqual(sidecar.read_text(), hashlib.sha256(asset.read_bytes()).hexdigest() + '  ' + asset.name + '\n')
            self.assertNotIn(directory, sidecar.read_text())
            asset.write_bytes(b'changed bytes')
            checksum(asset)
            self.assertTrue(sidecar.read_text().startswith(hashlib.sha256(b'changed bytes').hexdigest()))


class LinuxPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ofp-package-fixtures-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve() / 'source'
        self.root.mkdir()
        for rel in ('tools/package_linux.sh', 'scripts/release_assets.py'):
            target = self.root / rel; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, target)
        self.write('src/common/net.h', '#define OFP_VERSION "0.1"\n')
        for rel in ('LICENSE', 'CREDITS.md', 'THIRD-PARTY-NOTICES.md', 'server.cfg.example',
                    'docs/BUILDING.md', 'docs/NETWORK.md', 'packaging/linux/README.txt',
                    'data/maps/test.ofpmap', 'data/styles/test.txt', 'data/themes/test.txt',
                    'data/sounds/test.txt', 'data/music/test.txt', 'data/gamecontrollerdb.txt', 'data/avatars.png'):
            self.write(rel, 'fixture ' + rel)
        self.bin = self.root / 'fixture-tools'; self.bin.mkdir()
        self.write('fixture-tools/uname', '#!/bin/sh\ncase "$1" in -s) echo Linux ;; -m) echo x86_64 ;; esac\n', executable=True)
        self.write('fixture-tools/cmake', '#!' + sys.executable + '\n' + '''import json, os, pathlib, sys
args = sys.argv[1:]
with open(os.environ['FIXTURE_COMMANDS'], 'a') as log: log.write(json.dumps(args) + '\\n')
if '-B' in args:
    directory = pathlib.Path(args[args.index('-B') + 1]); directory.mkdir(parents=True, exist_ok=True)
    (directory / 'client-enabled').write_text(str('-DOFP_BUILD_SDL2=ON' in args))
else:
    directory = pathlib.Path(args[args.index('--build') + 1])
    names = ['ofp_server', 'ofp_match', 'ofp_bot']
    if (directory / 'client-enabled').read_text() == 'True': names.append('openfirepower')
    for name in names:
        file = directory / name; file.write_text('fixture executable ' + name); file.chmod(0o755)
''', executable=True)
        self.ready = self.root / 'ready files'; self.packages = self.root / 'package files'
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        PYTHON=sys.executable, WORK=str(self.root / 'compiler work'),
                        OFP_BUILD_DIR=str(self.ready), OFP_PACKAGE_DIR=str(self.packages),
                        FIXTURE_COMMANDS=str(self.root / 'commands.jsonl'), ARCH='x86_64', COPYFILE_DISABLE='1')

    def write(self, rel, text, executable=False):
        file = self.root / rel; file.parent.mkdir(parents=True, exist_ok=True); file.write_text(text)
        if executable: file.chmod(0o755)

    def invoke(self, *args):
        result = subprocess.run(['sh', 'tools/package_linux.sh', *args], cwd=self.root, env=self.env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def archive(self, role):
        name = package_name(role, 'linux', 'x86_64', '0.1', 'tar.gz')
        path = self.packages / name
        self.assertEqual(path.with_name(name + '.sha256').read_text(), hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + name + '\n')
        with tarfile.open(path) as tar: return {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}

    def test_separate_client_and_server_payloads_and_validation(self):
        self.invoke()
        client, server = self.archive('client'), self.archive('server')
        self.assertIn('OpenFirePower/openfirepower', client)
        self.assertNotIn('OpenFirePower/Tools/ofp_server', client)
        self.assertIn('OpenFirePowerServer/ofp_server', server)
        self.assertIn('OpenFirePowerServer/server.cfg.example', server)
        self.assertIn('OpenFirePowerServer/data/maps/test.ofpmap', server)
        self.assertFalse(any('styles/' in name or name.endswith('/openfirepower') for name in server))
        self.assertEqual(len(list(self.packages.iterdir())), 4)
        project = object.__new__(Project)
        project.root, project.packages = self.root, self.packages
        project.targets = {'linux-x86_64': {'dest': self.ready, 'files': [(package_name('server', 'linux', 'x86_64', '0.1', 'tar.gz'), '')]}}
        project.validate_linux_server('linux-x86_64')
        (self.ready / 'OpenFirePowerServer/ofp_server').write_text('changed')
        with self.assertRaises(ReleaseError): project.validate_linux_server('linux-x86_64')

    def test_headless_server_never_enables_sdl_or_packages_client(self):
        self.invoke('--server-only')
        self.assertIn('-DOFP_BUILD_SDL2=OFF', (self.root / 'commands.jsonl').read_text())
        self.assertIn('OpenFirePowerServer/ofp_server', self.archive('server'))
        self.assertFalse((self.ready / 'OpenFirePower').exists())
        self.assertEqual(len(list(self.packages.iterdir())), 2)

    def test_cannot_label_native_x86_build_as_arm(self):
        self.env['ARCH'] = 'aarch64'
        result = subprocess.run(['sh', 'tools/package_linux.sh'], cwd=self.root, env=self.env, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'commands.jsonl').exists())


if __name__ == '__main__': unittest.main()
