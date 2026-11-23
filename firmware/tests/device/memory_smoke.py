#!/usr/bin/env python3
r"""Finding F-21: a large buffer the PSRAM has no room for is refused, not fatal.

A whole artwork file is one buffer of up to 5 MB (a download, an upload, a file read from
the card), and the buffers that can exist at once exceed the 16 MB of PSRAM. With C++
exceptions off a failed allocation aborts and the device reboots, so every such buffer is
granted by `system::big` only while 2 MB stay free, and refused otherwise.

This test takes the PSRAM away with the test build's hook (`psram_hold`), leaving 4 MB
free, and asks for buffers:

- an upload of 3 MB is refused with 507 `NO_MEMORY`; one of 1 MB is taken;
- a play-this of a 3 MB file by URL is refused, served with its length and without it
  (a body without a Content-Length grows step by step, so it is refused on the way);
- the device did not restart, lost no memory, and plays on;
- with the memory given back, the same upload and the same download are taken.

The files are filler, not pictures: taken, they are then turned away for what they are
(415, "not a GIF..."), which is the proof that the buffer was given. Nothing is written
to the card.

    python tests\device\memory_smoke.py [http://p64.local]

Exit code 1 on a failure, 2 (SKIP) when the firmware has no hooks.
"""

import http.server
import socket
import sys
import threading
import time
import urllib.parse

import harness
import api_smoke
from api_smoke import check, request, skipped

MB = 1024 * 1024
LEFT_FREE = 4 * MB       # what the hold leaves: under 3 MB + the 2 MB reserve, over 1 MB + the reserve
BIG, SMALL = 3 * MB, 1 * MB
REFUSED = "not enough memory for this file now"


def local_ip_towards(host):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((host, 80))
    ip = s.getsockname()[0]
    s.close()
    return ip


class Filler(http.server.BaseHTTPRequestHandler):
    """/sized/<n> answers n bytes with a Content-Length; /stream/<n> the same bytes
    without one, the connection closed at the end."""
    protocol_version = "HTTP/1.0"
    served = []

    def log_message(self, *a):
        pass

    def do_GET(self):
        kind, n = self.path.strip("/").split("/")[:2]
        n = int(n.split(".")[0])
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        if kind == "sized":
            self.send_header("Content-Length", str(n))
        self.end_headers()
        sent = 0
        block = b"p64 " * 4096
        try:
            while sent < n:
                part = block[: min(len(block), n - sent)]
                self.wfile.write(part)
                sent += len(part)
        except OSError:
            pass  # the device hung up: that is the refusal
        Filler.served.append((self.path, sent))


def hook(base, **body):
    st, j = request(base, "POST", "/api/v1/test", body, timeout=30)
    if st != 200:
        raise RuntimeError("the test route answered %s: %s" % (st, j))
    return j["data"]


def heap(base):
    return harness.status(base)["heap"]


def upload(base, size):
    return request(base, "POST", "/api/v1/files?path=animations/memory-smoke.gif", b"\x00" * size,
                   content_type="application/octet-stream", timeout=120)


def play_url(base, url, seconds=40):
    """Queues a play-this by URL and returns the worker's verdict on it (its last error)."""
    hook(base, hold_worker_ms=0)
    before = harness.status(base)["makapix"]
    st, j = request(base, "POST", "/api/v1/action/play", {"url": url}, timeout=30)
    if st != 200:
        return "HTTP %s %s" % (st, j)
    deadline = time.time() + seconds
    while time.time() < deadline:
        time.sleep(1)
        m = harness.status(base)["makapix"]
        if m.get("last_error") and m.get("last_error") != before.get("last_error") or \
                m.get("last_error") and not m.get("activity") and time.time() > deadline - seconds + 6:
            return m["last_error"]
    return "(no verdict in %d s)" % seconds


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    st, _ = request(base, "GET", "/api/v1/test")
    if st != 200:
        return skipped("this firmware has no test hooks (build with -DP64_TEST=ON)")
    d0 = harness.status(base)
    if "big_refused" not in d0["heap"]:
        return skipped("this firmware has no large-buffer rule (finding F-21)")
    ip = local_ip_towards(socket.gethostbyname(urllib.parse.urlparse(base).hostname))
    server = http.server.ThreadingHTTPServer((ip, 0), Filler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    here = "http://%s:%d" % (ip, server.server_address[1])
    resets0 = dict(d0["reliability"]["counters"])
    free0 = d0["heap"]["psram_free"]
    print("     PSRAM free %d, largest block %d; serving filler at %s" % (free0, d0["heap"]["psram_largest"], here))
    try:
        held = hook(base, psram_hold=free0 - LEFT_FREE)["psram_hold"]
        h = heap(base)
        check(abs(h["psram_free"] - LEFT_FREE) < 600 * 1024, "the hook holds %d bytes: %d free" % (held, h["psram_free"]))
        refused0 = h["big_refused"]

        st, j = upload(base, BIG)
        check(st == 507 and isinstance(j, dict) and j.get("code") == "NO_MEMORY",
              "a %d MB upload with %.1f MB free is refused: %s %s" % (BIG // MB, h["psram_free"] / MB, st, j if st != 507 else j.get("code")))
        st, j = upload(base, SMALL)
        check(st == 415, "a %d MB upload is taken (then turned away as no picture): %s" % (SMALL // MB, st))

        err = play_url(base, "%s/sized/%d.gif" % (here, BIG))
        check(REFUSED in err, "a %d MB download with its length is refused: %r" % (BIG // MB, err))
        request(base, "POST", "/api/v1/action/play", {"url": here + "/sized/100.gif"})  # a different error in between
        time.sleep(4)
        err = play_url(base, "%s/stream/%d.gif" % (here, BIG))
        check(REFUSED in err, "a %d MB download without a length is refused on the way: %r" % (BIG // MB, err))
        stream = [s for p, s in Filler.served if p.startswith("/stream/")]
        check(bool(stream) and stream[-1] < BIG, "the device hung up before the end (%s of %d bytes sent)" % (stream[-1:] or "?", BIG))

        h = heap(base)
        check(h["big_refused"] >= refused0 + 3, "three refusals counted: %d" % (h["big_refused"] - refused0))
        d = harness.status(base)
        check(d["uptime_s"] >= d0["uptime_s"] and d["reliability"]["counters"] == resets0, "no restart")
    finally:
        hook(base, psram_hold=0, hold_worker_ms=0)
    time.sleep(2)
    h = heap(base)
    check(h["psram_free"] > free0 - 400 * 1024, "the memory is back: %d free (%d before)" % (h["psram_free"], free0))
    st, j = upload(base, BIG)
    check(st == 415, "the %d MB upload is taken now: %s" % (BIG // MB, st))
    err = play_url(base, "%s/stream/%d.gif" % (here, BIG))
    check(REFUSED not in err and "GIF" in err, "and the %d MB download: %r" % (BIG // MB, err))
    time.sleep(3)
    h = heap(base)
    check(h["psram_free"] > free0 - 400 * 1024, "nothing kept: %d free" % h["psram_free"])
    d = harness.status(base)
    check(d["reliability"]["counters"] == resets0, "no restart to the end")
    server.shutdown()
    print("memory smoke: %d failures" % api_smoke.failures)
    return 1 if api_smoke.failures else 0


if __name__ == "__main__":
    sys.exit(main())
