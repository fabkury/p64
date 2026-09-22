#!/usr/bin/env python3
r"""Host tests for the ESP-IDF-free components (p64_gfx, p64_decode, p64_content, widgets, streams).

Builds tests/host/main.cpp with the component sources and the vendored libraries
(AnimatedGIF, the APNG-patched libpng, zlib from managed_components, libwebp) using the
PC's gcc/g++ (the exact decoder code the board runs), then:

  1. runs the unit tests (scaler, rotation, gains, sniffing, delay rule, frame queue);
  2. decodes every file of the corpora (tests/host/corpus/* made with Pillow, plus the
     hardware tests' GIF corpus) with the firmware's decoders + scaler and compares each
     frame of the first loop against Pillow: canvas pixel-exact (transparent pixels
     black), delays after the browser rule, and the scaled output against a Python twin
     of the scaling rule.

Usage: python tests/host/run.py [--keep] [--no-files] [--sanitize] [--werror]
                                [--junit FILE] [--tc PATTERN] [file ...]
Needs gcc/g++ on PATH, Pillow (the system Python has it; the ESP-IDF venv does not),
cJSON from ESP-IDF (IDF_PATH, default C:\esp\v5.5.4\esp-idf) and zlib from
managed_components (a prior firmware build or `idf.py reconfigure`). The unit tests are
doctest cases under tests/host/unit/; --tc runs the cases matching a pattern.
--sanitize builds with AddressSanitizer and UndefinedBehaviorSanitizer (not on Windows,
where MinGW lacks them) into a separate build folder; --werror makes warnings in p64
code errors (vendored code keeps its warnings); CI uses both.
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
COMPONENTS = os.path.join(FIRMWARE, "components")
ZLIB = os.path.join(FIRMWARE, "managed_components", "espressif__zlib", "zlib")
LIBPNG = os.path.join(COMPONENTS, "libpng")
LIBWEBP = os.path.join(COMPONENTS, "libwebp", "libwebp")
# cJSON comes from the ESP-IDF tree (the playset JSON code uses it on the device too).
IDF_PATH = os.environ.get("IDF_PATH", r"C:\esp\v5.5.4\esp-idf")
CJSON = os.path.join(IDF_PATH, "components", "json", "cJSON")
DST_W, DST_H = 64, 64

UNIT_SOURCES = sorted(glob.glob(os.path.join(HERE, "unit", "*.cpp")))
CXX_SOURCES = [
    os.path.join(HERE, "main.cpp"),
    *UNIT_SOURCES,
    os.path.join(COMPONENTS, "p64_gfx", "src", "frame.cpp"),
    os.path.join(COMPONENTS, "p64_gfx", "src", "scaler.cpp"),
    os.path.join(COMPONENTS, "p64_decode", "src", "format.cpp"),
    os.path.join(COMPONENTS, "p64_decode", "src", "gif_decoder.cpp"),
    os.path.join(COMPONENTS, "p64_decode", "src", "png_decoder.cpp"),
    os.path.join(COMPONENTS, "p64_decode", "src", "webp_decoder.cpp"),
    os.path.join(COMPONENTS, "p64_decode", "src", "bmp_decoder.cpp"),
    os.path.join(COMPONENTS, "animatedgif", "src", "AnimatedGIF.cpp"),
    os.path.join(COMPONENTS, "p64_content", "src", "playset.cpp"),
    os.path.join(COMPONENTS, "p64_content", "src", "playset_json.cpp"),
    os.path.join(COMPONENTS, "p64_content", "src", "scheduler.cpp"),
    os.path.join(COMPONENTS, "p64_content", "src", "history.cpp"),
    os.path.join(COMPONENTS, "p64_content", "src", "makapix_index.cpp"),
    os.path.join(COMPONENTS, "p64_gfx", "src", "text.cpp"),
    os.path.join(COMPONENTS, "p64_gfx", "src", "fonts.cpp"),
    os.path.join(COMPONENTS, "p64_gfx", "src", "fonts_data.cpp"),
    os.path.join(COMPONENTS, "p64_widgets", "src", "clock_format.cpp"),
    os.path.join(COMPONENTS, "p64_widgets", "src", "analogue.cpp"),
    os.path.join(COMPONENTS, "p64_widgets", "src", "weather_model.cpp"),
    os.path.join(COMPONENTS, "p64_widgets", "src", "weather_icons.cpp"),
    os.path.join(COMPONENTS, "p64_widgets", "src", "weather_icons_util.cpp"),
    os.path.join(COMPONENTS, "p64_stream", "src", "protocol.cpp"),
    os.path.join(COMPONENTS, "p64_system", "src", "night.cpp"),
    os.path.join(COMPONENTS, "p64_system", "src", "rtc_codec.cpp"),
    os.path.join(COMPONENTS, "p64_system", "src", "settings_model.cpp"),
    os.path.join(COMPONENTS, "p64_gfx", "src", "png_encode.cpp"),
    os.path.join(COMPONENTS, "p64_content", "src", "local_index.cpp"),
    os.path.join(COMPONENTS, "p64_playback", "src", "artwork.cpp"),
    os.path.join(COMPONENTS, "p64_net", "src", "tz.cpp"),
    os.path.join(FIRMWARE, "main", "status_screens.cpp"),
    os.path.join(FIRMWARE, "main", "boot_animation.cpp"),
    os.path.join(COMPONENTS, "p64_inputs", "src", "tap.cpp"),
    os.path.join(COMPONENTS, "p64_inputs", "src", "orientation.cpp"),
    os.path.join(COMPONENTS, "p64_ota", "src", "version.cpp"),
]
ZLIB_SOURCES = [os.path.join(ZLIB, f) for f in (
    "adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c",
    "deflate.c", "trees.c", "compress.c", "uncompr.c")]
LIBPNG_SOURCES = sorted(glob.glob(os.path.join(LIBPNG, "libpng", "png*.c")))
LIBWEBP_SOURCES = sorted(
    glob.glob(os.path.join(LIBWEBP, "src", "dec", "*.c"))
    + glob.glob(os.path.join(LIBWEBP, "src", "dsp", "*.c"))
    + glob.glob(os.path.join(LIBWEBP, "src", "utils", "*.c"))
    + glob.glob(os.path.join(LIBWEBP, "src", "demux", "*.c")))
C_SOURCES = ZLIB_SOURCES + LIBPNG_SOURCES + LIBWEBP_SOURCES + [os.path.join(CJSON, "cJSON.c")]
INCLUDES = [
    os.path.join(COMPONENTS, "p64_net", "include"),
    os.path.join(COMPONENTS, "p64_net", "src"),        # tz_table.inc
    os.path.join(FIRMWARE, "main"),                    # status_screens.hpp, boot_animation.hpp (only those)
    os.path.join(HERE, "third_party"),     # doctest.h
    os.path.join(HERE, "unit"),
    os.path.join(COMPONENTS, "p64_gfx", "include"),
    os.path.join(COMPONENTS, "p64_decode", "include"),
    os.path.join(COMPONENTS, "p64_playback", "include"),
    os.path.join(COMPONENTS, "p64_content", "include"),
    os.path.join(COMPONENTS, "p64_widgets", "src"),
    os.path.join(COMPONENTS, "p64_stream", "src"),
    os.path.join(COMPONENTS, "p64_system", "include"),
    os.path.join(COMPONENTS, "p64_inputs", "src"),
    os.path.join(COMPONENTS, "p64_ota", "src"),
    CJSON,
    os.path.join(COMPONENTS, "animatedgif", "src"),
    LIBPNG,                               # pnglibconf.h
    os.path.join(LIBPNG, "libpng"),
    ZLIB,
    os.path.join(LIBWEBP, "src"),
    LIBWEBP,
]
HEADER_DIRS = [
    os.path.join(COMPONENTS, "p64_net", "include"),
    os.path.join(COMPONENTS, "p64_net", "src"),        # tz_table.inc
    os.path.join(FIRMWARE, "main"),                    # status_screens.hpp, boot_animation.hpp (only those)
    os.path.join(HERE, "unit"),
    os.path.join(COMPONENTS, "p64_gfx", "include"),
    os.path.join(COMPONENTS, "p64_decode", "include"),
    os.path.join(COMPONENTS, "p64_playback", "include"),
    os.path.join(COMPONENTS, "p64_content", "include"),
    os.path.join(COMPONENTS, "p64_widgets", "src"),
    os.path.join(COMPONENTS, "p64_stream", "src"),
    os.path.join(COMPONENTS, "p64_system", "include"),
    os.path.join(COMPONENTS, "p64_inputs", "src"),
    os.path.join(COMPONENTS, "p64_ota", "src"),
]


def newest_mtime(paths):
    latest = 0.0
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for f in files:
                    latest = max(latest, os.path.getmtime(os.path.join(root, f)))
        elif os.path.exists(p):
            latest = max(latest, os.path.getmtime(p))
    return latest


SANITIZE = False
WERROR = False
VENDORED = (os.path.join(COMPONENTS, "animatedgif"), os.path.join(HERE, "third_party"))
VENDORED_INCLUDES = (os.path.join(COMPONENTS, "animatedgif"), os.path.join(HERE, "third_party"), LIBPNG, ZLIB,
                     os.path.join(COMPONENTS, "libwebp"), CJSON)


def sanitizer_flags(src=None):
    if not SANITIZE:
        return []
    flags = ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=undefined"]
    # AnimatedGIF reads 16-bit fields through casts on 64-bit hosts (its ALLOWS_UNALIGNED
    # path, x86-64 only; the ESP32-S3 build takes the byte-wise macros), which UBSan's
    # alignment check reports. Vendored code, host-only path: that one check is off there.
    if src is not None and src.startswith(VENDORED):
        flags.append("-fno-sanitize=alignment")
    return flags


def compile_object(src, obj, is_cxx, header_mtime):
    if os.path.exists(obj) and os.path.getmtime(obj) > max(os.path.getmtime(src), header_mtime):
        return
    # Vendored headers as system headers: their warnings are not ours to fix.
    inc = ["-isystem" + i if i.startswith(VENDORED_INCLUDES) else "-I" + i for i in INCLUDES]
    if is_cxx:
        warn = ["-Wall", "-Wextra"]
        if WERROR and not src.startswith(VENDORED):
            warn.append("-Werror")
        tests_dir = "-DP64_HOST_TESTS_DIR=\"" + HERE.replace("\\", "/") + "\""
        cmd = ["g++", "-std=c++20", "-O2", *warn, "-D__LINUX__", tests_dir, *sanitizer_flags(src), *inc, "-c", src, "-o", obj]
    else:
        cmd = ["gcc", "-O2", "-w", "-DHAVE_UNISTD_H", *sanitizer_flags(), *inc, "-c", src, "-o", obj]
    subprocess.run(cmd, check=True)


def build(build_dir):
    if not os.path.isdir(CJSON):
        sys.exit("cJSON not found under IDF_PATH (" + CJSON + ")")
    if not os.path.isdir(ZLIB):
        sys.exit("managed_components/espressif__zlib is missing: run a firmware build first (tools\\build.ps1)")
    exe = os.path.join(build_dir, "p64_hosttest.exe" if os.name == "nt" else "p64_hosttest")
    obj_dir = os.path.join(build_dir, "obj")
    os.makedirs(obj_dir, exist_ok=True)
    header_mtime = newest_mtime(HEADER_DIRS + [os.path.abspath(__file__)])
    objects = []
    for src in CXX_SOURCES + C_SOURCES:
        rel = os.path.relpath(src, FIRMWARE).replace(os.sep, "_").replace("..", "up")
        obj = os.path.join(obj_dir, rel + ".o")
        compile_object(src, obj, src.endswith(".cpp"), header_mtime if src in CXX_SOURCES else 0.0)
        objects.append(obj)
    if not os.path.exists(exe) or any(os.path.getmtime(o) > os.path.getmtime(exe) for o in objects):
        print("linking host tests...")
        # -static: a dynamically linked MinGW build picks up whichever libstdc++ DLL comes
        # first on PATH and crashes at load time when it is from another toolchain.
        # The sanitizers need the dynamic runtime (and are Linux/macOS only).
        link = sanitizer_flags() if SANITIZE else ["-static"]
        subprocess.run(["g++", *link, *objects, "-o", exe], check=True)
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


def apng_reference_frames(path):
    """APNG oracle: composites the raw sub-frames per the APNG specification.

    Pillow's own APNG reader pastes OVER-blended sub-frames with their alpha as a mask,
    which halves colour and alpha over transparent areas instead of a true OVER (what
    browsers and libpng-based players do), so it cannot be the reference here. This
    parser decodes each fcTL/fdAT sub-frame as a static PNG through Pillow and applies
    dispose_op and blend_op itself, with Image.alpha_composite for OVER.
    """
    import io
    import struct
    import zlib

    from PIL import Image

    d = open(path, "rb").read()
    pos = 8
    ihdr = None
    frames = []
    cur = None
    has_default_image = False
    while pos + 8 <= len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            ihdr = data
        elif typ == b"fcTL":
            _seq, w, h, x, y, dn, dd, dop, bop = struct.unpack(">IIIIIHHBB", data)
            cur = dict(w=w, h=h, x=x, y=y, dn=dn, dd=dd, dispose=dop, blend=bop, data=b"")
            frames.append(cur)
        elif typ == b"IDAT":
            if cur is None:
                has_default_image = True  # IDAT before any fcTL: hidden default image
            else:
                cur["data"] += data
        elif typ == b"fdAT":
            cur["data"] += data[4:]
        elif typ == b"IEND":
            break
    iw, ih, bd, ct = struct.unpack(">IIBB", ihdr[:10])
    interlace = ihdr[12]

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)

    # Ancillary chunks that affect decoding (PLTE, tRNS) must follow each sub-frame.
    extras = b""
    pos = 8
    while pos + 8 <= len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        if typ in (b"PLTE", b"tRNS", b"gAMA"):
            extras += d[pos:pos + 12 + ln]
        if typ == b"IDAT" or typ == b"fdAT":
            break
        pos += 12 + ln

    def sub_image(fr):
        hdr = struct.pack(">IIBBBBB", fr["w"], fr["h"], bd, ct, 0, 0, interlace)
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", hdr) + extras + chunk(b"IDAT", fr["data"]) + chunk(b"IEND", b"")
        return Image.open(io.BytesIO(png)).convert("RGBA")

    canvas = Image.new("RGBA", (iw, ih), (0, 0, 0, 0))
    prev_snapshot = None
    animated = len(frames) > 1
    for i, fr in enumerate(frames):
        if i == 0 and has_default_image and fr["data"] == b"":
            continue
        sub = sub_image(fr)
        region = (fr["x"], fr["y"], fr["x"] + fr["w"], fr["y"] + fr["h"])
        dispose = fr["dispose"]
        if i == 0 and dispose == 2:
            dispose = 1
        if dispose == 2:
            prev_snapshot = canvas.crop(region)
        if fr["blend"] == 0:
            canvas.paste(sub, region)
        else:
            canvas.paste(Image.alpha_composite(canvas.crop(region), sub), region)
        px = canvas.tobytes()
        out = bytearray(iw * ih * 3)
        for p in range(iw * ih):
            a = px[p * 4 + 3]
            if a == 255:
                out[p * 3:p * 3 + 3] = px[p * 4:p * 4 + 3]
            elif a:
                for c in range(3):
                    v = px[p * 4 + c] * a + 128
                    out[p * 3 + c] = (v + (v >> 8)) >> 8
        den = fr["dd"] or 100
        yield bytes(out), (fr["dn"] * 1000) // den, animated
        if dispose == 1:
            canvas.paste(Image.new("RGBA", (fr["w"], fr["h"]), (0, 0, 0, 0)), region)
        elif dispose == 2 and prev_snapshot is not None:
            canvas.paste(prev_snapshot, region)


def pillow_frames(path):
    """Yields (rgb bytes composited over black, stored delay ms, animated) per frame."""
    from PIL import Image

    with open(path, "rb") as f:
        head = f.read(64 * 1024)
    if head.startswith(b"\x89PNG") and b"acTL" in head:
        yield from apng_reference_frames(path)
        return
    im = Image.open(path)
    w, h = im.size
    animated = getattr(im, "is_animated", False)
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
            a = px[p * 4 + 3]
            if a == 255:
                out[p * 3:p * 3 + 3] = px[p * 4:p * 4 + 3]
            elif a:
                inv = 255 - a
                for c in range(3):
                    out[p * 3 + c] = (px[p * 4 + c] * a + 0 * inv + 128 + ((px[p * 4 + c] * a + 128) >> 8)) >> 8
        yield bytes(out), im.info.get("duration", 0), animated
        i += 1


def first_diff(a, b, w, tolerance=0):
    worst = 0
    where = None
    for i in range(0, min(len(a), len(b)), 3):
        d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
        if d > worst:
            worst = d
            p = i // 3
            where = (p % w, p // w, tuple(a[i:i + 3]), tuple(b[i:i + 3]))
    if worst > tolerance:
        return where + (worst,)
    return None


def browser_delay(fmt, stored):
    if fmt == "GIF":
        return 100 if stored <= 10 else stored
    if fmt in ("APNG", "WebP"):
        return 100 if stored == 0 else stored
    return stored


def check(path, frames_path):
    with open(frames_path, "rb") as f:
        assert f.readline() == b"P64FRM\n", "bad header"
        fields = f.readline().split()
        fmt = fields[0].decode()
        w, h, ox, oy, ow, oh, animated, has_alpha = map(int, fields[1:])
        n = int(f.readline())
        delays = [int(x) for x in f.readline().split()]
        data = f.read()
    canvas_bytes, scaled_bytes = w * h * 3, DST_W * DST_H * 3
    per_frame = canvas_bytes + scaled_bytes
    assert len(data) == n * per_frame, "truncated frame data"
    # Partial alpha on lossy content may differ by rounding between compositors.
    tolerance = 1 if fmt in ("WebP", "APNG") else 0

    problems = []
    ref = list(pillow_frames(path))
    if len(ref) != n:
        problems.append(f"frame count {n} (firmware) vs {len(ref)} (Pillow)")
    for i in range(min(n, len(ref))):
        canvas = data[i * per_frame:i * per_frame + canvas_bytes]
        scaled = data[i * per_frame + canvas_bytes:(i + 1) * per_frame]
        ref_canvas, ref_delay, ref_animated = ref[i]
        d = first_diff(canvas, ref_canvas, w, tolerance)
        if d:
            problems.append(f"frame {i}: canvas differs from Pillow at ({d[0]},{d[1]}): ours {d[2]} vs {d[3]} (max diff {d[4]})")
        if ref_animated and delays[i] != browser_delay(fmt, ref_delay):
            problems.append(f"frame {i}: delay {delays[i]} ms vs browser rule {browser_delay(fmt, ref_delay)} ms (stored {ref_delay})")
        eox, eoy, eow, eoh, expected = expected_scaled(canvas, w, h)
        if (eox, eoy, eow, eoh) != (ox, oy, ow, oh):
            problems.append(f"placement {(ox, oy, ow, oh)} vs expected {(eox, eoy, eow, eoh)}")
        d = first_diff(scaled, expected, DST_W)
        if d:
            problems.append(f"frame {i}: scaled output differs at ({d[0]},{d[1]}): ours {d[2]} vs {d[3]}")
        if len(problems) > 5:
            problems.append("...")
            break
    return fmt, n, problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="artwork files (default: both corpora)")
    ap.add_argument("--keep", action="store_true", help="keep the dumped frames in tests/host/build/frames")
    ap.add_argument("--no-files", action="store_true", help="unit tests only")
    ap.add_argument("--sanitize", action="store_true", help="ASan + UBSan build (Linux/macOS)")
    ap.add_argument("--werror", action="store_true", help="warnings in p64 code are errors")
    ap.add_argument("--junit", help="write the unit test results as JUnit XML to this file")
    ap.add_argument("--tc", help="run only the unit test cases matching this doctest pattern")
    args = ap.parse_args()
    global SANITIZE, WERROR
    SANITIZE, WERROR = args.sanitize, args.werror
    if SANITIZE and os.name == "nt":
        sys.exit("--sanitize needs Linux or macOS (MinGW has no ASan/UBSan)")

    build_dir = os.path.join(HERE, "build-san" if SANITIZE else ("build-werror" if WERROR else "build"))
    os.makedirs(build_dir, exist_ok=True)
    exe = build(build_dir)

    unit_args = [exe]
    if args.tc:
        unit_args.append("-tc=" + args.tc)
    unit = subprocess.run(unit_args, capture_output=True, text=True)
    if args.junit:
        with open(args.junit, "w", encoding="utf-8") as f:
            f.write(subprocess.run(unit_args + ["-r=junit"], capture_output=True, text=True).stdout)
    print(unit.stdout, end="")
    if unit.returncode != 0:
        print(unit.stderr)
        print("UNIT TESTS FAILED")
        return 1
    if args.no_files:
        return 0

    files = args.files or (
        sorted(glob.glob(os.path.join(HERE, "corpus", "*.*")))
        + sorted(glob.glob(os.path.join(REPO, "hardware-tests", "assets", "gifs", "*.gif"))))
    if not files:
        print("no files found")
        return 2
    out_dir = os.path.join(build_dir, "frames") if args.keep else tempfile.mkdtemp(prefix="p64frm-")
    os.makedirs(out_dir, exist_ok=True)
    try:
        run = subprocess.run([exe, "dump", str(DST_W), str(DST_H), out_dir, *files], capture_output=True, text=True)
        if run.returncode != 0:
            print(run.stdout)
            print(run.stderr)
            print("dump reported failures")
        total_frames = 0
        bad = 0
        by_format = {}
        for path in files:
            frames_path = os.path.join(out_dir, os.path.basename(path) + ".frames")
            if not os.path.exists(frames_path):
                print(f"FAIL {os.path.basename(path)}: no output (decode failed)")
                bad += 1
                continue
            fmt, n, problems = check(path, frames_path)
            total_frames += n
            by_format[fmt] = by_format.get(fmt, 0) + 1
            if problems:
                bad += 1
                print(f"FAIL {os.path.basename(path)} ({fmt}, {n} frames)")
                for p in problems:
                    print("     " + p)
            else:
                print(f"ok   {os.path.basename(path)} ({fmt}, {n} frames)")
        summary = ", ".join(f"{v} {k}" for k, v in sorted(by_format.items()))
        print(f"\n{len(files)} files ({summary}), {total_frames} frames, {bad} with problems")
        return 1 if bad or run.returncode else 0
    finally:
        if not args.keep:
            shutil.rmtree(out_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
