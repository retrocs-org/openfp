# Packages

| Where | File | Build it |
|---|---|---|
| Linux ARM64 / x86_64 | portable `.tar.gz`, Ubuntu 24.04 `.deb` | `sh tools/package_linux.sh` on Linux |
| Windows x64 | portable ZIP and MSI, including QA kit | `sh tools/package_windows.sh --msi` |
| macOS universal | app, DMG | `sh tools/package_macos.sh` |
| AmigaOS 16 colours | Workbench installer `.lha` | `sh tools/amiga/package_lha.sh 0.1 build-amiga-ecs` |
| tvOS preview | simulator ZIP / unsigned device app | `sh tools/package_tvos.sh simulator` or `device`; Xcode signs device builds |
| openSUSE (OBS), Fedora | `openSUSE/openfirepower.spec` | `osc build` in an OBS checkout with the release archive beside the spec; locally `rpmbuild -ba` |
| macOS (Homebrew) | `homebrew/openfirepower.rb` | development source: `brew install --HEAD <your-tap>/openfirepower`; add a stable URL and its verified checksum in the tap after publication |
| Linux desktops (Flatpak) | `flatpak/org.openfirepower.OpenFirePower.yml` | `flatpak-builder --user --install build-flatpak packaging/flatpak/org.openfirepower.OpenFirePower.yml` |
| any systemd Linux | `systemd/ofp-server.service`, `systemd/ofp-server.sysusers` | installed with `-DOFP_INSTALL_SYSTEMD=ON` |

All of them build the same way underneath:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr -DOFP_DATA_DIR=/usr/share/openfirepower -DOFP_INSTALL_SYSTEMD=ON
cmake --build build
ctest --test-dir build
DESTDIR=/tmp/pkg cmake --install build
```

which puts in place:

```
/usr/bin/openfirepower, ofp_server, ofp_match
/usr/share/openfirepower/          the art, sounds, maps (the original game's levels are never packaged)
/usr/share/applications/…desktop, /usr/share/metainfo/…xml, the icon
/usr/share/doc/openfirepower/      README, CHANGELOG, server.cfg.example, docs/
/usr/lib/systemd/system/ofp-server.service, /usr/lib/sysusers.d/ofp-server.conf
/etc/ofp-server/server.cfg         (from server.cfg.example; the server's settings)
```

The game finds its data where `OFP_DATA_DIR` says, and also in `../share/openfirepower`
next to its own program, so a relocated install works too.

The server's service runs as the user `ofp-server` in `/var/lib/ofp-server` (its
database, log and backups go there with the relative paths in `server.cfg`), with the
usual systemd hardening. `systemctl reload ofp-server` sends SIGHUP: settings and maps
are read again and the log file reopened.

```sh
sudo systemctl enable --now ofp-server
journalctl -u ofp-server -f
```
