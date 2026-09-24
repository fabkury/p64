#!/usr/bin/env python3
"""Composes the rendered frames into the video: LED bloom, fades, captions, the end card.

    python compose.py [--out ../p64b-concept.mp4] [--step N] [--frames a,b,c]   (from docs/video/; Pillow, numpy, ffmpeg)

Reads build/frames/f_NNNN.png (from scene.py), writes build/comp/c_NNNN.png and encodes
them with ffmpeg (H.264, yuv420p, 30 fps). --frames composes only those frames, for a
look at the captions without a full pass; --step N takes every Nth frame (rendered with
the same step) into a preview at FPS / N. Times come from storyboard.py.
"""

import argparse
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

import storyboard as sb

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FRAMES = os.path.join(HERE, "build", "frames")
COMP = os.path.join(HERE, "build", "comp")
LOGO = os.path.join(ROOT, "docs", "images", "logo", "p64-logo.png")
FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
W, H = sb.WIDTH, sb.HEIGHT


def font(bold, size):
    for name in (("segoeuib.ttf" if bold else "segoeui.ttf"), ("arialbd.ttf" if bold else "arial.ttf")):
        p = os.path.join(FONT_DIR, name)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default(size=size)


F_HEAD, F_SUB, F_TITLE, F_SMALL = font(True, 44), font(False, 30), font(True, 96), font(False, 26)


def ramp(t, t0, t1, fade):
    """0 outside [t0, t1], 1 inside, linear fades of `fade` seconds at both ends."""
    if t < t0 or t > t1:
        return 0.0
    return max(0.0, min(1.0, (t - t0) / fade, (t1 - t) / fade))


def bloom(im, threshold=0.72, radius=18, gain=0.5):
    """Adds a soft glow around the bright LEDs: threshold, blur at quarter size, add."""
    a = np.asarray(im).astype(np.float32) / 255.0
    lum = a.max(axis=2)
    bright = a * np.clip((lum - threshold) / (1 - threshold), 0, 1)[..., None]
    small = Image.fromarray((bright * 255).astype(np.uint8)).resize((W // 4, H // 4), Image.BILINEAR)
    small = small.filter(ImageFilter.GaussianBlur(radius / 4))
    glow = np.asarray(small.resize((W, H), Image.BILINEAR)).astype(np.float32) / 255.0
    out = np.clip(a + glow * gain, 0, 1)
    return Image.fromarray((out * 255).astype(np.uint8))


def caption(im, t):
    for t0, t1, head, sub in sb.CAPTIONS:
        k = ramp(t, t0, t1, sb.CAPTION_FADE)
        if k <= 0:
            continue
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        # a soft dark band so the text reads over the floor's highlights
        band = Image.new("L", (W, 200), 0)
        bd = ImageDraw.Draw(band)
        for y in range(200):
            bd.line((0, y, W, y), fill=int(140 * min(1, y / 110)))
        layer.paste((0, 0, 0, 255), (0, H - 200), band)
        y = H - 92 + int((1 - k) * 18)
        d.text((W // 2, y), head, font=F_HEAD, fill=(255, 255, 255, 255), anchor="ms")
        d.text((W // 2, y + 46), sub, font=F_SUB, fill=(190, 196, 210, 255), anchor="ms")
        alpha = layer.split()[3].point(lambda v: int(v * k))
        layer.putalpha(alpha)
        im.alpha_composite(layer)


def end_card(t):
    k = ramp(t, sb.END_CARD[0], sb.END_CARD[1] + 1, 0.5)
    im = Image.new("RGBA", (W, H), (6, 6, 8, 255))
    logo = Image.open(LOGO).convert("RGBA")
    s = 9
    logo = logo.resize((logo.width * s, logo.height * s), Image.NEAREST)
    im.alpha_composite(logo, ((W - logo.width) // 2, H // 2 - logo.height - 40))
    d = ImageDraw.Draw(im)
    d.text((W // 2, H // 2 + 70), "github.com/fabkury/p64", font=F_HEAD, fill=(255, 255, 255, 255), anchor="ms")
    d.text((W // 2, H // 2 + 120), "Firmware, enclosure and build guide · Apache-2.0", font=F_SUB, fill=(190, 196, 210, 255), anchor="ms")
    d.text((W // 2, H // 2 + 165), "Waveshare ESP32-S3-RGB-Matrix + RGB-Matrix-P2-64x64 · engineering concept, v7b enclosure",
           font=F_SMALL, fill=(120, 126, 140, 255), anchor="ms")
    a = np.asarray(im).astype(np.float32)
    a[..., :3] *= k
    return Image.fromarray(a.astype(np.uint8))


def compose_frame(f):
    t = (f - 1) / sb.FPS
    if f > sb.LAST_RENDER_FRAME:
        return end_card(t).convert("RGB")
    im = Image.open(os.path.join(FRAMES, f"f_{f:04d}.png")).convert("RGB")
    im = bloom(im).convert("RGBA")
    caption(im, t)
    dim = 1.0
    if t < sb.FADE_IN[1]:
        dim = ramp(t, sb.FADE_IN[0], 1e9, sb.FADE_IN[1] - sb.FADE_IN[0])
    if t >= sb.FADE_OUT[0]:
        dim = 1.0 - min(1.0, (t - sb.FADE_OUT[0]) / (sb.FADE_OUT[1] - sb.FADE_OUT[0]))
    if dim < 1.0:
        a = np.asarray(im.convert("RGB")).astype(np.float32) * dim
        return Image.fromarray(a.astype(np.uint8))
    return im.convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "..", "p64b-concept.mp4"))
    ap.add_argument("--frames", default=None)
    ap.add_argument("--step", type=int, default=1)
    a = ap.parse_args()
    os.makedirs(COMP, exist_ok=True)
    if a.frames:
        frames = [int(x) for x in a.frames.split(",")]
        for f in frames:
            compose_frame(f).save(os.path.join(COMP, f"c_{f:04d}.png"))
        print("composed", frames)
        return
    for k, f in enumerate(range(1, sb.FRAMES + 1, a.step), start=1):
        compose_frame(f).save(os.path.join(COMP, f"c_{k:04d}.png"))    # numbered without gaps for ffmpeg
    out = os.path.abspath(a.out)
    subprocess.run(["ffmpeg", "-y", "-framerate", str(sb.FPS / a.step), "-i", os.path.join(COMP, "c_%04d.png"),
                    "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p",
                    "-movflags", "+faststart", out], check=True)
    print("wrote", out)


if __name__ == "__main__":
    main()
