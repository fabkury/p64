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


def flip_tile(tile, digits, text, ink=(246, 238, 220)):
    """The tile sprite with two numerals on it, as the firmware would keep it: one 30x34
    image per displayed pair, rebuilt when the pair changes."""
    t = tile.copy()
    x = 5
    for ch in text:
        stamp(t, digits[int(ch)], x, 9, ink)
        x += 11
    d = ImageDraw.Draw(t)
    d.line((2, 16, 29, 16), fill=(10, 10, 12, 255))  # the hinge on top of the numerals
    return t


def shade(img, factor):
    """The RGB of an RGBA sprite scaled by `factor` (alpha kept)."""
    r, g, b, a = img.split()
    tab = [min(255, int(round(v * factor))) for v in range(256)]
    return Image.merge("RGBA", (r.point(tab), g.point(tab), b.point(tab), a))


# The flip: the old upper leaf falls (its height shrinks towards the hinge, darkening as
# it tilts away from the light), then the new lower leaf lands (grows from the hinge,
# bright while it faces the light). Heights of the moving leaf per frame, 17 = flat.
FALL = [15, 12, 8, 4, 1]
LAND = [2, 6, 10, 14, 16]


def flip_transition(old, new):
    """The frames of one tile flipping from `old` to `new` (both 30x34 sprites), without the
    resting frames. Each is what the firmware would compose from three row copies."""
    frames = []
    for h in FALL:
        f = new.copy()
        f.paste(old.crop((0, 17, 30, 34)), (0, 17))  # the old lower leaf still shows
        leaf = old.crop((1, 0, 29, 17)).resize((28, h), Image.NEAREST)
        f.paste(shade(leaf, 1 - 0.55 * (1 - h / 17)), (1, 17 - h))
        frames.append(f)
    for h in LAND:
        f = new.copy()
        f.paste(old.crop((0, 17, 30, 34)), (0, 17))
        leaf = new.crop((1, 17, 29, 34)).resize((28, h), Image.NEAREST)
        f.paste(shade(leaf, 1 + 0.5 * (1 - h / 17)), (1, 17))
        frames.append(f)
    return frames


def flip_compose(tiles, weekday, day, month, second):
    """A frame from the two tile sprites plus the chrome around them."""
    frame = Image.new("RGB", (W, H), (6, 6, 8))
    small = load_font("capital-hill")
    dim = (120, 118, 112)
    blit(frame, tiles[0], 2, 15)
    blit(frame, tiles[1], 33, 15)
    draw_text(frame, small, 3, 4, weekday, dim)
    date = f"{day} {month}"
    draw_text(frame, small, W - 3 - text_width(small, date), 4, date, dim)
    # the seconds rail: 30 amber ticks, one lit every two seconds
    d = ImageDraw.Draw(frame)
    lit, unlit = (214, 150, 40), (34, 30, 24)
    for i in range(30):
        d.point((3 + i * 2, 54), fill=lit if i < second // 2 else unlit)
    return frame


def flip_face(hour, minute, weekday, day, month, second=0):
    tile = flip_assets()
    digits = digit_sheet("flip", "digits", FLIP_DIGITS)
    tiles = (flip_tile(tile, digits, f"{hour:02d}"), flip_tile(tile, digits, f"{minute:02d}"))
    return flip_compose(tiles, weekday, day, month, second)


def flip_animation(h0, m0, h1, m1, weekday, day, month):
    """(frame, duration ms) pairs: the last seconds before the minute, the flip, the rest.
    The hour tile starts one frame after the minute tile, as on a real clock."""
    tile = flip_assets()
    digits = digit_sheet("flip", "digits", FLIP_DIGITS)
    old = (flip_tile(tile, digits, f"{h0:02d}"), flip_tile(tile, digits, f"{m0:02d}"))
    new = (flip_tile(tile, digits, f"{h1:02d}"), flip_tile(tile, digits, f"{m1:02d}"))
    seq = []
    for s in (56, 58):
        seq.append((flip_compose(old, weekday, day, month, s), 700))
    minute_frames = flip_transition(old[1], new[1])
    hour_frames = flip_transition(old[0], new[0]) if h0 != h1 else None
    n = len(minute_frames) + (1 if hour_frames else 0)
    for i in range(n):
        m_tile = minute_frames[i] if i < len(minute_frames) else new[1]
        if hour_frames:
            h_tile = old[0] if i == 0 else hour_frames[i - 1]
        else:
            h_tile = old[0]
        seq.append((flip_compose((h_tile, m_tile), weekday, day, month, 0), 45))
    seq.append((flip_compose(new, weekday, day, month, 0), 1200))
    seq.append((flip_compose(new, weekday, day, month, 2), 700))
    return seq


def save_gif(path, seq, scale=1):
    frames = [upscale(f, scale).quantize(colors=64, dither=Image.NONE) for f, _ in seq]
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=[d for _, d in seq], loop=0, optimize=False)


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

