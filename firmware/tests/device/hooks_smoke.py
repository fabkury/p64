#!/usr/bin/env python3
r"""Device tests that need the test build's fault-injection hooks (CONFIG_P64_TEST_HOOKS,
built with -DP64_TEST=ON; docs/testing-strategy/infrastructure.md):

    python tests\device\hooks_smoke.py [http://p64.local] [--only like,panic,reset,time,imu,eject]

It REBOOTS THE DEVICE several times. In this order:

- like:  holds the Makapix worker for 30 s and sends a like meanwhile. The like is
         answered 202 "queued" after 5 s (decision D25); the worker then runs it, records
         the outcome in the status and frees the job (finding F-10: it used to write into
         the waiter's dead stack frame). Needs a pairing.
- time:  withholds the time at run time, then across a reboot: the device must say it is
         not synced, keep playing what it has, and recover when the time is released.
- panic: a deliberate panic. The device must come back, count it, keep a crash summary
         whose text is valid (finding F-03), and play again.
- reset: five abrupt resets while settings are being written. Each time the settings
         document must be one of the two being written, never defaults or garbage.
- eject: boots as if no card had mounted, twice (two different reasons), and records
         what the device does without a card; then boots with the card again. Cardless
         behaviour is being redesigned (ADR 0018): this part asserts only that the device
         stays up and truthful about the card, and prints the rest as observations.

Every hook is cleared at the end, also when a check raises. Exit code 1 on a failure, 2
(SKIP) when the firmware has no hooks.
"""

import json
import random
import sys
import threading
import time
import urllib.request

import harness
from api_smoke import check, request, skipped

BOOT_WAIT = 90


def hook(base, **body):
    st, j = request(base, "POST", "/api/v1/test", body, timeout=30)
    if st != 200:
        raise RuntimeError("the test route answered %s: %s" % (st, j))
    return j["data"]


def clear_hooks(base):
    try:
        hook(base, card_eject="", time_withheld=False, hold_worker_ms=0, imu_absent_s=0)
    except Exception as e:  # noqa: BLE001
        print("     could not clear the hooks: %r" % e)


def reboot(base, how="action"):
    """Restarts the device and waits for it. Returns the status after the boot."""
    before = harness.status(base)
    try:
        if how == "action":
            request(base, "POST", "/api/v1/action/reboot", timeout=10)
        else:
            request(base, "POST", "/api/v1/test", {how: True}, timeout=10)
    except Exception:  # noqa: BLE001  the connection may die with the device
        pass
    deadline = time.time() + BOOT_WAIT
    time.sleep(3)
    while time.time() < deadline:
        try:
            d = harness.status(base, timeout=5)
            if d["uptime_s"] < before["uptime_s"] or d["uptime_s"] < 60:
                return d
        except Exception:  # noqa: BLE001
            pass
        time.sleep(1)
    return None


def wait_status(base, predicate, seconds):
    deadline = time.time() + seconds
    d = None
    while time.time() < deadline:
        try:
            d = harness.status(base, timeout=10)
            if predicate(d):
                return d
        except Exception:  # noqa: BLE001
            pass
        time.sleep(1)
    return d


def playing(d):
    return bool(d["playback"].get("artwork"))


def settle_like(base, pid, want):
    """Sends a like or an unlike and waits for it to be done, answered at once (200) or
    queued behind a busy worker (202, the outcome in the status)."""
    st, j = request(base, "POST", "/api/v1/makapix/like", {"post_id": pid, "like": want}, timeout=60)
    if st == 200:
        return j["data"]["liked"] == want
    if st != 202:
        return False
    d = wait_status(base, lambda s: (s["makapix"].get("like") or {}).get("pending") is False and
                    (s["makapix"].get("like") or {}).get("liked") == want, 60)
    like = d["makapix"].get("like") or {}
    return like.get("pending") is False and like.get("error") == "" and like.get("liked") == want


