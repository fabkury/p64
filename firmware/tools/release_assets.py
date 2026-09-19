#!/usr/bin/env python3
r"""Prepare the two GitHub release assets the firmware's updater looks for.

    python tools\release_assets.py            # after .\tools\build.ps1

Writes build\release\p64-firmware.bin (a copy of build\p64.bin) and
build\release\p64-firmware.bin.sha256 ("<hex>  p64-firmware.bin"), and prints the
version embedded in the image. Publish both as assets of a release tagged
v<MAJOR.MINOR.PATCH> matching PROJECT_VER in CMakeLists.txt; the device installs only
when the tag is newer than what it runs.
"""

import hashlib
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.abspath(os.path.join(HERE, ".."))
ASSET = "p64-firmware.bin"


def main():
    src = os.path.join(FW, "build", "p64.bin")
    if not os.path.exists(src):
        sys.exit("build/p64.bin not found; run tools\\build.ps1 first")
    out_dir = os.path.join(FW, "build", "release")
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, ASSET)
    shutil.copyfile(src, dst)
    digest = hashlib.sha256(open(dst, "rb").read()).hexdigest()
    with open(dst + ".sha256", "w", newline="\n") as f:
        f.write("%s  %s\n" % (digest, ASSET))
    version = "?"
    m = re.search(r'set\(PROJECT_VER "([^"]+)"\)', open(os.path.join(FW, "CMakeLists.txt")).read())
    if m:
        version = m.group(1)
    print("%s (%d bytes), sha256 %s, version %s" % (dst, os.path.getsize(dst), digest, version))
    print("release tag: v%s" % version)


if __name__ == "__main__":
    main()
