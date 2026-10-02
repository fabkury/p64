#!/usr/bin/env python3
r"""The themed clock faces (spec 7.1) drawn on the host: the design reference of the
firmware's implementation and the source of its pixel-exact test references.

    python tools\mock_clock_faces.py        (from firmware/; needs Pillow)

What it does:
  - draws every face's bitmap assets into assets/clock/<face>/*.png when the file does not
    exist yet (edit a PNG by hand and it is kept; delete it to have it redrawn), the way
    gen_weather_icons.py does; tools/gen_clock_assets.py bakes them into the firmware;
  - renders the review images under docs/design/clock-candidates/ (each face at 64x64 and
    8x, moments strips, the flip's GIFs, the horizon's day and variants);
  - writes the test references under tests/host/corpus/clock/: one 64x64 PNG per moment,
    named <face>-HHMMSS[-flags].png (flags: s seconds on, b blinking colon, h 12-hour,
    pN the flip's transition frame N), all on Saturday 2026-09-26; tests/host/run.py
    renders the same moments with the firmware's code and compares pixel for pixel (the
    horizon, floating-point, within a tolerance).

Everything here is drawn the way the firmware draws: the bundled fonts through the very
glyph tables of components/p64_gfx/src/fonts_data.cpp (parsed below) with the same
placement as fonts.cpp, sprites stamped with a 0/1 alpha, integer arithmetic wherever a
pixel depends on it, and a shared sine table for the orrery (gen_clock_assets.py emits
the same numbers). A face that draws with floating point (the horizon's sky) says so.
"""

import math
import os
import re

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.abspath(os.path.join(HERE, ".."))
ASSETS = os.path.join(FIRMWARE, "assets", "clock")
DESIGN = os.path.join(FIRMWARE, "docs", "design", "clock-candidates")
CORPUS = os.path.join(FIRMWARE, "tests", "host", "corpus", "clock")
FONTS_DATA = os.path.join(FIRMWARE, "components", "p64_gfx", "src", "fonts_data.cpp")
W = H = 64

# ---------------------------------------------------------------- the frame


