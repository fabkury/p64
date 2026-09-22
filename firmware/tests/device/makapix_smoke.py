#!/usr/bin/env python3
r"""Device smoke test of the Makapix Club integration (M6) over the HTTP API.

    python tests\device\makapix_smoke.py [http://p64.local] [--paired]

Needs the device on a network with internet access. Without --paired it exercises what
works anonymously: the Makapix status document, the Promoted playset (listing and the
first downloads), play-this of a post by sqid and by site URL, play-this of a plain URL,
and the built-ins' availability. With --paired (after pairing through the web UI) it also
checks the MQTT connection flag, likes, the All playset and the Followed activation.
Exit code 1 on any failure.
"""

import sys
import time

from api_smoke import check, request


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def wait_for(base, predicate, what, timeout=60.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        d = status(base)
        if predicate(d):
            return d
        time.sleep(1.0)
    d = status(base)
    check(predicate(d), what + " (timed out; makapix=" + str(d.get("makapix"))[:200] + ")")
    return d


def channels(base):
    st, j = request(base, "GET", "/api/v1/channels")
    assert st == 200, j
    return j["data"]["channels"]


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    paired = "--paired" in sys.argv

    st, j = request(base, "GET", "/api/v1/makapix")
    check(st == 200 and j.get("ok"), "GET /api/v1/makapix")
    m = j["data"]
    check(m["state"] in ("unpaired", "pairing", "paired", "invalid"), "makapix state " + m["state"])
    check(m["host"] == "makapix.club", "host makapix.club")
    d = wait_for(base, lambda d: d["makapix"]["online"], "device online (Wi-Fi and clock)", 90)
    if paired:
        check(d["makapix"]["state"] == "paired", "device is paired")
        wait_for(base, lambda d: d["makapix"]["mqtt_connected"], "MQTT connected", 60)

    st, j = request(base, "GET", "/api/v1/playsets")
    b = {x["name"]: x for x in j["data"]["builtins"]}
    check(b["Promoted"]["enabled"], "Promoted built-in enabled")

    # The maximum artwork size (spec 5.3 and 16): four steps, other values snap up.
    st, j = request(base, "GET", "/api/v1/settings")
    max_size = j["data"]["makapix"]["max_size"]
    check(max_size in (32, 64, 128, 256), "makapix.max_size is one of the four steps (%s)" % max_size)
    st, j = request(base, "PUT", "/api/v1/settings", {"makapix": {"max_size": 100}})
    check(st == 200 and j["data"]["makapix"]["max_size"] == 128, "max_size 100 snaps up to 128")
    st, j = request(base, "PUT", "/api/v1/settings", {"makapix": {"max_size": 1000}})
    check(st == 200 and j["data"]["makapix"]["max_size"] == 256, "max_size 1000 snaps to 256")
    st, j = request(base, "PUT", "/api/v1/settings", {"makapix": {"max_size": max_size}})
    check(st == 200 and j["data"]["makapix"]["max_size"] == max_size, "max_size restored to %d" % max_size)
    check(b["All"]["enabled"] == paired, "All built-in enabled only when paired")
    check(b["Followed"]["enabled"] == paired, "Followed built-in enabled only when paired")

    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "Promoted"})
    check(st == 200, "activate Promoted")
    # The previous playset's artwork stays up until the first Promoted one is ready (the
    # seamless swap, spec 3.6), so wait for an artwork from the new channel (2026-09-22).
    d = wait_for(base, lambda d: d["playback"]["playset"]["name"] == "Promoted"
                 and (d["playback"].get("artwork") or {}).get("channel") == "Promoted",
                 "Promoted shows an artwork", 120)
    a = d["playback"].get("artwork", {})
    check(a.get("channel") == "Promoted" and a.get("post_id") is not None, "the artwork carries its post id")
    check(a.get("width", 0) <= max_size and a.get("height", 0) <= max_size,
          "the artwork fits the size limit (%sx%s within %d)" % (a.get("width"), a.get("height"), max_size))
    ch = channels(base)
    check(len(ch) == 1 and ch[0]["kind"] == "promoted", "one promoted channel")
    check(ch[0]["entries"] > 0 and ch[0]["cached"] > 0, "promoted listed %d, cached %d" % (ch[0]["entries"], ch[0]["cached"]))
    check(d["makapix"]["downloads"] > 0 or ch[0]["cached"] == ch[0]["entries"],
          "downloads counted: %d (cached %d of %d)" % (d["makapix"]["downloads"], ch[0]["cached"], ch[0]["entries"]))

    # Play-this of a post, by sqid and by site link (the same artwork).
    sqid = a.get("sqid") or "BVLa"
    st, j = request(base, "POST", "/api/v1/action/play", {"post": sqid})
    check(st == 200 and j["data"]["queued"], "play post " + sqid)
    d = wait_for(base, lambda d: d["playback"].get("artwork", {}).get("source") == "play_this", "the post plays as play-this", 60)
    check(d["playback"]["artwork"].get("post_id") is not None, "play-this artwork has a post id")
    st, j = request(base, "POST", "/api/v1/action/play", {"post": "https://makapix.club/p/" + sqid})
    check(st == 200, "play post by link")
    st, j = request(base, "POST", "/api/v1/action/play", {"post": "not a sqid!"})
    check(st == 422, "an unusable post reference is rejected (%d)" % st)
    st, j = request(base, "POST", "/api/v1/action/play", {"url": "ftp://nope"})
    check(st == 422, "an unusable URL is rejected (%d)" % st)

    # Play-this of an arbitrary URL: the vault file of the current post over HTTP.
    ch = channels(base)
    st, j = request(base, "GET", "/api/v1/history")
    items = [i for i in j["data"]["items"] if i.get("post_id") is not None and i["path"].startswith("cache/")]
    if items:
        name = items[-1]["path"].split("/")[-1]
        url = "http://vault.makapix.club/" + name  # shard-less URLs are not valid; expect a rejection or a miss
        st, j = request(base, "POST", "/api/v1/action/play", {"url": url})
        check(st in (200, 422), "play URL accepted or rejected cleanly (%d)" % st)

    if paired:
        pid = d["playback"]["artwork"]["post_id"]
        st, j = request(base, "POST", "/api/v1/makapix/like", {"post_id": pid, "like": True})
        check(st == 200 and j["data"]["liked"], "like post %d" % pid)
        st, j = request(base, "POST", "/api/v1/makapix/like", {"post_id": pid, "like": False})
        check(st == 200 and not j["data"]["liked"], "unlike post %d" % pid)
        st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "All"})
        check(st == 200, "activate All")
        deadline = time.time() + 180
        ch = channels(base)
        while time.time() < deadline and not (ch and ch[0]["kind"] == "all" and ch[0]["cached"] > 0):
            time.sleep(2)
            ch = channels(base)
        check(ch[0]["kind"] == "all" and ch[0]["entries"] > 0, "All listed %d entries" % ch[0]["entries"])
        d = wait_for(base, lambda d: d["playback"]["playset"]["name"] == "All" and d["playback"].get("artwork", {}).get("channel") == "All",
                     "All shows one of its artworks", 120)
        st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "Followed"})
        check(st == 200, "activate Followed")
        d = wait_for(base, lambda d: d["playback"]["playset"]["name"] == "Followed", "Followed activated", 60)
    else:
        st, j = request(base, "POST", "/api/v1/makapix/like", {"post_id": 1, "like": True})
        check(st == 409, "likes need pairing (%d)" % st)
        st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "Followed"})
        check(st == 404, "Followed needs pairing (%d)" % st)

    # Back to Local.
    st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": "Local"})
    check(st == 200, "activate Local again")
    from api_smoke import failures
    print("makapix smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
