#!/usr/bin/env python3
r"""Device smoke test of the IMU inputs (M9): live readings, the upright calibration and
the auto-rotation resolution. Taps need a hand on the shell, so they are only reported.

    python tests\device\imu_smoke.py [http://p64.local]

Puts the settings back. Exit code 1 on any failure.
"""

import math
import sys
import time

from api_smoke import check, request


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def imu(base):
    st, j = request(base, "GET", "/api/v1/diag/imu")
    assert st == 200, j
    return j["data"]


def settings(base, patch):
    st, j = request(base, "PUT", "/api/v1/settings", patch)
    assert st == 200, j
    return j["data"]


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    d = imu(base)
    check(d["present"], "IMU present")
    if not d["present"]:
        print("imu smoke: no IMU; stopping")
        return 1
    g = math.sqrt(d["ax"] ** 2 + d["ay"] ** 2 + d["az"] ** 2)
    check(0.8 < g < 1.2, "gravity magnitude plausible: %.2f g (%.2f, %.2f, %.2f)" % (g, d["ax"], d["ay"], d["az"]))
    check(d["samples"] > 100 and d["read_errors"] == 0, "sampling runs: %d samples, %d errors" % (d["samples"], d["read_errors"]))
    print("     gravity angle %.1f deg, in-plane %.2f g, peak %.2f g, taps %d/%d, last %s" % (
        d["gravity_angle_deg"], d["in_plane_g"], d["peak_g"], d["single_taps"], d["double_taps"], d["last_event"] or "-"))

    # Calibrate "upright now" at the current display rotation; auto mode must then resolve
    # to that rotation and keep it (the panel is not moving during the test).
    rotation = status(base)["panel"]["rotation"]
    st, j = request(base, "POST", "/api/v1/action/calibrate_upright", {"rotation": rotation})
    check(st == 200 and j["data"]["calibrated"], "calibrated upright at rotation %d" % rotation)
    settings(base, {"display": {"rotation_auto": True}})
    time.sleep(2)
    d = imu(base)
    s = status(base)
    if d["in_plane_g"] >= 0.55:
        check(d["resolved"] and d["auto_rotation"] == rotation, "auto-rotation resolved to %d (in-plane %.2f g)" % (d["auto_rotation"], d["in_plane_g"]))
        check(s["panel"]["rotation"] == rotation, "display rotation unchanged by auto mode (%d)" % s["panel"]["rotation"])
    else:
        print("     panel lies flat (in-plane %.2f g): auto-rotation holds; resolution not checked" % d["in_plane_g"])
    check(s["inputs"]["imu_present"] and s["inputs"]["calibrated"], "status reports the IMU and the calibration")
    st, j = request(base, "POST", "/api/v1/action/calibrate_upright", {"rotation": 45})
    check(st == 400, "calibration rejects a rotation that is not a right angle")

    # Tap gestures can be switched off and the sensitivity changes the threshold.
    settings(base, {"inputs": {"tap_enabled": False, "tap_sensitivity": 9}})
    time.sleep(0.5)
    d = imu(base)
    check(not d["tap_enabled"] and abs(d["tap_threshold_g"] - 0.5) < 0.01, "tap settings applied: off, threshold %.2f g" % d["tap_threshold_g"])

    settings(base, {"display": {"rotation_auto": original["display"]["rotation_auto"]},
                    "inputs": {"tap_enabled": original["inputs"]["tap_enabled"], "tap_sensitivity": original["inputs"]["tap_sensitivity"]}})
    from api_smoke import failures
    print("imu smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
