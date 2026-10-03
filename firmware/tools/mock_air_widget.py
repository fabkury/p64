#!/usr/bin/env python3
"""The air widget's design reference (p074): air quality and UV from Open-Meteo on 64x64.

Draws the widget with the firmware's own font tables (through mock_clock_faces' helpers)
and writes
  docs/design/air-widget/*.png      the states, enlarged, and a contact sheet
  tests/host/corpus/air/*.ppm       the pixel-exact references the host tests compare with,
  tests/host/corpus/air/*.txt       and the data each was drawn from

The C++ (p64_widgets/src/air_model.cpp, faces.cpp draw_air) must match this file pixel
for pixel: edit here first, then the C++.

    python tools/mock_air_widget.py
"""
import os

from PIL import Image

from mock_clock_faces import FIRMWARE, Frame, W, H, draw_text, load_font, text_width, upscale

DESIGN = os.path.join(FIRMWARE, "docs", "design", "air-widget")
CORPUS = os.path.join(FIRMWARE, "tests", "host", "corpus", "air")

INK = (255, 255, 255)
DIM = (140, 140, 160)
FAINT = (70, 70, 84)
UV_LINE = (255, 255, 255)

# (upper bound inclusive, word, colour). The colours are the indexes' own, the darkest
# two lifted so they read as text on a black panel.
US_BANDS = [
    (50, "GOOD", (0, 228, 0)),
    (100, "MODERATE", (255, 255, 0)),
    (150, "SENSITIVE", (255, 126, 0)),
    (200, "UNHEALTHY", (255, 0, 0)),
    (300, "V.UNHEALTHY", (170, 80, 190)),
    (10**9, "HAZARDOUS", (190, 0, 50)),
]
EU_BANDS = [
    (20, "GOOD", (80, 240, 230)),
    (40, "FAIR", (80, 204, 170)),
    (60, "MODERATE", (240, 230, 65)),
    (80, "POOR", (255, 80, 80)),
    (100, "VERY POOR", (190, 0, 60)),
    (10**9, "EXTREME", (160, 60, 170)),
]
UV_BANDS = [
    (2, "LOW", (60, 200, 0)),
    (5, "MODERATE", (247, 228, 0)),
    (7, "HIGH", (248, 120, 0)),
    (10, "VERY HIGH", (230, 0, 30)),
    (10**9, "EXTREME", (150, 100, 255)),
]

GRAPH_X = 8  # 24 bars of 2 px
GRAPH_TOP = 43
GRAPH_H = 18  # bars end on row 60; the hour ticks sit on rows 62 and 63


def band(bands, value):
    for upper, word, colour in bands:
        if value <= upper:
            return word, colour
    return bands[-1][1], bands[-1][2]


def scale_top(bands, peak):
    """The graph's full height: the upper bound of the band the day's peak is in, at
    least the second band's (so a clean day does not fill the graph); in the open last
    band the peak itself, at least one more band's width."""
    bounded = [upper for upper, _w, _c in bands[:-1]]
    if peak > bounded[-1]:
        return max(peak, 2 * bounded[-1] - bounded[-2])
    for upper in bounded[1:]:
        if peak <= upper:
            return upper
    return bounded[-1]


