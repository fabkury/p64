#!/usr/bin/env python3
r"""Draws the weather widget's icons (spec 7.2) and converts them into a C++ table.

    python tools\gen_weather_icons.py        (from firmware/; needs Pillow)

One pixel-art icon per WMO weather-code group, day and night, at 24x24 and 12x12:
clear, partly cloudy, overcast, fog, drizzle, rain, freezing rain, snow, rain showers,
snow showers, thunderstorm, thunderstorm with hail. The PNGs land in assets/weather/
(committed, so they can be redrawn by hand) and components/p64_widgets/src/
weather_icons.cpp holds them as RGBA arrays. Rerun after editing a PNG by hand: the
script only draws a PNG that does not exist yet.
"""

import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.abspath(os.path.join(HERE, ".."))
ASSETS = os.path.join(FIRMWARE, "assets", "weather")
OUT = os.path.join(FIRMWARE, "components", "p64_widgets", "src", "weather_icons.cpp")

GROUPS = ["clear", "partly", "overcast", "fog", "drizzle", "rain", "freezing", "snow", "showers", "snow_showers",
          "thunder", "hail"]

SUN = (255, 200, 60, 255)
MOON = (220, 220, 240, 255)
CLOUD = (225, 228, 236, 255)
DARK_CLOUD = (150, 155, 170, 255)
RAIN = (90, 160, 255, 255)
SNOW = (240, 245, 255, 255)
FOG = (185, 190, 200, 255)
BOLT = (255, 230, 80, 255)
ICE = (170, 220, 255, 255)


def sun(d, cx, cy, r, s):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=SUN)
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0), (-1, -1), (1, 1), (-1, 1), (1, -1)):
        x0 = cx + dx * (r + 2 * s)
        y0 = cy + dy * (r + 2 * s)
        d.rectangle((x0 - s // 2, y0 - s // 2, x0 + s // 2, y0 + s // 2), fill=SUN)


def moon(d, cx, cy, r):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=MOON)
    d.ellipse((cx - r + r // 2 + 1, cy - r - 1, cx + r + r // 2 + 1, cy + r - 1), fill=(0, 0, 0, 0))


def cloud(d, x, y, w, h, colour):
    d.rectangle((x + h // 3, y + h // 2, x + w - h // 3, y + h), fill=colour)
    d.ellipse((x, y + h // 3, x + h, y + h), fill=colour)
    d.ellipse((x + w - h, y + h // 3, x + w, y + h), fill=colour)
    d.ellipse((x + w // 4, y, x + w // 4 + h, y + h), fill=colour)


def drops(d, x, y, w, count, colour, length):
    step = max(1, w // count)
    for i in range(count):
        px = x + i * step + step // 2
        d.line((px, y, px - 1, y + length), fill=colour)


def flakes(d, x, y, w, count, colour, size):
    step = max(1, w // count)
    for i in range(count):
        px = x + i * step + step // 2
        py = y + (i % 2) * size
        d.line((px - size, py, px + size, py), fill=colour)
        d.line((px, py - size, px, py + size), fill=colour)


def draw_icon(group, night, size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size // 12  # 2 at 24, 1 at 12
    body = SUN if not night else MOON
    if group == "clear":
        if night:
            moon(d, size // 2, size // 2, size // 3)
        else:
            sun(d, size // 2, size // 2, size // 4, s)
    elif group == "partly":
        if night:
            moon(d, size // 3, size // 3, size // 5)
        else:
            sun(d, size // 3, size // 3, size // 6, s)
        cloud(d, size // 4, size // 2 - s, size - size // 4 - 1, size // 3, CLOUD)
    elif group == "overcast":
        cloud(d, s, size // 4, size - 2 * s - 1, size // 3 + s, DARK_CLOUD)
        cloud(d, size // 4, size // 3 + s, size - size // 4 - 1, size // 3, CLOUD)
    elif group == "fog":
        cloud(d, s, s, size - 2 * s - 1, size // 3, CLOUD)
        for i in range(3):
            y = size // 2 + s + i * (2 * s + 1)
            d.line((s + (i % 2) * 2 * s, y, size - s - 1 - ((i + 1) % 2) * 2 * s, y), fill=FOG, width=s)
    elif group in ("drizzle", "rain", "showers", "freezing"):
        if group == "showers" and not night:
            sun(d, size // 3, size // 4, size // 7, s)
        if group == "showers" and night:
            moon(d, size // 3, size // 4, size // 6)
        cloud(d, s, size // 6 + s, size - 2 * s - 1, size // 3, CLOUD if group != "rain" else DARK_CLOUD)
        colour = ICE if group == "freezing" else RAIN
        count = 3 if group == "drizzle" else 4
        drops(d, 2 * s, size // 2 + 2 * s, size - 4 * s, count, colour, 2 * s + (0 if group == "drizzle" else s))
    elif group in ("snow", "snow_showers"):
        if group == "snow_showers":
            if night:
                moon(d, size // 3, size // 4, size // 6)
            else:
                sun(d, size // 3, size // 4, size // 7, s)
        cloud(d, s, size // 6 + s, size - 2 * s - 1, size // 3, CLOUD)
        flakes(d, 2 * s, size // 2 + 3 * s, size - 4 * s, 3, SNOW, s)
    elif group in ("thunder", "hail"):
        cloud(d, s, size // 8, size - 2 * s - 1, size // 3, DARK_CLOUD)
        cx = size // 2
        top = size // 2 - s
        d.polygon([(cx + s, top), (cx - s, top + 3 * s), (cx, top + 3 * s), (cx - s, size - 2 * s), (cx + 2 * s, top + 2 * s),
                   (cx, top + 2 * s)], fill=BOLT)
        if group == "hail":
            for px in (3 * s, size - 4 * s):
                d.rectangle((px, size - 3 * s, px + s, size - 2 * s), fill=ICE)
    return img


def main():
    os.makedirs(ASSETS, exist_ok=True)
    out = ["// Generated by tools/gen_weather_icons.py from assets/weather; do not edit.",
           "#include \"weather_icons.hpp\"", "", "namespace p64::widgets::icons {", "namespace {"]
    names = []
    for group in GROUPS:
        for night in (False, True):
            for size in (24, 12):
                name = f"{group}_{'night' if night else 'day'}_{size}"
                path = os.path.join(ASSETS, name + ".png")
                if not os.path.exists(path):
                    draw_icon(group, night, size).save(path)
                img = Image.open(path).convert("RGBA")
                if img.size != (size, size):
                    sys.exit(f"{path}: expected {size}x{size}, got {img.size}")
                data = list(img.getdata())
                out.append(f"const uint8_t k_{name}[] = {{")
                for i in range(0, len(data), 12):
                    out.append("    " + ", ".join(f"{r},{g},{b},{a}" for r, g, b, a in data[i:i + 12]) + ",")
                out.append("};")
                names.append((group, night, size, name))
    out.append("}  // namespace")
    out.append("")
    out.append("const Icon kIcons[] = {")
    for group, night, size, name in names:
        out.append(f"    {{Group::{group.title().replace('_', '')}, {'true' if night else 'false'}, {size}, k_{name}}},")
    out.append("};")
    out.append(f"const size_t kIconCount = {len(names)};")
    out.append("")
    out.append("}  // namespace p64::widgets::icons")
    out.append("")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print("wrote", OUT, "with", len(names), "icons")


if __name__ == "__main__":
    sys.exit(main())
