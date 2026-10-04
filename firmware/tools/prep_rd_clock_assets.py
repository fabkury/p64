#!/usr/bin/env python3
r"""Cuts the Retro Diffusion pictures into the assets of the two faces drawn from them
(p075): Horizon-RD and the aquarium.

    python tools\prep_rd_clock_assets.py        (from firmware/; needs Pillow; free, offline)

Reads assets/rd-source/used/ (the raw outputs of the paid calls, each listed with its exact
payload and price in assets/rd-source/calls.json; the fish_*_k20 strips and moon_k12 are
the service's free k-centroid downscales) and writes assets/clock-png/horizon_rd/ and
aquarium/, which tools/gen_clock_assets.py embeds in the firmware as PNG files (the faces
decode them when they start). Every step here is deterministic, so the assets can be
rebuilt without spending anything; edit a constant below and rerun rather than editing the
PNGs. tools/mock_clock_faces.py draws the faces from the result.

Horizon-RD (the land sits at row LAND_Y of the frame):
  land.png     the landscape, its white sky and its painted sun keyed out
  lake.png     the lake's pixels (white where water), which mirror the computed sky
  lights.png   the cabin's windows, lit when it is dark
  sun.png, moon.png, cloud-a.png, cloud-b.png

Aquarium:
  tank.png     the 16 frames of the tank side by side (the plant sways, bubbles rise), the
               sign widened to hold the time, black outside the glass
  front.png    the same frames with only what stands in front of the far fish
  sign.png     the sign's board, drawn last
  lights.png   the castle's windows, lit at night
  fish-a.png, fish-b.png, fish-c.png   eight frames each, facing right
"""

import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.abspath(os.path.join(HERE, ".."))
SRC = os.path.join(FIRMWARE, "assets", "rd-source", "used")
ASSETS = os.path.join(FIRMWARE, "assets", "clock-png")

LAND_Y = 26  # the first row of land.png in the frame
SUN_ROWS = 26  # the painted sun lives above this row of the source
WINDOWS = [(11, 55), (11, 56), (18, 56), (19, 56), (18, 57), (19, 57)]  # the cabin's, in the source
WINDOW_COLOUR = (255, 206, 96, 255)
LAKE_COLOURS = {(105, 152, 162), (138, 178, 192), (165, 201, 213), (76, 122, 135), (119, 138, 151)}
LAKE_TOP = 51

TANK_FRAMES = 16
OUTSIDE = (148, 149, 151)  # the grey around the glass
WATER = [(122, 188, 195), (100, 160, 171), (53, 104, 119)]
BOARD = (33, 19, 50, 29)  # the sign's board in the source, [x0, y0, x1, y1)
BOARD_X, BOARD_W = 27, 27  # where the widened board goes
# the castle's window and door, [x0, y0, x1, y1) in the source, and their glow at night
CASTLE_LIGHTS = [((14, 32, 16, 37), (255, 200, 90, 255)), ((13, 44, 16, 49), (214, 138, 60, 255))]
FISH_KEY = (40, 90, 140)  # the flat colour the fish were animated on
FISH = {"fish-a": ("fish_a_k20.png", 20), "fish-b": ("fish_b_idle_0.gif", 32), "fish-c": ("fish_d_k20.png", 20)}


def src(name):
    return os.path.join(SRC, name)


