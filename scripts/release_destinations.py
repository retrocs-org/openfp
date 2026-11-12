# SPDX-License-Identifier: MIT
"""Publication adapters. Importing this module never contacts a service."""
import ftplib
from release_assets import package_name
import hashlib
import http.client
import json
import os
import re
import ssl
from pathlib import Path
from urllib.parse import quote, urljoin, urlsplit

from release_common import ReleaseError, atomic, canonical, read_json, require, run, sha
from release_credentials import credential


class HTTP:
    """One request per operation; no automatic retries or authenticated redirects."""
    def __init__(self, service, host, token):
        self.service, self.host, self.token = service, host, token

    def request(self, method, url, data=None, file=None, missing=False, digest=False):
        p = urlsplit(url)
        require(p.scheme == "https" and p.hostname == self.host and p.netloc == self.host and
                not p.username and not p.password and not p.fragment, "Unexpected API host")
        headers = {"User-Agent": "OpenFirePower-release", "Accept": "application/json"}
        if self.service == "github":
            headers.update({"Authorization": "Bearer " + self.token, "X-GitHub-Api-Version": "2022-11-28"})
        else:
            headers["PRIVATE-TOKEN"] = self.token
        body = None
        stream = None
        if data is not None:
            body = canonical(data)
            headers["Content-Type"] = "application/json"
        if file is not None:
            stream = Path(file).open("rb")
            body = stream
            headers.update({"Content-Type": "application/octet-stream", "Content-Length": str(Path(file).stat().st_size)})
        conn = http.client.HTTPSConnection(self.host, timeout=180, context=ssl.create_default_context())
        try:
            conn.request(method, p.path + ("?" + p.query if p.query else ""), body=body, headers=headers)
            response = conn.getresponse()
            if response.status == 404 and missing:
                return None
            # GitLab may redirect a package download to signed HTTPS object storage.
            if digest and method == "GET" and response.status in (301, 302, 303, 307, 308):
                return self.public_digest(urljoin(url, response.getheader("Location", "")))
            require(200 <= response.status < 300, self.service + " request failed (HTTP " + str(response.status) + ")")
            if digest:
                return self.response_digest(response)
            payload = response.read(8 * 1024 * 1024 + 1)
            require(len(payload) <= 8 * 1024 * 1024, "Oversized API response")
            return json.loads(payload) if payload else {}
        except (OSError, http.client.HTTPException, ValueError) as exc:
            raise ReleaseError(self.service + " request failed; no automatic retry was performed") from exc
        finally:
            conn.close()
            if stream:
                stream.close()

    @staticmethod
    def response_digest(response):
        h = hashlib.sha256()
        while True:
            data = response.read(1024 * 1024)
            if not data:
                return h.hexdigest()
            h.update(data)

    @classmethod
    def public_digest(cls, url):
        # Never forward an API token to an object-storage endpoint or through a redirect.
        for _ in range(4):
            p = urlsplit(url)
            require(p.scheme == "https" and p.hostname and not p.username and not p.password and
                    not p.fragment and p.port in (None, 443), "Invalid package download redirect")
            conn = http.client.HTTPSConnection(p.hostname, timeout=180, context=ssl.create_default_context())
            try:
                conn.request("GET", p.path + ("?" + p.query if p.query else ""), headers={"User-Agent": "OpenFirePower-release"})
                response = conn.getresponse()
                if response.status in (301, 302, 303, 307, 308):
                    url = urljoin(url, response.getheader("Location", ""))
                    continue
                require(response.status == 200, "Package download verification failed")
                return cls.response_digest(response)
            except (OSError, http.client.HTTPException) as exc:
                raise ReleaseError("Package download verification failed") from exc
            finally:
                conn.close()
        raise ReleaseError("Too many package download redirects")


