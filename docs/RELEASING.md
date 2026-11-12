# Releasing OpenFP

Use Python 3.9 or newer from the source root. The release tool builds the game,
checks its packages, prepares a manifest for review and publishes those same
files to GitHub and, optionally, Aminet. Existing platform packaging
scripts still work on their own.

The workflow uses the same command structure and external settings approach as
BitSkim, adapted to OpenFP's clients, Linux server tools and MSI installer.
It needs no separate release-tools checkout. Builds and packages stay in their
own directories, with verified source and package records for each platform.

## Version policy

The first release is **0.1**. Keep that version in game and package metadata
until a new version is explicitly requested. The release tool reads the version;
it never bumps it based on commits, dates or tags. `--tag` changes the tag name
only, not the version in the application.

## Commands

```sh
python3 scripts/release.py plan
python3 scripts/release.py build
python3 scripts/release.py check
python3 scripts/release.py prepare
```

`plan` prints the ordered tests and build commands without running them.
`build` first checks the build host, compiler tools and relevant local prerequisites
(Pillow for Amiga packaging; the signing identity for public Mac builds), so missing
tools fail before the lengthy test run or replacement of previous outputs.
It runs automated tests, builds each selected target from a fresh output
directory, signs as configured, packages and records the results. When every
configured target has a valid record it assembles the source archive, website
bundle and checksums. `check` verifies these files; `prepare` writes
`work/release/manifest.json`. None of these commands publishes a release.
Builds can download dependencies and, when configured, contact Apple for notarization.

Finish all edits and **commit before building**. Documentation, tests and website
files are release inputs too. This extracted source tree can build and prepare
packages with a null commit; publication requires a clean Git checkout at the
exact commit recorded during the builds. Required inputs must all be tracked,
including the redistributable sound source allowed by `.gitignore`.

## Configuration

The shared project settings file is:

```text
~/Development/tools/git-release/projects/openfp.json
```

Copy `examples/release-config.json` there on a new build machine. Configuration
selection is, in order: `--config PATH`, `OFP_RELEASE_CONFIG`, the shared file,
then the older ignored `work/release-config.json`. Without any settings file,
`plan` can still show the default workflow. An explicitly selected missing file
is an error. `plan` prints which path is selected.

Keep machine paths, signing profile names and credential lookup details outside
the checkout. Actual API tokens and passwords never belong in the JSON.
The public repository is <https://github.com/retrocs-org/openfp>.

Its HTTPS repository URL and Git transport URL must name the same repository.
Create the repository before pushing. Git uses your normal SSH key or credential
helper; API tokens do not authenticate Git pushes. Use `--to github` for public
source pushes and releases; other destinations may be configured privately.

The version comes from `OFP_VERSION` in `src/common/net.h`. CMake's project version
must agree. A version of `0.1` gives tag `v0.1`; `prepare --tag 0.1` selects the
exact tag `0.1` instead, without changing embedded versions or package names.
`--tag-message "OpenFP 0.1 release"` sets its annotation when preparing.
Publication uses the prepared tag/annotation. Bump the source version for a new
release; never move a published tag or replace different assets under that version.

## Source-only repository updates

```sh
python3 scripts/release.py source --to github --dry-run
python3 scripts/release.py source --to github --confirm-push
```

This checks every selected destination before pushing the current committed
branch. It never builds, uploads packages or pushes tags. Set `source_branch`
to override the remote branch name; an empty value uses the local branch name.
Git pushes use your existing SSH key or credential helper, separately from API
credentials. No force or mirror push is used.

For this extracted source, start in the directory containing `scripts/release.py`.
Create the empty public repository and configure your Git author name/email and
SSH access first.

For the first import into new, empty repositories only:

```sh
python3 scripts/release.py source --to github --init --dry-run --commit-message "OpenFP source release"
```

`--init` verifies all selected remotes are empty and checks the Git author
identity before initializing a local repository and committing the selected
source files. The dry run uploads nothing, but **can create that first local
commit**. It respects `.gitignore`; ignored required source must be reviewed and
tracked first. It refuses a project nested inside another checkout or remotes
with existing history. Existing committed checkouts get no extra commit.
Review the local commit, then upload the source to the public repository:

```sh
git show --stat --oneline HEAD
python3 scripts/release.py source --to github --confirm-push
```

For later source updates, commit the reviewed edits, run `source --to github --dry-run`, then
`source --to github --confirm-push`. These commands push source only; they do not publish a
tagged binary release or deploy the website.

