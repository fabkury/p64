#!/usr/bin/env python3
r"""Device smoke test of the operations layer (M9): reliability status, trusted time (NTP
only, ADR 0011), the night schedule and the brightness ceiling.

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
    check(tm.get("synced") and tm.get("source") == "ntp", "time trusted from NTP (source %s)" % tm.get("source"))
    check(0 <= tm.get("last_sync_s", -1) <= tm.get("interval_s", 0) + 300,
          "last NTP answer %s s ago, within the %s s interval" % (tm.get("last_sync_s"), tm.get("interval_s")))
    check(tm.get("interval_s") == 6 * 3600, "re-sync every 6 h (%s s)" % tm.get("interval_s"))
    check(tm.get("rejected") == 0, "no NTP answer refused (%s)" % tm.get("rejected"))
    hosts = [sv["host"] for sv in tm.get("servers", [])]
    check("time.google.com" in hosts and "time.cloudflare.com" in hosts, "fallback servers present: %s" % hosts)
    check(any(sv["answered"] for sv in tm.get("servers", [])), "a server answered the last poll: %s" % tm.get("servers"))
    # No way to set the time by hand any more (ADR 0011).
    st, _ = request(base, "POST", "/api/v1/action/set_time", {"utc": int(time.time())})
    check(st == 404, "set_time is gone (%s)" % st)
    # A new NTP server setting takes its slot and is asked at once; the time stays trusted.
    ntp = original["network"]["ntp_server"]
    settings(base, {"network": {"ntp_server": "time.google.com"}})
    time.sleep(2)
    tm = status(base)["time"]
    hosts = [sv["host"] for sv in tm.get("servers", [])]
    check(hosts.count("time.google.com") == 1 and tm.get("synced"), "a setting equal to a fallback is asked once: %s" % hosts)
    settings(base, {"network": {"ntp_server": ntp}})
    time.sleep(2)
    hosts = [sv["host"] for sv in status(base)["time"].get("servers", [])]
    check(ntp in hosts and "time.google.com" in hosts, "setting restored: %s" % hosts)

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
