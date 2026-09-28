#!/usr/bin/env python3
"""The TLS slot under contention (ADR 0009, architecture.md "Makapix fetcher", 2026-09-28).

The guard for the ten-minute hold seen on 2026-09-28: a Makapix walk paused because its
channel left the playset kept its keep-alive listing session, and with it the one TLS
slot, until a reboot. This test activates the All playset (a walk of up to 41 pages
starts), waits for the walk to be in progress, restores the playset that was active, and
checks that the slot is free (or held by someone else) within a few seconds and that the
counters in `network.tls_slot` are sane. It ends with the playset it found.

usage: python tests/device/tls_slot_smoke.py http://<ip>
"""
import sys
import time

import api_smoke
from api_smoke import check, request
from makapix_smoke import channels, playset_restored, status, wait_for


def slot(base):
    return status(base)["network"]["tls_slot"]


def run(base):
    d = wait_for(base, lambda d: d["makapix"]["online"], "device online (Wi-Fi and clock)", 90)
    s = slot(base)
    check(all(k in s for k in ("held", "holder", "depth", "waiters", "grants", "waits", "handoffs", "max_wait_ms")),
          "network.tls_slot carries the slot's fields (%s)" % s)
    check(s["depth"] <= 2 and s["waiters"] <= 3, "the slot is not piled up at rest (depth %d, waiters %d)" % (s["depth"], s["waiters"]))
    if d["makapix"]["state"] != "paired":
        check(True, "not paired: the paused-walk case needs the All channel; skipped")
        return

    st, j = request(base, "GET", "/api/v1/channels")
    before = j["data"]["playset"]
    check(bool(before), "the active playset is known (%s)" % before)

    # A walk of All starts: up to 41 pages on one keep-alive session.
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "All"})
    check(st == 200, "activate All")
    deadline = time.time() + 60
    walking = False
    while time.time() < deadline and not walking:
        ch = [c for c in channels(base) if c.get("provider") == "makapix" and c.get("kind") == "all"]
        walking = bool(ch) and ch[0].get("refreshing")
        time.sleep(1)
    check(walking, "the All channel is walking (refreshing)")
    held_by = slot(base)
    check(held_by["held"] and held_by["holder"] == "makapix", "the fetcher holds the slot during the walk (%s)" % held_by)

    # The channel leaves the playset mid-walk: the paused walk must give the slot back.
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": before})
    check(st == 200, "restored the playset %s" % before)
    freed = False
    t0 = time.time()
    while time.time() - t0 < 15 and not freed:
        s = slot(base)
        freed = not s["held"] or s["holder"] != "makapix" or s["depth"] < held_by["depth"]
        time.sleep(0.5)
    check(freed, "the paused walk released the slot within %.1f s (%s)" % (time.time() - t0, s))
    # Held again later only for the fetcher's own short work (a view, a download), never
    # for the paused walk: over 20 s the slot must be seen free at least once.
    seen_free = False
    for _ in range(40):
        if not slot(base)["held"]:
            seen_free = True
            break
        time.sleep(0.5)
    check(seen_free, "the slot is seen free within 20 s of the restore")
    s = slot(base)
    check(s["grants"] > 0, "grants counted (%d)" % s["grants"])
    check(s["max_wait_ms"] < 5 * 60 * 1000, "no wait longer than five minutes since boot (%d ms)" % s["max_wait_ms"])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    base = sys.argv[1].rstrip("/")
    with playset_restored(base):
        run(base)
    print("tls slot smoke: %d failures" % api_smoke.failures)
    sys.exit(1 if api_smoke.failures else 0)
