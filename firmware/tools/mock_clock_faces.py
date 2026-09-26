#!/usr/bin/env python3
r"""Draws the candidate clock faces (prompt p043) as 64x64 mock-ups for review.

    python tools\mock_clock_faces.py        (from firmware/; needs Pillow)

Every candidate is built the way the firmware would build it: bitmap assets (drawn here
as PNGs into docs/design/clock-candidates/assets/<face>/) stamped onto a 64x64 frame, and
text from the bundled pixel fonts at their native sizes with integer scaling and no
anti-aliasing (the same rasterisation as tools/gen_fonts.py), so nothing in a mock-up is
beyond what the device can draw. Output: docs/design/clock-candidates/<face>.png (64x64),
<face>@8x.png (nearest-neighbour, for looking at), a contact sheet, and for the faces that
change with the time of day a strip of moments.
"""

import math
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(FIRMWARE, "docs", "design", "clock-candidates")
ASSETS = os.path.join(OUT, "assets")
FONT_FILES = {
    "capital-hill": ("assets/fonts/capital-hill/Capital_Hill.ttf", 6),
    "everyday-slight": ("assets/fonts/everyday-slight/Everyday_Slight.ttf", 5),
    "everyday-standard": ("assets/fonts/everyday-standard/Everyday_Standard.ttf", 6),
    "everyday-typical": ("assets/fonts/everyday-typical/Everyday_Typical.ttf", 7),
    "everyday-ample": ("assets/fonts/everyday-ample/Everyday_Ample.ttf", 9),
    "high-birth": ("assets/fonts/high-birth/High_Birth.ttf", 9),
}
W = H = 64

# ---------------------------------------------------------------- text helpers


def load_font(name):
    path, size = FONT_FILES[name]
    return ImageFont.truetype(os.path.join(FIRMWARE, path), size)


def text_mask(font, text):
    """The string as a tight 1-bit mask, glyph by glyph with the font's advances (no kerning),
    thresholded like gen_fonts.py."""
    img = Image.new("L", (256, 32), 0)
    d = ImageDraw.Draw(img)
    x = 8
    for ch in text:
        d.text((x, 8), ch, font=font, fill=255)
        x += int(round(font.getlength(ch)))
    img = img.point(lambda v: 255 if v >= 128 else 0)
    box = img.getbbox()
    return img.crop(box) if box else Image.new("L", (1, 1), 0)


def scale_mask(mask, s):
    return mask if s == 1 else mask.resize((mask.width * s, mask.height * s), Image.NEAREST)


def dilate(mask, eight=True):
    out = mask.copy()
    px = mask.load()
    op = out.load()
    for y in range(mask.height):
        for x in range(mask.width):
            if px[x, y]:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if not eight and dx and dy:
                            continue
                        if 0 <= x + dx < mask.width and 0 <= y + dy < mask.height:
                            op[x + dx, y + dy] = 255
    return out


def pad(mask, n):
    out = Image.new("L", (mask.width + 2 * n, mask.height + 2 * n), 0)
    out.paste(mask, (n, n))
    return out


def stamp(frame, mask, x, y, colour):
    """Paints `colour` where `mask` is set, top-left at (x, y)."""
    layer = Image.new("RGB", mask.size, colour)
    frame.paste(layer, (x, y), mask)


def draw_text(frame, font, x, y, text, colour, scale=1, outline=None):
    m = scale_mask(text_mask(font, text), scale)
    if outline is not None:
        stamp(frame, dilate(pad(m, 1)), x - 1, y - 1, outline)
    stamp(frame, m, x, y, colour)
    return m.width


def text_width(font, text, scale=1):
    return text_mask(font, text).width * scale


def draw_centred(frame, font, y, text, colour, scale=1, outline=None, cx=W // 2):
    w = text_width(font, text, scale)
    return draw_text(frame, font, cx - w // 2, y, text, colour, scale, outline)


def blit(frame, sprite, x, y):
    frame.paste(sprite, (x, y), sprite)


def save_asset(face, name, img):
    d = os.path.join(ASSETS, face)
    os.makedirs(d, exist_ok=True)
    img.save(os.path.join(d, name + ".png"))
    return img


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))



