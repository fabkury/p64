#!/usr/bin/env python3
r"""A fake p64 for the browser lane: serves the web UI's own files (the ones the firmware
embeds, from components/p64_web/ui) and enough of /api/v1 from a scripted state for the
pages to run in a real browser with no device.

    python tests\ui\fake_device.py [port]      # then open http://127.0.0.1:<port>/

The tests (run_ui.py) start it in a thread and script it: per-route delays and failures,
a status document they can edit, and a log of every request with how many of each were
in flight at once. That is the point of a fake: a real device cannot be made slow, busy
or absent on command, and cannot count what a page sent it.

There is no WebSocket here on purpose: the upgrade is answered 404, so the pages fall
back to polling and retry the socket, which is the behaviour under test.
"""

import copy
import json
import os
import struct
import sys
import threading
import time
import urllib.parse
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, "..", "..", "components", "p64_web", "ui")

# The routes of the pages, as components/p64_web/src/ui.cpp serves them.
PAGES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/playsets": ("playsets.html", "text/html; charset=utf-8"),
    "/settings": ("settings.html", "text/html; charset=utf-8"),
    "/update": ("update.html", "text/html; charset=utf-8"),
    "/static/common.css": ("static/common.css", "text/css; charset=utf-8"),
    "/static/theme.js": ("static/theme.js", "application/javascript; charset=utf-8"),
    "/static/app.js": ("static/app.js", "application/javascript; charset=utf-8"),
    "/manifest.json": ("manifest.json", "application/manifest+json"),
    "/static/icon-192.png": ("static/icon-192.png", "image/png"),
    "/static/icon-512.png": ("static/icon-512.png", "image/png"),
    "/favicon.png": ("static/icon-192.png", "image/png"),
}


def png_64():
    """A 64x64 PNG (a gradient), for /api/v1/frame."""
    rows = b"".join(b"\x00" + bytes((x * 4, y * 4, 96) [c] for x in range(64) for c in range(3)) for y in range(64))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 64, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def merge(into, patch):
    for k, v in patch.items():
        if isinstance(v, dict) and isinstance(into.get(k), dict):
            merge(into[k], v)
        else:
            into[k] = v


