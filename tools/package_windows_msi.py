#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build an x64 Windows Installer from the staged portable game."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import uuid
from xml.etree.ElementTree import Element, ElementTree, SubElement


UPGRADE_CODE = uuid.UUID("42a4e5b4-67c9-4513-9b0b-3d3ad2cfb0ab")


def package(payload, version, output):
    if not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", version):
        raise ValueError(f"Windows Installer needs a numeric release version, got {version!r}")
    if version.count(".") == 1:
        version += ".0"
    major, minor, patch = map(int, version.split("."))
    if major > 255 or minor > 255 or patch > 65535:
        raise ValueError(f"Windows Installer version is out of range: {version}")

    root = Element("Wix", xmlns="http://schemas.microsoft.com/wix/2006/wi")
    product = SubElement(root, "Product", Id="*", Name="OpenFP", Language="1033",
                         Version=version, Manufacturer="OpenFP", UpgradeCode=str(UPGRADE_CODE))
    SubElement(product, "Package", InstallerVersion="500", Compressed="yes", InstallScope="perMachine")
    SubElement(product, "MajorUpgrade", AllowSameVersionUpgrades="yes",
               DowngradeErrorMessage="A newer version of OpenFP is already installed.")
    SubElement(product, "Media", Id="1", Cabinet="OpenFirePower.cab", EmbedCab="yes")
    target = SubElement(product, "Directory", Id="TARGETDIR", Name="SourceDir")
    program_files = SubElement(target, "Directory", Id="ProgramFiles64Folder")
    game_dir = SubElement(program_files, "Directory", Id="INSTALLDIR", Name="OpenFirePower")
    start_menu = SubElement(target, "Directory", Id="ProgramMenuFolder")
    feature = SubElement(product, "Feature", Id="Game", Title="OpenFP", Level="1")

    with tempfile.TemporaryDirectory(prefix="ofp-msi-", dir=output.parent) as temporary:
        work = Path(temporary)

        def source(path):
            return os.path.relpath(path, work)

        def component(directory, identifier, guid):
            SubElement(feature, "ComponentRef", Id=identifier)
            return SubElement(directory, "Component", Id=identifier, Guid=str(guid), Win64="yes")

        def add_files(path, directory):
            for entry in sorted(path.iterdir()):
                if entry.name.startswith("."):
                    continue
                key = entry.relative_to(payload).as_posix()
                guid = uuid.uuid5(UPGRADE_CODE, key.casefold())
                if entry.is_dir():
                    subdirectory = SubElement(directory, "Directory", Id="D" + guid.hex, Name=entry.name)
                    add_files(entry, subdirectory)
                elif entry.is_file():
                    record = component(directory, "C" + guid.hex, guid)
                    identifier = "GameExe" if key == "openfirepower.exe" else "F" + guid.hex
                    SubElement(record, "File", Id=identifier, Name=entry.name, Source=source(entry), KeyPath="yes")

        add_files(payload, game_dir)
        shortcut = component(start_menu, "MenuShortcut", uuid.uuid5(UPGRADE_CODE, "start-menu"))
        SubElement(shortcut, "Shortcut", Id="OpenFirePowerShortcut", Name="OpenFP",
                   Target="[#GameExe]", WorkingDirectory="INSTALLDIR")
        SubElement(shortcut, "RegistryValue", Root="HKLM", Key=r"Software\OpenFirePower",
                   Name="StartMenuShortcut", Type="integer", Value="1", KeyPath="yes")
        icon = Path(__file__).resolve().parents[1] / "packaging/windows/openfirepower.ico"
        SubElement(product, "Icon", Id="GameIcon", SourceFile=source(icon))
        SubElement(product, "Property", Id="ARPPRODUCTICON", Value="GameIcon")

        manifest = work / "OpenFirePower.wxs"
        installer = work / output.name
        ElementTree(root).write(manifest, encoding="utf-8", xml_declaration=True)
        subprocess.run(["wixl", "-a", "x64", "-o", installer.name, manifest.name], cwd=work, check=True)
        installer.replace(output)
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
    from release_assets import checksum
    checksum(output)
    print(f"built {output}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(f"Usage: {sys.argv[0]} PAYLOAD VERSION OUTPUT.msi")
    package(Path(sys.argv[1]).resolve(), sys.argv[2], Path(sys.argv[3]).resolve())
