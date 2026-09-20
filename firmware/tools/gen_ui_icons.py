#!/usr/bin/env python3
r"""Draw the web UI's PWA icons: a 16x16 pixel-art tile ("p64" in a 3x5 pixel font over a
spectrum stripe on the deep-violet canvas) scaled crisply to 192 and 512 px.

    python tools\gen_ui_icons.py      # writes components\p64_web\ui\static\icon-192.png and icon-512.png
"""

import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, "..", "components", "p64_web", "ui", "static"))

BG = (14, 10, 26)
INK = (237, 233, 245)
STRIPE = [(255, 77, 77), (255, 138, 51), (255, 197, 51), (66, 216, 110), (43, 214, 214), (51, 166, 255), (139, 92, 255), (232, 77, 214)]

# A 3x5 pixel font for the three glyphs.
GLYPHS = {
    "p": ["###", "#.#", "###", "#..", "#.."],
    "6": ["###", "#..", "###", "#.#", "###"],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
}


def tile():
    im = Image.new("RGB", (16, 16), BG)
    px = im.load()
    for x in range(16):  # the spectrum stripe across the top rows
        c = STRIPE[x * len(STRIPE) // 16]
        px[x, 1] = c
        px[x, 2] = c
    x0, y0 = 2, 6
    for glyph in "p64":
        rows = GLYPHS[glyph]
        for dy, row in enumerate(rows):
            for dx, ch in enumerate(row):
                if ch == "#":
                    px[x0 + dx, y0 + dy] = INK
        x0 += 4
    for x in range(16):  # a faint baseline stripe at the bottom
        c = STRIPE[(15 - x) * len(STRIPE) // 16]
        px[x, 13] = tuple(v // 3 for v in c)
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    t = tile()
    for size in (192, 512):
        t.resize((size, size), Image.NEAREST).save(os.path.join(OUT, "icon-%d.png" % size), optimize=True)
        print("icon-%d.png" % size)


if __name__ == "__main__":
    main()