# The scene is procedural from the sun's real position (elevation and azimuth from the
# weather widget's latitude and longitude, the date and the local time), so every hour
# of every day and every place looks like itself: the twilight glow sits on the sun's side
# of the horizon, the sun climbs as high as it does there, stars come out when the sun
# is far enough below, and the moon shows its phase. The formulas are NOAA's low-precision
# ones (well under a degree), pure arithmetic for a host-tested solar.cpp.


def solar_position(lat, lon, tz_hours, year_day, hour_local):
    """Sun elevation and azimuth in degrees (azimuth from north, clockwise)."""
    g = 2 * math.pi / 365 * (year_day - 1 + (hour_local - tz_hours - 12) / 24)
    eqtime = 229.18 * (0.000075 + 0.001868 * math.cos(g) - 0.032077 * math.sin(g) - 0.014615 * math.cos(2 * g)
                       - 0.040849 * math.sin(2 * g))
    decl = (0.006918 - 0.399912 * math.cos(g) + 0.070257 * math.sin(g) - 0.006758 * math.cos(2 * g)
            + 0.000907 * math.sin(2 * g) - 0.002697 * math.cos(3 * g) + 0.00148 * math.sin(3 * g))
    tst = hour_local * 60 + eqtime + 4 * lon - 60 * tz_hours
    ha = math.radians(tst / 4 - 180)
    return _elev_az(math.radians(lat), decl, ha)


def _elev_az(lat, decl, ha):
    sin_el = math.sin(lat) * math.sin(decl) + math.cos(lat) * math.cos(decl) * math.cos(ha)
    el = math.asin(max(-1, min(1, sin_el)))
    az = math.atan2(math.sin(ha), math.cos(ha) * math.sin(lat) - math.tan(decl) * math.cos(lat)) + math.pi
    return math.degrees(el), math.degrees(az) % 360


def moon_phase(year_day, hour_local, year=2026):
    """0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter; from the new moon of
    2000-01-06 18:14 UTC and the synodic month."""
    days = (year - 2000) * 365.25 + year_day - 6.76 + hour_local / 24
    return (days / 29.530588) % 1.0


def moon_position(lat, lon, tz_hours, year_day, hour_local, phase):
    """Where the moon is, roughly: it trails the sun by the phase around the sky, and sits
    where the sun sits that many months later along the ecliptic (the 5 degree tilt of its
    orbit ignored)."""
    g = 2 * math.pi / 365 * (year_day - 1 + phase * 365.25)
    decl = (0.006918 - 0.399912 * math.cos(g) + 0.070257 * math.sin(g) - 0.006758 * math.cos(2 * g)
            + 0.000907 * math.sin(2 * g))
    g0 = 2 * math.pi / 365 * (year_day - 1 + (hour_local - tz_hours - 12) / 24)
    eqtime = 229.18 * (0.000075 + 0.001868 * math.cos(g0) - 0.032077 * math.sin(g0))
    tst = hour_local * 60 + eqtime + 4 * lon - 60 * tz_hours
    ha = math.radians(tst / 4 - 180 - 360 * phase)
    return _elev_az(math.radians(lat), decl, ha)


def sky_x(az, el, lat):
    """Screen x of a body: the viewer faces the equator, so the sun rises on the left and sets
    on the right in the northern hemisphere (mirrored in the southern); the east-west
    component is projected onto the view plane, so a body near the zenith stays near the
    middle instead of jumping to an edge."""
    e = math.sin(math.radians(az)) * math.cos(math.radians(el))
    if lat < 0:
        e = -e
    return int(round(31.5 - 25.5 * e))


def sky_y(el):
    # the arc's top stays under the time text (rows 3 to 17)
    return int(round(45 - 27 * max(-0.2, min(1, el / 90))))


# Colours keyed by the sun's elevation: (elevation, top of sky, horizon near the sun,
# horizon away from the sun).
SKY = [
    (-18, (2, 4, 18), (8, 12, 38), (8, 12, 38)),
    (-12, (5, 6, 30), (28, 18, 60), (12, 14, 46)),
    (-6, (16, 16, 64), (140, 60, 90), (40, 30, 80)),
    (-2, (30, 34, 100), (245, 118, 60), (90, 60, 110)),
    (2, (48, 74, 160), (255, 168, 84), (150, 120, 150)),
    (8, (44, 100, 205), (240, 205, 150), (190, 190, 210)),
    (16, (38, 104, 226), (165, 205, 245), (165, 205, 245)),
    (35, (30, 96, 224), (150, 205, 250), (150, 205, 250)),
    (90, (24, 86, 220), (150, 205, 250), (150, 205, 250)),
]


