# SPDX-License-Identifier: MIT
"""Push committed source independently of builds and release publication.

The source workflow is adapted from BitSkim's MIT-licensed release tool.
Copyright (c) 2026 BitSkim contributors. See THIRD-PARTY-NOTICES.md.
"""
from pathlib import Path
import subprocess

from release_common import ReleaseError, clean_git, git_commit, require, run, source_files


def validate_ref(root, ref):
    require(isinstance(ref, str) and ref.startswith("refs/") and not ref.endswith("/"), "Invalid Git ref")
    run(["git", "check-ref-format", ref], root, capture=True)


def checkout(root):
    result = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=str(root),
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    if result.returncode:
        return False
    require(Path(result.stdout.strip()).resolve() == root.resolve(), "Project must be a separate Git checkout, not a folder inside another repository")
    return True


def initialize_source(root, cfg, services, message=None):
    existing = checkout(root)
    if existing and git_commit_unborn(root):
        return  # Never stage or commit changes in an established repository.
    require(existing or not (root / ".git").exists() and not (root / ".git").is_symlink(), "Invalid existing .git metadata")
    require((root / ".gitignore").is_file(), "Review .gitignore before initializing the source")
    files = source_files(root)
    require(files, "No release source files")
    branch = cfg.get("source_branch") or "main"
    validate_ref(root, "refs/heads/" + branch)
    message = message if message is not None else "OpenFP source release"
    require(isinstance(message, str) and message.strip() and "\x00" not in message, "Invalid initial commit message")
    # Check EVERY selected remote before creating metadata or staging any source.
    for service in services:
        refs = run(["git", "ls-remote", "--refs", cfg[service + "_git"]], root, capture=True)
        require(not refs.strip(), service + " already has Git history; clone it and import these files instead. Nothing initialized or uploaded")
    for identity in ("GIT_AUTHOR_IDENT", "GIT_COMMITTER_IDENT"):
        run(["git", "var", identity], root, capture=True)
    if not existing:
        run(["git", "init", "--initial-branch=" + branch], root)
    else:
        staged = set(run(["git", "ls-files", "-z"], root, capture=True).split("\0")) - {""}
        require(staged <= set(files), "Unborn repository contains staged files outside the release source; review its index")
    names = sorted(files)
    for start in range(0, len(names), 100):
        run(["git", "--literal-pathspecs", "add", "--", *names[start:start + 100]], root, capture=True)
    run(["git", "commit", "-m", message], root)
    print("Created the initial local source commit. Initialization uploaded nothing.")


def git_commit_unborn(root):
    return subprocess.run(["git", "rev-parse", "--verify", "HEAD"], cwd=str(root),
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def push_source(root, cfg, services, *, dry_run=False, confirmed=False, initialize=False, message=None):
    require(dry_run != confirmed, "Choose source --dry-run OR --confirm-push")
    require(services and len(services) == len(set(services)) and set(services) <= {"github", "gitlab"}, "Source destinations are gitlab and github only")
    require(message is None or initialize, "--commit-message requires source --init")
    if initialize:
        initialize_source(root, cfg, services, message)
    require(checkout(root), "Source needs a committed Git checkout; use source --init --dry-run only for new, empty repositories")
    commit = git_commit(root)
    files = source_files(root)
    clean_git(root, files, commit)
    branch = run(["git", "symbolic-ref", "--quiet", "--short", "HEAD"], root, capture=True).strip()
    target = cfg.get("source_branch") or branch
    validate_ref(root, "refs/heads/" + target)
    refspec = commit + ":refs/heads/" + target
    # Explicitly disable configured mirror/follow-tags/force behaviour. Pin the reviewed commit.
    command = ["git", "-c", "push.followTags=false", "-c", "push.autoSetupRemote=false", "push", "--no-follow-tags", "--no-force", "--no-mirror"]
    for service in services:
        print("Checking source push: " + service + " (" + branch + " -> " + target + ")", flush=True)
        run(command + ["--dry-run", cfg[service + "_git"], refspec], root)
    if dry_run:
        print("Source push checks passed. Nothing uploaded; no build or release created.")
        return
    for service in services:
        clean_git(root, source_files(root), commit)
        require(source_files(root) == files, "Source changed during push")
        run(command + [cfg[service + "_git"], refspec], root)
        refs = run(["git", "ls-remote", "--heads", cfg[service + "_git"], "refs/heads/" + target], root, capture=True).splitlines()
        require(refs == [commit + "\trefs/heads/" + target], "Remote source branch verification failed: " + service)
        print(service + ": source branch verified")
