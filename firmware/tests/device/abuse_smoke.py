#!/usr/bin/env python3
r"""Device regression test of the abuse findings of 2026-11-11 (firmware 0.2.2):

    python tests\device\abuse_smoke.py [http://p64.local]

What it does to the device, in this order:

- F-14: POSTs to the setup portal's /setup/save and /setup/erase on the running device
  and expects 403. /setup/erase is only tried once /setup/save was refused, so a
  firmware without the fix keeps its Wi-Fi credentials.
- F-02: sends request bodies nested 17 to 15000 levels deep to every kind of route that
  parses JSON and expects 400 with the device still up. A FIRMWARE WITHOUT THE FIX
  REBOOTS HERE (that is the finding).
- F-01: fires bursts of previous/next, history jumps and playset activations from 16
  connections at once, then expects the show to go on by itself: an artwork up, a swap
  within the interval, the next artwork prepared, nothing dropped in the log.
- F-03: every status document read on the way must be strict UTF-8 JSON, and the crash
  summary's task name printable.
- The web hardening of 2026-11-11 (decisions D25 to D30): a foreign Origin is refused, errors
  come in the API's envelope, a wrong type in a settings write is a 400 naming the key, a
  burst of 120 settings writes costs a handful of flash writes with applied equal to
  stored, six half-sent requests hold other clients for the 5 s timeout at most, a
  refused 8 MB upload is cut off instead of drained, and five pages with their
  WebSockets are served.

It sets the auto-swap interval to 5 s for the run and restores the whole settings
document and the active playset at the end, also when a check raises. Needs artworks to
play (any playset with at least three). Exit code 1 on any failure.
"""

import base64
import concurrent.futures
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from api_smoke import budgets, check, playset_restored, request

NESTING_LIMIT = 16  # CJSON_NESTING_LIMIT in firmware/CMakeLists.txt


def status(base):
    """The status document, parsed strictly: bytes that are not UTF-8 raise (F-03)."""
    with urllib.request.urlopen(base + "/api/v1/status", timeout=20) as r:
        return json.loads(r.read().decode("utf-8", "strict"))["data"]


def try_request(base, method, path, body=None, content_type="application/json", timeout=30):
    """request() that reports a transport failure as status -1 instead of raising."""
    try:
        st, j = request(base, method, path, body, content_type=content_type, timeout=timeout)
        return st, j
    except Exception as e:  # noqa: BLE001  a timeout or a reset is a result here
        return -1, repr(e)


def log_since(base, uptime_ms):
    """The device log's warning and error lines stamped at or after `uptime_ms`."""
    with urllib.request.urlopen(base + "/api/v1/diag/log?bytes=65536", timeout=30) as r:
        text = r.read().decode("utf-8", "replace")
    lines = []
    for line in text.splitlines():
        if len(line) < 4 or line[0] not in "WE" or line[1:3] != " (":
            continue
        try:
            stamp = int(line[3:line.index(")")])
        except ValueError:
            continue
        if stamp >= uptime_ms:
            lines.append(line)
    return lines


def nested(depth, opener=b"[", closer=b"]"):
    return opener * depth + closer * depth


def still_up(base, before, what):
    """The device answers, did not reboot and counted no panic since `before`."""
    try:
        now = status(base)
    except Exception as e:  # noqa: BLE001
        check(False, "%s: the device answers (%r)" % (what, e))
        time.sleep(20)  # give a rebooting device time, so the later checks say something
        return None
    ok = now["uptime_s"] >= before["uptime_s"] and \
        now["reliability"]["counters"]["panic"] == before["reliability"]["counters"]["panic"]
    check(ok, "%s: no reboot, no panic (uptime %d -> %d s)" % (what, before["uptime_s"], now["uptime_s"]))
    return now


def setup_routes(base):
    form = "application/x-www-form-urlencoded"
    st, _ = try_request(base, "POST", "/setup/save", b"ssid=", content_type=form)
    refused = check(st == 403, "F-14: /setup/save is refused outside setup mode (%s)" % st)
    if not refused:
        check(False, "F-14: /setup/erase not tried (it would erase the Wi-Fi credentials of this firmware)")
        return
    st, _ = try_request(base, "POST", "/setup/erase", b"", content_type=form)
    check(st == 403, "F-14: /setup/erase is refused outside setup mode (%s)" % st)
    time.sleep(3)
    d = status(base)
    check(d["network"]["connected"] and not d["network"]["setup_mode"], "F-14: the device is still on its network")


