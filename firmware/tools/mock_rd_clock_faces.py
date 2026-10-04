#!/usr/bin/env python3
r"""Two clock face candidates drawn from Retro Diffusion pixel art (p075), on the host:
Horizon-RD (the horizon face's sky and almanac over a painted landscape) and the aquarium.

    python tools\mock_rd_clock_faces.py        (from firmware/; needs Pillow)

A design mock-up for review, not yet the reference of a firmware implementation: it writes
only review images under docs/design/clock-candidates/ (no test references). It draws with
the helpers, the fonts and the almanac of tools/mock_clock_faces.py, from the assets that
tools/prep_rd_clock_assets.py cuts out of the pictures in assets/rd-source/.

Both faces are animated, so both take the milliseconds into the second: the aquarium runs
at the tank loop's 150 ms a frame, Horizon-RD moves the lake's glints every 500 ms.
"""

import math
import os

from PIL import Image

import mock_clock_faces as M
from mock_clock_faces import DESIGN, Frame, H, Moment, Options, W, blend, daylight, lerp, load_font, text_width, upscale

LAND_Y = 26  # prep_rd_clock_assets.py's
LAKE_TOP = 51
SKY_ROWS = 52  # the sky's gradient ends where the lake begins
ZENITH_Y = 18
GLINT_MS = 500
NIGHT_LAND = (0.13, 0.19, 0.36)  # what moonless night leaves of the land's colours
CLOUD_SPOTS = [(4, 19), (38, 25), (22, 13), (50, 9), (12, 29)]
STARS = M.STARS + [(18, 27), (33, 6), (44, 24), (62, 4), (1, 12), (28, 30)]


CANDIDATES = os.path.join(M.FIRMWARE, "assets", "clock-candidates")


def rd_asset(face, name):
    path = os.path.join(CANDIDATES, face, name + ".png")
    if not os.path.exists(path):
        raise SystemExit(f"{path} is missing; run tools/prep_rd_clock_assets.py")
    return Image.open(path).convert("RGBA")


def mul(c, k):
    """A colour scaled per channel by k (three factors, 0..1)."""
    return tuple(min(255, int(round(c[i] * k[i]))) for i in range(3))


def hash3(x, y, t):
    """A small integer hash, the same arithmetic the firmware would use (32-bit)."""
    v = (x * 73856093) ^ (y * 19349663) ^ (t * 83492791)
    v = (v ^ (v >> 13)) * 1274126177 & 0xFFFFFFFF
    return (v ^ (v >> 16)) & 0xFFFF


def draw_time_and_date(frame, m, o, light):
    """The horizon face's lettering (white ink, one outline at 75 % black), laid out for a
    picture whose ground is worth seeing: the date on the lake, right-aligned, clear of the
    cabin, and the meridiem beside the time, so the sky under the time stays the sun's."""
    font = load_font("capital-hill")
    big = load_font("everyday-vast-black")
    hh = f"{m.hour:02d}" if o.h24 else str(m.h12())
    time_text = f"{hh}:{m.minute:02d}" if M.colon_on(m, o) else f"{hh} {m.minute:02d}"
    gap = 1
    spaced = text_width(big, time_text) + gap * (len(time_text) - 1)
    mer = "" if o.h24 else m.meridiem()
    mer_w = text_width(font, mer) + 2 if mer else 0
    x0 = (W - spaced - mer_w) // 2
    small = load_font("everyday-slight")
    date = m.date(o.month_first)
    dx = W - 1 - text_width(small, date)

    def letters(target, text_c, outline):
        x = x0
        for ch in time_text:
            M.draw_text(target, big, x, 3, ch, text_c, 1, outline=outline)
            x += text_width(big, ch) + gap
        if mer:
            M.draw_text(target, font, x0 + spaced + 2, 8, mer, text_c, 1, outline=outline)
        M.draw_text(target, small, dx, 58, date, text_c, 1, outline=outline)

    mask = Frame()
    letters(mask, M.MASK_TEXT, M.MASK_OUTLINE)
    for y in range(H):
        for x in range(W):
            if mask.get(x, y) == M.MASK_OUTLINE:
                frame.set(x, y, blend(frame.get(x, y), (0, 0, 0), M.HORIZON_OUTLINE_ALPHA))
    letters(frame, (255, 255, 255), None)


