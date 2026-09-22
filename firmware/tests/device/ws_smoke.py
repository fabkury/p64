#!/usr/bin/env python3
"""The WebSocket status push held open, as a browser holds it (spec 11.2, api.md /api/v1/ws).

This is the guard for the M10 crash loop (firmware/docs/PROGRESS.md, 2026-09-19): opening
the web UI crash-looped the device because the push task, whose stack is in PSRAM, built a
status document that read NVS. The HTTP status route builds the same document on the httpd
task, so no other test reached that path.

It opens /api/v1/ws, reads status messages for `--seconds` (default 60), checks that they
keep coming every 2 s or so, that an event (Next) is pushed at once rather than at the next
tick, that the documents parse and carry the status fields, and that the device neither
rebooted nor counted a panic or a watchdog meanwhile. Two clients at once, like two open
tabs, for the second half.

usage: python tests/device/ws_smoke.py http://<ip> [--seconds N]
"""
import base64
import json
import os
import socket
import struct
import sys
import threading
import time
import urllib.parse

from api_smoke import check, request


class WebSocket:
    """The smallest RFC 6455 client this test needs: text frames from the server."""

    def __init__(self, url, timeout=10):
        u = urllib.parse.urlparse(url)
        self.sock = socket.create_connection((u.hostname, u.port or 80), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET {u.path} HTTP/1.1\r\nHost: {u.hostname}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.sock.sendall(req.encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(1)
            if not chunk:
                raise ConnectionError("closed during the handshake")
            head += chunk
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError("handshake refused: %r" % head.split(b"\r\n", 1)[0])

    def _read(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("closed")
            buf += chunk
        return buf

    def recv_text(self):
        """The next complete text message (control frames answered or skipped)."""
        parts = []
        while True:
            b0, b1 = self._read(2)
            opcode, fin = b0 & 0x0F, b0 & 0x80
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if b1 & 0x80 else None
            payload = self._read(n)
            if mask:
                payload = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
            if opcode == 0x8:
                raise ConnectionError("closed by the device")
            if opcode == 0x9:  # ping: answer
                self._send(0xA, payload)
                continue
            if opcode in (0x1, 0x0):
                parts.append(payload)
                if fin:
                    return b"".join(parts).decode()

    def _send(self, opcode, payload):
        mask = os.urandom(4)
        head = bytes([0x80 | opcode])
        n = len(payload)
        head += bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n)
        self.sock.sendall(head + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(payload)))

    def close(self):
        try:
            self._send(0x8, b"")
        except OSError:
            pass
        self.sock.close()


def listen(ws, seconds, out):
    """Collects (arrival time, document) until the time is up or the socket fails."""
    end = time.time() + seconds
    ws.sock.settimeout(5)
    try:
        while time.time() < end:
            text = ws.recv_text()
            out.append((time.time(), json.loads(text)))
    except (OSError, ConnectionError, ValueError) as e:
        out.append((time.time(), {"error": str(e)}))


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    seconds = float(sys.argv[sys.argv.index("--seconds") + 1]) if "--seconds" in sys.argv else 60.0
    ws_url = base.replace("http://", "ws://") + "/api/v1/ws"
    st, j = request(base, "GET", "/api/v1/status")
    d0 = j["data"]
    counters0 = d0["reliability"]["counters"]

    ws = WebSocket(ws_url)
    got = []
    t = threading.Thread(target=listen, args=(ws, seconds / 2, got))
    t.start()
    time.sleep(seconds / 4)
    t_next = time.time()
    request(base, "POST", "/api/v1/action/next")
    t.join()
    errors = [m for _, m in got if "error" in m]
    docs = [(at, m) for at, m in got if "error" not in m]
    check(not errors, "the socket stayed open for %.0f s (%s)" % (seconds / 2, errors[0]["error"] if errors else "no error"))
    check(len(docs) >= seconds / 2 / 2.5, "status pushed at least every 2.5 s (%d in %.0f s)" % (len(docs), seconds / 2))
    check(all(m.get("type") == "status" and "playback" in m.get("data", {}) for _, m in docs),
          "every message is a status document")
    after = [at for at, _ in docs if at > t_next]
    check(bool(after) and after[0] - t_next < 1.5, "an event is pushed at once (%.2f s after Next)" %
          ((after[0] - t_next) if after else -1))

    # Two tabs.
    ws2 = WebSocket(ws_url)
    got1, got2 = [], []
    t1 = threading.Thread(target=listen, args=(ws, seconds / 2, got1))
    t2 = threading.Thread(target=listen, args=(ws2, seconds / 2, got2))
    t1.start()
    t2.start()
    t1.join()
    t2.join()
    for name, g in (("first", got1), ("second", got2)):
        errs = [m for _, m in g if "error" in m]
        check(not errs and len(g) >= seconds / 2 / 2.5, "%s client: %d documents, %s" %
              (name, len(g), errs[0]["error"] if errs else "no error"))
    ws.close()
    ws2.close()

    st, j = request(base, "GET", "/api/v1/status")
    d1 = j["data"]
    check(d1["uptime_s"] >= d0["uptime_s"] + int(seconds) - 2, "no reboot (uptime %d -> %d s)" % (d0["uptime_s"], d1["uptime_s"]))
    c1 = d1["reliability"]["counters"]
    check(c1["panic"] == counters0["panic"] and c1["watchdog"] == counters0["watchdog"], "no panic or watchdog counted")
    from api_smoke import failures
    print("ws smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