class GitHub:
    def __init__(self, manifest, folder, token, api=None):
        self.m, self.folder = manifest, folder
        self.path = manifest["github"].split("github.com/", 1)[1]
        self.base = "https://api.github.com/repos/" + self.path
        self.api = api or HTTP("github", "api.github.com", token)
        self.upload = HTTP("github", "uploads.github.com", token) if api is None else api

    def assets(self, release):
        assets = []
        for page in range(1, 101):
            batch = self.api.request("GET", self.base + "/releases/" + str(release["id"]) + "/assets?per_page=100&page=" + str(page))
            assets.extend(batch)
            if len(batch) < 100:
                break
        else:
            raise ReleaseError("Too many GitHub release assets")
        names = [a["name"] for a in assets]
        require(len(set(names)) == len(names), "Duplicate GitHub asset names")
        require(set(names) <= set(self.m["files"]), "Unexpected GitHub release asset")
        for asset in assets:
            require(asset.get("state") == "uploaded" and asset.get("digest") == "sha256:" + self.m["files"][asset["name"]],
                    "Unknown or conflicting GitHub asset digest: " + asset["name"])
        return {a["name"]: a for a in assets}

    def release(self):
        # The by-tag endpoint only guarantees published releases. List with push
        # credentials so an interrupted draft is found and reused as well.
        matches = []
        for page in range(1, 101):
            batch = self.api.request("GET", self.base + "/releases?per_page=100&page=" + str(page))
            matches.extend(r for r in batch if r.get("tag_name") == self.m["tag"])
            if len(batch) < 100:
                break
        else:
            raise ReleaseError("Too many GitHub releases to check safely")
        require(len(matches) <= 1, "Duplicate GitHub releases for this tag")
        release = matches[0] if matches else None
        if release:
            require(release["tag_name"] == self.m["tag"] and (release.get("body") or "") == self.m["notes"] and
                    release.get("name") == self.m["title"], "GitHub release notes or title conflict")
        return release

    def preflight(self):
        repo = self.api.request("GET", self.base)
        require(repo["full_name"].lower() == self.path.lower() and repo.get("permissions", {}).get("push"),
                "GitHub token lacks access to the configured repository")
        release = self.release()
        if release:
            present = self.assets(release)
            require(release.get("draft") or set(present) == set(self.m["files"]), "Published GitHub release is incomplete; review it manually")

    def publish(self):
        release = self.release()
        if not release:
            release = self.api.request("POST", self.base + "/releases", data={
                "tag_name": self.m["tag"], "target_commitish": self.m["commit"], "name": self.m["title"],
                "body": self.m["notes"], "draft": True, "prerelease": False})
            require(release.get("draft") is True, "GitHub did not create a draft")
        present = self.assets(release)
        for name, expected in self.m["files"].items():
            if name in present:
                continue
            require(release.get("draft") is True, "Refusing to add files to a published release")
            # Construct the known endpoint instead of trusting an API-returned upload host.
            url = "https://uploads.github.com/repos/" + self.path + "/releases/" + str(release["id"]) + "/assets?name=" + quote(name, safe="")
            asset = self.upload.request("POST", url, file=self.folder / name)
            require(asset.get("name") == name and asset.get("state") == "uploaded" and asset.get("digest") == "sha256:" + expected,
                    "GitHub uploaded asset digest mismatch: " + name)
        require(set(self.assets(release)) == set(self.m["files"]), "GitHub release is missing assets")
        if release.get("draft"):
            result = self.api.request("PATCH", self.base + "/releases/" + str(release["id"]), data={"draft": False})
            require(result.get("draft") is False, "GitHub release is still a draft")


class GitLab:
    def __init__(self, manifest, folder, token, api=None):
        self.m, self.folder = manifest, folder
        p = urlsplit(manifest["gitlab"])
        self.path = p.path.strip("/")
        self.base = "https://" + p.netloc + "/api/v4/projects/" + quote(self.path, safe="")
        self.api = api or HTTP("gitlab", p.netloc, token)

    def package_url(self, name):
        return self.base + "/packages/generic/openfirepower/" + quote(self.m["version"], safe="") + "/" + quote(name, safe="")

    def links(self):
        return {name: self.package_url(name) for name in self.m["files"]}

    def release(self):
        result = self.api.request("GET", self.base + "/releases/" + quote(self.m["tag"], safe=""), missing=True)
        if result:
            require(result.get("description") == self.m["notes"] and result.get("name") == self.m["title"], "GitLab release notes or title conflict")
            links = result.get("assets", {}).get("links", [])
            require(len({a["name"] for a in links}) == len(links) and {a["name"]: a["url"] for a in links} == self.links(),
                    "GitLab release asset links conflict")
        return result

    def preflight(self):
        host = urlsplit(self.base).netloc
        token = self.api.request("GET", "https://" + host + "/api/v4/personal_access_tokens/self")
        require(token.get("active") and not token.get("revoked") and "api" in token.get("scopes", []),
                "GitLab requires an active personal or project access token with the api scope")
        project = self.api.request("GET", self.base)
        require(project.get("path_with_namespace") == self.path, "GitLab project mismatch")
        permissions = project.get("permissions", {})
        level = max((permissions.get(k) or {}).get("access_level", 0) for k in ("project_access", "group_access"))
        require(level >= 30, "GitLab token needs project Developer access and the api scope")
        require(project.get("packages_enabled") is not False, "The configured GitLab package registry is disabled")
        self.release()
        for name, expected in self.m["files"].items():
            got = self.api.request("GET", self.package_url(name), missing=True, digest=True)
            require(got is None or got == expected, "Conflicting GitLab package: " + name)

    def publish(self):
        self.release()
        for name, expected in self.m["files"].items():
            url = self.package_url(name)
            got = self.api.request("GET", url, missing=True, digest=True)
            require(got is None or got == expected, "Conflicting GitLab package: " + name)
            if got is None:
                self.api.request("PUT", url, file=self.folder / name)
                got = self.api.request("GET", url, digest=True)
            require(got == expected, "GitLab download verification failed: " + name)
        if not self.release():
            self.api.request("POST", self.base + "/releases", data={"tag_name": self.m["tag"],
                "name": self.m["title"], "description": self.m["notes"], "assets": {"links": [
                    {"name": name, "url": url, "link_type": "package"} for name, url in self.links().items()]}})
        require(self.release() is not None, "GitLab release verification failed")


