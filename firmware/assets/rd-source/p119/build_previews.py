"""p119: builds the preview mocks (stills at 8x, GIFs at 4x, alternate sheets) in ./previews."""
import math
import random
import re
import sys
from pathlib import Path

from PIL import Image

FIRMWARE = Path(r"C:\Users\fab\F\Estudo\Tecnologia\p64\firmware")
SRC = FIRMWARE / "assets" / "rd-source" / "p119"
OUT = Path(__file__).parent / "previews"  # git-ignored scratch output
OUT.mkdir(exist_ok=True)
FONTS_DATA = FIRMWARE / "components" / "p64_gfx" / "src" / "fonts_data.cpp"


# ---- the firmware's fonts (same parser as tools/mock_clock_faces.py)
class Font:
    def __init__(self, name, size, top, bottom, first, last, glyphs, bits):
        self.name, self.size, self.top, self.bottom = name, size, top, bottom
        self.first, self.last, self.glyphs, self.bits = first, last, glyphs, bits

    def glyph(self, ch):
        code = ord(ch)
        return None if code < self.first or code > self.last else self.glyphs[code - self.first]

    def bit(self, g, col, row):
        _c, _a, width, _h, _x, _y, off = g
        return (self.bits[off + row * ((width + 7) // 8) + col // 8] >> (7 - col % 8)) & 1


_FONTS = {}


def font(name):
    if not _FONTS:
        text = FONTS_DATA.read_text(encoding="utf-8")
        for m in re.finditer(r"const Font k_(\w+) = \{\"([\w-]+)\", \"[^\"]*\", (\d+), \w+, (\d+), (\d+), (\d+), (\d+), (\d+)", text):
            ident, fname, size, top, bottom, _b, first, last = m.groups()
            bits_src = re.search(r"const uint8_t k_%s_bits\[\] = \{(.*?)\};" % ident, text, re.S).group(1)
            bits = bytes(int(v, 16) for v in re.findall(r"0x[0-9a-f]{2}", bits_src))
            gs = re.search(r"const Glyph k_%s_glyphs\[\] = \{(.*?)\};" % ident, text, re.S).group(1)
            glyphs = [tuple(int(v) for v in g) for g in re.findall(r"\{(\d+), (\d+), (\d+), (\d+), (-?\d+), (-?\d+), (\d+)\}", gs)]
            _FONTS[fname] = Font(fname, int(size), int(top), int(bottom), int(first), int(last), glyphs, bits)
    return _FONTS[name]


def text_width(f, text):
    return sum((g[1] if (g := f.glyph(ch)) else f.size) for ch in text)


def draw_text(img, f, x, y, text, colour, halo=None):
    px = img.load()
    W, H = img.size

    def put(xx, yy, c):
        if 0 <= xx < W and 0 <= yy < H:
            px[xx, yy] = c

    for pass_halo in ((True, False) if halo else (False,)):
        cx = x
        for ch in text:
            g = f.glyph(ch)
            if not g:
                cx += f.size
                continue
            _c, adv, width, height, x_off, y_off, _o = g
            for row in range(height):
                for col in range(width):
                    if not f.bit(g, col, row):
                        continue
                    X, Y = cx + x_off + col, y + y_off - f.top + row
                    if pass_halo:
                        for dx in (-1, 0, 1):
                            for dy in (-1, 0, 1):
                                put(X + dx, Y + dy, halo)
                    else:
                        put(X, Y, colour)
            cx += adv
    return text_width(f, text)


def draw_centred(img, f, cx, y, text, colour, halo=None):
    return draw_text(img, f, cx - text_width(f, text) // 2, y, text, colour, halo)


# ---- images
def load(name, crop_middle=False):
    im = Image.open(SRC / name).convert("RGB")
    if crop_middle and im.size != (64, 64):
        x0 = (im.width - 64) // 2 + 1
        y0 = (im.height - 64) // 2 + 1
        im = im.crop((x0, y0, x0 + 64, y0 + 64))
    return im


def gif_frames(name, limit=None):
    im = Image.open(SRC / name)
    out = []
    for k in range(im.n_frames if limit is None else min(limit, im.n_frames)):
        try:
            im.seek(k)
            out.append(im.convert("RGB").copy())
        except Exception:
            break
    return out


def is_sky(r, g, b):
    return b > r + 20 and b >= g - 10 and (r + g + b) > 300


def night(im, seed=1, keep_warm=True, sky=True, sky_rows=48, protect=None):
    """What the firmware would compute from the day picture: a blue night, warm lights kept, sky to navy with stars."""
    out = im.copy()
    px = out.load()
    src = im.load()
    rnd = random.Random(seed)
    skypx = []
    for y in range(64):
        for x in range(64):
            if protect and protect(x, y):
                continue
            r, g, b = src[x, y]
            if sky and y < sky_rows and is_sky(r, g, b):
                t = y / sky_rows
                px[x, y] = (int(8 + 10 * t), int(12 + 14 * t), int(38 + 30 * t))
                skypx.append((x, y))
                continue
            if keep_warm and r > 150 and r > g + 40 and r > b + 60:
                px[x, y] = (int(r * 0.95), int(g * 0.9), int(b * 0.85))
                continue
            px[x, y] = (int(r * 0.26) + 4, int(g * 0.30) + 6, int(b * 0.48) + 16)
    rnd.shuffle(skypx)
    stars = skypx[: max(1, len(skypx) // 45)]
    for i, (x, y) in enumerate(stars):
        px[x, y] = (230, 230, 255) if i % 3 else (255, 240, 200)
    out.stars = stars
    return out


def scale(im, s):
    return im.resize((im.width * s, im.height * s), Image.NEAREST)


def save_still(im, name):
    scale(im, 8).save(OUT / f"{name}.png")


def save_gif(frames, name, ms=150):
    fr = [scale(f, 4) for f in frames]
    fr[0].save(OUT / f"{name}.gif", save_all=True, append_images=fr[1:], duration=ms, loop=0, optimize=False)


def sheet(names, out, crop_middle=False, s=4):
    ims = [scale(load(n, crop_middle), s) for n in names if (SRC / n).exists()]
    cols = min(4, len(ims))
    rows = (len(ims) + cols - 1) // cols
    sh = Image.new("RGB", (cols * (64 * s + 6) - 6, rows * (64 * s + 6) - 6), (24, 24, 28))
    for i, im in enumerate(ims):
        sh.paste(im, ((i % cols) * (64 * s + 6), (i // cols) * (64 * s + 6)))
    sh.save(OUT / f"{out}.png")


TIME, DATE = "09:41", "TUE 03 NOV"
AMPLE, TYPICAL, STANDARD, SLIGHT = "everyday-ample", "everyday-typical", "everyday-standard", "everyday-slight"


# ================================================================ observatory
def observatory(base):
    day = load(base, crop_middle=True)
    nt = night(day, seed=3, sky_rows=40)

    def put_time(im):
        draw_centred(im, font(AMPLE), 32, 5, TIME, (255, 255, 255), (12, 16, 48))

    d = day.copy()
    put_time(d)
    save_still(d, "observatory_day")
    n = nt.copy()
    put_time(n)
    save_still(n, "observatory_night")
    # day loop: two birds cross the sky; night loop: stars twinkle, one shooting star
    frames = []
    for k in range(16):
        f = day.copy()
        px = f.load()
        for b, (y0, x0) in enumerate(((22, -6), (26, -14))):
            x = x0 + k * 5
            y = y0 + (k % 4 == b)
            for dx, dy in ((0, 0), (1, 1), (2, 0)):
                if 0 <= x + dx < 64:
                    px[x + dx, y + dy] = (30, 30, 40)
        put_time(f)
        frames.append(f)
    save_gif(frames, "observatory_day")
    frames = []
    for k in range(16):
        f = nt.copy()
        px = f.load()
        for i, (x, y) in enumerate(nt.stars):
            if (i + k) % 5 == 0:
                px[x, y] = (120, 120, 170)
        if 4 <= k <= 7:
            for t in range(4):
                x, y = 10 + (k - 4) * 6 + t, 8 + (k - 4) * 2 + t // 2
                if 0 <= x < 64 and 0 <= y < 64:
                    px[x, y] = (255, 255, 230) if t == 3 else (170, 170, 220)
        put_time(f)
        frames.append(f)
    save_gif(frames, "observatory_night")


# ================================================================ hearth
def hearth():
    frames = gif_frames("hearth_subtle_0.gif")

    def put_time(im):
        f = font(STANDARD) if text_width(font(TYPICAL), TIME) > 26 else font(TYPICAL)
        draw_centred(im, f, 32, 12, TIME, (245, 215, 160), (52, 30, 18))
        draw_centred(im, font(SLIGHT), 32, 21, "3 NOV", (200, 170, 120))

    def window(x, y):
        return 3 <= x <= 9 and 19 <= y <= 35

    def paint_window(im):
        px = im.load()
        for y in range(64):
            for x in range(64):
                if window(x, y) and not (x in (3, 9) or y in (19, 35)):
                    px[x, y] = (14, 18, 52) if (x, y) not in ((5, 23), (7, 28)) else (235, 235, 255)

    d = frames[0].copy()
    put_time(d)
    save_still(d, "hearth_day")
    nt = night(frames[0], seed=5, sky=False)
    put_time(nt)
    paint_window(nt)
    save_still(nt, "hearth_night")
    out = []
    for fr in frames:
        g = fr.copy()
        put_time(g)
        out.append(g)
    save_gif(out, "hearth_day")
    out = []
    for fr in frames:
        g = night(fr, seed=5, sky=False)
        put_time(g)
        paint_window(g)
        out.append(g)
    save_gif(out, "hearth_night")


# ================================================================ terminal
def terminal(base, screen, crop_middle=False):
    x0, y0, x1, y1 = screen
    day = load(base, crop_middle=crop_middle)

    def on_screen(x, y):
        return x0 <= x <= x1 and y0 <= y <= y1

    def phosphor(im, k):
        px = im.load()
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                r, g, b = px[x, y]
                dim = 0.22 if (y - y0) % 2 else 0.30
                if (y - y0) == (k * 2) % (y1 - y0 + 1):
                    dim += 0.12
                px[x, y] = (int(r * dim * 0.5), int(g * dim) + 10, int(b * dim * 0.6) + 4)
        green, dimgreen = (110, 255, 130), (50, 150, 70)
        draw_centred(im, font(TYPICAL), (x0 + x1) // 2, y0 + 3, TIME, green)
        draw_centred(im, font(SLIGHT), (x0 + x1) // 2, y0 + 13, DATE, dimgreen)
        draw_text(im, font(SLIGHT), x0 + 2, y0 + 20, ">", dimgreen)
        if k % 8 < 4:
            for yy in range(y0 + 20, y0 + 25):
                for xx in range(x0 + 6, x0 + 9):
                    if on_screen(xx, yy):
                        px[xx, yy] = green

    d = day.copy()
    phosphor(d, 0)
    save_still(d, "terminal_day")
    nt = night(day, seed=7, sky=False, protect=on_screen)
    px = nt.load()
    for y in range(64):
        for x in range(64):
            if not on_screen(x, y):
                dist = max(0, max(x0 - x, x - x1, y0 - y, y - y1))
                if dist <= 10:
                    r, g, b = px[x, y]
                    t = (10 - dist) / 10 * 0.5
                    px[x, y] = (min(255, int(r + 10 * t)), min(255, int(g + 60 * t)), min(255, int(b + 20 * t)))
    phosphor(nt, 0)
    save_still(nt, "terminal_night")
    for tag, src in (("day", day), ("night", nt)):
        frames = []
        for k in range(16):
            f = src.copy()
            phosphor(f, k)
            frames.append(f)
        save_gif(frames, f"terminal_{tag}")


# ================================================================ seasons
def seasons():
    bases = {"spring": "seasons_spring3_0.png", "summer": "seasons_2.png", "autumn": "seasons_autumn3_0.png",
             "winter": "seasons_winter3_0.png"}

    def put_time(im, colour=(255, 255, 255), halo=(20, 30, 60)):
        draw_centred(im, font(AMPLE), 32, 4, TIME, colour, halo)

    stills = {}
    for s, n in bases.items():
        im = load(n)
        stills[s] = im
        d = im.copy()
        put_time(d)
        save_still(d, f"seasons_{s}")
    sn = night(stills["summer"], seed=11, sky_rows=30)
    sn_t = sn.copy()
    put_time(sn_t)
    save_still(sn_t, "seasons_summer_night")
    rnd = random.Random(4)

    def particles(base, n, colours, drift, kind):
        parts = [(rnd.randrange(2, 62), rnd.randrange(0, 64), rnd.random() * 6.28) for _ in range(n)]
        frames = []
        for k in range(16):
            f = base.copy()
            px = f.load()
            for (sx, sy, ph) in parts:
                if kind == "firefly":
                    if math.sin(ph + k * 0.8) > 0.2:
                        x, y = sx, 20 + (sy % 36)
                        px[x, y] = (255, 240, 120)
                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                            if 0 <= x + dx < 64 and 0 <= y + dy < 64:
                                r, g, b = px[x + dx, y + dy]
                                px[x + dx, y + dy] = (min(255, r + 70), min(255, g + 60), b)
                    continue
                y = (sy + k * drift) % 70 - 3
                x = sx + int(2 * math.sin(ph + k * 0.5))
                c = colours[(sx + sy) % len(colours)]
                for dx, dy in (((0, 0), (1, 0)) if kind != "snow" else ((0, 0),)):
                    if 0 <= x + dx < 64 and 0 <= y + dy < 64:
                        px[x + dx, y + dy] = c
            put_time(f)
            frames.append(f)
        return frames

    save_gif(particles(stills["spring"], 10, [(255, 190, 215), (255, 150, 190), (255, 230, 240)], 2, "petal"), "seasons_spring")
    save_gif(particles(stills["autumn"], 9, [(240, 120, 30), (210, 60, 30), (250, 190, 50)], 2, "leaf"), "seasons_autumn")
    save_gif(particles(stills["winter"], 14, [(255, 255, 255), (220, 230, 255)], 1, "snow"), "seasons_winter")
    save_gif(particles(sn, 7, [], 0, "firefly"), "seasons_summer_night")


if __name__ == "__main__":
    cfg = {}
    for a in sys.argv[1:]:
        k, v = a.split("=", 1)
        cfg[k] = v
    observatory(cfg.get("obs", "observatory_2.png"))
    hearth()
    scr = tuple(int(v) for v in cfg.get("screen", "14,10,46,36").split(","))
    terminal(cfg.get("term", "terminal_1.png"), scr, crop_middle=cfg.get("term_crop", "0") == "1")
    seasons()
    sheet(["observatory_0.png", "observatory_1.png", "observatory_2.png", "observatory_night2_0.png",
           "observatory_pro_0.png", "observatory_pro_1.png", "observatory_pro80_0.png", "observatory_pro80_1.png"],
          "observatory_alts", crop_middle=True)
    sheet(["hearth_0.png", "hearth_1.png", "hearth_2.png", "hearth_night2_0.png"], "hearth_alts")
    sheet(["terminal_0.png", "terminal_1.png", "terminal_2.png", "terminal_night2_0.png",
           "terminal_pro_0.png", "terminal_pro_1.png", "terminal_pro80_0.png", "terminal_pro80_1.png"],
          "terminal_alts", crop_middle=True)
    sheet(["seasons_0.png", "seasons_1.png", "seasons_2.png", "seasons_night_0.png",
           "seasons_spring2_0.png", "seasons_autumn2_0.png", "seasons_winter2_0.png"], "seasons_alts")
    print("ok", sorted(p.name for p in OUT.iterdir()))
