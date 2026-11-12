#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build once, review the exact manifest, publish the same release bytes."""
import argparse
import contextlib
import ftplib
import os
import sys
import tarfile
import zipfile
from pathlib import Path

from release_common import ReleaseError, config, config_path, read_json, require
from release_build import Project
from release_destinations import publish
from release_source import push_source


@contextlib.contextmanager
def locked(work):
    work.mkdir(parents=True, exist_ok=True)
    path = work / "release.lock"
    try:
        fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError as exc:
        raise ReleaseError("Another release operation has a lock. If it was interrupted, inspect receipts and confirm no operation is running before removing release.lock") from exc
    try:
        with os.fdopen(fd, "w") as f:
            f.write(str(os.getpid()) + "\n")
        yield
    finally:
        path.unlink()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("source", "plan", "build", "check", "prepare", "publish", "release"))
    parser.add_argument("--config", help="External JSON configuration; default OFP_RELEASE_CONFIG or ~/Development/tools/git-release/projects/openfp.json")
    parser.add_argument("--targets", help="Comma-separated configured targets for plan/build; check/prepare require the complete matrix")
    parser.add_argument("--notes", default="docs/RELEASE-NOTES.md", help="Release notes for prepare")
    parser.add_argument("--uploader", help="Public Aminet uploader email, supplied to prepare when submission is wanted")
    parser.add_argument("--to", default="gitlab,github", help="Publication destinations; default gitlab,github")
    parser.add_argument("--confirm-publish", action="store_true", help="Explicitly allow tag pushes and publication")
    parser.add_argument("--confirm-push", action="store_true", help="Explicitly allow source branch pushes")
    parser.add_argument("--dry-run", action="store_true", help="Check source pushes without uploading")
    parser.add_argument("--init", action="store_true", help="Initialize a first local source commit, only after every selected remote is verified empty")
    parser.add_argument("--commit-message", help="Initial source commit message, used only with source --init")
    parser.add_argument("--tag", help="Exact release tag; default vVERSION")
    parser.add_argument("--tag-message", help="Annotation of a new release tag")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    try:
        require(args.command in ("publish", "release") or not args.confirm_publish, "--confirm-publish is only for publish/release")
        require(args.command in ("prepare", "release") or not args.uploader, "Supply --uploader to prepare/release")
        require(not args.targets or args.command in ("plan", "build"), "--targets is only for plan/build")
        require(args.command not in ("publish", "release") or args.confirm_publish, "Publication requires --confirm-publish")
        require(args.command == "source" or not (args.confirm_push or args.dry_run or args.init or args.commit_message is not None), "Source push flags are only for source")
        require(args.command in ("prepare", "publish", "release") or not (args.tag is not None or args.tag_message is not None), "Tag options are for prepare/publish/release")
        cfg = config(root, args.config)
        if args.command == "source":
            with locked(Path(cfg["work"])):
                push_source(root, cfg, args.to.split(","), dry_run=args.dry_run, confirmed=args.confirm_push,
                            initialize=args.init, message=args.commit_message)
            return 0
        manifest_path = Path(cfg["work"]) / "manifest.json"
        manifest = read_json(manifest_path) if args.command in ("publish", "check") and manifest_path.exists() else None
        tag = args.tag if args.tag is not None else (manifest["tag"] if manifest else None)
        annotation = args.tag_message if args.tag_message is not None else (manifest.get("tag_message") if manifest else None)
        project = Project(root, cfg, tag, annotation)
        targets = args.targets.split(",") if args.targets else project.cfg["targets"]
        require(targets and len(set(targets)) == len(targets) and set(targets) <= set(project.cfg["targets"]), "Choose distinct configured targets")
        if args.command == "plan":
            print("Configuration: " + str(config_path(root, args.config)))
            project.plan(targets)
        elif args.command == "check":
            project.check()
            if manifest:
                project.verify_manifest(manifest)
            print("Source, builds, signatures, packages, source archive and website checksums match")
        else:
            with locked(project.work):
                if args.command == "build":
                    project.build(targets)
                elif args.command in ("prepare", "release"):
                    notes = Path(args.notes)
                    manifest = project.prepare(notes if notes.is_absolute() else root / notes, args.uploader)
                if args.command in ("publish", "release"):
                    require(manifest is not None, "No reviewed manifest; run prepare first")
                    publish(root, manifest, project.folder, project.work, args.to.split(","), args.confirm_publish,
                            lambda publication=False: project.verify_manifest(manifest, publication), cfg=cfg)
        return 0
    except (ReleaseError, OSError, KeyError, TypeError, ValueError, EOFError, tarfile.TarError, zipfile.BadZipFile, ftplib.Error) as exc:
        # Service error bodies and environment variables are never logged.
        from release_common import redact
        print("Release stopped: " + redact(str(exc)), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
