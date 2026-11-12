#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 PACKAGES/OPENFIREPOWER SITE_DIRECTORY VERSION" >&2
    exit 2
fi
packages=$1
destination=$2/assets/releases
version=$3
case "$version" in
    *[!0-9A-Za-z.-]*) echo "Invalid release version" >&2; exit 2 ;;
    [0-9]*) ;;
    *) echo "Invalid release version" >&2; exit 2 ;;
esac

dmg=openfp-client-macos-universal-${version}.dmg
zip=openfp-client-macos-universal-${version}.zip
lha=openfp-client-amigaos-m68k-${version}.lha
for archive in "macOS/$dmg" "macOS/$zip" "AmigaOS/$lha"; do
    [ -s "$packages/$archive" ] || { echo "Missing $packages/$archive" >&2; exit 1; }
done

mkdir -p "$destination"
stage=$(mktemp -d "$destination/.release.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
cp "$packages/macOS/$dmg" "$packages/macOS/$zip" "$packages/AmigaOS/$lha" "$stage/"
# Optional desktop releases are included when built; tvOS remains a testing preview.
for archive in "Windows/openfp-client-windows-x86_64-${version}.msi" \
               "Windows/openfp-client-windows-x86_64-${version}.zip" \
               "Linux/x86_64/openfp-server-linux-x86_64-${version}.tar.gz" \
               "Linux/x86_64/openfp-client-linux-x86_64-${version}.tar.gz" \
               "Linux/aarch64/openfp-server-linux-aarch64-${version}.tar.gz" \
               "Linux/aarch64/openfp-client-linux-aarch64-${version}.tar.gz" \
               "Source/OpenFirePower-${version}-source.tar.gz"; do
    [ ! -s "$packages/$archive" ] || cp "$packages/$archive" "$stage/"
done
# Generate portable per-file checksums for every staged binary package.
for archive in "$stage"/*.zip "$stage"/*.dmg "$stage"/*.tar.gz "$stage"/*.lha "$stage"/*.msi; do
    [ ! -f "$archive" ] || python3 "$(dirname "$0")/../scripts/release_assets.py" checksum "$archive"
done
# Releases are public downloads even when the source archive was created privately.
chmod 644 "$stage"/*
(
    cd "$stage"
    find . -type f ! -name '*SHA256SUMS*' -exec shasum -a 256 {} + > "OpenFirePower-${version}-SHA256SUMS.txt"
)
for archive in "$stage"/*; do
    mv -f "$archive" "$destination/$(basename "$archive")"
done
