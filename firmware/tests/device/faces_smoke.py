#!/usr/bin/env python3
r"""Device smoke test of the clock faces (spec 7.1) over the HTTP API.

    python tests\device\faces_smoke.py [http://p64.local] [--keep FACE]

Puts the device in the Widget state with the clock, sets each of the nine faces in
turn and checks that the panel draws it (the frame is lit and differs from the other
faces), that the settings document echoes the face, and, for the five themed faces whose
drawing is exact (flip, nixie, words, hourglass, orrery), that the device's frame is pixel
for pixel what tools/mock_clock_faces.py draws for the same minute on the host (seconds and
the blinking colon off, so the frame does not depend on the second). The LED face breathes
(its glow follows the time of day in 200 ms steps), so its frame must be one of the ten the
host draws for the two seconds around the capture. The horizon depends
on the location, the zone and the weather, so it is only checked for a sky. Needs the
system Python with Pillow and a synced clock on the device. Puts the settings back, or
leaves the device on `--keep FACE` in the Widget state. Exit code 1 on any failure.
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))

import api_smoke
from api_smoke import check, request
import mock_clock_faces as mock

FACES = ["digital", "analogue", "flip", "nixie", "horizon", "words", "hourglass", "orrery", "led", "horizon_rd", "aquarium"]
EXACT = {"flip": mock.draw_flip, "nixie": mock.draw_nixie, "words": mock.draw_words,
         "hourglass": mock.draw_hourglass, "orrery": mock.draw_orrery}


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def frame(base):
    st, r = request(base, "GET", "/api/v1/frame.raw", raw=True)
    assert st == 200 and len(r) == 64 * 64 * 3
    return r


def lit(px):
    return sum(1 for i in range(0, len(px), 3) if px[i] or px[i + 1] or px[i + 2])


def settings(base, patch):
    st, j = request(base, "PUT", "/api/v1/settings", patch)
    assert st == 200, j
    return j["data"]


def local_moment(base):
    """The device's local time as a mock Moment, or None while the clock is not synced."""
    t = status(base).get("time", {})
    text = t.get("local") or ""
    m = re.match(r"(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})", text)
    if not t.get("synced") or not m:
        return None
    year, mon, day, hh, mm, ss = (int(v) for v in m.groups())
    tt = time.struct_time(time.strptime(f"{year}-{mon:02d}-{day:02d}", "%Y-%m-%d"))
    return mock.Moment(hh, mm, ss, (tt.tm_wday + 1) % 7, day, mon, tt.tm_yday - 1)


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    keep = sys.argv[sys.argv.index("--keep") + 1] if "--keep" in sys.argv else None
    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    clock = original["clock"]
    opts = mock.Options(seconds=False, blink=False, h24=clock["h24"], month_first=clock["date_order"] == "month_day")
    check(local_moment(base) is not None, "the device's clock is synced")
    settings(base, {"show": {"main_state": "widget"}, "widgets": {"widget": "clock"},
                    "clock": {"seconds": False, "blink_colon": False}})
    frames = {}
    for face in FACES:
        s = settings(base, {"clock": {"face": face}})
        check(s["clock"]["face"] == face, "settings echo the face " + face)
        time.sleep(2.5)
        # the frame and the minute it was drawn for: retry when the minute turned in between
        for _ in range(3):
            before = local_moment(base)
            px = frame(base)
            after = local_moment(base)
            if before and after and (before.hour, before.minute) == (after.hour, after.minute):
                break
            time.sleep(1)
        frames[face] = px
        check(lit(px) > 20, "%s draws something (%d lit pixels)" % (face, lit(px)))
        if face in EXACT and before:
            expected = EXACT[face](before, opts).img.tobytes()
            if px != expected and face == "flip":
                time.sleep(1)  # a change may have been mid-flip
                px = frame(base)
            diff = sum(1 for i in range(0, len(px), 3) if px[i:i + 3] != expected[i:i + 3])
            check(diff == 0, "%s on the device is the host render for %02d:%02d (%d pixels differ)" % (face, before.hour, before.minute, diff))
        if face == "led" and before and after:
            style = clock.get("led_style", "red")
            wanted = [mock.draw_led(mo, opts, ms, None, 0, style).img.tobytes() for mo in (before, after) for ms in range(0, 1000, 200)]
            check(px in wanted, "led (%s) on the device is one of the host's frames of %02d:%02d:%02d" % (style, before.hour, before.minute, before.second))
        if face in ("horizon", "horizon_rd"):
            # a sky: the top rows are not black and vary down the frame
            top = px[0:64 * 3]
            mid = px[30 * 64 * 3:31 * 64 * 3]
            check(lit(top) > 40 and top != mid, "the %s draws a sky" % face)
        if face == "aquarium" and before:
            # the time on the sign: its ink is drawn after the lamp's dimming, so the pixels
            # in the ink's colour are the host's whatever the hour (the fish never are)
            ink = bytes(mock.SIGN_INK)
            want = mock.draw_aquarium(before, opts).img.tobytes()
            ours = {i for i in range(0, len(px), 3) if px[i:i + 3] == ink}
            host = {i for i in range(0, len(want), 3) if want[i:i + 3] == ink}
            check(ours == host and len(host) > 20, "the aquarium's sign shows %02d:%02d as the host draws it (%d ink pixels)" % (before.hour, before.minute, len(host)))
    names = list(frames)
    distinct = all(frames[a] != frames[b] for i, a in enumerate(names) for b in names[i + 1:])
    check(distinct, "the %d faces all differ" % len(names))
    # the flip's minute change: with the seconds on, the rail ticks; the frame keeps changing
    settings(base, {"clock": {"face": "flip", "seconds": True}})
    time.sleep(2.5)
    a = frame(base)
    time.sleep(2.5)
    b = frame(base)
    check(a != b, "the flip's seconds rail advances")
    settings(base, {"clock": {"face": "hourglass"}})
    time.sleep(2.5)
    a = frame(base)
    time.sleep(1.5)
    b = frame(base)
    check(a != b, "the hourglass's stream moves with the second")
    settings(base, {"clock": {"face": "orrery", "blink_colon": True}})
    time.sleep(2.5)
    a = frame(base)
    time.sleep(1.2)
    b = frame(base)
    check(a != b, "the orrery redraws every second with Mercury and the blinking colon")
    # the two faces from PNG art move on their own: the lake's glints, the tank
    settings(base, {"clock": {"face": "horizon_rd", "seconds": False, "blink_colon": False}})
    time.sleep(2.0)
    a = frame(base)
    time.sleep(0.8)
    b = frame(base)
    check(a != b, "the Horizon-RD's lake glints between two frames 0.8 s apart")
    settings(base, {"clock": {"face": "aquarium"}})
    time.sleep(2.0)
    a = frame(base)
    time.sleep(0.5)
    b = frame(base)
    check(a != b, "the aquarium moves between two frames 0.5 s apart")
    # the LED breathes (the glow) even with the seconds off, and every style draws
    settings(base, {"clock": {"face": "led", "seconds": False, "blink_colon": False}})
    time.sleep(1.5)
    a = frame(base)
    time.sleep(0.7)
    b = frame(base)
    check(a != b, "the LED's glow breathes between two frames 0.7 s apart")
    styled = {}
    for style in ("red", "green", "amber", "blue", "vfd"):
        s = settings(base, {"clock": {"led_style": style}})
        check(s["clock"]["led_style"] == style, "settings echo the LED style " + style)
        time.sleep(1.0)
        styled[style] = frame(base)
        check(lit(styled[style]) > 20, "the %s style draws" % style)
    check(len(set(styled.values())) == 5, "the five LED styles all differ")
    settings(base, {"clock": {"led_style": clock.get("led_style", "red")}})
    # memory after all the faces
    st, mem = request(base, "GET", "/api/v1/diag/memory")
    if st == 200:
        heap = mem["data"].get("heap", {}).get("internal", {})
        print("  internal heap after the faces: %s free, largest %s" % (heap.get("free"), heap.get("largest_free")))
    if keep:
        settings(base, {"show": {"main_state": "widget"}, "widgets": {"widget": "clock"},
                        "clock": {"face": keep, "seconds": clock["seconds"], "blink_colon": clock["blink_colon"]}})
        print("  left on the %s face" % keep)
    else:
        settings(base, {"show": {"main_state": original["show"]["main_state"]}, "widgets": {"widget": original["widgets"]["widget"]},
                        "clock": {"face": clock["face"], "seconds": clock["seconds"], "blink_colon": clock["blink_colon"]}})
    return api_smoke.failures


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