## Before the first public upload

Keep live website/server configuration, API credentials, exported signing keys
and local release settings outside Git. The public example files stay included.
Source archives exclude these private files, and publication rejects them if
they were already tracked. Imported original-game maps/art and generated build
directories must also remain untracked; the map distribution notice is included.

Inspect the exact source snapshot and any existing Git history for credentials
before the first push. Ignore rules do not remove already committed files or
rewrite older commits. The release tool checks common credential patterns and
private filenames; it does not claim a comprehensive scan of every historical
commit. If available, run Gitleaks with redacted output on the intended source
snapshot and on Git history. Review findings without copying secret values into
logs or issue reports.

After the reviewed source is committed, build every configured release target,
complete platform runtime checks, and run `prepare` and `check`. Existing archives
without current build records are not a verified release. Mac publication needs
the configured Developer ID identity and notarization profile to be accessible.

## Public binary package names

All binary assets use `openfp-ROLE-PLATFORM-ARCH-0.1.EXT` and have a matching
`.sha256` sidecar. The shared naming/checksum helper is `scripts/release_assets.py`.

- macOS: `openfp-client-macos-universal-0.1.zip` and `.dmg`.
- Windows: `openfp-client-windows-x86_64-0.1.zip` and `.msi`.
- Linux client: `openfp-client-linux-ARCH-0.1.tar.gz`.
- Linux server: `openfp-server-linux-ARCH-0.1.tar.gz`.
- AmigaOS: `openfp-client-amigaos-m68k-0.1.lha`, plus the matching Aminet `.readme`.

