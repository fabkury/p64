#!/usr/bin/env python3
r"""Device smoke test of the cache sweep (spec 5.4) over the HTTP API.

    python tests\device\cache_sweep_smoke.py [http://p64.local] [--delete]

Checks the cache retention setting (round trip and clamping), the cache counters in the
Makapix status document, a dry-run sweep through POST /api/v1/diag/cache_sweep (queued,
202 at once, the outcome in `makapix.cache.sweep`), that the web server keeps answering
while the sweep runs (every status request within 2 s: on 2026-09-29 a sweep of 8158
files ran on the HTTP task and held every request for 230 s), that the sweep's cost per
file examined stays under `sweep_ms_per_file_max` of budgets.json (the walk used to
stat() every file, a directory search each), a second request while one runs is 409, and
the last-played touch: after the show reads a cached artwork, a dry run with a short age
finds at least one file young enough to keep. With --delete it also runs a real sweep of
everything not played in the last hour, checks that the affected channels' cached counts
drop and that the download loop brings them back up (the churn the policy accepts: the
device downloads the deleted artworks again). Needs a card and a synced clock; the
Promoted playset is activated, and the playset active at the start is active again at the
end. Exit code 1 on any failure.
"""

import sys
import threading
import time
import urllib.request

from api_smoke import budgets, check, playset_restored, request


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def channels(base):
    st, j = request(base, "GET", "/api/v1/channels")
    assert st == 200, j
    return j["data"]["channels"]


def cached_total(base):
    return sum(c.get("cached", 0) for c in channels(base))


