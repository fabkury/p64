#!/usr/bin/env python3
r"""A cardless soak (docs/testing-strategy/no-card-mode.md, NC-D14): ejects the card
virtually, reboots, runs soak.py for the given time on the cardless device while
recording the memory window's figures every minute, then gives the card back.

    python tests\device\cardless_soak.py http://<ip> [--minutes 60] [--interval 30] [--playset Promoted]

It answers what the first cardless soak was meant to (decision D16): how many downloads
an hour the window really makes at the default interval, how many different artworks a
viewer sees, and whether the heap holds. A line per minute goes to the console and the
whole record to tests/device/reports/cardless-soak-<time>.csv (ignored by git). Needs
the test build and a card to give back. Exit code as soak.py's.
"""

import os
import subprocess
import sys
import threading
import time

import harness
from api_smoke import request, skipped
from hooks_smoke import hook, reboot, wait_status

HERE = os.path.dirname(os.path.abspath(__file__))


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    minutes, interval, playset = int(arg("--minutes", "60")), arg("--interval", "30"), arg("--playset", "Promoted")
    d = harness.status(base)
    if not d.get("test_hooks"):
        return skipped("firmware %s has no test hooks" % d["firmware"]["version"])
    if not d["card"]["mounted"]:
        return skipped("no card mounted to begin with")
    os.makedirs(os.path.join(HERE, "reports"), exist_ok=True)
    log = os.path.join(HERE, "reports", "cardless-soak-%s.csv" % time.strftime("%Y%m%d-%H%M%S"))
    hook(base, card_eject="no card answers")
    code = 1
    stop = False
    rows = []

    def record():
        seen = set()
        with open(log, "w", encoding="utf-8") as f:
            f.write("minute,uptime_s,files,bytes,stored,evictions,refused,renewals,misses,downloads,swaps,distinct,internal_free,internal_largest,psram_free\n")
            t0 = time.time()
            last = 0
            while not stop:
                try:
                    s = harness.status(base, timeout=10)
                    a = s["playback"].get("artwork") or {}
                    if a.get("name"):
                        seen.add(a["name"])
                    minute = int((time.time() - t0) / 60)
                    if minute > last or not rows:
                        last = minute
                        w = s["storage"]["window"]
                        row = (minute, s["uptime_s"], w["files"], w["bytes"], w["stored"], w["evictions"], w["refused"], w["renewals"], w["misses"],
                               s["makapix"]["downloads"], s["playback"]["swaps"], len(seen), s["heap"]["internal_free"], s["heap"]["internal_largest"],
                               s["heap"]["psram_free"])
                        rows.append(row)
                        f.write(",".join(str(x) for x in row) + "\n")
                        f.flush()
                except Exception:  # noqa: BLE001
                    pass
                time.sleep(5)

    try:
        d = reboot(base)
        if d is None or d["storage"]["mode"] != "cardless":
            print("FAIL the device did not come back cardless")
            return 1
        wait_status(base, lambda s: s["network"]["connected"] and s["time"]["synced"], 40)
        t = threading.Thread(target=record, daemon=True)
        t.start()
        code = subprocess.call([sys.executable, os.path.join(HERE, "soak.py"), base, "--minutes", str(minutes), "--interval", interval, "--playset", playset])
    finally:
        stop = True
        time.sleep(6)
        try:
            hook(base, card_eject="")
            reboot(base)
        except Exception as e:  # noqa: BLE001
            print("could not give the card back: %r (POST /api/v1/test {\"card_eject\": \"\"} and reboot)" % e)
    if len(rows) >= 2:
        a, b = rows[min(3, len(rows) - 1)], rows[-1]   # from minute 3: after the first fill
        span = max(1, b[0] - a[0])
        print("cardless soak: %d min after the fill: %.1f downloads an hour, %.1f swaps an hour, %d different artworks seen in all; "
              "window %d files %d KB, refused %d, misses %d; internal heap low %d, largest block low %d; record in %s" % (
                  span, 60.0 * (b[4] - a[4]) / span, 60.0 * (b[10] - a[10]) / span, b[11], b[2], b[3] // 1024, b[6], b[8],
                  min(r[12] for r in rows), min(r[13] for r in rows), log))
    return code


if __name__ == "__main__":
    sys.exit(main())