Linux `ARCH` is `x86_64` or `aarch64`. macOS is one universal app; AmigaOS uses
one 16-colour 68040 build compatible with 68060. DEB and tvOS preview files are
not in the release inventory. Existing source/website archive names are retained.
Old build records are stale after changing this convention; rebuild the targets
before preparing a future release. Do not rename previously published assets.
For manual test builds without any release operation, see
[BUILDING.md](BUILDING.md#manual-package-builds-without-publishing).

## Targets and tools

| Target | Ready to test | Release packages | Build host |
| --- | --- | --- | --- |
| `macos` | Universal `.app` | DMG and app ZIP | macOS with Xcode tools, CMake |
| `windows` | EXE, SDL2 and data folder | x64 MSI and portable ZIP | macOS/Linux with MinGW-w64, CMake, Python, msitools |
| `amiga` | 16-colour 68040 binaries, maps and music | Workbench installer LHA and Aminet readme | Amiga cross-compiler, SDK headers, make, lha, Python/Pillow |
| `linux-x86_64` | Separate desktop client and server folders | Separate client and server tar.gz | x86_64 Linux with SDL2, curl, SQLite development packages, CMake |
| `linux-aarch64` | Same for ARM64 | Separate client and server tar.gz | ARM64 Linux with the same dependencies |

The Linux server archive contains `ofp_server`, `ofp_match`, `ofp_bot`, bundled
maps and configuration examples, without desktop graphics or audio. See [BUILDING.md](BUILDING.md)
for individual client and headless server build commands. Apple TV is an
experimental preview and is excluded from the release inventory. A separate
higher-colour Amiga edition is planned, not a current release target.

Install host tools through the system package manager or Homebrew where available.
Keep other cross-compilers and SDKs under `~/Development/tools/`. The release tool
places its downloaded Windows SDL2 SDK under that tools directory. It never
installs compilers automatically. Put the Amiga compiler in `PATH`; set `AMISSLINC`
for the AmiSSL SDK when building the HTTPS public server list. Alternatively,
set `AMIGA_CC` to the compiler executable and `LHA` to the archiver executable.
`OFP_RELEASE_PYTHON` selects Python for automated release tests and packaging;
that installation needs Pillow for the Amiga installer icons. Standalone shell
packagers also accept `PYTHON`. Build subprocesses do not inherit the configured
publication tokens or 1Password session credentials.

For example, a Mac configuration can add non-secret build variables:

```json
{
  "env": {
    "OFP_RELEASE_PYTHON": "/absolute/path/to/python"
  },
  "target_settings": {
    "amiga": {
      "env": {
        "AMIGA_CC": "/absolute/path/to/m68k-amigaos-gcc",
        "LHA": "/absolute/path/to/lha",
        "AMISSLINC": "/absolute/path/to/AmiSSL/Developer/include"
      }
    }
  }
}
```

These settings merge with the defaults. `target_settings.<target>.env` overrides
them for one target. CMake and make also receive the inherited process environment;
use a controlled build environment and keep build flags consistent between runs.

## Building on several machines

The default matrix needs macOS and both Linux architectures. On a Mac, start with:

```sh
python3 scripts/release.py build --targets macos,windows,amiga
```

Use openSUSE Leap 16 as the preferred release build and test host on both Linux
architectures. On those machines, use the **same committed source and target configuration**:

```sh
python3 scripts/release.py build --targets linux-x86_64
# On the ARM64 machine:
python3 scripts/release.py build --targets linux-aarch64
```

Keep the full matrix in the configuration on each machine; `--targets` selects
which part to build. Transfer each target's `work/release/records/TARGET.json`,
its `builds` subdirectory and its `packages` subdirectory to the release Mac,
using the same relative layout. A current `work/release/tests.json` must also be
present. Paths may differ between machines; source fingerprints, commit, target
commands and configured build variables must agree. Do not edit records or adopt
old prebuilt binaries. Run `prepare`, then `check` on the Mac after collecting
all targets; preparation assembles the combined bundle and verifies every record.
The usual single-machine or wrapper-driven sequence remains `build`, `check`, `prepare`.

Alternatively, `target_settings.TARGET.commands` and `.tools` can supply argument
arrays invoking your own build-host wrappers. Tokens `{root}`, `{work}`, `{version}`,
`{builds}`, `{packages}` and `{tools}` are expanded in these arrays. A wrapper must
finish synchronously and place its output where `plan` expects it; it must fail
when remote compilation or tests fail. The release tool does not configure remote
machines or transfer their files automatically. All configured targets are required
for `check` and `prepare`; reduce the configured matrix only for an intentional
platform-specific release, whose website will offer only the packaged downloads.

On the final Mac, installer checks also need `msiextract` from msitools,
and `bsdtar` for LHA inspection. These are available through
Homebrew. Checks extract payloads temporarily and never run an installer.

## Signing the Mac packages

The example uses ad-hoc app signing and no notarization. That is a local test
policy, not a notarized public Mac release. For a normal public Mac release set:

```json
{
  "macos": {
    "identity": "Developer ID Application: Your Name (TEAMID)",
    "notary_profile": "openfirepower",
    "require_notarization": true
  }
}
```

Store the signing certificate and private key in the Mac keychain and create the
named `notarytool` keychain profile beforehand. The tool signs the app, submits and
staples it before packaging, then signs, submits and staples the DMG. Checks verify
the app signature, the configured identity and both staples. A failed signing or
notarization operation does not produce a successful record. The manifest states
the signing policy explicitly; it does not silently upgrade an ad-hoc build to a
notarized release. Windows packages currently have no Authenticode signing step.

## Output folders and review

Defaults, relative to your home directory:

```text
Development/builds/OpenFirePower/macOS/OpenFirePower.app
Development/builds/OpenFirePower/Windows/OpenFirePower/
Development/builds/OpenFirePower/AmigaOS/ECS/
Development/builds/OpenFirePower/Linux/{x86_64,aarch64}/OpenFirePower/
Development/builds/OpenFirePower/Linux/{x86_64,aarch64}/OpenFirePowerServer/
Development/packages/OpenFirePower/{macOS,Windows,AmigaOS,Linux}/
Development/packages/OpenFirePower/Release/v0.1/
```

Compiler output stays in ignored `work/release/targets/`. Test-ready folders
contain runnable files and data. The combined `Release/vVERSION` directory has
the exact publication inventory: all configured platform packages, the source
archive, the website update archive, `build-records.json` and `SHA256SUMS`.
Each platform package also has its own `<filename>.sha256` file, checked against
the actual package bytes. These accompany the packages in the release and website.
The website bundle contains matching client downloads and a versioned checksum
file. It excludes private website configuration and database files. Deploy it
manually; release publication never updates the web server.

Review `manifest.json`, `SHA256SUMS`, signing status and
[RELEASE-NOTES.md](RELEASE-NOTES.md). Supply a different notes file with
`prepare --notes PATH`. Build records identify tools, source and package hashes.
They report build checks separately from runtime checks, which start as
`not-recorded`. A successful checksum comparison does not establish physical
controller support, native runtime compatibility or reproducible builds. Complete
the platform runtime checklists separately, including the bundled Windows tests.

If submitting to Aminet, supply the public uploader contact when preparing:

```sh
python3 scripts/release.py prepare --uploader release@example.org
```

There is no contact address hardcoded into the tool. The optional configuration
field is a convenience. The supplied address appears in the prepared public readme
and manifest. Review it before publication. The category is `game/actio`.

## Publishing

Provide `GITHUB_TOKEN` through a hidden prompt or an environment variable.
Alternatively, configure a 1Password lookup in the external JSON:

```json
{
  "credentials": {
    "github": {
      "provider": "1password",
      "reference": "op://YourVault/YourItem/YourField"
    }
  }
}
```

Choose an exact copied `reference` **or** `vault`/`item`/`field` for each service.
The named-field form supports labels containing slashes. Add `account` to select
a 1Password account when needed. The `op` CLI must already be available and
signed in or connected to the desktop app. Only selected publication services
read credentials, captured in memory with no secret output logged. A missing,
ambiguous or cancelled lookup stops publication. Planning, builds, checks and
source-only pushes never read these credentials. No credential is minted or
written back to 1Password by this tool. See the official
[read command](https://developer.1password.com/docs/cli/reference/commands/read/)
and [item get](https://developer.1password.com/docs/cli/reference/management-commands/item/#item-get)
documentation.

Environment providers can select a different variable:
`"github": {"provider": "env", "variable": "GITHUB_TOKEN"}`.
Do not paste tokens into commands, configuration or notes. Do not enable
shell tracing. GitHub needs repository Contents write access and permission to
create the chosen tag. Normal Git authentication must also work for the public
repository.

For an interactive **Bash** shell, prompts keep tokens out of command history:

```bash
read -r -s -p 'GitHub token: ' GITHUB_TOKEN
printf '\n'
export GITHUB_TOKEN
python3 scripts/release.py publish --to github --confirm-publish
unset GITHUB_TOKEN
```

To prepare and publish verified builds in one explicit operation:

```sh
python3 scripts/release.py release --tag v0.1 --to github --confirm-publish
```

This performs the same full checks as separate `prepare` and `publish`. It does
not build missing targets. Use separate commands when you want to inspect the
manifest between preparation and upload.

Add `aminet` to `--to github` only after preparing its readme. Destination names
must be distinct. Specify `--to` explicitly to avoid selecting other configured
destinations. Omitting `--confirm-publish` refuses publication.

All selected services and remote tags are checked before any tag push or upload.
Only an annotated release tag is created/pushed, at the reviewed commit. Publication
does not push source branches; that is the separate `source` command. Existing
tags must resolve to the same commit and are never
force-moved. GitHub uploads go into a draft; every asset needs an equal, known
SHA-256 digest before the draft is made public. Existing equal assets are reused;
different or unknown bytes, titles, notes or links stop the run.
These checks reduce partial publication; several services cannot be one atomic transaction.

The public release adapter uses the official [GitHub release API](https://docs.github.com/en/rest/releases/releases)
and [release asset digests](https://github.blog/changelog/2025-06-03-releases-now-expose-digests-for-release-assets/).
The `gh` CLI is not required. Aminet uses anonymous FTP to
`main.aminet.net:/new`, uploading the LHA first and readme last. Successful
submission means **awaiting moderation**, not already publicly listed.

## Interrupted operations

`work/release/receipt.json` records the manifest hash, current destination and
completed destinations. Repository uploads can be resumed with the same manifest:
matching assets are checked and reused, without deleting successful releases.
There are no automatic network retries or cross-service rollbacks.

An Aminet attempt also writes `aminet-receipt.json` before connecting. An incomplete
attempt blocks resubmission. Inspect that receipt and the incoming server before
manual recovery; deleting the receipt is not a recovery procedure. A completed
submission is not sent again.

Keep receipts. The tool refuses to rebuild or replace a bundle once publication
has started. Use a new work directory for a new release, and a new version when
changing published content. A stale `release.lock` also requires inspection before
manual removal; first confirm that no release operation is running.

## Offline checks

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_release*.py'
```

Tests use fake APIs and temporary Git repositories. They never contact a release
service or submit an Aminet upload. Optional installed packaging tools are exercised
with tiny fixture payloads. Run these checks manually; automatic repository jobs
are disabled for now. A tag push does not build or upload packages. Exercise
publication against a dedicated test repository before the first production release.
