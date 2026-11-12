# SPDX-License-Identifier: MIT
"""Project-specific build matrix and source/website packaging."""
import gzip
import hashlib
import io
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

from release_common import (ReleaseError, atomic, canonical, clean_git, config, fingerprint,
                            git_commit, read_json, regular, require, run, sha, source_files,
                            tree_hashes, version)
from release_credentials import build_environment
from release_assets import package_name


def catalog(root, cfg, ver):
    work = Path(cfg["work"]) / "targets"
    result = {}
    for name in cfg["targets"]:
        w = work / name
        if name == "macos":
            commands = [["sh", "tools/package_macos.sh"],
                        ["cmake", "--build", str(w / "build"), "--parallel", "4"],
                        ["ctest", "--test-dir", str(w / "build"), "--output-on-failure"]]
            files = [(package_name("client", "macos", "universal", ver, ext), "macOS") for ext in ("dmg", "zip")]
            ready, dest = "OpenFirePower.app", "macOS/OpenFirePower.app"
            tools = [["cc", "--version"], ["cmake", "--version"], ["xcrun", "--version"]]
            host = "Darwin"
        elif name == "windows":
            commands = [["sh", "tools/package_windows.sh", "--msi"]]
            files = [(package_name("client", "windows", "x86_64", ver, ext), "Windows") for ext in ("zip", "msi")]
            ready, dest = "OpenFirePower", "Windows/OpenFirePower"
            tools = [["x86_64-w64-mingw32-gcc", "--version"], ["cmake", "--version"], ["wixl", "--version"]]
            host = None
        elif name == "amiga":
            commands = [["sh", "tools/package_amiga.sh"]]
            files = [(package_name("client", "amigaos", "m68k", ver, ext), "AmigaOS") for ext in ("lha", "readme")]
            ready, dest = "Ready", "AmigaOS/ECS"
            compiler = cfg["target_settings"].get(name, {}).get("env", {}).get("AMIGA_CC", cfg["env"].get("AMIGA_CC", "m68k-amigaos-gcc"))
            tools = [[os.path.expandvars(os.path.expanduser(compiler)), "--version"],
                     [os.path.expandvars(os.path.expanduser(cfg["env"].get("OFP_RELEASE_PYTHON", sys.executable))), "--version"]]
            host = None
        else:
            arch = name.split("-", 1)[1]
            commands = [["sh", "tools/package_linux.sh"],
                        ["ctest", "--test-dir", str(w / "build"), "--output-on-failure"]]
            files = [(package_name(role, "linux", arch, ver, "tar.gz"), "Linux/" + arch) for role in ("client", "server")]
            ready, dest = "Ready", "Linux/" + arch
            tools = [["cc", "--version"], ["cmake", "--version"]]
            host = "Linux:" + arch
        settings = cfg["target_settings"].get(name, {})
        require(isinstance(settings, dict), "Target settings must be an object: " + name)
        require(not (set(settings) - {"commands", "tools", "env"}), "Unknown target build setting: " + name)
        result[name] = {"commands": settings.get("commands", commands), "tools": settings.get("tools", tools),
                        "env": settings.get("env", {}), "files": files, "work": w,
                        "ready": w / ready, "dest": Path(cfg["builds"]) / dest,
                        "host": None if "commands" in settings else host}
    return result