def sky_palette(el):
    for a, b in zip(SKY, SKY[1:]):
        if a[0] <= el <= b[0]:
            t = (el - a[0]) / (b[0] - a[0])
            return tuple(lerp(a[i], b[i], t) for i in (1, 2, 3))
    return SKY[0][1:] if el < SKY[0][0] else SKY[-1][1:]


def daylight(el):
    return max(0.0, min(1.0, (el + 6) / 14))


def draw_moon(frame, x, y, phase, lat, dark_sky):
    """A 9 px moon with its terminator; the lit side is the right one while waxing (mirrored
    in the southern hemisphere); the dark side shows faintly on a dark sky."""
    px = frame.load()
    c = math.cos(2 * math.pi * phase)
    for dy in range(-4, 5):
        v = dy / 4.5
        s = math.sqrt(max(0.0, 1 - v * v))
        for dx in range(-4, 5):
            u = dx / 4.5
            if u * u + v * v > 1:
                continue
            uu = u if lat >= 0 else -u
            lit = (uu > s * c) if phase < 0.5 else (uu < -s * c)
            X, Y = x + dx, y + dy
            if not (0 <= X < W and 0 <= Y < 46):
                continue
            if lit:
                px[X, Y] = (236, 236, 248)
            elif dark_sky:
                px[X, Y] = lerp(px[X, Y], (60, 62, 90), 0.6)


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


