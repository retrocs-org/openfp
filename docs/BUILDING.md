# Building OpenFP

Use a tagged full source archive, or clone the repository:

```sh
git clone https://github.com/retrocs-org/openfp.git
cd openfp
```

Run the platform commands below from the directory containing `CMakeLists.txt`.
The desktop clients and dedicated server use CMake 3.16 or newer and a C99
compiler. The archive includes the game data, generated Amiga graphics and
vendored libraries.

## Linux client (openSUSE Leap 16)

openSUSE Leap 16 is the preferred Linux build and test platform for both the
desktop client and dedicated server. Other distributions can build from source
with equivalent development packages.

```sh
sudo zypper refresh
sudo zypper install gcc make cmake pkgconf-pkg-config python3 SDL2-devel sqlite3-devel libcurl-devel ca-certificates
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel "$(nproc)"
./build-linux/openfirepower
```

To build ready-to-run folders and separate client/server archives, run:

```sh
sh tools/package_linux.sh
```

Run natively on x86_64 Linux for Intel/AMD packages or aarch64 Linux for ARM64.
This writes `openfp-client-linux-ARCH-0.1.tar.gz` and
`openfp-server-linux-ARCH-0.1.tar.gz`, each with a `.sha256` file.
The client archive has `OpenFirePower/Play`; the separate server archive has
`OpenFirePowerServer/ofp_server`, configuration examples and maps.
Build and test release archives on openSUSE Leap 16 for each architecture.
The archives use the system libraries from their build host. Other distributions
may need a source build. On Debian or Ubuntu, `--deb` also creates an optional
local DEB; it is not a release asset or the preferred Linux package format.

This also builds the dedicated server and tests. To run the tests on a machine
without a display or sound device, use SDL's dummy drivers.

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest --test-dir build-linux --output-on-failure
```

## Linux dedicated server (headless)

```sh
sudo zypper refresh
sudo zypper install gcc make cmake pkgconf-pkg-config python3 sqlite3-devel libcurl-devel ca-certificates
cmake -S . -B build-server -DCMAKE_BUILD_TYPE=Release -DOFP_BUILD_SDL2=OFF -DOFP_BUILD_TESTS=OFF
cmake --build build-server --parallel "$(nproc)"
./build-server/ofp_server --version
```

For a new private server, copy the example configuration and set `public`,
`share_matches` and `share_replays` to `no`. The example enables all three. Set
a `password` if you want to limit who can join. Use a separate configuration
file or keep your existing one when upgrading.

```sh
cp -n server.cfg.example server.cfg
${EDITOR:-vi} server.cfg
./build-server/ofp_server -c server.cfg --check
./build-server/ofp_server -c server.cfg
```

Players can join by the server's address and UDP port `29870`. Allow that UDP
port through the firewall for remote players and forward it on the router if
they connect from outside the network. Build server and clients from the same
source version so their protocol and rules fingerprints match.

To run it as a systemd service, stop the foreground server with Ctrl+C, then
install the service using the configured directory as its source. The installer
copies the configuration to `/etc/ofp-server/server.cfg` and existing server
data to `/var/lib/ofp-server/`.

```sh
sudo sh tools/install_server_service.sh --from "$PWD" --no-start
sudo systemctl enable --now ofp-server
sudo tail -F /var/lib/ofp-server/ofp_server.log
```

The service runs with `--quiet`, so normal game logs go to `log_file` in
`server.cfg`. A relative path is under `/var/lib/ofp-server/`. Use
`sudo journalctl -u ofp-server -f` for service startup errors and restarts.

`OFP_SERVER_SQLITE=OFF` disables persistent SQLite statistics. `OFP_WITH_CURL=OFF`
disables HTTPS integration on Linux for a direct-connect-only build. The headless
build also works on macOS with its system libraries or the Homebrew dependencies.

## macOS client

Install the Xcode Command Line Tools with `xcode-select --install` if needed.

```sh
brew install cmake sdl2
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DOFP_BUILD_SERVER=OFF -DOFP_BUILD_TESTS=OFF
cmake --build build-mac --parallel
./build-mac/openfirepower
```

To create a universal app for Apple Silicon and Intel, build the DMG and ZIP
with the following commands. The packaging script bundles SDL2. Set `SIGN_ID`
and `NOTARY_PROFILE` to sign and notarize the DMG with your Developer ID.

```sh
sh tools/package_macos.sh
```

## Windows x64 client

Cross-build the Windows client on macOS or Linux using MinGW-w64. Install the
following dependencies for your build host. MSI packaging also needs `wixl`,
available from `msitools` on openSUSE Leap 16 or Homebrew.
On other Linux distributions, install the package providing `wixl`.

```sh
# macOS
brew install cmake mingw-w64 python msitools