def test_like(base):
    d = harness.status(base)
    if d["makapix"]["state"] != "paired":
        print("SKIP like: needs a Makapix pairing")
        return
    original = d["playback"]["playset"]["name"]
    request(base, "POST", "/api/v1/action/play_playset", {"name": "Promoted"})
    d = wait_status(base, lambda s: (s["playback"].get("artwork") or {}).get("post_id") is not None, 60)
    art = d["playback"].get("artwork") or {}
    if not check(art.get("post_id") is not None, "like: a Makapix artwork is on the panel (post %s)" % art.get("post_id")):
        return
    pid = art["post_id"]
    try:
        hook(base, hold_worker_ms=30000)
        time.sleep(1.5)  # the worker's loop turns every 500 ms: it is asleep now
        t0 = time.time()
        st, j = request(base, "POST", "/api/v1/makapix/like", {"post_id": pid, "like": True}, timeout=60)
        took = time.time() - t0
        check(st == 202 and j["data"].get("queued") and 4.0 <= took <= 7.5,
              "like: with the worker held, the like is queued after its 5 s (%.1f s, %s %s)" % (took, st, str(j)[:70]))
        like = harness.status(base)["makapix"].get("like") or {}
        check(like.get("post_id") == pid and like.get("pending") is True, "like: the status shows it pending (%s)" % like)
        # The worker wakes, finds the abandoned job, runs it and frees it. Before the fix
        # of F-10 this wrote into the handler's dead stack frame.
        done = wait_status(base, lambda s: (s["makapix"].get("like") or {}).get("pending") is False, 45)
        like = done["makapix"].get("like") or {}
        check(like.get("pending") is False and like.get("error") == "" and like.get("liked") is True,
              "like: the worker ran it and the status shows the outcome (%s)" % like)
        after = harness.status(base)
        check(after["uptime_s"] > d["uptime_s"], "like: the device is still up after the worker ran the abandoned job")
        check(settle_like(base, pid, False), "like: the worker serves the next request (unlike post %d)" % pid)
        # Twice more, back to back: a job abandoned while another waits behind it.
        hook(base, hold_worker_ms=25000)
        time.sleep(1.5)
        st1, _ = request(base, "POST", "/api/v1/makapix/like", {"post_id": pid, "like": True}, timeout=60)
        check(st1 == 202, "like: a second queued like (%s)" % st1)
        wait_status(base, lambda s: (s["makapix"].get("like") or {}).get("pending") is False, 45)
        check(settle_like(base, pid, False), "like: and the worker still answers (unlike)")
        h = harness.status(base)["heap"]
        check(h["internal_free"] >= harness.budgets()["internal_free_min"], "like: internal heap %d above its floor" % h["internal_free"])
    finally:
        hook(base, hold_worker_ms=0)
        request(base, "POST", "/api/v1/action/play_playset", {"name": original})
        time.sleep(3)


def test_time(base):
    d = harness.status(base)
    if not check(d["time"]["synced"], "time: the device has the time to begin with"):
        return
    try:
        hook(base, time_withheld=True)
        d = wait_status(base, lambda s: not s["time"]["synced"], 10)
        check(not d["time"]["synced"], "time: withheld, the status says not synced")
        check(not d["makapix"]["online"] or wait_status(base, lambda s: not s["makapix"]["online"], 10) is not None and
              not harness.status(base)["makapix"]["online"], "time: the Makapix client counts itself offline without the time")
        hook(base, time_withheld=False)
        d = wait_status(base, lambda s: s["time"]["synced"], 10)
        check(d["time"]["synced"], "time: released, the status says synced again")

        # Across a boot: no trusted time from power-on. With a card, what is cached plays.
        hook(base, time_withheld=True)
        d = reboot(base)
        if not check(d is not None, "time: the device came back from the reboot"):
            return
        check(d.get("test_hooks") and not d["time"]["synced"], "time: withheld from boot (synced %s)" % d["time"]["synced"])
        if d["card"]["mounted"]:
            d = wait_status(base, playing, 30)
            check(playing(d), "time: without the time, an artwork plays from the card (%s)" % (d["playback"].get("artwork") or {}).get("name"))
        time.sleep(15)
        d = harness.status(base)
        check(not d["time"]["synced"], "time: still withheld 15 s later, whatever NTP answered underneath")
        hook(base, time_withheld=False)
        d = wait_status(base, lambda s: s["time"]["synced"], 60)
        check(d["time"]["synced"], "time: released, the device is synced (NTP had answered meanwhile)")
        d = wait_status(base, lambda s: s["makapix"]["online"], 60)
        check(d["makapix"]["online"], "time: and the Makapix client is online again")
    finally:
        hook(base, time_withheld=False)


