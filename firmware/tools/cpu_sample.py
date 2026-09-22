#!/usr/bin/env python3
"""Per-task CPU share and heap owners of the live device.

Reads GET /api/v1/diag/memory twice, `seconds` apart, and prints every task's share of
its core over the window (from the FreeRTOS run-time counters,
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS in sdkconfig.defaults), the stack headroom, the
heap figures, and, when the build has CONFIG_HEAP_TASK_TRACKING, which task allocated
how much of each heap. Written for the review of 2026-09-22 (docs/review-2026-09/).

usage: python tools/cpu_sample.py http://p64.local [seconds] [label]
"""
import json
import sys
import time
import urllib.request


def get(base):
    with urllib.request.urlopen(base + "/api/v1/diag/memory", timeout=10) as r:
        return json.load(r)["data"]


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else "http://p64.local"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    label = sys.argv[3] if len(sys.argv) > 3 else ""
    a = get(base)
    time.sleep(seconds)
    b = get(base)
    if "uptime_us" not in b or not b["tasks"] or "run_time" not in b["tasks"][0]:
        print("this build has no run-time statistics (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)")
        return 1
    dt_us = b["uptime_us"] - a["uptime_us"]
    before = {t["name"]: t for t in a["tasks"]}
    h = b["heap"]["internal"]
    print("== %s sample over %.1f s; internal free %d, largest %d, minimum since boot %d; psram free %d"
          % (label, dt_us / 1e6, h["free"], h["largest_free"], h["minimum_free"], b["heap"]["psram"]["free"]))
    print("%-12s %4s %4s %7s %10s" % ("task", "core", "prio", "cpu%", "stack_free"))
    rows = []
    for t in b["tasks"]:
        if t["name"] in before:
            rows.append(((t["run_time"] - before[t["name"]]["run_time"]) / dt_us * 100.0, t))
    busy = [0.0, 0.0]
    for pct, t in sorted(rows, key=lambda r: -r[0]):
        core = t["core"]
        if core in (0, 1) and not t["name"].startswith("IDLE"):
            busy[core] += pct
        print("%-12s %4s %4d %7.2f %10d" % (t["name"], core if core in (0, 1) else "any", t["priority"], pct, t["stack_free"]))
    print("busy: core 0 %.2f %%, core 1 %.2f %% (pinned tasks only; 'any' is unpinned)" % (busy[0], busy[1]))
    if "heap_by_task" in b:
        print()
        print("%-22s %9s %7s %11s %9s" % ("owner", "int_bytes", "int_blk", "psram_bytes", "psram_blk"))
        for o in sorted(b["heap_by_task"], key=lambda o: -o["internal_bytes"]):
            if o["internal_bytes"] == 0 and o["psram_bytes"] < 1024:
                continue
            print("%-22s %9d %7d %11d %9d" % (o["task"], o["internal_bytes"], o["internal_blocks"], o["psram_bytes"], o["psram_blocks"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
