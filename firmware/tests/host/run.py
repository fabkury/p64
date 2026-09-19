#!/usr/bin/env python3
r"""Host tests for the ESP-IDF-free components (p64_gfx, p64_decode).

Builds tests/host/main.cpp with the component sources using the PC's g++ (the exact code
the board runs), then:

  1. runs the unit tests (scaler, rotation, gains, sniffing, delay rule, frame queue);
  2. decodes every GIF of the corpus (default: hardware-tests/assets/gifs/*.gif) with
     the firmware's decoder + scaler and compares each frame of the first loop against
     Pillow (canvas pixel-exact, transparent pixels black) and against a Python twin of
     the scaling rule (integer nearest-neighbour up, box average down).

Usage: python tests/host/run.py [--keep] [--no-gifs] [gif ...]
Needs g++ on PATH and Pillow (the system Python has it; the ESP-IDF venv does not).
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(FIRMWARE, ".."))
DST_W, DST_H = 64, 64

COMPONENTS = os.path.join(FIRMWARE, "components")
SOURCES = [
    os.path.join(HERE, "main.cpp"),
    os.path.join(COMPONENTS, "p64_gfx", "src", "frame.cpp"),
    os.path.join(COMPONENTS, "p64_gfx", "src", "scaler.cpp"),
    os.path.join(COMPONENTS, "p64_decode", "src", "format.cpp"),
    os.path.join(COMPONENTS, "p64_decode", "src", "gif_decoder.cpp"),
    os.path.join(COMPONENTS, "animatedgif", "src", "AnimatedGIF.cpp"),
]
INCLUDES = [
    os.path.join(COMPONENTS, "p64_gfx", "include"),
    os.path.join(COMPONENTS, "p64_decode", "include"),
    os.path.join(COMPONENTS, "p64_playback", "include"),
    os.path.join(COMPONENTS, "animatedgif", "src"),
]


def newest_mtime(paths):
    latest = 0.0
    for p in paths:
        for root, _dirs, files in os.walk(p) if os.path.isdir(p) else [(os.path.dirname(p), [], [os.path.basename(p)])]:
            for f in files:
                latest = max(latest, os.path.getmtime(os.path.join(root, f)))
    return latest


def build(build_dir):
    exe = os.path.join(build_dir, "p64_hosttest.exe" if os.name == "nt" else "p64_hosttest")
    inputs = SOURCES + INCLUDES + [os.path.abspath(__file__)]
    if os.path.exists(exe) and newest_mtime(inputs) < os.path.getmtime(exe):
        return exe
    # -static: a dynamically linked MinGW build picks up whichever libstdc++ DLL comes
    # first on PATH and crashes at load time when it is from another toolchain.
    cmd = ["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-static", "-D__LINUX__"]
    cmd += ["-I" + inc for inc in INCLUDES]
    cmd += SOURCES + ["-o", exe]
    print("building host tests...")
    subprocess.run(cmd, check=True)
    return exe


def expected_scaled(canvas, w, h):
    """Python twin of gfx::Scaler: returns (ox, oy, ow, oh, bytes of DST_W*DST_H*3)."""
    if w <= DST_W and h <= DST_H:
        factor = max(1, min(DST_W // w, DST_H // h))
        ow, oh = w * factor, h * factor
    else:
        s = min(DST_W / w, DST_H / h)
        ow = max(1, min(DST_W, int(w * s + 0.5)))
        oh = max(1, min(DST_H, int(h * s + 0.5)))
    ox, oy = (DST_W - ow) // 2, (DST_H - oh) // 2

    def spans(src, out):
        v = []
        for i in range(out):
            if out >= src:
                p = min(src - 1, (i * src) // out)
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


def pillow_frames(path):
    """Yields each frame composited over black as bytes (w*h*3) and its delay."""
    from PIL import Image

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
        yield bytes(out), im.info.get("duration", 0)
        i += 1


def first_diff(a, b, w):
    for i in range(0, min(len(a), len(b)), 3):
        if a[i:i + 3] != b[i:i + 3]:
            p = i // 3
            return p % w, p // w, tuple(a[i:i + 3]), tuple(b[i:i + 3])
    return None


def browser_delay(stored):
    return 100 if stored <= 10 else stored


def check(path, frames_path):
    with open(frames_path, "rb") as f:
        assert f.readline() == b"P64GIF\n", "bad header"
        w, h, ox, oy, ow, oh = map(int, f.readline().split())
        n = int(f.readline())
        delays = [int(x) for x in f.readline().split()]
        data = f.read()
    canvas_bytes, scaled_bytes = w * h * 3, DST_W * DST_H * 3
    per_frame = canvas_bytes + scaled_bytes
    assert len(data) == n * per_frame, "truncated frame data"

    problems = []
    ref = list(pillow_frames(path))
    if len(ref) != n:
        problems.append(f"frame count {n} (firmware) vs {len(ref)} (Pillow)")
    for i in range(min(n, len(ref))):
        canvas = data[i * per_frame:i * per_frame + canvas_bytes]
        scaled = data[i * per_frame + canvas_bytes:(i + 1) * per_frame]
        ref_canvas, ref_delay = ref[i]
        d = first_diff(canvas, ref_canvas, w)
        if d:
            problems.append(f"frame {i}: canvas differs from Pillow at ({d[0]},{d[1]}): ours {d[2]} vs {d[3]}")
        if delays[i] != browser_delay(ref_delay):
            problems.append(f"frame {i}: delay {delays[i]} ms vs browser rule {browser_delay(ref_delay)} ms (stored {ref_delay})")
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
    ap.add_argument("gifs", nargs="*", help="GIF files (default: hardware-tests/assets/gifs/*.gif)")
    ap.add_argument("--keep", action="store_true", help="keep the dumped frames in tests/host/build/frames")
    ap.add_argument("--no-gifs", action="store_true", help="unit tests only")
    args = ap.parse_args()

    build_dir = os.path.join(HERE, "build")
    os.makedirs(build_dir, exist_ok=True)
    exe = build(build_dir)

    unit = subprocess.run([exe, "unit"], capture_output=True, text=True)
    print(unit.stdout, end="")
    if unit.returncode != 0:
        print(unit.stderr)
        print("UNIT TESTS FAILED")
        return 1
    if args.no_gifs:
        return 0

    gifs = args.gifs or sorted(glob.glob(os.path.join(REPO, "hardware-tests", "assets", "gifs", "*.gif")))
    if not gifs:
        print("no GIFs found")
        return 2
    out_dir = os.path.join(build_dir, "frames") if args.keep else tempfile.mkdtemp(prefix="p64gif-")
    os.makedirs(out_dir, exist_ok=True)
    try:
        run = subprocess.run([exe, "gifdump", str(DST_W), str(DST_H), out_dir, *gifs], capture_output=True, text=True)
        if run.returncode != 0:
            print(run.stdout)
            print(run.stderr)
            print("gifdump reported failures")
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