def nesting(base, before):
    # The limit itself: 16 levels parse (and are refused for what they are), 17 do not.
    st, j = try_request(base, "PUT", "/api/v1/settings", nested(NESTING_LIMIT))
    check(st == 400 and "object" in str(j), "F-02: %d levels are parsed and refused as a non-object (%s %s)" % (NESTING_LIMIT, st, str(j)[:60]))
    routes = [
        ("PUT", "/api/v1/settings"),
        ("POST", "/api/v1/auth/login"),
        ("POST", "/api/v1/action/history_go"),
        ("POST", "/api/v1/action/play_playset"),
        ("POST", "/api/v1/action/play"),
        ("POST", "/api/v1/files/rename"),
    ]
    for depth in (NESTING_LIMIT + 1, 50, 200, 1000, 15000):
        for method, path in routes:
            if depth > 200 and path != "/api/v1/settings":
                continue  # the deepest bodies once: each costs a second of the device's time
            for body in (nested(depth), b'{"display":' + nested(depth) + b"}", nested(depth, b'{"a":', b"}")):
                st, j = try_request(base, method, path, body)
                if not (400 <= st < 500):
                    check(False, "F-02: %s %s with %d levels answers 4xx (%s %s)" % (method, path, depth, st, str(j)[:60]))
        if still_up(base, before, "F-02: after bodies of %d levels" % depth) is None:
            return
    check(True, "F-02: every nested body was answered 4xx")


def burst(base, calls, workers=16):
    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        results = list(ex.map(lambda c: try_request(base, "POST", c[0], c[1], timeout=40)[0], calls))
    histogram = {}
    for st in results:
        histogram[st] = histogram.get(st, 0) + 1
    return histogram, time.time() - t0


def wait_for(base, predicate, seconds):
    deadline = time.time() + seconds
    d = None
    while time.time() < deadline:
        try:
            d = status(base)
            if predicate(d):
                return d
        except Exception:  # noqa: BLE001  the next poll tells
            pass
        time.sleep(0.5)
    return d


def storms(base, before, interval):
    # A history to walk: a few artworks shown one after the other.
    for _ in range(4):
        try_request(base, "POST", "/api/v1/action/next")
        time.sleep(1.2)
    d = status(base)
    count = d["playback"]["history"]["count"]
    check(count >= 3 and d["playback"].get("artwork"), "the show plays and has a history to walk (%d items)" % count)
    start_ms = d["uptime_s"] * 1000

    cases = [
        ("240 previous/next", [("/api/v1/action/previous" if i % 2 else "/api/v1/action/next", None) for i in range(240)]),
        ("120 history jumps", [("/api/v1/action/history_go", {"position": i % count}) for i in range(120)]),
        ("40 activations", [("/api/v1/action/play_playset", {"name": d["playback"]["playset"]["name"]})] * 40),
        ("120 next", [("/api/v1/action/next", None)] * 120),
    ]
    for name, calls in cases:
        swaps0 = status(base)["playback"]["swaps"]
        histogram, took = burst(base, calls)
        check(set(histogram) <= {200, 202, 503}, "F-01: %s from 16 connections: every request answered (%s in %.1f s)" % (name, histogram, took))
        check(took < 20, "F-01: %s: the web server kept up (%.1f s)" % (name, took))
        # The show goes on by itself: an artwork (or an interlude) up now, then a fresh
        # swap within the interval, then the next artwork prepared.
        settled = status(base)
        swaps_settled = settled["playback"]["swaps"]
        after = wait_for(base, lambda s: s["playback"]["swaps"] > swaps_settled and s["playback"].get("artwork"), 3 * interval + 15)
        ok = after is not None and after["playback"]["swaps"] > swaps_settled and bool(after["playback"].get("artwork"))
        check(ok, "F-01: %s: a new artwork went up by itself afterwards (swaps %d -> %d -> %s)" %
              (name, swaps0, swaps_settled, after and after["playback"]["swaps"]))
        prepared = wait_for(base, lambda s: s["playback"]["prepared"], 15)
        check(prepared is not None and prepared["playback"]["prepared"], "F-01: %s: the next artwork is prepared" % name)
        if still_up(base, before, "F-01: %s" % name) is None:
            return
    lines = log_since(base, start_ms)
    bad = [line for line in lines if "queue full" in line or "dropped" in line or "never came back" in line]
    check(not bad, "F-01: nothing was dropped or lost in the device log (%d lines%s)" % (len(bad), ": " + bad[0][:90] if bad else ""))


