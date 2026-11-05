"""p120: preview mocks of the four sprite-rich concepts (square, harbour, farm, station) in ./previews120."""
import math
import random
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent.parent / "p119"))  # the p119 builder (fonts, text, scale) is reused
from build_previews import font, draw_text, draw_centred, text_width, scale, AMPLE, TYPICAL, STANDARD, SLIGHT  # noqa: E402

SRC = Path(r"C:\Users\fab\F\Estudo\Tecnologia\p64\firmware\assets\rd-source\p120")
OUT = Path(__file__).parent / "previews"  # git-ignored scratch output
OUT.mkdir(exist_ok=True)
TIME = "09:41"
PHASES = ["dawn", "day", "dusk", "night"]
# where the 64x64 panel sits in each 80x80 painting (x0, y0), per scene; the painted border is outside
CROPS = {"square": (4, 0), "harbour": (9, 9), "farm": (9, 9), "station": (9, 9)}


DAY_FILES = {"square": "square_day_0.png", "harbour": "harbour_day_0.png", "farm": "farm_day_1.png", "station": "station_day_0.png"}
_ALIGN = {}


def _edges(im):
    g = im.convert("L").load()
    w, h = im.size
    return [[abs(g[x, y] - g[x + 1, y]) + abs(g[x, y] - g[x, y + 1]) for x in range(w - 1)] for y in range(h - 1)]


def align(scene, phase):
    """The crop offset of a phase painting that best matches the day crop (edges compared over offsets 0..16)."""
    key = (scene, phase)
    if key in _ALIGN:
        return _ALIGN[key]
    x0, y0 = CROPS[scene]
    if phase == "day":
        _ALIGN[key] = (x0, y0)
        return _ALIGN[key]
    day = Image.open(SRC / DAY_FILES[scene]).convert("RGB").crop((x0, y0, x0 + 64, y0 + 64))
    src = Image.open(SRC / f"{scene}_{phase}_0.png").convert("RGB")
    ed = _edges(day)
    es = _edges(src)
    best = None
    for dy in range(0, 17):
        for dx in range(0, 17):
            d = 0
            for y in range(0, 63, 2):
                row_d, row_s = ed[y], es[y + dy]
                for x in range(0, 63, 2):
                    d += abs(row_d[x] - row_s[x + dx])
            if best is None or d < best[0]:
                best = (d, dx, dy)
    _ALIGN[key] = (best[1], best[2])
    print(f"align {scene} {phase}: offset {best[1]},{best[2]} (day {x0},{y0})")
    return _ALIGN[key]


def painting(scene, phase):
    name = DAY_FILES[scene] if phase == "day" else f"{scene}_{phase}_0.png"
    p = SRC / name
    if not p.exists():
        return None
    im = Image.open(p).convert("RGB")
    x0, y0 = align(scene, phase)
    return im.crop((x0, y0, x0 + 64, y0 + 64))


def gif_frames(name):
    im = Image.open(SRC / name)
    out = []
    for k in range(getattr(im, "n_frames", 1)):
        try:
            im.seek(k)
            out.append(im.convert("RGBA").copy())
        except Exception:
            break
    return out


