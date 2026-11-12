# SPDX-License-Identifier: MIT
"""Shared settings, credential providers and source publication, entirely offline."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
from unittest.mock import Mock, patch

from test_release_tool import Fixture, FixtureProject, git
import release_common as common
import release_credentials as credentials
import release_source as source
import release_destinations as destinations
import release as cli


class ConfigTests(Fixture):
    def test_external_config_priority_and_project_override(self):
        home = self.base / "home"
        shared = home / "Development/tools/git-release/projects/openfp.json"
        common.atomic(shared, common.canonical({"source_branch": "shared"}))
        self.write("work/release-config.json", '{"source_branch":"legacy"}')
        with patch.object(Path, "home", return_value=home), patch.dict(os.environ, {"OFP_RELEASE_CONFIG": ""}):
            self.assertEqual(common.config(self.root)["source_branch"], "shared")
            self.assertEqual(common.config(self.root, "work/release-config.json")["source_branch"], "legacy")
            with patch.dict(os.environ, {"OFP_RELEASE_CONFIG": str(self.config_path)}):
                self.assertEqual(common.config_path(self.root), self.config_path)
            shared.unlink()
            self.assertEqual(common.config(self.root)["source_branch"], "legacy")

    def test_missing_explicit_or_environment_config_fails(self):
        with patch.dict(os.environ, {"OFP_RELEASE_CONFIG": str(self.base / "missing.json")}), self.assertRaises(common.ReleaseError):
            common.config(self.root)

    def test_config_accepts_lookup_specs_but_not_inline_credentials(self):
        cfg = common.read_json(self.config_path)
        cfg["credentials"] = {"github": {"provider": "1password", "vault": "Example", "item": "github.com", "field": "org/openfp-rw"}}
        common.atomic(self.config_path, common.canonical(cfg))
        common.config(self.root, self.config_path)
        cfg["credentials"]["github"]["token"] = "inline-value"
        common.atomic(self.config_path, common.canonical(cfg))
        with self.assertRaises(common.ReleaseError):
            common.config(self.root, self.config_path)

    def test_plan_never_reads_credentials_or_runs_a_build(self):
        self.project.cfg["credentials"] = {"github": {"provider": "1password", "reference": "op://vault/item/field"}}
        with patch.object(credentials.subprocess, "run", side_effect=AssertionError("credential/build requested")), contextlib.redirect_stdout(io.StringIO()):
            self.project.plan(["windows"])

    def test_configured_python_and_amiga_compiler_reach_commands(self):
        cfg = common.config(self.root, self.config_path)
        cfg["targets"] = ["amiga"]
        cfg["env"] = {"OFP_RELEASE_PYTHON": "/tools/python/bin/python", "AMIGA_CC": "/tools/amiga/bin/cc", "LHA": "/tools/amiga/bin/lha"}
        p = FixtureProject(self.root, cfg)
        self.assertEqual(p.targets["amiga"]["tools"][0][0], "/tools/amiga/bin/cc")
        self.assertEqual(p.env("amiga")["PYTHON"], "/tools/python/bin/python")


class CredentialTests(Fixture):
    def tearDown(self):
        common.SECRETS.clear()

    def spec(self, **extra):
        return {"credentials": {"github": {"provider": "1password", "reference": "op://vault/item/field", **extra}}}

    def test_environment_provider_with_custom_variable(self):
        with patch.dict(os.environ, {"REPO_ACCESS": "fixture-custom-value"}):
            cfg = {"credentials": {"github": {"provider": "env", "variable": "REPO_ACCESS"}}}
            self.assertEqual(credentials.credential("github", cfg), "fixture-custom-value")
            self.assertEqual(common.redact("error fixture-custom-value"), "error [redacted]")

    def test_exact_reference_and_account_are_captured_without_logging(self):
        result = Mock(returncode=0, stdout="fixture-private-value", stderr="")
        with patch.object(credentials.subprocess, "run", return_value=result) as lookup, contextlib.redirect_stdout(io.StringIO()) as log:
            self.assertEqual(credentials.credential("github", self.spec(account="example")), "fixture-private-value")
        self.assertEqual(lookup.call_args.args[0], ["op", "read", "--no-newline", "op://vault/item/field", "--account", "example"])
        self.assertEqual(log.getvalue(), "")
        self.assertNotIn("fixture-private-value", common.redact("fixture-private-value"))

    def test_named_field_with_slash_and_unique_match(self):
        cfg = {"credentials": {"github": {"provider": "1password", "vault": "Example", "item": "github.com", "field": "org/openfp-rw"}}}
        result = Mock(returncode=0, stdout=json.dumps([{"label": "org/openfp-rw", "value": "fixture-named-token"}]))
        with patch.object(credentials.subprocess, "run", return_value=result) as lookup:
            self.assertEqual(credentials.credential("github", cfg), "fixture-named-token")
        self.assertIn("label=org/openfp-rw", lookup.call_args.args[0])
        result.stdout = json.dumps([{"label": "org/openfp-rw", "value": "a"}, {"label": "org/openfp-rw", "value": "b"}])
        with patch.object(credentials.subprocess, "run", return_value=result), self.assertRaisesRegex(common.ReleaseError, "ambiguous"):
            credentials.credential("github", cfg)

    def test_failed_cancelled_and_unresolved_lookup_never_prints_output(self):
        for result in (Mock(returncode=1, stdout="secret-output", stderr="secret-error"),
                       Mock(returncode=0, stdout="op://unresolved/item/field", stderr="")):
            with patch.object(credentials.subprocess, "run", return_value=result), self.assertRaises(common.ReleaseError) as error:
                credentials.credential("github", self.spec())
            self.assertNotIn("secret-output", str(error.exception))
            self.assertNotIn("secret-error", str(error.exception))

    def test_invalid_lookup_specifications_fail_before_running_op(self):
        values = [self.spec(vault="both"), self.spec(reference="plain-token"), self.spec(account="--inject"),
                  {"credentials": {"github": {"provider": "1password", "vault": "only-one"}}},
                  {"credentials": {"github": {"provider": "env", "variable": "BAD NAME"}}}]
        for cfg in values:
            with patch.object(credentials.subprocess, "run") as lookup, self.assertRaises(common.ReleaseError):
                credentials.credential("github", cfg)
            lookup.assert_not_called()

    def test_build_does_not_inherit_publishing_credentials(self):
        env = {"GITHUB_TOKEN": "fixture-secret", "OP_SERVICE_ACCOUNT_TOKEN": "fixture-op", "OP_SESSION_test": "session", "CUSTOM_RELEASE_TOKEN": "fixture-custom"}
        cfg = {"env": {}, "credentials": {"gitlab": {"provider": "env", "variable": "CUSTOM_RELEASE_TOKEN"}}}
        with patch.dict(os.environ, env), contextlib.redirect_stdout(io.StringIO()) as output:
            common.run([sys.executable, "-c", "import os; print(','.join(k for k in os.environ if k in " + repr(list(env)) + "))"], self.root,
                       credentials.build_environment(cfg), inherit=False)
        self.assertEqual(output.getvalue().strip(), "")

    def test_no_credential_lookup_until_publication_confirmed_and_verified(self):
        with patch.object(destinations, "credential") as lookup:
            with self.assertRaises(common.ReleaseError):
                destinations.publish(self.root, {}, self.base, self.base, ["github"], False, lambda **k: None)
            with self.assertRaises(common.ReleaseError):
                destinations.publish(self.root, {}, self.base, self.base, ["github"], True,
                                     lambda **k: common.require(False, "stale builds"))
            lookup.assert_not_called()


class SourceTests(Fixture):
    def remotes(self):
        cfg = self.project.cfg.copy()
        cfg["source_branch"] = "main"
        for service in ("github", "gitlab"):
            path = self.base / (service + ".git")
            git(self.base, "init", "--bare", "-q", str(path))
            cfg[service + "_git"] = str(path)
        return cfg

    def invoke(self, cfg, **flags):
        with contextlib.redirect_stdout(io.StringIO()):
            source.push_source(self.root, cfg, ["gitlab", "github"], **flags)

    def test_source_dry_run_does_not_push_refs_or_tags(self):
        self.committed()
        cfg = self.remotes()
        git(self.root, "tag", "-a", "unrelated", "-m", "local only")
        git(self.root, "config", "push.followTags", "true")
        self.invoke(cfg, dry_run=True)
        for service in ("github", "gitlab"):
            self.assertEqual(git(self.root, "ls-remote", "--refs", cfg[service + "_git"]), "")

    def test_source_pushes_only_reviewed_branch_and_allows_no_force(self):
        commit = self.committed()
        cfg = self.remotes()
        git(self.root, "tag", "-a", "unrelated", "-m", "local only")
        git(self.root, "config", "push.followTags", "true")
        self.invoke(cfg, confirmed=True)
        for service in ("github", "gitlab"):
            self.assertEqual(git(self.root, "ls-remote", "--refs", cfg[service + "_git"]), commit + "\trefs/heads/main")

    def test_source_needs_explicit_action_and_clean_checkout(self):
        self.committed()
        cfg = self.remotes()
        with self.assertRaises(common.ReleaseError): self.invoke(cfg)
        self.write("README.md", "dirty\n")
        with self.assertRaises(common.ReleaseError): self.invoke(cfg, confirmed=True)
        for service in ("github", "gitlab"):
            self.assertEqual(git(self.root, "ls-remote", "--refs", cfg[service + "_git"]), "")

    def test_init_dry_run_creates_local_commit_only_for_empty_remotes(self):
        cfg = self.remotes()
        env = {"GIT_AUTHOR_NAME": "Fixture", "GIT_AUTHOR_EMAIL": "fixture@example.invalid", "GIT_COMMITTER_NAME": "Fixture", "GIT_COMMITTER_EMAIL": "fixture@example.invalid"}
        self.write("work/private.txt", "not source")
        with patch.dict(os.environ, env): self.invoke(cfg, dry_run=True, initialize=True, message="First local source")
        self.assertEqual(git(self.root, "log", "-1", "--format=%s"), "First local source")
        self.assertNotIn("work/private.txt", git(self.root, "ls-files"))
        self.assertEqual(git(self.root, "ls-remote", "--refs", cfg["github_git"]), "")

    def test_init_checks_second_remote_before_changing_local_repository(self):
        cfg = self.remotes()
        other = self.base / "other"
        other.mkdir()
        git(other, "init", "-q")
        git(other, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "--allow-empty", "-qm", "Already exists")
        git(other, "push", "-q", cfg["github_git"], "HEAD:refs/heads/main")
        with self.assertRaisesRegex(common.ReleaseError, "history"):
            self.invoke(cfg, dry_run=True, initialize=True)
        self.assertFalse((self.root / ".git").exists())

    def test_all_source_preflights_run_before_first_push(self):
        self.committed()
        cfg = self.remotes()
        events = []
        actual = source.run
        def intercept(argv, *args, **kw):
            if "push" in argv:
                events.append(argv)
                if "--dry-run" in argv and cfg["github_git"] in argv:
                    raise common.ReleaseError("second destination rejected")
            return actual(argv, *args, **kw)
        with patch.object(source, "run", side_effect=intercept), self.assertRaises(common.ReleaseError):
            self.invoke(cfg, confirmed=True)
        self.assertTrue(events and all("--dry-run" in cmd for cmd in events))
        self.assertEqual(git(self.root, "ls-remote", "--refs", cfg["gitlab_git"]), "")

    def test_init_does_not_add_commits_to_existing_checkout(self):
        commit = self.committed()
        cfg = self.remotes()
        self.invoke(cfg, dry_run=True, initialize=True, message="Should not commit")
        self.assertEqual(common.git_commit(self.root), commit)

    def test_nested_project_initialization_rejected(self):
        self.committed()
        nested = self.root / "nested"
        nested.mkdir()
        with self.assertRaisesRegex(common.ReleaseError, "separate Git checkout"):
            source.checkout(nested)


class ExtendedReleaseTests(Fixture):
    def test_public_example_config_is_allowed_in_committed_source(self):
        self.write("examples/release-config.json", '{"credentials": {}}\n')
        commit = self.committed()
        files = common.source_files(self.root)
        self.assertIn("examples/release-config.json", files)
        common.clean_git(self.root, files, commit)

    def test_private_files_are_excluded_and_blocked_if_already_committed(self):
        self.committed()
        names = ("release-config.json", ".env.production", "signing.pem", "signing.p12", "id_ed25519", "credentials.json", ".aws/credentials")
        for name in names:
            self.write(name, "fixture private settings\n")
        self.assertFalse(set(names) & set(common.source_files(self.root)))
        git(self.root, "add", "-f", "--", *names)
        git(self.root, "commit", "-qm", "Accidentally committed private settings")
        with self.assertRaisesRegex(common.ReleaseError, "Private configuration"):
            common.clean_git(self.root, common.source_files(self.root), common.git_commit(self.root))

    def test_private_key_in_an_ordinary_source_file_blocks_packaging(self):
        marker = "-" * 5 + "BEGIN " + "PRIVATE KEY" + "-" * 5
        self.write("accidental.txt", marker + "\nfixture\n")
        with self.assertRaisesRegex(common.ReleaseError, "Credential-like"):
            common.source_files(self.root)

    def test_already_tracked_original_maps_block_source_publication(self):
        self.write("data/maps/original/README.txt", "Imported maps are not distributed.\n")
        self.write("data/maps/original/MapP1_t1.ofpmap", "fixture imported map\n")
        self.committed()
        with self.assertRaisesRegex(common.ReleaseError, "imported game assets"):
            common.clean_git(self.root, common.source_files(self.root), common.git_commit(self.root))

    def test_real_ignore_rules_protect_secrets_but_keep_public_examples(self):
        actual = Path(__file__).resolve().parents[1] / ".gitignore"
        (self.root / ".gitignore").write_bytes(actual.read_bytes())
        self.committed()
        for name in (".env", ".env.production", "key.p12", "signing.pem", "id_ed25519", "credentials.json", "release-config.json", "data/maps/original/MapP1_t1.ofpmap"):
            self.write(name, "fixture")
            self.assertEqual(git(self.root, "check-ignore", name), name)
        for name in (".env.example", "examples/release-config.json", "data/maps/original/README.txt"):
            self.write(name, "{}")
            result = subprocess.run(["git", "check-ignore", "--quiet", name], cwd=str(self.root))
            self.assertEqual(result.returncode, 1)

    def test_missing_compiler_stops_before_tests_or_removing_ready_outputs(self):
        self.built()
        before = common.tree_hashes(self.project.targets["windows"]["dest"])
        import release_build
        with patch.object(release_build, "run", side_effect=common.ReleaseError("missing compiler")) as command:
            with self.assertRaisesRegex(common.ReleaseError, "missing compiler"):
                self.project.build(["windows"])
        self.assertEqual(command.call_args.args[0][-1], "--version")
        self.assertEqual(common.tree_hashes(self.project.targets["windows"]["dest"]), before)

    def test_custom_tag_and_annotation_do_not_change_embedded_version(self):
        self.built()
        self.project.tag = "0.1"
        self.project.tag_message = "OpenFirePower first release"
        self.project.folder = self.project.packages / "Release/0.1"
        with contextlib.redirect_stdout(io.StringIO()):
            m = self.project.prepare(self.root / "docs/RELEASE-NOTES.md")
        self.project.verify_manifest(m)
        self.assertEqual((m["version"], m["tag"], m["tag_message"]), ("0.1", "0.1", "OpenFirePower first release"))
        changed = dict(m, tag_message="different")
        with self.assertRaisesRegex(common.ReleaseError, "annotation"):
            self.project.verify_manifest(changed)

    def test_every_platform_package_has_verified_checksum_sidecar(self):
        m = self.prepared()
        for name in self.project.package_names():
            self.assertIn(name + ".sha256", m["files"])
            self.assertEqual((self.project.folder / (name + ".sha256")).read_bytes(), self.project.sums({name: m["files"][name]}))
        (self.project.folder / (next(iter(self.project.package_names())) + ".sha256")).write_text("tampered")
        with self.assertRaisesRegex(common.ReleaseError, "sidecar"):
            self.project.check()

    def test_invalid_custom_tag_rejected(self):
        for tag in ("../bad", "-option", "bad..tag", "bad.lock", "/absolute"):
            from release_build import Project
            with self.assertRaises(common.ReleaseError):
                Project(self.root, self.project.cfg, tag=tag)

    def test_combined_release_without_confirmation_stops_before_configuration(self):
        with patch.object(cli, "config", side_effect=AssertionError("unconfirmed release loaded config")), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(cli.main(["release"]), 1)