def raw_request(base, head, body=b"", read_s=8.0):
    """Sends raw bytes on a fresh connection and returns what came back (b"" on a close)."""
    host = urllib.parse.urlparse(base).hostname
    s = socket.create_connection((host, 80), timeout=10)
    try:
        s.sendall(head + body)
        s.settimeout(read_s)
        data = b""
        try:
            while b"\r\n\r\n" not in data:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
        except OSError:
            pass
        return data
    finally:
        s.close()


def timed_status(base):
    t0 = time.time()
    try:
        status(base)
        return time.time() - t0
    except Exception:  # noqa: BLE001
        return 99.0


def origin_and_errors(base):
    host = urllib.parse.urlparse(base).netloc
    # D29: a request that changes the device and names another site is refused.
    def post(origin, path="/api/v1/action/reset_timer", method="POST"):
        head = "%s %s HTTP/1.1\r\nHost: %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n" % (
            method, path, host, ("Origin: %s\r\n" % origin) if origin else "")
        reply = raw_request(base, head.encode())
        return int(reply.split(b" ")[1]) if reply.startswith(b"HTTP/1.1 ") else -1

    check(post("") == 200, "origin: a request with no Origin (a script) is served")
    check(post("http://" + host) == 200, "origin: the device's own page is served")
    check(post("https://example.com") == 403, "origin: another site's page is refused (403)")
    check(post("null") == 403, "origin: a sandboxed or local page is refused (403)")
    check(post("http://evil." + host) == 403, "origin: a look-alike host is refused (403)")
    check(post("https://example.com", "/api/v1/settings", "PUT") == 403, "origin: also for a settings write")
    check(post("https://example.com", "/api/v1/status", "GET") == 200, "origin: reading is not refused")
    # D28: unknown routes and wrong methods answer in the API's envelope.
    st, j = try_request(base, "GET", "/api/v1/no_such_route")
    check(st == 404 and isinstance(j, dict) and j.get("code") == "NOT_FOUND", "errors: an unknown route answers 404 in the envelope (%s %s)" % (st, str(j)[:60]))
    st, j = try_request(base, "DELETE", "/api/v1/settings")
    check(st == 405 and isinstance(j, dict) and j.get("code") == "METHOD_NOT_ALLOWED", "errors: a wrong method answers 405 in the envelope (%s %s)" % (st, str(j)[:60]))


