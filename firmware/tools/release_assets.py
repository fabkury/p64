#!/usr/bin/env python3
r"""Collect the release assets of a build: the files a GitHub release of the firmware
carries (docs/release-build.md).

    python tools\release_assets.py                     # after .\tools\build.ps1
    python tools\release_assets.py --build-dir build_public
    python tools\release_assets.py --check-tag v0.2.0  # CI: the tag must match PROJECT_VER

Writes <build>/release/ with:

    p64-firmware.bin          the application image (build/p64.bin); the asset the
                              updater installs (CONFIG_P64_OTA_ASSET)
    p64-firmware.bin.sha256   "<hex>  p64-firmware.bin": the updater verifies it
    bootloader.bin            the second-stage bootloader (flash offset 0x0 on the S3)
    partition-table.bin       the partition table (0x8000)
    ota_data_initial.bin      the OTA selector pointing at ota_0 (0x19000)
    flash_args                one "<offset> <file>" line per file above, in flash order,
                              with the files named as published (the web flasher and
                              the command-line guide read it); the esptool options line
                              is kept first
    SHA256SUMS                every file's digest, "sha256sum -c" format

The first two are the OTA contract (firmware/docs/api.md, "Updates"); the rest are
what a USB flash needs. The release is tagged v<PROJECT_VER>; the device installs a
release only when its tag is newer than the version it runs, and the CI release
workflow (.github/workflows/release.yml) refuses a tag that does not match
CMakeLists.txt.
"""

import argparse
import hashlib
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.abspath(os.path.join(HERE, ".."))
ASSET = "p64-firmware.bin"

# build-relative source -> published name
FILES = [
    ("p64.bin", ASSET),
    ("bootloader/bootloader.bin", "bootloader.bin"),
    ("partition_table/partition-table.bin", "partition-table.bin"),
    ("ota_data_initial.bin", "ota_data_initial.bin"),
]


def project_version():
    text = open(os.path.join(FW, "CMakeLists.txt"), encoding="utf-8").read()
    m = re.search(r'set\(PROJECT_VER "([^"]+)"\)', text)
    return m.group(1) if m else "?"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def normalize_flash_args(text):
    """The options line first, then "<offset> <published name>" in offset order."""
    options = None
    entries = []
    names = {src.split("/")[-1]: dst for src, dst in FILES}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("--"):
            options = line
            continue
        offset, path = line.split(None, 1)
        base = path.replace("\\", "/").split("/")[-1]
        if base not in names:
            sys.exit("flash_args names a file this script does not publish: %s" % path)
        entries.append((int(offset, 16), names[base]))
    if len(entries) != len(FILES):
        sys.exit("flash_args lists %d files, expected %d" % (len(entries), len(FILES)))
    entries.sort()
    lines = ([options] if options else []) + ["0x%x %s" % (off, name) for off, name in entries]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="build", help="build directory under firmware/ (default: build)")
    ap.add_argument("--check-tag", metavar="TAG", help="fail unless TAG equals v<PROJECT_VER>")
    ap.add_argument("--only-check", action="store_true", help="stop after the tag check (no build needed)")
    args = ap.parse_args()

    version = project_version()
    if args.check_tag is not None and args.check_tag != "v" + version:
        sys.exit("tag %s does not match PROJECT_VER %s (CMakeLists.txt)" % (args.check_tag, version))
    if args.only_check:
        print("PROJECT_VER %s%s" % (version, ", tag %s matches" % args.check_tag if args.check_tag else ""))
        return

    build = os.path.join(FW, args.build_dir)
    if not os.path.exists(os.path.join(build, "p64.bin")):
        sys.exit("%s/p64.bin not found; run tools\\build.ps1 first" % args.build_dir)
    out_dir = os.path.join(build, "release")
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    sums = []
    for src, dst in FILES:
        path = os.path.join(build, src)
        if not os.path.exists(path):
            sys.exit("%s not found" % path)
        shutil.copyfile(path, os.path.join(out_dir, dst))
        sums.append((sha256(os.path.join(out_dir, dst)), dst))

    with open(os.path.join(out_dir, ASSET + ".sha256"), "w", newline="\n") as f:
        f.write("%s  %s\n" % (sums[0][0], ASSET))
    flash_args = normalize_flash_args(open(os.path.join(build, "flash_args"), encoding="utf-8").read())
    with open(os.path.join(out_dir, "flash_args"), "w", newline="\n") as f:
        f.write(flash_args)
    with open(os.path.join(out_dir, "SHA256SUMS"), "w", newline="\n") as f:
        for digest, name in sums:
            f.write("%s  %s\n" % (digest, name))

    print("release assets in %s (version %s, tag v%s):" % (out_dir, version, version))
    for digest, name in sums:
        print("  %-24s %9d bytes  %s" % (name, os.path.getsize(os.path.join(out_dir, name)), digest))
    print("flash_args:")
    for line in flash_args.splitlines():
        print("  " + line)


if __name__ == "__main__":
    main()