def test_imu(base):
    """An IMU that misses its probe at boot is found later (finding F-33): until
    2026-11-12 one miss left the device without taps and auto-rotation, at the fallback
    rotation, until the next restart."""
    d = harness.status(base)
    if not d["inputs"]["imu_present"]:
        print("     imu: this device has no IMU answering; not tested")
        return
    want = d["inputs"]["auto_rotation"]
    try:
        hook(base, imu_absent_s=25)
        d = reboot(base)
        if not check(d is not None, "imu: the device came back from the reboot"):
            return
        check(not d["inputs"]["imu_present"], "imu: withheld, the device boots without it (uptime %s s)" % d["uptime_s"])
        d = wait_status(base, lambda s: s["inputs"]["imu_present"] and s["inputs"]["auto_rotation_resolved"], 45)
        check(d["inputs"]["imu_present"], "imu: found later, without a restart (uptime %s s)" % d["uptime_s"])
        check(d["inputs"]["calibrated"] and d["inputs"]["auto_rotation"] == want,
              "imu: its calibration is loaded and the rotation is the one before (%s, was %s)" % (d["inputs"]["auto_rotation"], want))
    finally:
        hook(base, imu_absent_s=0)


def test_panic(base):
    before = harness.status(base)
    request(base, "POST", "/api/v1/diag/coredump/erase")
    d = reboot(base, "panic")
    if not check(d is not None, "panic: the device came back"):
        return
    # harness.status() parses strictly: a crash summary with raw bytes raises here (F-03).
    rel = d["reliability"]
    check(rel["reset_reason"] == "panic", "panic: the reset reason is panic (%s)" % rel["reset_reason"])
    check(rel["counters"]["panic"] == before["reliability"]["counters"]["panic"] + 1,
          "panic: counted once (%d -> %d)" % (before["reliability"]["counters"]["panic"], rel["counters"]["panic"]))
    crash = rel["crash"]
    check(crash.get("present") and crash.get("task") and all(32 <= ord(c) <= 126 for c in crash["task"]),
          "panic: a crash summary with a printable task name (%r, pc %s)" % (crash.get("task"), crash.get("pc")))
    d = wait_status(base, playing, 40)
    check(playing(d), "panic: the show plays again after the boot")
    st, _ = request(base, "POST", "/api/v1/diag/coredump/erase")
    check(st == 200 and not harness.status(base)["reliability"]["crash"]["present"], "panic: the core dump is erased")


def test_reset(base):
    original = harness.settings(base)
    values = (201, 202)
    stop = threading.Event()

    def writer():
        i = 0
        while not stop.is_set():
            try:
                urllib.request.urlopen(urllib.request.Request(
                    base + "/api/v1/settings", data=json.dumps({"display": {"brightness": values[i % 2]}}).encode(),
                    method="PUT", headers={"Content-Type": "application/json"}), timeout=5).read()
            except Exception:  # noqa: BLE001  the device is resetting
                time.sleep(0.2)
            i += 1

    rng = random.Random(64)
    try:
        request(base, "PUT", "/api/v1/settings", {"display": {"brightness": values[0]}})
        # A change is stored within 2 s (0.3 s after the last one, decision D30): wait
        # until the first value is in flash, so that from here on the stored document is
        # always one of the two being written.
        time.sleep(3)
        for n in range(5):
            stop.clear()
            t = threading.Thread(target=writer, daemon=True)
            t.start()
            time.sleep(rng.uniform(0.3, 2.5))
            d = reboot(base, "reset")
            stop.set()
            t.join(timeout=10)
            if not check(d is not None, "reset %d: the device came back" % (n + 1)):
                return
            now = harness.settings(base)
            expect = json.loads(json.dumps(original))
            got = now["display"]["brightness"]
            expect["display"]["brightness"] = got
            check(got in values and now == expect,
                  "reset %d: the settings are one of the two documents being written (brightness %s, rest intact: %s)" %
                  (n + 1, got, now == expect))
            check(d["card"]["mounted"] == harness.status(base)["card"]["mounted"], "reset %d: the card state is stable" % (n + 1))
        d = wait_status(base, playing, 40)
        check(playing(d), "reset: the show plays after the last reset")
    finally:
        stop.set()
        request(base, "PUT", "/api/v1/settings", original)


