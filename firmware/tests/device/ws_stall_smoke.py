#!/usr/bin/env python3
"""A page that stops reading its WebSocket (finding F-22, ADR 0019).

The status push is sent by the one HTTP task. A client whose connection is open but who
no longer reads (a laptop asleep, a phone out of range) fills its window, and the next
push to it blocks that task until the send timeout (5 s). The lead of the audit was that
this costs every broadcast; the code drops such a client from the push list after the
first failed send, so it should cost the server one timeout, once.

This test opens a WebSocket with a small receive buffer and never reads it, and for
90 s asks for the status once a second beside it, while a second, healthy client listens:

- no answer takes longer than one send timeout and a margin (8 s);
- at most two answers are slow (over 3 s): the stall happens once, not at every push;
- the healthy client keeps receiving pushes, the last one within 5 s of the end;
- the device did not restart.

usage: python tests/device/ws_stall_smoke.py http://<ip> [--seconds N]
"""
import base64
import os
import socket
import sys
import threading
import time
import urllib.parse

import api_smoke
import harness
from api_smoke import check, request
from ws_smoke import WebSocket, listen


def stalled_client(base):
    """A WebSocket that is opened and then left alone: nothing is ever read from it."""
    u = urllib.parse.urlparse(base)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)  # before connect: it sizes the window
    s.settimeout(10)
    s.connect((u.hostname, u.port or 80))
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(("GET /api/v1/ws HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (u.hostname, key)).encode())
    head = s.recv(256)
    return s, head.startswith(b"HTTP/1.1 101")


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    seconds = int(sys.argv[sys.argv.index("--seconds") + 1]) if "--seconds" in sys.argv else 90
    d0 = harness.status(base)
    resets0 = dict(d0["reliability"]["counters"])
    dead, upgraded = stalled_client(base)
    check(upgraded, "a WebSocket opened and left unread")
    healthy = WebSocket(base.replace("http://", "ws://") + "/api/v1/ws")
    got = []
    t = threading.Thread(target=listen, args=(healthy, seconds, got), daemon=True)
    t.start()
    times = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        a = time.time()
        try:
            st, _ = request(base, "GET", "/api/v1/status", timeout=30)
            times.append((time.time() - a, st))
        except Exception as e:  # noqa: BLE001
            times.append((time.time() - a, repr(e)))
        time.sleep(max(0.0, 1.0 - (time.time() - a)))
    t.join(timeout=10)
    worst = max(x for x, _ in times)
    slow = [round(x, 1) for x, _ in times if x > 3]
    bad = [s for _, s in times if s != 200]
    check(not bad, "every status request was answered (%d of %d; %s)" % (len(times) - len(bad), len(times), bad[:2]))
    check(worst <= 8, "no answer took longer than one send timeout: worst %.1f s" % worst)
    check(len(slow) <= 2, "the stall happens once, not at every push: %d slow answers %s" % (len(slow), slow))
    docs = [(at, doc) for at, doc in got if "error" not in doc]
    last = max((at for at, _ in docs), default=0)
    check(len(docs) >= seconds / 2 / 3, "the healthy client kept receiving pushes (%d in %d s)" % (len(docs), seconds))
    check(last and (t0 + seconds) - last < 8, "its last push came %.1f s before the end" % ((t0 + seconds) - last if last else -1))
    d = harness.status(base)
    check(d["uptime_s"] >= d0["uptime_s"] and d["reliability"]["counters"] == resets0, "no restart")
    try:
        dead.close()
        healthy.sock.close()
    except Exception:  # noqa: BLE001
        pass
    print("ws stall smoke: %d failures" % api_smoke.failures)
    return 1 if api_smoke.failures else 0


if __name__ == "__main__":
    sys.exit(main())
