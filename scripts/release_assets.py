# SPDX-License-Identifier: MIT
"""Shared public package names and portable SHA-256 sidecars. No build or upload."""
import argparse
import hashlib
from pathlib import Path
import re


FORMATS = {
    ("client", "macos", "universal"): ("zip", "dmg"),
    ("client", "windows", "x86_64"): ("zip", "msi"),
    ("client", "linux", "x86_64"): ("tar.gz",),
    ("client", "linux", "aarch64"): ("tar.gz",),
    ("server", "linux", "x86_64"): ("tar.gz",),
    ("server", "linux", "aarch64"): ("tar.gz",),
    ("client", "amigaos", "m68k"): ("lha", "readme"),
}


def package_name(role, platform, architecture, version, extension):
    if not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", version):
        raise ValueError("Package version must be numeric, for example 0.1")
    if extension not in FORMATS.get((role, platform, architecture), ()):
        raise ValueError("Unsupported package platform, architecture or format")
    return f"openfp-{role}-{platform}-{architecture}-{version}.{extension}"


def checksum(path):
    path = Path(path)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    # Only the basename belongs in a downloadable checksum, never a build-host path.
    result = path.with_name(path.name + ".sha256")
    result.write_text(digest.hexdigest() + "  " + path.name + "\n", encoding="ascii")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    name = actions.add_parser("name")
    for field in ("role", "platform", "architecture", "version", "extension"):
        name.add_argument(field)
    sums = actions.add_parser("checksum")
    sums.add_argument("files", nargs="+")
    args = parser.parse_args()
    if args.action == "name":
        try:
            print(package_name(args.role, args.platform, args.architecture, args.version, args.extension))
        except ValueError as error:
            parser.error(str(error))
    else:
        for path in args.files:
            checksum(path)


if __name__ == "__main__":
    main()
