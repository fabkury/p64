#!/usr/bin/env python3
"""Playback cadence on the device (spec 4.3 and acceptance criterion 18.4).

Makes three small animations with Pillow, uploads them to animations/_timing, plays each
one and measures the presented frame rate from the renderer's own counter
(`playback.frames` in the status document) against the device's microsecond clock
(`uptime_us` in diag/memory), then deletes them and restores the auto-swap interval.

  GIF with 40 ms delays   -> 25 fps within 1 %, no late frame
  GIF with 10 ms delays   -> 10 fps (the browser rule turns 10 ms into 100 ms)
  APNG with 16 ms delays  -> 60 fps within 1 % (the presentation cap)

This is the device-side guard for the timing rules the host tests check in
tests/host/unit/playback.cpp (the renderer's schedule and the player's timeline).
Give it the device's IP rather than p64.local (see firmware/README.md).

usage: python tests/device/timing_smoke.py http://<ip> [--seconds N]
"""
import io
import sys
import time

from PIL import Image

from api_smoke import check, request

FOLDER = "animations/_timing"


def make(fmt, duration_ms, frames=24):
    ims = []
    for i in range(frames):
        im = Image.new("RGB", (64, 64), (0, 0, 0))
        px = im.load()
        for y in range(64):
            for x in range(64):
                px[x, y] = ((x * 4 + i * 10) % 256, (y * 4) % 256, (i * 37) % 256)
        ims.append(im)
    buf = io.BytesIO()
    if fmt == "gif":
        ims[0].save(buf, "GIF", save_all=True, append_images=ims[1:], duration=duration_ms, loop=0)
    else:
        ims[0].save(buf, "PNG", save_all=True, append_images=ims[1:], duration=duration_ms, loop=0)
    return buf.getvalue()


def counters(base):
    st, j = request(base, "GET", "/api/v1/status")
    p = j["data"]["playback"]
    st2, m = request(base, "GET", "/api/v1/diag/memory")
    return p["frames"], p["late"], m["data"]["uptime_us"], p["artwork"].get("path", "")


def measure(base, path, seconds):
    st, _ = request(base, "POST", "/api/v1/action/play", {"path": path})
    if not check(st == 200, "play %s" % path):
        return None
    time.sleep(3)  # the first frame, and the renderer's schedule settles
    f0, l0, t0, now_playing = counters(base)
    check(now_playing.endswith(path.split("/")[-1]), "%s is the one playing (%s)" % (path, now_playing))
    time.sleep(seconds)
    f1, l1, t1, _ = counters(base)
    fps = (f1 - f0) / ((t1 - t0) / 1e6)
    return fps, l1 - l0


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    seconds = float(sys.argv[sys.argv.index("--seconds") + 1]) if "--seconds" in sys.argv else 20.0
    original = request(base, "GET", "/api/v1/settings")[1]["data"]["show"]["auto_swap_seconds"]
    request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": 3600}})
    request(base, "POST", "/api/v1/files/mkdir?path=" + FOLDER)
    cases = [
        ("gif40.gif", make("gif", 40), 25.0, 0.01),
        ("gif10.gif", make("gif", 10), 10.0, 0.01),
        ("apng16.png", make("png", 16), 60.0, 0.01),
    ]
    try:
        for name, data, want, tol in cases:
            path = FOLDER + "/" + name
            st, _ = request(base, "POST", "/api/v1/files?path=" + path, data, "application/octet-stream")
            if not check(st == 200, "upload %s (%d bytes)" % (path, len(data))):
                continue
            r = measure(base, path, seconds)
            if not r:
                continue
            fps, late = r
            check(abs(fps - want) <= want * tol, "%s: %.2f fps (want %.1f within %.0f %%)" % (name, fps, want, tol * 100))
            check(late == 0, "%s: no late frame (%d)" % (name, late))
    finally:
        request(base, "POST", "/api/v1/action/next")
        for name, *_ in cases:
            request(base, "DELETE", "/api/v1/files?path=%s/%s" % (FOLDER, name))
        request(base, "DELETE", "/api/v1/files?path=" + FOLDER)
        request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": original}})
    from api_smoke import failures
    print("timing smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
