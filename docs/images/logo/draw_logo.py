#!/usr/bin/env python3
"""Draws the p64 logo as a 64x64 pixel-art PNG, docs/images/logo/p64-logo-64.png.

It is a pixel-art translation of the creative direction in
creative-direction-2026-09-23.png (same folder): the device seen in
three-quarter view, a dark box with a rainbow screen, and a chunky lowercase
"p64" wordmark in white with a black outline under it. The canvas is the
panel's own size, so the file can be shown on the device unchanged.

Run from the repo root with the system Python (needs Pillow):

    python docs/images/logo/draw_logo.py                 # writes p64-logo-64.png
    python docs/images/logo/draw_logo.py --preview P.png # also an 8x nearest-neighbour copy

Every coordinate below is a pixel on the 64x64 canvas, x to the right and y
down. The decisions behind the picture (settled with the user on 2026-09-23):
lowercase wordmark, smooth gradient without an LED grid, three-quarter view,
transparent background.
"""
import argparse
import math
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE / "p64-logo-64.png"
SIZE = 64

# Palette.
CLEAR = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
BEZEL = (58, 58, 58, 255)  # front face around the screen
BEZEL_EDGE = (92, 92, 92, 255)  # 1 px highlight on the front face's top edge
SIDE = (30, 30, 30, 255)  # the left side face
SIDE_EDGE = (72, 72, 72, 255)  # its slanted top edge
WHITE = (250, 250, 250, 255)
SHADOW = (88, 88, 88, 255)  # 1 px drop shadow under the letters

# Device geometry. The front face is a 44x44 square: 1 px outline, 2 px bezel,
# 38x38 screen. The side face is a 6 px strip on the left, sheared so that its
# far edge sits 3 px lower than the front, as in the direction (the box is seen
# a little from below and from the left).
FRONT_X0, FRONT_Y0 = 18, 1
FRONT_W = FRONT_H = 44
SIDE_W = 6
SIDE_DROP = 3
SCREEN_INSET = 3  # outline + bezel

# Wordmark: three 11-wide glyphs with 3 px vertical strokes, digits 12 tall,
# the p with an 8-row bowl on the baseline and a 3-row descender. Rows are
# relative to the digits' top row; the p's bowl starts four rows lower, so it
# reads as lowercase. The gaps make the three glyphs span the front face.
GLYPHS = {
    "p": (
        4,
        [
            "XXXXXXXXX..",
            "XXXXXXXXXX.",
            "XXX.....XXX",
            "XXX.....XXX",
            "XXX.....XXX",
            "XXX.....XXX",
            "XXXXXXXXXX.",
            "XXXXXXXXX..",
            "XXX........",
            "XXX........",
            "XXX........",
        ],
    ),
    "6": (
        0,
        [
            "..XXXXXXX..",
            ".XXXXXXXXX.",
            "XXX.....XXX",
            "XXX........",
            "XXX........",
            "XXXXXXXXX..",
            "XXXXXXXXXX.",
            "XXX.....XXX",
            "XXX.....XXX",
            "XXX.....XXX",
            ".XXXXXXXXX.",
            "..XXXXXXX..",
        ],
    ),
    "4": (
        0,
        [
            ".......XXX.",
            "......XXXX.",
            ".....XXXXX.",
            "....XX.XXX.",
            "...XX..XXX.",
            "..XX...XXX.",
            ".XX....XXX.",
            "XXXXXXXXXXX",
            "XXXXXXXXXXX",
            ".......XXX.",
            ".......XXX.",
            ".......XXX.",
        ],
    ),
}
TEXT_X0, TEXT_Y0 = 19, 47  # top-left of the first glyph's cell (digit row 0)
GLYPH_W, GLYPH_GAP = 11, 3


