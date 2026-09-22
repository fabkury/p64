#!/usr/bin/env python3
r"""Device smoke test of the panel modes (spec 3.1): switches Quality <-> Photo many times
and checks after every switch that the panel is still streaming.

    python tests\device\panel_mode_smoke.py [http://p64.local] [--switches N] [--gap S]

Checks per switch: the mode took, the refresh rate and plane count match the mode
(Quality 271 Hz / 10 planes, Photo 814 Hz / 8 planes), the DMA descriptor pointer moves,
no stall was reported, the frame lock held (dma_sync) and no wait timed out. Checks at
the end that the internal heap's largest free block did not shrink by more than the
noise of the running system (the switch must not allocate: the 2026-09-20 bug was a
re-allocation of the descriptor chains that failed after hours of uptime and left the
panel dark). Restores the mode the device started in. Exit code 1 on any failure.
"""

import sys
import time

from api_smoke import check, request

EXPECT = {
    "quality": {"refresh_hz": 271.3, "bit_depth": 10, "transition_bit": 4},
    "photo": {"refresh_hz": 813.8, "bit_depth": 8, "transition_bit": 4},
}


def panel(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]["panel"]


def memory(base):
    st, j = request(base, "GET", "/api/v1/diag/memory")
    assert st == 200, j
    return j["data"]["heap"]["internal"]


def set_mode(base, mode):
    st, j = request(base, "PUT", "/api/v1/settings", {"display": {"panel_mode": mode}})
    assert st == 200, j


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    switches = int(sys.argv[sys.argv.index("--switches") + 1]) if "--switches" in sys.argv else 12
    gap = float(sys.argv[sys.argv.index("--gap") + 1]) if "--gap" in sys.argv else 1.5

    # The heap settles once the network sessions are up; right after a boot the MQTT
    # handshake alone moves the largest block by 16 KB (seen 2026-09-22), so wait for it.
    deadline = time.time() + 90
    while time.time() < deadline:
        st, j = request(base, "GET", "/api/v1/status")
        mk = j.get("data", {}).get("makapix", {}) if st == 200 else {}
        if mk.get("state") != "paired" or mk.get("mqtt_connected"):
            break
        time.sleep(2)
    time.sleep(5)
    before = panel(base)
    original = before["mode"]
    mem_before = memory(base)
    timeouts_before = before["timeouts"]
    print("start: mode %s, %.1f Hz, %d planes, internal largest block %d B (free %d B)" %
          (original, before["refresh_hz"], before["bit_depth"], mem_before["largest_free"], mem_before["free"]))
    check(not before["stalled"] and before["dma_moving"], "panel streaming before the test")

    mode = original
    for i in range(switches):
        mode = "photo" if mode == "quality" else "quality"
        set_mode(base, mode)
        time.sleep(gap)
        p = panel(base)
        e = EXPECT[mode]
        ok = (p["mode"] == mode and abs(p["refresh_hz"] - e["refresh_hz"]) < 1.0 and p["bit_depth"] == e["bit_depth"]
              and p["transition_bit"] == e["transition_bit"])
        check(ok, "switch %d -> %s: %.1f Hz, %d planes, transition bit %d" %
              (i + 1, mode, p["refresh_hz"], p["bit_depth"], p["transition_bit"]))
        check(p["dma_moving"] and not p["stalled"], "switch %d: DMA streaming, no stall" % (i + 1))
        check(p["dma_sync"], "switch %d: frame lock held" % (i + 1))
        check(p["timeouts"] == timeouts_before, "switch %d: no wait timeouts (%d)" % (i + 1, p["timeouts"]))

    # Quick succession: several requests inside one second must end in the last one.
    for m in ("photo", "quality", "photo", "quality", "photo"):
        set_mode(base, m)
        time.sleep(0.2)
    time.sleep(gap)
    p = panel(base)
    check(p["mode"] == "photo" and p["dma_moving"] and not p["stalled"],
          "five requests in one second end in the last one (photo), still streaming")

    set_mode(base, original)
    time.sleep(gap)
    p = panel(base)
    check(p["mode"] == original and p["dma_moving"], "restored to %s and streaming" % original)
    mem_after = memory(base)
    print("end: internal largest block %d B (free %d B)" % (mem_after["largest_free"], mem_after["free"]))
    check(mem_after["largest_free"] >= mem_before["largest_free"] - 4096,
          "largest free internal block not eaten by the switches (%d -> %d B)" %
          (mem_before["largest_free"], mem_after["largest_free"]))
    from api_smoke import failures
    print("panel mode smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
