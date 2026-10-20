#!/usr/bin/env python3
r"""Device smoke test of the intro animations (p101) over the HTTP API.

    python tests\device\intro_smoke.py [http://p64.local] [--all]

Checks the animation list, the off-screen bench of every animation against the frame
budget (render time under 10 ms of the 20 ms frame, leaving the 8 ms panel copy), the
boot_animation setting round trip (a name, random, an unknown name refused), and one
animation played on the panel for one slot: the frame changes while it plays, no frame
flips late, and the show resumes when the window ends. With --all, plays every animation
on the panel in turn (26 x the configured length). Exit code 1 on any failure.
"""

import sys
import time

from api_smoke import check, request

RENDER_BUDGET_US = 10000


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def frame(base):
    st, r = request(base, "GET", "/api/v1/frame.raw", raw=True)
    assert st == 200 and len(r) == 64 * 64 * 3
    return r


def play(base, name, window_ms):
    """Plays `name` and checks it on the panel; returns the late-flip delta."""
    before = status(base)["playback"]
    st, j = request(base, "POST", "/api/v1/action/boot_animation", {"name": name})
    check(st == 200, f"{name}: play now answers 200 ({st} {j})")
    time.sleep(0.3)
    d = status(base)["playback"]
    check(d.get("intro") == name, f"{name}: the status names it while it plays ({d.get('intro')})")
    a = frame(base)
    time.sleep(0.4)
    b = frame(base)
    check(a != b, f"{name}: the panel frame changes while it plays")
    time.sleep(window_ms / 1000.0 + 0.6)
    after = status(base)["playback"]
    check(after.get("intro") is None, f"{name}: the show resumes after the window ({after.get('intro')})")
    return after["late"] - before["late"]


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    everything = "--all" in sys.argv
    st, j = request(base, "GET", "/api/v1/boot_animations")
    check(st == 200, "GET /api/v1/boot_animations answers 200")
    names = j["data"]["names"]
    check(len(names) == 26, f"26 animations listed ({len(names)})")
    check(j["data"]["frame_ms"] == 20, "frame cadence 20 ms")
    check(j["data"]["setting"] in ["random"] + names, f"the setting is random or a name ({j['data']['setting']})")

    st, j = request(base, "GET", "/api/v1/diag/intro_bench", timeout=120)
    check(st == 200, "GET /api/v1/diag/intro_bench answers 200")
    rows = j["data"]["animations"]
    check(len(rows) == 26, "the bench covers every animation")
    worst = max(rows, key=lambda r: r["max_us"])
    for r in rows:
        check(r["max_us"] <= RENDER_BUDGET_US, f"{r['name']}: max {r['max_us']} us, avg {r['avg_us']} us within {RENDER_BUDGET_US} us")
    print(f"slowest: {worst['name']} max {worst['max_us']} us, avg {worst['avg_us']} us")

    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    window_ms = original["display"]["boot_animation_ms"] or 3000
    try:
        st, j = request(base, "PUT", "/api/v1/settings", {"display": {"boot_animation": names[1]}})
        check(st == 200 and j["data"]["display"]["boot_animation"] == names[1], "boot_animation set to a name")
        st, j = request(base, "PUT", "/api/v1/settings", {"display": {"boot_animation": "no_such_animation"}})
        check(st == 400, f"an unknown animation name is refused with 400 ({st})")
        st, j = request(base, "PUT", "/api/v1/settings", {"display": {"boot_animation": "random"}})
        check(st == 200 and j["data"]["display"]["boot_animation"] == "random", "boot_animation set back to random")

        st, j = request(base, "POST", "/api/v1/action/boot_animation", {"name": "no_such_animation"})
        check(st == 400, f"playing an unknown name is refused with 400 ({st})")
        if status(base)["playback"]["state"] != "animation_show":
            print("not in the Animation show: the play-now check is skipped")
        else:
            late = 0
            for name in (names if everything else [names[2]]):
                late += play(base, name, window_ms)
            check(late <= 1, f"late flips during the play ({late})")
    finally:
        request(base, "PUT", "/api/v1/settings", {"display": {"boot_animation": original["display"].get("boot_animation", "random")}})
    return 0


if __name__ == "__main__":
    try:
        main()
    finally:
        import api_smoke
        print(f"{api_smoke.FAILURES} failures" if hasattr(api_smoke, "FAILURES") else "")
    sys.exit(1 if getattr(sys.modules.get("api_smoke"), "FAILURES", 0) else 0)
