<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../inc/site.php';
$version = site_release_version();
$downloads = site_releases();
$source = $downloads['groups']['source'] ?? null;
page_start('Source', 'source', 'Build OpenFP for AmigaOS, macOS, Linux and Windows, or build the Linux dedicated server.');
?>
<section class="wrap prose">
  <h1>Source</h1>
  <p class="lead">Browse the code, download a source release, or build a client and the Linux dedicated server.</p>
  <p class="tags build-links"><a class="tag" href="#amiga">AmigaOS</a> <a class="tag" href="#macos">macOS</a>
    <a class="tag" href="#linux">Linux</a> <a class="tag" href="#windows">Windows</a>
    <a class="tag" href="#tvos">Apple TV preview</a> <a class="tag" href="#server">Linux dedicated server</a></p>
  <?php if ($source): foreach ($source['downloads'] as $download): ?>
    <p><a class="btn primary" href="<?= h($download['url']) ?>">Download full source <?= h($source['version']) ?></a></p>
  <?php endforeach; else: ?>
    <p><?= $downloads['status'] === 'unavailable' ? 'Source release availability could not be checked.' : 'The first tagged source package is not released yet.' ?> <a href="<?= h(site()['repo_url']) ?>">Browse the source repository</a>.</p>
  <?php endif; ?>
  <p>Unpack the source and open a terminal in the folder containing <code>CMakeLists.txt</code>.
    The included <code>docs/BUILDING.md</code> has the full build and packaging instructions. The public repository is <a href="https://github.com/retrocs-org/openfp">retrocs-org/openfp on GitHub</a>.</p>

  <details class="build-guide" id="amiga"><summary>AmigaOS · ECS</summary><div class="build-content">
  <p>Use the <code>m68k-amigaos-gcc</code> cross-compiler with libnix and the Amiga headers. Put its <code>bin</code>
    folder on your path. The game requires AmigaOS 3.x, a 68040 or 68060, 2 MB Chip RAM and at least 8 MB Fast RAM; 16 MB Fast RAM is recommended. OCS, ECS and AGA all use the same four-plane, 16-colour renderer.
    <a href="https://franke.ms/amiga/amiga-gcc.wiki">Amiga GCC toolchain and setup</a></p>
  <pre class="code"><code>make -f src/platform/amiga/Makefile COMPILER=gcc CPU=68040 online local
sh tools/amiga/stage_maps.sh build-amiga-ecs
sh tools/amiga/stage_music.sh build-amiga-ecs</code></pre>
  <p>For the HTTPS public server list, also download the AmiSSL 4.x developer SDK and add
    <code>AMISSLINC=/path/to/AmiSSL/Developer/include</code> to the make command.
    Install AmiSSL 4.10–4.12 and its certificates on the Amiga. Without the SDK, address-based Private connections still work through the TCP/IP stack.</p>
  <p>Copy <code>OpenFirePowerOnline</code> and the <code>maps/</code> and <code>music/</code> folders from
    <code>build-amiga-ecs/</code> to the Amiga. The game reserves a 64 KB stack when needed.
    To make the Workbench installer, install <code>lha</code> and Python 3 with Pillow, then run</p>
  <pre class="code"><code>sh tools/amiga/package_lha.sh <?= h($version) ?> build-amiga-ecs</code></pre>

  </div></details>
  <details class="build-guide" id="macos"><summary>macOS</summary><div class="build-content">
  <p>Install the Xcode Command Line Tools with <code>xcode-select --install</code> if needed, then use Homebrew.</p>
  <pre class="code"><code>brew install cmake sdl2
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DOFP_BUILD_SERVER=OFF -DOFP_BUILD_TESTS=OFF
cmake --build build-mac --parallel
./build-mac/openfirepower</code></pre>
  <p>For the release build, run <code>sh tools/package_macos.sh</code>. It uses the pinned upstream SDL2 framework and creates a universal app, DMG, ZIP and SHA-256 files; this is also the recommended build if your Homebrew SDL package uses SDL2-compat.</p>

  </div></details>
  <details class="build-guide" id="linux"><summary>Linux desktop</summary><div class="build-content">
  <p>On Debian or Ubuntu, install SDL2 and the build tools.</p>
  <pre class="code"><code>sudo apt update
sudo apt install build-essential cmake pkg-config libsdl2-dev libcurl4-openssl-dev libsqlite3-dev
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel "$(nproc)"
./build-linux/openfirepower</code></pre>

  <p>Run <code>sh tools/package_linux.sh</code> on Linux for separate client and dedicated-server archives and their SHA-256 files.
    Build natively for ARM64 or Intel/AMD x86_64. Extract the archive and run <code>Play</code>,
    or build from source on other distributions.</p>

  </div></details>
  <details class="build-guide" id="windows"><summary>Windows x64</summary><div class="build-content">
  <p>Cross-build on macOS or Linux with MinGW-w64, CMake, curl and Python 3. On macOS use
    <code>brew install cmake mingw-w64 python</code>. On Debian or Ubuntu use
    <code>sudo apt install build-essential cmake mingw-w64 curl python3</code>.</p>
  <pre class="code"><code>sh tools/package_windows.sh</code></pre>
  <p>Find the portable ZIP under <code>~/Development/packages/OpenFirePower/Windows/</code>. Add <code>--msi</code> to make an installer after installing
    Homebrew’s <code>msitools</code> or Debian or Ubuntu’s <code>wixl</code> package.</p>

  <p>The Windows package includes <code>windows-qa.ps1</code> and <code>WINDOWS-TESTS.md</code>
    for automated checks and a short fullscreen, controller and installer checklist.</p>

  </div></details>
  <details class="build-guide" id="tvos"><summary>Apple TV · testing preview</summary><div class="build-content">
  <p>On a Mac with full Xcode and the tvOS SDK, run one of these commands:</p>
  <pre class="code"><code>sh tools/package_tvos.sh simulator
sh tools/package_tvos.sh device</code></pre>
  <p>The preview needs tvOS 15 or newer and an extended game controller. The ARM64 simulator app
    runs on Apple Silicon Macs. Device builds need your signing team in Xcode.
    Local saves use cache storage that tvOS may erase; cloud saves and physical Apple TV testing
    remain before a public release. See the source README for details.</p>

  </div></details>
  <details class="build-guide" id="server"><summary>Linux dedicated server</summary><div class="build-content">
  <p>Build the server on Debian or Ubuntu without SDL. It runs the bots and manages the match for connected clients.</p>
  <pre class="code"><code>sudo apt update
sudo apt install build-essential cmake pkg-config libsqlite3-dev libcurl4-openssl-dev ca-certificates
cmake -S . -B build-server -DCMAKE_BUILD_TYPE=Release -DOFP_BUILD_SDL2=OFF -DOFP_BUILD_TESTS=OFF
cmake --build build-server --parallel "$(nproc)"
cp -n server.cfg.example server.cfg
nano server.cfg
./build-server/ofp_server -c server.cfg --check
./build-server/ofp_server -c server.cfg</code></pre>
  <p>For private games, set <code>public</code>, <code>share_matches</code> and <code>share_replays</code> to <code>no</code> before starting.
    Players connect by address on UDP port <b>29870</b>. Server and clients must use the same protocol and rules. When upgrading a systemd installation, replace the executable named by its <code>ExecStart</code>, then restart the service; rebuilding in a source folder alone does not update the running server. The source build guide also covers running the server as a systemd service.</p>
  </div></details>
</section>
<?php page_end();
