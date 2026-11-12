#!/usr/bin/env python3
r"""Cardless operation on the test build (ADR 0018, docs/testing-strategy/no-card-mode.md):
the card is ejected virtually (hook `card_eject`), the device reboots without it, and
the tests watch what it does. The card stays in its slot and is back after the last
reboot. Needs the test build, a network and a card to give back.

    python tests\device\cardless_smoke.py [http://p64.local] [--only window]

window  NC-D1 and a short NC-D2: on Promoted the first artwork is up within the budget,
        the memory window fills once and never beyond its capacity, then takes at most
        one new file per swap and per minute; nothing the show asks for is missing;
        "previous" plays from memory; the heap floors hold.

Exit code 1 on any failure, 2 when the firmware has no test hooks.
"""

import sys
import time

import harness
from api_smoke import check, request, skipped
from hooks_smoke import hook, playing, reboot, wait_status

FIRST_ARTWORK_S = 30   # uptime; measured 7 to 11 s on 2026-11-12 (to become a budget)
FILL_S = 150           # uptime by which a window of 48 small files is full (51 s measured)


def window(d):
    return d["storage"]["window"]


def boot_cardless(base, reason="no card answers"):
    hook(base, card_eject=reason)
    d = reboot(base)
    if not check(d is not None, "the device came back from the reboot"):
        return None
    check(d["storage"]["mode"] == "cardless" and not d["card"]["mounted"], "it runs cardless (%s)" % d["card"]["error"])
    return d


def test_window(base):
    request(base, "POST", "/api/v1/action/play_playset", {"name": "Promoted"})
    time.sleep(1)
    if boot_cardless(base) is None:
        return
    d = wait_status(base, playing, FIRST_ARTWORK_S + 10)
    check(playing(d) and d["uptime_s"] <= FIRST_ARTWORK_S + 5, "window: the first artwork is up %s s into the boot (budget %d)" % (
        d["uptime_s"], FIRST_ARTWORK_S))
    art = d["playback"]["artwork"]
    check(art["path"].startswith("mem:"), "window: it plays from memory (%s)" % art["path"][:24])

    # The first fill: free, never beyond the capacity.
    b = harness.budgets()
    low_heap = 10 ** 9
    most_files = most_bytes = 0
    deadline = time.time() + FILL_S
    while time.time() < deadline:
        d = harness.status(base)
        w = window(d)
        most_files, most_bytes = max(most_files, w["files"]), max(most_bytes, w["bytes"])
        low_heap = min(low_heap, d["heap"]["internal_free"])
        if w["filled"]:
            break
        time.sleep(3)
    w = window(d)
    if not check(w["filled"], "window: filled within %d s (%d files, %d KB at uptime %d s)" % (FILL_S, w["files"], w["bytes"] // 1024, d["uptime_s"])):
        return
    check(most_files <= w["capacity_files"] and most_bytes <= w["capacity_bytes"],
          "window: never beyond its capacity (most %d files, %d KB)" % (most_files, most_bytes // 1024))
    check(w["evictions"] == 0 or w["stored"] - w["evictions"] == w["files"], "window: the count of files is stored minus evicted")

    # Steady state at a 5 s interval: one new file per swap, one a minute at most (F-11:
    # until 2026-11-12 the downloader never stopped, 85 downloads in 73 s).
    st, s = request(base, "GET", "/api/v1/settings")
    interval = s["data"]["show"]["auto_swap_seconds"]
    request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": 5}})
    try:
        time.sleep(2)
        d0 = harness.status(base)
        w0, t0 = window(d0), time.time()
        seen = set()
        while time.time() - t0 < 150:
            d = harness.status(base)
            w = window(d)
            most_files = max(most_files, w["files"])
            low_heap = min(low_heap, d["heap"]["internal_free"])
            if d["playback"].get("artwork"):
                seen.add(d["playback"]["artwork"]["name"])
            time.sleep(2)
        d1 = harness.status(base)
        w1 = window(d1)
        swaps = d1["playback"]["swaps"] - d0["playback"]["swaps"]
        new = w1["stored"] - w0["stored"]
        check(swaps >= 20, "window: the show swaps every 5 s (%d swaps in 150 s)" % swaps)
        check(1 <= new <= 4, "window: %d new files in 150 s of %d swaps (one a minute at most)" % (new, swaps))
        check(d1["makapix"]["downloads"] - d0["makapix"]["downloads"] == new, "window: every download was kept (%d downloads)" % (
            d1["makapix"]["downloads"] - d0["makapix"]["downloads"]))
        check(w1["evictions"] - w0["evictions"] == new, "window: each new file took the place of one (%d evictions)" % (w1["evictions"] - w0["evictions"]))
        check(w1["misses"] == 0, "window: nothing the show asked for was missing (%d misses)" % w1["misses"])
        check(d1["playback"]["load_failures"] == d0["playback"]["load_failures"], "window: no load failure")
        check(most_files <= w1["capacity_files"], "window: never more than %d files" % w1["capacity_files"])
        check(len(seen) >= 10, "window: %d different artworks seen in 150 s" % len(seen))
        check(low_heap >= b["soak_internal_free_min"], "window: internal heap never under the soak floor (low %d)" % low_heap)

        # "Previous" plays from memory: the last items are protected from eviction.
        now = d1["playback"]["artwork"]["name"]
        request(base, "POST", "/api/v1/action/previous")
        d = wait_status(base, lambda s: playing(s) and s["playback"]["artwork"]["name"] != now, 10)
        check(playing(d) and d["playback"]["artwork"]["name"] != now, "window: previous goes back to an artwork still in memory")
        check(window(harness.status(base))["misses"] == 0, "window: and found it there")
    finally:
        request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": interval}})
        time.sleep(1)


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    d = harness.status(base)
    if not d.get("test_hooks"):
        return skipped("firmware %s has no test hooks (build with -DP64_TEST=ON)" % d["firmware"]["version"])
    if not d["card"]["mounted"]:
        return skipped("no card mounted to begin with: these tests eject one virtually and give it back")
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    name = d["playback"]["playset"]["name"]
    print("device: firmware %s, playset %s" % (d["firmware"]["version"], name))
    import api_smoke
    try:
        for label, fn in [("window", test_window)]:
            if only and label not in only:
                continue
            print("--- %s" % label)
            fn(base)
    finally:
        try:
            hook(base, card_eject="")
        except Exception as e:  # noqa: BLE001
            print("     could not clear the eject: %r" % e)
        d = reboot(base)
        check(d is not None and d["card"]["mounted"], "the card is mounted again after the tests")
        request(base, "POST", "/api/v1/action/play_playset", {"name": name})
        d = wait_status(base, lambda s: playing(s) and s["playback"]["playset"]["name"] == name, 40)
        check(d["playback"]["playset"]["name"] == name, "the playset of before is back (%s)" % d["playback"]["playset"]["name"])
    print("cardless smoke: %d failures" % api_smoke.failures)
    return 1 if api_smoke.failures else 0


if __name__ == "__main__":
    sys.exit(main())