class Frame:
    """A 64x64 RGB picture with the firmware Frame's semantics: drawing outside is ignored."""

    def __init__(self, colour=(0, 0, 0)):
        self.img = Image.new("RGB", (W, H), colour)
        self.px = self.img.load()

    def set(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[x, y] = c

    def get(self, x, y):
        return self.px[x, y] if 0 <= x < W and 0 <= y < H else (0, 0, 0)

    def fill_rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def line(self, x0, y0, x1, y1, c):
        """Bresenham, the same steps as the firmware's sprite::line."""
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.set(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy


# ---------------------------------------------------------------- the fonts, from the firmware's tables


class Font:
    def __init__(self, name, size, top, bottom, first, last, glyphs, bits):
        self.name, self.size, self.top, self.bottom = name, size, top, bottom
        self.first, self.last, self.glyphs, self.bits = first, last, glyphs, bits

    def glyph(self, ch):
        code = ord(ch)
        if code < self.first or code > self.last:
            return None
        return self.glyphs[code - self.first]

    def bit(self, g, col, row):
        _code, _adv, width, _h, _xo, _yo, off = g
        byte = self.bits[off + row * ((width + 7) // 8) + col // 8]
        return (byte >> (7 - col % 8)) & 1


_FONTS = {}


def load_font(name):
    """The font's glyph table as the firmware has it (fonts_data.cpp)."""
    if not _FONTS:
        text = open(FONTS_DATA, encoding="utf-8").read()
        for m in re.finditer(r"const Font k_(\w+) = \{\"([\w-]+)\", \"[^\"]*\", (\d+), \w+, (\d+), (\d+), (\d+), (\d+), (\d+)", text):
            ident, fname, size, top, bottom, _baseline, first, last = m.groups()
            bits_src = re.search(r"const uint8_t k_%s_bits\[\] = \{(.*?)\};" % ident, text, re.S).group(1)
            bits = bytes(int(v, 16) for v in re.findall(r"0x[0-9a-f]{2}", bits_src))
            glyph_src = re.search(r"const Glyph k_%s_glyphs\[\] = \{(.*?)\};" % ident, text, re.S).group(1)
            glyphs = [tuple(int(v) for v in g) for g in re.findall(r"\{(\d+), (\d+), (\d+), (\d+), (-?\d+), (-?\d+), (\d+)\}", glyph_src)]
            _FONTS[fname] = Font(fname, int(size), int(top), int(bottom), int(first), int(last), glyphs, bits)
    return _FONTS[name]


def text_width(font, text, scale=1):
    return sum((g[1] if (g := font.glyph(ch)) else font.size) for ch in text) * scale


def draw_text(frame, font, x, y, text, colour, scale=1, outline=None):
    """fonts::draw: the ink box's top-left at (x, y); the outline is a 3x3 halo per pixel."""
    for halo in ((True, False) if outline is not None else (False,)):
        cx = x
        for ch in text:
            g = font.glyph(ch)
            if not g:
                if not halo:
                    w, h = font.size - 1, font.size
                    frame.fill_rect(cx, y, w * scale, scale, colour)
                    frame.fill_rect(cx, y + (h - 1) * scale, w * scale, scale, colour)
                    frame.fill_rect(cx, y, scale, h * scale, colour)
                    frame.fill_rect(cx + (w - 1) * scale, y, scale, h * scale, colour)
                cx += font.size * scale
                continue
            _code, adv, width, height, x_off, y_off, _off = g
            for row in range(height):
                for col in range(width):
                    if not font.bit(g, col, row):
                        continue
                    px = cx + (x_off + col) * scale
                    py = y + (y_off - font.top + row) * scale
                    if halo:
                        frame.fill_rect(px - 1, py - 1, scale + 2, scale + 2, outline)
                    else:
                        frame.fill_rect(px, py, scale, scale, colour)
            cx += adv * scale
    return text_width(font, text, scale)


def draw_centred(frame, font, y, text, colour, scale=1, outline=None):
    return draw_text(frame, font, (W - text_width(font, text, scale)) // 2, y, text, colour, scale, outline)


def draw_centred_at(frame, font, cx, y, text, colour, scale=1):
    return draw_text(frame, font, cx - text_width(font, text, scale) // 2, y, text, colour, scale)


# ---------------------------------------------------------------- sprites


def blit(frame, sprite, x, y):
    """Copies the pixels whose alpha is 128 or more (sprite::blit)."""
    px = sprite.load()
    for yy in range(sprite.height):
        for xx in range(sprite.width):
            c = px[xx, yy]
            if c[3] >= 128:
                frame.set(x + xx, y + yy, c[:3])


def blit_tinted(frame, sprite, x, y, colour):
    """Paints `colour` where the sprite's alpha is 128 or more (sprite::stamp)."""
    px = sprite.load()
    for yy in range(sprite.height):
        for xx in range(sprite.width):
            if px[xx, yy][3] >= 128:
                frame.set(x + xx, y + yy, colour)


def stamp_mask(frame, mask, x, y, colour):
    """`mask` is an "L" image: 255 = ink."""
    px = mask.load()
    for yy in range(mask.height):
        for xx in range(mask.width):
            if px[xx, yy]:
                frame.set(x + xx, y + yy, colour)


def stamp_halo(frame, mask, x, y, colour):
    """The 8-neighbourhood of every inked pixel (sprite::stamp_halo)."""
    px = mask.load()
    for yy in range(mask.height):
        for xx in range(mask.width):
            if px[xx, yy]:
                frame.fill_rect(x + xx - 1, y + yy - 1, 3, 3, colour)


def bitmap(rows):
    """An ASCII-art glyph ('#' = ink) as an "L" mask."""
    m = Image.new("L", (len(rows[0]), len(rows)), 0)
    px = m.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "#":
                px[x, y] = 255
    return m


def asset_path(face, name):
    return os.path.join(ASSETS, face, name + ".png")


def asset(face, name, draw):
    """The asset PNG, drawn by `draw()` (an RGBA or "L" image) when it does not exist yet."""
    path = asset_path(face, name)
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        img = draw()
        if img.mode == "L":
            white = Image.new("RGBA", img.size, (255, 255, 255, 255))
            white.putalpha(img)
            img = white
        img.save(path)
    return Image.open(path).convert("RGBA")


def sheet_from(glyphs):
    """Glyph masks side by side, 1 px apart, as one RGBA sheet."""
    masks = [bitmap(g) for g in glyphs]
    w, h = masks[0].width, masks[0].height
    sheet = Image.new("RGBA", (len(masks) * (w + 1) - 1, h), (0, 0, 0, 0))
    for i, m in enumerate(masks):
        sheet.paste(Image.new("RGBA", m.size, (255, 255, 255, 255)), (i * (w + 1), 0), m)
    return sheet


def cells(sheet, count):
    """The masks back out of a sheet of `count` equal cells 1 px apart."""
    w = (sheet.width + 1) // count - 1
    return [sheet.crop((i * (w + 1), 0, i * (w + 1) + w, sheet.height)).split()[3].point(lambda v: 255 if v >= 128 else 0)
            for i in range(count)]


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def upscale(img, s):
    return img.resize((img.width * s, img.height * s), Image.NEAREST)


# ---------------------------------------------------------------- the moment


class Moment:
    """A local time on Saturday 2026-09-26 (the references' date)."""

    def __init__(self, hour, minute, second=0, wday=6, mday=26, mon=9, yday=268):
        self.hour, self.minute, self.second = hour, minute, second
        self.wday, self.mday, self.mon, self.yday = wday, mday, mon, yday

    @property
    def weekday(self):
        return ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"][self.wday]

    @property
    def month(self):
        return ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"][self.mon - 1]

    def date(self, month_first=False):
        return f"{self.weekday} {self.month} {self.mday}" if month_first else f"{self.weekday} {self.mday} {self.month}"

    def h12(self):
        return self.hour % 12 or 12

    def meridiem(self):
        return "AM" if self.hour < 12 else "PM"


class Options:
    def __init__(self, seconds=False, blink=False, h24=True, month_first=False):
        self.seconds, self.blink, self.h24, self.month_first = seconds, blink, h24, month_first


def hour_text(m, o):
    """The hour as the faces show it: two digits in 24 h, the leading zero blank in 12 h."""
    return f"{m.hour:02d}" if o.h24 else f"{m.h12():2d}"


def colon_on(m, o):
    return not o.blink or m.second % 2 == 0


# ---------------------------------------------------------------- 1. Flip

# The flip's numerals, 9x16, Helvetica-bold proportions: 3 px stems, 2 px bars.
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
FLIP_INK, FLIP_DIM, FLIP_BG = (246, 238, 220), (120, 118, 112), (6, 6, 8)
FLIP_HINGE = (10, 10, 12)
RAIL_LIT, RAIL_UNLIT = (214, 150, 40), (34, 30, 24)
# The transition: the old upper leaf falls (heights 15 to 1, darkening), the new lower leaf
# lands (2 to 16, lit). Ten frames of 45 ms.
FALL = [15, 12, 8, 4, 1]
LAND = [2, 6, 10, 14, 16]


def draw_flip_tile():
    """A split-flap tile: 28x34 card in two leaves, rounded corners, a top highlight, a
    hinge line, axle pins 1 px outside on either side (30x34)."""
    tw, th = 28, 34
    tile = Image.new("RGBA", (tw + 2, th), (0, 0, 0, 0))
    d = ImageDraw.Draw(tile)
    upper, lower = (58, 58, 68, 255), (44, 44, 52, 255)
    edge, hinge, pin = (30, 30, 36, 255), (10, 10, 12, 255), (110, 110, 120, 255)
    d.rectangle((1, 0, tw, th // 2 - 1), fill=upper)
    d.rectangle((1, th // 2, tw, th - 1), fill=lower)
    for cx, cy in ((1, 0), (tw, 0), (1, th - 1), (tw, th - 1)):
        tile.putpixel((cx, cy), (0, 0, 0, 0))
    for x, y in ((2, 0), (1, 1), (tw - 1, 0), (tw, 1), (2, th - 1), (1, th - 2), (tw - 1, th - 1), (tw, th - 2)):
        tile.putpixel((x, y), edge)
    d.line((3, 0, tw - 2, 0), fill=(92, 92, 104, 255))
    d.line((2, th - 1, tw - 1, th - 1), fill=edge)
    d.line((1, th // 2 - 1, tw, th // 2 - 1), fill=hinge)
    d.line((1, th // 2, tw, th // 2), fill=(36, 36, 42, 255))
    for x in (0, tw + 1):
        tile.putpixel((x, th // 2 - 1), pin)
        tile.putpixel((x, th // 2), (70, 70, 80, 255))
    return tile


def flip_assets():
    return asset("flip", "tile", draw_flip_tile), cells(asset("flip", "digits", lambda: sheet_from(FLIP_DIGITS)), 10)


def flip_tile_image(tile, digits, text):
    """The tile with its two numerals stamped on and the hinge over them (30x34 RGBA)."""
    t = tile.copy()
    px = t.load()
    x = 5
    for ch in text:
        if ch != " ":
            m = digits[int(ch)].load()
            for yy in range(16):
                for xx in range(9):
                    if m[xx, yy]:
                        px[x + xx, 9 + yy] = FLIP_INK + (255,)
        x += 11
    for xx in range(2, 30):
        px[xx, 16] = FLIP_HINGE + (255,)
    return t


def shade_pct(h, falling):
    return 100 - (55 * (17 - h)) // 17 if falling else 100 + (50 * (17 - h)) // 17


def shade(c, pct):
    return tuple(min(255, (v * pct + 50) // 100) for v in c[:3])


def flip_leaf(frame, tile_img, src_y0, h, dst_x, dst_y, pct):
    """The 17 rows from src_y0 squashed to h rows at (dst_x, dst_y), shaded by pct %."""
    px = tile_img.load()
    for y in range(h):
        sy = src_y0 + ((2 * y + 1) * 17) // (2 * h)
        for x in range(1, 29):
            c = px[x, sy]
            if c[3] >= 128:
                frame.set(dst_x + x, dst_y + y, shade(c, pct))


def draw_flip(m, o, phase=0, prev=None):
    """The flip at `m`; `phase` 1..10 draws that frame of the change from `prev` to `m`
    (the hour tile one frame behind the minute tile, as on a real clock)."""
    frame = Frame(FLIP_BG)
    tile, digits = flip_assets()
    small = load_font("capital-hill")
    new = (flip_tile_image(tile, digits, hour_text(m, o)), flip_tile_image(tile, digits, f"{m.minute:02d}"))
    old = (flip_tile_image(tile, digits, hour_text(prev, o)), flip_tile_image(tile, digits, f"{prev.minute:02d}")) if prev else new
    for i, x0 in enumerate((2, 33)):
        k = phase if i == 1 else max(0, phase - 1)
        changed = old[i].tobytes() != new[i].tobytes()
        if not phase or not changed:
            blit(frame, new[i], x0, 15)
            continue
        if k == 0:  # the hour tile, not started yet
            blit(frame, old[i], x0, 15)
            continue
        blit(frame, new[i], x0, 15)
        blit(frame, old[i].crop((0, 17, 30, 34)), x0, 15 + 17)
        if k <= 5:
            h = FALL[k - 1]
            flip_leaf(frame, old[i], 0, h, x0, 15 + 17 - h, shade_pct(h, True))
        else:
            h = LAND[k - 6]
            flip_leaf(frame, new[i], 17, h, x0, 15 + 17, shade_pct(h, False))
    draw_text(frame, small, 3, 4, m.weekday, FLIP_DIM)
    date = f"{m.month} {m.mday}" if o.month_first else f"{m.mday} {m.month}"
    draw_text(frame, small, 61 - text_width(small, date), 4, date, FLIP_DIM)
    for i in range(30):
        frame.set(3 + i * 2, 54, RAIL_LIT if o.seconds and i < m.second // 2 else RAIL_UNLIT)
    if not o.h24:
        draw_centred(frame, small, 57, m.meridiem(), FLIP_DIM)
    return frame


def flip_animation(m0, m1, o):
    """(frame, duration ms): the seconds before the minute, the ten frames, the rest."""
    seq = [(draw_flip(Moment(m0.hour, m0.minute, s), o), 700) for s in (56, 58)]
    for k in range(1, 11):
        seq.append((draw_flip(m1, o, k, m0), 45))
    seq.append((draw_flip(m1, o), 1200))
    seq.append((draw_flip(Moment(m1.hour, m1.minute, 2), o), 700))
    return seq


# ---------------------------------------------------------------- 2. Nixie

# Wire numerals, 7x11, one pixel thick like the cathodes of a tube.
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
NIXIE_HALO, NIXIE_INK, NIXIE_DIM = (150, 46, 6), (255, 132, 30), (90, 30, 4)
NIXIE_DATE = (150, 96, 36)
NIXIE_XS, NIXIE_TY = (2, 17, 34, 49), 8


def draw_nixie_tube():
    """A lit tube, 13x38: domed glass with the warm haze inside and a reflection, socket
    and two pins."""
    tw, th = 13, 38
    tube = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    d = ImageDraw.Draw(tube)
    glass, haze, hl = (46, 50, 68, 255), (30, 9, 2, 255), (96, 60, 40, 255)
    d.rounded_rectangle((0, 0, tw - 1, 31), radius=5, fill=glass)
    d.rounded_rectangle((1, 1, tw - 2, 30), radius=4, fill=haze)
    d.line((2, 6, 2, 22), fill=hl)
    tube.putpixel((3, 4), (84, 92, 118, 255))
    d.rectangle((1, 32, tw - 2, 34), fill=(34, 32, 38, 255))
    d.rectangle((2, 35, tw - 3, 35), fill=(24, 22, 26, 255))
    for x in (4, 8):
        d.line((x, 36, x, 37), fill=(150, 130, 90, 255))
    return tube


def draw_nixie_base():
    base = Image.new("RGBA", (W, 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(base)
    d.rectangle((0, 0, W - 1, 0), fill=(168, 124, 52, 255))
    d.rectangle((0, 1, W - 1, 3), fill=(70, 38, 20, 255))
    for x in range(0, W, 7):
        d.point((x + 3, 2), fill=(84, 46, 24, 255))
    return base


def nixie_assets():
    return (asset("nixie", "tube", draw_nixie_tube), cells(asset("nixie", "digits", lambda: sheet_from(NIXIE_DIGITS)), 10),
            asset("nixie", "base", draw_nixie_base))


def draw_nixie(m, o):
    frame = Frame((0, 0, 0))
    tube, digits, base = nixie_assets()
    small = load_font("capital-hill")
    ty = NIXIE_TY
    for x in NIXIE_XS:
        blit(frame, tube, x, ty)
    for x, ch in zip(NIXIE_XS, hour_text(m, o) + f"{m.minute:02d}"):
        if ch == " ":
            continue
        d = digits[int(ch)]
        stamp_halo(frame, d, x + 3, ty + 10, NIXIE_HALO)
        stamp_mask(frame, d, x + 3, ty + 10, NIXIE_INK)
    if colon_on(m, o):
        for y in (ty + 11, ty + 18):
            for x in (31, 32):
                frame.set(x, y, NIXIE_DIM)
                frame.set(x, y + 1, NIXIE_INK)
                frame.set(x, y + 2, NIXIE_DIM)
    blit(frame, base, 0, ty + 38)
    draw_centred(frame, small, 55, m.date(o.month_first), NIXIE_DATE)
    if not o.h24:
        draw_text(frame, small, 63 - text_width(small, m.meridiem()), 1, m.meridiem(), NIXIE_DATE)
    return frame


# ---------------------------------------------------------------- 3. Horizon (floating point)

# The scene is procedural from the sun's real position (elevation and azimuth from the
# weather widget's latitude and longitude, the date and the local time): the twilight glow
# sits on the sun's side of the horizon, the sun climbs as high as it does there, stars
# come out when the sun is far enough below, and the moon shows its phase. NOAA's
# low-precision sun (well under a degree) and the Astronomical Almanac's low-precision
# moon (about 0.3 degrees), the same in solar.cpp.


def solar_position(lat, lon, tz_hours, year_day, hour_local):
    """Sun elevation and azimuth in degrees (azimuth from north, clockwise); year_day 0-based."""
    g = 2 * math.pi / 365 * (year_day + (hour_local - tz_hours - 12) / 24)
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


def _j2000_days(year, year_day, hour_local, tz_hours):
    """Days from J2000.0 (2000-01-01 12:00 UT) to a local moment."""
    def leaps(y):
        return (y - 1) // 4 - (y - 1) // 100 + (y - 1) // 400
    return 365 * (year - 2000) + (leaps(year) - leaps(2000)) + year_day + (hour_local - tz_hours) / 24 - 0.5


def _sun_longitude(n):
    """The sun's apparent ecliptic longitude in degrees (the Astronomical Almanac's low precision)."""
    g = math.radians(357.528 + 0.9856003 * n)
    return 280.460 + 0.9856474 * n + 1.915 * math.sin(g) + 0.020 * math.sin(2 * g)


def _moon_ecliptic(n):
    """The moon's geocentric ecliptic longitude, latitude and horizontal parallax in degrees:
    the Astronomical Almanac's low-precision series (about 0.3 degrees)."""
    t = n / 36525

    def s(a, b):
        return math.sin(math.radians(a + b * t))

    def c(a, b):
        return math.cos(math.radians(a + b * t))
    lon = (218.32 + 481267.881 * t + 6.29 * s(135.0, 477198.87) - 1.27 * s(259.3, -413335.36)
           + 0.66 * s(235.7, 890534.22) + 0.21 * s(269.9, 954397.74) - 0.19 * s(357.5, 35999.05)
           - 0.11 * s(186.5, 966404.03))
    lat = (5.13 * s(93.3, 483202.02) + 0.28 * s(228.2, 960400.89) - 0.28 * s(318.3, 6003.15)
           - 0.17 * s(217.6, -407332.21))
    par = (0.9508 + 0.0518 * c(135.0, 477198.87) + 0.0095 * c(259.3, -413335.36) + 0.0078 * c(235.7, 890534.22)
           + 0.0028 * c(269.9, 954397.74))
    return lon, lat, par


def moon_phase(year, year_day, hour_local, tz_hours):
    """0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter: the moon's elongation from the sun."""
    n = _j2000_days(year, year_day, hour_local, tz_hours)
    return ((_moon_ecliptic(n)[0] - _sun_longitude(n)) / 360) % 1.0


def moon_position(lat, lon, tz_hours, year, year_day, hour_local):
    """Where the moon is as seen from the place (parallax included, refraction not)."""
    n = _j2000_days(year, year_day, hour_local, tz_hours)
    mlon, mlat, par = _moon_ecliptic(n)
    obliquity = math.radians(23.439 - 0.0000004 * n)
    mlon, mlat = math.radians(mlon), math.radians(mlat)
    ra = math.atan2(math.sin(mlon) * math.cos(obliquity) - math.tan(mlat) * math.sin(obliquity), math.cos(mlon))
    decl = math.asin(math.sin(mlat) * math.cos(obliquity) + math.cos(mlat) * math.sin(obliquity) * math.sin(mlon))
    sidereal = math.radians(280.46061837 + 360.98564736629 * n + lon)
    el, az = _elev_az(math.radians(lat), decl, sidereal - ra)
    return el - par * math.cos(math.radians(el)), az


def sky_x(az, el, lat):
    """The viewer faces the equator: the sun rises on the left and sets on the right in the
    northern hemisphere (mirrored in the southern); the east-west component projected on
    the view plane, so a body near the zenith stays in the middle."""
    e = math.sin(math.radians(az)) * math.cos(math.radians(el))
    if lat < 0:
        e = -e
    return int(round(31.5 - 25.5 * e))


# The sun and the moon rise and set on the far hills' line, not on the horizon row. The
# day's arc is one curve of the elevation, the old one lifted onto a raised horizon (the
# higher of the hill line's two ends) and running to row 18 at the zenith, so it stays
# round and symmetric. Where the hills under a body are lower than that, the body sinks
# to their line instead, the difference fading out over the first 5 degrees (it only ever
# lowers a body near the line, so a body never dips while rising); where they are higher,
# they simply hide it. The setting: at +0.7 degrees the disc rests on the line, by -0.83
# (the almanac's sunset: the upper limb at the horizon, refraction included) it has slid
# behind it, under a pixel a minute for the sun in New York.
RESTS, SET, BLEND = 0.7, -0.83, 5.0
ZENITH_Y = 18


def hill_line(far):
    """The far hills' top row in the frame per column, smoothed over five columns."""
    a = far.split()[-1].load()
    tops = [next((y for y in range(far.size[1]) if a[x, y] >= 128), far.size[1]) + HORIZON_Y - 12 for x in range(W)]
    return [sum(tops[min(W - 1, max(0, x + d))] for d in range(-2, 3)) / 5 for x in range(W)]


def body_y(el, x, radius, line):
    """The row of a body's centre (a disc of that radius) at column x, or None once set."""
    if el <= SET:
        return None
    raised = min(line[0], line[W - 1])
    local = max(line[min(W - 1, max(0, x))], raised)
    edge = raised + (local - raised) * max(0.0, 1 - (el - RESTS) / BLEND)
    rests, hidden = edge - radius - 1, edge + radius
    if el < RESTS:
        return int(round(hidden + (rests - hidden) * (el - SET) / (RESTS - SET)))
    return int(round(rests + (ZENITH_Y - rests) * (min(el, 90) - RESTS) / (90 - RESTS)))


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
STARS = [(5, 4), (14, 9), (22, 3), (30, 12), (41, 6), (50, 2), (57, 10), (9, 16), (36, 18), (60, 20), (26, 22), (47, 15), (3, 24), (54, 27)]
CLOUD_SPOTS = [(5, 20), (40, 28), (24, 14), (52, 10), (12, 32), (34, 22), (58, 30)]
HORIZON_Y = 46


def sky_palette(el):
    for a, b in zip(SKY, SKY[1:]):
        if a[0] <= el <= b[0]:
            t = (el - a[0]) / (b[0] - a[0])
            return tuple(lerp(a[i], b[i], t) for i in (1, 2, 3))
    return SKY[0][1:] if el < SKY[0][0] else SKY[-1][1:]


def daylight(el):
    return max(0.0, min(1.0, (el + 6) / 14))


def draw_horizon_sun():
    sun = Image.new("RGBA", (11, 11), (0, 0, 0, 0))
    d = ImageDraw.Draw(sun)
    d.ellipse((2, 2, 8, 8), fill=(255, 222, 90, 255))
    d.ellipse((3, 3, 7, 7), fill=(255, 240, 150, 255))
    for x, y in ((5, 0), (5, 10), (0, 5), (10, 5), (1, 1), (9, 9), (1, 9), (9, 1)):
        sun.putpixel((x, y), (255, 200, 70, 255))
    return sun


def draw_horizon_sun_low():
    """The sun within 6 degrees of the horizon: a red disc, no rays (tinted at draw time)."""
    sun = Image.new("RGBA", (11, 11), (0, 0, 0, 0))
    d = ImageDraw.Draw(sun)
    d.ellipse((2, 2, 8, 8), fill=(255, 255, 255, 255))
    return sun


def draw_horizon_cloud():
    cloud = Image.new("L", (13, 5), 0)
    d = ImageDraw.Draw(cloud)
    d.rectangle((1, 3, 11, 4), fill=255)
    d.rectangle((3, 1, 8, 2), fill=255)
    d.rectangle((5, 0, 6, 0), fill=255)
    d.rectangle((9, 2, 10, 2), fill=255)
    d.rectangle((0, 4, 12, 4), fill=255)
    return cloud


def draw_horizon_far_hills():
    far = Image.new("L", (W, 18), 0)
    d = ImageDraw.Draw(far)
    for x in range(W):
        top = 6 + int(round(4 * math.sin(x / 9.0) + 2 * math.sin(x / 4.0 + 1)))
        d.line((x, top, x, 17), fill=255)
    return far


def draw_horizon_near_hills():
    near = Image.new("L", (W, 18), 0)
    d = ImageDraw.Draw(near)
    for x in range(W):
        top = 11 + int(round(3 * math.sin(x / 13.0 + 2.5) + 1.5 * math.sin(x / 5.0)))
        d.line((x, top, x, 17), fill=255)
    return near


def draw_horizon_tree():
    tree = Image.new("L", (5, 8), 0)
    d = ImageDraw.Draw(tree)
    d.rectangle((2, 5, 2, 7), fill=255)
    d.polygon(((2, 0), (0, 4), (4, 4)), fill=255)
    d.polygon(((2, 2), (0, 5), (4, 5)), fill=255)
    return tree


def horizon_assets():
    return (asset("horizon", "sun", draw_horizon_sun), asset("horizon", "sun-low", draw_horizon_sun_low),
            asset("horizon", "cloud", draw_horizon_cloud), asset("horizon", "far-hills", draw_horizon_far_hills),
            asset("horizon", "near-hills", draw_horizon_near_hills), asset("horizon", "tree", draw_horizon_tree))


def draw_moon(frame, x, y, phase, lat, dark_sky):
    """A 9 px moon with its terminator; the lit side is the right one while waxing (mirrored
    in the southern hemisphere); the dark side shows faintly on a dark sky."""
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
            if not (0 <= X < W and 0 <= Y < HORIZON_Y):
                continue
            if lit:
                frame.set(X, Y, (236, 236, 248))
            elif dark_sky:
                frame.set(X, Y, lerp(frame.get(X, Y), (60, 62, 90), 0.6))


def draw_horizon(m, o, lat=40.0, lon=0.0, tz=0.0, year=2026, cover=0, precip=""):
    """`cover` 0..3 (clear, few, broken, overcast) and `precip` ("", "rain", "snow") come
    from the weather widget's current condition; without a location the firmware uses
    40 N, 0 E and the local time as solar time (the defaults here)."""
    h = m.hour + m.minute / 60.0
    frame = Frame((0, 0, 0))
    sun, sun_low, cloud, far, near, tree = horizon_assets()
    el, az = solar_position(lat, lon, tz, m.yday, h)
    top, near_c, away_c = sky_palette(el)
    grey = 0.22 * cover
    top, near_c, away_c = (lerp(c, (110, 116, 130), grey) for c in (top, near_c, away_c))
    sx = sky_x(az, el, lat)
    glow_width = 30 if el < 12 else 60
    for x in range(W):
        w = math.exp(-((x - sx) / glow_width) ** 2)
        hz = lerp(away_c, near_c, w)
        for y in range(HORIZON_Y):
            t = (y / (HORIZON_Y - 1)) ** 1.5
            frame.set(x, y, lerp(top, hz, t))
    light = daylight(el)
    star_k = max(0.0, min(1.0, (-el - 4) / 8)) * (1 - 0.8 * min(1, cover / 2))
    if star_k > 0:
        for i, (x, y) in enumerate(STARS):
            frame.set(x, y, lerp(frame.get(x, y), (232, 232, 250), star_k * (0.85 if i % 3 else 1.0)))
            if i % 3 == 0 and star_k > 0.6:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if 0 <= x + dx < W and 0 <= y + dy < HORIZON_Y:
                        frame.set(x + dx, y + dy, lerp(frame.get(x + dx, y + dy), (150, 150, 190), star_k))
    line = hill_line(far)
    phase = moon_phase(year, m.yday, h, tz)
    mel, maz = moon_position(lat, lon, tz, year, m.yday, h)
    mx = sky_x(maz, mel, lat)
    my = body_y(mel, mx, 4, line)
    if my is not None and el < 25:
        draw_moon(frame, mx, my, phase, lat, el < -6)
    sy = body_y(el, sx, 3, line)
    if sy is not None:
        if el < 6:
            k = max(0.0, el / 6)
            blit_tinted(frame, sun_low, sx - 5, sy - 5, lerp((255, 110, 60), (255, 222, 90), k))
            frame.fill_rect(sx - 1, sy - 1, 3, 3, lerp((255, 150, 90), (255, 240, 150), k))
        else:
            blit(frame, sun, sx - 5, sy - 5)
    tint = lerp((50, 52, 80), lerp((255, 255, 255), near_c, 0.35), light)
    if cover >= 3:
        tint = lerp((40, 42, 60), (168, 172, 186), light)
    for base_x, y in CLOUD_SPOTS[: (1, 2, 4, 7)[cover]]:
        x = (base_x + m.minute * 64 // 60) % (W + 13) - 13
        blit_tinted(frame, cloud, x, y, tint)
    if precip:
        rng = (m.minute * 7919 + m.hour * 104729) % 65536
        for _ in range(22):
            rng = (rng * 1103515245 + 12345) % 2147483648
            x, y = (rng >> 8) % W, (rng >> 4) % 44
            if precip == "rain":
                for k in range(3):
                    if y + k < HORIZON_Y:
                        frame.set(x, y + k, lerp(frame.get(x, y + k), (150, 190, 240), 0.7))
            else:
                frame.set(x, y, (240, 244, 255))
    blit_tinted(frame, far, 0, HORIZON_Y - 12, lerp(lerp((8, 14, 26), (66, 128, 78), light), away_c, 0.25 * light))
    blit_tinted(frame, near, 0, HORIZON_Y, lerp((4, 8, 14), (32, 84, 46), light))
    blit_tinted(frame, tree, 49, 43, (0, 0, 0))
    if precip == "snow" and light > 0:
        c = lerp((60, 64, 80), (225, 230, 240), light)
        for x in range(W):
            frame.set(x, HORIZON_Y, c)
    font = load_font("capital-hill")
    big = load_font("everyday-vast-black")  # the time, at its native 11 px
    hh = f"{m.hour:02d}" if o.h24 else str(m.h12())
    time_text = f"{hh}:{m.minute:02d}" if colon_on(m, o) else f"{hh} {m.minute:02d}"
    # one extra pixel between the characters; the outlines first, then the ink, as one string draws
    gap = 1
    spaced = text_width(big, time_text) + gap * (len(time_text) - 1)
    x0 = (W - spaced) // 2
    x_end = x0 + spaced
    for ink in (False, True):
        x = x0
        for ch in time_text:
            if ink:
                draw_text(frame, big, x, 4, ch, (255, 255, 255), 1)
            else:
                draw_text(frame, big, x, 4, ch, (0, 0, 0), 1, outline=(0, 0, 0))
            x += text_width(big, ch) + gap
    if not o.h24:
        draw_text(frame, font, x_end - text_width(font, m.meridiem()), 18, m.meridiem(), (255, 255, 255), 1, outline=(0, 0, 0))
    draw_centred(frame, font, 55, m.date(o.month_first), lerp((150, 160, 190), (230, 240, 230), light), 1, outline=(0, 0, 0))
    return frame


# ---------------------------------------------------------------- 4. Words

# A word clock: a 12x9 grid of letters, the ones that spell the time lit. The grid is
# p64's own: the minute words on top, the hours in the middle, O'CLOCK and AM/PM at the
# bottom; four corner dots add the minutes past the five.
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
WORDS_BG, WORDS_UNLIT, WORDS_LIT = (12, 12, 15), (42, 42, 50), (255, 250, 232)


def draw_words_bezel():
    """A brushed dark-steel frame, 3 px, with a lit inner edge and corner screws."""
    bezel = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(bezel)
    for i in range(3):
        v = (74, 72, 96, 58)[i]
        d.rectangle((i, i, W - 1 - i, H - 1 - i), outline=(v, v, v + 4, 255))
    for y in range(0, H, 3):
        for x in list(range(0, 3)) + list(range(W - 3, W)):
            r, g, b, a = bezel.getpixel((x, y))
            bezel.putpixel((x, y), (min(255, r + 14), min(255, g + 14), min(255, b + 14), 255))
    d.rectangle((3, 3, W - 4, H - 4), outline=(28, 28, 32, 255))
    for cx, cy in ((1, 1), (W - 2, 1), (1, H - 2), (W - 2, H - 2)):
        bezel.putpixel((cx, cy), (130, 130, 138, 255))
    return bezel


def words_assets():
    letters = dict(zip(sorted(ALPHABET), cells(asset("words", "alphabet", lambda: sheet_from([ALPHABET[k] for k in sorted(ALPHABET)])), 26)))
    return letters, asset("words", "bezel", draw_words_bezel)


def words_lit(hour, minute):
    """The words for a time, and the number of corner dots."""
    step, rem = minute // 5, minute % 5
    h12 = hour % 12 if step < 7 else (hour + 1) % 12
    active = {"IT", "IS", HOUR_WORDS[h12], "AM" if hour < 12 else "PM"} | set(MINUTE_WORDS[step])
    if step == 0:
        active.add("OCLOCK")
    return active, rem


def draw_words(m, o=None):
    frame = Frame(WORDS_BG)
    letters, bezel = words_assets()
    active, rem = words_lit(m.hour, m.minute)
    lit_cells = set()
    for w in active:
        r, c, n = WORDS[w]
        lit_cells |= {(r, c + i) for i in range(n)}
    for r, row in enumerate(WORD_ROWS):
        for c, ch in enumerate(row):
            stamp_mask(frame, letters[ch], 8 + c * 4, 5 + r * 6, WORDS_LIT if (r, c) in lit_cells else WORDS_UNLIT)
    for i, (x, y) in enumerate(((4, 4), (58, 4), (58, 58), (4, 58))):
        frame.fill_rect(x, y, 2, 2, WORDS_LIT if i < rem else WORDS_UNLIT)
    blit(frame, bezel, 0, 0)
    return frame


# ---------------------------------------------------------------- 5. Hourglass

# The hour as sand: the top bulb holds what is left of it, the bottom what has run, a
# stream between them grain by grain. The numerals are the flip's (a shared asset).
HG_BG, HG_INK, HG_DIM = (10, 10, 18), (226, 214, 190), (110, 104, 96)
HG_X, HG_Y = 3, 6  # the frame's top-left on the panel; the bulbs' axis at x = HG_X + 12


# The bulbs' half-widths per row of the frame: the top bulb rows 5..25, the neck 26, the
# bottom bulb 27..46 (9 - 8 * t^2.2 rounded, t along the bulb; face_hourglass.cpp has the
# same numbers).
TOP_WIDTHS = [9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 6, 6, 5, 5, 4, 3, 3, 2, 1]
BOTTOM_WIDTHS = [1, 2, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9]


def hourglass_widths():
    widths = {5 + i: w for i, w in enumerate(TOP_WIDTHS)}
    widths[26] = 1
    widths.update({27 + i: w for i, w in enumerate(BOTTOM_WIDTHS)})
    return widths


def draw_hourglass_frame():
    """The frame (26x52): walnut end plates with a highlight, two brass posts, the glass
    outline as one light line."""
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
    for y, hw in hourglass_widths().items():
        d.point((12 - hw - 1, y), fill=glass)
        d.point((12 + hw + 1, y), fill=glass)
    d.line((3, 5, 21, 5), fill=glass)
    d.line((3, 46, 21, 46), fill=glass)
    return frame


def draw_hourglass_sand():
    sand = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
    for y in range(4):
        for x in range(4):
            sand.putpixel((x, y), (232, 184, 84, 255) if (x + y) % 2 else (204, 150, 56, 255))
    return sand


def hourglass_assets():
    return asset("hourglass", "frame", draw_hourglass_frame), asset("hourglass", "sand", draw_hourglass_sand)


def draw_hourglass(m, o):
    frame = Frame(HG_BG)
    glass, sand = hourglass_assets()
    _tile, digits = flip_assets()
    widths = hourglass_widths()
    cx = HG_X + 12
    blit(frame, glass, HG_X, HG_Y)
    sand_px = sand.load()

    def sand_at(x, y):
        frame.set(x, y, sand_px[x % 4, y % 4][:3])

    elapsed = m.minute * 60 + (m.second if o.seconds else 0)  # seconds into the hour, 0..3599
    # the top bulb, filled from the neck up by what is left of the hour
    top_rows = [(y, widths[y]) for y in range(5, 26)]
    area = sum(2 * hw + 1 for _, hw in top_rows)
    filled = 0
    surface = None
    for y, hw in reversed(top_rows):
        if filled * 3600 >= area * (3600 - elapsed):
            break
        for x in range(cx - hw, cx + hw + 1):
            sand_at(x, HG_Y + y)
        filled += 2 * hw + 1
        surface = (y, hw)
    if surface and surface[1] >= 2:  # the funnel: the surface dips towards the middle
        for x in range(cx - 1, cx + 2):
            frame.set(x, HG_Y + surface[0], HG_BG)
    # the bottom bulb: a heap from the floor up by what has run, peaked under the neck
    bot_rows = [(y, widths[y]) for y in range(27, 47)]
    area = sum(2 * hw + 1 for _, hw in bot_rows)
    filled = 0
    heap_top = 47
    for y, hw in reversed(bot_rows):
        if filled * 3600 >= area * elapsed:
            break
        for x in range(cx - hw, cx + hw + 1):
            sand_at(x, HG_Y + y)
        filled += 2 * hw + 1
        heap_top = y
    if elapsed > 0:
        for k, hw in ((1, 2), (2, 0)):
            y = heap_top - k
            if y > 27:
                for x in range(cx - hw, cx + hw + 1):
                    sand_at(x, HG_Y + y)
        heap_top -= 2
    # the stream: one pixel wide from the neck to the heap; a grain missing every third
    # row, moving with the second (all grains when the seconds are off)
    for y in range(26, min(heap_top, 46)):
        if not o.seconds or (y + m.second) % 3 != 0:
            sand_at(cx, HG_Y + y)
    for text, y in ((hour_text(m, o), 7), (f"{m.minute:02d}", 27)):
        x = 40
        for ch in text:
            if ch != " ":
                stamp_mask(frame, digits[int(ch)], x, y, HG_INK)
            x += 11
    small = load_font("everyday-slight")
    draw_centred_at(frame, small, 49, 48, m.weekday if o.h24 else f"{m.weekday} {m.meridiem()}", HG_DIM)
    draw_centred_at(frame, small, 49, 55, f"{m.month} {m.mday}" if o.month_first else f"{m.mday} {m.month}", HG_DIM)
    return frame


# ---------------------------------------------------------------- 6. Orrery

# A brass orrery on a star chart: the Earth goes round the Sun once in twelve hours, the
# Moon round the Earth once an hour, Mercury round the Sun once a minute; the time in
# figures on a brass plaque at the foot. Positions come from a Q14 sine table in tenths of
# a degree (the same table as the firmware's, emitted by gen_clock_assets.py).
ORRERY_CX, ORRERY_CY = 31.5, 27.5
R_EARTH, R_MERCURY, R_MOON = 20, 10, 4
Q = 14
SIN_Q14 = [int(round(math.sin(math.radians(i / 10)) * (1 << Q))) for i in range(3600)]
ARM, ARM_DIM, PLAQUE_INK = (150, 112, 44), (110, 82, 34), (240, 208, 130)


def sin_q(i):
    return SIN_Q14[i % 3600]


def cos_q(i):
    return SIN_Q14[(i + 900) % 3600]


def q_to_px(v):
    """A Q14 coordinate to the pixel it falls in (round half up)."""
    return (v + (1 << (Q - 1))) >> Q


def draw_orrery_sun():
    sun = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
    d = ImageDraw.Draw(sun)
    d.ellipse((1, 1, 7, 7), fill=(255, 176, 40, 255))
    d.ellipse((2, 2, 6, 6), fill=(255, 226, 110, 255))
    d.ellipse((3, 3, 5, 5), fill=(255, 250, 200, 255))
    for x, y in ((4, 0), (4, 8), (0, 4), (8, 4)):
        sun.putpixel((x, y), (255, 150, 30, 255))
    return sun


def draw_orrery_earth():
    earth = Image.new("RGBA", (5, 5), (0, 0, 0, 0))
    ImageDraw.Draw(earth).ellipse((0, 0, 4, 4), fill=(48, 110, 220, 255))
    for x, y in ((1, 1), (2, 1), (1, 2), (3, 3), (2, 3)):
        earth.putpixel((x, y), (70, 170, 80, 255))
    earth.putpixel((2, 0), (180, 210, 255, 255))
    return earth


def draw_orrery_moon():
    moon = Image.new("RGBA", (3, 3), (0, 0, 0, 0))
    ImageDraw.Draw(moon).ellipse((0, 0, 2, 2), fill=(214, 214, 224, 255))
    moon.putpixel((1, 1), (170, 170, 184, 255))
    return moon


def draw_orrery_mercury():
    mercury = Image.new("RGBA", (3, 3), (0, 0, 0, 0))
    ImageDraw.Draw(mercury).ellipse((0, 0, 2, 2), fill=(200, 150, 110, 255))
    return mercury


def draw_orrery_plate():
    """The star chart with the brass rings (dotted circles), twelve ticks and the plaque."""
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
    return plate


def orrery_assets():
    return (asset("orrery", "sun", draw_orrery_sun), asset("orrery", "earth", draw_orrery_earth),
            asset("orrery", "moon", draw_orrery_moon), asset("orrery", "mercury", draw_orrery_mercury),
            asset("orrery", "plate", draw_orrery_plate))


def orrery_positions(m):
    """Pixel centres of the Earth, the Moon and Mercury (face_orrery.cpp does the same)."""
    cx_q, cy_q = int(ORRERY_CX * (1 << Q)), int(ORRERY_CY * (1 << Q))
    ih = (m.hour % 12) * 300 + m.minute * 5  # tenths of a degree: 30 degrees an hour
    im = m.minute * 60 + m.second            # 6 degrees a minute
    isec = m.second * 60                     # 6 degrees a second
    ex_q, ey_q = cx_q + R_EARTH * sin_q(ih), cy_q - R_EARTH * cos_q(ih)
    mx_q, my_q = ex_q + R_MOON * sin_q(im), ey_q - R_MOON * cos_q(im)
    qx_q, qy_q = cx_q + R_MERCURY * sin_q(isec), cy_q - R_MERCURY * cos_q(isec)
    return (q_to_px(ex_q), q_to_px(ey_q)), (q_to_px(mx_q), q_to_px(my_q)), (q_to_px(qx_q), q_to_px(qy_q))


def draw_orrery(m, o):
    frame = Frame((0, 0, 0))
    sun, earth, moon, mercury, plate = orrery_assets()
    blit(frame, plate, 0, 0)
    (ex, ey), (mx, my), (qx, qy) = orrery_positions(m)
    hub = (31, 27)
    frame.line(hub[0], hub[1], ex, ey, ARM)
    if o.seconds:
        frame.line(hub[0], hub[1], qx, qy, ARM_DIM)
    frame.line(ex, ey, mx, my, ARM)
    blit(frame, sun, 27, 23)
    blit(frame, earth, ex - 2, ey - 2)
    blit(frame, moon, mx - 1, my - 1)
    if o.seconds:
        blit(frame, mercury, qx - 1, qy - 1)
    font = load_font("capital-hill")
    hh = f"{m.hour:02d}" if o.h24 else str(m.h12())
    draw_centred(frame, font, 57, f"{hh}:{m.minute:02d}" if colon_on(m, o) else f"{hh} {m.minute:02d}", PLAQUE_INK)
    if not o.h24:
        draw_text(frame, font, 63 - text_width(font, m.meridiem()), 57, m.meridiem(), ARM)
    return frame


# ---------------------------------------------------------------- 7. LED (the seven-segment face, five styles)

# A seven-segment clock (prompts p052 and p053, 2026-09-28) in VEXED's Digital Display
# (assets/fonts/digital-display: 15x19 seven-segment digits with bevelled ends, a 3x19
# colon), rasterised once from the TTF at its native 19 px into assets/clock/led/digits.png
# and colon.png; the font is only right at integer multiples of 19 px, and HH:MM in one row
# is 67 px, so the hours sit at the upper left and the minutes at the lower right (a
# staircase), the seconds in two 5x9 seven-segment digits of the same style at the lower
# left, the indicators (AM, PM, ALM) at the upper right. Five styles (the `led_style`
# setting): red, green, amber and blue LEDs behind a tinted filter in a dark plastic bezel
# with the labels printed on the filter and a lit dot beside each; and vfd, a cyan-green
# vacuum fluorescent display on dark glass with a faint mesh in a chrome frame, where the
# indicator words themselves light up, a bell marks ALM and a six-bar meter dances in the
# band between the rows. Everything lit is first written into a 0..255 intensity map over a
# ghost map (the unlit segments, an "8" behind every digit, as on a real display); the face
# is then coloured from them: a lit pixel between the ghost and the lit colour by its
# intensity, and around the lit pixels a one-pixel glow (the brightest of the eight
# neighbours, scaled by a slow pulse). A digit change cross-fades the segments: FADE_FRAMES
# frames of FADE_MS in which the segments that go out fall from lit to ghost and the ones
# that come in rise, the shared ones staying lit. The face's own clock is the local time of
# day in milliseconds (`seg_ms`), so the pulse and the meter are functions of the moment
# and every frame is reproducible; the firmware redraws every PULSE_STEP_MS. Integer
# arithmetic throughout.

DD_FONT = os.path.join(FIRMWARE, "assets", "fonts", "digital-display", "Digital_Display.ttf")
DW, DH = 15, 19  # a Digital Display digit
CW = 3  # its colon
MW, MH = 5, 9  # a mini digit
FADE_FRAMES, FADE_MS = 5, 40
PULSE_MS, PULSE_STEP_MS = 4000, 200
WIN = (3, 3, 61, 61)  # the window inside the 3 px frame, [x0, y0, x1, y1)
SEG_HOURS = (6, 7)  # the hours' first digit
SEG_MINUTES = (27, 38)  # the minutes' first digit (right-aligned to x 57)
SEG_COLON = (38, 7)  # after the hours
SEG_SECONDS = (7, 48)  # the mini digits, bottom-aligned with the minutes
SEG_ROWS = (8, 15, 22)  # the indicator rows (AM, PM, ALM), 3x5 letters at x 47
SEG_LABEL_X, SEG_DOT_X = 47, 43
VU_X, VU_Y, VU_BARS, VU_H = 6, 35, 6, 6  # the VFD's meter: bottom row, six 2 px bars, 6 rows
LED_STYLES = ["red", "green", "amber", "blue", "vfd"]

# The mini seven-segment digits: segment a..g as (rows, cols) of a 5x9 cell.
MINI_SEGMENTS = {"a": ((0,), (1, 2, 3)), "b": ((1, 2, 3), (4,)), "c": ((5, 6, 7), (4,)), "d": ((8,), (1, 2, 3)),
                 "e": ((5, 6, 7), (0,)), "f": ((1, 2, 3), (0,)), "g": ((4,), (1, 2, 3))}
MINI_DIGITS = ["abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg"]
BELL = ["..#..", ".###.", ".###.", "#####", "..#.."]


class SegStyle:
    """window: the filter or glass; window_alt: the glass's other rows (the VFD mesh);
    ghost, lit, glow: the segments; label: the printed labels' colour (None: the words light
    up, VFD)."""

    def __init__(self, window, window_alt, ghost, lit, glow, label):
        self.window, self.window_alt, self.ghost, self.lit, self.glow, self.label = window, window_alt, ghost, lit, glow, label

    @property
    def vfd(self):
        return self.label is None


STYLES = {
    # The ghost and the glow are half the first design (p054 and p055, 2026-09-28, tuned on
    # the panel: on the LED matrix the dark end is lifted, so the first values competed with
    # the lit segments and a third of them was too faint to notice). That lift was the
    # driver's window bug (fixed 2026-10-02, darks two to three times too bright); after
    # the fix the ghosts were too faint and came up a quarter (p066).
    "red": SegStyle((10, 2, 2), None, (29, 5, 4), (255, 48, 24), (40, 7, 3), (128, 78, 72)),
    "green": SegStyle((2, 8, 3), None, (5, 25, 8), (60, 255, 70), (6, 40, 9), (76, 120, 84)),
    "amber": SegStyle((10, 6, 1), None, (30, 18, 3), (255, 160, 24), (42, 26, 3), (128, 104, 70)),
    "blue": SegStyle((2, 3, 12), None, (5, 9, 33), (60, 120, 255), (5, 13, 48), (80, 90, 130)),
    "vfd": SegStyle((5, 11, 11), (3, 8, 8), (6, 21, 19), (150, 255, 225), (10, 33, 29), None),
}
LED_PLASTIC, LED_LIGHT, LED_DARK, LED_INNER = (46, 46, 52), (92, 92, 100), (14, 14, 16), (24, 24, 28)
CHROME_HI, CHROME, CHROME_LO, CHROME_EDGE = (236, 240, 244), (168, 174, 182), (96, 102, 110), (38, 42, 48)


def mix(a, b, v):
    """a towards b by v/255, integer (the firmware's blend: C++ division rounds toward zero,
    which differs from // when b is darker than a, as a glow beside a brighter ghost is)."""
    return tuple(a[i] + int((b[i] - a[i]) * v / 255) for i in range(3))


def mask_rows(mask):
    px = mask.load()
    return ["".join("#" if px[x, y] else "." for x in range(mask.width)) for y in range(mask.height)]


def dd_masks():
    """The ten digits and the colon of Digital Display at 19 px, straight from the TTF (only
    when the asset PNGs do not exist yet; gen_fonts.py does not rasterise this font, it is
    too tall for the overlay and 2x would be 38 px)."""
    from PIL import ImageFont
    font = ImageFont.truetype(DD_FONT, 19)

    def cell(ch, w):
        img = Image.new("L", (w + 8, 40), 0)
        ImageDraw.Draw(img).text((0, 0), ch, font=font, fill=255)
        return img.crop((0, 8, w, 27)).point(lambda v: 255 if v >= 128 else 0)  # the ascender is 8 px above the digits

    return [cell(str(i), DW) for i in range(10)], cell(":", CW)


def mini_rows(segments):
    rows = [["."] * MW for _ in range(MH)]
    for s in segments:
        for r in MINI_SEGMENTS[s][0]:
            for c in MINI_SEGMENTS[s][1]:
                rows[r][c] = "#"
    return ["".join(r) for r in rows]


def words_letters():
    """The words face's 3x5 alphabet, shared by the LED face's indicator words."""
    return dict(zip(sorted(ALPHABET), cells(asset("words", "alphabet", lambda: sheet_from([ALPHABET[k] for k in sorted(ALPHABET)])), 26)))


def seg_assets():
    """digits (ten 15x19 masks), colon (3x19), mini (ten 5x9), letters (the words face's 3x5)."""
    digits = cells(asset("led", "digits", lambda: sheet_from([mask_rows(d) for d in dd_masks()[0]])), 10)
    colon = asset("led", "colon", lambda: dd_masks()[1]).split()[3].point(lambda v: 255 if v >= 128 else 0)
    mini = cells(asset("led", "mini", lambda: sheet_from([mini_rows(s) for s in MINI_DIGITS])), 10)
    return digits, colon, mini, words_letters()


def frame_outline(img, hi, body, lo, inner_top, inner_bottom):
    """A 3 px frame with rounded corners and the window clear: the outer edge lit at the
    top and left (hi) and dark at the bottom and right (lo), the body, the inner edge."""
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), fill=body + (255,))
    d.line((0, 0, W - 1, 0), fill=hi + (255,))
    d.line((0, 0, 0, H - 1), fill=hi + (255,))
    d.line((0, H - 1, W - 1, H - 1), fill=lo + (255,))
    d.line((W - 1, 0, W - 1, H - 1), fill=lo + (255,))
    d.line((2, 2, W - 3, 2), fill=inner_top + (255,))
    d.line((2, 2, 2, H - 3), fill=inner_top + (255,))
    d.line((2, H - 3, W - 3, H - 3), fill=inner_bottom + (255,))
    d.line((W - 3, 2, W - 3, H - 3), fill=inner_bottom + (255,))
    d.rectangle((WIN[0], WIN[1], WIN[2] - 1, WIN[3] - 1), fill=(0, 0, 0, 0))
    for x, y in ((0, 0), (W - 1, 0), (0, H - 1), (W - 1, H - 1)):
        img.putpixel((x, y), (0, 0, 0, 0))


def draw_led_bezel():
    """The LEDs' housing, 64x64 RGBA with the window clear: dark plastic, the light on the
    top and left edges, the inner edge of the recess dark above and light below."""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    frame_outline(img, LED_LIGHT, LED_PLASTIC, LED_DARK, LED_INNER, (60, 60, 68))
    return img


def draw_vfd_frame():
    """The VFD's chrome frame: a bright top-left edge, the body, a darker bottom-right, a
    dark line where the glass meets it."""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    frame_outline(img, CHROME_HI, CHROME, CHROME_LO, CHROME_EDGE, CHROME_EDGE)
    d = ImageDraw.Draw(img)
    d.line((1, H - 2, W - 2, H - 2), fill=(130, 136, 144, 255))
    d.line((W - 2, 1, W - 2, H - 2), fill=(130, 136, 144, 255))
    return img


def seg_stamp(grid, mask, x, y, value):
    """max() the mask's ink into the grid at `value`."""
    px = mask.load()
    for yy in range(mask.height):
        for xx in range(mask.width):
            if px[xx, yy] and 0 <= x + xx < W and 0 <= y + yy < H:
                grid[y + yy][x + xx] = max(grid[y + yy][x + xx], value)


def seg_digit(lit, ghost, masks, x, y, new, old, t):
    """A digit slot: the ghost 8 behind, then `new` lit; during a change (`t` 1..254) the
    pixels only in `old` at 255 - t, the ones only in `new` at t, the shared ones at 255."""
    seg_stamp(ghost, masks[8], x, y, 1)
    m_new = masks[int(new)] if new != " " else None
    m_old = masks[int(old)] if old != " " else None
    if new == old or not 0 < t < 255:
        if m_new:
            seg_stamp(lit, m_new, x, y, 255)
        return
    pn = m_new.load() if m_new else None
    po = m_old.load() if m_old else None
    for yy in range(masks[8].height):
        for xx in range(masks[8].width):
            a, b = bool(po and po[xx, yy]), bool(pn and pn[xx, yy])
            if a or b:
                lit[y + yy][x + xx] = max(lit[y + yy][x + xx], 255 if a and b else (t if b else 255 - t))


def seg_word(lit, ghost, letters, x, y, word, on):
    for ch in word:
        seg_stamp(ghost, letters[ch], x, y, 1)
        if on:
            seg_stamp(lit, letters[ch], x, y, 255)
        x += 4


def seg_dot(lit, ghost, x, y, on):
    for yy in range(2):
        for xx in range(2):
            ghost[y + yy][x + xx] = 1
            if on:
                lit[y + yy][x + xx] = 255


def seg_ms(m, millis=0):
    """The face's clock: the local time of day in milliseconds."""
    return ((m.hour * 60 + m.minute) * 60 + m.second) * 1000 + millis


def pulse(ms):
    """The glow's breathing: 255 at 0, 140 at PULSE_MS / 2, back, a triangle."""
    ph = ms % PULSE_MS
    tri = ph if ph < PULSE_MS // 2 else PULSE_MS - ph
    return 255 - (115 * tri) // (PULSE_MS // 2)


def tri255(ms, period):
    ph = ms % period
    return (2 * ph if ph < period // 2 else 2 * (period - ph)) * 255 // period


def vu_height(ms, i):
    """The meter's bar i, 1..6 rows: two triangles of unrelated periods, so it never repeats soon."""
    return 1 + ((VU_H - 1) * (tri255(ms, 900 + 170 * i) + tri255(ms, 1300 + 230 * i))) // 510


def seg_compose(style, lit, ghost, ms):
    """The window coloured from the maps: base (glass, ghost), lit by intensity, the glow of
    the eight neighbours scaled by the pulse."""
    frame = Frame((0, 0, 0))
    p = pulse(ms)
    x0, y0, x1, y1 = WIN
    for y in range(y0, y1):
        for x in range(x0, x1):
            base = style.window if style.window_alt is None or y % 2 == 0 else style.window_alt
            if ghost[y][x]:
                base = style.ghost
            v = lit[y][x]
            if v:
                frame.set(x, y, mix(base, style.lit, v))
                continue
            n = 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if 0 <= y + dy < H and 0 <= x + dx < W:
                        n = max(n, lit[y + dy][x + dx])
            frame.set(x, y, mix(base, style.glow, (n * p) // 255) if n else base)
    return frame


def draw_led(m, o, millis=0, prev=None, k=0, style="red"):
    """The face at `m` and `millis` into its second; `k` 1..FADE_FRAMES draws that frame of
    the change from `prev` (every digit that differs cross-fades)."""
    st = STYLES[style]
    digits, colon, mini, letters = seg_assets()
    lit = [[0] * W for _ in range(H)]
    ghost = [[0] * W for _ in range(H)]
    t = (255 * k) // FADE_FRAMES if prev else 0
    prev = prev or m
    hh, ph = hour_text(m, o), hour_text(prev, o)
    mm, pm = f"{m.minute:02d}", f"{prev.minute:02d}"
    for i in range(2):
        seg_digit(lit, ghost, digits, SEG_HOURS[0] + i * (DW + 1), SEG_HOURS[1], hh[i], ph[i], t)
        seg_digit(lit, ghost, digits, SEG_MINUTES[0] + i * (DW + 1), SEG_MINUTES[1], mm[i], pm[i], t)
    seg_stamp(ghost, colon, SEG_COLON[0], SEG_COLON[1], 1)
    if colon_on(m, o):
        seg_stamp(lit, colon, SEG_COLON[0], SEG_COLON[1], 255)
    if o.seconds:
        ss, ps = f"{m.second:02d}", f"{prev.second:02d}"
        for i in range(2):
            seg_digit(lit, ghost, mini, SEG_SECONDS[0] + i * (MW + 1), SEG_SECONDS[1], ss[i], ps[i], t)
    am, pm_on = (not o.h24 and m.hour < 12), (not o.h24 and m.hour >= 12)
    ms = seg_ms(m, millis)
    if st.vfd:
        for y, word, on in zip(SEG_ROWS, ("AM", "PM", "ALM"), (am, pm_on, True)):
            seg_word(lit, ghost, letters, SEG_LABEL_X, y, word, on)
        seg_stamp(ghost, bitmap(BELL), SEG_DOT_X - 2, SEG_ROWS[2], 1)
        seg_stamp(lit, bitmap(BELL), SEG_DOT_X - 2, SEG_ROWS[2], 255)
        for i in range(VU_BARS):
            h = vu_height(ms, i)
            for r in range(VU_H):
                for xx in range(2):
                    ghost[VU_Y - r][VU_X + 3 * i + xx] = 1
                    if r < h:
                        lit[VU_Y - r][VU_X + 3 * i + xx] = 255
    else:
        for y, on in zip(SEG_ROWS, (am, pm_on, True)):
            seg_dot(lit, ghost, SEG_DOT_X, y + 1, on)
    frame = seg_compose(st, lit, ghost, ms)
    if st.vfd:
        blit(frame, asset("led", "vfd-frame", draw_vfd_frame), 0, 0)
    else:
        blit(frame, asset("led", "bezel", draw_led_bezel), 0, 0)
        for y, word in zip(SEG_ROWS, ("AM", "PM", "ALM")):  # printed on the filter
            for i, ch in enumerate(word):
                stamp_mask(frame, letters[ch], SEG_LABEL_X + 4 * i, y, st.label)
    return frame


def plus_seconds(m, s):
    total = (m.hour * 3600 + m.minute * 60 + m.second + s) % 86400
    return Moment(total // 3600, total // 60 % 60, total % 60, m.wday, m.mday, m.mon, m.yday)


def led_animation(m0, o, style="red", seconds=8):
    """(frame, duration ms) for `seconds` seconds from m0, as the firmware draws them: at
    every second the FADE_FRAMES frames of the change 40 ms apart, then a frame every
    PULSE_STEP_MS in which the pulse and the meter move."""
    seq = []
    prev = None
    for s in range(seconds):
        m = plus_seconds(m0, s)
        millis = 0
        if prev:
            for k in range(1, FADE_FRAMES + 1):
                seq.append((draw_led(m, o, millis, prev, k, style), FADE_MS))
                millis += FADE_MS
        while millis < 1000:
            seq.append((draw_led(m, o, millis, None, 0, style), PULSE_STEP_MS))
            millis += PULSE_STEP_MS
        prev = m
    return seq


# ---------------------------------------------------------------- output

FACES = {"flip": draw_flip, "nixie": draw_nixie, "horizon": draw_horizon, "words": draw_words,
         "hourglass": draw_hourglass, "orrery": draw_orrery, "led": draw_led}


def contact_sheet(items, scale=6, gap=12, columns=None):
    font = load_font("everyday-typical")
    cw = W * scale
    columns = columns or len(items)
    rows = (len(items) + columns - 1) // columns
    ch = cw + gap + 30
    sheet = Image.new("RGB", (columns * (cw + gap) + gap, rows * ch + gap), (24, 24, 28))
    for i, (name, fr) in enumerate(items):
        x = gap + (i % columns) * (cw + gap)
        y = gap + (i // columns) * ch
        sheet.paste(upscale(fr.img, scale), (x, y))
        label = Frame((24, 24, 28))
        draw_text(label, font, 0, 0, name.upper(), (220, 220, 230))
        sheet.paste(upscale(label.img.crop((0, 0, min(64, text_width(font, name.upper()) + 1), 8)), 3), (x, y + cw + 8))
    return sheet


def save_gif(path, seq, scale=1):
    frames = [upscale(f.img, scale).quantize(colors=64, dither=Image.NONE) for f, _ in seq]
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=[d for _, d in seq], loop=0, optimize=False)


# The references (tests/host/run.py renders the same): (face, moment, flags).
REFERENCES = [
    ("flip", Moment(10, 32, 37), "s"), ("flip", Moment(10, 32, 0), ""), ("flip", Moment(19, 32, 5), "h"),
    ("flip", Moment(0, 15, 0), "h"), ("flip", Moment(11, 0, 0), "p3"), ("flip", Moment(11, 0, 0), "p7"),
    ("flip", Moment(10, 33, 0), "p5"),
    ("nixie", Moment(10, 32, 37), ""), ("nixie", Moment(10, 32, 37), "b"), ("nixie", Moment(19, 32, 5), "h"),
    ("nixie", Moment(9, 5, 0), "h"),
    ("horizon", Moment(18, 42, 0), ""), ("horizon", Moment(12, 30, 0), ""), ("horizon", Moment(3, 0, 0), ""),
    ("horizon", Moment(6, 20, 0), "h"),
    ("words", Moment(10, 32, 0), ""), ("words", Moment(7, 15, 0), ""), ("words", Moment(14, 45, 0), ""),
    ("words", Moment(19, 58, 0), ""), ("words", Moment(0, 0, 0), ""), ("words", Moment(23, 30, 0), ""),
    ("hourglass", Moment(10, 32, 37), "s"), ("hourglass", Moment(10, 32, 37), ""), ("hourglass", Moment(0, 0, 0), "s"),
    ("hourglass", Moment(19, 58, 5), "s"), ("hourglass", Moment(7, 15, 12), "hs"),
    ("orrery", Moment(10, 32, 37), "s"), ("orrery", Moment(10, 32, 37), ""), ("orrery", Moment(0, 0, 0), "s"),
    ("orrery", Moment(14, 45, 50), "s"), ("orrery", Moment(19, 32, 5), "hs"), ("orrery", Moment(10, 32, 37), "b"),
    # the LED face: flags g/a/u/v pick the green, amber, blue and vfd styles (red without),
    # pN frame N of the cross-fade from the second before
    ("led", Moment(10, 32, 37), "s"), ("led", Moment(10, 32, 37), ""), ("led", Moment(10, 32, 37), "sb"),
    ("led", Moment(19, 32, 5), "hs"), ("led", Moment(9, 5, 0), "h"), ("led", Moment(0, 0, 0), "s"),
    ("led", Moment(10, 33, 0), "sp3"), ("led", Moment(10, 32, 38), "sp1"), ("led", Moment(10, 32, 38), "sp4"),
    ("led", Moment(11, 0, 0), "p2"),
    ("led", Moment(10, 32, 37), "sv"), ("led", Moment(21, 5, 9), "hsv"), ("led", Moment(10, 32, 37), "bv"),
    ("led", Moment(10, 33, 0), "svp2"),
    ("led", Moment(10, 32, 37), "sg"), ("led", Moment(10, 32, 37), "sa"), ("led", Moment(10, 32, 37), "su"),
]


def render_reference(face, m, flags):
    o = Options(seconds="s" in flags, blink="b" in flags, h24="h" not in flags)
    p = re.search(r"p(\d+)", flags)
    if face == "flip" and p:
        k = int(p.group(1))
        prev = Moment(m.hour if m.minute else (m.hour - 1) % 24, (m.minute - 1) % 60, 59)
        return draw_flip(m, o, k, prev)
    if face == "led":
        style = {"g": "green", "a": "amber", "u": "blue", "v": "vfd"}
        st = next((style[c] for c in flags if c in style), "red")
        return draw_led(m, o, 0, plus_seconds(m, -1) if p else None, int(p.group(1)) if p else 0, st)
    return FACES[face](m, o)


def write_references():
    os.makedirs(CORPUS, exist_ok=True)
    for f in os.listdir(CORPUS):
        os.remove(os.path.join(CORPUS, f))
    for face, m, flags in REFERENCES:
        name = f"{face}-{m.hour:02d}{m.minute:02d}{m.second:02d}" + (f"-{flags}" if flags else "") + ".png"
        render_reference(face, m, flags).img.save(os.path.join(CORPUS, name))
    return len(REFERENCES)


def write_design():
    os.makedirs(DESIGN, exist_ok=True)
    o = Options()
    faces = [("flip", draw_flip(Moment(10, 32, 37), Options(seconds=True))),
             ("nixie", draw_nixie(Moment(10, 32, 37), o)),
             ("horizon", draw_horizon(Moment(18, 42), o, lat=40.7, lon=-74.0, tz=-4)),
             ("words", draw_words(Moment(10, 32))),
             ("hourglass", draw_hourglass(Moment(10, 32, 37), Options(seconds=True))),
             ("orrery", draw_orrery(Moment(10, 32, 37), Options(seconds=True))),
             ("led", draw_led(Moment(10, 32, 37), Options(seconds=True)))]
    for name, fr in faces:
        fr.img.save(os.path.join(DESIGN, name + ".png"))
        upscale(fr.img, 8).save(os.path.join(DESIGN, name + "@8x.png"))
    contact_sheet(faces, scale=5, columns=3).save(os.path.join(DESIGN, "contact-sheet.png"))
    for name, seq in (("flip-minute", flip_animation(Moment(10, 32), Moment(10, 33), o)),
                      ("flip-hour", flip_animation(Moment(10, 59), Moment(11, 0), o))):
        save_gif(os.path.join(DESIGN, name + ".gif"), seq)
        save_gif(os.path.join(DESIGN, name + "@8x.gif"), seq, 8)
    ny = dict(lat=40.7, lon=-74.0, tz=-4)
    moments = [(0, 30), (3, 0), (5, 30), (6, 20), (6, 50), (8, 0), (12, 30), (16, 30), (18, 20), (18, 50), (19, 25), (21, 0)]
    contact_sheet([(f"{hh:02d}:{mm:02d}", draw_horizon(Moment(hh, mm), o, **ny)) for hh, mm in moments], scale=4, columns=6).save(
        os.path.join(DESIGN, "horizon-day.png"))
    variants = [("clear", draw_horizon(Moment(15, 0), o, **ny)),
                ("broken", draw_horizon(Moment(15, 0), o, cover=2, **ny)),
                ("rain", draw_horizon(Moment(15, 0), o, cover=3, precip="rain", **ny)),
                ("snow", draw_horizon(Moment(15, 0), o, cover=3, precip="snow", **ny)),
                ("sao paulo", draw_horizon(Moment(18, 10), o, lat=-23.55, lon=-46.63, tz=-3)),
                ("tromso dec", draw_horizon(Moment(13, 0, 0, 1, 21, 12, 354), o, lat=69.65, lon=18.96, tz=1)),
                ("reykjavik jun", draw_horizon(Moment(23, 30, 0, 0, 21, 6, 171), o, lat=64.15, lon=-21.94, tz=0)),
                ("singapore", draw_horizon(Moment(12, 0), o, lat=1.35, lon=103.8, tz=8))]
    contact_sheet(variants, scale=4, columns=4, gap=24).save(os.path.join(DESIGN, "horizon-variants.png"))
    times = [(0, 0, 0), (7, 15, 12), (10, 32, 37), (14, 45, 50), (19, 58, 5), (23, 30, 20)]
    for name, fn in (("words", lambda mm: draw_words(mm)),
                     ("hourglass", lambda mm: draw_hourglass(mm, Options(seconds=True))),
                     ("orrery", lambda mm: draw_orrery(mm, Options(seconds=True)))):
        contact_sheet([(f"{h:02d}:{m:02d}", fn(Moment(h, m, s))) for h, m, s in times], scale=4, columns=6).save(
            os.path.join(DESIGN, name + "-moments.png"))
    save_gif(os.path.join(DESIGN, "hourglass@8x.gif"), [(draw_hourglass(Moment(10, 32, s), Options(seconds=True)), 500) for s in range(6)], 8)
    save_gif(os.path.join(DESIGN, "orrery@8x.gif"), [(draw_orrery(Moment(10, 32, s), Options(seconds=True)), 500) for s in range(30, 42)], 8)
    # The LED face (p052, p053): the five styles, six moments in 12 h with seconds, and eight
    # seconds across a minute change with the fades, the pulse and the meter, red and vfd.
    seg_o = Options(seconds=True, blink=True)
    contact_sheet([(st, draw_led(Moment(10, 32, 37), Options(seconds=True), 0, None, 0, st)) for st in LED_STYLES], scale=4, columns=5).save(
        os.path.join(DESIGN, "led-styles.png"))
    contact_sheet([(f"{h:02d}:{m:02d}", draw_led(Moment(h, m, s), Options(seconds=True, h24=False))) for h, m, s in times], scale=4, columns=6).save(
        os.path.join(DESIGN, "led-moments.png"))
    vfd = draw_led(Moment(10, 32, 37), Options(seconds=True), 0, None, 0, "vfd")
    vfd.img.save(os.path.join(DESIGN, "led-vfd.png"))
    upscale(vfd.img, 8).save(os.path.join(DESIGN, "led-vfd@8x.png"))
    for name, st in (("led", "red"), ("led-vfd", "vfd")):
        seq = led_animation(Moment(10, 31, 57), seg_o, st)
        save_gif(os.path.join(DESIGN, name + ".gif"), seq)
        save_gif(os.path.join(DESIGN, name + "@8x.gif"), seq, 8)


def main():
    n = write_references()
    write_design()
    print(f"assets in {ASSETS}; {n} references in {CORPUS}; review images in {DESIGN}")


if __name__ == "__main__":
    main()
