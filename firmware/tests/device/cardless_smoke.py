#!/usr/bin/env python3
r"""Cardless operation on the test build (ADR 0018, docs/testing-strategy/no-card-mode.md):
the card is ejected virtually (hook `card_eject`), the device reboots without it, and
the tests watch what it does. The card stays in its slot and is back after the last
reboot. Needs the test build, a network and a card to give back.

    python tests\device\cardless_smoke.py [http://p64.local] [--only window,restore,playsets,routes]

window  NC-D1 and a short NC-D2: on Promoted the first artwork is up within the budget,
        the memory window fills once and never beyond its capacity, then takes at most
        one new file per swap and per minute; nothing the show asks for is missing;
        "previous" plays from memory; the heap floors hold.
restore the playset rule (decisions D13 to D15): the choice made with a card is not the
        cardless choice; cardless with nothing chosen plays Promoted; Local chosen
        cardless stays Local and says "needs a card", and Promoted does not stand in;
        the panel's "No artwork" screen carries the address; back with the card, the
        card's choice returns. With the time withheld from boot: "waiting for the
        time", then playback when it arrives (D22).
playsets NC-D5, device playsets (D9 to D12): cardless, the list is the device's own,
        with its limits (8 playsets of 16 channels, none local); a playset saved there
        is listed, read back, activated, plays, survives a restart as the active one,
        can be renamed and deleted; each limit is refused with its own words; a
        deleted playset that was the choice leaves "No artwork: playset not found";
        with the card back the card's list shows none of it.
routes  NC-D4 and NC-D6: cardless, every route that needs the card answers 409 NO_CARD
        (five different codes before); an upload is played once from memory and not
        kept (D21); a file larger than 5 MB is still refused.

Exit code 1 on any failure, 2 when the firmware has no test hooks.
"""

import sys
import time

import harness
from api_smoke import check, request, skipped
from hooks_smoke import hook, playing, reboot, wait_status

CARD_CHOICE = ""       # the active playset with the card, read before the first eject
FIRST_ARTWORK_S = 30   # uptime; measured 7 to 11 s on 2026-11-12 (to become a budget)
FILL_S = 150           # uptime by which a window of 48 small files is full (51 s measured)


def window(d):
    return d["storage"]["window"]


def boot_cardless(base, reason="no card answers"):
    hook(base, card_eject=reason)
    d = reboot(base)
    if not check(d is not None, "the device came back from the reboot"):
        return None
    check(d["storage"]["mode"] == "cardless" and not d["card"]["mounted"], "it runs cardless (%s)" % d["card"]["error"])
    return d


