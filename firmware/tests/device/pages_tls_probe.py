#!/usr/bin/env python3
r"""Finding F-32: does a TLS operation still work with four pages open?

Open pages cost internal RAM (measured 2026-11-12: the largest free block drops from
32.8 KB to as little as 14 KB with four), and a TLS session needs 12 to 13 KB. This
probe opens N pages in a real browser (Playwright), then makes the device open TLS
sessions while they are open: an update check (GitHub), a play-this of a Makapix post
(an API lookup and a file download), a like when the device is paired. It samples the
internal heap throughout and reports the lows and what failed.

    python tests\device\pages_tls_probe.py http://<ip> [--pages 4] [--rounds 5] [--reload]

`--reload` loads one of the open pages again every 3 s while the TLS work runs: a page
load is six connections at once, the costliest thing a browser does to the device.

Without `--reload` it is a measurement, with one failure: a TLS operation that fails
with the pages open and works without them (exit code 1). With `--reload` it is the test
of finding F-32 (the suite runs it so): the largest free block must stay at or above
`pages_tls_largest_block_min` of budgets.json while the pages load during the TLS work,
be back at its floor (or where it was) 180 s after they close, and no page load may fail.
What the pages took, before the fix of 2026-11-25, were the Wi-Fi driver's transmit
buffers: `tools\heap_trace_report.py` is how that was found.
"""

import sys
import threading
import time

from playwright.sync_api import sync_playwright

import harness
from api_smoke import request


def heap(base):
    d = harness.status(base, timeout=15)
    return d["heap"]["internal_free"], d["heap"]["internal_largest"]


class Sampler:
    def __init__(self, base):
        self.base, self.low_free, self.low_block, self.n, self.stop = base, 10 ** 9, 10 ** 9, 0, False
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        while not self.stop:
            try:
                f, b = heap(self.base)
                self.low_free, self.low_block, self.n = min(self.low_free, f), min(self.low_block, b), self.n + 1
            except Exception:  # noqa: BLE001
                pass
            time.sleep(0.4)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *a):
        self.stop = True
        self.thread.join(timeout=5)


def tls_round(base, post, tick=lambda: None):
    """One round of TLS work. Returns {name: (ok, detail)}. `tick` is called about once a
    second while waiting (the pages' reloads)."""
    out = {}
    st, j = request(base, "POST", "/api/v1/update/check", timeout=60)
    for _ in range(6):
        time.sleep(1)
        tick()
    st2, u = request(base, "GET", "/api/v1/update")
    u = u.get("data", {}) if isinstance(u, dict) else {}
    out["update check"] = (st in (200, 202) and not u.get("error"), "%s state %s error %r" % (st, u.get("state"), u.get("error")))
    d0 = harness.status(base)
    swaps = d0["playback"]["swaps"]
    fails = d0["makapix"]["download_failures"]
    st, j = request(base, "POST", "/api/v1/action/play", {"post": post}, timeout=30)
    ok = False
    deadline = time.time() + 25
    d = d0
    while time.time() < deadline and not ok:
        time.sleep(1)
        tick()
        d = harness.status(base)
        art = d["playback"].get("artwork") or {}
        ok = art.get("source") in ("play_this_makapix", "play-this", "play_this") or d["playback"]["swaps"] > swaps and art.get("sqid") == post
    out["play-this of a post"] = (ok, "%s, activity %r, last error %r, download failures +%d" % (
        st, d["makapix"].get("activity"), d["makapix"].get("last_error"), d["makapix"]["download_failures"] - fails))
    return out


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    pages = int(sys.argv[sys.argv.index("--pages") + 1]) if "--pages" in sys.argv else 4
    rounds = int(sys.argv[sys.argv.index("--rounds") + 1]) if "--rounds" in sys.argv else 5
    reload = "--reload" in sys.argv
    d = harness.status(base)
    # A post to play: the sqid of something the device has shown.
    st, h = request(base, "GET", "/api/v1/history")
    post = next((i.get("sqid") for i in reversed(h["data"]["items"]) if i.get("sqid")), None)
    if not post:
        cur = d["playback"].get("artwork") or {}
        post = cur.get("sqid")
    if not post:
        print("SKIP no Makapix artwork in the history to play as a post")
        return 2
    idle = heap(base)
    print("device %s, firmware %s; post %s; idle heap %s" % (base, d["firmware"]["version"], post, idle))
    failures = 0
    results = {}
    for label, n in (("no page", 0), ("%d pages" % pages, pages)):
        with sync_playwright() as pw:
            browser = pw.chromium.launch()
            ctx = browser.new_context()
            opened = []
            for i in range(n):
                p = ctx.new_page()
                p.goto(base + ("/", "/settings", "/playsets", "/")[i % 4], wait_until="domcontentloaded")
                opened.append(p)
            time.sleep(5 if n else 1)
            turn = {"at": time.time(), "i": 0, "loads": 0, "failed": 0}

            def tick():
                if not (reload and opened) or time.time() - turn["at"] < 3:
                    return
                turn["at"] = time.time()
                turn["i"] += 1
                try:
                    opened[turn["i"] % len(opened)].reload(wait_until="domcontentloaded", timeout=15000)
                    turn["loads"] += 1
                except Exception:  # noqa: BLE001
                    turn["failed"] += 1

            with Sampler(base) as s:
                tally = {}
                for r in range(rounds):
                    for name, (ok, detail) in tls_round(base, post, tick).items():
                        t = tally.setdefault(name, [0, 0, ""])
                        t[0 if ok else 1] += 1
                        if not ok:
                            t[2] = detail
                    time.sleep(2)
            browser.close()
        results[label] = tally
        print("%-8s lowest free %6d, lowest largest block %6d (%d samples)" % (label, s.low_free, s.low_block, s.n))
        if reload and n:
            print("         %d page loads meanwhile, %d failed" % (turn["loads"], turn["failed"]))
        for name, (ok, bad, detail) in tally.items():
            print("         %-20s %d of %d worked%s" % (name, ok, ok + bad, (" - " + detail) if bad else ""))
        time.sleep(5)
    for name in results["no page"]:
        base_ok = results["no page"][name][0]
        with_ok = results["%d pages" % pages][name][0]
        if with_ok < base_ok:
            print("FAIL %s: %d of %d with %d pages open, %d of %d without" % (name, with_ok, rounds, pages, base_ok, rounds))
            failures += 1
    # The recovery: free memory comes back at once, the largest block may not (fragments).
    t0 = time.time()
    last = idle
    for at in (0, 30, 60, 120, 180):
        time.sleep(max(0, at - (time.time() - t0)))
        last = heap(base)
        print("%3d s after the pages closed: free %6d, largest block %6d" % ((at,) + last))
    if reload:
        # With --reload this is a test (finding F-32, decision D47): the pages may not take
        # the room a TLS session needs, and what they took must come back.
        b = harness.budgets()
        low = b["pages_tls_largest_block_min"]
        if s.low_block < low:
            print("FAIL the largest block fell to %d with %d pages loading during TLS work (budget %d)" % (s.low_block, pages, low))
            failures += 1
        want = min(b["internal_largest_block_min"], idle[1])
        if last[1] < want:
            print("FAIL 180 s after the pages closed the largest block is %d (%d wanted: the floor, or what it was before)" % (last[1], want))
            failures += 1
        if turn["failed"]:
            print("FAIL %d page loads failed" % turn["failed"])
            failures += 1
    d = harness.status(base)
    print("after: heap %s, uptime %d s, reset reason %s" % (heap(base), d["uptime_s"], d.get("reliability", {}).get("reset_reason")))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
