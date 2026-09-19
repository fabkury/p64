#!/usr/bin/env python3
r"""Device test of the updater (M9, spec 15.2): a GitHub release check, then a full
install of this PC's build over HTTP into the other slot (SHA256 verified), a reboot
into it, its confirmation, and a rollback to the original slot.

    python tests\device\ota_smoke.py [http://p64.local] [--no-install]

Needs build\p64.bin (the image just flashed, so the device ends up running the same
firmware from either slot). Takes about three minutes because of two reboots. Exit
code 1 on any failure.
"""

import hashlib
import http.server
import os
import socket
import sys
import threading
import time
import urllib.parse

from api_smoke import check, request

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.abspath(os.path.join(HERE, "..", "..", "build", "p64.bin"))


def status(base):
    st, j = request(base, "GET", "/api/v1/status")
    assert st == 200, j
    return j["data"]


def update(base):
    st, j = request(base, "GET", "/api/v1/update")
    assert st == 200, j
    return j["data"]


def wait_state(base, states, seconds):
    deadline = time.time() + seconds
    u = update(base)
    while time.time() < deadline and u["state"] not in states:
        time.sleep(2)
        u = update(base)
    return u


def wait_up(base, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            return status(base)
        except Exception:
            time.sleep(3)
    return None


def local_ip_towards(host):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((host, 80))
    ip = s.getsockname()[0]
    s.close()
    return ip


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    no_install = "--no-install" in sys.argv
    u = update(base)
    check(u["state"] in ("idle", "up_to_date", "available", "error"), "updater idle: %s" % u["state"])
    check(u["current_version"] == status(base)["firmware"]["version"], "current version %s" % u["current_version"])
    st, j = request(base, "POST", "/api/v1/update/check")
    check(st == 200, "check queued")
    u = wait_state(base, ("up_to_date", "available", "error"), 40)
    check(u["state"] in ("up_to_date", "available", "error"), "check finished: %s%s" % (u["state"], (" (" + u["error"] + ")") if u["error"] else ""))
    st, j = request(base, "POST", "/api/v1/update/install", {"url": "http://x/y.bin"})
    check(st == 400, "an install from a URL without its sha256 is refused")
    if no_install:
        from api_smoke import failures
        print("ota smoke (check only): %d failures" % failures)
        return 1 if failures else 0

    if not os.path.exists(IMAGE):
        check(False, "build/p64.bin present")
        return 1
    digest = hashlib.sha256(open(IMAGE, "rb").read()).hexdigest()
    before = status(base)["reliability"]["image"]["partition"]
    host = urllib.parse.urlparse(base).hostname
    ip = local_ip_towards(socket.gethostbyname(host))
    os.chdir(os.path.dirname(IMAGE))
    server = http.server.ThreadingHTTPServer((ip, 0), Quiet)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = "http://%s:%d/p64.bin" % (ip, port)
    print("     serving %s (%d bytes) at %s" % (IMAGE, os.path.getsize(IMAGE), url))
    st, j = request(base, "POST", "/api/v1/update/install", {"url": url, "sha256": digest})
    check(st == 200, "install queued from the local URL")
    t0 = time.time()
    u = wait_state(base, ("ready_to_reboot", "error"), 240)
    check(u["state"] == "ready_to_reboot", "install finished in %.0f s: %s%s" % (time.time() - t0, u["state"], (" (" + u["error"] + ")") if u["error"] else ""))
    server.shutdown()
    if u["state"] != "ready_to_reboot":
        return 1
    request(base, "POST", "/api/v1/action/reboot")
    time.sleep(12)
    d = wait_up(base, 90)
    check(d is not None, "device back after the reboot")
    if d is None:
        return 1
    after = d["reliability"]["image"]["partition"]
    check(after != before, "running from the other slot now (%s -> %s)" % (before, after))
    check(d["reliability"]["image"]["pending_verify"], "new image awaits confirmation")
    print("     waiting for the 30 s confirmation...")
    time.sleep(35)
    d = status(base)
    check(not d["reliability"]["image"]["pending_verify"], "new image confirmed")
    u = update(base)
    check(u["can_rollback"] and u["rollback_partition"] == before, "rollback to %s offered (%s)" % (before, u["rollback_version"]))
    st, j = request(base, "POST", "/api/v1/update/rollback")
    check(st == 200, "rollback accepted")
    time.sleep(12)
    d = wait_up(base, 90)
    check(d is not None and d["reliability"]["image"]["partition"] == before, "back on %s after the rollback" % before)
    from api_smoke import failures
    print("ota smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
