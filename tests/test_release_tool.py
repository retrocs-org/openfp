# SPDX-License-Identifier: MIT
"""Offline release acceptance tests. All uploads are fakes; Git remotes are temporary folders."""
import contextlib
import copy
import hashlib
import io
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release_common as common
import release_build as build
import release_destinations as destinations
import release as cli


def git(root, *args):
    return subprocess.check_output(["git", "-c", "commit.gpgsign=false", *args], cwd=str(root), stderr=subprocess.DEVNULL).decode().strip()


class FixtureProject(build.Project):
    def test_commands(self):
        return [[sys.executable, "-c", "print('fixture automated checks')"]]

    def validate_installer(self, name):
        # Fixture installers are text. Real package extraction is tested separately.
        pass


class Fixture(unittest.TestCase):
    def enterContext(self, cm):
        value = cm.__enter__()
        self.addCleanup(cm.__exit__, None, None, None)
        return value

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / "source"
        self.root.mkdir()
        self.write("src/common/net.h", '#define OFP_VERSION "0.1"\n')
        self.write("CMakeLists.txt", "project(openfirepower VERSION 0.1 LANGUAGES C)\n")
        self.write("LICENSE", "Fixture project licence\n")
        self.write("CREDITS.md", "Fixture credits\n")
        self.write("THIRD-PARTY-NOTICES.md", "Fixture notices\n")
        self.write("public-website/index.php", "<?php echo 'fixture';\n")
        self.write("data/maps/builtin.txt", "fixture map\n")
        self.write("docs/RELEASE-NOTES.md", "OpenFirePower 0.1\n\nFixture release notes.\n")
        self.write(".gitignore", "/work/\n__pycache__/\n.env\n*.key\n")
        self.write("tests/fake_build.py", '''import os, pathlib, zipfile
w = pathlib.Path(os.environ["WORK"])
d = w / "OpenFirePower"
d.mkdir(parents=True, exist_ok=True)
(d / "openfirepower.exe").write_bytes(b"fixture executable")
for name in ("LICENSE", "CREDITS.md", "THIRD-PARTY-NOTICES.md"):
    (d / name).write_bytes(pathlib.Path(name).read_bytes())
(d / "data/maps").mkdir(parents=True, exist_ok=True)
(d / "data/maps/builtin.txt").write_bytes(pathlib.Path("data/maps/builtin.txt").read_bytes())
with zipfile.ZipFile(w / "openfp-client-windows-x86_64-0.1.zip", "w") as z:
    for f in sorted(d.rglob("*")):
        if f.is_file(): z.write(f, "OpenFirePower/" + str(f.relative_to(d)))
(w / "openfp-client-windows-x86_64-0.1.msi").write_bytes(b"fixture installer")
''')
        path = self.base / "config.json"
        common.atomic(path, common.canonical({"targets": ["windows"], "work": str(self.base / "work"),
            "builds": str(self.base / "builds"), "packages": str(self.base / "packages"), "tools": str(self.base / "tools"),
            "target_settings": {"windows": {"commands": [[sys.executable, "tests/fake_build.py"]], "tools": [[sys.executable, "--version"]]}}}))
        self.config_path = path
        self.project = FixtureProject(self.root, common.config(self.root, path))

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def committed(self):
        git(self.root, "init", "-q")
        git(self.root, "config", "user.email", "release@example.invalid")
        git(self.root, "config", "user.name", "Release tests")
        git(self.root, "add", ".")
        git(self.root, "commit", "-qm", "Fixture")
        return common.git_commit(self.root)

    def built(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.project.build(["windows"])

    def prepared(self):
        self.built()
        with contextlib.redirect_stdout(io.StringIO()):
            return self.project.prepare(self.root / "docs/RELEASE-NOTES.md")


class InputsTests(Fixture):
    def test_plan_is_read_only(self):
        before = list(self.base.rglob("*"))
        with patch.object(build, "run", side_effect=AssertionError("plan executed a command")), contextlib.redirect_stdout(io.StringIO()) as out:
            self.project.plan(["windows"])
        self.assertEqual(before, list(self.base.rglob("*")))
        self.assertIn("--msi", " ".join(build.catalog(self.root, common.DEFAULT, "0.1")["windows"]["commands"][0]))
        self.assertIn("Experimental", out.getvalue())

    def test_default_matrix_and_destinations(self):
        cfg = common.config(self.root)
        self.assertEqual(len(build.catalog(self.root, cfg, "0.1")), 5)
        self.assertEqual(cfg["gitlab"], "https://gitlab.kladhest.se/retrocs-org/openfp")
        self.assertEqual(cfg["github"], "https://github.com/retrocs-org/openfp")

    def test_nested_gitlab_and_matching_transport(self):
        url = common.repo_url("https://gitlab.kladhest.se/a/b/openfp.git", "gitlab.kladhest.se")
        self.assertEqual(url, "https://gitlab.kladhest.se/a/b/openfp")
        common.git_url("git@gitlab.kladhest.se:a/b/openfp.git", url, "gitlab.kladhest.se")
        with self.assertRaises(common.ReleaseError):
            common.git_url("git@gitlab.kladhest.se:a/other.git", url, "gitlab.kladhest.se")

    def test_invalid_repository_urls(self):
        for value in ("http://github.com/a/b", "https://user:pass@github.com/a/b", "https://github.com/a/b?q=x",
                      "https://github.com/a/b#tag", "https://github.com/a/../b", "https://other.invalid/a/b", "https://github.com/a/b/c"):
            with self.subTest(value=value), self.assertRaises(common.ReleaseError):
                common.repo_url(value, "github.com")

    def test_config_rejects_tokens_and_overlapping_outputs(self):
        for extra in ({"env": {"GITHUB_TOKEN": "test"}}, {"github": "https://user:pass@github.com/a/b"},
                      {"builds": str(self.base / "work/builds")}, {"env": {"JOBS": 4}}):
            data = common.read_json(self.config_path)
            data.update(extra)
            common.atomic(self.config_path, common.canonical(data))
            with self.assertRaises(common.ReleaseError):
                common.config(self.root, self.config_path)

    def test_source_fingerprint_covers_docs_tests_website(self):
        before = self.project.inputs()[0]
        for path in ("docs/RELEASE-NOTES.md", "tests/fake_build.py", "public-website/index.php"):
            p = self.root / path
            original = p.read_text()
            p.write_text(original + "\n")
            self.assertNotEqual(self.project.inputs()[0], before)
            p.write_text(original)

    def test_local_credentials_and_generated_records_excluded(self):
        for path in (".env", "work/release-config.json", "private.key", "release/build-records.json", "public-website/config.inc.php", "data/styles/imported/a.png"):
            self.write(path, "fixture secret\n")
        files = common.source_files(self.root)
        self.assertFalse(any("fixture secret" in (self.root / p).read_text() for p in files))

    def test_symlinked_source_rejected(self):
        (self.root / "bad.c").symlink_to(self.root / "LICENSE")
        with self.assertRaises(common.ReleaseError):
            self.project.inputs()

    def test_credentials_in_an_included_file_are_rejected(self):
        secret = "fixture-private-credential-value"
        self.write("accidental.txt", secret)
        with patch.dict(os.environ, {"GITHUB_TOKEN": secret}), self.assertRaisesRegex(common.ReleaseError, "Credential-like"):
            self.project.inputs()

    def test_imported_maps_excluded_but_distribution_notice_tracked(self):
        self.write("data/maps/original/README.txt", "No imported maps are distributed.\n")
        self.write("data/maps/original/imported.ofp", "private map")
        files = common.source_files(self.root)
        self.assertIn("data/maps/original/README.txt", files)
        self.assertNotIn("data/maps/original/imported.ofp", files)

    def test_version_mismatch_and_duplicate_version_rejected(self):
        self.write("CMakeLists.txt", "project(openfirepower VERSION 0.2 LANGUAGES C)\n")
        with self.assertRaises(common.ReleaseError):
            common.version(self.root)
        self.write("src/common/net.h", '#define OFP_VERSION "0.1"\n#define OFP_VERSION "0.1"\n')
        with self.assertRaises(common.ReleaseError):
            common.version(self.root)

    def test_duplicate_json_keys_rejected(self):
        self.config_path.write_text('{"targets": [], "targets": ["windows"]}')
        with self.assertRaises(common.ReleaseError):
            common.read_json(self.config_path)


class BuildTests(Fixture):
    def test_build_check_prepare_and_runtime_separation(self):
        manifest = self.prepared()
        self.project.verify_manifest(manifest)
        self.assertIsNone(manifest["commit"])
        self.assertEqual(manifest["runtime_checks"], {"windows": "not-recorded"})
        records = common.read_json(self.project.folder / "build-records.json")
        self.assertEqual(records["tests"]["automated_tests"], "passed")
        self.assertIn("Python", records["targets"]["windows"]["tool_versions"][0])
        self.assertEqual(set(manifest["files"]), self.project.expected_names())

    def test_source_mutation_blocks_check(self):
        self.built()
        self.write("public-website/index.php", "changed caption\n")
        with self.assertRaisesRegex(common.ReleaseError, "stale"):
            self.project.check()

    def test_changed_ready_binary_rejected(self):
        self.built()
        (self.project.targets["windows"]["dest"] / "openfirepower.exe").write_bytes(b"changed")
        with self.assertRaisesRegex(common.ReleaseError, "binary changed"):
            self.project.check()

    def test_missing_and_changed_packages_rejected(self):
        self.built()
        name = self.project.packages / "Windows/openfp-client-windows-x86_64-0.1.msi"
        name.write_bytes(b"changed")
        with self.assertRaisesRegex(common.ReleaseError, "Changed artifact"):
            self.project.check()
        name.unlink()
        with self.assertRaises(common.ReleaseError):
            self.project.check()

    def test_missing_extra_and_symlink_release_assets_rejected(self):
        self.built()
        extra = self.project.folder / "extra.zip"
        extra.write_bytes(b"extra")
        with self.assertRaisesRegex(common.ReleaseError, "unexpected"):
            self.project.check()
        extra.unlink()
        asset = self.project.folder / "openfp-client-windows-x86_64-0.1.msi"
        contents = asset.read_bytes()
        asset.unlink()
        with self.assertRaises(common.ReleaseError):
            self.project.check()
        other = self.base / "same.msi"
        other.write_bytes(contents)
        asset.symlink_to(other)
        with self.assertRaisesRegex(common.ReleaseError, "symlinked"):
            self.project.check()

    def test_duplicate_checksum_entries_rejected(self):
        self.built()
        p = self.project.folder / "SHA256SUMS"
        p.write_bytes(p.read_bytes() + p.read_bytes().splitlines()[0] + b"\n")
        with self.assertRaisesRegex(common.ReleaseError, "Checksum"):
            self.project.check()

    def test_changed_staged_asset_rejected_even_with_updated_checksum(self):
        self.built()
        p = self.project.folder / "openfp-client-windows-x86_64-0.1.msi"
        p.write_bytes(b"changed")
        hashes = {f.name: common.sha(f) for f in self.project.folder.iterdir() if f.name != "SHA256SUMS"}
        common.atomic(self.project.folder / "SHA256SUMS", self.project.sums(hashes))
        with self.assertRaisesRegex(common.ReleaseError, "differs from build record"):
            self.project.check()

    def test_website_downloads_and_source_records_match(self):
        manifest = self.prepared()
        with tarfile.open(self.project.folder / "OpenFirePower-0.1-website-update.tar.gz") as t:
            for name, expected in manifest["files"].items():
                if name not in ("SHA256SUMS", "OpenFirePower-0.1-website-update.tar.gz"):
                    self.assertEqual(hashlib.sha256(t.extractfile("assets/releases/" + name).read()).hexdigest(), expected)
        with tarfile.open(self.project.folder / "OpenFirePower-0.1-source.tar.gz") as t:
            self.assertIn("openfirepower-0.1/release/source-inputs.json", t.getnames())
            self.assertIn("openfirepower-0.1/release/build-records.json", t.getnames())

    def test_source_change_during_build_invalidates_record(self):
        original = build.run
        def mutate(argv, *args, **kwargs):
            result = original(argv, *args, **kwargs)
            if "tests/fake_build.py" in argv:
                self.write("src/changed.c", "changed\n")
            return result
        with patch.object(build, "run", side_effect=mutate), contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(common.ReleaseError, "Source changed"):
            self.project.build(["windows"])
        self.assertFalse((self.project.work / "records/windows.json").exists())

    def test_incomplete_matrix_blocks_packaging(self):
        self.built()
        self.project.cfg["targets"].append("amiga")
        with self.assertRaises(common.ReleaseError):
            self.project.records()

    def test_failed_build_cannot_reuse_previous_record(self):
        self.built()
        self.project.targets["windows"]["commands"] = [[sys.executable, "-c", "raise SystemExit(1)"]]
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(common.ReleaseError):
            self.project.build(["windows"])
        self.assertFalse((self.project.work / "records/windows.json").exists())

    def test_receipt_prevents_replacing_reviewed_bundle(self):
        self.built()
        common.atomic(self.project.work / "receipt.json", b"{}")
        with self.assertRaisesRegex(common.ReleaseError, "Preserve publication"):
            self.project.make_bundle(self.project.records())

    def test_policy_and_runtime_manifest_changes_rejected(self):
        manifest = self.prepared()
        manifest["runtime_checks"]["windows"] = "passed"
        with self.assertRaisesRegex(common.ReleaseError, "runtime"):
            self.project.verify_manifest(manifest)

    def test_macos_notarization_check_does_not_assume_success(self):
        cfg = copy.deepcopy(self.project.cfg)
        cfg["targets"] = ["macos"]
        cfg["macos"] = {"identity": "Developer ID Application: Test", "notary_profile": "test", "require_notarization": True}
        p = FixtureProject(self.root, cfg)
        with patch.object(build.platform, "system", return_value="Darwin"), patch.object(build, "run", return_value="Authority=Developer ID Application: Test\n") as run:
            self.assertEqual(p.signing_check("macos")["notarization"], "staples-verified")
            self.assertEqual(sum("stapler" in c.args[0] for c in run.call_args_list), 2)
            run.side_effect = common.ReleaseError("staple invalid")
            with self.assertRaises(common.ReleaseError):
                p.signing_check("macos")

    @unittest.skipUnless(shutil.which("wixl") and shutil.which("msiextract"), "msitools is optional for offline checks")
    def test_real_windows_installer_payload_round_trip(self):
        self.built()
        script = Path(__file__).resolve().parents[1] / "tools/package_windows_msi.py"
        installer = self.project.packages / "Windows/openfp-client-windows-x86_64-0.1.msi"
        result = subprocess.run([sys.executable, str(script), str(self.project.targets["windows"]["dest"]), "0.1", str(installer)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        build.Project.validate_installer(self.project, "windows")
        (self.project.targets["windows"]["dest"] / "openfirepower.exe").write_bytes(b"different")
        with self.assertRaisesRegex(common.ReleaseError, "payload differs"):
            build.Project.validate_installer(self.project, "windows")


class GitTests(Fixture):
    def test_extracted_tree_cannot_publish(self):
        m = self.prepared()
        with self.assertRaisesRegex(common.ReleaseError, "Git checkout"):
            self.project.verify_manifest(m, publication=True)

    def test_clean_git_and_dirty_untracked_ignored_inputs(self):
        commit = self.committed()
        files = common.source_files(self.root)
        common.clean_git(self.root, files, commit)
        self.write("LICENSE", "modified")
        with self.assertRaises(common.ReleaseError): common.clean_git(self.root, files, commit)
        git(self.root, "checkout", "--", "LICENSE")
        self.write("new.c", "new")
        with self.assertRaises(common.ReleaseError): common.clean_git(self.root, common.source_files(self.root), commit)
        (self.root / "new.c").unlink()
        self.write(".git/info/exclude", "ignored.c\n")
        self.write("ignored.c", "required input")
        with self.assertRaisesRegex(common.ReleaseError, "untracked or ignored"):
            common.clean_git(self.root, common.source_files(self.root), commit)

    def test_commit_change_requires_rebuild(self):
        self.committed()
        self.built()
        git(self.root, "commit", "--allow-empty", "-qm", "New source commit")
        with self.assertRaisesRegex(common.ReleaseError, "stale"):
            self.project.check()

    def test_annotated_tag_push_and_no_force_on_conflict(self):
        commit = self.committed()
        remote = self.base / "remote.git"
        remote.mkdir()
        git(remote, "init", "--bare", "-q")
        m = {"tag": "v0.1", "commit": commit, "title": "OpenFirePower 0.1"}
        with contextlib.redirect_stdout(io.StringIO()):
            destinations.push_tag(self.root, str(remote), m)
            destinations.push_tag(self.root, str(remote), m)
        self.assertTrue(destinations.local_tag(self.root, m))
        self.assertEqual(destinations.remote_tag(self.root, str(remote), m)["refs/tags/v0.1^{}"], commit)
        git(self.root, "commit", "--allow-empty", "-qm", "Other commit")
        m["commit"] = common.git_commit(self.root)
        with self.assertRaisesRegex(common.ReleaseError, "never moved"):
            destinations.remote_tag(self.root, str(remote), m)

    def test_lightweight_local_tag_rejected(self):
        commit = self.committed()
        git(self.root, "tag", "v0.1")
        with self.assertRaisesRegex(common.ReleaseError, "not annotated"):
            destinations.local_tag(self.root, {"tag": "v0.1", "commit": commit})

    def test_worktree_marker_is_not_a_release_input(self):
        self.committed()
        worktree = self.base / "worktree"
        git(self.root, "worktree", "add", "--detach", str(worktree))
        files = common.source_files(worktree)
        self.assertNotIn(".git", files)
        self.assertEqual(files, common.source_files(self.root))
        common.clean_git(worktree, files, common.git_commit(worktree))


class GitHubAPI:
    def __init__(self, manifest):
        self.m, self.events, self.item, self.assets = manifest, [], None, {}
        self.bad_digest = False
        self.fail_name = None

    def request(self, method, url, **kw):
        self.events.append((method, url))
        if method == "GET" and url.endswith("/repos/retrocs-org/openfp"):
            return {"full_name": "retrocs-org/openfp", "permissions": {"push": True}}
        if method == "GET" and "/releases/tags/" in url: return copy.deepcopy(self.item)
        if method == "GET" and "/releases?per_page=" in url: return [copy.deepcopy(self.item)] if self.item else []
        if method == "GET" and "/assets?" in url: return list(copy.deepcopy(self.assets).values())
        if method == "POST" and url.endswith("/releases"):
            self.item = {"id": 1, **kw["data"]}
            return copy.deepcopy(self.item)
        if method == "POST" and "/assets?name=" in url:
            name = Path(kw["file"]).name
            if name == self.fail_name: raise common.ReleaseError("interrupted upload")
            asset = {"name": name, "state": "uploaded", "digest": "sha256:" + ("bad" if self.bad_digest else self.m["files"][name])}
            self.assets[name] = asset
            return copy.deepcopy(asset)
        if method == "PATCH":
            self.item.update(kw["data"])
            return copy.deepcopy(self.item)
        raise AssertionError((method, url))


class GitLabAPI:
    def __init__(self, manifest):
        self.m, self.events, self.item, self.packages = manifest, [], None, {}
        self.bad_download = False
        self.scopes = ["api"]

    def request(self, method, url, **kw):
        self.events.append((method, url))
        if url.endswith("/personal_access_tokens/self"):
            return {"active": True, "revoked": False, "scopes": self.scopes}
        if method == "GET" and url.endswith("projects/retrocs-org%2Fopenfp"):
            return {"path_with_namespace": "retrocs-org/openfp", "permissions": {"group_access": {"access_level": 40}}}
        if method == "GET" and "/releases/" in url: return copy.deepcopy(self.item)
        if "/packages/generic/" in url:
            name = url.rsplit("/", 1)[1]
            if method == "GET": return ("bad" if self.bad_download else self.packages[name]) if name in self.packages else None
            if method == "PUT": self.packages[name] = common.sha(kw["file"]); return {}
        if method == "POST" and url.endswith("/releases"):
            self.item = kw["data"]
            return copy.deepcopy(self.item)
        raise AssertionError((method, url))


class AdapterTests(Fixture):
    def manifest(self):
        folder = self.base / "assets"
        folder.mkdir()
        files = {}
        for name in ("a.zip", "b.txt"):
            (folder / name).write_bytes(name.encode())
            files[name] = common.sha(folder / name)
        return {"tag": "v0.1", "version": "0.1", "commit": "1" * 40, "title": "OpenFirePower 0.1", "notes": "Reviewed notes",
                "files": files, "github": common.DEFAULT["github"], "gitlab": common.DEFAULT["gitlab"]}, folder

    def test_github_draft_becomes_public_last(self):
        m, folder = self.manifest()
        api = GitHubAPI(m)
        a = destinations.GitHub(m, folder, "", api)
        a.preflight(); a.publish()
        self.assertFalse(api.item["draft"])
        self.assertEqual(set(api.assets), set(m["files"]))
        self.assertEqual(api.events[-1][0], "PATCH")
        self.assertEqual(sum(e[0] == "POST" and "/assets?" in e[1] for e in api.events), 2)

    def test_github_unknown_digest_and_notes_conflicts_stop(self):
        m, folder = self.manifest()
        api = GitHubAPI(m)
        a = destinations.GitHub(m, folder, "", api)
        api.bad_digest = True
        with self.assertRaisesRegex(common.ReleaseError, "digest mismatch"): a.publish()
        self.assertTrue(api.item["draft"])
        api.assets["a.zip"]["digest"] = None
        with self.assertRaisesRegex(common.ReleaseError, "digest"): a.preflight()
        api.item["body"] = "other notes"
        with self.assertRaisesRegex(common.ReleaseError, "notes"): a.preflight()

    def test_github_interruption_reuses_only_equal_assets(self):
        m, folder = self.manifest()
        api = GitHubAPI(m)
        a = destinations.GitHub(m, folder, "", api)
        api.fail_name = "b.txt"
        with self.assertRaises(common.ReleaseError): a.publish()
        api.fail_name = None
        api.events.clear()
        a.preflight(); a.publish()
        uploads = [url for method, url in api.events if method == "POST" and "/assets?" in url]
        self.assertEqual(len(uploads), 1)
        self.assertTrue(uploads[0].endswith("b.txt"))
        api.assets["a.zip"]["digest"] = "sha256:conflict"
        with self.assertRaises(common.ReleaseError): a.preflight()

    def test_github_extra_and_duplicate_assets_rejected(self):
        m, folder = self.manifest()
        api = GitHubAPI(m)
        a = destinations.GitHub(m, folder, "", api)
        a.publish()
        api.assets["extra"] = {"name": "extra"}
        with self.assertRaisesRegex(common.ReleaseError, "Unexpected"): a.preflight()
        api.assets["extra"] = api.assets["a.zip"]
        with self.assertRaisesRegex(common.ReleaseError, "Duplicate"): a.preflight()

    def test_gitlab_download_verifies_before_advertising(self):
        m, folder = self.manifest()
        api = GitLabAPI(m)
        a = destinations.GitLab(m, folder, "", api)
        a.preflight(); a.publish()
        release_index = next(i for i, e in enumerate(api.events) if e[0] == "POST")
        self.assertTrue(all(i < release_index for i, e in enumerate(api.events) if e[0] == "PUT"))
        for name in m["files"]:
            upload = next(i for i, (verb, url) in enumerate(api.events) if verb == "PUT" and url.endswith(name))
            self.assertEqual(api.events[upload + 1], ("GET", a.package_url(name)))

    def test_gitlab_bad_download_never_creates_release(self):
        m, folder = self.manifest()
        api = GitLabAPI(m); api.bad_download = True
        with self.assertRaisesRegex(common.ReleaseError, "verification"):
            destinations.GitLab(m, folder, "", api).publish()
        self.assertIsNone(api.item)

    def test_gitlab_read_only_token_stops_preflight(self):
        m, folder = self.manifest()
        api = GitLabAPI(m); api.scopes = ["read_api"]
        with self.assertRaisesRegex(common.ReleaseError, "api scope"):
            destinations.GitLab(m, folder, "", api).preflight()
        self.assertFalse(any(method in ("POST", "PUT") for method, url in api.events))

    def test_gitlab_equal_assets_reused_and_conflicts_not_overwritten(self):
        m, folder = self.manifest()
        api = GitLabAPI(m); api.packages = m["files"].copy()
        a = destinations.GitLab(m, folder, "", api)
        a.preflight(); a.publish()
        self.assertFalse(any(v == "PUT" for v, u in api.events))
        api.packages["a.zip"] = "different"
        with self.assertRaisesRegex(common.ReleaseError, "Conflicting"): a.preflight()
        api.packages = m["files"].copy()
        api.item["assets"]["links"][0]["url"] = "https://other.invalid/changed"
        with self.assertRaisesRegex(common.ReleaseError, "links conflict"): a.preflight()

    def test_http_does_not_send_credentials_to_unexpected_host(self):
        with patch.object(destinations.http.client, "HTTPSConnection") as conn:
            with self.assertRaises(common.ReleaseError):
                destinations.HTTP("github", "api.github.com", "fixture").request("GET", "https://other.invalid/api")
            conn.assert_not_called()

    def test_redirect_download_drops_all_authentication(self):
        response = unittest.mock.Mock(status=200)
        response.read.side_effect = [b"fixture", b""]
        conn = unittest.mock.Mock()
        conn.getresponse.return_value = response
        with patch.object(destinations.http.client, "HTTPSConnection", return_value=conn):
            got = destinations.HTTP.public_digest("https://objects.example.invalid/file?signature=x")
        self.assertEqual(got, hashlib.sha256(b"fixture").hexdigest())
        headers = conn.request.call_args.kwargs["headers"]
        self.assertNotIn("Authorization", headers)
        self.assertNotIn("PRIVATE-TOKEN", headers)


class PublicationTests(Fixture):
    def setup_publish(self):
        self.committed()
        m = self.prepared()
        events = []
        class Service:
            def __init__(self, name, fail=False): self.name, self.fail = name, fail
            def preflight(self): events.append("preflight-" + self.name)
            def publish(self):
                events.append("publish-" + self.name)
                if self.fail: raise common.ReleaseError("fixture service failure")
        adapters = {name: Service(name) for name in ("gitlab", "github")}
        self.enterContext(patch.dict(os.environ, {"GITHUB_TOKEN": "fixture-github", "GITLAB_TOKEN": "fixture-gitlab"}))
        self.enterContext(patch.object(destinations, "remote_tag", side_effect=lambda *a: events.append("read-tag")))
        self.enterContext(patch.object(destinations, "push_tag", side_effect=lambda *a: events.append("push-tag")))
        return m, events, adapters

    def invoke(self, m, adapters, names=None, confirmed=True):
        with contextlib.redirect_stdout(io.StringIO()):
            destinations.publish(self.root, m, self.project.folder, self.project.work, names or ["gitlab", "github"], confirmed,
                                 lambda publication=False: self.project.verify_manifest(m, publication), adapters)

    def test_all_preflights_before_any_mutation(self):
        m, events, adapters = self.setup_publish()
        self.invoke(m, adapters)
        self.assertLess(events.index("preflight-github"), events.index("push-tag"))
        self.assertEqual(common.read_json(self.project.work / "receipt.json")["completed"], ["gitlab", "github"])

    def test_missing_token_and_confirmation_have_no_side_effects(self):
        m, events, adapters = self.setup_publish()
        with self.assertRaisesRegex(common.ReleaseError, "confirm"): self.invoke(m, adapters, confirmed=False)
        with patch.dict(os.environ, {"GITHUB_TOKEN": ""}), self.assertRaisesRegex(common.ReleaseError, "GITHUB_TOKEN"):
            self.invoke(m, adapters)
        self.assertEqual(events, [])

    def test_bad_destination_duplicate_or_remote_tag_stops_before_upload(self):
        m, events, adapters = self.setup_publish()
        for names in (["github", "github"], ["invalid"]):
            with self.assertRaises(common.ReleaseError): self.invoke(m, adapters, names)
        with patch.object(destinations, "remote_tag", side_effect=common.ReleaseError("Remote tag conflict")), self.assertRaises(common.ReleaseError):
            self.invoke(m, adapters)
        self.assertFalse(any(e.startswith("publish-") or e == "push-tag" for e in events))

    def test_completed_destination_survives_later_failure_and_resume(self):
        m, events, adapters = self.setup_publish()
        adapters["github"].fail = True
        with self.assertRaises(common.ReleaseError): self.invoke(m, adapters)
        receipt = common.read_json(self.project.work / "receipt.json")
        self.assertEqual(receipt["completed"], ["gitlab"])
        self.assertEqual(receipt["current"], "github")
        adapters["github"].fail = False
        self.invoke(m, adapters, ["github"])
        self.assertEqual(common.read_json(self.project.work / "receipt.json")["completed"], ["gitlab", "github"])

    def test_receipt_is_bound_to_manifest(self):
        p = self.base / "receipt.json"
        destinations.Receipt(p, "first").start("github")
        with self.assertRaisesRegex(common.ReleaseError, "another manifest"):
            destinations.Receipt(p, "second")

    def test_secrets_not_in_logs_manifest_or_source_archive(self):
        value = "fixture-private-value-123"
        with patch.dict(os.environ, {"GITHUB_TOKEN": value}), contextlib.redirect_stdout(io.StringIO()) as out:
            common.run([sys.executable, "-c", "import os; print(os.environ['GITHUB_TOKEN'])"], self.root)
            m = self.prepared()
            self.assertNotIn(value, json.dumps(m))
            with tarfile.open(self.project.folder / "OpenFirePower-0.1-source.tar.gz") as t:
                self.assertFalse(any(value.encode() in t.extractfile(n).read() for n in t.getnames()))
        self.assertNotIn(value, out.getvalue())

    def test_lock_prevents_concurrent_mutation(self):
        with cli.locked(self.base / "lock"):
            with self.assertRaises(common.ReleaseError):
                with cli.locked(self.base / "lock"): pass


class AminetTests(Fixture):
    def setup_aminet(self, fail=False):
        folder = self.base / "aminet"
        folder.mkdir()
        names = ["openfp-client-amigaos-m68k-0.1.lha", "openfp-client-amigaos-m68k-0.1.readme"]
        for n in names: (folder / n).write_bytes(n.encode())
        hashes = {n: common.sha(folder / n) for n in names}
        m = {"version": "0.1", "files": hashes, "aminet": {"host": "main.aminet.net", "directory": "/new", "uploader": "release@example.invalid", "files": hashes}}
        events = []
        class FTP:
            def __init__(self, *a, **k): events.append("connect")
            def __enter__(self): return self
            def __exit__(self, *a): pass
            def login(self, *a): events.append("login")
            def cwd(self, *a): pass
            def nlst(self): return []
            def storbinary(self, cmd, f):
                events.append(cmd)
                if fail and cmd.endswith(".readme"): raise common.ReleaseError("fixture interruption")
        path = self.base / "aminet-receipt.json"
        return destinations.Aminet(m, folder, path, FTP), events, path

    def test_submission_readme_last_and_receipt_prevents_duplicate(self):
        a, events, path = self.setup_aminet()
        a.preflight(); a.publish()
        uploads = [e for e in events if e.startswith("STOR")]
        self.assertTrue(uploads[-1].endswith(".readme"))
        self.assertEqual(common.read_json(path)["state"], "submitted")
        before = list(events)
        a.preflight(); a.publish()
        self.assertEqual(before, events)

    def test_partial_upload_cannot_be_automatically_repeated(self):
        a, events, path = self.setup_aminet(fail=True)
        a.preflight()
        with self.assertRaises(common.ReleaseError): a.publish()
        self.assertEqual(common.read_json(path)["uploaded"], ["openfp-client-amigaos-m68k-0.1.lha"])
        before = list(events)
        with self.assertRaisesRegex(common.ReleaseError, "partial"): a.preflight()
        self.assertEqual(before, events)

    def test_contact_argument_and_header_validation(self):
        base = "Short: game\nAuthor: old\nType: game/actio\n"
        value = self.project.aminet_readme(base, "release@example.invalid")
        self.assertTrue(value.startswith("Uploader:     release@example.invalid\n"))
        self.assertIn("OpenFirePower contributors", value)
        with self.assertRaises(common.ReleaseError):
            self.project.aminet_readme(base, "bad\nUploader: injected")


if __name__ == "__main__":
    unittest.main()