def wait_for(base, predicate, what, timeout=90.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        d = status(base)
        if predicate(d):
            return d
        time.sleep(1.0)
    d = status(base)
    check(predicate(d), what + " (timed out)")
    return d


class Prober(threading.Thread):
    """Requests the status document every 0.5 s and records the slowest answer."""

    def __init__(self, base):
        super().__init__(daemon=True)
        self.base, self.stop, self.slowest, self.failures, self.count = base, False, 0.0, 0, 0

    def run(self):
        while not self.stop:
            t0 = time.time()
            try:
                urllib.request.urlopen(self.base + "/api/v1/status", timeout=10).read()
                self.slowest = max(self.slowest, time.time() - t0)
            except Exception:
                self.failures += 1
            self.count += 1
            time.sleep(0.5)


def sweep(base, older_than_s=None, dry_run=True, timeout=300.0):
    """Queues a sweep and waits for its outcome; returns (status, reply, outcome or None,
    slowest status answer while it ran)."""
    body = {"dry_run": dry_run}
    if older_than_s is not None:
        body["older_than_s"] = older_than_s
    prober = Prober(base)
    prober.start()
    st, j = request(base, "POST", "/api/v1/diag/cache_sweep", body)
    outcome = None
    if st == 202:
        deadline = time.time() + timeout
        # The request marks the sweep queued before it answers, so the first poll sees it
        # queued, running, or already finished with its own result in `last`.
        while time.time() < deadline:
            sw = status(base)["makapix"]["cache"]["sweep"]
            if sw["queued"] or sw["running"]:
                time.sleep(0.5)
                continue
            last = sw.get("last")
            if last and last["result"]["older_than_s"] == j["data"]["older_than_s"] and last["result"]["dry_run"] == dry_run:
                outcome = last
                break
            time.sleep(0.5)
    prober.stop = True
    prober.join()
    return st, j, outcome, prober


def run(base, delete):
    d = status(base)
    if not d.get("card", {}).get("mounted"):
        print("no card mounted: nothing to sweep")
        return 0
    check(d.get("time", {}).get("synced"), "clock synced")
    b = budgets()

    # The setting: present, round trip, clamped to 1..365.
    st, j = request(base, "GET", "/api/v1/settings")
    original = j["data"]["makapix"].get("cache_retention_days")
    check(isinstance(original, int) and 1 <= original <= 365, f"cache_retention_days present ({original})")
    st, j = request(base, "PUT", "/api/v1/settings", {"makapix": {"cache_retention_days": 45}})
    check(st == 200 and j["data"]["makapix"]["cache_retention_days"] == 45, "PUT cache_retention_days 45")
    st, j = request(base, "PUT", "/api/v1/settings", {"makapix": {"cache_retention_days": 0}})
    check(j["data"]["makapix"]["cache_retention_days"] == 1, "0 clamps to 1")
    st, j = request(base, "PUT", "/api/v1/settings", {"makapix": {"cache_retention_days": 1000}})
    check(j["data"]["makapix"]["cache_retention_days"] == 365, "1000 clamps to 365")
    request(base, "PUT", "/api/v1/settings", {"makapix": {"cache_retention_days": original}})

    # The counters in the status document.
    mc = d.get("makapix", {}).get("cache")
    check(isinstance(mc, dict) and all(k in mc for k in ("files", "bytes", "last_sweep", "last_deleted", "last_freed_bytes", "sweep")),
          "makapix.cache in the status document")
    check(isinstance(mc.get("sweep"), dict) and "running" in mc["sweep"] and "queued" in mc["sweep"] and "last" in mc["sweep"],
          "makapix.cache.sweep {queued, running, last}")

    # A dry run with the defaults: 202 at once, counts everything, deletes nothing, and the
    # server keeps answering meanwhile.
    st, j, outcome, prober = sweep(base)
    check(st == 202 and j.get("ok") and j["data"]["queued"] is True, f"POST diag/cache_sweep (defaults) answers 202 at once ({st})")
    check(j["data"]["dry_run"] is True, "dry_run defaults to true")
    check(j["data"]["older_than_s"] == original * 86400, f"older_than_s defaults to the retention ({j['data']['older_than_s']} s)")
    check(outcome is not None and outcome["ok"], "the sweep ran and reported in makapix.cache.sweep.last")
    check(prober.failures == 0 and prober.slowest < 2.0,
          f"the web server answered throughout the sweep (slowest status {prober.slowest * 1000:.0f} ms, {prober.failures} failed of {prober.count})")
    if outcome:
        r = outcome["result"]
        check(r["dry_run"] is True and r["deleted"] <= r["examined"], f"dry run: {r['examined']} files, {r['deleted']} older, {r['took_ms']} ms")
        per_file = r["took_ms"] / r["examined"] if r["examined"] else 0.0
        check(per_file <= b["sweep_ms_per_file_max"], f"sweep cost {per_file:.2f} ms per file <= budget {b['sweep_ms_per_file_max']} ms")
        mc = status(base)["makapix"]["cache"]
        check(mc["files"] == r["examined"], "status cache.files follows the dry run")

    # The touch: play a cached Makapix artwork, then a short age keeps at least that file.
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "Promoted"})
    check(st == 200, "play Promoted")
    wait_for(base, lambda d: (d["playback"].get("artwork") or {}).get("post_id") is not None, "a Makapix artwork playing", 120)
    request(base, "POST", "/api/v1/action/next")
    time.sleep(3)
    st, j, outcome, _ = sweep(base, older_than_s=60)
    check(st == 202 and outcome is not None and outcome["ok"], "dry run with a 60 s age ran")
    if outcome:
        r = outcome["result"]
        check(r["examined"] - r["deleted"] >= 1, f"a file read for the show is younger than 60 s ({r['examined'] - r['deleted']} kept)")
    st, j = request(base, "POST", "/api/v1/diag/cache_sweep", {"older_than_s": -1})
    check(st == 400, "older_than_s out of range is refused")

    # Two at once: the second is 409 while the first is queued or running.
    st1, j1 = request(base, "POST", "/api/v1/diag/cache_sweep", {"dry_run": True})
    st2, j2 = request(base, "POST", "/api/v1/diag/cache_sweep", {"dry_run": True})
    check(st1 == 202, "a dry run is queued")
    check(st2 == 409, f"a second sweep while one is queued or running is 409 ({st2})")
    wait_for(base, lambda d: not d["makapix"]["cache"]["sweep"]["running"] and not d["makapix"]["cache"]["sweep"]["queued"],
             "the queued dry run finished", 300)

    if not delete:
        print("skipping the real sweep (pass --delete to exercise deletion and the re-download)")
        return 0

    before = cached_total(base)
    st, j, outcome, prober = sweep(base, older_than_s=3600, dry_run=False)
    check(st == 202 and outcome is not None and outcome["ok"], "real sweep of files not played in the last hour")
    if outcome:
        r = outcome["result"]
        check(r["dry_run"] is False, "real sweep reported")
        print(f"     deleted {r['deleted']} of {r['examined']} files, {r['freed_bytes']} bytes, {r['indexes_deleted']} indexes, {r['took_ms']} ms")
        mc = status(base)["makapix"]["cache"]
        check(mc["last_sweep"] > 0 and mc["last_deleted"] == r["deleted"], "status records the sweep")
        after = cached_total(base)
        check(after <= before, f"cached counts did not grow through the sweep ({before} -> {after})")
        if r["deleted"]:
            check(after < before, f"cached counts dropped ({before} -> {after})")
            low = after
            wait_for(base, lambda d: cached_total(base) > low, "the download loop brings deleted artworks back", 120)
    return 0


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    with playset_restored(base):
        return run(base, "--delete" in sys.argv)


if __name__ == "__main__":
    code = main()
    from api_smoke import failures
    print("cache sweep smoke: %d failures" % failures)
    sys.exit(1 if failures else code)
