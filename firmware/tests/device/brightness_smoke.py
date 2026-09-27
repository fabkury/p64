#!/usr/bin/env python3
r"""Device smoke test of the brightness scale (spec 3.2, 2026-09-27): run against a p64.

    python tests\device\brightness_smoke.py [http://p64.local]

Sets a ladder of brightness values through the settings API and reads the panel's light
plan back from the status document: the share of full light follows the perceptual
curve (lightness even in the number, light its cube), the output-enable level never
drops below the driver floor (17 on this panel), the LUT scale is 1 at 255 and about a
sixteenth at brightness 1, and the light is strictly increasing up the ladder. Restores
the brightness it found. Skips (exit 0) while the night schedule is active, since the
effective brightness is then the schedule's. Exit code 1 on any failure.
"""

import json
import sys
import time
import urllib.error
import urllib.request

failures = 0
FLOOR_LIGHT = 127.0 / 1979.0 / 16.0  # brightness 1 as a share of full light
FLOOR_LEVEL = 17                      # the driver's floor on a 64-wide panel


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1
    return cond


def request(base, method, path, body=None, timeout=30):
    data = json.dumps(body).encode() if body is not None else None
    headers = {"Content-Type": "application/json"} if data is not None else {}
    req = urllib.request.Request(base + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        payload = e.read()
        try:
            return e.code, json.loads(payload)
        except ValueError:
            return e.code, payload


def expected_light(v):
    l1 = FLOOR_LIGHT ** (1.0 / 3.0)
    l = l1 + (v - 1) / 254.0 * (1.0 - l1)
    return l ** 3


def panel(base):
    st, j = request(base, "GET", "/api/v1/status")
    if st != 200:
        raise SystemExit("GET /api/v1/status: %s" % st)
    return j["data"]["panel"]


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    st, j = request(base, "GET", "/api/v1/settings")
    if st != 200:
        print("GET /api/v1/settings: %s" % st)
        return 1
    display = j["data"]["display"]
    original = display["brightness"]
    ceiling = display.get("brightness_ceiling", 255)
    p = panel(base)
    if p.get("night_active"):
        print("night schedule active: the effective brightness is the schedule's; skipping")
        return 0
    if ceiling < 255:
        print("brightness ceiling is %d: the ladder stops there" % ceiling)
    check("light" in p and "oe_level" in p and "lut_scale" in p, "status panel reports light, oe_level, lut_scale")

    ladder = [v for v in (1, 2, 8, 32, 64, 128, 192, 255) if v <= ceiling]
    last_light = -1.0
    try:
        for v in ladder:
            st, j = request(base, "PUT", "/api/v1/settings", {"display": {"brightness": v}})
            check(st == 200 and j["data"]["display"]["brightness"] == v, "PUT brightness %d" % v)
            time.sleep(0.3)
            p = panel(base)
            light, level, scale = p["light"], p["oe_level"], p["lut_scale"]
            want = expected_light(v)
            check(p["brightness"] == v, "panel brightness %d" % v)
            check(abs(light - want) <= max(1.0 / 1979, want * 0.02),
                  "brightness %3d: light %.5f of full (curve says %.5f)" % (v, light, want))
            check(FLOOR_LEVEL <= level <= 255, "brightness %3d: OE level %d at or above the floor" % (v, level))
            check(0 < scale <= 1.0, "brightness %3d: LUT scale %.4f in (0, 1]" % (v, scale))
            check(light > last_light, "brightness %3d: brighter than the step below" % v)
            last_light = light
            if v == 1:
                check(level == FLOOR_LEVEL and abs(scale - 1.0 / 16) < 0.01,
                      "brightness 1 is the floor level with a sixteenth of the LUT (level %d, scale %.4f)" % (level, scale))
            if v == 255:
                check(level == 255 and scale == 1.0, "brightness 255 is the full level with no LUT scale")
            if 32 <= v < 255:
                check(scale >= 0.74, "brightness %3d: above the floor the LUT scale stays near one (%.3f)" % (v, scale))
    finally:
        request(base, "PUT", "/api/v1/settings", {"display": {"brightness": original}})
        print("restored brightness %d" % original)

    print("\n%d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
