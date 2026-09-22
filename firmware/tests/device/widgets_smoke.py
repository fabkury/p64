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

    # The clock overlay on an artwork: enabling and disabling it changes the frame. The
    # overlay only draws over an artwork, so wait for one (right after a boot the playset
    # may still be activating; the check failed that way on 2026-09-22).
    settings(base, {"show": {"main_state": "animation_show", "clock_overlay": {"enabled": True, "corner": "top_left"}}})
    t0 = time.time()
    while not status(base)["playback"].get("artwork", {}).get("name") and time.time() - t0 < 60:
        time.sleep(1)
    check(bool(status(base)["playback"].get("artwork", {}).get("name")), "an artwork is up for the overlay check")
    time.sleep(2)
    with_overlay = frame(base)
    settings(base, {"show": {"clock_overlay": {"enabled": False}}})
    time.sleep(2)
    without = frame(base)
    check(with_overlay != without, "the clock overlay changes the frame")
    # The corner, not a pixel count: on a fully lit artwork the outline removes lit pixels.
    check(with_overlay[: 64 * 3 * 8] != without[: 64 * 3 * 8], "the overlay changes the top rows")
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

    # An artwork request in the Widget state leaves it: the playset pill and Next both
    # switch the device to the Animation show, persist it, and the swap timer runs again
    # (the bug of 2026-09-21: an artwork frozen on the panel with the state still Widget).
    playset = d["playback"]["playset"]["name"]
    settings(base, {"show": {"main_state": "widget", "auto_swap_seconds": 5}})
    time.sleep(2)
    check(status(base)["playback"]["state"] == "widget", "back in the Widget state")
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": playset})
    check(st == 200, "play_playset accepted in the Widget state")
    deadline = time.time() + 15
    d = status(base)
    while time.time() < deadline and not (d["playback"]["state"] == "animation_show" and d["playback"].get("artwork")):
        time.sleep(1)
        d = status(base)
    check(d["playback"]["state"] == "animation_show" and bool(d["playback"].get("artwork")),
          "the playset pill switched to the Animation show with an artwork up")
    check(request(base, "GET", "/api/v1/settings")[1]["data"]["show"]["main_state"] == "animation_show",
          "the switch is persisted in the settings")
    check(d["playback"]["auto_swap"]["remaining_s"] >= 0, "the swap timer runs (%s s left)" % d["playback"]["auto_swap"]["remaining_s"])
    swaps = d["playback"]["swaps"]
    deadline = time.time() + 20
    while time.time() < deadline and status(base)["playback"]["swaps"] == swaps:
        time.sleep(1)
    check(status(base)["playback"]["swaps"] > swaps, "an auto-swap followed")
    settings(base, {"show": {"main_state": "widget"}})
    time.sleep(2)
    request(base, "POST", "/api/v1/action/next")
    deadline = time.time() + 15
    d = status(base)
    while time.time() < deadline and d["playback"]["state"] != "animation_show":
        time.sleep(1)
        d = status(base)
    check(d["playback"]["state"] == "animation_show", "Next in the Widget state switched to the Animation show")

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