def save(face, name, img):
    path = os.path.join(ASSETS, face, name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print(f"{face}/{name}.png {img.width}x{img.height}")


def near(a, b, tol):
    return all(abs(a[i] - b[i]) <= tol for i in range(3))


def gif_frames(path):
    im = Image.open(path)
    out = []
    for i in range(im.n_frames):
        im.seek(i)
        out.append(im.convert("RGB"))
    return out


def crop_alpha(img):
    return img.crop(img.getbbox())


# ---------------------------------------------------------------- Horizon-RD


def horizon_rd():
    day = Image.open(src("land_1.png")).convert("RGB")
    px = day.load()
    # the sky: the white connected to the top edge, and everything of the painted sun
    sky = [[False] * 64 for _ in range(64)]
    todo = [(x, 0) for x in range(64)]
    while todo:
        x, y = todo.pop()
        if not (0 <= x < 64 and 0 <= y < 64) or sky[y][x]:
            continue
        if min(px[x, y]) < 226 and y >= SUN_ROWS:
            continue
        sky[y][x] = True
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    land = Image.new("RGBA", (64, 64 - LAND_Y), (0, 0, 0, 0))
    lake = Image.new("RGBA", land.size, (0, 0, 0, 0))
    for y in range(LAND_Y, 64):
        for x in range(64):
            if sky[y][x]:
                continue
            land.putpixel((x, y - LAND_Y), px[x, y] + (255,))
            if y >= LAKE_TOP and px[x, y] in LAKE_COLOURS:
                lake.putpixel((x, y - LAND_Y), (255, 255, 255, 255))
    lights = Image.new("RGBA", land.size, (0, 0, 0, 0))
    for x, y in WINDOWS:
        lights.putpixel((x, y - LAND_Y), WINDOW_COLOUR)
    save("horizon_rd", "land", land)
    save("horizon_rd", "lake", lake)
    save("horizon_rd", "lights", lights)

    save("horizon_rd", "sun", crop_alpha(Image.open(src("sun3_1.png")).convert("RGBA")))
    moon = Image.open(src("moon_k12.png")).convert("RGBA")  # on black, from the downscale
    for y in range(moon.height):
        for x in range(moon.width):
            if max(moon.getpixel((x, y))[:3]) < 40:
                moon.putpixel((x, y), (0, 0, 0, 0))
    save("horizon_rd", "moon", moon)
    save("horizon_rd", "cloud-a", crop_alpha(Image.open(src("cloud_0.png")).convert("RGBA")))
    save("horizon_rd", "cloud-b", crop_alpha(Image.open(src("cloud_1.png")).convert("RGBA")))


# ---------------------------------------------------------------- Aquarium


def wide_board(frame0):
    """The sign's board stretched to BOARD_W: its two ends kept, its middle columns repeated."""
    x0, y0, x1, y1 = BOARD
    board = frame0.crop(BOARD)
    ends = 4
    mid = board.crop((ends, 0, board.width - ends, board.height))
    out = Image.new("RGB", (BOARD_W, board.height))
    out.paste(board.crop((0, 0, ends, board.height)), (0, 0))
    x = ends
    while x < BOARD_W - ends:
        out.paste(mid, (x, 0))
        x += mid.width
    out.paste(board.crop((board.width - ends, 0, board.width, board.height)), (BOARD_W - ends, 0))
    return out


def aquarium():
    frames = gif_frames(src("tank_subtle_0.gif"))
    assert len(frames) == TANK_FRAMES
    board = wide_board(frames[0])
    tank = Image.new("RGB", (64 * TANK_FRAMES, 64))
    front = Image.new("RGBA", tank.size, (0, 0, 0, 0))
    for i, f in enumerate(frames):
        f = f.copy()
        px = f.load()
        # black outside the glass: the grey connected to the picture's edge
        todo = [(x, y) for x in range(64) for y in (0, 63)] + [(x, y) for y in range(64) for x in (0, 63)]
        seen = set()
        while todo:
            x, y = todo.pop()
            if not (0 <= x < 64 and 0 <= y < 64) or (x, y) in seen or not near(px[x, y], OUTSIDE, 10):
                continue
            seen.add((x, y))
            px[x, y] = (0, 0, 0)
            todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
        f.paste(board, (BOARD_X, BOARD[1]))
        tank.paste(f, (64 * i, 0))
        for y in range(64):
            for x in range(64):
                if (x, y) in seen or any(near(px[x, y], w, 14) for w in WATER):
                    continue
                front.putpixel((64 * i + x, y), px[x, y] + (255,))
    save("aquarium", "tank", tank)
    save("aquarium", "front", front)
    save("aquarium", "sign", board)
    lights = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    for (x0, y0, x1, y1), glow in CASTLE_LIGHTS:
        for y in range(y0, y1):
            for x in range(x0, x1):
                lights.putpixel((x, y), glow)
    save("aquarium", "lights", lights)
    for name, (file, cell) in FISH.items():
        if file.endswith(".gif"):
            cells = gif_frames(src(file))
        else:
            strip = Image.open(src(file)).convert("RGB")
            cells = [strip.crop((i * cell, 0, (i + 1) * cell, cell)) for i in range(strip.width // cell)]
        keyed = []
        for c in cells:
            k = c.convert("RGBA")
            for y in range(k.height):
                for x in range(k.width):
                    if near(k.getpixel((x, y)), FISH_KEY, 24):
                        k.putpixel((x, y), (0, 0, 0, 0))
            keyed.append(k)
        # one box for every frame, so the fish does not jump as its tail moves
        boxes = [k.getbbox() for k in keyed]
        box = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
        w, h = box[2] - box[0], box[3] - box[1]
        sheet = Image.new("RGBA", (w * len(keyed), h), (0, 0, 0, 0))
        for i, k in enumerate(keyed):
            sheet.paste(k.crop(box), (i * w, 0))
        save("aquarium", name, sheet)


# ---------------------------------------------------------------- Bracket and station


def dials():
    """The two analogue dials, baked like the other sprites (assets/clock/). Both were
    generated at 80x80 and the middle 64x64 is kept, so that the dial fills the panel."""
    baked = os.path.join(FIRMWARE, "assets", "clock")
    brass = Image.open(src("brass3_0.png")).convert("RGB").crop((8, 8, 72, 72))
    os.makedirs(os.path.join(baked, "bracket"), exist_ok=True)
    brass.save(os.path.join(baked, "bracket", "dial.png"), optimize=True)
    print("bracket/dial.png 64x64 (assets/clock)")
    station = Image.open(src("station3_1.png")).convert("RGB").crop((8, 9, 72, 73))
    px = station.load()
    # black outside the rim: the dark grey connected to the picture's edge
    todo = [(x, y) for x in range(64) for y in (0, 63)] + [(x, y) for y in range(64) for x in (0, 63)]
    seen = set()
    while todo:
        x, y = todo.pop()
        if not (0 <= x < 64 and 0 <= y < 64) or (x, y) in seen or max(px[x, y]) > 58:
            continue
        seen.add((x, y))
        px[x, y] = (0, 0, 0)
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    os.makedirs(os.path.join(baked, "station"), exist_ok=True)
    station.save(os.path.join(baked, "station", "dial.png"), optimize=True)
    print("station/dial.png 64x64 (assets/clock)")


if __name__ == "__main__":
    horizon_rd()
    aquarium()
    dials()
