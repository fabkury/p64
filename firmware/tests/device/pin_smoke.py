#!/usr/bin/env python3
r"""Device smoke test of the PIN (M9, spec 10.3): setting it, the route gate, the session
cookie, the script header, wrong-PIN counting with the 30 s lockout, and clearing it.

    python tests\device\pin_smoke.py [http://p64.local]

Leaves the device without a PIN. Exit code 1 on any failure. The lockout check waits
30 s.
"""

import json
import sys
import time
import urllib.error
import urllib.request

from api_smoke import check

PIN = "246810"


def call(base, method, path, body=None, headers=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode()
            try:
                return r.status, json.loads(raw or "{}"), r.headers
            except ValueError:
                return r.status, {"raw": raw[:80]}, r.headers
    except urllib.error.HTTPError as e:
        try:
            j = json.loads(e.read().decode() or "{}")
        except ValueError:
            j = {}
        return e.code, j, e.headers


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    st, j, _ = call(base, "GET", "/api/v1/auth")
    check(st == 200 and not j["data"]["pin_set"], "no PIN to begin with")
    st, j, _ = call(base, "PUT", "/api/v1/auth/pin", {"pin": "12"})
    check(st == 400, "a 2-digit PIN is refused")
    st, j, _ = call(base, "PUT", "/api/v1/auth/pin", {"pin": "12ab"})
    check(st == 400, "a PIN with letters is refused")
    st, j, _ = call(base, "PUT", "/api/v1/auth/pin", {"pin": PIN})
    check(st == 200 and j["data"]["pin_set"], "PIN set")

    st, j, _ = call(base, "GET", "/api/v1/status")
    check(st == 401 and j.get("code") == "UNAUTHORIZED", "status refused without a session (401)")
    st, j, _ = call(base, "GET", "/api/v1/auth")
    check(st == 200 and j["data"]["pin_set"] and not j["data"]["authenticated"], "auth state readable without a session")
    st, _, _ = call(base, "GET", "/")
    check(st == 200, "the UI shell page stays open")
    st, j, _ = call(base, "GET", "/api/v1/status", headers={"X-P64-Pin": PIN})
    check(st == 200, "the PIN header opens a route for scripts")
    st, j, _ = call(base, "GET", "/api/v1/status", headers={"X-P64-Pin": "000000"})
    check(st == 401, "a wrong PIN header is refused")

    st, j, h = call(base, "POST", "/api/v1/auth/login", {"pin": "111111"})
    check(st == 401 and j.get("code") == "WRONG_PIN", "wrong login refused")
    st, j, h = call(base, "POST", "/api/v1/auth/login", {"pin": PIN})
    cookie = (h.get("Set-Cookie") or "").split(";")[0]
    check(st == 200 and j["data"]["authenticated"] and cookie.startswith("p64_session="), "login sets the session cookie")
    st, j, _ = call(base, "GET", "/api/v1/status", headers={"Cookie": cookie})
    check(st == 200, "the cookie opens the API")
    token = cookie.split("=", 1)[1]
    st, j, _ = call(base, "GET", "/api/v1/status", headers={"Authorization": "Bearer " + token})
    check(st == 200, "the bearer token opens the API too")
    st, j, _ = call(base, "PUT", "/api/v1/settings", {"show": {}}, headers={"Cookie": cookie})
    check(st == 200, "a PUT with the cookie works")

    # Five wrong PINs lock authentication for 30 s; the session still works meanwhile.
    for _ in range(5):
        call(base, "POST", "/api/v1/auth/login", {"pin": "999999"})
    st, j, h = call(base, "POST", "/api/v1/auth/login", {"pin": PIN})
    check(st == 429 and (h.get("Retry-After") or "").strip().isdigit(), "locked after 5 failures (429, Retry-After %s)" % h.get("Retry-After"))
    st, j, _ = call(base, "GET", "/api/v1/status", headers={"X-P64-Pin": PIN})
    check(st == 429, "the PIN header is refused during the lockout")
    st, j, _ = call(base, "GET", "/api/v1/status", headers={"Cookie": cookie})
    check(st == 200, "an existing session keeps working during the lockout")
    print("     waiting out the 30 s lockout...")
    time.sleep(31)
    st, j, h = call(base, "POST", "/api/v1/auth/login", {"pin": PIN})
    check(st == 200, "login works again after the lockout")

    st, j, _ = call(base, "PUT", "/api/v1/auth/pin", {"pin": "1357", "current": "0000"}, headers={"Cookie": cookie})
    check(st == 401, "changing the PIN needs the current one")
    st, j, _ = call(base, "PUT", "/api/v1/auth/pin", {"pin": "", "current": PIN}, headers={"Cookie": cookie})
    check(st == 200 and not j["data"]["pin_set"], "PIN cleared with the current one")
    st, j, _ = call(base, "GET", "/api/v1/status")
    check(st == 200, "routes open again without a PIN")
    st, j, _ = call(base, "POST", "/api/v1/auth/logout")
    check(st == 200, "logout answers")
    from api_smoke import failures
    print("pin smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
