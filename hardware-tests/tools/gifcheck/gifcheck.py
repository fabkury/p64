#!/usr/bin/env python3
"""Check the firmware's GIF pipeline on the PC against Pillow.

Builds tools/gifcheck/gifcheck.cpp with the vendored AnimatedGIF and the firmware's
gif_player.cpp (the exact code the board runs), decodes every GIF given (default:
assets/gifs/*.gif), and compares, frame by frame:

  1. the composited canvas against Pillow's decode of the same frame (transparent
     pixels expected black), pixel-exact;
  2. the 64x64 scaled output against a Python re-implementation of the scaling rule
     (nearest when enlarging, box average when shrinking), pixel-exact.

Usage: python gifcheck.py [--keep] [gif ...]
Needs g++ on PATH (any recent GCC) and Pillow.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.abspath(os.path.join(HERE, "..", ".."))
DST_W, DST_H = 64, 64


def build(build_dir):
    exe = os.path.join(build_dir, "gifcheck.exe" if os.name == "nt" else "gifcheck")
    sources = [
        os.path.join(HERE, "gifcheck.cpp"),
        os.path.join(FIRMWARE, "main", "gif_player.cpp"),
        os.path.join(FIRMWARE, "components", "animatedgif", "src", "AnimatedGIF.cpp"),
    ]
    # Rebuild when any input changes: the sources, the headers they include, or this
    # script (the flags below).
    inputs = sources + [
        os.path.join(FIRMWARE, "main", "gif_player.hpp"),
        os.path.join(FIRMWARE, "components", "animatedgif", "src", "AnimatedGIF.h"),
        os.path.join(FIRMWARE, "components", "animatedgif", "src", "gif.inl"),
        os.path.abspath(__file__),
    ]
    if os.path.exists(exe) and all(os.path.getmtime(s) < os.path.getmtime(exe) for s in inputs):
        return exe
    # -static: a dynamically linked MinGW build picks up whichever libstdc++ DLL comes
    # first on PATH and crashes at load time when it is from another toolchain.
    cmd = [
        "g++", "-std=c++20", "-O2", "-w", "-static",
        "-D__LINUX__",
        "-I" + os.path.join(FIRMWARE, "main"),
        "-I" + os.path.join(FIRMWARE, "components", "animatedgif", "src"),
        *sources, "-o", exe,
    ]
    print("building:", " ".join(os.path.basename(c) if c.endswith(".cpp") else c for c in cmd))
    subprocess.run(cmd, check=True)
    return exe


def expected_scaled(canvas, w, h):
    """Python twin of Scaler: returns (ox, oy, ow, oh, bytes of DST_W*DST_H*3)."""
    s = min(DST_W / w, DST_H / h)
    ow = max(1, min(DST_W, int(round_half_away(w * s))))
    oh = max(1, min(DST_H, int(round_half_away(h * s))))
    ox, oy = (DST_W - ow) // 2, (DST_H - oh) // 2

    def spans(src, out):
        v = []
        for i in range(out):
            if out >= src:
                p = min(src - 1, ((i * 2 + 1) * src) // (2 * out))
                v.append((p, p + 1))
            else:
                b = (i * src) // out
                e = ((i + 1) * src) // out
                if e <= b:
                    e = b + 1
                v.append((b, min(e, src)))
        return v

    cols, rows = spans(w, ow), spans(h, oh)
    out = bytearray(DST_W * DST_H * 3)
    for r in range(oh):
        y0, y1 = rows[r]
        for c in range(ow):
            x0, x1 = cols[c]
            n = (y1 - y0) * (x1 - x0)
            sr = sg = sb = 0
            for y in range(y0, y1):
                base = (y * w + x0) * 3
                for x in range(x1 - x0):
                    sr += canvas[base + x * 3]
                    sg += canvas[base + x * 3 + 1]
                    sb += canvas[base + x * 3 + 2]
            half = n // 2
            o = ((oy + r) * DST_W + ox + c) * 3
            out[o] = (sr + half) // n
            out[o + 1] = (sg + half) // n
            out[o + 2] = (sb + half) // n
    return ox, oy, ow, oh, bytes(out)


def round_half_away(x):
    # C's lround: halves away from zero.
    return int(x + 0.5) if x >= 0 else -int(-x + 0.5)


def pillow_frames(path):
    """Yields each frame composited over black as bytes (w*h*3), like the firmware does."""
    im = Image.open(path)
    w, h = im.size
    i = 0
    while True:
        try:
            im.seek(i)
        except EOFError:
            return
        rgba = im.convert("RGBA")
        px = rgba.tobytes()
        out = bytearray(w * h * 3)
        for p in range(w * h):
            if px[p * 4 + 3]:
                out[p * 3:p * 3 + 3] = px[p * 4:p * 4 + 3]
        yield bytes(out)
        i += 1


def first_diff(a, b, w):
    for i in range(0, min(len(a), len(b)), 3):
        if a[i:i + 3] != b[i:i + 3]:
            p = i // 3
            return p % w, p // w, tuple(a[i:i + 3]), tuple(b[i:i + 3])
    return None


def check(path, frames_path):
    with open(frames_path, "rb") as f:
        assert f.readline() == b"P64GIF\n", "bad header"
        w, h, ox, oy, ow, oh = map(int, f.readline().split())
        data = f.read()
    canvas_bytes, scaled_bytes = w * h * 3, DST_W * DST_H * 3
    per_frame = canvas_bytes + scaled_bytes
    assert len(data) % per_frame == 0, "truncated frame data"
    n = len(data) // per_frame

    problems = []
    ref_frames = list(pillow_frames(path))
    if len(ref_frames) != n:
        problems.append(f"frame count {n} (firmware) vs {len(ref_frames)} (Pillow)")
    for i in range(min(n, len(ref_frames))):
        canvas = data[i * per_frame:i * per_frame + canvas_bytes]
        scaled = data[i * per_frame + canvas_bytes:(i + 1) * per_frame]
        d = first_diff(canvas, ref_frames[i], w)
        if d:
            problems.append(f"frame {i}: canvas differs from Pillow at ({d[0]},{d[1]}): ours {d[2]} vs {d[3]}")
        eox, eoy, eow, eoh, expected = expected_scaled(canvas, w, h)
        if (eox, eoy, eow, eoh) != (ox, oy, ow, oh):
            problems.append(f"placement {(ox, oy, ow, oh)} vs expected {(eox, eoy, eow, eoh)}")
        d = first_diff(scaled, expected, DST_W)
        if d:
            problems.append(f"frame {i}: scaled output differs at ({d[0]},{d[1]}): ours {d[2]} vs {d[3]}")
        if len(problems) > 5:
            problems.append("...")
            break
    return n, problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gifs", nargs="*", help="GIF files (default: assets/gifs/*.gif)")
    ap.add_argument("--keep", action="store_true", help="keep the dumped frames in tools/gifcheck/build/")
    args = ap.parse_args()

    gifs = args.gifs or sorted(glob.glob(os.path.join(FIRMWARE, "assets", "gifs", "*.gif")))
    if not gifs:
        print("no GIFs found")
        return 2
    build_dir = os.path.join(HERE, "build")
    os.makedirs(build_dir, exist_ok=True)
    exe = build(build_dir)

    out_dir = os.path.join(build_dir, "frames") if args.keep else tempfile.mkdtemp(prefix="gifcheck-")
    os.makedirs(out_dir, exist_ok=True)
    try:
        run = subprocess.run([exe, str(DST_W), str(DST_H), out_dir, *gifs], capture_output=True, text=True)
        if run.returncode != 0:
            print(run.stdout)
            print(run.stderr)
            print("gifcheck binary reported failures")
        total_frames = 0
        bad = 0
        for path in gifs:
            frames_path = os.path.join(out_dir, os.path.basename(path) + ".frames")
            if not os.path.exists(frames_path):
                print(f"FAIL {os.path.basename(path)}: no output (decode failed)")
                bad += 1
                continue
            n, problems = check(path, frames_path)
            total_frames += n
            if problems:
                bad += 1
                print(f"FAIL {os.path.basename(path)} ({n} frames)")
                for p in problems:
                    print("     " + p)
            else:
                print(f"ok   {os.path.basename(path)} ({n} frames)")
        print(f"\n{len(gifs)} GIFs, {total_frames} frames, {bad} with problems")
        return 1 if bad or run.returncode else 0
    finally:
        if not args.keep:
            shutil.rmtree(out_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
