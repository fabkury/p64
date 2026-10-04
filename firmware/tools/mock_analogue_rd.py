#!/usr/bin/env python3
r"""Two candidates for an analogue clock face on Retro Diffusion art (p077), on the host:
Analogue-RD-1, an antique bracket clock (engraved brass plate, cream enamel dial, blued
hands, a mock pendulum), and Analogue-RD-2, a railway station clock (white dial, steel
rim, black bar hands, the red stop-to-go second hand).

    python tools\mock_analogue_rd.py        (from firmware/; needs Pillow)

A design mock-up for review, not yet the reference of a firmware implementation: it cuts
the two dials out of the service's pictures (assets/rd-source/used/, every paid call in
calls.json) into assets/clock-candidates/analogue_rd_N/dial.png and writes review images
under docs/design/clock-candidates/. Only the dials are painted; the markers, the
numerals (bundled fonts at their native size), the hands, the date and whatever moves are
drawn here the way the firmware would draw them, so they are exact at every minute.
"""

import math
import os

from PIL import Image

import mock_clock_faces as M
from mock_clock_faces import DESIGN, FIRMWARE, Frame, H, Moment, Options, W, blend, lerp, load_font, text_width, upscale

SRC = os.path.join(FIRMWARE, "assets", "rd-source", "used")
CANDIDATES = os.path.join(FIRMWARE, "assets", "clock-candidates")
STEP_MS = 100  # both faces move at ten frames a second


# ---------------------------------------------------------------- the dials