def bitmap(rows):
    """An ASCII-art glyph ('#' = ink) as a 1-bit mask."""
    m = Image.new("L", (len(rows[0]), len(rows)), 0)
    px = m.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "#":
                px[x, y] = 255
    return m


def digit_sheet(face, name, glyphs):
    """Saves the ten digits side by side (1 px apart) as one asset and returns the masks."""
    masks = [bitmap(g) for g in glyphs]
    w, h = masks[0].width, masks[0].height
    sheet = Image.new("RGBA", (10 * (w + 1) - 1, h), (0, 0, 0, 0))
    for i, m in enumerate(masks):
        sheet.paste(Image.new("RGBA", m.size, (255, 255, 255, 255)), (i * (w + 1), 0), m)
    save_asset(face, name, sheet)
    return masks


# Flip numerals, 9x16, Helvetica-bold proportions: 3 px stems, 2 px bars, rounded bowls.
FLIP_DIGITS = [
    ["..#####..", ".##...##.", "###...###", "###...###", "###...###", "###...###", "###...###", "###...###",
     "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", ".##...##.", "..#####.."],
    ["....###..", "...####..", "..#####..", ".##.###..", "....###..", "....###..", "....###..", "....###..",
     "....###..", "....###..", "....###..", "....###..", "....###..", "....###..", "....###..", "....###.."],
    ["..#####..", ".##...##.", "###...###", "###...###", "......###", "......###", ".....###.", "....###..",
     "...###...", "..###....", ".###.....", "###......", "###......", "###......", "#########", "#########"],
    ["..#####..", ".##...##.", "###...###", "......###", "......###", "......##.", "...####..", "...#####.",
     "......##.", "......###", "......###", "###...###", "###...###", "###...###", ".##...##.", "..#####.."],
    [".....###.", "....####.", "....####.", "...#####.", "...#####.", "..##.###.", "..##.###.", ".##..###.",
     ".##..###.", "##...###.", "#########", "#########", "#########", ".....###.", ".....###.", ".....###."],
    ["#########", "#########", "###......", "###......", "###......", "###......", "#######..", "########.",
     "##....###", "......###", "......###", "......###", "###...###", "###...###", ".##...##.", "..#####.."],
    ["..#####..", ".##...##.", "###...###", "###......", "###......", "###......", "###.###..", "########.",
     "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", ".##...##.", "..#####.."],
    ["#########", "#########", "......###", "......###", ".....###.", ".....###.", "....###..", "....###..",
     "....###..", "...###...", "...###...", "...###...", "...###...", "...###...", "...###...", "...###..."],
    ["..#####..", ".##...##.", "###...###", "###...###", "###...###", ".##...##.", "..#####..", ".##...##.",
     "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", ".##...##.", "..#####.."],
    ["..#####..", ".##...##.", "###...###", "###...###", "###...###", "###...###", "###...###", ".########",
     "..###.###", "......###", "......###", "......###", "###...###", "###...###", ".##...##.", "..#####.."],
]

