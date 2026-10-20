#!/usr/bin/env python3
r"""Renders the intro animations to animated GIFs on the host, for appraisal in a browser
(p101): the same C++ that runs on the device, compiled with g++ against p64_gfx and
p64_intro (tools/intro_dump.cpp), writes every frame; Pillow scales them up without
smoothing and writes one GIF per animation.

    python tools\render_intro.py [--out DIR] [--window-ms 3000] [--seed 7] [--scale 4] [name ...]

Defaults: docs/images/intro/<name>.gif at 4x (256x256) plus an index.html that shows them
all, a 3 s window at 50 fps, one seed for every animation so the gallery is reproducible.
The folder is git-ignored (26 GIFs are 11 MB); the tool rebuilds it in under a minute.
Needs g++ on the PATH and Pillow.
"""

import argparse
import glob
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.abspath(os.path.join(HERE, ".."))
REPO = os.path.abspath(os.path.join(FIRMWARE, ".."))
COMPONENTS = os.path.join(FIRMWARE, "components")
BUILD = os.path.join(FIRMWARE, "tests", "host", "build", "intro_dump")
FRAME_MS = 20
W = H = 64


def build():
    os.makedirs(BUILD, exist_ok=True)
    exe = os.path.join(BUILD, "intro_dump.exe" if os.name == "nt" else "intro_dump")
    sources = [os.path.join(HERE, "intro_dump.cpp"), os.path.join(COMPONENTS, "p64_gfx", "src", "frame.cpp")]
    sources += sorted(glob.glob(os.path.join(COMPONENTS, "p64_intro", "src", "*.cpp")))
    newest = max(os.path.getmtime(s) for s in sources + glob.glob(os.path.join(COMPONENTS, "p64_intro", "**", "*.hpp"), recursive=True))
    if os.path.exists(exe) and os.path.getmtime(exe) >= newest:
        return exe
    cmd = ["g++", "-std=c++20", "-O2", "-Wall", "-Wextra",
           "-I" + os.path.join(COMPONENTS, "p64_gfx", "include"),
           "-I" + os.path.join(COMPONENTS, "p64_intro", "include"),
           "-I" + os.path.join(COMPONENTS, "p64_intro", "src"),
           *sources, "-o", exe]
    print("building intro_dump...")
    subprocess.run(cmd, check=True)
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(REPO, "docs", "images", "intro"))
    ap.add_argument("--window-ms", type=int, default=3000)
    ap.add_argument("--seed", default="7")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("names", nargs="*")
    args = ap.parse_args()
    exe = build()
    os.makedirs(args.out, exist_ok=True)
    names = args.names or [None]
    written = []
    for only in names:
        cmd = [exe, str(args.window_ms), args.seed] + ([only] if only else [])
        with subprocess.Popen(cmd, stdout=subprocess.PIPE) as proc:
            while True:
                header = proc.stdout.readline()
                if not header:
                    break
                name, frames = header.decode().split()
                frames = int(frames)
                images = []
                for _ in range(frames):
                    raw = proc.stdout.read(W * H * 3)
                    if len(raw) != W * H * 3:
                        sys.exit(f"{name}: short frame")
                    im = Image.frombytes("RGB", (W, H), raw)
                    if args.scale != 1:
                        im = im.resize((W * args.scale, H * args.scale), Image.NEAREST)
                    images.append(im)
                path = os.path.join(args.out, name + ".gif")
                # Pillow quantises each frame to 256 colours; the wordmark's rainbow has more,
                # so colours are close, not exact (the host test is the pixel-exact check).
                images[0].save(path, save_all=True, append_images=images[1:], duration=FRAME_MS, loop=0,
                               optimize=False, disposal=1)
                written.append((name, os.path.getsize(path)))
                print(f"{name}: {frames} frames, {os.path.getsize(path) // 1024} KB")
    # A page that shows them all, side by side, to open in a browser.
    rows = "\n".join(
        f'<figure><img src="{n}.gif" alt="{n}" width="256" height="256"><figcaption>{i + 1}. {n.replace("_", " ")} ({kb // 1024} KB)</figcaption></figure>'
        for i, (n, kb) in enumerate(written))
    with open(os.path.join(args.out, "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write("<!doctype html><meta charset=utf-8><title>p64 intro animations</title>"
                "<style>body{background:#111;color:#ddd;font:14px sans-serif;margin:16px}"
                "main{display:grid;grid-template-columns:repeat(auto-fill,272px);gap:16px}"
                "figure{margin:0}img{image-rendering:pixelated;display:block;background:#000}figcaption{margin-top:4px}</style>"
                f"<h1>p64 intro animations</h1><p>{len(written)} animations, a {args.window_ms} ms window at 50 fps, seed {args.seed}; "
                "the last 15 % of the window holds the finished logo. Regenerate with tools/render_intro.py.</p>"
                f"<main>{rows}</main>")
    print(f"{len(written)} GIFs and index.html in {args.out}")


if __name__ == "__main__":
    main()