# openSUSE Leap 16
sudo zypper install gcc make cmake mingw64-cross-gcc mingw64-cross-binutils curl python3 msitools
```

```sh
sh tools/package_windows.sh --msi
```

The output is a runnable folder containing `openfirepower.exe`, SDL2 and game
data, plus `openfp-client-windows-x86_64-0.1.zip` and `.msi` packages and their
`.sha256` files. Omit `--msi` to create the ZIP only. `SDL2_MINGW_DIR` can reuse
an existing Windows x64 SDL2 SDK. Output locations are listed below.

The portable folder and MSI include `windows-qa.ps1` and `WINDOWS-TESTS.md`.
Run the automated tests in PowerShell from the extracted game folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows-qa.ps1 -GameFolder .
```

Logs and isolated test profiles go into a dated folder on the Desktop. Follow
the checklist for the MSI, fullscreen, physical controllers and network play.
Those Windows checks remain pending until performed on Windows.

## Apple TV / tvOS preview

Requires a Mac with full Xcode, the tvOS SDK and, for the simulator, its tvOS
runtime. The preview targets tvOS 15 or newer and an extended game controller.
It shares the desktop client and uses the game's controller-driven keyboard.

```sh
WORK=build-tvos/simulator sh tools/package_tvos.sh simulator
WORK=build-tvos/device sh tools/package_tvos.sh device
```

The script downloads and verifies SDL2 2.32.10, keeping its SDK under
`~/Development/tools/tvos/` (`TVOS_TOOLS` overrides that location). The ARM64
simulator app is for Apple Silicon Macs. Boot an Apple TV simulator in Xcode,
then install and launch it:

```sh
xcrun simctl install booted build-tvos/simulator/OpenFirePower.app
xcrun simctl launch booted org.openfirepower.OpenFirePower
```

The device app is **unsigned**. Open `build-tvos/device/xcode/openfirepower.xcodeproj`,
select your Apple development team and Apple TV, then Run or Archive in Xcode.
This is a testing preview, not an App Store release. Profiles, settings and maps
use local cache storage that tvOS may erase; cloud saves, distribution artwork
and a physical Apple TV/controller test remain to be completed.

## AmigaOS client (16 colours)

Put Bebbo's `m68k-amigaos-gcc` toolchain with libnix and the Amiga headers on
your `PATH`. The release targets AmigaOS 3.0 or newer and runs on 68040 and 68060 CPUs.
All worlds use the same 16-colour renderer, including on AGA machines.
Higher-colour graphics are deferred to a future version. The native game needs
2 MB Chip RAM and at least 8 MB Fast RAM; 16 MB Fast RAM is recommended.

```sh
export PATH="$HOME/Development/tools/amiga/m68k-amigaos-gcc-16.2/bin:$PATH"
make -f src/platform/amiga/Makefile COMPILER=gcc CPU=68040 -j4 online local
sh tools/amiga/stage_maps.sh build-amiga-ecs
sh tools/amiga/stage_music.sh build-amiga-ecs
```

For the HTTPS public server list, install the AmiSSL 4.x developer SDK on the
build host and add `AMISSLINC=/path/to/AmiSSL/Developer/include` to the `make`
command. Install AmiSSL 4.10–4.12 and its certificates on the target Amiga.
Without those SDK headers, the executable can still join Private servers by
address through the running TCP/IP stack, but cannot fetch the HTTPS list.