# Nixie numerals, 7x11, one-pixel wire like the cathodes of a real tube.
NIXIE_DIGITS = [
    [".#####.", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", ".#####."],
    ["...#...", "..##...", ".#.#...", "...#...", "...#...", "...#...", "...#...", "...#...", "...#...", "...#...", "...#..."],
    [".#####.", "#.....#", "......#", "......#", ".....#.", "....#..", "...#...", "..#....", ".#.....", "#......", "#######"],
    [".#####.", "#.....#", "......#", "......#", ".....#.", "..####.", ".....#.", "......#", "......#", "#.....#", ".#####."],
    [".....#.", "....##.", "...#.#.", "..#..#.", ".#...#.", "#....#.", "#######", ".....#.", ".....#.", ".....#.", ".....#."],
    ["#######", "#......", "#......", "#......", "######.", "......#", "......#", "......#", "......#", "#.....#", ".#####."],
    ["..####.", ".#.....", "#......", "#......", "######.", "##....#", "#.....#", "#.....#", "#.....#", "#.....#", ".#####."],
    ["#######", "......#", "......#", ".....#.", ".....#.", "....#..", "....#..", "...#...", "...#...", "...#...", "...#..."],
    [".#####.", "#.....#", "#.....#", "#.....#", ".#####.", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", ".#####."],
    [".#####.", "#.....#", "#.....#", "#.....#", "#....##", ".######", "......#", "......#", "......#", ".....#.", ".####.."],
]

# ---------------------------------------------------------------- 1. Flip


def flip_assets():
    """A split-flap tile: 28x34 card in two leaves with a hinge line, rounded corners, a top
    highlight and axle pins; drawn as one sprite the firmware would stamp twice."""
    tw, th = 28, 34
    tile = Image.new("RGBA", (tw + 2, th), (0, 0, 0, 0))  # 1 px of pin on either side
    d = ImageDraw.Draw(tile)
    upper, lower = (58, 58, 68, 255), (44, 44, 52, 255)
    edge, hinge, pin = (30, 30, 36, 255), (10, 10, 12, 255), (110, 110, 120, 255)
    d.rectangle((1, 0, tw, th // 2 - 1), fill=upper)
    d.rectangle((1, th // 2, tw, th - 1), fill=lower)
    # rounded corners: knock the corner pixel out, darken its neighbours
    for cx, cy in ((1, 0), (tw, 0), (1, th - 1), (tw, th - 1)):
        tile.putpixel((cx, cy), (0, 0, 0, 0))
    for x, y in ((2, 0), (1, 1), (tw - 1, 0), (tw, 1), (2, th - 1), (1, th - 2), (tw - 1, th - 1), (tw, th - 2)):
        tile.putpixel((x, y), edge)
    # top highlight and bottom shadow line
    d.line((3, 0, tw - 2, 0), fill=(92, 92, 104, 255))
    d.line((2, th - 1, tw - 1, th - 1), fill=edge)
    # the hinge line through the middle, with its shadow under it
    d.line((1, th // 2 - 1, tw, th // 2 - 1), fill=hinge)
    d.line((1, th // 2, tw, th // 2), fill=(36, 36, 42, 255))
    # axle pins outside the card, 2 px tall on the hinge
    for x in (0, tw + 1):
        tile.putpixel((x, th // 2 - 1), pin)
        tile.putpixel((x, th // 2), (70, 70, 80, 255))
    save_asset("flip", "tile", tile)
    return tile


def flip_face(hour, minute, weekday, day, month):
    frame = Image.new("RGB", (W, H), (6, 6, 8))
    tile = flip_assets()
    digits = digit_sheet("flip", "digits", FLIP_DIGITS)
    small = load_font("capital-hill")
    ink, dim = (246, 238, 220), (120, 118, 112)
    y0 = 15
    blit(frame, tile, 2, y0)
    blit(frame, tile, 33, y0)
    # numerals 9x16, two per tile 2 px apart, centred on the leaves; the hinge cuts them
    for text, cx in ((f"{hour:02d}", 17), (f"{minute:02d}", 48)):
        x = cx - 10
        for ch in text:
            stamp(frame, digits[int(ch)], x, y0 + 9, ink)
            x += 11
    # the hinge again on top of the numerals, so the leaves read as two
    d = ImageDraw.Draw(frame)
    d.line((3, y0 + 16, 30, y0 + 16), fill=(10, 10, 12))
    d.line((34, y0 + 16, 61, y0 + 16), fill=(10, 10, 12))
    # weekday top-left, date top-right, small and dim
    draw_text(frame, small, 3, 4, weekday, dim)
    date = f"{day} {month}"
    draw_text(frame, small, W - 3 - text_width(small, date), 4, date, dim)
    # a seconds rail under the tiles: 30 amber ticks, lit up to the current second
    return frame


def flip_with_seconds(frame, second):
    d = ImageDraw.Draw(frame)
    lit, unlit = (214, 150, 40), (34, 30, 24)
    for i in range(30):
        x = 3 + i * 2
        d.point((x, 54), fill=lit if i < second // 2 else unlit)
    return frame


# ---------------------------------------------------------------- 2. Nixie


def nixie_assets():
    """A tube: 13x38 domed glass with a left highlight, a socket and two pins; the base
    plate spans the panel."""
    tw, th = 13, 38
    tube = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    d = ImageDraw.Draw(tube)
    glass, inner, hl = (46, 50, 68, 255), (14, 11, 16, 255), (84, 92, 118, 255)
    d.rounded_rectangle((0, 0, tw - 1, 31), radius=5, fill=glass)
    d.rounded_rectangle((1, 1, tw - 2, 30), radius=4, fill=inner)
    # the glass's highlight: a vertical streak on the left, dome dot on top
    d.line((2, 6, 2, 22), fill=hl)
    tube.putpixel((3, 4), hl)
    # socket and pins
    d.rectangle((1, 32, tw - 2, 34), fill=(34, 32, 38, 255))
    d.rectangle((2, 35, tw - 3, 35), fill=(24, 22, 26, 255))
    for x in (4, 8):
        d.line((x, 36, x, 37), fill=(150, 130, 90, 255))
    save_asset("nixie", "tube", tube)
    base = Image.new("RGBA", (W, 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(base)
    d.rectangle((0, 0, W - 1, 0), fill=(168, 124, 52, 255))  # brass trim
    d.rectangle((0, 1, W - 1, 3), fill=(70, 38, 20, 255))  # walnut
    for x in range(0, W, 7):  # grain
        d.point((x + 3, 2), fill=(84, 46, 24, 255))
    save_asset("nixie", "base", base)
    return tube, base


def nixie_face(hour, minute, weekday, day, month, colon=True):
    frame = Image.new("RGB", (W, H), (0, 0, 0))
    tube, base = nixie_assets()
    digits = digit_sheet("nixie", "digits", NIXIE_DIGITS)
    small = load_font("capital-hill")
    haze, halo, ink = (30, 9, 2), (150, 46, 6), (255, 132, 30)
    xs = (2, 17, 34, 49)
    ty = 8
    for x in xs:
        blit(frame, tube, x, ty)
        # the warm haze a lit tube has, filling the glass behind the cathode
        d = ImageDraw.Draw(frame)
        d.rounded_rectangle((x + 1, ty + 1, x + 11, ty + 30), radius=4, fill=haze)
        d.line((x + 2, ty + 6, x + 2, ty + 22), fill=(96, 60, 40))  # the glass's reflection over the haze
    # the wire digits, 7x11, with a one-pixel halo
    for x, ch in zip(xs, f"{hour:02d}{minute:02d}"):
        m = digits[int(ch)]
        gx, gy = x + 3, ty + 10
        stamp(frame, dilate(pad(m, 1)), gx - 1, gy - 1, halo)
        stamp(frame, m, gx, gy, ink)
    # neon colon between the pairs
    if colon:
        for y in (ty + 11, ty + 18):
            frame.putpixel((31, y), (90, 30, 4))
            frame.putpixel((32, y), (90, 30, 4))
            frame.putpixel((31, y + 1), ink)
            frame.putpixel((32, y + 1), ink)
            frame.putpixel((31, y + 2), (90, 30, 4))
            frame.putpixel((32, y + 2), (90, 30, 4))
    blit(frame, base, 0, ty + 38)
    # the date in dim amber under the base
    draw_centred(frame, small, 55, f"{weekday} {day} {month}", (150, 96, 36))
    return frame


# ---------------------------------------------------------------- 3. Horizon

SKY_KEYS = [  # hour, (top, horizon)
    (0.0, ((3, 5, 22), (8, 12, 42))),
    (4.5, ((3, 5, 22), (8, 12, 42))),
    (6.0, ((28, 22, 72), (240, 120, 60))),
    (7.0, ((50, 90, 180), (250, 190, 110))),
    (9.0, ((44, 110, 225), (140, 195, 245))),
    (13.0, ((36, 105, 230), (150, 205, 250))),
    (17.0, ((60, 100, 205), (245, 190, 110))),
    (18.5, ((70, 40, 120), (255, 110, 56))),
    (19.5, ((24, 14, 60), (120, 40, 70))),
    (21.0, ((3, 5, 22), (8, 12, 42))),
    (24.0, ((3, 5, 22), (8, 12, 42))),
]


def sky_colours(h):
    for (h0, c0), (h1, c1) in zip(SKY_KEYS, SKY_KEYS[1:]):
        if h0 <= h <= h1:
            t = (h - h0) / (h1 - h0)
            return lerp(c0[0], c1[0], t), lerp(c0[1], c1[1], t)
    return SKY_KEYS[0][1]


def daylight(h):
    """0 at night, 1 in full day, ramping over dawn and dusk."""
    if h < 5 or h > 20:
        return 0.0
    if h < 7:
        return (h - 5) / 2
    if h > 18:
        return (20 - h) / 2
    return 1.0


def horizon_assets():
    sun = Image.new("RGBA", (11, 11), (0, 0, 0, 0))
    d = ImageDraw.Draw(sun)
    d.ellipse((2, 2, 8, 8), fill=(255, 222, 90, 255))
    d.ellipse((3, 3, 7, 7), fill=(255, 240, 150, 255))
    for x, y in ((5, 0), (5, 10), (0, 5), (10, 5), (1, 1), (9, 9), (1, 9), (9, 1)):
        sun.putpixel((x, y), (255, 200, 70, 255))
    save_asset("horizon", "sun", sun)
    moon = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
    d = ImageDraw.Draw(moon)
    d.ellipse((0, 0, 8, 8), fill=(232, 232, 245, 255))
    d.ellipse((2, -1, 10, 7), fill=(0, 0, 0, 0))
    moon.putpixel((3, 6), (200, 200, 220, 255))
    save_asset("horizon", "moon", moon)
    cloud = Image.new("RGBA", (13, 5), (0, 0, 0, 0))
    d = ImageDraw.Draw(cloud)
    d.rectangle((1, 3, 11, 4), fill=(255, 255, 255, 255))
    d.rectangle((3, 1, 8, 2), fill=(255, 255, 255, 255))
    d.rectangle((5, 0, 6, 0), fill=(255, 255, 255, 255))
    d.rectangle((9, 2, 10, 2), fill=(255, 255, 255, 255))
    d.rectangle((0, 4, 12, 4), fill=(255, 255, 255, 255))
    save_asset("horizon", "cloud", cloud)
    # hills: two silhouettes over the panel's width, drawn as masks and tinted at draw time
    far = Image.new("L", (W, 18), 0)
    d = ImageDraw.Draw(far)
    for x in range(W):
        top = 6 + int(round(4 * math.sin(x / 9.0) + 2 * math.sin(x / 4.0 + 1)))
        d.line((x, top, x, 17), fill=255)
    near = Image.new("L", (W, 18), 0)
    d = ImageDraw.Draw(near)
    for x in range(W):
        top = 11 + int(round(3 * math.sin(x / 13.0 + 2.5) + 1.5 * math.sin(x / 5.0)))
        d.line((x, top, x, 17), fill=255)
    # a tree on the near hill
    tree = Image.new("RGBA", (5, 8), (0, 0, 0, 0))
    d = ImageDraw.Draw(tree)
    d.rectangle((2, 5, 2, 7), fill=(0, 0, 0, 255))
    d.polygon(((2, 0), (0, 4), (4, 4)), fill=(0, 0, 0, 255))
    d.polygon(((2, 2), (0, 5), (4, 5)), fill=(0, 0, 0, 255))
    save_asset("horizon", "far-hills", far)
    save_asset("horizon", "near-hills", near)
    save_asset("horizon", "tree", tree)
    stars = [(5, 4), (14, 9), (22, 3), (30, 12), (41, 6), (50, 2), (57, 10), (9, 16), (36, 18), (60, 20), (26, 22), (47, 15), (3, 24), (54, 27)]
    return sun, moon, cloud, far, near, tree, stars


def horizon_face(hour, minute, weekday, day, month):
    h = hour + minute / 60.0
    frame = Image.new("RGB", (W, H), (0, 0, 0))
    sun, moon, cloud, far, near, tree, stars = horizon_assets()
    top, hz = sky_colours(h)
    horizon_y = 46
    px = frame.load()
    for y in range(horizon_y):
        c = lerp(top, hz, y / (horizon_y - 1))
        for x in range(W):
            px[x, y] = c
    light = daylight(h)
    # stars, fading with the daylight; the brighter ones get a cross
    if light < 1:
        for i, (x, y) in enumerate(stars):
            c = lerp(sky_colours(h)[0], (230, 230, 250), (1 - light) * (0.9 if i % 3 else 1.0))
            px[x, y] = c
            if i % 3 == 0 and light < 0.3:
                c2 = lerp(sky_colours(h)[0], (150, 150, 190), 1 - light)
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if 0 <= x + dx < W and 0 <= y + dy < horizon_y:
                        px[x + dx, y + dy] = c2
    # the sun by day (06 to 18) and the moon by night on the same arc
    if 6 <= h <= 18:
        f = (h - 6) / 12
        body, r = sun, 5
    else:
        f = ((h - 18) % 24) / 12
        body, r = moon, 4
    cx = 6 + int(round((W - 13) * f))
    cy = 44 - int(round(22 * math.sin(math.pi * f)))
    blit(frame, body, cx - r, cy - r)
    # two clouds drifting with the minute
    tint = lerp((60, 60, 90), (255, 255, 255), light)
    for i, (base_x, y) in enumerate(((5, 22), (40, 31))):
        x = (base_x + minute * 64 // 60) % (W + 13) - 13
        c = cloud.copy()
        c.paste(Image.new("RGBA", c.size, tint + (255,)), (0, 0), c)
        blit(frame, c, x, y)
    # the hills, darker at night
    stamp(frame, far, 0, horizon_y - 12, lerp((10, 18, 30), (56, 120, 70), light))
    stamp(frame, near, 0, horizon_y, lerp((5, 10, 16), (30, 80, 44), light))
    blit(frame, tree, 49, 43)
    # the time over the sky, outlined so it reads on any colour; the date on the ground
    font = load_font("capital-hill")
    draw_centred(frame, font, 4, f"{hour:02d}:{minute:02d}", (255, 255, 255), 2, outline=(0, 0, 0))
    draw_centred(frame, font, 55, f"{weekday} {day} {month}", lerp((150, 160, 190), (230, 240, 230), light), 1, outline=(0, 0, 0))
    return frame


# ---------------------------------------------------------------- output


def upscale(img, s):
    return img.resize((img.width * s, img.height * s), Image.NEAREST)


def contact_sheet(items, scale=6, gap=12):
    font = load_font("everyday-typical")
    cw = W * scale
    sheet = Image.new("RGB", (len(items) * (cw + gap) + gap, cw + gap * 2 + 30), (24, 24, 28))
    for i, (name, img) in enumerate(items):
        x = gap + i * (cw + gap)
        sheet.paste(upscale(img, scale), (x, gap))
        m = scale_mask(text_mask(font, name.upper()), 3)
        stamp(sheet, m, x, gap + cw + 8, (220, 220, 230))
    return sheet


def main():
    os.makedirs(OUT, exist_ok=True)
    wd, day, mon = "SAT", "26", "SEP"
    faces = [
        ("flip", flip_with_seconds(flip_face(10, 32, wd, day, mon), 37)),
        ("nixie", nixie_face(10, 32, wd, day, mon)),
        ("horizon", horizon_face(18, 42, wd, day, mon)),
    ]
    for name, img in faces:
        img.save(os.path.join(OUT, name + ".png"))
        upscale(img, 8).save(os.path.join(OUT, name + "@8x.png"))
    contact_sheet(faces).save(os.path.join(OUT, "contact-sheet.png"))
    # the horizon through a day
    moments = [(0, 15), (6, 20), (8, 5), (12, 30), (17, 10), (18, 42), (20, 5), (23, 10)]
    strip = contact_sheet([(f"{hh:02d}:{mm:02d}", horizon_face(hh, mm, wd, day, mon)) for hh, mm in moments], scale=4)
    strip.save(os.path.join(OUT, "horizon-day.png"))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