def horizon_face(hour, minute, weekday, day, month, lat=40.7, lon=-74.0, tz=-4, year_day=269, cover=0, precip=""):
    """`cover` 0..3 (clear, few, broken, overcast) and `precip` ("", "rain", "snow") come
    from the weather widget's current condition when a location is set."""
    h = hour + minute / 60.0
    frame = Image.new("RGB", (W, H), (0, 0, 0))
    sun, moon_unused, cloud, far, near, tree, stars = horizon_assets()
    el, az = solar_position(lat, lon, tz, year_day, h)
    top, near_c, away_c = sky_palette(el)
    grey = 0.22 * cover
    top, near_c, away_c = (lerp(c, (110, 116, 130), grey) for c in (top, near_c, away_c))
    sx, sy = sky_x(az, el, lat), sky_y(el)
    horizon_y = 46
    px = frame.load()
    glow_width = 30 if el < 12 else 60
    for x in range(W):
        w = math.exp(-((x - sx) / glow_width) ** 2)
        hz = lerp(away_c, near_c, w)
        for y in range(horizon_y):
            t = (y / (horizon_y - 1)) ** 1.5
            px[x, y] = lerp(top, hz, t)
    light = daylight(el)
    # stars once the sun is 4 degrees down, brightest from 12 down
    star_k = max(0.0, min(1.0, (-el - 4) / 8)) * (1 - 0.8 * min(1, cover / 2))
    if star_k > 0:
        for i, (x, y) in enumerate(stars):
            px[x, y] = lerp(px[x, y], (232, 232, 250), star_k * (0.85 if i % 3 else 1.0))
            if i % 3 == 0 and star_k > 0.6:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if 0 <= x + dx < W and 0 <= y + dy < horizon_y:
                        px[x + dx, y + dy] = lerp(px[x + dx, y + dy], (150, 150, 190), star_k)
    # the moon, when up
    phase = moon_phase(year_day, h)
    mel, maz = moon_position(lat, lon, tz, year_day, h, phase)
    if mel > -2 and el < 25:
        draw_moon(frame, sky_x(maz, mel, lat), sky_y(mel), phase, lat, el < -6)
    # the sun: a red disc at the horizon, gold with rays once it is up
    if el > -1.5:
        disc = sun.copy()
        if el < 6:
            k = max(0.0, el / 6)
            tint = lerp((255, 110, 60), (255, 222, 90), k)
            core = lerp((255, 150, 90), (255, 240, 150), k)
            d = ImageDraw.Draw(disc)
            disc.paste((0, 0, 0, 0), (0, 0, 11, 11))
            d.ellipse((2, 2, 8, 8), fill=tint + (255,))
            d.ellipse((3, 3, 7, 7), fill=core + (255,))
        blit(frame, disc, sx - 5, sy - 5)
    # clouds: their number from the cover, drifting with the minute, lit by the sky
    tint = lerp((50, 52, 80), lerp((255, 255, 255), near_c, 0.35), light)
    if cover >= 3:
        tint = lerp((40, 42, 60), (168, 172, 186), light)
    spots = [(5, 20), (40, 28), (24, 14), (52, 10), (12, 32), (34, 22), (58, 30)][: (1, 2, 4, 7)[cover]]
    for base_x, y in spots:
        x = (base_x + minute * 64 // 60) % (W + 13) - 13
        c = cloud.copy()
        c.paste(Image.new("RGBA", c.size, tint + (255,)), (0, 0), c)
        blit(frame, c, x, y)
    # rain or snow, streaking down, placed by the minute
    if precip:
        rng = (minute * 7919 + hour * 104729) % 65536
        for i in range(22):
            rng = (rng * 1103515245 + 12345) % 2147483648
            x, y = (rng >> 8) % W, (rng >> 4) % 44
            if precip == "rain":
                for k in range(3):
                    if y + k < horizon_y:
                        px[x, y + k] = lerp(px[x, y + k], (150, 190, 240), 0.7)
            else:
                px[x, y] = (240, 244, 255)
    # the hills, hazed by the horizon by day and black by night
    stamp(frame, far, 0, horizon_y - 12, lerp(lerp((8, 14, 26), (66, 128, 78), light), away_c, 0.25 * light))
    stamp(frame, near, 0, horizon_y, lerp((4, 8, 14), (32, 84, 46), light))
    blit(frame, tree, 49, 43)
    if precip == "snow" and light > 0:
        ImageDraw.Draw(frame).line((0, 46, 63, 46), fill=lerp((60, 64, 80), (225, 230, 240), light))
    # the time over the sky, outlined so it reads on any colour; the date on the ground
    font = load_font("capital-hill")
    draw_centred(frame, font, 4, f"{hour:02d}:{minute:02d}", (255, 255, 255), 2, outline=(0, 0, 0))
    draw_centred(frame, font, 55, f"{weekday} {day} {month}", lerp((150, 160, 190), (230, 240, 230), light), 1, outline=(0, 0, 0))
    return frame


# ---------------------------------------------------------------- 4. Words

# A word clock: a 12x9 grid of letters, the ones that spell the time lit. The grid is
# its own (no product's layout): the minute words on top, the hours in the middle,
# O'CLOCK and AM/PM at the bottom; four corner dots add the minutes past the five.
WORD_ROWS = [
    "ITBISQHALFAX",
    "QUARTERXFIVE",
    "TWENTYKTENTO",
    "PASTXONETWOK",
    "THREEFOURSIX",
    "FIVESEVENTEN",
    "EIGHTNINEXYZ",
    "ELEVENTWELVE",
    "OCLOCKQAMPMX",
]
WORDS = {  # word: (row, first column, length); the "m" suffix marks a minute word
    "IT": (0, 0, 2), "IS": (0, 3, 2), "HALF": (0, 6, 4), "QUARTER": (1, 0, 7), "FIVEm": (1, 8, 4),
    "TWENTY": (2, 0, 6), "TENm": (2, 7, 3), "TO": (2, 10, 2), "PAST": (3, 0, 4), "ONE": (3, 5, 3),
    "TWO": (3, 8, 3), "THREE": (4, 0, 5), "FOUR": (4, 5, 4), "SIX": (4, 9, 3), "FIVE": (5, 0, 4),
    "SEVEN": (5, 4, 5), "TEN": (5, 9, 3), "EIGHT": (6, 0, 5), "NINE": (6, 5, 4), "ELEVEN": (7, 0, 6),
    "TWELVE": (7, 6, 6), "OCLOCK": (8, 0, 6), "AM": (8, 7, 2), "PM": (8, 9, 2),
}
HOUR_WORDS = ["TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN"]
MINUTE_WORDS = [[], ["FIVEm", "PAST"], ["TENm", "PAST"], ["QUARTER", "PAST"], ["TWENTY", "PAST"],
                ["TWENTY", "FIVEm", "PAST"], ["HALF", "PAST"], ["TWENTY", "FIVEm", "TO"], ["TWENTY", "TO"],
                ["QUARTER", "TO"], ["TENm", "TO"], ["FIVEm", "TO"]]

# A 3x5 capital alphabet, drawn for the grid (the bundled fonts are wider).
ALPHABET = {
    "A": ["###", "#.#", "###", "#.#", "#.#"], "B": ["##.", "#.#", "##.", "#.#", "##."],
    "C": ["###", "#..", "#..", "#..", "###"], "D": ["##.", "#.#", "#.#", "#.#", "##."],
    "E": ["###", "#..", "##.", "#..", "###"], "F": ["###", "#..", "##.", "#..", "#.."],
    "G": ["###", "#..", "#.#", "#.#", "###"], "H": ["#.#", "#.#", "###", "#.#", "#.#"],
    "I": ["###", ".#.", ".#.", ".#.", "###"], "J": ["..#", "..#", "..#", "#.#", "###"],
    "K": ["#.#", "#.#", "##.", "#.#", "#.#"], "L": ["#..", "#..", "#..", "#..", "###"],
    "M": ["#.#", "###", "###", "#.#", "#.#"], "N": ["##.", "#.#", "#.#", "#.#", "#.#"],
    "O": ["###", "#.#", "#.#", "#.#", "###"], "P": ["###", "#.#", "###", "#..", "#.."],
    "Q": ["###", "#.#", "#.#", "###", "..#"], "R": ["###", "#.#", "##.", "#.#", "#.#"],
    "S": ["###", "#..", "###", "..#", "###"], "T": ["###", ".#.", ".#.", ".#.", ".#."],
    "U": ["#.#", "#.#", "#.#", "#.#", "###"], "V": ["#.#", "#.#", "#.#", "#.#", ".#."],
    "W": ["#.#", "#.#", "###", "###", "#.#"], "X": ["#.#", "#.#", ".#.", "#.#", "#.#"],
    "Y": ["#.#", "#.#", ".#.", ".#.", ".#."], "Z": ["###", "..#", ".#.", "#..", "###"],
}


def words_assets():
    letters = {k: bitmap(v) for k, v in ALPHABET.items()}
    sheet = Image.new("RGBA", (26 * 4 - 1, 5), (0, 0, 0, 0))
    for i, k in enumerate(sorted(letters)):
        sheet.paste(Image.new("RGBA", (3, 5), (255, 255, 255, 255)), (i * 4, 0), letters[k])
    save_asset("words", "alphabet", sheet)
    # the bezel: a brushed dark-steel frame, 3 px, with a lit inner edge and corner screws
    bezel = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(bezel)
    for i in range(3):
        v = (74, 72, 96, 58)[i]
        d.rectangle((i, i, W - 1 - i, H - 1 - i), outline=(v, v, v + 4, 255))
    for y in range(0, H, 3):  # the brush: every third row a shade lighter
        for x in list(range(0, 3)) + list(range(W - 3, W)):
            r, g, b, a = bezel.getpixel((x, y))
            bezel.putpixel((x, y), (min(255, r + 14), min(255, g + 14), min(255, b + 14), 255))
    d.rectangle((3, 3, W - 4, H - 4), outline=(28, 28, 32, 255))
    for cx, cy in ((1, 1), (W - 2, 1), (1, H - 2), (W - 2, H - 2)):  # screw heads
        bezel.putpixel((cx, cy), (130, 130, 138, 255))
    save_asset("words", "bezel", bezel)
    return letters, bezel


def words_face(hour, minute, second=0):
    frame = Image.new("RGB", (W, H), (12, 12, 15))
    letters, bezel = words_assets()
    unlit, lit, dot_on, dot_off = (42, 42, 50), (255, 250, 232), (255, 250, 232), (42, 42, 50)
    step, rem = minute // 5, minute % 5
    h12 = hour % 12 if step < 7 else (hour + 1) % 12
    active = {"IT", "IS", HOUR_WORDS[h12], "AM" if hour < 12 else "PM"} | set(MINUTE_WORDS[step])
    if step == 0:
        active.add("OCLOCK")
    lit_cells = set()
    for w in active:
        r, c, n = WORDS[w]
        lit_cells |= {(r, c + i) for i in range(n)}
    x0, y0 = 8, 5
    for r, row in enumerate(WORD_ROWS):
        for c, ch in enumerate(row):
            stamp(frame, letters[ch], x0 + c * 4, y0 + r * 6, lit if (r, c) in lit_cells else unlit)
    # the minute dots in the corners, clockwise from top-left
    d = ImageDraw.Draw(frame)
    for i, (x, y) in enumerate(((4, 4), (58, 4), (58, 58), (4, 58))):
        d.rectangle((x, y, x + 1, y + 1), fill=dot_on if i < rem else dot_off)
    blit(frame, bezel, 0, 0)
    return frame


# ---------------------------------------------------------------- 5. Hourglass

# The hour as sand: the top bulb holds what is left of it, the bottom what has run, a
# stream between them grain by grain. The numerals are the flip's (a shared asset).


def hourglass_assets():
    """The frame (26x52): walnut end plates with a highlight, two brass posts, the glass
    outline as one light line. The bulbs' inner widths per row are returned for the sand."""
    fw, fh = 26, 52
    frame = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
    d = ImageDraw.Draw(frame)
    walnut, wal_hi, wal_lo = (92, 52, 28, 255), (128, 78, 40, 255), (60, 34, 18, 255)
    brass, brass_hi = (176, 132, 56, 255), (222, 184, 92, 255)
    glass = (120, 140, 170, 255)
    for y0 in (0, fh - 5):
        d.rectangle((0, y0, fw - 1, y0 + 4), fill=walnut)
        d.line((1, y0, fw - 2, y0), fill=wal_hi)
        d.line((1, y0 + 4, fw - 2, y0 + 4), fill=wal_lo)
        d.point((0, y0), fill=wal_lo)
        d.point((fw - 1, y0), fill=wal_lo)
    for x in (1, fw - 3):
        d.rectangle((x, 5, x + 1, fh - 6), fill=brass)
        d.line((x, 5, x, fh - 6), fill=brass_hi)
    # the bulbs: half-widths per row, top bulb rows 5..25, neck 26, bottom bulb 27..46
    widths = {}
    for i, y in enumerate(range(5, 26)):
        t = i / 20
        widths[y] = int(round(9 - 8 * t ** 2.2))
    widths[26] = 1
    for i, y in enumerate(range(27, 47)):
        t = 1 - i / 19
        widths[y] = int(round(9 - 8 * t ** 2.2))
    cx = 12.5
    for y, hw in widths.items():
        d.point((int(cx - hw - 1), y), fill=glass)
        d.point((int(cx + hw + 1), y), fill=glass)
    d.line((int(cx - 9), 5, int(cx + 9), 5), fill=glass)
    d.line((int(cx - 9), 46, int(cx + 9), 46), fill=glass)
    save_asset("hourglass", "frame", frame)
    sand = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
    for y in range(4):
        for x in range(4):
            sand.putpixel((x, y), (232, 184, 84, 255) if (x + y) % 2 else (204, 150, 56, 255))
    save_asset("hourglass", "sand", sand)
    return frame, widths, sand


def hourglass_face(hour, minute, second, weekday, day, month):
    frame = Image.new("RGB", (W, H), (10, 10, 18))
    glass, widths, sand = hourglass_assets()
    digits = digit_sheet("flip", "digits", FLIP_DIGITS)
    gx, gy = 3, 6
    cx = gx + 12
    blit(frame, glass, gx, gy)
    px = frame.load()

    def sand_at(x, y):
        c = sand.getpixel((x % 4, y % 4))
        px[x, y] = c[:3]

    # the top bulb: rows 5..25 inside the glass, filled from the neck up by the fraction left
    top_rows = [(y, widths[y]) for y in range(5, 26)]
    area = sum(2 * hw + 1 for _, hw in top_rows)
    left = area * (60 - minute) / 60
    filled = 0
    surface = None
    for y, hw in reversed(top_rows):
        if filled >= left:
            break
        for x in range(int(cx - hw), int(cx + hw) + 1):
            sand_at(x, gy + y)
        filled += 2 * hw + 1
        surface = (y, hw)
    if surface and surface[1] >= 2:  # the funnel: the surface dips towards the middle
        y, hw = surface
        for x in range(int(cx - 1), int(cx + 1) + 1):
            px[x, gy + y] = (10, 10, 18)
    # the bottom bulb: a heap from the floor up by the fraction run, peaked under the neck
    bot_rows = [(y, widths[y]) for y in range(27, 47)]
    area = sum(2 * hw + 1 for _, hw in bot_rows)
    run = area * minute / 60
    filled = 0
    heap_top = 47
    for y, hw in reversed(bot_rows):
        if filled >= run:
            break
        for x in range(int(cx - hw), int(cx + hw) + 1):
            sand_at(x, gy + y)
        filled += 2 * hw + 1
        heap_top = y
    if minute > 0:  # the peak: two rows narrowing above the heap
        for k, hw in ((1, 2), (2, 0)):
            y = heap_top - k
            if y > 27:
                for x in range(int(cx - hw), int(cx + hw) + 1):
                    sand_at(x, gy + y)
        heap_top -= 2
    # the stream: one pixel wide from the neck to the heap, a grain missing on odd seconds
    for y in range(26, min(heap_top, 46)):
        if (y + second) % 3 != 0:
            sand_at(int(cx), gy + y)
    # the readout: the hour and the minute stacked in the flip numerals, the date under
    ink, dim = (226, 214, 190), (110, 104, 96)
    for text, y in ((f"{hour:02d}", 7), (f"{minute:02d}", 27)):
        x = 40
        for ch in text:
            stamp(frame, digits[int(ch)], x, y, ink)
            x += 11
    small = load_font("everyday-slight")
    draw_centred(frame, small, 48, weekday, dim, cx=49)
    draw_centred(frame, small, 55, f"{day} {month}", dim, cx=49)
    return frame


# ---------------------------------------------------------------- 6. Orrery

# A brass orrery on a star chart: the Earth goes round the Sun once in twelve hours, the
# Moon round the Earth once an hour, Mercury round the Sun once a minute; the time in
# figures on a brass plaque at the bottom.
ORRERY_CX, ORRERY_CY = 31.5, 27.5
R_EARTH, R_MERCURY, R_MOON = 20, 10, 4


def orrery_assets():
    sun = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
    d = ImageDraw.Draw(sun)
    d.ellipse((1, 1, 7, 7), fill=(255, 176, 40, 255))
    d.ellipse((2, 2, 6, 6), fill=(255, 226, 110, 255))
    d.ellipse((3, 3, 5, 5), fill=(255, 250, 200, 255))
    for x, y in ((4, 0), (4, 8), (0, 4), (8, 4)):
        sun.putpixel((x, y), (255, 150, 30, 255))
    save_asset("orrery", "sun", sun)
    earth = Image.new("RGBA", (5, 5), (0, 0, 0, 0))
    d = ImageDraw.Draw(earth)
    d.ellipse((0, 0, 4, 4), fill=(48, 110, 220, 255))
    for x, y in ((1, 1), (2, 1), (1, 2), (3, 3), (2, 3)):
        earth.putpixel((x, y), (70, 170, 80, 255))
    earth.putpixel((2, 0), (180, 210, 255, 255))
    save_asset("orrery", "earth", earth)
    moon = Image.new("RGBA", (3, 3), (0, 0, 0, 0))
    ImageDraw.Draw(moon).ellipse((0, 0, 2, 2), fill=(214, 214, 224, 255))
    moon.putpixel((1, 1), (170, 170, 184, 255))
    save_asset("orrery", "moon", moon)
    mercury = Image.new("RGBA", (3, 3), (0, 0, 0, 0))
    ImageDraw.Draw(mercury).ellipse((0, 0, 2, 2), fill=(200, 150, 110, 255))
    save_asset("orrery", "mercury", mercury)
    # the plate: the star chart with the brass rings (dotted circles), twelve ticks and
    # the plaque; drawn once, static
    plate = Image.new("RGBA", (W, H), (7, 8, 20, 255))
    px = plate.load()
    rng = 12345
    for _ in range(70):
        rng = (rng * 1103515245 + 12345) % 2147483648
        x, y = (rng >> 8) % W, (rng >> 3) % 53
        px[x, y] = (40, 44, 70, 255) if rng % 5 else (70, 76, 110, 255)
    brass, brass_dim = (196, 150, 64, 255), (120, 92, 40, 255)
    for r, colour in ((R_EARTH, brass), (R_MERCURY, brass_dim)):
        for i in range(720):
            a = math.radians(i / 2)
            x, y = int(round(ORRERY_CX + r * math.sin(a))), int(round(ORRERY_CY - r * math.cos(a)))
            if (x + y) % 2 == 0:
                px[x, y] = colour
    for i in range(12):
        a = math.radians(i * 30)
        for r in ((23, 25) if i % 3 == 0 else (23, 24)):
            x, y = int(round(ORRERY_CX + r * math.sin(a))), int(round(ORRERY_CY - r * math.cos(a)))
            px[x, y] = brass
    d = ImageDraw.Draw(plate)
    d.rectangle((15, 54, 48, 63), fill=(150, 112, 44, 255))
    d.rectangle((16, 55, 47, 62), fill=(96, 70, 26, 255))
    d.line((16, 55, 47, 55), fill=(214, 172, 84, 255))
    for x, y in ((17, 63), (46, 63), (17, 54), (46, 54)):
        plate.putpixel((x, y), (230, 200, 120, 255))
    save_asset("orrery", "plate", plate)
    return sun, earth, moon, mercury, plate


def orrery_face(hour, minute, second):
    frame = Image.new("RGB", (W, H), (0, 0, 0))
    sun, earth, moon, mercury, plate = orrery_assets()
    blit(frame, plate, 0, 0)
    d = ImageDraw.Draw(frame)

    def at(r, angle_deg, cx=ORRERY_CX, cy=ORRERY_CY):
        a = math.radians(angle_deg)
        return cx + r * math.sin(a), cy - r * math.cos(a)

    hour_a = ((hour % 12) + minute / 60) * 30
    minute_a = (minute + second / 60) * 6
    second_a = second * 6
    ex, ey = at(R_EARTH, hour_a)
    mx, my = at(R_MOON, minute_a, ex, ey)
    qx, qy = at(R_MERCURY, second_a)
    # the arms, brass, from the hub to each body
    arm = (150, 112, 44)
    d.line((ORRERY_CX, ORRERY_CY, ex, ey), fill=arm)
    d.line((ORRERY_CX, ORRERY_CY, qx, qy), fill=(110, 82, 34))
    d.line((ex, ey, mx, my), fill=arm)
    blit(frame, sun, int(round(ORRERY_CX - 4.5)), int(round(ORRERY_CY - 4.5)))
    blit(frame, earth, int(round(ex - 2.5)), int(round(ey - 2.5)))
    blit(frame, moon, int(round(mx - 1.5)), int(round(my - 1.5)))
    blit(frame, mercury, int(round(qx - 1.5)), int(round(qy - 1.5)))
    font = load_font("capital-hill")
    draw_centred(frame, font, 57, f"{hour:02d}:{minute:02d}", (240, 208, 130))
    return frame


# ---------------------------------------------------------------- output


def upscale(img, s):
    return img.resize((img.width * s, img.height * s), Image.NEAREST)


def contact_sheet(items, scale=6, gap=12, columns=None):
    font = load_font("everyday-typical")
    cw = W * scale
    columns = columns or len(items)
    rows = (len(items) + columns - 1) // columns
    ch = cw + gap + 30
    sheet = Image.new("RGB", (columns * (cw + gap) + gap, rows * ch + gap), (24, 24, 28))
    for i, (name, img) in enumerate(items):
        x = gap + (i % columns) * (cw + gap)
        y = gap + (i // columns) * ch
        sheet.paste(upscale(img, scale), (x, y))
        m = scale_mask(text_mask(font, name.upper()), 3)
        stamp(sheet, m, x, y + cw + 8, (220, 220, 230))
    return sheet


def main():
    os.makedirs(OUT, exist_ok=True)
    wd, day, mon = "SAT", "26", "SEP"
    faces = [
        ("flip", flip_face(10, 32, wd, day, mon, 37)),
        ("nixie", nixie_face(10, 32, wd, day, mon)),
        ("horizon", horizon_face(18, 42, wd, day, mon)),
        ("words", words_face(10, 32)),
        ("hourglass", hourglass_face(10, 32, 37, wd, day, mon)),
        ("orrery", orrery_face(10, 32, 37)),
    ]
    for name, img in faces:
        img.save(os.path.join(OUT, name + ".png"))
        upscale(img, 8).save(os.path.join(OUT, name + "@8x.png"))
    contact_sheet(faces, scale=5, columns=3).save(os.path.join(OUT, "contact-sheet.png"))
    # the second three over a few moments
    times = [(0, 0, 0), (7, 15, 12), (10, 32, 37), (14, 45, 50), (19, 58, 5), (23, 30, 20)]
    for name, fn in (("words", lambda h, m, s: words_face(h, m, s)),
                     ("hourglass", lambda h, m, s: hourglass_face(h, m, s, wd, day, mon)),
                     ("orrery", lambda h, m, s: orrery_face(h, m, s))):
        contact_sheet([(f"{h:02d}:{m:02d}", fn(h, m, s)) for h, m, s in times], scale=4, columns=6).save(
            os.path.join(OUT, name + "-moments.png"))
    # the hourglass and the orrery over a few seconds
    save_gif(os.path.join(OUT, "hourglass@8x.gif"), [(hourglass_face(10, 32, s, wd, day, mon), 500) for s in range(6)], 8)
    save_gif(os.path.join(OUT, "orrery@8x.gif"), [(orrery_face(10, 32, s), 500) for s in range(30, 42)], 8)
    # the flip's animation: a minute, and the hour with both tiles
    for name, seq in (("flip-minute", flip_animation(10, 32, 10, 33, wd, day, mon)),
                      ("flip-hour", flip_animation(10, 59, 11, 0, wd, day, mon))):
        save_gif(os.path.join(OUT, name + ".gif"), seq)
        save_gif(os.path.join(OUT, name + "@8x.gif"), seq, 8)
    # the horizon through one day at 40.7 N (New York, 2026-09-26): twelve moments
    moments = [(0, 30), (3, 0), (5, 30), (6, 20), (6, 50), (8, 0), (12, 30), (16, 30), (18, 20), (18, 50), (19, 25), (21, 0)]
    strip = contact_sheet([(f"{hh:02d}:{mm:02d}", horizon_face(hh, mm, wd, day, mon)) for hh, mm in moments], scale=4, columns=6)
    strip.save(os.path.join(OUT, "horizon-day.png"))
    # the same hour in four weathers, and three other places on the same day
    variants = [("clear", horizon_face(15, 0, wd, day, mon)),
                ("broken", horizon_face(15, 0, wd, day, mon, cover=2)),
                ("rain", horizon_face(15, 0, wd, day, mon, cover=3, precip="rain")),
                ("snow", horizon_face(15, 0, wd, day, mon, cover=3, precip="snow")),
                ("sao paulo", horizon_face(18, 10, wd, day, mon, lat=-23.55, lon=-46.63, tz=-3)),
                ("tromso dec", horizon_face(13, 0, "MON", "21", "DEC", lat=69.65, lon=18.96, tz=1, year_day=355)),
                ("reykjavik jun", horizon_face(23, 30, "SUN", "21", "JUN", lat=64.15, lon=-21.94, tz=0, year_day=172)),
                ("singapore", horizon_face(12, 0, wd, day, mon, lat=1.35, lon=103.8, tz=8))]
    contact_sheet(variants, scale=4, columns=4, gap=24).save(os.path.join(OUT, "horizon-variants.png"))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