def shrink2(im):
    """A 2x k-centroid-like downscale: each 2x2 block keeps its most common opaque colour, or stays clear."""
    w, h = im.size
    out = Image.new("RGBA", (w // 2, h // 2), (0, 0, 0, 0))
    src = im.load()
    dst = out.load()
    for y in range(h // 2):
        for x in range(w // 2):
            px = [src[2 * x + dx, 2 * y + dy] for dx in (0, 1) for dy in (0, 1)]
            op = [p for p in px if p[3] >= 128]
            if len(op) >= 2:
                r = sum(p[0] for p in op) // len(op)
                g = sum(p[1] for p in op) // len(op)
                b = sum(p[2] for p in op) // len(op)
                dst[x, y] = (r, g, b, 255)
    return out


def trim(im):
    bbox = im.getchannel("A").getbbox()
    return im.crop(bbox) if bbox else im


def sprite(name, frames_idx, small=True, flip=False):
    """Frames (1-based) of a small_sprites sheet, shrunk to 16 px and trimmed to their ink."""
    fr = gif_frames(f"sp_{name}_0.gif")
    out = []
    for i in frames_idx:
        f = fr[i - 1]
        if small:
            f = shrink2(f)
        f = trim(f)
        if flip:
            f = f.transpose(Image.FLIP_LEFT_RIGHT)
        out.append(f)
    return out


def still(name, small=False, flip=False):
    im = trim(Image.open(SRC / f"{name}.png").convert("RGBA"))
    if small:
        im = shrink2(im)
    if flip:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    return im


def blit(frame, spr, x, y):
    """Copies the pixels whose alpha is 128 or more (sprite::blit); x, y is the sprite's bottom-left."""
    px = frame.load()
    sp = spr.load()
    for yy in range(spr.height):
        for xx in range(spr.width):
            p = sp[xx, yy]
            if p[3] >= 128:
                X, Y = x + xx, y - spr.height + yy
                if 0 <= X < 64 and 0 <= Y < 64:
                    px[X, Y] = p[:3]


def blend(a, b, t):
    return Image.blend(a, b, t)


def save_still(im, name):
    scale(im, 8).save(OUT / f"{name}.png")


def save_gif(frames, name, ms=150):
    fr = [scale(f, 4) for f in frames]
    fr[0].save(OUT / f"{name}.gif", save_all=True, append_images=fr[1:], duration=ms, loop=0, optimize=False)


def phases_sheet(scene, s=4):
    ims = [painting(scene, p) for p in PHASES]
    ims = [i for i in ims if i]
    sh = Image.new("RGB", (len(ims) * (64 * s + 6) - 6, 64 * s), (24, 24, 28))
    for i, im in enumerate(ims):
        sh.paste(scale(im, s), (i * (64 * s + 6), 0))
    sh.save(OUT / f"{scene}_phases.png")


WALK = [6, 7, 8, 9]
IDLE = [1, 2, 1, 3]


# ================================================================ square
def square():
    day, night, dawn, dusk = painting("square", "day"), painting("square", "night"), painting("square", "dawn"), painting("square", "dusk")
    dial = (23, 7)  # the tower clock's centre in the crop; radius 6

    def put_time(im, hour=9, minute=41):
        px = im.load()
        cx, cy = dial
        for hand, length, ang in ((1, 4, (hour % 12 + minute / 60) / 12 * 2 * math.pi), (0, 6, minute / 60 * 2 * math.pi)):
            for t in range(length + 1):
                x = cx + int(round(math.sin(ang) * t))
                y = cy - int(round(math.cos(ang) * t))
                if 0 <= x < 64 and 0 <= y < 64:
                    px[x, y] = (30, 24, 20)
        draw_text(im, font(SLIGHT), 40, 1, TIME, (255, 255, 255), (20, 30, 60))

    for nm, im in (("day", day), ("night", night), ("dawn", dawn), ("dusk", dusk)):
        if im:
            d = im.copy()
            put_time(d)
            save_still(d, f"square_{nm}")
    woman = sprite("woman", WALK, flip=True)
    man = sprite("man", WALK)
    cat = sprite("cat", WALK)
    lamp = sprite("lamplighter", IDLE) if (SRC / "sp_lamplighter_0.gif").exists() else None
    tram = still("st_tram_0")
    frames = []
    for k in range(32):
        f = day.copy()
        blit(f, woman[k % 4], 60 - k * 2, 63)
        blit(f, man[(k // 2) % 4], 6 + k, 60)
        blit(f, cat[k % 4], 14 + k, 62)
        if 8 <= k < 24:
            blit(f, tram, -48 + (k - 8) * 7, 63)
        put_time(f)
        frames.append(f)
    save_gif(frames, "square_day", 120)
    if night:
        fw = gif_frames("fx_firework_0.gif")
        frames = []
        for k in range(32):
            f = night.copy()
            if lamp:
                blit(f, lamp[(k // 4) % 4], 44, 60)
            if 4 <= k < 4 + len(fw):
                fx = fw[k - 4]
                px = f.load()
                sp = fx.load()
                for yy in range(32):
                    for xx in range(32):
                        p = sp[xx, yy]
                        if p[3] >= 128 and sum(p[:3]) > 90:
                            X, Y = 36 + xx, 2 + yy
                            if 0 <= X < 64 and 0 <= Y < 64:
                                px[X, Y] = p[:3]
            put_time(f, 0, 0)
            frames.append(f)
        save_gif(frames, "square_night", 120)
    phases_sheet("square")


# ================================================================ harbour
def harbour():
    ph = {p: painting("harbour", p) for p in PHASES}

    def put_time(im):
        draw_centred(im, font(AMPLE), 32, 3, TIME, (255, 255, 255), (20, 30, 60))

    for p, im in ph.items():
        if im:
            d = im.copy()
            put_time(d)
            save_still(d, f"harbour_{p}")
    boat = still("st_boat_0")
    fisher = sprite("fisherman", WALK)
    gull = sprite("gull", [2, 3, 4, 3])
    sub = gif_frames("harbour_subtle_0.gif") if (SRC / "harbour_subtle_0.gif").exists() else []
    frames = []
    for k in range(32):
        base = sub[k % len(sub)].convert("RGB") if len(sub) >= 8 else ph["day"]
        f = base.copy()
        blit(f, boat, 2 + k * 2, 50 + (k % 4 == 1))
        blit(f, fisher[k % 4], 4 + k, 36)
        blit(f, gull[(k // 2) % 4], 48 - k, 14 + (k % 6 > 2))
        put_time(f)
        frames.append(f)
    save_gif(frames, "harbour_day", 120)
    if ph["night"]:
        frames = []
        lx, ly = 54, 20  # the lamp
        for k in range(32):
            f = ph["night"].copy()
            px = f.load()
            ang = -math.pi / 2 + (k / 32) * 2 * math.pi
            for r in range(2, 40):
                for spread in (-0.08, 0, 0.08):
                    x = lx + int(round(math.cos(ang + spread) * r))
                    y = ly + int(round(math.sin(ang + spread) * r * 0.5))
                    if 0 <= x < 64 and 0 <= y < 64:
                        c = px[x, y]
                        g = max(0, 1 - r / 40)
                        px[x, y] = (min(255, int(c[0] + 120 * g)), min(255, int(c[1] + 110 * g)), min(255, int(c[2] + 60 * g)))
            put_time(f)
            frames.append(f)
        save_gif(frames, "harbour_night", 120)
    phases_sheet("harbour")


# ================================================================ farm
def farm():
    ph = {p: painting("farm", p) for p in PHASES}
    sign = (41, 48, 62, 58)  # the signboard's face in the crop, x0 y0 x1 y1

    def put_time(im):
        f = font(STANDARD) if text_width(font(TYPICAL), TIME) > sign[2] - sign[0] - 1 else font(TYPICAL)
        draw_centred(im, f, (sign[0] + sign[2]) // 2, sign[1] + 2, TIME, (250, 240, 210), (60, 40, 20))

    for p, im in ph.items():
        if im:
            d = im.copy()
            put_time(d)
            save_still(d, f"farm_{p}")
    rooster = sprite("rooster", IDLE)
    cow = sprite("cow", WALK, flip=True)
    sheep = sprite("sheep", WALK) if (SRC / "sp_sheep_0.gif").exists() else None
    farmer = sprite("farmer", WALK)
    owl = still("st_owl_1")
    smoke = [trim(shrink2(f)) for f in gif_frames("fx_smoke_0.gif")]
    sub = gif_frames("farm_subtle_0.gif") if (SRC / "farm_subtle_0.gif").exists() else []
    frames = []
    for k in range(32):
        base = sub[k % len(sub)].convert("RGB") if len(sub) >= 8 else ph["day"]
        f = base.copy()
        blit(f, cow[(k // 2) % 4], 30 - k // 2, 52)
        if sheep:
            blit(f, sheep[(k // 2) % 4], 4 + k // 3, 56)
        blit(f, farmer[k % 4], 10 + k, 48)
        put_time(f)
        frames.append(f)
    save_gif(frames, "farm_day", 120)
    if ph["dawn"]:
        frames = []
        for k in range(32):
            f = ph["dawn"].copy()
            blit(f, rooster[(k // 3) % 4], 24, 46 - (k % 12 == 0))
            put_time(f)
            frames.append(f)
        save_gif(frames, "farm_dawn", 120)
    if ph["night"]:
        frames = []
        for k in range(32):
            f = ph["night"].copy()
            blit(f, smoke[k % len(smoke)], 14, 18)
            blit(f, owl, 50, 48)
            put_time(f)
            frames.append(f)
        save_gif(frames, "farm_night", 120)
    phases_sheet("farm")


# ================================================================ station
def station():
    ph = {p: painting("station", p) for p in PHASES}
    screen = (2, 53, 40, 62)

    def put_time(im):
        px = im.load()
        for y in range(screen[1], screen[3] + 1):
            for x in range(screen[0], screen[2] + 1):
                r, g, b = px[x, y]
                px[x, y] = (r // 4, g // 3 + 8, b // 3 + 14)
        draw_text(im, font(TYPICAL), screen[0] + 2, screen[1] + 1, TIME, (120, 230, 255))

    for p, im in ph.items():
        if im:
            d = im.copy()
            put_time(d)
            save_still(d, f"station_{p}")
    astro = sprite("astronaut", [1, 2, 1, 3])
    drone = sprite("drone", IDLE) if (SRC / "sp_drone_0.gif").exists() else None
    ship = still("st_ship_0")
    for p in ("day", "night"):
        if not ph[p]:
            continue
        frames = []
        for k in range(32):
            f = ph[p].copy()
            blit(f, astro[(k // 4) % 4], 6 + k, 30 + int(3 * math.sin(k / 5)))
            if drone:
                blit(f, drone[(k // 3) % 4], 48 - k // 2, 16 + int(2 * math.sin(k / 3)))
            if 10 <= k < 30:
                t = k - 10
                x = 70 - t * 4 if t < 10 else 30
                blit(f, ship, x, 24)
            put_time(f)
            frames.append(f)
        save_gif(frames, f"station_{p}", 120)
    phases_sheet("station")


if __name__ == "__main__":
    which = sys.argv[1:] or ["square", "harbour", "farm", "station"]
    for w in which:
        globals()[w]()
    print("ok", sorted(p.name for p in OUT.iterdir()))
