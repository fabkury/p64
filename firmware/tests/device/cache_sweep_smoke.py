#!/usr/bin/env python3
r"""Device smoke test of the cache sweep (spec 5.4) over the HTTP API.

    python tests\device\cache_sweep_smoke.py [http://p64.local] [--delete]

Checks the cache retention setting (round trip and clamping), the cache counters in the
Makapix status document, a dry-run sweep through POST /api/v1/diag/cache_sweep, and the
last-played touch: after the show reads a cached artwork, a dry run with a short age
finds at least one file young enough to keep. With --delete it also runs a real sweep
of everything not played in the last hour, checks that the affected channels' cached
counts drop and that the download loop brings them back up (the churn the policy
accepts: the device downloads the deleted artworks again). Needs a card and a synced
clock; the Promoted playset is activated and left playing. Exit code 1 on any failure.
"""

import sys
import time

from api_smoke import check, request


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


def sweep(base, older_than_s=None, dry_run=True):
    body = {"dry_run": dry_run}
    if older_than_s is not None:
        body["older_than_s"] = older_than_s
    return request(base, "POST", "/api/v1/diag/cache_sweep", body)


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    delete = "--delete" in sys.argv

    d = status(base)
    if not d.get("card", {}).get("mounted"):
        print("no card mounted: nothing to sweep")
        return 0
    check(d.get("time", {}).get("synced"), "clock synced")

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
    check(isinstance(mc, dict) and all(k in mc for k in ("files", "bytes", "last_sweep", "last_deleted", "last_freed_bytes")),
          "makapix.cache in the status document")

    # A dry run with the defaults: counts everything, deletes nothing.
    st, j = sweep(base)
    check(st == 200 and j.get("ok"), "POST diag/cache_sweep (defaults)")
    r = j["data"]
    check(r["dry_run"] is True, "dry_run defaults to true")
    check(r["older_than_s"] == original * 86400, f"older_than_s defaults to the retention ({r['older_than_s']} s)")
    check(r["examined"] >= 0 and r["deleted"] <= r["examined"], f"dry run: {r['examined']} files, {r['deleted']} older, {r['took_ms']} ms")
    mc = status(base)["makapix"]["cache"]
    check(mc["files"] == r["examined"], "status cache.files follows the dry run")

    # The touch: play a cached Makapix artwork, then a short age keeps at least that file.
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "Promoted"})
    check(st == 200, "play Promoted")
    wait_for(base, lambda d: (d["playback"].get("artwork") or {}).get("post_id") is not None, "a Makapix artwork playing", 120)
    request(base, "POST", "/api/v1/action/next")
    time.sleep(3)
    st, j = sweep(base, older_than_s=60)
    r = j["data"]
    check(st == 200 and r["examined"] - r["deleted"] >= 1, f"a file read for the show is younger than 60 s ({r['examined'] - r['deleted']} kept)")
    st, j = sweep(base, older_than_s=-1)
    check(st == 400, "older_than_s out of range is refused")

    if not delete:
        print("skipping the real sweep (pass --delete to exercise deletion and the re-download)")
        return 0

    before = cached_total(base)
    st, j = sweep(base, older_than_s=3600, dry_run=False)
    check(st == 200 and j.get("ok"), "real sweep of files not played in the last hour")
    r = j["data"]
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


if __name__ == "__main__":
    code = main()
    from api_smoke import failures
    print("cache sweep smoke: %d failures" % failures)
    sys.exit(1 if failures else code)