def screen_colour(u: float, v: float) -> tuple[int, int, int, int]:
    """Colour of the screen at (u, v) in [-1, 1]^2, a hue wheel with a white centre.

    Clockwise from the top-left corner the direction goes green, yellow, red,
    magenta, blue, cyan: hue falls with the clockwise angle, offset so that the
    top-right corner is orange-red and the bottom is violet-blue.
    """
    angle = math.degrees(math.atan2(-v, u))  # y up
    hue = (angle - 15.0) % 360.0
    dist = math.hypot(u, v) / math.sqrt(2.0)  # 1 at the corners
    sat = min(1.0, 1.45 * dist)
    # HSV -> RGB with value 1.
    h6 = hue / 60.0
    i = int(h6) % 6
    f = h6 - int(h6)
    p, q, t = 1.0 - sat, 1.0 - sat * f, 1.0 - sat * (1.0 - f)
    r, g, b = [(1, t, p), (q, 1, p), (p, 1, t), (p, q, 1), (t, p, 1), (1, p, q)][i]
    return (round(255 * r), round(255 * g), round(255 * b), 255)


def draw_device(px) -> None:
    fx0, fy0, fx1, fy1 = FRONT_X0, FRONT_Y0, FRONT_X0 + FRONT_W - 1, FRONT_Y0 + FRONT_H - 1

    # Side face: for column x left of the front, the top and bottom edges drop
    # by SIDE_DROP over SIDE_W columns. Outline in black, fill in SIDE, a 1 px
    # SIDE_EDGE highlight just under the top outline.
    for k in range(SIDE_W):
        x = fx0 - 1 - k
        drop = round(SIDE_DROP * (k + 1) / SIDE_W)
        top, bottom = fy0 + drop, fy1 + drop
        for y in range(top, bottom + 1):
            px[x, y] = SIDE
        px[x, top] = BLACK
        px[x, top + 1] = SIDE_EDGE
        px[x, bottom] = BLACK
        if k == SIDE_W - 1:  # far edge
            for y in range(top, bottom + 1):
                px[x, y] = BLACK

    # Front face: outline, bezel, highlight, screen.
    for y in range(fy0, fy1 + 1):
        for x in range(fx0, fx1 + 1):
            px[x, y] = BEZEL
    for x in range(fx0, fx1 + 1):
        px[x, fy0] = BLACK
        px[x, fy1] = BLACK
        px[x, fy0 + 1] = BEZEL_EDGE
    for y in range(fy0, fy1 + 1):
        px[fx0, y] = BLACK
        px[fx1, y] = BLACK

    sx0, sy0 = fx0 + SCREEN_INSET, fy0 + SCREEN_INSET
    sw = FRONT_W - 2 * SCREEN_INSET
    for j in range(sw):
        for i in range(sw):
            u = (i + 0.5) / sw * 2.0 - 1.0
            v = (j + 0.5) / sw * 2.0 - 1.0
            px[sx0 + i, sy0 + j] = screen_colour(u, v)


def draw_wordmark(px) -> None:
    fill: set[tuple[int, int]] = set()
    x = TEXT_X0
    for ch in "p64":
        row_offset, rows = GLYPHS[ch]
        for r, row in enumerate(rows):
            for c, cell in enumerate(row):
                if cell == "X":
                    fill.add((x + c, TEXT_Y0 + row_offset + r))
        x += GLYPH_W + GLYPH_GAP

    outline = {(x + dx, y + dy) for (x, y) in fill for dx in (-1, 0, 1) for dy in (-1, 0, 1)} - fill
    body = fill | outline
    shadow = {(x + 1, y + 1) for (x, y) in body} - body

    def inside(p):
        return 0 <= p[0] < SIZE and 0 <= p[1] < SIZE

    for p in filter(inside, shadow):
        px[p] = SHADOW
    for p in filter(inside, outline):
        px[p] = BLACK
    for p in filter(inside, fill):
        px[p] = WHITE
    assert all(inside(p) for p in body), "wordmark leaves the canvas"


def render() -> Image.Image:
    img = Image.new("RGBA", (SIZE, SIZE), CLEAR)
    px = img.load()
    draw_device(px)
    draw_wordmark(px)
    return img


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--preview", type=Path, help="also write an 8x nearest-neighbour PNG here")
    args = ap.parse_args()
    img = render()
    img.save(args.out, optimize=True)
    print(f"wrote {args.out}")
    if args.preview:
        img.resize((SIZE * 8, SIZE * 8), Image.NEAREST).save(args.preview)
        print(f"wrote {args.preview}")


if __name__ == "__main__":
    main()