class Fake:
    """The scripted device. Tests read and write its fields between page actions."""

    def __init__(self):
        with open(os.path.join(HERE, "fixtures", "device.json"), encoding="utf-8") as f:
            self.docs = json.load(f)
        self.lock = threading.Lock()
        self.delay = {}      # "METHOD /path" -> seconds before the answer
        self.fail = {}       # "METHOD /path" -> (http status, error text, code)
        self.like_queued = False  # the like route answers 202 "queued"
        self.away = False    # the device is gone: every connection is dropped unanswered
        self.cardless = False  # set_cardless()
        self.requests = []   # (time, method, path, body)
        self.in_flight = {}  # "METHOD /path" -> requests being served now
        self.most = {}       # "METHOD /path" -> the most at once
        self.frame = png_64()
        self.server = None
        self.thread = None
        self.port = 0

    # -- scripting helpers --
    @property
    def status(self):
        return self.docs["status"]

    def set_cardless(self, on, error="no card answers"):
        """The device booted without a card (ADR 0018): what the documents then say."""
        with self.lock:
            self.cardless = on
            self.status["card"].update({"mounted": not on, "error": error if on else "", "failed": on and "stopped" in error,
                                        "unformatted": on and "FAT" in error})
            self.status.setdefault("storage", {})["mode"] = "cardless" if on else "card"
            self.status["storage"]["window"] = {"files": 31 if on else 0, "bytes": 900000 if on else 0, "capacity_files": 48,
                                                "capacity_bytes": 6291456, "filled": False, "stored": 31 if on else 0, "evictions": 0,
                                                "refused": 0, "renewals": 0, "hits": 0, "misses": 0}
            p = self.docs["playsets"]
            p.update({"store": "device" if on else "card", "max_playsets": 8 if on else 32, "max_channels": 16 if on else 64})
            for b in p["builtins"]:
                if b["name"] == "Local":
                    b.update({"enabled": not on, "reason": "needs a card" if on else ""})

    def count(self, method, path):
        with self.lock:
            return sum(1 for _, m, p, _ in self.requests if m == method and p == path)

    def bodies(self, method, path):
        with self.lock:
            return [b for _, m, p, b in self.requests if m == method and p == path]

    def clear(self):
        with self.lock:
            self.requests.clear()
            self.most.clear()

    # -- lifecycle --
    def start(self, port=0):
        fake = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def _send(self, status, body, ctype="application/json"):
                data = body if isinstance(body, bytes) else json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(data)

            def _ok(self, data, status=200):
                self._send(status, {"ok": True, "data": data})

            def _error(self, status, text, code):
                self._send(status, {"ok": False, "error": text, "code": code})

            def _serve(self, method):
                url = urllib.parse.urlparse(self.path)
                path = url.path
                length = int(self.headers.get("Content-Length") or 0)
                raw = self.rfile.read(length) if length else b""
                key = method + " " + path
                if fake.away:  # as a device that lost power: no answer, the connection dies
                    self.close_connection = True
                    try:
                        self.connection.close()
                    except OSError:
                        pass
                    return None
                with fake.lock:
                    fake.requests.append((time.time(), method, path, raw[:4096]))
                    fake.in_flight[key] = fake.in_flight.get(key, 0) + 1
                    fake.most[key] = max(fake.most.get(key, 0), fake.in_flight[key])
                try:
                    if fake.delay.get(key):
                        time.sleep(fake.delay[key])
                    if key in fake.fail:
                        status, text, code = fake.fail[key]
                        return self._error(status, text, code)
                    return self._route(method, path, url, raw)
                except (BrokenPipeError, ConnectionResetError):
                    return None
                finally:
                    with fake.lock:
                        fake.in_flight[key] -= 1

            def _route(self, method, path, url, raw):
                d = fake.docs
                body = {}
                if raw and self.headers.get("Content-Type", "").startswith("application/json"):
                    try:
                        body = json.loads(raw)
                    except ValueError:
                        return self._error(400, "invalid JSON", "INVALID_JSON")
                if method == "GET" and path in PAGES:
                    name, ctype = PAGES[path]
                    with open(os.path.join(UI, name), "rb") as f:
                        return self._send(200, f.read(), ctype)
                if path == "/api/v1/ws":
                    return self._error(404, "no WebSocket on the fake", "NOT_FOUND")
                card_routes = ("/api/v1/files", "/api/v1/folders", "/api/v1/files/get", "/api/v1/files/mkdir", "/api/v1/files/rename",
                               "/api/v1/files/format")
                if fake.cardless and path in card_routes and not (method == "POST" and path == "/api/v1/files"):
                    return self._error(409, "this needs a microSD card, and the device is running without one", "NO_CARD")
                if method == "GET":
                    simple = {"/api/v1/status": "status", "/api/v1/settings": "settings", "/api/v1/playsets": "playsets",
                              "/api/v1/channels": "channels", "/api/v1/history": "history", "/api/v1/folders": "folders",
                              "/api/v1/update": "update", "/api/v1/fonts": "fonts", "/api/v1/boot_animations": "boot_animations",
                              "/api/v1/auth": "auth", "/api/v1/diag/encoders": "encoders", "/api/v1/files": "files"}
                    if path in simple:
                        with fake.lock:
                            return self._ok(copy.deepcopy(d[simple[path]]))
                    if path == "/api/v1/timezones":
                        return self._send(200, d["timezones"])
                    if path == "/api/v1/frame":
                        return self._send(200, fake.frame, "image/png")
                    if path.startswith("/api/v1/playsets/"):
                        name = urllib.parse.unquote(path.rsplit("/", 1)[1])
                        doc = d.get("playset:" + name)
                        return self._ok(doc) if doc else self._error(404, "no such playset", "NOT_FOUND")
                    if path == "/api/v1/diag/log":
                        return self._send(200, b"I (1) fake: no log\n", "text/plain")
                    return self._error(404, "no such route", "NOT_FOUND")
                if method == "PUT" and path == "/api/v1/settings":
                    with fake.lock:
                        merge(d["settings"], body)
                        display = d["settings"].get("display", {})
                        fake.status["panel"]["brightness"] = display.get("brightness", fake.status["panel"].get("brightness"))
                        return self._ok(copy.deepcopy(d["settings"]))
                if method == "POST" and path == "/api/v1/makapix/like":
                    data = {"post_id": body.get("post_id"), "liked": bool(body.get("like", True))}
                    if fake.like_queued:
                        data["queued"] = True
                        with fake.lock:
                            fake.status["makapix"]["like"] = {"post_id": data["post_id"], "liked": data["liked"], "pending": True, "error": ""}
                        return self._ok(data, 202)
                    return self._ok(data)
                if method == "POST" and path.startswith("/api/v1/action/"):
                    action = path.rsplit("/", 1)[1]
                    with fake.lock:
                        pb = fake.status["playback"]
                        if action == "pause":
                            pb["paused"] = True
                        elif action == "resume":
                            pb["paused"] = False
                        elif action == "next":
                            pb["swaps"] = pb.get("swaps", 0) + 1
                        elif action == "play_playset":
                            pb["playset"]["name"] = body.get("name", "")
                            d["playsets"]["active"] = body.get("name", "")
                    return self._ok({"action": action})
                if method == "POST" and path == "/api/v1/files":
                    data = {"path": urllib.parse.parse_qs(url.query).get("path", [""])[0], "size": len(raw), "kept": not fake.cardless}
                    if fake.cardless:
                        data["playing"] = True
                    return self._ok(data)
                if method in ("POST", "DELETE", "PUT"):
                    return self._ok({})
                return self._error(405, "this route does not take that method", "METHOD_NOT_ALLOWED")

            def do_GET(self):
                self._serve("GET")

            def do_POST(self):
                self._serve("POST")

            def do_PUT(self):
                self._serve("PUT")

            def do_DELETE(self):
                self._serve("DELETE")

        class Quiet(ThreadingHTTPServer):
            def handle_error(self, request, client_address):  # a browser that closed its end
                pass

        self.server = Quiet(("127.0.0.1", port or self.port), Handler)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        return "http://127.0.0.1:%d" % self.port

    def stop(self):
        """Stops the server. To make the device vanish for a page that is open, set `away`
        instead: a stopped server still answers on connections the browser keeps alive."""
        if self.server:
            self.server.shutdown()
            self.server.server_close()
            self.server = None


if __name__ == "__main__":
    f = Fake()
    print("fake p64 at", f.start(int(sys.argv[1]) if len(sys.argv) > 1 else 8064))
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