class Project:
    def __init__(self, root, cfg, tag=None, tag_message=None):
        self.root = Path(root).resolve()
        self.cfg = cfg
        self.ver = version(self.root)
        require(tag is None or isinstance(tag, str) and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._/-]*", tag), "Invalid release tag")
        self.tag = tag or "v" + self.ver
        if tag is not None:
            from release_source import validate_ref
            validate_ref(self.root, "refs/tags/" + tag)
        self.tag_message = tag_message or "OpenFP " + self.ver
        require(isinstance(self.tag_message, str) and self.tag_message.strip() and "\x00" not in self.tag_message, "Empty or invalid tag annotation")
        self.work = Path(cfg["work"])
        self.packages = Path(cfg["packages"])
        self.folder = self.packages / "Release" / self.tag
        self.targets = catalog(self.root, cfg, self.ver)

    def inputs(self):
        files = source_files(self.root)
        return {"source_sha256": fingerprint(files), "commit": git_commit(self.root), "version": self.ver}, files

    def settings(self, name):
        # Build locations may differ between machines; commands and tool configuration may not silently change.
        target = self.targets[name]
        value = {"commands": target["commands"], "tools": target["tools"], "env": {**self.cfg["env"], **target["env"]}}
        if name == "macos":
            value["signing"] = self.cfg["macos"]
        for key in ("root", "work", "builds", "packages", "tools"):
            prefix = str(self.root) if key == "root" else self.cfg[key]
            value = json.loads(json.dumps(value).replace(prefix, "{" + key + "}"))
        return hashlib.sha256(canonical(value)).hexdigest()

    def expand(self, argv, name):
        tokens = {"root": str(self.root), "work": str(self.targets[name]["work"]), "version": self.ver,
                  "builds": self.cfg["builds"], "packages": self.cfg["packages"], "tools": self.cfg["tools"]}
        require(isinstance(argv, list) and argv and all(isinstance(s, str) for s in argv), "Commands must be argument arrays")
        return [s.format(**tokens) for s in argv]

    def env(self, name):
        result = {"WORK": str(self.targets[name]["work"]), "OFP_PACKAGE_DIR": str(self.targets[name]["work"]),
                  "OFP_BUILD_DIR": str(self.targets[name]["work"] / "Ready" if name.startswith("linux-") or name == "amiga" else self.targets[name]["work"]),
                  "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "SDL_RENDER_DRIVER": "software"}
        result.update(self.cfg["env"])
        result.update(self.targets[name]["env"])
        result = {k: os.path.expandvars(os.path.expanduser(v)) for k, v in result.items()}
        result["PYTHON"] = result.get("OFP_RELEASE_PYTHON", sys.executable)
        if name == "windows":
            result["SDL2_CACHE_DIR"] = str(Path(self.cfg["tools"]) / "windows/SDL2")
        if name == "macos":
            result["SDL2_CACHE_DIR"] = str(Path(self.cfg["tools"]) / "macos/SDL2")
            result.update(SIGN_ID=self.cfg["macos"]["identity"], NOTARY_PROFILE=self.cfg["macos"]["notary_profile"])
        if name.startswith("linux-"):
            result["ARCH"] = name.split("-", 1)[1]
        return build_environment(self.cfg, result)

    def test_commands(self):
        w = self.work / "tests"
        python = os.path.expandvars(os.path.expanduser(self.cfg["env"].get("OFP_RELEASE_PYTHON", sys.executable)))
        return [[python, "-m", "unittest", "discover", "-s", "tests", "-p", "test_release*.py", "-b"],
                ["cmake", "-S", str(self.root), "-B", str(w), "-DCMAKE_BUILD_TYPE=Release", "-DOFP_BUILD_SDL2=OFF", "-DOFP_BUILD_TESTS=ON"],
                ["cmake", "--build", str(w), "--parallel", "4"],
                ["ctest", "--test-dir", str(w), "--output-on-failure"]]

    def plan(self, names):
        import shlex
        print("OpenFP " + self.ver + " (tag " + self.tag + ")")
        print("Available release targets: " + ", ".join(self.cfg["targets"]))
        print("Experimental, excluded from release: Apple TV preview")
        print("Planned, excluded from release: separate higher-colour Amiga edition")
        print("Tool availability is checked before tests and compilation; API credentials are not requested.")
        print("Tests:")
        for cmd in self.test_commands():
            print("  " + shlex.join(cmd))
        for name in names:
            print(name + " (WORK=" + str(self.targets[name]["work"]) + "):")
            for cmd in self.targets[name]["tools"]:
                print("  " + shlex.join(self.expand(cmd, name)))
            for cmd in self.targets[name]["commands"]:
                print("  " + shlex.join(self.expand(cmd, name)))
            print("  verify signatures, package contents, ready build and source fingerprint; record hashes")
        print("build completion: package source + build records + website downloads; write SHA256SUMS")
        print("check: verify every configured target and the complete inventory")
        print("prepare: write " + str(self.work / "manifest.json") + "; no publication")

    def signing_check(self, name):
        if name != "macos":
            return {"signature": "not-applicable", "notarization": "not-applicable"}
        require(platform.system() == "Darwin", "Check macOS signatures on a Mac before preparing or publishing")
        policy = self.cfg["macos"]
        app = self.targets[name]["dest"]
        dmg = self.packages / "macOS" / package_name("client", "macos", "universal", self.ver, "dmg")
        run(["codesign", "--verify", "--deep", "--strict", str(app)], self.root, capture=True)
        info = run(["codesign", "-dv", "--verbose=4", str(app)], self.root, capture=True)
        if policy["identity"] != "-":
            require("Authority=" + policy["identity"] in info and "Signature=adhoc" not in info,
                    "macOS signing identity does not match release policy")
        else:
            require("Signature=adhoc" in info, "Expected ad-hoc macOS signature")
        if policy["require_notarization"] or policy["notary_profile"]:
            require(policy["identity"] != "-" and policy["notary_profile"], "Notarization requires Developer ID and a keychain profile")
            for path in (app, dmg):
                run(["xcrun", "stapler", "validate", str(path)], self.root, capture=True)
            run(["codesign", "--verify", "--strict", str(dmg)], self.root, capture=True)
            return {"signature": "developer-id-verified", "notarization": "staples-verified"}
        return {"signature": "ad-hoc-verified" if policy["identity"] == "-" else "developer-id-verified", "notarization": "not-requested"}

    def build(self, names):
        start, _ = self.inputs()
        require(not (self.work / "receipt.json").exists(), "Preserve publication receipts; use a new release work folder for rebuilding")
        # Check platform requirements before doing expensive work. Overrides may invoke another build machine.
        for name in names:
            host = self.targets[name]["host"]
            if host:
                require(platform.system() == host.split(":")[0], name + " requires " + host + "; configure a build wrapper or run this target on that platform")
                if ":" in host:
                    arch = {"arm64": "aarch64", "amd64": "x86_64"}.get(platform.machine(), platform.machine())
                    require(arch == host.split(":")[1], name + " requires a native build machine or a configured cross-build wrapper")
        # Fail on a missing tool before the lengthy test suite, preserving earlier build outputs.
        versions = {}
        for name in names:
            versions[name] = [run(self.expand(cmd, name), self.root, self.env(name), capture=True, inherit=False).strip()
                              for cmd in self.targets[name]["tools"]]
            require(all(versions[name]), "Missing tool identity: " + name)
            if name == "amiga" and "commands" not in self.cfg["target_settings"].get(name, {}):
                run([self.env(name)["PYTHON"], "-c", "from PIL import Image"], self.root, self.env(name), capture=True, inherit=False)
            if name == "macos" and platform.system() == "Darwin" and self.cfg["macos"]["identity"] != "-":
                identities = run(["security", "find-identity", "-v", "-p", "codesigning"], self.root, capture=True)
                require('"' + self.cfg["macos"]["identity"] + '"' in identities,
                        "Configured Developer ID signing identity is unavailable; unlock the signing keychain or install the identity before building macos")
        tests_file = self.work / "tests.json"
        if tests_file.exists():
            tests_file.unlink()
        for cmd in self.test_commands():
            run(cmd, self.root, build_environment(self.cfg, {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}), inherit=False)
        require(self.inputs()[0] == start, "Source changed during tests")
        atomic(tests_file, canonical({**start, "automated_tests": "passed", "commands": self.test_commands()}))
        for name in names:
            target = self.targets[name]
            record = self.work / "records" / (name + ".json")
            if record.exists():
                record.unlink()
            require(not target["work"].is_symlink(), "Target work folder is a symlink")
            if target["work"].exists():
                shutil.rmtree(target["work"])
            target["work"].mkdir(parents=True, exist_ok=True)
            tool_versions = versions[name]
            before, _ = self.inputs()
            require(before == start, "Source changed before target build")
            for cmd in target["commands"]:
                run(self.expand(cmd, name), self.root, self.env(name), inherit=False)
            require(self.inputs()[0] == start, "Source changed during " + name + " build")
            dest = target["dest"]
            require(not dest.is_symlink(), "Ready build destination is a symlink")
            dest.parent.mkdir(parents=True, exist_ok=True)
            if dest.exists():
                shutil.rmtree(dest)
            if name == "macos":
                # Preserve bundle metadata and stapled tickets as well as framework links.
                run(["ditto", str(target["ready"]), str(dest)], self.root)
            else:
                shutil.copytree(target["ready"], dest, symlinks=True)
            artifacts = {}
            for file, category in target["files"]:
                p = regular(target["work"] / file)
                out = self.packages / category / file
                atomic(out, p.read_bytes())
                artifacts[category + "/" + file] = sha(out)
                atomic(out.with_name(out.name + ".sha256"), self.sums({file: sha(out)}))
            signing = self.signing_check(name)
            self.validate_contents(name)
            atomic(record, canonical({**start, "target": name, "settings_sha256": self.settings(name),
                "tool_versions": tool_versions, "artifacts": artifacts, "ready_build": tree_hashes(dest),
                "signing": signing, "build_check": "passed", "runtime_check": "not-recorded"}))
            print(name + ": build and package checks passed; physical runtime check is separate")
        if all((self.work / "records" / (n + ".json")).exists() for n in self.cfg["targets"]):
            self.make_bundle(self.records(), self.cfg["aminet"]["uploader"] or None)
            self.check()
            print("Complete release inventory: " + str(self.folder))
        else:
            print("Target records saved; build the remaining configured targets before check/prepare")

    def validate_contents(self, name):
        """Compare ready binaries/assets with portable packages, including project notices."""
        target = self.targets[name]
        dest = target["dest"]
        if name.startswith("linux-"):
            self.validate_linux_server(name)
            dest = dest / "OpenFirePower"
        if name == "amiga":
            archive = self.packages / "AmigaOS" / package_name("client", "amigaos", "m68k", self.ver, "lha")
            listing = run(["bsdtar", "-tf", str(archive)], self.root, capture=True).splitlines()
            expected = {"OpenFirePower/bin/ECS/" + n: dest / n for n in ("OpenFirePower", "OpenFirePowerOnline")}
            expected.update({"OpenFirePower/LICENSE": self.root / "LICENSE", "OpenFirePower/Credits": self.root / "CREDITS.md",
                             "OpenFirePower/ThirdPartyNotices": self.root / "THIRD-PARTY-NOTICES.md"})
            for member, local in expected.items():
                require(listing.count(member) == 1, "Missing or duplicate Amiga package member: " + member)
                result = subprocess.run(["bsdtar", "-xOf", str(archive), member], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                require(result.returncode == 0 and hashlib.sha256(result.stdout).hexdigest() == sha(local), "Amiga package content mismatch: " + member)
            return
        portable = next((self.packages / category / f for f, category in target["files"] if f.endswith((".zip", ".tar.gz"))), None)
        require(portable is not None, "Missing portable package")
        if name == "macos":
            prefix = "OpenFirePower.app/"
        else:
            prefix = "OpenFirePower/"
        if portable.suffix == ".zip":
            with zipfile.ZipFile(regular(portable)) as z:
                members = [n for n in z.namelist() if not n.endswith("/") and not n.startswith("__MACOSX/")]
                require(len(members) == len(set(members)), "Duplicate ZIP members")
                hashes = {n: hashlib.sha256(z.read(n)).hexdigest() for n in members}
        else:
            with tarfile.open(regular(portable)) as t:
                members = [m for m in t.getmembers() if m.isfile()]
                require(len({m.name for m in members}) == len(members), "Duplicate archive members")
                hashes = {m.name: hashlib.sha256(t.extractfile(m).read()).hexdigest() for m in members}
        required = (["Contents/MacOS/openfirepower", "Contents/Resources/LICENSE", "Contents/Resources/CREDITS.md"] if name == "macos" else
                    ["openfirepower.exe" if name == "windows" else "openfirepower", "LICENSE", "CREDITS.md"])
        required.append(("Contents/Resources/" if name == "macos" else "") + "THIRD-PARTY-NOTICES.md")
        for rel in required:
            require(hashes.get(prefix + rel) == sha(dest / rel), "Portable package does not match ready build: " + rel)
        if name != "windows":
            binary = dest / ("Contents/MacOS/openfirepower" if name == "macos" else "openfirepower")
            require(binary.stat().st_mode & 0o111, "Ready game binary is not executable")
        # All packaged runtime data must belong to the checked source selection.
        inputs = source_files(self.root)
        data_prefix = prefix + ("Contents/Resources/data/" if name == "macos" else "data/")
        for member, digest in hashes.items():
            if member.startswith(data_prefix):
                rel = "data/" + member[len(data_prefix):]
                require(rel in inputs and inputs[rel]["sha256"] == digest, "Untracked, imported or changed packaged data: " + rel)
        self.validate_installer(name)

    def validate_linux_server(self, name):
        target = self.targets[name]
        folder = target["dest"] / "OpenFirePowerServer"
        archive = next(self.packages / category / file for file, category in target["files"] if file.startswith("openfp-server-"))
        with tarfile.open(regular(archive)) as tar:
            members = tar.getmembers()
            names = [m.name for m in members]
            require(len(names) == len(set(names)) and all(m.isfile() or m.isdir() for m in members), "Unsafe or duplicate server archive members")
            hashes = {m.name: hashlib.sha256(tar.extractfile(m).read()).hexdigest() for m in members if m.isfile()}
        expected = {"OpenFirePowerServer/" + rel: digest for rel, digest in tree_hashes(folder).items()}
        require(hashes == expected, "Server archive differs from ready server build")
        for binary in ("ofp_server", "ofp_match", "ofp_bot"):
            require((folder / binary).stat().st_mode & 0o111, "Server tool is not executable: " + binary)
        for rel in ("server.cfg.example", "LICENSE", "CREDITS.md", "THIRD-PARTY-NOTICES.md"):
            require(sha(folder / rel) == sha(self.root / rel), "Server package source differs: " + rel)
        for file in (folder / "data/maps").rglob("*"):
            if file.is_file():
                require(sha(file) == sha(self.root / "data/maps" / file.relative_to(folder / "data/maps")), "Server map differs from source")

    def validate_installer(self, name):
        """Check installed payloads as well as portable archives; never launch an installer."""
        dest = self.targets[name]["dest"]
        self.work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="installer-check-", dir=str(self.work)) as directory:
            temp = Path(directory)
            if name == "windows":
                installer = self.packages / "Windows" / package_name("client", "windows", "x86_64", self.ver, "msi")
                run(["msiextract", "-C", str(temp), str(installer)], self.root, capture=True)
                matches = list(temp.rglob("openfirepower.exe"))
                require(len(matches) == 1, "Windows installer must contain one game executable")
                require(tree_hashes(matches[0].parent) == tree_hashes(dest), "Windows installer payload differs from portable build")
            elif name == "macos":
                installer = self.packages / "macOS" / package_name("client", "macos", "universal", self.ver, "dmg")
                mount = temp / "volume"
                mount.mkdir()
                run(["hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint", str(mount), str(installer)], self.root, capture=True)
                try:
                    require(tree_hashes(mount / "OpenFirePower.app") == tree_hashes(dest), "Disk image app differs from portable build")
                finally:
                    run(["hdiutil", "detach", str(mount)], self.root, capture=True)

    def records(self):
        current, _ = self.inputs()
        tests = read_json(self.work / "tests.json")
        require(all(tests.get(k) == v for k, v in current.items()) and tests.get("automated_tests") == "passed", "Missing or stale automated test record")
        records = {}
        for name in self.cfg["targets"]:
            record = read_json(self.work / "records" / (name + ".json"))
            require(all(record.get(k) == v for k, v in current.items()) and record.get("target") == name and
                    record.get("settings_sha256") == self.settings(name) and record.get("build_check") == "passed" and record.get("tool_versions"),
                    "Missing or stale build record: " + name)
            expected = {c + "/" + f for f, c in self.targets[name]["files"]}
            require(set(record["artifacts"]) == expected, "Incorrect artifact inventory: " + name)
            for rel, digest in record["artifacts"].items():
                require(sha(self.packages / rel) == digest, "Changed artifact: " + rel)
            require(tree_hashes(self.targets[name]["dest"]) == record["ready_build"], "Ready binary changed: " + name)
            require(self.signing_check(name) == record["signing"], "Signing policy or signature changed: " + name)
            self.validate_contents(name)
            records[name] = record
        # Only portable records are embedded; local commands may contain machine paths.
        return {"source": current, "tests": {"automated_tests": "passed"}, "targets": records}

    def archive_source(self, path, records, files):
        prefix = "openfirepower-" + self.ver
        with path.open("wb") as stream:
            with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as gz:
                with tarfile.open(fileobj=gz, mode="w") as t:
                    for rel, desc in files.items():
                        data = (self.root / rel).read_bytes()
                        require(hashlib.sha256(data).hexdigest() == desc["sha256"], "Source changed during packaging")
                        self.tar_bytes(t, prefix + "/" + rel, data, 0o755 if desc["executable"] else 0o644)
                    self.tar_bytes(t, prefix + "/release/build-records.json", canonical(records))
                    self.tar_bytes(t, prefix + "/release/source-inputs.json", canonical(files))

    @staticmethod
    def tar_bytes(t, name, data, mode=0o644):
        entry = tarfile.TarInfo(name)
        entry.size, entry.mode, entry.mtime = len(data), mode, 0
        t.addfile(entry, io.BytesIO(data))

    @staticmethod
    def sums(files):
        return "".join(digest + "  " + name + "\n" for name, digest in sorted(files.items())).encode()

    def make_bundle(self, records, uploader=None):
        current, files = self.inputs()
        require(not (self.work / "receipt.json").exists() and not (self.work / "aminet-receipt.json").exists(),
                "Preserve publication receipts; use a new release work folder for preparing different bytes")
        require(current == records["source"], "Source changed before packaging")
        require(not self.folder.is_symlink(), "Release folder is symlinked")
        self.folder.parent.mkdir(parents=True, exist_ok=True)
        temp = Path(tempfile.mkdtemp(prefix=".prepare-", dir=str(self.folder.parent)))
        try:
            for record in records["targets"].values():
                for rel in record["artifacts"]:
                    shutil.copy2(regular(self.packages / rel), temp / Path(rel).name)
            aminet = None
            if uploader:
                require("amiga" in self.targets, "Aminet needs the Amiga target")
                readme = temp / package_name("client", "amigaos", "m68k", self.ver, "readme")
                readme.write_text(self.aminet_readme(readme.read_text(), uploader))
                names = [package_name("client", "amigaos", "m68k", self.ver, ext) for ext in ("lha", "readme")]
                aminet = {"uploader": uploader, "host": "main.aminet.net", "directory": "/new", "files": {n: sha(temp / n) for n in names}}
            for name in sorted(self.package_names()):
                atomic(temp / (name + ".sha256"), self.sums({name: sha(temp / name)}))
            source = "OpenFirePower-" + self.ver + "-source.tar.gz"
            self.archive_source(temp / source, records, files)
            atomic(temp / "build-records.json", canonical(records))
            downloads = {p.name: sha(p) for p in sorted(temp.iterdir())}
            website = "OpenFirePower-" + self.ver + "-website-update.tar.gz"
            with tarfile.open(temp / website, "w:gz") as t:
                for rel in files:
                    if rel.startswith("public-website/"):
                        self.tar_bytes(t, rel[len("public-website/"):], (self.root / rel).read_bytes())
                for name in downloads:
                    t.add(temp / name, arcname="assets/releases/" + name)
                self.tar_bytes(t, "assets/releases/OpenFirePower-" + self.ver + "-SHA256SUMS.txt", self.sums(downloads))
            inventory = {p.name: sha(p) for p in sorted(temp.iterdir())}
            atomic(temp / "SHA256SUMS", self.sums(inventory))
            require(self.inputs()[0] == current, "Source changed during release packaging")
            if self.folder.exists():
                # Preserve prepared manifests and receipts. Do not silently replace a reviewed release.
                require(not (self.work / "receipt.json").exists(), "A publication receipt exists; use a new work folder instead of replacing release bytes")
                shutil.rmtree(self.folder)
            os.replace(temp, self.folder)
            return aminet
        finally:
            if temp.exists():
                shutil.rmtree(temp)

    def verify_source_archive(self, records, files):
        archive = self.folder / ("OpenFirePower-" + self.ver + "-source.tar.gz")
        prefix = "openfirepower-" + self.ver + "/"
        with tarfile.open(regular(archive)) as t:
            members = t.getmembers()
            names = [m.name for m in members]
            expected = {prefix + n for n in files} | {prefix + "release/build-records.json", prefix + "release/source-inputs.json"}
            require(len(names) == len(set(names)) and set(names) == expected and all(m.isfile() for m in members), "Source archive inventory mismatch")
            for rel, desc in files.items():
                data = t.extractfile(prefix + rel).read()
                require(hashlib.sha256(data).hexdigest() == desc["sha256"], "Stale source archive member: " + rel)
                require(bool(t.getmember(prefix + rel).mode & 0o100) == desc["executable"], "Source archive executable mode differs: " + rel)
            require(t.extractfile(prefix + "release/build-records.json").read() == canonical(records) and
                    t.extractfile(prefix + "release/source-inputs.json").read() == canonical(files), "Source archive build records mismatch")

    def package_names(self):
        return {f for target in self.targets.values() for f, _ in target["files"]}

    def expected_names(self):
        names = self.package_names()
        return names | {n + ".sha256" for n in names} | {"build-records.json", "SHA256SUMS", "OpenFirePower-" + self.ver + "-source.tar.gz",
                        "OpenFirePower-" + self.ver + "-website-update.tar.gz"}

    def check(self):
        records = self.records()
        require(self.folder.is_dir() and not self.folder.is_symlink(), "No release inventory; run prepare after building all targets")
        actual = {p.name for p in self.folder.iterdir()}
        require(actual == self.expected_names(), "Missing or unexpected release artifacts")
        hashes = {n: sha(self.folder / n) for n in sorted(actual)}
        for record in records["targets"].values():
            for rel, expected in record["artifacts"].items():
                name = Path(rel).name
                if name.endswith(".readme") and hashes[name] != expected:
                    base = (self.packages / rel).read_text()
                    final = (self.folder / name).read_text()
                    match = re.match(r"Uploader:     ([^\r\n]+)\n", final)
                    require(match is not None, "Modified Aminet readme")
                    require(final == self.aminet_readme(base, match.group(1)), "Modified Aminet readme")
                else:
                    require(hashes[name] == expected, "Release artifact differs from build record: " + name)
        for name in self.package_names():
            require((self.folder / (name + ".sha256")).read_bytes() == self.sums({name: hashes[name]}), "Package checksum sidecar differs: " + name)
        require((self.folder / "SHA256SUMS").read_bytes() == self.sums({n: d for n, d in hashes.items() if n != "SHA256SUMS"}),
                "Checksum inventory is missing, duplicated, changed or unexpected")
        require((self.folder / "build-records.json").read_bytes() == canonical(records), "Release build records changed")
        _, files = self.inputs()
        self.verify_source_archive(records, files)
        site = self.folder / ("OpenFirePower-" + self.ver + "-website-update.tar.gz")
        with tarfile.open(site) as t:
            members = t.getmembers()
            names = [m.name for m in members]
            require(len(names) == len(set(names)), "Duplicate website members")
            require(all(m.isfile() for m in members), "Unexpected website archive entry")
            downloads = {n: h for n, h in hashes.items() if not n.endswith("-website-update.tar.gz") and n != "SHA256SUMS"}
            expected = {n[len("public-website/"):] for n in files if n.startswith("public-website/")}
            expected |= {"assets/releases/" + n for n in downloads}
            sum_name = "assets/releases/OpenFirePower-" + self.ver + "-SHA256SUMS.txt"
            expected.add(sum_name)
            require(set(names) == expected, "Website file/download inventory mismatch")
            for n in files:
                if n.startswith("public-website/"):
                    require(hashlib.sha256(t.extractfile(n[len("public-website/"):]).read()).hexdigest() == files[n]["sha256"], "Website source changed")
            for n, h in downloads.items():
                require(hashlib.sha256(t.extractfile("assets/releases/" + n).read()).hexdigest() == h, "Website download bytes mismatch")
            require(t.extractfile(sum_name).read() == self.sums(downloads), "Website checksums mismatch")
        return hashes

    def aminet_readme(self, base, uploader):
        require(len(uploader) <= 254 and re.fullmatch(r"[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}", uploader), "Invalid Aminet uploader email")
        author = self.cfg["aminet"]["author"]
        require(author and "\n" not in author and "\r" not in author, "Invalid Aminet author")
        text = re.sub(r"^Uploader:.*\n", "", base, flags=re.M)
        text = re.sub(r"^Author:.*$", lambda _: "Author:       " + author, text, flags=re.M)
        return "Uploader:     " + uploader + "\n" + text

    def prepare(self, notes_path, uploader=None):
        records = self.records()
        notes = regular(notes_path).read_text().strip()
        require(notes and not re.search(r"github_pat_|gh[pousr]_|glpat-", notes), "Missing release notes or credential-like text in notes")
        for key in ("GITHUB_TOKEN", "GITLAB_TOKEN"):
            require(not os.environ.get(key) or os.environ[key] not in notes, "Credential in release notes")
        aminet = self.make_bundle(records, uploader or self.cfg["aminet"]["uploader"])
        hashes = self.check()
        manifest = {"schema": 1, **records["source"], "tag": self.tag, "tag_message": self.tag_message, "title": "OpenFP " + self.ver,
                    "github": self.cfg["github"], "github_git": self.cfg["github_git"],
                    "gitlab": self.cfg["gitlab"], "gitlab_git": self.cfg["gitlab_git"],
                    "targets": self.cfg["targets"], "notes": notes, "files": hashes, "aminet": aminet,
                    "macos_signing": self.cfg["macos"] if "macos" in self.targets else None,
                    "runtime_checks": {n: r["runtime_check"] for n, r in records["targets"].items()}}
        path = self.work / "manifest.json"
        atomic(path, canonical(manifest))
        print("Prepared " + str(path) + "; review notes, destinations, signing policy and SHA256SUMS")
        return manifest

    def verify_manifest(self, manifest, publication=False):
        hashes = self.check()
        current, files = self.inputs()
        require(manifest.get("schema") == 1 and all(manifest.get(k) == v for k, v in current.items()) and
                manifest.get("tag") == self.tag and manifest.get("targets") == self.cfg["targets"] and
                manifest.get("title") == "OpenFP " + self.ver and manifest.get("files") == hashes,
                "Prepared manifest is stale; rebuild/check/prepare as needed")
        require(manifest.get("tag_message", "OpenFP " + self.ver) == self.tag_message, "Prepared tag annotation differs")
        for k in ("github", "github_git", "gitlab", "gitlab_git"):
            require(manifest.get(k) == self.cfg[k], "Prepared destination differs from configuration: " + k)
        require(manifest.get("macos_signing") == (self.cfg["macos"] if "macos" in self.targets else None), "Prepared signing policy differs")
        records = read_json(self.folder / "build-records.json")
        require(manifest.get("runtime_checks") == {n: r["runtime_check"] for n, r in records["targets"].items()}, "Prepared runtime checks differ")
        require(isinstance(manifest.get("notes"), str) and manifest["notes"].strip(), "Missing reviewed release notes")
        if manifest.get("aminet"):
            a = manifest["aminet"]
            expected = {package_name("client", "amigaos", "m68k", self.ver, ext) for ext in ("lha", "readme")}
            require("amiga" in self.targets and a.get("host") == "main.aminet.net" and a.get("directory") == "/new" and
                    set(a.get("files", {})) == expected and all(hashes[n] == a["files"][n] for n in expected), "Prepared Aminet inventory differs")
            name = package_name("client", "amigaos", "m68k", self.ver, "readme")
            require((self.folder / name).read_text() == self.aminet_readme((self.packages / "AmigaOS" / name).read_text(), a["uploader"]),
                    "Prepared Aminet contact differs from readme")
        if publication:
            clean_git(self.root, files, manifest["commit"])
        return hashes
