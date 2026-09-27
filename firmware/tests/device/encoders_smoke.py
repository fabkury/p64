#!/usr/bin/env python3
r"""Device smoke test of the p64b rotary encoders (stage B, the probe): both seesaw
boards answer at their addresses with a known hardware id, the poll runs without read
errors, and, while you turn and press the knobs, the positions and counters move.

    python tests\device\encoders_smoke.py [http://p64.local] [--watch SECONDS] [--expect N]

--watch prints the live state once a second for that long (default 15 s; 0 skips it) so
a turn or a press can be seen landing; --expect is how many boards must answer (default
2; p64a or a bench with one board: 1). Exit code 1 on any failure.
"""

import sys
import time

from api_smoke import check, request


def encoders(base):
    st, j = request(base, "GET", "/api/v1/diag/encoders")
    assert st == 200, j
    return j["data"]


def line(d):
    parts = []
    for k in d["knobs"]:
        if not k["present"]:
            parts.append("%s@0x%02x: missing" % (k["name"], k["address"]))
            continue
        parts.append("%s@0x%02x: pos %d, detents %d, %s, presses %d/%d long, err %d, last %s" % (
            k["name"], k["address"], k["position"], k["detents"], "DOWN" if k["pressed"] else "up",
            k["presses"], k["long_presses"], k["read_errors"], k["last_event"] or "-"))
    return " | ".join(parts)


def main():
    args = sys.argv[1:]
    base = next((a for a in args if a.startswith("http")), "http://p64.local")
    watch = int(args[args.index("--watch") + 1]) if "--watch" in args else 15
    expect = int(args[args.index("--expect") + 1]) if "--expect" in args else 2

    d = encoders(base)
    check(d["enabled"], "encoders enabled in the firmware (SDA %s, SCL %s)" % (d.get("sda"), d.get("scl")))
    if not d["enabled"]:
        print("encoders smoke: CONFIG_P64_ENCODERS is off; stopping")
        return 1
    present = [k for k in d["knobs"] if k["present"]]
    check(len(present) >= expect, "%d of %d boards answer (expected %d): %s" % (
        len(present), len(d["knobs"]), expect, ", ".join("%s@0x%02x" % (k["name"], k["address"]) for k in present)))
    for k in d["knobs"]:
        if k["present"]:
            check(k["hw_id"] in (0x55, 0x84, 0x86, 0x87, 0x88, 0x89), "%s: seesaw hardware id 0x%02x (0x87 = ATtiny817)" % (k["name"], k["hw_id"]))
    check(d["polls"] > 50, "the poll task runs: %d polls" % d["polls"])
    st, j = request(base, "GET", "/api/v1/status")
    check(st == 200 and j["data"]["inputs"]["encoders_present"] == len(present), "status reports %d encoders" % len(present))

    if watch > 0:
        before = {k["name"]: k for k in d["knobs"]}
        print("     turn and press the knobs for %d s:" % watch)
        end = time.time() + watch
        while time.time() < end:
            d = encoders(base)
            print("     " + line(d))
            time.sleep(1)
        d = encoders(base)
        moved = [k["name"] for k in d["knobs"] if k["present"] and (
            k["detents"] != before[k["name"]]["detents"] or k["presses"] != before[k["name"]]["presses"])]
        print("     knobs that moved: %s" % (", ".join(moved) or "none"))
        for k in d["knobs"]:
            if k["present"]:
                check(k["read_errors"] == before[k["name"]]["read_errors"], "%s: no read errors while in use (%d)" % (k["name"], k["read_errors"]))
                check(k["lost"] == 0, "%s: never lost the board (%d)" % (k["name"], k["lost"]))
    else:
        for k in d["knobs"]:
            if k["present"]:
                check(k["read_errors"] == 0, "%s: no read errors (%d)" % (k["name"], k["read_errors"]))

    from api_smoke import failures
    print("encoders smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