def save(face, name, img):
    path = os.path.join(CANDIDATES, face, name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    return img


def prep_brass():
    """The bracket clock's face, generated at 80x80 so that its enamel fills the panel: the
    middle 64x64 (the plate's engraved corners stay in the picture's corners)."""
    im = Image.open(os.path.join(SRC, "brass3_0.png")).convert("RGB").crop((8, 8, 72, 72))
    return save("analogue_rd_1", "dial", im)


def prep_station():
    """The station clock, generated at 80x80 so that its dial fills the panel: the middle
    64x64, black outside the rim."""
    im = Image.open(os.path.join(SRC, "station3_1.png")).convert("RGB").crop((8, 9, 72, 73))
    px = im.load()
    todo = [(x, y) for x in range(64) for y in (0, 63)] + [(x, y) for y in range(64) for x in (0, 63)]
    seen = set()
    while todo:
        x, y = todo.pop()
        if not (0 <= x < 64 and 0 <= y < 64) or (x, y) in seen or max(px[x, y]) > 58:
            continue
        seen.add((x, y))
        px[x, y] = (0, 0, 0)
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return save("analogue_rd_2", "dial", im)


# ---------------------------------------------------------------- hands


def unit(angle_deg):
    """The direction of a hand at `angle_deg` (0 = 12 o'clock, clockwise) and its normal."""
    a = math.radians(angle_deg)
    return (math.sin(a), -math.cos(a)), (math.cos(a), math.sin(a))


def draw_hand(frame, cx, cy, angle_deg, length, tail, half_width, shade, shadow=0):
    """A hand as a shape along its axis: `half_width(t)` is its half width at distance t
    from the pivot (-tail..length), `shade(t, s)` its colour there (s = -1..1 across it).
    `shadow` > 0 first darkens its outline shifted one pixel down and right."""
    (ux, uy), (vx, vy) = unit(angle_deg)
    reach = int(max(length, tail)) + 3
    cells = []
    for y in range(int(cy) - reach, int(cy) + reach + 1):
        for x in range(int(cx) - reach, int(cx) + reach + 1):
            dx, dy = x - cx, y - cy
            t = dx * ux + dy * uy
            if t < -tail or t > length:
                continue
            w = half_width(t)
            s = dx * vx + dy * vy
            if abs(s) <= w:
                cells.append((x, y, t, s / w if w else 0))
    if shadow:
        inked = {(x, y) for x, y, _t, _s in cells}
        for x, y in inked:
            if (x + 1, y + 1) not in inked:
                frame.set(x + 1, y + 1, blend(frame.get(x + 1, y + 1), (0, 0, 0), shadow))
    for x, y, t, s in cells:
        frame.set(x, y, shade(t, s))


def thin_hand(frame, cx, cy, angle_deg, length, tail, colour):
    """A one-pixel hand: a Bresenham line from its tail to its tip (no kinks)."""
    (ux, uy), _ = unit(angle_deg)
    frame.line(int(round(cx - ux * tail)), int(round(cy - uy * tail)), int(round(cx + ux * length)), int(round(cy + uy * length)), colour)


def disc(frame, cx, cy, r, colour):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                frame.set(x, y, colour(x - cx, y - cy) if callable(colour) else colour)


def hand_angles(m, sweep_seconds=None):
    hour = (m.hour % 12) * 30 + m.minute * 0.5
    minute = m.minute * 6
    return hour, minute


def draw_date(frame, m, o, cx, y, font_name, ink):
    font = load_font(font_name)
    text = m.date(o.month_first)
    M.draw_text(frame, font, int(round(cx - text_width(font, text) / 2)), y, text, ink)


# ---------------------------------------------------------------- Analogue-RD-1: the bracket clock

# The painted face of an antique bracket clock: a square engraved brass plate with corner
# spandrels and a cream enamel dial. The firmware adds what must be exact: the Roman
# cardinals (Capital Hill, whose I has serifs) and a diamond at the other hours, the
# minute track, blued-steel hands with a spade on the hour hand, a brass cap, the date in
# small letters, and, as such clocks have, a mock pendulum: a curved slot under the XII
# in which a brass bob swings once every two seconds. The seconds setting adds a thin
# red second hand that ticks.

B_CX, B_CY = 31.5, 31.5  # the enamel's centre in the picture (its radius is 28)
B_INK = (46, 30, 22)
B_TRACK = 25.0  # the minute track's radius
B_BLUE, B_BLUE_HI, B_BLUE_LO = (30, 42, 96), (86, 112, 190), (14, 20, 52)
B_SLOT_R, B_SLOT_HALF, B_SWING = 11.5, 34, 24  # the slot's radius, its half angle, the bob's swing (degrees)
B_PERIOD_MS = 2000


def blued(t, s):
    """Blued steel: a highlight along one edge, a dark edge on the other."""
    return B_BLUE_HI if s < -0.34 else B_BLUE_LO if s > 0.5 else B_BLUE


def draw_analogue_rd_1(m, o, millis=0):
    frame = Frame()
    frame.img.paste(prep_brass(), (0, 0))
    frame.px = frame.img.load()
    cx, cy = B_CX, B_CY
    # the minute track: a dot a minute, a diamond at the hours, the cardinals in Roman
    for i in range(60):
        (ux, uy), _ = unit(i * 6)
        x, y = int(round(cx + ux * B_TRACK)), int(round(cy + uy * B_TRACK))
        if i % 5:
            frame.set(x, y, lerp(frame.get(x, y), B_INK, 0.45))
        elif i % 15:
            for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                frame.set(x + dx, y + dy, B_INK)
    font = load_font("capital-hill")
    for text, angle in (("XII", 0), ("III", 90), ("VI", 180), ("IX", 270)):
        (ux, uy), _ = unit(angle)
        w = text_width(font, text) - 1
        r = B_TRACK - (5.5 if angle in (0, 180) else w / 2 + 3.0)
        M.draw_text(frame, font, int(round(cx + ux * r - w / 2)), int(round(cy + uy * r - 2.5)), text, B_INK)
    # the mock pendulum: a slot under the XII, the bob swinging in it
    t_ms = (m.second * 1000 + millis) % B_PERIOD_MS
    swing = B_SWING * math.sin(2 * math.pi * t_ms / B_PERIOD_MS)
    for y in range(int(cy) - 15, int(cy)):
        for x in range(int(cx) - 10, int(cx) + 12):
            dx, dy = x - cx, y - cy
            r = math.hypot(dx, dy)
            a = math.degrees(math.atan2(dx, -dy))
            if abs(r - B_SLOT_R) <= 1.6 and abs(a) <= B_SLOT_HALF:
                edge = abs(r - B_SLOT_R) > 0.9 or abs(a) > B_SLOT_HALF - 5
                frame.set(x, y, (92, 66, 30) if edge else (26, 18, 14))
    (ux, uy), _ = unit(swing)
    bx, by = cx + ux * B_SLOT_R, cy + uy * B_SLOT_R
    disc(frame, bx, by, 1.5, lambda dx, dy: (255, 232, 150) if dx + dy < -0.6 else (226, 172, 62))
    draw_date(frame, m, o, cx + 0.5, int(cy) + 9, "everyday-slight", lerp((233, 222, 190), B_INK, 0.62))
    hour, minute = hand_angles(m)

    def spade(t):  # the hour hand: a stem, a spade, a point
        if t < 0:
            return 1.0
        if t < 8:
            return 1.0
        if t < 11.5:
            return 1.0 + (t - 8) * 0.6
        return max(0.3, 3.1 - (t - 11.5) * 0.8)

    draw_hand(frame, cx, cy, hour, 15.5, 4, spade, blued, shadow=70)
    draw_hand(frame, cx, cy, minute, 23.5, 5, lambda t: 1.1 if t < 13 else max(0.5, 1.1 - (t - 13) * 0.06), blued, shadow=70)
    if o.seconds:
        thin_hand(frame, cx, cy, m.second * 6, 24.5, 7, (176, 34, 30))
    disc(frame, cx, cy, 2.3, lambda dx, dy: (255, 236, 160) if dx + dy < -1 else (150, 104, 36) if dx + dy > 1.6 else (222, 170, 64))
    return frame


# ---------------------------------------------------------------- Analogue-RD-2: the station clock

# A railway station clock: the painted white dial in its steel rim, generated larger than
# the panel so that the dial fills it. The firmware draws the markers (a bar at every
# hour, heavier at the quarters, a tick at every minute), black bar hands with a lighter
# edge, a hub, the date in grey, and a glint that crosses the glass now and then. With
# the seconds setting the red second hand runs the way station clocks do: once round in
# 58.5 seconds, then it waits at the top for the minute hand to jump.

S_CX, S_CY = 32.0, 31.5
S_R = 28.5  # the white dial's radius
S_BLACK, S_EDGE = (14, 14, 18), (58, 58, 66)
S_RED = (222, 30, 34)
S_SWEEP_MS = 58500
S_GLINT_EVERY_MS, S_GLINT_MS = 12000, 1500


def bar(frame, cx, cy, angle_deg, r0, r1, half_width, colour):
    draw_hand(frame, cx, cy, angle_deg, r1, -r0, lambda t: half_width, lambda t, s: colour)


def draw_analogue_rd_2(m, o, millis=0):
    frame = Frame()
    frame.img.paste(prep_station(), (0, 0))
    frame.px = frame.img.load()
    cx, cy = S_CX, S_CY
    for i in range(60):
        if i % 5:
            bar(frame, cx, cy, i * 6, S_R - 2.2, S_R - 0.4, 0.5, (96, 96, 104))
        elif i % 15:
            bar(frame, cx, cy, i * 6, S_R - 6.5, S_R - 0.4, 1.0, S_BLACK)
        else:
            bar(frame, cx, cy, i * 6, S_R - 7.5, S_R - 0.4, 1.5, S_BLACK)
    draw_date(frame, m, o, cx, int(cy) + 6, "everyday-slight", (118, 118, 128))
    hour, minute = hand_angles(m)

    def steel(t, s):
        return S_EDGE if s < -0.45 else S_BLACK

    draw_hand(frame, cx, cy, hour, 15, 4, lambda t: 2.0, steel, shadow=46)
    draw_hand(frame, cx, cy, minute, 24, 5, lambda t: 1.5, steel, shadow=46)
    disc(frame, cx, cy, 2.6, S_BLACK)
    if o.seconds:
        angle = min(1.0, (m.second * 1000 + millis) / S_SWEEP_MS) * 360
        thin_hand(frame, cx, cy, angle, 19, 7, S_RED)
        (ux, uy), _ = unit(angle)
        disc(frame, cx + ux * 19, cy + uy * 19, 2.6, S_RED)
        disc(frame, cx, cy, 1.3, S_RED)
    # the glint: a band of light that crosses the glass from the upper left
    t_ms = ((m.minute * 60 + m.second) * 1000 + millis) % S_GLINT_EVERY_MS
    if t_ms < S_GLINT_MS:
        pos = -44 + 88 * t_ms / S_GLINT_MS  # the band's place along the diagonal
        for y in range(H):
            for x in range(W):
                dx, dy = x - cx, y - cy
                if dx * dx + dy * dy > S_R * S_R:
                    continue
                d = abs((dx + dy) / math.sqrt(2) - pos)
                if d < 4:
                    frame.set(x, y, blend(frame.get(x, y), (255, 255, 255), int(70 * (1 - d / 4))))
    return frame


# ---------------------------------------------------------------- output


def save_gif(path, seq, scale=1):
    frames = [upscale(f.img, scale).quantize(colors=255, dither=Image.NONE) for f, _ in seq]
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=[d for _, d in seq], loop=0, optimize=False)