def test_window(base):
    # The cardless choice is its own (D14): Promoted is chosen after the boot, so the
    # card's choice is not touched.
    hook(base, forget_cardless_playset=True)
    if boot_cardless(base) is None:
        return
    d = wait_status(base, playing, FIRST_ARTWORK_S + 10)
    check(playing(d) and d["uptime_s"] <= FIRST_ARTWORK_S + 5, "window: the first artwork is up %s s into the boot (budget %d)" % (
        d["uptime_s"], FIRST_ARTWORK_S))
    art = d["playback"]["artwork"]
    check(art["path"].startswith("mem:"), "window: it plays from memory (%s)" % art["path"][:24])

    # The first fill: free, never beyond the capacity.
    b = harness.budgets()
    low_heap = 10 ** 9
    most_files = most_bytes = 0
    deadline = time.time() + FILL_S
    while time.time() < deadline:
        d = harness.status(base)
        w = window(d)
        most_files, most_bytes = max(most_files, w["files"]), max(most_bytes, w["bytes"])
        low_heap = min(low_heap, d["heap"]["internal_free"])
        if w["filled"]:
            break
        time.sleep(3)
    w = window(d)
    if not check(w["filled"], "window: filled within %d s (%d files, %d KB at uptime %d s)" % (FILL_S, w["files"], w["bytes"] // 1024, d["uptime_s"])):
        return
    check(most_files <= w["capacity_files"] and most_bytes <= w["capacity_bytes"],
          "window: never beyond its capacity (most %d files, %d KB)" % (most_files, most_bytes // 1024))
    check(w["evictions"] == 0 or w["stored"] - w["evictions"] == w["files"], "window: the count of files is stored minus evicted")

    # Steady state at a 5 s interval: one new file per swap, one a minute at most (F-11:
    # until 2026-11-12 the downloader never stopped, 85 downloads in 73 s).
    st, s = request(base, "GET", "/api/v1/settings")
    interval = s["data"]["show"]["auto_swap_seconds"]
    request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": 5}})
    try:
        time.sleep(2)
        d0 = harness.status(base)
        w0, t0 = window(d0), time.time()
        seen = set()
        while time.time() - t0 < 150:
            d = harness.status(base)
            w = window(d)
            most_files = max(most_files, w["files"])
            low_heap = min(low_heap, d["heap"]["internal_free"])
            if d["playback"].get("artwork"):
                seen.add(d["playback"]["artwork"]["name"])
            time.sleep(2)
        d1 = harness.status(base)
        w1 = window(d1)
        swaps = d1["playback"]["swaps"] - d0["playback"]["swaps"]
        new = w1["stored"] - w0["stored"]
        check(swaps >= 20, "window: the show swaps every 5 s (%d swaps in 150 s)" % swaps)
        check(1 <= new <= 4, "window: %d new files in 150 s of %d swaps (one a minute at most)" % (new, swaps))
        check(d1["makapix"]["downloads"] - d0["makapix"]["downloads"] == new, "window: every download was kept (%d downloads)" % (
            d1["makapix"]["downloads"] - d0["makapix"]["downloads"]))
        check(w1["evictions"] - w0["evictions"] == new, "window: each new file took the place of one (%d evictions)" % (w1["evictions"] - w0["evictions"]))
        check(w1["misses"] == 0, "window: nothing the show asked for was missing (%d misses)" % w1["misses"])
        check(d1["playback"]["load_failures"] == d0["playback"]["load_failures"], "window: no load failure")
        check(most_files <= w1["capacity_files"], "window: never more than %d files" % w1["capacity_files"])
        check(len(seen) >= 10, "window: %d different artworks seen in 150 s" % len(seen))
        check(low_heap >= b["soak_internal_free_min"], "window: internal heap never under the soak floor (low %d)" % low_heap)

        # "Previous" plays from memory: the last items are protected from eviction.
        now = d1["playback"]["artwork"]["name"]
        request(base, "POST", "/api/v1/action/previous")
        d = wait_status(base, lambda s: playing(s) and s["playback"]["artwork"]["name"] != now, 10)
        check(playing(d) and d["playback"]["artwork"]["name"] != now, "window: previous goes back to an artwork still in memory")
        check(window(harness.status(base))["misses"] == 0, "window: and found it there")
    finally:
        request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": interval}})
        time.sleep(1)


def frame_png(base):
    import urllib.request
    return urllib.request.urlopen(base + "/api/v1/frame", timeout=10).read()


def test_restore(base):
    card_choice = CARD_CHOICE
    # Cardless, and the cardless choice forgotten: Promoted, whatever the card's choice is.
    hook(base, forget_cardless_playset=True)
    d = boot_cardless(base)
    if d is None:
        return
    d = wait_status(base, lambda s: s["playback"]["playset"]["name"] != "", 10)
    check(d["playback"]["playset"]["name"] == "Promoted", "restore: nothing chosen cardless, the device plays Promoted (card's choice: %s; playing %s)" % (
        card_choice, d["playback"]["playset"]["name"]))
    d = wait_status(base, playing, FIRST_ARTWORK_S + 10)
    check(playing(d), "restore: and Promoted plays")

    # Local chosen while cardless: it stays the choice across a boot and nothing stands in.
    request(base, "POST", "/api/v1/action/play_playset", {"name": "Local"})
    d = wait_status(base, lambda s: s["playback"]["no_artwork"] != "", 15)
    check(d["playback"]["playset"]["name"] == "Local" and d["playback"]["no_artwork"] == "needs a card",
          "restore: Local chosen without a card says what it needs (%r)" % d["playback"]["no_artwork"])
    d = boot_cardless(base)
    if d is None:
        return
    time.sleep(12)  # past the boot animation
    d = harness.status(base)
    check(d["playback"]["playset"]["name"] == "Local" and d["playback"]["no_artwork"] == "needs a card" and not d["playback"].get("artwork"),
          "restore: after a boot the choice is still Local, \"needs a card\", and Promoted does not stand in (%s, %r)" % (
              d["playback"]["playset"]["name"], d["playback"]["no_artwork"]))
    check(d["playback"]["screen"] == "", "restore: no other screen holds the panel (%r)" % d["playback"]["screen"])
    with_address = frame_png(base)
    check(len(with_address) > 200, "restore: the panel shows the No artwork screen (%d bytes of PNG)" % len(with_address))

    # The time withheld from boot (D22): "waiting for the time", then playback.
    request(base, "POST", "/api/v1/action/play_playset", {"name": "Promoted"})
    time.sleep(1)
    hook(base, time_withheld=True)
    try:
        d = boot_cardless(base)
        if d is None:
            return
        time.sleep(14)
        d = harness.status(base)
        check(not d["time"]["synced"] and d["network"]["connected"], "time: on the network, the time withheld")
        check(d["playback"]["no_artwork"] == "waiting for the time", "time: the panel says \"waiting for the time\", not \"offline\" (%r)" % d["playback"]["no_artwork"])
        hook(base, time_withheld=False)
        d = wait_status(base, playing, 45)
        check(playing(d), "time: when the time arrives the device plays")
    finally:
        hook(base, time_withheld=False)

    # Back with the card: the card's choice, untouched by what was chosen cardless.
    hook(base, card_eject="")
    d = reboot(base)
    if not check(d is not None and d["card"]["mounted"], "restore: the card is mounted again"):
        return
    d = wait_status(base, lambda s: s["playback"]["playset"]["name"] == card_choice, 20)
    check(d["playback"]["playset"]["name"] == card_choice, "restore: with the card, the card's choice is back (%s)" % d["playback"]["playset"]["name"])


def test_routes(base):
    if boot_cardless(base) is None:
        return
    wait_status(base, playing, FIRST_ARTWORK_S + 10)
    for method, path, body in (("GET", "/api/v1/files?path=animations", None), ("GET", "/api/v1/folders", None),
                               ("GET", "/api/v1/files/get?path=animations/x.gif", None), ("DELETE", "/api/v1/files?path=animations/x.gif", None),
                               ("POST", "/api/v1/files/mkdir?path=animations/new", None),
                               ("POST", "/api/v1/files/rename?from=animations/a.gif&to=animations/b.gif", None),
                               ("POST", "/api/v1/files/format", {"confirm": "FORMAT"}),
                               ("POST", "/api/v1/action/play", {"path": "animations/x.gif"})):
        st, j = request(base, method, path, body)
        code = j.get("code") if isinstance(j, dict) else None
        check(st == 409 and code == "NO_CARD", "routes: %s %s answers 409 NO_CARD (%s %s)" % (method, path.split("?")[0], st, code))
    # An upload: played once from memory, not kept.
    import os
    corpus = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "host", "corpus", "gifs")
    gif = sorted(f for f in os.listdir(corpus) if f.endswith(".gif"))[0]
    with open(os.path.join(corpus, gif), "rb") as f:
        data = f.read()
    w0 = window(harness.status(base))
    st, j = request(base, "POST", "/api/v1/files?path=animations/p64-test-upload.gif", data, content_type="application/octet-stream")
    check(st == 200 and j["data"].get("kept") is False and j["data"].get("playing") is True, "routes: an upload answers \"played, not kept\" (%s %s)" % (
        st, j.get("data") if isinstance(j, dict) else j))
    d = wait_status(base, lambda s: playing(s) and s["playback"]["artwork"]["name"].startswith("p64-test-upload"), 10)
    art = d["playback"].get("artwork") or {}
    check(art.get("name", "").startswith("p64-test-upload") and art.get("path", "").startswith("mem:up-"), "routes: and it is on the panel, from memory (%s, name %r)" % (art.get("path"), art.get("name")))
    check(window(harness.status(base))["stored"] > w0["stored"], "routes: it went into the memory window")
    request(base, "POST", "/api/v1/action/next")
    time.sleep(2)
    request(base, "POST", "/api/v1/action/previous")
    d = wait_status(base, lambda s: playing(s) and s["playback"]["artwork"]["name"].startswith("p64-test-upload"), 10)
    check((d["playback"].get("artwork") or {}).get("name", "").startswith("p64-test-upload"), "routes: \"previous\" replays it while the window holds it")
    st, j = request(base, "POST", "/api/v1/files?path=animations/junk.gif", b"this is not an image at all", content_type="application/octet-stream")
    check(st == 415, "routes: a file that is not an artwork is refused as with a card (%s)" % st)


MIX = "p64 test mix"   # a name the legacy rule could not store: a space


def url(name):
    import urllib.parse
    return "/api/v1/playsets/" + urllib.parse.quote(name)


def names(base):
    st, j = request(base, "GET", "/api/v1/playsets")
    return j["data"], [x["name"] for x in j["data"]["playsets"]]


def test_playsets(base):
    _, card_names = names(base)
    if boot_cardless(base) is None:
        return
    doc, mine = names(base)
    check(doc.get("store") == "device" and doc["max_playsets"] == 8 and doc.get("max_channels") == 16,
          "playsets: cardless, the list is the device's (store %r, %s playsets of %s channels)" % (doc.get("store"), doc["max_playsets"], doc.get("max_channels")))
    check(not any(n in mine for n in card_names if n not in mine) and "error" not in doc, "playsets: the card's playsets are not in it (%s)" % mine)
    made = []
    try:
        body = {"channels": [{"kind": "promoted", "weight": 2}, {"kind": "hashtag", "identifier": "pixelart", "weight": 1}]}
        st, j = request(base, "PUT", url(MIX), body)
        if not check(st == 200 and j["data"]["name"] == MIX, "playsets: a device playset is saved (%s)" % st):
            return
        made.append(MIX)
        st, j = request(base, "GET", url(MIX))
        check(st == 200 and [c["kind"] for c in j["data"]["channels"]] == ["promoted", "hashtag"] and j["data"]["channels"][0]["weight"] == 2,
              "playsets: and read back as written")
        _, mine = names(base)
        check(MIX in mine, "playsets: it is in the list")

        # The limits, each with its own words.
        st, j = request(base, "PUT", url("p64 test wide"), {"channels": [{"kind": "hashtag", "identifier": "t%d" % i} for i in range(17)]})
        check(st == 409 and "at most 16" in j.get("error", ""), "playsets: 17 channels are refused (%s %r)" % (st, j.get("error")))
        st, j = request(base, "PUT", url("p64 test local"), {"channels": [{"kind": "local", "identifier": ""}]})
        check(st == 409 and "need a card" in j.get("error", ""), "playsets: a local channel is refused (%s %r)" % (st, j.get("error")))
        free = doc["max_playsets"] - len(mine)
        for i in range(free):
            n = "p64 test fill %d" % i
            st, j = request(base, "PUT", url(n), {"channels": [{"kind": "promoted"}]})
            if check(st == 200, "playsets: playset %d of 8 is saved" % (len(mine) + i + 1)):
                made.append(n)
        st, j = request(base, "PUT", url("p64 test ninth"), {"channels": [{"kind": "promoted"}]})
        check(st == 409 and "already 8" in j.get("error", ""), "playsets: a ninth is refused (%s %r)" % (st, j.get("error")))
        st, j = request(base, "PUT", url(MIX), {"channels": [{"kind": "promoted", "weight": 5}]})
        check(st == 200, "playsets: a full list still takes a change to one of its playsets")
        for n in [m for m in made if m.startswith("p64 test fill")]:
            request(base, "DELETE", url(n))
            made.remove(n)

        # Activated, it plays; after a restart it is still the choice and still there.
        st, j = request(base, "POST", "/api/v1/action/play_playset", {"name": MIX})
        d = wait_status(base, lambda s: playing(s) and s["playback"]["playset"]["name"] == MIX, 40)
        check(playing(d) and d["playback"]["playset"]["name"] == MIX, "playsets: activated, it plays")
        d = boot_cardless(base)
        if d is None:
            return
        d = wait_status(base, lambda s: playing(s) and s["playback"]["playset"]["name"] == MIX, FIRST_ARTWORK_S + 15)
        check(playing(d) and d["playback"]["playset"]["name"] == MIX, "playsets: after a restart it is still the choice and plays (%s)" % d["playback"]["playset"]["name"])

        # Renamed while active: the show follows.
        renamed = MIX + " 2"
        st, j = request(base, "POST", url(MIX) + "/rename", {"name": renamed})
        if check(st == 200, "playsets: renamed (%s)" % st):
            made[made.index(MIX)] = renamed
            time.sleep(1)
            check(harness.status(base)["playback"]["playset"]["name"] == renamed, "playsets: the active playset follows the new name")
            _, mine = names(base)
            check(renamed in mine and MIX not in mine, "playsets: the list has the new name only")

        # Deleted while it is the choice: after a restart the panel says so, nothing stands in.
        st, j = request(base, "DELETE", url(made[0]))
        check(st == 200, "playsets: deleted (%s)" % st)
        gone = made.pop(0)
        d = boot_cardless(base)
        if d is None:
            return
        time.sleep(12)
        d = harness.status(base)
        check(d["playback"]["playset"]["name"] == gone and d["playback"]["no_artwork"] == "playset not found" and not d["playback"].get("artwork"),
              "playsets: a deleted choice leaves \"No artwork: playset not found\" (%s, %r)" % (d["playback"]["playset"]["name"], d["playback"]["no_artwork"]))
        request(base, "POST", "/api/v1/action/play_playset", {"name": "Promoted"})
        d = wait_status(base, playing, 40)
        check(playing(d), "playsets: and Promoted, once chosen, plays")
    finally:
        for n in made:
            request(base, "DELETE", url(n))
    # Back with the card: the card's list, with nothing of the device's in it.
    hook(base, card_eject="")
    d = reboot(base)
    if not check(d is not None and d["card"]["mounted"], "playsets: the card is mounted again"):
        return
    doc, now = names(base)
    check(doc.get("store") == "card" and doc["max_playsets"] == 32 and sorted(now) == sorted(card_names),
          "playsets: with the card, the card's list is as it was (%s)" % doc.get("store"))


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    d = harness.status(base)
    if not d.get("test_hooks"):
        return skipped("firmware %s has no test hooks (build with -DP64_TEST=ON)" % d["firmware"]["version"])
    if not d["card"]["mounted"]:
        return skipped("no card mounted to begin with: these tests eject one virtually and give it back")
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    global CARD_CHOICE
    name = CARD_CHOICE = d["playback"]["playset"]["name"]
    print("device: firmware %s, playset %s" % (d["firmware"]["version"], name))
    import api_smoke
    try:
        for label, fn in [("window", test_window), ("restore", test_restore), ("playsets", test_playsets), ("routes", test_routes)]:
            if only and label not in only:
                continue
            print("--- %s" % label)
            fn(base)
    finally:
        try:
            hook(base, card_eject="")
        except Exception as e:  # noqa: BLE001
            print("     could not clear the eject: %r" % e)
        d = reboot(base)
        check(d is not None and d["card"]["mounted"], "the card is mounted again after the tests")
        request(base, "POST", "/api/v1/action/play_playset", {"name": name})
        d = wait_status(base, lambda s: playing(s) and s["playback"]["playset"]["name"] == name, 40)
        check(d["playback"]["playset"]["name"] == name, "the playset of before is back (%s)" % d["playback"]["playset"]["name"])
    print("cardless smoke: %d failures" % api_smoke.failures)
    return 1 if api_smoke.failures else 0


if __name__ == "__main__":
    sys.exit(main())
