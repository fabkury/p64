#!/usr/bin/env python3
"""Bakes what the panel shows into a 64 x 64 PNG per video frame (build/leds/led_NNNN.png).

    python bake_leds.py            (from docs/video/; needs Pillow and numpy)

Follows storyboard.PANEL: corpus GIFs from firmware/tests/host/corpus/gifs (played at their
own frame timings, looping, small canvases scaled up by whole numbers like the firmware
does), the digital clock, the analogue clock, the weather widget (with the firmware's own
pixel fonts from firmware/assets/fonts and its icons from firmware/assets/weather) and a
plasma standing in for a live DDP stream. Blender maps the sequence onto the LED face.
"""

import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import storyboard as sb

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GIFS = os.path.join(ROOT, "firmware", "tests", "host", "corpus", "gifs")
FONTS = os.path.join(ROOT, "firmware", "assets", "fonts")
WEATHER = os.path.join(ROOT, "firmware", "assets", "weather")
OUT = os.path.join(HERE, "build", "leds")
W = 64


def font(name, size):
    paths = {
        "ample": "everyday-ample/Everyday_Ample.ttf",
        "typical": "everyday-typical/Everyday_Typical.ttf",
        "standard": "everyday-standard/Everyday_Standard.ttf",
        "slight": "everyday-slight/Everyday_Slight.ttf",
    }
    return ImageFont.truetype(os.path.join(FONTS, paths[name]), size)


def text(im, xy, s, f, fill, scale=1, anchor="la"):
    """Draws pixel text without anti-aliasing; scale draws it n times larger, still crisp."""
    if scale == 1:
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text(xy, s, font=f, fill=fill, anchor=anchor)
        return
    tmp = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(tmp)
    d.fontmode = "1"
    d.text((xy[0] / scale, xy[1] / scale), s, font=f, fill=fill, anchor=anchor)
    big = tmp.resize((W * scale, W * scale), Image.NEAREST)
    im.alpha_composite(big.crop((0, 0, W, W)))