`build-amiga-ecs/OpenFirePowerOnline` is the main client with menus, the map
editor and networking through `bsdsocket.library`. `build-amiga-ecs/OpenFirePower`
is the standalone local game. Copy the executable with the `maps/` and `music/`
folders to the Amiga. Both clients ensure a 64 KB stack when launched from
the Shell or Workbench. See [PORTING.md](PORTING.md) for native controls and
alternate toolchains.

With `lha` and Python 3 with Pillow installed, create the Workbench installer
archive and its Aminet description in the packages directory.

```sh
sh tools/amiga/package_lha.sh 0.1 build-amiga-ecs
```

Pass `--uploader EMAIL` before the version when you prepare an Aminet upload.

## Manual package builds without publishing

These commands compile/package only. They do not create a tag, contact release
services, upload files or change the version. Version **0.1** stays fixed until
explicitly changed. Run from your source checkout.

On macOS, build the universal client and Windows x86_64 client:

```sh
SIGN_ID=- NOTARY_PROFILE= sh tools/package_macos.sh
sh tools/package_windows.sh --msi
```

`SIGN_ID=-` uses local ad-hoc signing for testing. Public Mac releases use the
Developer ID/notarization policy in the release configuration.

For AmigaOS, use your installed cross-compiler, LHA tool and Python with Pillow.
For this workspace:

```sh
export AMIGA_CC="$HOME/Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/m68k-amigaos-gcc"
export LHA="$HOME/Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/lha"
export PYTHON="$HOME/Development/tools/python/bin/python"
sh tools/package_amiga.sh
```

This builds both 16-colour clients for 68040 (also runs on 68060), stages maps and
music and creates `openfp-client-amigaos-m68k-0.1.lha` and its checksum. The
Aminet readme uses the same basename. An uploader email is only needed when you
choose to prepare an Aminet submission.

On each Linux architecture you want to test:

```sh
sh tools/package_linux.sh
```

For a server-only host without SDL:

```sh
sh tools/package_linux.sh --server-only
```

Apple TV remains a testing preview; use the tvOS commands above. It is excluded
from the release assets.

## Output folders

The package scripts keep compilation work under `work/package/` in the checkout,
SDK downloads under `~/Development/tools/`, ready-to-test builds under
`~/Development/builds/OpenFirePower/`, and packages under
`~/Development/packages/OpenFirePower/`.

| Platform | Ready to test (under builds) | Packages (under packages) |
| --- | --- | --- |
| macOS | `macOS/OpenFirePower.app` | `macOS/openfp-client-macos-universal-0.1.{zip,dmg}` |
| Windows | `Windows/OpenFirePower/openfirepower.exe` | `Windows/openfp-client-windows-x86_64-0.1.{zip,msi}` |
| AmigaOS | `AmigaOS/ECS/OpenFirePowerOnline` and `OpenFirePower` | `AmigaOS/openfp-client-amigaos-m68k-0.1.lha` |
| Linux | `Linux/ARCH/OpenFirePower/Play` | `Linux/ARCH/openfp-client-linux-ARCH-0.1.tar.gz` |
| Linux server | `Linux/ARCH/OpenFirePowerServer/ofp_server` | `Linux/ARCH/openfp-server-linux-ARCH-0.1.tar.gz` |

`ARCH` is `x86_64` or `aarch64`. Every binary package has a neighbouring
`FILENAME.sha256` containing its hash and basename. Verify from its directory
with `shasum -a 256 -c FILENAME.sha256` (macOS) or
`sha256sum -c FILENAME.sha256` (Linux).

`WORK`, `OFP_BUILD_DIR` and `OFP_PACKAGE_DIR` override intermediate, runnable and
package directories respectively. Ready builds retain their existing executable,
application-ID and settings filenames for compatibility; their visible name is
**OpenFP**.

The reviewed release tool uses its own isolated work directory and verifies the
packages before copying them to the configured builds/packages directories.
See [RELEASING.md](RELEASING.md) for that separate publication workflow.
