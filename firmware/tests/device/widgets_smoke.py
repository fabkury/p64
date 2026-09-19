#!/usr/bin/env python3
r"""Device smoke test of the widgets (M7) over the HTTP API.

    python tests\device\widgets_smoke.py [http://p64.local]

Checks the sensor reading in the status, the Widget state with each widget (the panel
frame changes and is not black), the clock overlay on an artwork, the weather fetch
after setting a location (needs internet), an interlude at auto-swap, and puts the
settings back. Exit code 1 on any failure.
"""

import io
import sys
import time

from api_smoke import check, request

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None


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


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    d = status(base)
    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    sn = d.get("sensor", {})
    check(sn.get("valid"), "sensor reading valid: %.1f C, %.0f %% RH" % (sn.get("temperature_c", 0), sn.get("humidity", 0)))
    check(-20 < sn.get("temperature_c", -100) < 80, "sensor temperature plausible")

    # The clock overlay on an artwork: enabling and disabling it changes the frame.
    settings(base, {"show": {"main_state": "animation_show", "clock_overlay": {"enabled": True, "corner": "top_left"}}})
    time.sleep(2)
    with_overlay = frame(base)
    settings(base, {"show": {"clock_overlay": {"enabled": False}}})
    time.sleep(2)
    without = frame(base)
    check(with_overlay != without, "the clock overlay changes the frame")
    corner_with = lit(with_overlay[: 64 * 3 * 10])
    corner_without = lit(without[: 64 * 3 * 10])
    check(corner_with >= corner_without, "the overlay adds lit pixels to the top rows")
    settings(base, {"show": {"clock_overlay": {"enabled": True}}})

    # The Widget state, each widget in turn.
    frames = {}
    for w in ("clock", "temperature", "weather"):
        settings(base, {"show": {"main_state": "widget"}, "widgets": {"widget": w}})
        time.sleep(3)
        d = status(base)
        check(d["playback"]["state"] == "widget" and d["playback"].get("widget") == w, "widget state shows " + w)
        frames[w] = frame(base)
        check(lit(frames[w]) > 20, w + " draws something (%d lit pixels)" % lit(frames[w]))
    check(frames["clock"] != frames["temperature"] != frames["weather"], "the three widgets differ")

    # The weather with a location (Greenwich).
    settings(base, {"weather": {"latitude": 51.4779, "longitude": -0.0015, "units": "metric"}})
    deadline = time.time() + 60
    we = {}
    while time.time() < deadline:
        we = status(base).get("weather", {})
        if we.get("valid"):
            break
        time.sleep(3)
    check(we.get("valid"), "weather fetched: %s" % str(we)[:120])
    if we.get("valid"):
        check(-40 < we["temperature"] < 50 and len(we.get("days", [])) >= 2, "weather numbers plausible")

    # An interlude: 100 % clock at a 5 s auto-swap in the Animation show.
    settings(base, {"show": {"main_state": "animation_show", "auto_swap_seconds": 5}, "widgets": {"interlude_percent": {"clock": 100}}})
    deadline = time.time() + 20
    seen = False
    while time.time() < deadline and not seen:
        st, j = request(base, "GET", "/api/v1/history")
        seen = any(i["kind"] == "interlude" for i in j["data"]["items"])
        time.sleep(1)
    check(seen, "an interlude entered history at auto-swap")
    d = status(base)
    check(d["playback"]["state"] == "animation_show", "still in the animation show")

    # Back to what it was.
    settings(base, {"show": {"main_state": original["show"]["main_state"], "auto_swap_seconds": original["show"]["auto_swap_seconds"],
                             "clock_overlay": {"enabled": original["show"]["clock_overlay"]["enabled"]}},
                    "widgets": {"widget": original["widgets"]["widget"], "interlude_percent": original["widgets"]["interlude_percent"]}})
    from api_smoke import failures
    print("widgets smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