def local_tag(root, manifest):
    result = run(["git", "tag", "--list", manifest["tag"]], root, capture=True).strip()
    if result:
        resolved = run(["git", "rev-parse", manifest["tag"] + "^{commit}"], root, capture=True).strip()
        kind = run(["git", "cat-file", "-t", "refs/tags/" + manifest["tag"]], root, capture=True).strip()
        require(resolved == manifest["commit"] and kind == "tag", "Local release tag conflicts or is not annotated")
    return bool(result)


def remote_tag(root, url, manifest):
    ref = "refs/tags/" + manifest["tag"]
    lines = run(["git", "ls-remote", "--tags", url, ref, ref + "^{}"], root, capture=True).splitlines()
    pairs = [line.split() for line in lines]
    require(all(len(x) == 2 and x[1] in (ref, ref + "^{}") for x in pairs), "Unexpected remote tag response")
    refs = {x[1]: x[0] for x in pairs}
    require(len(refs) == len(pairs), "Duplicate remote tag response")
    if refs:
        require(ref in refs and refs.get(ref + "^{}") == manifest["commit"], "Remote tag conflicts or is not annotated; tags are never moved")
    return refs


def push_tag(root, url, manifest):
    if not local_tag(root, manifest):
        run(["git", "tag", "-a", manifest["tag"], manifest["commit"], "-m", manifest.get("tag_message", manifest["title"])], root)
    if not remote_tag(root, url, manifest):
        run(["git", "push", "--porcelain", "--no-follow-tags", "--no-force", "--no-mirror", url, "refs/tags/" + manifest["tag"] + ":refs/tags/" + manifest["tag"]], root)
    require(remote_tag(root, url, manifest), "Remote tag was not verified")


class Receipt:
    def __init__(self, path, manifest_hash):
        self.path = path
        self.value = read_json(path) if path.exists() else {"manifest_sha256": manifest_hash, "completed": [], "current": None}
        require(self.value.get("manifest_sha256") == manifest_hash, "Receipt belongs to another manifest; preserve it and use a new release work folder")
        done = self.value.get("completed")
        require(isinstance(done, list) and len(done) == len(set(done)) and set(done) <= {"gitlab", "github", "aminet"} and
                self.value.get("current") in (None, "gitlab", "github", "aminet"), "Invalid publication receipt")

    def save(self):
        atomic(self.path, canonical(self.value))

    def start(self, name):
        self.value["current"] = name
        self.save()

    def done(self, name):
        if name not in self.value["completed"]:
            self.value["completed"].append(name)
        self.value["current"] = None
        self.save()