def settings_rules(base, original):
    # D28: a wrong type is refused with the key's name; nothing of the document is applied.
    cases = [({"display": {"brightness": "bright"}}, "brightness"), ({"display": {"mirror": 1}}, "mirror"),
             ({"display": {"panel_mode": "cinema"}}, "panel_mode"), ({"display": "x"}, "display"),
             ({"display": {"brightness": 9, "mirror": "yes"}}, "mirror")]
    for body, key in cases:
        st, j = try_request(base, "PUT", "/api/v1/settings", body)
        check(st == 400 and isinstance(j, dict) and j.get("code") == "INVALID_SETTINGS" and str(j.get("error", "")).startswith(key),
              "settings: %s is refused naming %s (%s %s)" % (json.dumps(body), key, st, str(j)[:70]))
    st, j = try_request(base, "PUT", "/api/v1/settings", {"display": {"no_such_key": 1}, "no_such_group": {}})
    check(st == 200, "settings: unknown keys are ignored (%s)" % st)
    check(request(base, "GET", "/api/v1/settings")[1]["data"] == original, "settings: none of that changed the document")

    # D30 and F-05: a burst of writes costs few flash writes, drops no event, and what is
    # applied in the end is what is stored.
    d0 = status(base)
    start_ms = d0["uptime_s"] * 1000
    writes0 = d0.get("settings_nvs_writes")
    values = [200 + (i % 2) for i in range(119)] + [137]

    def put(v):
        return try_request(base, "PUT", "/api/v1/settings", {"display": {"brightness": v}})[0]

    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(put, values[:-1]))
    burst_s = time.time() - t0
    check(all(st == 200 for st in results), "settings: 119 writes from 8 connections all answered (%.1f s)" % burst_s)
    check(put(values[-1]) == 200, "settings: the last write of the burst")
    time.sleep(1.0)
    d = status(base)
    stored = request(base, "GET", "/api/v1/settings")[1]["data"]["display"]["brightness"]
    check(stored == 137 and d["panel"]["brightness"] == 137,
          "settings: applied equals stored after the burst (stored %s, on the panel %s)" % (stored, d["panel"]["brightness"]))
    time.sleep(2.5)  # the deferred write lands
    d = status(base)
    if writes0 is not None:
        n = d["settings_nvs_writes"] - writes0
        check(1 <= n <= 6, "settings: the burst of 120 changes cost %d flash writes" % n)
    st, _ = try_request(base, "PUT", "/api/v1/settings", {"display": {"brightness": 137}})
    time.sleep(3)
    if writes0 is not None:
        check(status(base)["settings_nvs_writes"] == d["settings_nvs_writes"], "settings: a write that changes nothing writes nothing")
    dropped = [line for line in log_since(base, start_ms) if "dropped" in line]
    check(not dropped, "settings: no event was dropped (%d%s)" % (len(dropped), ": " + dropped[0][:80] if dropped else ""))


