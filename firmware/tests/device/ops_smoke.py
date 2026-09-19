#!/usr/bin/env python3
r"""Device smoke test of the operations layer (M9): reliability status, time sources,
the night schedule and the brightness ceiling.

    python tests\device\ops_smoke.py [http://p64.local]

Does not run the factory reset (it would unpair the device and erase its Wi-Fi). Puts
the settings back. Exit code 1 on any failure.
"""

import sys
import time

from api_smoke import check, request


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def settings(base, patch):
    st, j = request(base, "PUT", "/api/v1/settings", patch)
    assert st == 200, j
    return j["data"]


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    d = status(base)
    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    r = d.get("reliability", {})
    check(r.get("reset_reason") in ("power", "software", "panic", "watchdog", "brownout", "usb", "deep_sleep", "other"),
          "reset reason reported: %s" % r.get("reset_reason"))
    counters = r.get("counters", {})
    check(sum(counters.values()) >= 1, "reboot counters persisted: %s" % counters)
    check("present" in r.get("crash", {}), "crash summary object present (crash present: %s)" % r.get("crash", {}).get("present"))
    check(r.get("image", {}).get("partition", "").startswith("ota_"), "running partition named: %s" % r.get("image", {}).get("partition"))
    check(r.get("image", {}).get("pending_verify") is False, "image confirmed (not pending)")
    tm = d.get("time", {})
    check(tm.get("synced") and tm.get("source") in ("ntp", "rtc", "manual"), "time synced from %s" % tm.get("source"))

    # Set time by hand: the clock follows, the source says manual, then NTP corrects it again later.
    now = int(time.time())
    st, j = request(base, "POST", "/api/v1/action/set_time", {"utc": now - 120})
    check(st == 200 and j["data"]["source"] == "manual", "set_time accepted (source manual)")
    st, j = request(base, "POST", "/api/v1/action/set_time", {"utc": 12345})
    check(st == 400, "set_time rejects an implausible time")
    request(base, "POST", "/api/v1/action/set_time", {"utc": now})

    # Night schedule: a window that covers now with brightness 40 -> effective 40 and night_active.
    lt = status(base)["time"]["local"]  # "YYYY-MM-DDTHH:MM:SS"
    hh, mm = int(lt[11:13]), int(lt[14:16])
    minutes = hh * 60 + mm
    start = (minutes - 60) % 1440
    end = (minutes + 60) % 1440
    settings(base, {"display": {"brightness": 255, "brightness_ceiling": 255,
                                "night": {"enabled": True, "start_minutes": start, "end_minutes": end, "brightness": 40}}})
    time.sleep(1)
    d = status(base)
    check(d["panel"]["night_active"] and d["panel"]["brightness"] == 40, "night window applies: brightness %d, night %s" % (d["panel"]["brightness"], d["panel"]["night_active"]))
    # A window that does not cover now: back to the user brightness, capped by the ceiling.
    start2 = (minutes + 120) % 1440
    end2 = (minutes + 180) % 1440
    settings(base, {"display": {"brightness_ceiling": 100, "night": {"start_minutes": start2, "end_minutes": end2}}})
    time.sleep(1)
    d = status(base)
    check(not d["panel"]["night_active"] and d["panel"]["brightness"] == 100, "outside the window the ceiling caps: brightness %d" % d["panel"]["brightness"])
    # Panel off inside the window (brightness 0).
    settings(base, {"display": {"night": {"start_minutes": start, "end_minutes": end, "brightness": 0}}})
    time.sleep(1)
    d = status(base)
    check(d["panel"]["night_active"] and d["panel"]["brightness"] == 0, "night 'panel off' blanks: brightness %d" % d["panel"]["brightness"])

    # Factory reset needs the confirmation word.
    st, j = request(base, "POST", "/api/v1/action/factory_reset", {"confirm": "no"})
    check(st == 400, "factory reset refused without the confirmation word")

    # Back to what it was.
    o = original["display"]
    settings(base, {"display": {"brightness": o["brightness"], "brightness_ceiling": o["brightness_ceiling"], "night": o["night"]}})
    from api_smoke import failures
    print("ops smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