def observe_cardless(base, reason, seconds=75):
    """Boots ejected for `reason` and prints what the device does; asserts little."""
    hook(base, card_eject=reason)
    t0 = time.time()
    d = reboot(base)
    if not check(d is not None, "eject (%s): the device came back" % reason):
        return
    check(not d["card"]["mounted"] and d["card"]["error"] == reason,
          "eject (%s): the status says no card, with the reason (%s)" % (reason, d["card"]["error"]))
    first = None
    samples = []
    while time.time() - t0 < seconds:
        try:
            d = harness.status(base, timeout=10)
        except Exception as e:  # noqa: BLE001
            check(False, "eject (%s): the device answers (%r)" % (reason, e))
            return
        pb = d["playback"]
        if first is None and pb.get("artwork"):
            first = d["uptime_s"]
        samples.append((d["uptime_s"], pb["playset"]["name"], (pb.get("artwork") or {}).get("name"), pb.get("no_artwork"), pb.get("screen"),
                        d["makapix"]["downloads"], d["heap"]["internal_free"], d["heap"]["psram_free"]))
        time.sleep(5)
    check(d["uptime_s"] >= seconds - 25, "eject (%s): up for the whole observation (uptime %d s)" % (reason, d["uptime_s"]))
    b = harness.budgets()
    low = min(s[6] for s in samples)
    check(low >= b["soak_internal_free_min"], "eject (%s): internal heap never under the soak floor (low %d)" % (reason, low))
    print("     observed cardless (%s): playset %s, first artwork at uptime %s s, downloads %d in %d s, PSRAM low %d, no_artwork %r" % (
        reason, samples[-1][1], first, samples[-1][5], samples[-1][0], min(s[7] for s in samples), samples[-1][3]))
    for s in samples[::3]:
        print("       t=%3d s  artwork %-28s screen %-10s downloads %3d  heap %d" % (s[0], str(s[2])[:28], s[4] or "-", s[5], s[6]))


def test_eject(base):
    before = harness.status(base)
    if not check(before["card"]["mounted"], "eject: the card is mounted to begin with"):
        return
    name = before["playback"]["playset"]["name"]
    try:
        observe_cardless(base, "no card answers")
        observe_cardless(base, "no FAT file system on the card", seconds=40)
    finally:
        hook(base, card_eject="")
    d = reboot(base)
    if not check(d is not None, "eject: the device came back with the hook cleared"):
        return
    check(d["card"]["mounted"] and not d["card"]["error"], "eject: the card is mounted again")
    d = wait_status(base, lambda s: playing(s) and s["playback"]["playset"]["name"] == name, 40)
    check(playing(d) and d["playback"]["playset"]["name"] == name,
          "eject: the saved playset is back and plays (%s)" % d["playback"]["playset"]["name"])


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    d = harness.status(base)
    if not d.get("test_hooks"):
        return skipped("firmware %s has no test hooks (build with -DP64_TEST=ON)" % d["firmware"]["version"])
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    tests = [("like", test_like), ("time", test_time), ("panic", test_panic), ("reset", test_reset), ("imu", test_imu), ("eject", test_eject)]
    print("device: firmware %s, uptime %d s" % (d["firmware"]["version"], d["uptime_s"]))
    try:
        for name, fn in tests:
            if only and name not in only:
                continue
            print("--- %s" % name)
            fn(base)
    finally:
        clear_hooks(base)
    from api_smoke import failures
    print("hooks smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