def stalled_clients(base):
    host = urllib.parse.urlparse(base).hostname
    # F-04, decision D25: one stalled client holds the server for its 5 s timeout, no longer
    # (10 s each way before, and a body drained for as long as it took). Stalled clients
    # are served one after the other, so several at once add up: six cost about 30 s once,
    # where they cost 90 s on 0.2.1.
    def stall(n):
        socks = []
        try:
            for _ in range(n):
                s = socket.create_connection((host, 80), timeout=10)
                s.sendall(b"GET /api/v1/status HTTP/1.1\r\nHost: p64\r\nX-Slow: ")
                socks.append(s)
            time.sleep(0.3)
            t0 = time.time()
            while time.time() - t0 < 60:
                try:
                    with urllib.request.urlopen(base + "/api/v1/status", timeout=60) as r:
                        r.read()
                    return time.time() - t0
                except Exception:  # noqa: BLE001
                    time.sleep(0.5)
            return 99.0
        finally:
            for s in socks:
                s.close()

    one = stall(1)
    check(one <= 7.0, "stall: behind one half-sent request, another client is answered in %.1f s (5 s timeout)" % one)
    six = stall(6)
    check(six <= 6 * 5 + 5, "stall: behind six half-sent requests, in %.1f s (they are timed out one after the other)" % six)
    check(timed_status(base) <= 1.0, "stall: and the server is back to normal afterwards")
    # An upload that is refused is not drained: the refusal comes at once and nobody waits.
    size = 8 * 1024 * 1024
    s = socket.create_connection((host, 80), timeout=10)
    try:
        s.sendall(("POST /api/v1/files?path=animations/_abuse_big.gif HTTP/1.1\r\nHost: p64\r\n"
                   "Content-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n" % size).encode())
        t0 = time.time()
        sent, closed = 0, False
        chunk = b"\0" * 16384
        waits = []
        try:
            while sent < size and time.time() - t0 < 30:
                sent += s.send(chunk)
                if len(waits) < 4 and time.time() - t0 > len(waits):
                    waits.append(timed_status(base))
        except OSError:
            closed = True
        check(closed and sent < size, "upload: an 8 MB upload is refused and cut off (%d KB sent in %.1f s)" % (sent // 1024, time.time() - t0))
        waits.append(timed_status(base))
        check(max(waits) <= 3.0, "upload: other clients are answered meanwhile (slowest %.1f s)" % max(waits))
    finally:
        s.close()


def many_pages(base):
    # D27: four pages open at once, each with its WebSocket; a fifth is polite.
    host = urllib.parse.urlparse(base).hostname
    socks = []
    try:
        for i in range(5):
            s = socket.create_connection((host, 80), timeout=10)
            key = base64.b64encode(os.urandom(16)).decode()
            s.sendall(("GET /api/v1/ws HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (host, key)).encode())
            first = s.recv(1024).split(b"\r\n")[0]
            socks.append((s, b"101" in first))
        check(sum(1 for _, ok in socks if ok) >= 4, "pages: four WebSockets are accepted (%d of 5 upgraded)" % sum(1 for _, ok in socks if ok))
        got = [0] * len(socks)
        worst = 0.0
        t0 = time.time()
        while time.time() - t0 < 12:
            for i, (s, _) in enumerate(socks):
                s.settimeout(0.01)
                try:
                    got[i] += len(s.recv(65536))
                except OSError:
                    pass
            worst = max(worst, timed_status(base))
            try_request(base, "GET", "/api/v1/frame", timeout=10)
        check(sum(1 for n in got if n > 0) >= 4, "pages: at least four of them receive the status push (%s bytes)" % got)
        check(worst <= 3.0, "pages: with five pages open a request is answered in %.1f s at the most" % worst)
    finally:
        for s, _ in socks:
            s.close()
    # A WebSocket from another site is refused at its handshake (D29).
    s = socket.create_connection((host, 80), timeout=10)
    try:
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall(("GET /api/v1/ws HTTP/1.1\r\nHost: %s\r\nOrigin: https://example.com\r\nUpgrade: websocket\r\n"
                   "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (host, key)).encode())
        # The server answers the upgrade before the device's check runs; the refusal is
        # the connection closed at once, with no status frame ever sent.
        s.settimeout(4)
        data, closed = b"", False
        t0 = time.time()
        try:
            while time.time() - t0 < 4:
                chunk = s.recv(4096)
                if not chunk:
                    closed = True
                    break
                data += chunk
        except OSError as e:
            closed = not isinstance(e, socket.timeout)
        frames = data.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in data else b""
        check(closed and b'"type"' not in frames, "pages: a WebSocket from another site is closed without a frame (closed %s, %d bytes after the handshake)" % (closed, len(frames)))
    finally:
        s.close()


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    before = status(base)
    print("device: firmware %s, uptime %d s, heap %s" % (before["firmware"]["version"], before["uptime_s"], before["heap"]))
    task = before["reliability"]["crash"].get("task", "")
    check(all(32 <= ord(c) <= 126 for c in task), "F-03: the crash summary's task name is printable (%r)" % task)
    original = request(base, "GET", "/api/v1/settings")[1]["data"]
    interval = 5
    with playset_restored(base):
        try:
            setup_routes(base)
            origin_and_errors(base)
            settings_rules(base, original)
            request(base, "PUT", "/api/v1/settings", original)
            stalled_clients(base)
            many_pages(base)
            nesting(base, before)
            st, _ = request(base, "PUT", "/api/v1/settings", {"show": {"auto_swap_seconds": interval}})
            check(st == 200, "auto-swap at %d s for the storms" % interval)
            storms(base, before, interval)
        finally:
            st, _ = try_request(base, "PUT", "/api/v1/settings", original)
            restored = try_request(base, "GET", "/api/v1/settings")[1]
            check(st == 200 and isinstance(restored, dict) and restored.get("data") == original, "the settings are what they were")
    time.sleep(10)
    after = still_up(base, before, "at the end")
    if after:
        b = budgets()
        heap = after["heap"]
        check(heap["internal_free"] >= b["internal_free_min"],
              "internal heap back above its floor (%d >= %d)" % (heap["internal_free"], b["internal_free_min"]))
        print("device: uptime %d s, heap %s" % (after["uptime_s"], heap))
    from api_smoke import failures
    print("abuse smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