# ---------------------------------------------------------------- Horizon-RD (floating point)

# The horizon face's sky (its colours by the sun's elevation, the glow on the sun's side,
# the stars, the sun's and the moon's real places, the moon's phase, the weather's clouds
# and rain) over a landscape painted by Retro Diffusion: mountains, a lake, pines and a
# cabin. The land keeps its own colours by day and sinks to a blue night by the daylight,
# warmed at the twilights; the lake mirrors the sky and glitters under the sun and the
# moon; the cabin's windows are lit while it is dark.


def skyline(land):
    """The land's top row in the frame per column, smoothed over five columns."""
    a = land.split()[-1].load()
    tops = [next((y for y in range(land.height) if a[x, y] >= 128), land.height) + LAND_Y for x in range(W)]
    return [sum(tops[min(W - 1, max(0, x + d))] for d in range(-2, 3)) / 5 for x in range(W)]


def body_y(el, x, radius, line):
    """mock_clock_faces.body_y with this face's zenith row, the raised horizon being the
    skyline's median: the trees and the peaks above it simply hide a body."""
    if el <= M.SET:
        return None
    raised = sorted(line)[W // 2]
    local = max(line[min(W - 1, max(0, x))], raised)
    edge = raised + (local - raised) * max(0.0, 1 - (el - M.RESTS) / M.BLEND)
    rests, hidden = edge - radius - 1, edge + radius
    if el < M.RESTS:
        return int(round(hidden + (rests - hidden) * (el - M.SET) / (M.RESTS - M.SET)))
    return int(round(rests + (ZENITH_Y - rests) * (min(el, 90) - M.RESTS) / (90 - M.RESTS)))


def draw_moon_rd(frame, moon, x, y, phase, lat, dark_sky):
    """The painted moon with its terminator: the lit side keeps the picture's craters, the
    dark side shows faintly on a dark sky."""
    r = moon.width / 2
    c = math.cos(2 * math.pi * phase)
    px = moon.load()
    for yy in range(moon.height):
        v = (yy + 0.5 - r) / r
        s = math.sqrt(max(0.0, 1 - v * v))
        for xx in range(moon.width):
            if px[xx, yy][3] < 128:
                continue
            u = (xx + 0.5 - r) / r
            uu = u if lat >= 0 else -u
            lit = (uu > s * c) if phase < 0.5 else (uu < -s * c)
            X, Y = x - moon.width // 2 + xx, y - moon.height // 2 + yy
            if lit:
                frame.set(X, Y, mul(px[xx, yy][:3], (1.25, 1.25, 1.3)))
            elif dark_sky:
                frame.set(X, Y, lerp(frame.get(X, Y), (60, 62, 90), 0.5))


def draw_horizon_rd(m, o, lat=40.0, lon=0.0, tz=0.0, year=2026, cover=0, precip="", millis=0):
    h = m.hour + m.minute / 60.0
    frame = Frame((0, 0, 0))
    land, lake, lights = (rd_asset("horizon-rd", n) for n in ("land", "lake", "lights"))
    sun, moon = rd_asset("horizon-rd", "sun"), rd_asset("horizon-rd", "moon")
    clouds = (rd_asset("horizon-rd", "cloud-a"), rd_asset("horizon-rd", "cloud-b"))
    el, az = M.solar_position(lat, lon, tz, m.yday, h)
    top, near_c, away_c = M.sky_palette(el)
    grey = 0.22 * cover
    top, near_c, away_c = (lerp(c, (110, 116, 130), grey) for c in (top, near_c, away_c))
    sx = M.sky_x(az, el, lat)
    glow_width = 30 if el < 12 else 60
    sky = [[None] * SKY_ROWS for _ in range(W)]
    for x in range(W):
        w = math.exp(-((x - sx) / glow_width) ** 2)
        hz = lerp(away_c, near_c, w)
        for y in range(SKY_ROWS):
            sky[x][y] = lerp(top, hz, (y / (SKY_ROWS - 1)) ** 1.5)
            frame.set(x, y, sky[x][y])
    light = daylight(el)
    star_k = max(0.0, min(1.0, (-el - 4) / 8)) * (1 - 0.8 * min(1, cover / 2))
    if star_k > 0:
        for i, (x, y) in enumerate(STARS):
            frame.set(x, y, lerp(frame.get(x, y), (232, 232, 250), star_k * (0.85 if i % 3 else 1.0)))
            if i % 3 == 0 and star_k > 0.6:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if 0 <= x + dx < W and 0 <= y + dy < SKY_ROWS:
                        frame.set(x + dx, y + dy, lerp(frame.get(x + dx, y + dy), (150, 150, 190), star_k))
    line = skyline(land)
    phase = M.moon_phase(year, m.yday, h, tz)
    mel, maz = M.moon_position(lat, lon, tz, year, m.yday, h)
    mx = M.sky_x(maz, mel, lat)
    my = body_y(mel, mx, moon.width // 2, line)
    moon_up = my is not None and el < 25
    if moon_up:
        draw_moon_rd(frame, moon, mx, my, phase, lat, el < -6)
    sy = body_y(el, sx, sun.width // 2, line)
    sun_tint = lerp((255, 120, 90), (255, 255, 255), max(0.0, min(1.0, el / 6)))
    if sy is not None:
        # a soft glow around the disc, then the disc
        for dy in range(-6, 7):
            for dx in range(-6, 7):
                d = math.hypot(dx, dy)
                if d < 6.5:
                    frame.set(sx + dx, sy + dy, lerp(frame.get(sx + dx, sy + dy), sun_tint, 0.30 * (1 - d / 6.5)))
        px = sun.load()
        for yy in range(sun.height):
            for xx in range(sun.width):
                if px[xx, yy][3] >= 128:
                    frame.set(sx - sun.width // 2 + xx, sy - sun.height // 2 + yy, mul(px[xx, yy][:3], [v / 255 for v in sun_tint]))
    tint = lerp((50, 52, 80), lerp((255, 255, 255), near_c, 0.35), light)
    if cover >= 3:
        tint = lerp((40, 42, 60), (168, 172, 186), light)
    for i, (base_x, y) in enumerate(CLOUD_SPOTS[: (1, 2, 3, 5)[cover]]):
        cloud = clouds[i % 2]
        x = (base_x + m.minute * 64 // 60) % (W + cloud.width) - cloud.width
        px = cloud.load()
        for yy in range(cloud.height):
            for xx in range(cloud.width):
                if px[xx, yy][3] >= 128:
                    frame.set(x + xx, y + yy, mul(px[xx, yy][:3], [v / 255 for v in tint]))
    if precip:
        rng = (m.minute * 7919 + m.hour * 104729) % 65536
        for _ in range(22):
            rng = (rng * 1103515245 + 12345) % 2147483648
            x, y = (rng >> 8) % W, (rng >> 4) % 50
            if precip == "rain":
                for k in range(3):
                    frame.set(x, y + k, lerp(frame.get(x, y + k), (150, 190, 240), 0.7))
            else:
                frame.set(x, y, (240, 244, 255))
    # the land: its own colours by day, a blue night, warmed by the twilight's glow
    warm = [0.6 + 0.4 * v / 255 for v in near_c]
    dusk = 0.5 * (1 - abs(2 * light - 1))
    tick = (m.second * 1000 + millis) // GLINT_MS + m.minute * 120
    lp, kp = land.load(), lake.load()
    for yy in range(land.height):
        for xx in range(W):
            if lp[xx, yy][3] < 128:
                continue
            c = lp[xx, yy][:3]
            if cover:
                g = (c[0] * 3 + c[1] * 6 + c[2]) // 10
                c = lerp(c, (g, g, g), 0.15 * cover)
            if precip == "snow":
                c = lerp(c, (228, 232, 242), 0.3)
            lit = lerp(mul(c, NIGHT_LAND), c, light)
            lit = lerp(lit, mul(c, warm), dusk)
            y = yy + LAND_Y
            if kp[xx, yy][3] >= 128:
                # the lake mirrors the sky, and glitters under the sun and the moon
                lit = lerp(lit, sky[xx][max(0, SKY_ROWS - 1 - (y - LAKE_TOP) * 4)], 0.45)
                spread = 1 + (y - LAKE_TOP) // 4
                if sy is not None and el < 30 and abs(xx - sx) <= spread and hash3(xx, y, tick) % 5 < 2:
                    lit = lerp(lit, mul((255, 236, 170), [v / 255 for v in sun_tint]), 0.5)
                elif moon_up and el < -4 and abs(xx - mx) <= spread and hash3(xx, y, tick) % 5 < 2:
                    lit = lerp(lit, (214, 220, 240), 0.4)
                elif hash3(xx, y, tick) % 29 == 0:
                    lit = lerp(lit, (255, 255, 255), 0.10 + 0.15 * light)
            frame.set(xx, y, lit)
    # the cabin's windows, lit while it is dark, with a faint glow around them
    lamp = max(0.0, min(1.0, (0.35 - light) / 0.25))
    if lamp > 0:
        gp = lights.load()
        spots = [(xx, yy) for yy in range(lights.height) for xx in range(W) if gp[xx, yy][3] >= 128]
        for xx, yy in spots:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (xx + dx, yy + dy) not in spots:
                    X, Y = xx + dx, yy + dy + LAND_Y
                    frame.set(X, Y, lerp(frame.get(X, Y), gp[xx, yy][:3], 0.22 * lamp))
        for xx, yy in spots:
            frame.set(xx, yy + LAND_Y, lerp(frame.get(xx, yy + LAND_Y), gp[xx, yy][:3], lamp))
    draw_time_and_date(frame, m, o, light)
    return frame


# ---------------------------------------------------------------- Aquarium (integer but for the daylight)

# A goldfish tank painted by Retro Diffusion: a sixteen-frame loop of the tank (the plant
# sways, bubbles rise from the chest), three goldfish that swim from wall to wall and turn,
# each on its own lane and wagging its tail (eight frames played forth and back), and the
# time on the sunken sign. The far fish pass behind the castle, the plant, the chest and
# the sign; the near one in front of everything but the sign's board. The tank's lamp
# follows the sun: by night the water goes dark blue, the castle's windows glow, the
# small fish hides and the others swim at half speed.

TANK_FRAMES, TANK_MS = 16, 150
BOARD_X, BOARD_Y = 27, 19
SIGN_INK = (255, 240, 200)
NIGHT_TANK = (0.30, 0.40, 0.70)
FISH_FRAMES = 8


class Fish:
    def __init__(self, name, y0, x_min, x_max, speed, offset, bob, bob_ms, near, shy):
        self.name, self.y0, self.x_min, self.x_max = name, y0, x_min, x_max
        self.speed, self.offset = speed, offset  # px per second; the path's phase in px
        self.bob, self.bob_ms = bob, bob_ms  # the rise and fall, px and period
        self.near, self.shy = near, shy  # in front of the decor; hidden at night


# name, lane, the walls it turns at (the sprite's left edge), px/s, phase, bob, layer, shy
FISHES = [
    Fish("fish-c", 9, 6, 40, 7, 13, 3, 5200, False, True),
    Fish("fish-a", 27, 6, 35, 4, 31, 2, 7900, False, False),
    Fish("fish-b", 3, 5, 29, 3, 5, 1, 9700, True, False),
]


def fish_pose(f, t_ms, cell_w):
    """Where the fish is and how it looks at a time of day: (x, y, facing right, frame)."""
    span = f.x_max - f.x_min
    p = (t_ms * f.speed // 1000 + f.offset) % (2 * span)
    right = p < span
    x = f.x_min + p if right else f.x_max - (p - span)
    y = f.y0 + int(round(f.bob * math.sin(2 * math.pi * (t_ms % f.bob_ms) / f.bob_ms)))
    k = (t_ms // TANK_MS + f.offset) % (2 * FISH_FRAMES - 2)
    return x, y, right, (k if k < FISH_FRAMES else 2 * FISH_FRAMES - 2 - k)


def draw_fish(frame, sheet, f, t_ms):
    w = sheet.width // FISH_FRAMES
    x, y, right, k = fish_pose(f, t_ms, w)
    px = sheet.load()
    for yy in range(sheet.height):
        for xx in range(w):
            c = px[k * w + (xx if right else w - 1 - xx), yy]
            if c[3] >= 128:
                frame.set(x + xx, y + yy, c[:3])


def draw_aquarium(m, o, lat=40.0, lon=0.0, tz=0.0, millis=0):
    el, _az = M.solar_position(lat, lon, tz, m.yday, m.hour + m.minute / 60.0)
    light = daylight(el)
    night = light < 0.3
    t_ms = ((m.hour * 60 + m.minute) * 60 + m.second) * 1000 + millis
    swim_ms = t_ms // 2 if night else t_ms
    tank, front, sign, lights = (rd_asset("aquarium", n) for n in ("tank", "front", "sign", "lights"))
    k = (t_ms // TANK_MS) % TANK_FRAMES
    frame = Frame((0, 0, 0))
    frame.img.paste(tank.crop((k * W, 0, (k + 1) * W, H)).convert("RGB"), (0, 0))
    frame.px = frame.img.load()
    for f in FISHES:
        if not f.near and not (f.shy and night):
            draw_fish(frame, rd_asset("aquarium", f.name), f, swim_ms)
    M.blit(frame, front.crop((k * W, 0, (k + 1) * W, H)), 0, 0)
    for f in FISHES:
        if f.near:
            draw_fish(frame, rd_asset("aquarium", f.name), f, swim_ms)
    M.blit(frame, sign, BOARD_X, BOARD_Y)
    # the lamp: everything dims to a blue night, then the castle's windows and the time glow
    if light < 1:
        dim = [NIGHT_TANK[i] + (1 - NIGHT_TANK[i]) * light for i in range(3)]
        for y in range(H):
            for x in range(W):
                frame.set(x, y, mul(frame.get(x, y), dim))
    lamp = max(0.0, min(1.0, (0.35 - light) / 0.25))
    if lamp > 0:
        gp = lights.load()
        for y in range(H):
            for x in range(W):
                if gp[x, y][3] >= 128:
                    frame.set(x, y, lerp(frame.get(x, y), gp[x, y][:3], 0.85 * lamp))
    font = load_font("everyday-standard")
    hh = f"{m.hour:02d}" if o.h24 else str(m.h12())
    text = f"{hh}:{m.minute:02d}" if M.colon_on(m, o) else f"{hh} {m.minute:02d}"
    x = BOARD_X + (sign.width - text_width(font, text) + 1) // 2
    M.draw_text(frame, font, x + 1, BOARD_Y + 3, text, mul((58, 38, 30), [0.5 + 0.5 * light] * 3))  # a carved shadow
    M.draw_text(frame, font, x, BOARD_Y + 2, text, SIGN_INK)
    return frame


# ---------------------------------------------------------------- output


def save_gif(path, seq, scale=1):
    frames = [upscale(f.img, scale).quantize(colors=255, dither=Image.NONE) for f, _ in seq]
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=[d for _, d in seq], loop=0, optimize=False)


def at(m, ms):
    """The moment `ms` milliseconds later, and the milliseconds left over."""
    total = m.second * 1000 + ms
    return M.plus_seconds(Moment(m.hour, m.minute, 0, m.wday, m.mday, m.mon, m.yday), total // 1000), total % 1000


def main():
    os.makedirs(DESIGN, exist_ok=True)
    o = Options()
    ny = dict(lat=40.7, lon=-74.0, tz=-4)
    faces = [("horizon-rd", draw_horizon_rd(Moment(18, 42), o, **ny)), ("aquarium", draw_aquarium(Moment(10, 32, 37), o, **ny))]
    for name, fr in faces:
        fr.img.save(os.path.join(DESIGN, name + ".png"))
        upscale(fr.img, 8).save(os.path.join(DESIGN, name + "@8x.png"))
    moments = [(0, 30), (3, 0), (5, 30), (6, 20), (6, 50), (8, 0), (12, 30), (16, 30), (18, 20), (18, 50), (19, 25), (21, 0)]
    M.contact_sheet([(f"{hh:02d}:{mm:02d}", draw_horizon_rd(Moment(hh, mm), o, **ny)) for hh, mm in moments], scale=4, columns=6).save(
        os.path.join(DESIGN, "horizon-rd-day.png"))
    variants = [("clear", draw_horizon_rd(Moment(15, 0), o, **ny)),
                ("broken", draw_horizon_rd(Moment(15, 0), o, cover=2, **ny)),
                ("rain", draw_horizon_rd(Moment(15, 0), o, cover=3, precip="rain", **ny)),
                ("snow", draw_horizon_rd(Moment(15, 0), o, cover=3, precip="snow", **ny)),
                ("12 h", draw_horizon_rd(Moment(6, 40), Options(h24=False), **ny)),
                ("full moon", draw_horizon_rd(Moment(22, 30), o, **ny)),
                ("tromso dec", draw_horizon_rd(Moment(13, 0, 0, 1, 21, 12, 354), o, lat=69.65, lon=18.96, tz=1)),
                ("sao paulo", draw_horizon_rd(Moment(18, 10), o, lat=-23.55, lon=-46.63, tz=-3))]
    M.contact_sheet(variants, scale=4, columns=4, gap=24).save(os.path.join(DESIGN, "horizon-rd-variants.png"))
    seq = []
    for i in range(12):
        mm, ms = at(Moment(18, 42, 0), i * GLINT_MS)
        seq.append((draw_horizon_rd(mm, o, millis=ms, **ny), GLINT_MS))
    save_gif(os.path.join(DESIGN, "horizon-rd@8x.gif"), seq, 8)
    tank_moments = [(10, 32, 37), (10, 32, 49), (14, 5, 12), (6, 25, 0), (19, 5, 30), (22, 40, 10)]
    M.contact_sheet([(f"{hh:02d}:{mi:02d}:{ss:02d}", draw_aquarium(Moment(hh, mi, ss), o, **ny)) for hh, mi, ss in tank_moments],
                    scale=4, columns=6).save(os.path.join(DESIGN, "aquarium-moments.png"))
    for name, start, n in (("aquarium", Moment(10, 32, 30), 96), ("aquarium-night", Moment(22, 40, 0), 64)):
        seq = []
        for i in range(n):
            mm, ms = at(start, i * TANK_MS)
            seq.append((draw_aquarium(mm, o, millis=ms, **ny), TANK_MS))
        save_gif(os.path.join(DESIGN, name + ".gif"), seq)
        save_gif(os.path.join(DESIGN, name + "@8x.gif"), seq, 8)
    print(f"review images in {DESIGN}")


if __name__ == "__main__":
    main()
