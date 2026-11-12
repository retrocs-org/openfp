# SPDX-License-Identifier: MIT
"""Release inputs, exact inventories and build records (Python 3.9+)."""
import hashlib
import json
import os
import re
import stat
import subprocess
import tempfile
from pathlib import Path
from urllib.parse import urlsplit


class ReleaseError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ReleaseError(message)


def canonical(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False) + "\n").encode()


def digest_stream(stream):
    h = hashlib.sha256()
    while True:
        block = stream.read(1024 * 1024)
        if not block:
            return h.hexdigest()
        h.update(block)


def regular(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), "Missing or symlinked file: " + str(path))
    # Reject symlinked parents as well as symlinked assets.
    require(not any(p.is_symlink() for p in path.parents), "Symlinked parent: " + str(path))
    return path


def sha(path):
    with regular(path).open("rb") as f:
        return digest_stream(f)


def atomic(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    require(not path.is_symlink() and not any(p.is_symlink() for p in path.parents), "Refusing symlink: " + str(path))
    fd, name = tempfile.mkstemp(prefix=".release-", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(name, 0o644)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def read_json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "Duplicate JSON key: " + key)
            result[key] = value
        return result
    try:
        return json.loads(regular(path).read_text(), object_pairs_hook=pairs)
    except (ValueError, UnicodeError) as exc:
        raise ReleaseError("Invalid JSON: " + str(path)) from exc


SECRETS = set()


def redact(text):
    for secret in sorted(SECRETS, key=len, reverse=True):
        text = text.replace(secret, "[redacted]")
    for name in ("GITHUB_TOKEN", "GITLAB_TOKEN"):
        secret = os.environ.get(name)
        if secret:
            text = text.replace(secret, "[redacted]")
    return re.sub(r"(?:github_pat_|gh[pousr]_|glpat-)[A-Za-z0-9_-]+", "[redacted]", text)


def run(argv, root, env=None, capture=False, inherit=True):
    require(isinstance(argv, list) and argv and all(isinstance(a, str) for a in argv), "Commands must be argument arrays")
    merged = os.environ.copy() if inherit else {}
    merged.update(env or {})
    try:
        result = subprocess.run(argv, cwd=str(root), env=merged, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False)
    except OSError as exc:
        raise ReleaseError("Cannot run " + Path(argv[0]).name) from exc
    output = redact(result.stdout.decode("utf-8", "replace"))
    if not capture and output:
        print(output, end="", flush=True)
    require(result.returncode == 0, "Command failed: " + Path(argv[0]).name + " (" + str(result.returncode) + ")")
    return output


def version(root):
    net = (root / "src/common/net.h").read_text()
    matches = re.findall(r'^#define OFP_VERSION "([0-9]+\.[0-9]+(?:\.[0-9]+)?)"$', net, re.M)
    require(len(matches) == 1, "Expected one numeric OFP_VERSION in src/common/net.h")
    value = matches[0]
    cmake = (root / "CMakeLists.txt").read_text()
    require(re.search(r'project\(openfirepower VERSION ' + re.escape(value) + r' LANGUAGES', cmake),
            "CMake project version must match OFP_VERSION")
    return value


def repo_url(url, host):
    require(isinstance(url, str), "Repository URL must be a string")
    p = urlsplit(url)
    require(p.scheme == "https" and p.hostname == host and p.netloc == host and
            not p.username and not p.password and not p.query and not p.fragment,
            "Repository URL must use the configured HTTPS host without credentials or query strings")
    parts = p.path.strip("/").removesuffix(".git").split("/")
    require(len(parts) >= 2 and all(re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*", x) and x not in (".", "..") for x in parts),
            "Invalid repository path")
    if host == "github.com":
        require(len(parts) == 2, "GitHub requires owner/repository")
    return "https://" + host + "/" + "/".join(parts)


def git_url(value, https, host):
    path = https.split(host + "/", 1)[1]
    allowed = (https, https + ".git", "git@" + host + ":" + path + ".git",
               "ssh://git@" + host + "/" + path + ".git")
    require(value in allowed, "Git transport destination does not match repository URL")
    return value


DEFAULT = {
    "github": "https://github.com/retrocs-org/openfp",
    "github_git": "git@github.com:retrocs-org/openfp.git",
    "gitlab": "https://gitlab.kladhest.se/retrocs-org/openfp",
    "gitlab_git": "git@gitlab.kladhest.se:retrocs-org/openfp.git",
    "gitlab_host": "gitlab.kladhest.se",
    "builds": "~/Development/builds/OpenFirePower",
    "packages": "~/Development/packages/OpenFirePower",
    "work": "work/release",
    "tools": "~/Development/tools",
    "targets": ["macos", "windows", "amiga", "linux-x86_64", "linux-aarch64"],
    "env": {}, "target_settings": {},
    "credentials": {}, "source_branch": "",
    "macos": {"identity": "-", "notary_profile": "", "require_notarization": False},
    "aminet": {"uploader": "", "author": "OpenFirePower contributors", "category": "game/actio"},
}
TARGET_NAMES = {"macos", "windows", "amiga", "linux-x86_64", "linux-aarch64"}


def config_path(root, path=None):
    """Explicit path, project environment, shared settings, then the old local file."""
    value = path or os.environ.get("OFP_RELEASE_CONFIG")
    if value:
        local = Path(value).expanduser()
        return local if local.is_absolute() else root / local
    shared = Path.home() / "Development/tools/git-release/projects/openfp.json"
    return shared if shared.exists() else root / "work/release-config.json"


def config(root, path=None):
    data = json.loads(json.dumps(DEFAULT))
    local = config_path(root, path)
    if path or os.environ.get("OFP_RELEASE_CONFIG") or local.exists():
        changes = read_json(local)
        require(isinstance(changes, dict) and not (set(changes) - set(DEFAULT)), "Unknown release configuration setting")
        for key, value in changes.items():
            if isinstance(data.get(key), dict):
                require(isinstance(value, dict), "Expected configuration object: " + key)
                data[key].update(value)
            else:
                data[key] = value
    host = data["gitlab_host"]
    require(isinstance(host, str) and re.fullmatch(r"[A-Za-z0-9.-]+", host) and "." in host, "Invalid GitLab host")
    for service, hostname in (("github", "github.com"), ("gitlab", host)):
        data[service] = repo_url(data[service], hostname)
        data[service + "_git"] = git_url(data[service + "_git"], data[service], hostname)
    require(isinstance(data["targets"], list) and data["targets"] and len(set(data["targets"])) == len(data["targets"]) and
            set(data["targets"]) <= TARGET_NAMES, "Select distinct supported release targets")
    require(not (set(data["target_settings"]) - TARGET_NAMES), "Unknown target settings")
    require(all(isinstance(v, dict) for v in data["target_settings"].values()), "Target settings must be objects")
    for env in [data["env"]] + [v.get("env", {}) for v in data["target_settings"].values()]:
        require(isinstance(env, dict) and all(isinstance(k, str) and isinstance(v, str) for k, v in env.items()),
                "Build environments must contain string names and values")
    for key in ("builds", "packages", "work", "tools"):
        p = Path(data[key]).expanduser()
        p = p if p.is_absolute() else root / p
        require(not p.is_symlink() and not any(x.is_symlink() for x in p.parents), "Output path must not use symlinks")
        data[key] = str(p.resolve())
    require(len({data[k] for k in ("work", "builds", "packages")}) == 3, "Use separate work, builds and packages folders")
    for a in ("work", "builds", "packages"):
        for b in ("work", "builds", "packages"):
            require(a == b or Path(data[a]) not in Path(data[b]).parents, "Release output folders must not contain one another")
    work = Path(data["work"])
    require(work == root / "work/release" or root not in work.parents, "In-tree release work must use work/release")
    for key in ("builds", "packages"):
        require(root not in Path(data[key]).parents and Path(data[key]) != root, "Builds and packages must be outside the source tree")
    from release_credentials import validate_credentials
    validate_credentials(data["credentials"])
    require(isinstance(data["source_branch"], str) and (not data["source_branch"] or
            re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_./-]*", data["source_branch"])), "Invalid source branch")
    # Credential specifications contain lookup details only, never token values.
    def inspect(obj):
        if isinstance(obj, dict):
            for key, val in obj.items():
                require(not re.search(r"token|password|secret|private.key", key, re.I), "Credentials belong in the environment, not configuration")
                inspect(val)
        elif isinstance(obj, list):
            for val in obj:
                inspect(val)
        elif isinstance(obj, str):
            require(not re.search(r"github_pat_|gh[pousr]_|glpat-|://[^/\s]+:[^/@\s]+@", obj), "Credential-like value in configuration")
    inspect({k: v for k, v in data.items() if k != "credentials"})
    require(not (set(data["macos"]) - {"identity", "notary_profile", "require_notarization"}), "Unknown macOS signing setting")
    require(isinstance(data["macos"]["identity"], str) and data["macos"]["identity"] and
            isinstance(data["macos"]["notary_profile"], str) and isinstance(data["macos"]["require_notarization"], bool),
            "Invalid macOS signing policy")
    require(not (set(data["aminet"]) - {"uploader", "author", "category"}), "Unknown Aminet setting")
    require(data["aminet"]["category"] == "game/actio", "Aminet category for this project is game/actio")
    return data


EXCLUDE_PREFIX = ("Amiga/", "macOS/", "work/", "outputs/", "build/", "validation/", "release/", "rip/",
                  "data/maps/original/", "data/themes/classic/", "data/styles/original", "data/styles/imported/",
                  "public-website/assets/releases/")


def private_file(name):
    """Apply the same private-file policy to archives and already tracked files."""
    p = Path(name)
    lower = p.name.lower()
    if name == "examples/release-config.json":
        return False
    return (lower == ".env" or (lower.startswith(".env.") and not lower.endswith(".example")) or
            lower in {"config.inc.php", "server.cfg", "release-config.json", "id_rsa", "id_dsa", "id_ecdsa", "id_ed25519",
                      ".netrc", ".npmrc", ".pypirc", "credentials.json", "service-account.json", "service_account.json"} or
            p.suffix.lower() in {".pem", ".key", ".p12", ".pfx", ".jks", ".keystore", ".mobileprovision", ".provisionprofile",
                                ".db", ".sqlite", ".sqlite3"} or lower.endswith((".db-wal", ".db-shm")) or
            any(part.lower() in {".aws", ".ssh", ".gnupg", ".docker"} for part in p.parts))


def included(name):
    if name in ("data/maps/original/", "data/maps/original/README.txt"):
        return True
    p = Path(name)
    directories = p.parts if name.endswith("/") else p.parts[:-1]
    return not (private_file(name) or name.startswith(EXCLUDE_PREFIX) or p.parts[0].startswith(("build-", "soak-")) or
                any(x.startswith(".") and x not in {".github", ".gitlab", ".well-known"} for x in directories) or
                any(x == "__pycache__" for x in p.parts) or p.suffix in {".o", ".obj", ".pyc", ".bmp", ".ppm", ".db", ".log", ".adf", ".pem", ".key", ".p12", ".pfx"} or
                p.name == ".env" or (p.name.startswith(".env.") and not p.name.endswith(".example")) or
                p.name in {".git", ".DS_Store", "config.inc.php", "server.cfg", "amiga-preview", "amiga-preview-online", "soak.pid", "soak.current", "soak-last.out"} or
                (p.suffix == ".ofprep" and not name.startswith("tests/replays/")))


def source_files(root):
    secrets = [s.encode() for s in SECRETS]
    secrets += [os.environ[k].encode() for k in ("GITHUB_TOKEN", "GITLAB_TOKEN") if os.environ.get(k)]
    token_pattern = re.compile(rb"(?:github_pat_|gh[pousr]_|glpat-)[A-Za-z0-9_-]{20,}|-----BEGIN (?:RSA |EC |DSA |OPENSSH |ENCRYPTED )?PRIVATE KEY-----")
    overlap = max([256] + [len(s) for s in secrets])
    result = {}
    for current, dirs, files in os.walk(root, followlinks=False):
        base = Path(current)
        for d in list(dirs):
            p = base / d
            rel = p.relative_to(root).as_posix()
            if not included(rel + "/"):
                dirs.remove(d)
            else:
                require(not p.is_symlink(), "Symlinked source directory: " + rel)
        for name in sorted(files):
            p = base / name
            rel = p.relative_to(root).as_posix()
            if included(rel):
                h, tail = hashlib.sha256(), b""
                with regular(p).open("rb") as stream:
                    while True:
                        block = stream.read(1024 * 1024)
                        if not block:
                            break
                        window = tail + block
                        require(not any(s in window for s in secrets) and not token_pattern.search(window),
                                "Credential-like content in release input: " + rel)
                        h.update(block)
                        tail = window[-overlap:]
                result[rel] = {"sha256": h.hexdigest(), "executable": bool(p.stat().st_mode & stat.S_IXUSR)}
    return dict(sorted(result.items()))


def fingerprint(files):
    return hashlib.sha256(canonical(files)).hexdigest()


def git_commit(root):
    if not (root / ".git").exists():
        return None
    value = run(["git", "rev-parse", "HEAD"], root, capture=True).strip()
    require(re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", value), "Invalid source commit")
    return value


def clean_git(root, files, commit):
    require(commit is not None and git_commit(root) == commit, "Publication requires a committed Git checkout matching the manifest")
    top = Path(run(["git", "rev-parse", "--show-toplevel"], root, capture=True).strip()).resolve()
    require(top == root.resolve(), "Publication requires the project root to be a separate Git checkout")
    status = run(["git", "status", "--porcelain", "--untracked-files=all"], root, capture=True)
    require(not status.strip(), "Finish and commit source changes before publishing")
    tracked = set(run(["git", "ls-files", "-z"], root, capture=True).split("\0")) - {""}
    require(set(files) <= tracked, "Release inputs include untracked or ignored files; track them before publishing")
    require(not any(private_file(p) or p.startswith("work/") for p in tracked),
            "Private configuration or release work is tracked in Git")
    require(all(included(p) for p in tracked), "Excluded build files or imported game assets are tracked in Git; remove them before publication")
    entries = run(["git", "ls-files", "--stage"], root, capture=True)
    require(not any(n.startswith("160000 ") for n in entries.splitlines()), "Release submodules are not supported")


def tree_hashes(folder):
    require(folder.is_dir() and not folder.is_symlink(), "Missing ready-to-test build: " + str(folder))
    hashes = {}
    for current, dirs, files in os.walk(folder):
        for d in list(dirs):
            p = Path(current) / d
            if p.is_symlink():
                require(folder.resolve() in p.resolve().parents, "Build link escapes its folder")
                hashes[p.relative_to(folder).as_posix()] = {"link": os.readlink(p)}
                dirs.remove(d)
        for name in files:
            p = Path(current) / name
            if p.is_symlink():
                # Apple frameworks use internal links. Only links contained in this build are allowed.
                require(folder.resolve() in p.resolve().parents, "Build link escapes its folder")
                hashes[p.relative_to(folder).as_posix()] = {"link": os.readlink(p)}
            else:
                hashes[p.relative_to(folder).as_posix()] = sha(p)
    require(hashes, "Empty ready-to-test build")
    return hashes