def main():
    os.makedirs(DESIGN, exist_ok=True)
    faces = (("analogue_rd_1", draw_analogue_rd_1), ("analogue_rd_2", draw_analogue_rd_2))
    times = [(10, 9, 37), (0, 0, 0), (3, 45, 12), (6, 30, 50), (8, 20, 5), (11, 55, 28)]
    for name, draw in faces:
        fr = draw(Moment(10, 9, 37), Options(seconds=True), 300)
        fr.img.save(os.path.join(DESIGN, name + ".png"))
        upscale(fr.img, 8).save(os.path.join(DESIGN, name + "@8x.png"))
        sheet = [(f"{h:02d}:{mi:02d}", draw(Moment(h, mi, s), Options(seconds=True), 300)) for h, mi, s in times]
        sheet += [("no seconds", draw(Moment(10, 9, 37), Options(), 300)), ("month first", draw(Moment(16, 40, 0), Options(month_first=True), 300))]
        M.contact_sheet(sheet, scale=4, columns=4, gap=16).save(os.path.join(DESIGN, name + "-moments.png"))
        # twelve seconds across a minute change, seconds on
        seq = []
        for i in range(120):
            mm, ms = M.at_ms(Moment(10, 9, 54), i * STEP_MS)
            seq.append((draw(mm, Options(seconds=True), ms), STEP_MS))
        save_gif(os.path.join(DESIGN, name + "@8x.gif"), seq, 8)
        save_gif(os.path.join(DESIGN, name + ".gif"), seq)
    both = [(n.replace("_", " "), d(Moment(10, 9, 37), Options(seconds=True), 300)) for n, d in faces]
    M.contact_sheet(both, scale=8, columns=2, gap=16).save(os.path.join(DESIGN, "analogue_rd-both.png"))
    print(f"review images in {DESIGN}")


if __name__ == "__main__":
    main()