def dimmed(c, pct):
    return tuple((v * pct + 50) // 100 for v in c)


def round_half_up(v):
    return int(v + 0.5)


def number_text(v):
    """One decimal below 10, whole above (PM values)."""
    return ("%.1f" % v) if v < 9.95 else str(round_half_up(v))


def draw_air(data, european=False, location_set=True, error="", age_min=0, stale=False):
    """data: dict(aqi, uv, pm2_5, pm10, hour, aqi_hours[24], uv_hours[24]) in the chosen index."""
    f = Frame()
    font = load_font("capital-hill")
    small = load_font("everyday-slight")
    big = load_font("everyday-vast-black")  # the numbers: every font at its native size
    if not location_set:
        draw_c(f, font, 20, "AIR", INK)
        draw_c(f, font, 32, "SET A", DIM)
        draw_c(f, font, 40, "LOCATION", DIM)
        return f
    if data is None or stale:
        draw_c(f, font, 20, "AIR", INK)
        draw_c(f, font, 32, "NO DATA", DIM)
        if error:
            draw_c(f, font, 44, error[:10], DIM)
        return f
    bands = EU_BANDS if european else US_BANDS
    aqi = data["aqi"]
    uv = round_half_up(data["uv"])
    aqi_word, aqi_colour = band(bands, aqi)
    uv_word, uv_colour = band(UV_BANDS, uv)

    # The two numbers: AQI on the left, UV on the right.
    draw_text(f, big, 1, 1, str(aqi), aqi_colour)
    uv_text = str(uv)
    draw_text(f, big, W - 1 - text_width(big, uv_text), 1, uv_text, uv_colour)
    # Their words, each after its label: AQI's from the left, UV's to the right.
    x = 1 + draw_text(f, small, 1, 15, "AQI", DIM) + 3
    draw_text(f, small, x, 15, aqi_word, aqi_colour)
    x = W - 1 - text_width(small, uv_word)
    draw_text(f, small, x, 22, uv_word, uv_colour)
    draw_text(f, small, x - 3 - text_width(small, "UV"), 22, "UV", DIM)
    # The particulates, and the data's age when it is old.
    draw_text(f, small, 1, 29, "PM2.5 %s" % number_text(data["pm2_5"]), DIM)
    draw_text(f, small, 1, 36, "PM10 %s" % number_text(data["pm10"]), DIM)
    if age_min >= 90:
        age = "%dH" % (age_min // 60)
        draw_text(f, small, W - 1 - text_width(small, age), 36, age, DIM)

    # The day: 24 bars of the hourly index in their band colours, the hours that are not
    # now dimmed; the UV index over them as a line.
    hours = data["aqi_hours"]
    top = scale_top(bands, max(hours))
    for h, v in enumerate(hours):
        if v is None:
            continue
        height = max(1, min(GRAPH_H, (v * GRAPH_H + top // 2) // top))
        colour = band(bands, v)[1]
        if h != data["hour"]:
            colour = dimmed(colour, 50)
        f.fill_rect(GRAPH_X + h * 2, GRAPH_TOP + GRAPH_H - height, 2, height, colour)
    uv_hours = data["uv_hours"]
    uv_top = max(8, round_half_up(max(v or 0 for v in uv_hours)))
    prev = None
    for h, v in enumerate(uv_hours):
        if v is None or v < 0.5:
            prev = None
            continue
        y = GRAPH_TOP + GRAPH_H - 1 - min(GRAPH_H - 1, round_half_up(v * (GRAPH_H - 1) / uv_top))
        x = GRAPH_X + h * 2
        if prev is not None:
            f.line(prev[0], prev[1], x, y, UV_LINE)
        f.set(x, y, UV_LINE)
        f.set(x + 1, y, UV_LINE)
        prev = (x + 1, y)
    # The hour ticks: midnight, 6, noon, 18 faint; now in white.
    for h in (0, 6, 12, 18, 24):
        f.set(min(GRAPH_X + h * 2, GRAPH_X + 47), 62, FAINT)
    f.fill_rect(GRAPH_X + data["hour"] * 2, 62, 2, 1, INK)
    return f


def draw_c(f, font, y, text, colour):
    draw_text(f, font, (W - text_width(font, text)) // 2, y, text, colour)


def day(aqi_hours, uv_peak, hour, pm2_5, pm10):
    uv_hours = [0.0] * 24
    for h in range(7, 19):
        x = (h - 12.5) / 5.5
        uv_hours[h] = round(max(0.0, uv_peak * (1 - x * x)), 2)  # two decimals, as the API gives
    return dict(aqi=aqi_hours[hour], uv=uv_hours[hour], pm2_5=pm2_5, pm10=pm10, hour=hour, aqi_hours=aqi_hours,
                uv_hours=uv_hours)


def ramp(a, b, c):
    """24 hourly values from a (midnight) through b (noon) to c (the next midnight)."""
    return [round(a + (b - a) * h / 12) if h <= 12 else round(b + (c - b) * (h - 12) / 12) for h in range(24)]


NEW_YORK = dict(aqi=44, uv=1.4, pm2_5=5.7, pm10=5.7, hour=16,
                aqi_hours=[55, 55, 54, 54, 53, 53, 52, 52, 51, 51, 51, 51, 50, 49, 47, 45, 44, 42, 40, 40, 41, 43, 45, 48],
                uv_hours=[0, 0, 0, 0, 0, 0, 0, 0, 0.4, 1.3, 2.75, 4.25, 5.25, 5.35, 4.5, 3.0, 1.4, 0.45, 0.05, 0, 0, 0, 0, 0])
NEW_YORK_EU = dict(NEW_YORK, aqi=38,
                   aqi_hours=[29, 24, 20, 19, 18, 21, 24, 29, 31, 28, 23, 23, 28, 32, 35, 37, 38, 35, 41, 54, 63, 64, 63, 61])

# name, what it shows, the data, draw_air's other arguments
STATES = [
    ("good", "New York, 3 Oct, 16:00 (real reply)", NEW_YORK, {}),
    ("moderate", "moderate, UV 7 at 13:00", day(ramp(60, 95, 70), 7.2, 13, 22.4, 31.0), {}),
    ("sensitive", "sensitive groups, UV 9", day(ramp(80, 142, 110), 9.4, 12, 48.0, 66.0), {}),
    ("unhealthy", "unhealthy, UV 12", day(ramp(120, 188, 160), 12.3, 12, 88.0, 120.0), {}),
    ("very-unhealthy", "very unhealthy, night", day(ramp(180, 260, 240), 3.0, 22, 190.0, 240.0), {}),
    ("hazardous", "hazardous, 3 digits everywhere", day(ramp(300, 480, 420), 11.0, 12, 388.0, 520.0), {}),
    ("european", "European index, same real reply", NEW_YORK_EU, {"european": True}),
    ("old", "data 2 h old", NEW_YORK, {"age_min": 130}),
    ("no-location", "no location set", None, {"location_set": False}),
    ("no-data", "no data (error shown)", None, {"error": "HTTP 500"}),
]


def write_state(path, data, args):
    """The state as the host test reads it: one 'key values' line each."""
    with open(path, "w", newline="\n") as fh:
        fh.write("european %d\n" % args.get("european", False))
        fh.write("location_set %d\n" % args.get("location_set", True))
        fh.write("age_min %d\n" % args.get("age_min", 0))
        fh.write("error %s\n" % args.get("error", "-").replace(" ", "_"))
        fh.write("valid %d\n" % (data is not None))
        if data is None:
            return
        fh.write("hour %d\naqi %d\nuv %.2f\npm2_5 %.1f\npm10 %.1f\n" % (data["hour"], data["aqi"], data["uv"], data["pm2_5"], data["pm10"]))
        fh.write("aqi_hours %s\n" % " ".join(str(v) for v in data["aqi_hours"]))
        fh.write("uv_hours %s\n" % " ".join("%.2f" % v for v in data["uv_hours"]))


def write_ppm(path, img):
    with open(path, "wb") as fh:
        fh.write(b"P6\n%d %d\n255\n" % img.size)
        fh.write(img.tobytes())


def main():
    os.makedirs(DESIGN, exist_ok=True)
    os.makedirs(CORPUS, exist_ok=True)
    scale, pad = 6, 8
    sheet = Image.new("RGB", (5 * (W * scale + pad) + pad, 2 * (H * scale + pad) + pad), (32, 32, 36))
    for i, (name, _label, data, args) in enumerate(STATES):
        frame = draw_air(data, **args)
        write_ppm(os.path.join(CORPUS, name + ".ppm"), frame.img)
        write_state(os.path.join(CORPUS, name + ".txt"), data, args)
        big = upscale(frame.img, scale)
        big.save(os.path.join(DESIGN, name + ".png"))
        sheet.paste(big, (pad + (i % 5) * (W * scale + pad), pad + (i // 5) * (H * scale + pad)))
    sheet.save(os.path.join(DESIGN, "sheet.png"))
    print("wrote %d states to %s and %s" % (len(STATES), DESIGN, CORPUS))


if __name__ == "__main__":
    main()
