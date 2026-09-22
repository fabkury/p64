#!/usr/bin/env python3
r"""Device smoke test of the content model (M5) over the HTTP API.

    python tests\device\content_smoke.py [http://p64.local]

Needs a card with artwork files in animations/ (run api_smoke.py --corpus first).
Exercises: the playset list with its built-ins, creating, reading, rejecting, activating
and deleting a user playset, the channels view, next/previous/history navigation,
history_go, pause/resume, reset_timer, play-this from a file, and the Local built-in.
The playset active at the start is active again at the end.
Exit code 1 on any failure.
"""

import sys
import time

from api_smoke import check, playset_restored, request


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]["playback"]


def wait_for(base, predicate, what, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        p = status(base)
        if predicate(p):
            return p
        time.sleep(0.25)
    p = status(base)
    check(predicate(p), what + " (timed out; playback=" + str(p)[:200] + ")")
    return p


def run(base):
    st, j = request(base, "GET", "/api/v1/playsets")
    check(st == 200 and j.get("ok"), "GET /api/v1/playsets")
    d = j["data"]
    names = [b["name"] for b in d.get("builtins", [])]
    check(names == ["Promoted", "All", "Followed", "Local"], "built-ins listed in order: " + str(names))
    check(d.get("max_playsets") == 32, "max_playsets 32")
    local = next(b for b in d["builtins"] if b["name"] == "Local")
    check(local["enabled"], "Local built-in enabled (card present)")

    st, j = request(base, "GET", "/api/v1/playsets/Local")
    check(st == 200 and j["data"]["builtin"] and len(j["data"]["channels"]) >= 1, "GET built-in Local playset")

    body = {"channels": [{"kind": "local", "identifier": "", "weight": 3}, {"kind": "promoted", "weight": 1}]}
    st, j = request(base, "PUT", "/api/v1/playsets/smoke_test", body)
    check(st == 200 and j["data"]["name"] == "smoke_test" and len(j["data"]["channels"]) == 2, "PUT playset smoke_test")
    st, j = request(base, "GET", "/api/v1/playsets/smoke_test")
    check(st == 200 and j["data"]["channels"][0]["kind"] == "local" and j["data"]["channels"][0]["weight"] == 3, "GET playset smoke_test")
    st, j = request(base, "GET", "/api/v1/playsets")
    check(any(p["name"] == "smoke_test" for p in j["data"]["playsets"]), "smoke_test listed")

    st, j = request(base, "PUT", "/api/v1/playsets/bad%20name", body)
    check(st == 400, "PUT with an invalid name is rejected (" + str(st) + ")")
    st, j = request(base, "PUT", "/api/v1/playsets/Local", body)
    check(st == 400, "PUT with a built-in name is rejected (" + str(st) + ")")
    st, j = request(base, "PUT", "/api/v1/playsets/bad_kind", {"channels": [{"kind": "giphy"}]})
    check(st == 400, "PUT with an unknown kind is rejected (" + str(st) + ")")
    st, j = request(base, "PUT", "/api/v1/playsets/no_channels", {"channels": []})
    check(st == 400, "PUT with no channels is rejected (" + str(st) + ")")

    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "smoke_test"})
    check(st == 200 and j["data"]["activating"], "POST action/play_playset smoke_test")
    # Right after a boot the "connected" screen holds the panel for 15 s (spec 15.1), and
    # the first artwork waits for it; hence the longer wait (2026-09-22).
    p = wait_for(base, lambda p: p["playset"]["name"] == "smoke_test" and p.get("artwork"), "smoke_test plays an artwork",
                 30)
    check(p.get("artwork", {}).get("channel") == "animations", "artwork came from the local channel")

    st, j = request(base, "GET", "/api/v1/channels")
    check(st == 200 and j["data"]["playset"] == "smoke_test" and len(j["data"]["channels"]) == 2, "GET /api/v1/channels")
    chs = j["data"]["channels"]
    check(chs[0]["available"] > 0 and chs[0]["status"] == "", "local channel has files: " + str(chs[0]["available"]))
    # Promoted either has cached artworks (then it is available and takes its weight, 1 of
    # 4) or reports why not; which one depends on what the device has downloaded before.
    if chs[1]["available"] > 0:
        check(chs[1]["status"] == "", "promoted channel available with %d cached" % chs[1]["available"])
        check(abs(chs[0]["share"] - 0.75) < 0.01, "the local channel gets its 3:1 share (%.2f)" % chs[0]["share"])
    else:
        check(chs[1]["status"] != "", "promoted channel reports its status: " + chs[1]["status"])
        check(abs(chs[0]["share"] - 1.0) < 0.01, "the only available channel gets the whole share")

    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "nope_missing"})
    check(st == 404, "activating an unknown playset is 404 (" + str(st) + ")")

    # Navigation: three nexts, then two previous, then forward again. History holds 32
    # items; once full, a push drops the oldest and the position stays at 31, so every
    # position below is counted back from where the three nexts end (2026-09-22: the
    # test assumed a history with room and failed on a device that had run all day).
    seen = []
    for i in range(3):
        before = status(base)
        st, j = request(base, "POST", "/api/v1/action/next")
        check(st == 200, "POST action/next")
        old = (before.get("artwork") or {}).get("name"), before["history"]["count"], before["history"]["position"]
        p = wait_for(base, lambda p, old=old: p.get("artwork") and
                     ((p["artwork"]["name"], p["history"]["count"], p["history"]["position"]) != old),
                     "next %d put a new history item up" % (i + 1))
        seen.append(p["artwork"].get("path") if p.get("artwork") else None)
    top = status(base)["history"]["position"]
    st, j = request(base, "GET", "/api/v1/history")
    check(st == 200 and j["data"]["count"] >= 4 and j["data"]["position"] == top,
          "GET /api/v1/history count %d position %d" % (j["data"]["count"], j["data"]["position"]))
    items = j["data"]["items"]
    check(items[j["data"]["position"]]["current"], "the current history item is flagged")
    check([it["path"] for it in items[top - 2:top + 1]] == seen, "history ends with the three artworks shown")
    st, j = request(base, "POST", "/api/v1/action/previous")
    p = wait_for(base, lambda p: p["history"]["position"] == top - 1, "previous walks back to %d" % (top - 1))
    check(p.get("artwork", {}).get("path") == seen[1], "previous shows the earlier artwork again")
    st, j = request(base, "POST", "/api/v1/action/previous")
    p = wait_for(base, lambda p: p["history"]["position"] == top - 2, "previous walks back to %d" % (top - 2))
    check(p.get("artwork", {}).get("path") == seen[0], "previous shows the first of the three again")
    st, j = request(base, "POST", "/api/v1/action/next")
    p = wait_for(base, lambda p: p["history"]["position"] == top - 1, "next walks forward to %d" % (top - 1))
    check(p.get("artwork", {}).get("path") == seen[1], "next walks forward through history")
    st, j = request(base, "POST", "/api/v1/action/history_go", {"position": top})
    p = wait_for(base, lambda p: p["history"]["position"] == top, "history_go jumps to %d" % top)
    check(p.get("artwork", {}).get("path") == seen[2], "history_go shows that item")

    # Pause darkens, resume restores the same artwork.
    before = status(base)["artwork"]["name"]
    st, j = request(base, "POST", "/api/v1/action/pause")
    p = wait_for(base, lambda p: p["paused"], "paused")
    check(not p.get("artwork"), "no artwork reported while paused")
    st, r = request(base, "GET", "/api/v1/frame.raw", raw=True)
    check(st == 200 and len(r) == 64 * 64 * 3 and max(r) == 0, "panel is dark while paused")
    st, j = request(base, "POST", "/api/v1/action/resume")
    p = wait_for(base, lambda p: not p["paused"] and p.get("artwork"), "resumed")
    check(p["artwork"]["name"] == before, "resume restored the same artwork")

    # Reset timer.
    st, j = request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": 30}})
    time.sleep(2.5)
    st, j = request(base, "POST", "/api/v1/action/reset_timer")
    p = status(base)
    check(p["auto_swap"]["remaining_s"] >= 28, "reset_timer restarts the interval (%s s left)" % p["auto_swap"]["remaining_s"])

    # Play-this from a file enters history as a play-this item.
    st, j = request(base, "GET", "/api/v1/files?path=animations")
    files = [e["name"] for e in j["data"]["entries"] if not e["dir"]]
    if files:
        target = files[0]
        pos = status(base)["history"]["position"]
        st, j = request(base, "POST", "/api/v1/action/play", {"path": "animations/" + target})
        check(st == 200, "POST action/play " + target)
        want = min(pos + 1, 31)  # a full history keeps the position at 31
        p = wait_for(base, lambda p: p["history"]["position"] == want and (p.get("artwork") or {}).get("name") == target,
                     "play-this entered history")
        check(p.get("artwork", {}).get("source") == "play_this" and p["artwork"]["name"] == target, "play-this artwork is current")
    st, j = request(base, "POST", "/api/v1/action/play", {"path": "animations/does_not_exist.gif"})
    check(st == 422, "play-this of a missing file is rejected (" + str(st) + ")")

    # Clean up: delete the playset; then the Local built-in on its own.
    st, j = request(base, "DELETE", "/api/v1/playsets/smoke_test")
    check(st == 200, "DELETE playset smoke_test")
    st, j = request(base, "GET", "/api/v1/playsets/smoke_test")
    check(st == 404, "deleted playset is gone")
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "Local"})
    p = wait_for(base, lambda p: p["playset"]["name"] == "Local" and p.get("artwork"), "Local plays again")
    st, j = request(base, "GET", "/api/v1/channels")
    check(st == 200 and all(c["kind"] == "local" for c in j["data"]["channels"]), "Local built-in has only local channels (%d)" % len(j["data"]["channels"]))


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    with playset_restored(base):
        run(base)
    from api_smoke import failures
    print("content smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