class Gif:
    def __init__(self, name):
        im = Image.open(os.path.join(GIFS, name))
        self.frames, self.times = [], []
        t = 0.0
        for i in range(getattr(im, "n_frames", 1)):
            im.seek(i)
            fr = im.convert("RGBA")
            k = max(1, W // max(fr.size))          # whole-number scale-up, like the firmware
            fr = fr.resize((fr.width * k, fr.height * k), Image.NEAREST)
            canvas = Image.new("RGBA", (W, W), (0, 0, 0, 255))
            canvas.alpha_composite(fr, ((W - fr.width) // 2, (W - fr.height) // 2))
            self.frames.append(canvas)
            self.times.append(t)
            t += max(im.info.get("duration", 100), 20) / 1000.0
        self.period = t

    def at(self, t):
        t = t % self.period
        i = max(j for j, t0 in enumerate(self.times) if t0 <= t + 1e-9)
        return self.frames[i]


def clock(t):
    im = Image.new("RGBA", (W, W), (0, 0, 0, 255))
    colon = ":" if (t % 1.0) < 0.5 else " "
    text(im, (32, 14), f"21{colon}47", font("typical", 7), (255, 255, 255, 255), scale=2, anchor="mm")
    text(im, (32, 36), "Thu 24 Sep", font("standard", 6), (150, 160, 190, 255), anchor="mm")
    f6 = font("standard", 6)
    s = "23.4 C  41%"
    x0 = 32 - ImageDraw.Draw(im).textlength(s, font=f6) / 2
    text(im, (32, 50), s, f6, (255, 170, 60, 255), anchor="mm")
    degree(im, (round(x0 + ImageDraw.Draw(im).textlength("23.4 ", font=f6)) - 1, 47), (255, 170, 60, 255))
    return im


def degree(im, xy, fill):
    """The pixel fonts have no degree sign: a 2 x 2 ring of single pixels."""
    d = ImageDraw.Draw(im)
    x, y = xy
    d.point([(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)], fill=fill)


def analogue(t):
    im = Image.new("RGBA", (W, W), (0, 0, 0, 255))
    d = ImageDraw.Draw(im)
    c = 31.5
    for k in range(12):
        a = k * math.pi / 6
        for r0 in ((28, 29, 30) if k % 3 == 0 else (30,)):
            d.point((round(c + r0 * math.sin(a)), round(c - r0 * math.cos(a))),
                    fill=(255, 255, 255, 255) if k % 3 == 0 else (150, 150, 170, 255))
    sec = (47 * 60 + t * 6) % 60                       # 21:47:xx, seconds ticking from the segment start
    hm = 21 % 12 + 47 / 60
    hand = lambda frac, r, col, w: d.line((c, c, c + r * math.sin(frac * 2 * math.pi), c - r * math.cos(frac * 2 * math.pi)),
                                         fill=col, width=w)
    hand(hm / 12, 16, (255, 255, 255, 255), 2)
    hand(47 / 60, 24, (255, 255, 255, 255), 2)
    hand(math.floor(sec) / 60, 26, (255, 60, 60, 255), 1)
    d.point((c, c), fill=(255, 60, 60, 255))
    return im


def weather(t):
    im = Image.new("RGBA", (W, W), (0, 0, 0, 255))
    icon = Image.open(os.path.join(WEATHER, "partly_day_24.png")).convert("RGBA")
    im.alpha_composite(icon, (4, 6))
    text(im, (56, 18), "23", font("ample", 9), (255, 255, 255, 255), scale=2, anchor="rm")
    degree(im, (58, 9), (255, 255, 255, 255))
    text(im, (32, 42), "Partly cloudy", font("slight", 5), (200, 205, 220, 255), anchor="mm")
    text(im, (32, 54), "H 26   L 17", font("standard", 6), (150, 160, 190, 255), anchor="mm")
    return im


def stream(t):
    y, x = np.mgrid[0:W, 0:W].astype(np.float32) / W
    v = (np.sin(x * 6 + t * 2.0) + np.sin((y * 5 + x * 2) - t * 1.6)
         + np.sin(np.hypot(x - 0.5 + 0.3 * math.sin(t), y - 0.5 + 0.3 * math.cos(t * 0.7)) * 12 - t * 3)) / 3
    r = (np.sin(v * math.pi) * 0.5 + 0.5) * 255
    g = (np.sin(v * math.pi + 2.1) * 0.5 + 0.5) * 255
    b = (np.sin(v * math.pi + 4.2) * 0.5 + 0.5) * 255
    a = np.full_like(r, 255)
    return Image.fromarray(np.stack([r, g, b, a], -1).astype(np.uint8), "RGBA")


def write_dot_mask(path, n=W, px=2048, fill=0.78):
    """A grid of n x n rounded dots (the LED apertures): white where an LED is, black between."""
    im = Image.new("L", (px, px), 0)
    d = ImageDraw.Draw(im)
    cell = px / n
    r = cell * fill / 2
    for j in range(n):
        for i in range(n):
            cx, cy = (i + 0.5) * cell, (j + 0.5) * cell
            d.rounded_rectangle((cx - r, cy - r, cx + r, cy + r), radius=r * 0.35, fill=255)
    im.save(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    write_dot_mask(os.path.join(HERE, "build", "led_mask.png"))
    gifs = {}
    for f in range(1, sb.FRAMES + 1):
        t = (f - 1) / sb.FPS
        seg = next(s for s in sb.PANEL if s[0] <= t < s[1])
        t0, _, src = seg
        if src.startswith("gif:"):
            name = src[4:]
            gifs.setdefault(name, Gif(name))
            im = gifs[name].at(t - t0)
        elif src == "clock":
            im = clock(t - t0)
        elif src == "analogue":
            im = analogue(t - t0)
        elif src == "weather":
            im = weather(t - t0)
        elif src == "stream":
            im = stream(t - t0)
        else:
            im = Image.new("RGBA", (W, W), (0, 0, 0, 255))
        im.convert("RGB").save(os.path.join(OUT, f"led_{f:04d}.png"))
    print(f"wrote {sb.FRAMES} frames to {OUT}")


if __name__ == "__main__":
    main()