class Aminet:
    HOST = "main.aminet.net"
    DIRECTORY = "/new"

    def __init__(self, manifest, folder, receipt_path, ftp_factory=ftplib.FTP):
        self.m, self.folder, self.path, self.ftp_factory = manifest, folder, receipt_path, ftp_factory
        self.value = manifest.get("aminet")

    def preflight(self):
        require(self.value and re.fullmatch(r"[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}", self.value.get("uploader", "")),
                "Prepare Aminet files with --uploader EMAIL before selecting Aminet")
        require(self.value.get("host") == self.HOST and self.value.get("directory") == self.DIRECTORY, "Unexpected Aminet destination")
        require(set(self.value.get("files", {})) == {package_name("client", "amigaos", "m68k", self.m["version"], ext) for ext in ("lha", "readme")}, "Unexpected Aminet inventory")
        for name, expected in self.value["files"].items():
            require(sha(self.folder / name) == expected and self.m["files"].get(name) == expected, "Aminet files changed")
        if self.path.exists():
            receipt = read_json(self.path)
            require(receipt.get("manifest_sha256") == hashlib.sha256(canonical(self.m)).hexdigest(), "Aminet receipt belongs to another manifest")
            require(receipt.get("state") == "submitted", "Aminet upload may be partial. Inspect the receipt and incoming server before manual recovery; no resubmission was attempted")
            return
        # Read-only connection test. The submission connection also checks the incoming names.
        with self.ftp_factory(self.HOST, timeout=60) as ftp:
            ftp.login("anonymous", self.value["uploader"])
            ftp.cwd(self.DIRECTORY)
            present = self.incoming(ftp)
            require(not (set(self.value["files"]) & present), "Aminet incoming already contains a submission with these names; inspect before submitting")

    @staticmethod
    def incoming(ftp):
        try:
            return {Path(n).name for n in ftp.nlst()}
        except ftplib.error_perm as exc:
            # Some servers report an empty incoming directory as 550.
            if str(exc).lower().startswith("550 no files"):
                return set()
            raise ReleaseError("Cannot list Aminet incoming directory") from exc

    def publish(self):
        if self.path.exists():
            self.preflight()
            return
        receipt = {"manifest_sha256": hashlib.sha256(canonical(self.m)).hexdigest(), "host": self.HOST,
                   "directory": self.DIRECTORY, "state": "started", "files": self.value["files"], "uploaded": []}
        # Persist intent BEFORE opening the submission connection. Even an interrupted login requires inspection.
        atomic(self.path, canonical(receipt))
        with self.ftp_factory(self.HOST, timeout=120) as ftp:
            ftp.login("anonymous", self.value["uploader"])
            ftp.cwd(self.DIRECTORY)
            require(not (self.incoming(ftp) & set(self.value["files"])), "Aminet incoming name conflict")
            # Submit the readme last; moderation can pick up the pair once both are complete.
            for name in sorted(self.value["files"], key=lambda x: x.endswith(".readme")):
                with (self.folder / name).open("rb") as f:
                    ftp.storbinary("STOR " + name, f)
                receipt["uploaded"].append(name)
                atomic(self.path, canonical(receipt))
        receipt["state"] = "submitted"
        atomic(self.path, canonical(receipt))


def publish(root, manifest, folder, work, destinations, confirmed, verify, adapters=None, cfg=None):
    require(confirmed, "Publication requires --confirm-publish")
    require(destinations and len(destinations) == len(set(destinations)) and
            set(destinations) <= {"github", "gitlab", "aminet"}, "Choose distinct destinations: gitlab, github, aminet")
    verify(publication=True)
    mh = hashlib.sha256(canonical(manifest)).hexdigest()
    receipt = Receipt(work / "receipt.json", mh)
    selected = {}
    # Fetch every credential before contacting any destination.
    tokens = {}
    for name in destinations:
        if name != "aminet":
            tokens[name] = credential(name, cfg or {})
    for name in destinations:
        if adapters and name in adapters:
            selected[name] = adapters[name]
        elif name == "aminet":
            selected[name] = Aminet(manifest, folder, work / "aminet-receipt.json")
        else:
            selected[name] = (GitHub if name == "github" else GitLab)(manifest, folder, tokens[name])
    local_tag(root, manifest)
    if any(n != "aminet" for n in destinations):
        run(["git", "var", "GIT_COMMITTER_IDENT"], root, capture=True)
    # No remote mutations until ALL selected services and tag destinations pass preflight.
    for name, adapter in selected.items():
        adapter.preflight()
        if name != "aminet":
            remote_tag(root, manifest[name + "_git"], manifest)
    for name, adapter in selected.items():
        # Revalidate even a completed destination, so changed remote assets are never silently trusted.
        verify(publication=True)
        receipt.start(name)
        if name != "aminet":
            push_tag(root, manifest[name + "_git"], manifest)
        adapter.publish()
        receipt.done(name)
        print(name + (": submitted for moderation" if name == "aminet" else ": release verified"))
