#!/usr/bin/env python3
"""Static size budgets: the image and the internal RAM the linker uses, against budgets.json.

Reads build/p64.bin and build/p64.map (a build must exist), runs esp_idf_size on the map
for the DIRAM figure, and fails when a ceiling in firmware/budgets.json is exceeded.
Run with ESP-IDF's Python (the venv under C:/Espressif/tools/python, or `idf.py`'s
environment); the system Python works when esp_idf_size is importable.

usage: python tools/check_size.py [build_dir]
"""
import json
import os
import subprocess
import sys


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "build")
    with open(os.path.join(root, "budgets.json")) as f:
        b = json.load(f)
    binary = os.path.join(build, "p64.bin")
    mapfile = os.path.join(build, "p64.map")
    if not os.path.exists(binary) or not os.path.exists(mapfile):
        print("no build under %s" % build)
        return 2
    image = os.path.getsize(binary)
    out = subprocess.run([sys.executable, "-m", "esp_idf_size", "--format", "json", mapfile], capture_output=True, text=True)
    if out.returncode != 0:
        print("esp_idf_size failed: %s" % out.stderr.strip())
        return 2
    report = json.loads(out.stdout)
    # The legacy JSON (esp_idf_size 1.x, the one in the v5.5.4 venv) reports used_diram;
    # newer versions list the memory types under "layout".
    diram = report.get("used_diram")
    if diram is None:
        for layout in report.get("layout", []) if isinstance(report.get("layout"), list) else []:
            if isinstance(layout, dict) and layout.get("name") == "DIRAM":
                diram = layout.get("used")
    failures = 0

    def check(cond, what):
        nonlocal failures
        print(("  ok   " if cond else "  FAIL ") + what)
        if not cond:
            failures += 1

    check(image <= b["image_bytes_max"], "image %d bytes <= budget %d" % (image, b["image_bytes_max"]))
    if diram is None:
        print("  ?    DIRAM figure not found in the size report; check esp_idf_size's format")
        failures += 1
    else:
        check(diram <= b["static_diram_bytes_max"], "static DIRAM %d bytes <= budget %d" % (diram, b["static_diram_bytes_max"]))
    print("check_size: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
