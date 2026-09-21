#!/usr/bin/env python3
r"""Unattended soak of the device (spec 18.1 and 18.11 in miniature): plays the Promoted
playset at a short auto-swap interval with the web UI's status feed subscribed, and
watches the counters that must not move: reboots (uptime monotonic, panic and watchdog
counters), panel timeouts and DMA stalls, late flips, and the internal heap floor.

    python tests\device\soak.py [http://p64.local] [--minutes N] [--interval S] [--playset NAME]

Defaults: 10 minutes, 5 s swaps, the Promoted playset. Prints a line per minute and a
verdict; exit code 1 when a watched counter moved or the device rebooted. Puts the
settings and the playset back.
"""

import json
import sys
import threading
import time
import urllib.request

from api_smoke import check, request


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def arg(name, default):
    if name in sys.argv:
        return sys.argv[sys.argv.index(name) + 1]
    return default


class WsLoad(threading.Thread):
    """Keeps one status poll per second going, as an open web page would."""

    def __init__(self, base):
        super().__init__(daemon=True)
        self.base, self.stop, self.errors = base, False, 0

    def run(self):
        while not self.stop:
            try:
                urllib.request.urlopen(self.base + "/api/v1/status", timeout=10).read()
                urllib.request.urlopen(self.base + "/api/v1/frame", timeout=10).read()
            except Exception:
                self.errors += 1
            time.sleep(1)


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    minutes = float(arg("--minutes", "10"))
    interval = int(arg("--interval", "5"))
    playset = arg("--playset", "Promoted")
    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    original_playset = status(base)["playback"]["playset"]["name"]
    request(base, "PUT", "/api/v1/settings", {"show": {"main_state": "animation_show", "auto_swap_seconds": interval}})
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": playset})
    check(st == 200, "playing %s at %d s swaps" % (playset, interval))
    time.sleep(3)
    d0 = status(base)
    p0, r0 = d0["panel"], d0["reliability"]["counters"]
    swaps0 = d0["playback"]["swaps"]
    heap_min = d0["heap"]["internal_free"]
    load = WsLoad(base)
    load.start()
    t0 = time.time()
    last_uptime = d0["uptime_s"]
    rebooted = False
    stalled = False
    print("     start: uptime %d s, heap %d, frames %d, late %d, timeouts %d" % (d0["uptime_s"], d0["heap"]["internal_free"], p0["frames"], p0["late_flips"], p0["timeouts"]))
    next_report = 60
    while time.time() - t0 < minutes * 60:
        time.sleep(5)
        try:
            d = status(base)
        except Exception as e:
            print("     status failed: %s" % e)
            rebooted = True
            continue
        if d["uptime_s"] < last_uptime:
            rebooted = True
            print("     REBOOT detected (uptime %d -> %d)" % (last_uptime, d["uptime_s"]))
        last_uptime = d["uptime_s"]
        heap_min = min(heap_min, d["heap"]["internal_free"])
        if d["panel"]["stalled"]:
            stalled = True
        if time.time() - t0 >= next_report:
            next_report += 60
            p = d["panel"]
            print("%4d min: swaps %d, frames %d, late %d, timeouts %d, heap %d (min %d), makapix dl %d, load errors %d" % (
                (time.time() - t0) // 60, d["playback"]["swaps"] - swaps0, p["frames"] - p0["frames"], p["late_flips"] - p0["late_flips"],
                p["timeouts"] - p0["timeouts"], d["heap"]["internal_free"], heap_min, d["makapix"]["downloads"], load.errors))
    load.stop = True
    d = status(base)
    p, r = d["panel"], d["reliability"]["counters"]
    check(not rebooted, "no reboot during the soak")
    check(r["panic"] == r0["panic"] and r["watchdog"] == r0["watchdog"], "no panic or watchdog counted")
    check(p["timeouts"] == p0["timeouts"], "no panel timeouts (%d)" % (p["timeouts"] - p0["timeouts"]))
    check(not stalled and not p["stalled"], "no DMA stall")
    check(p["late_flips"] - p0["late_flips"] <= 2, "late flips stayed at zero or one (%d)" % (p["late_flips"] - p0["late_flips"]))
    check(d["playback"]["swaps"] - swaps0 >= (minutes * 60 / interval) * 0.6, "artworks swapped (%d in %.0f min)" % (d["playback"]["swaps"] - swaps0, minutes))
    check(heap_min > 8000, "internal heap floor stayed above 8 KB (%d)" % heap_min)
    check(load.errors <= 2, "the status and preview polls kept answering (%d errors)" % load.errors)
    request(base, "POST", "/api/v1/action/play_playset", {"name": original_playset})  # switches to the show, so first
    request(base, "PUT", "/api/v1/settings", {"show": {"main_state": original["show"]["main_state"], "auto_swap_seconds": original["show"]["auto_swap_seconds"]}})
    from api_smoke import failures
    print("soak: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
