#!/usr/bin/env python3
r"""Device smoke test over the HTTP API (spec 12): run against a p64 on the network.

    python tests\device\api_smoke.py [http://p64.local] [--corpus] [--bench]

Checks status, settings round trip (brightness), the live frame (a valid 64x64 PNG),
the file manager (make a folder, upload files of awkward sizes, read them back byte for
byte, delete), play-this on an uploaded file, and optionally uploads the host corpus and
runs the decode benchmark on each file. Exit code 1 on any failure. Needs Pillow.
"""

import contextlib
import hashlib
import io
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.join(HERE, "..", "host", "corpus")
failures = 0


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1
    return cond


def request(base, method, path, body=None, content_type="application/json", timeout=60, raw=False):
    data = body
    headers = {}
    if body is not None and not isinstance(body, (bytes, bytearray)):
        data = json.dumps(body).encode()
    if data is not None:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(base + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            payload = r.read()
            return r.status, (payload if raw else json.loads(payload))
    except urllib.error.HTTPError as e:
        payload = e.read()
        try:
            return e.code, json.loads(payload)
        except ValueError:
            return e.code, payload


@contextlib.contextmanager
def playset_restored(base):
    """Puts back the playset that was active when the block began, even when the block
    raised midway: a smoke test leaves the device playing what it found."""
    st, j = request(base, "GET", "/api/v1/status")
    original = j["data"]["playback"]["playset"]["name"] if st == 200 else ""
    try:
        yield original
    finally:
        if original:
            st, _ = request(base, "POST", "/api/v1/action/play_playset", {"name": original})
            active = ""
            deadline = time.time() + 15  # activation is asynchronous ("activating": true)
            while st == 200 and time.time() < deadline:
                active = request(base, "GET", "/api/v1/status")[1]["data"]["playback"]["playset"]["name"]
                if active == original:
                    break
                time.sleep(0.25)
            check(st == 200 and active == original, "restored the playset %s (%s active)" % (original, active or "none"))


def budgets():
    """The resource floors in firmware/budgets.json (docs/review-2026-09/)."""
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "budgets.json")) as f:
        return json.load(f)


def check_heap_budget(base, b):
    """Steady-state internal RAM against the budget: free and largest block."""
    st, j = request(base, "GET", "/api/v1/diag/memory")
    h = j.get("data", {}).get("heap", {}).get("internal", {}) if st == 200 else {}
    check(h.get("free", 0) >= b["internal_free_min"],
          "internal heap free %s >= budget %d" % (h.get("free"), b["internal_free_min"]))
    check(h.get("largest_free", 0) >= b["internal_largest_block_min"],
          "internal largest block %s >= budget %d" % (h.get("largest_free"), b["internal_largest_block_min"]))
    print("     heap: free %s, largest %s, minimum since boot %s" % (h.get("free"), h.get("largest_free"), h.get("minimum_free")))


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    with_corpus = "--corpus" in sys.argv
    with_bench = "--bench" in sys.argv
    b = budgets()

    st, j = request(base, "GET", "/api/v1/status")
    check(st == 200 and j.get("ok"), "GET /api/v1/status")
    d = j.get("data", {})
    check(d.get("api_version") == 1, "api_version 1")
    check(d.get("panel", {}).get("refresh_hz", 0) > 100, f"panel refresh {d.get('panel', {}).get('refresh_hz', 0):.1f} Hz")
    check_heap_budget(base, b)
    check(isinstance(d.get("playback", {}).get("playset", {}).get("version"), int), "status carries playback.playset.version")
    check(isinstance(d.get("playsets_version"), int), "status carries playsets_version")

    st, j = request(base, "GET", "/api/v1/settings")
    check(st == 200 and "display" in j.get("data", {}), "GET /api/v1/settings")
    original = j["data"]["display"]["brightness"]
    st, j = request(base, "PUT", "/api/v1/settings", {"display": {"brightness": 77}})
    check(st == 200 and j["data"]["display"]["brightness"] == 77, "PUT settings brightness 77")
    st, j = request(base, "PUT", "/api/v1/settings", {"display": {"brightness": 999}})
    check(st == 200 and j["data"]["display"]["brightness"] == 255, "PUT settings clamps 999 to 255")
    request(base, "PUT", "/api/v1/settings", {"display": {"brightness": original}})

    st, png = request(base, "GET", "/api/v1/frame", raw=True)
    ok = st == 200 and png[:8] == b"\x89PNG\r\n\x1a\n"
    if ok:
        try:
            from PIL import Image
            im = Image.open(io.BytesIO(png))
            ok = im.size == (64, 64)
        except Exception:  # noqa: BLE001
            pass
    check(ok, f"GET /api/v1/frame is a 64x64 PNG ({len(png)} bytes)")

    # File manager round trip with sizes around the stdio and sector boundaries.
    folder = "animations/_smoke"
    st, j = request(base, "POST", f"/api/v1/files/mkdir?path={folder}")
    check(st == 200, "mkdir animations/_smoke")
    gif = open(os.path.join(CORPUS, "gif_anim_32.gif"), "rb").read()
    for size in (100, 511, 512, 4095, 4096, 4097, 8191, 12345):
        # A valid GIF header followed by the real file (so the upload's sniff passes),
        # padded with a pattern to the wanted size.
        body = gif + bytes((i * 7 + size) & 0xFF for i in range(max(0, size - len(gif))))
        body = body[:max(size, len(gif))]
        name = f"{folder}/t{size}.gif"
        st, j = request(base, "POST", f"/api/v1/files?path={name}", body, content_type="application/octet-stream")
        if not check(st == 200 and j.get("ok"), f"upload {name} ({len(body)} bytes): {j.get('error', '')}"):
            continue
        st, back = request(base, "GET", f"/api/v1/files/get?path={name}", raw=True)
        check(st == 200 and back == body, f"read back {name} byte for byte" + ("" if back == body else f" (got {len(back)} bytes, md5 {hashlib.md5(back).hexdigest()[:8]} vs {hashlib.md5(body).hexdigest()[:8]})"))
    st, j = request(base, "GET", f"/api/v1/files?path={folder}")
    check(st == 200 and len(j["data"]["entries"]) >= 8, "list shows the uploads")
    st, j = request(base, "POST", "/api/v1/action/play", {"path": f"{folder}/t100.gif"})
    check(st == 200, "play an uploaded file")
    time.sleep(1)
    st, j = request(base, "GET", "/api/v1/status")
    check(j["data"].get("playback", {}).get("artwork", {}).get("name") == "t100.gif", "status shows the played file")
    for e in request(base, "GET", f"/api/v1/files?path={folder}")[1]["data"]["entries"]:
        request(base, "DELETE", f"/api/v1/files?path={folder}/{e['name']}")
    st, j = request(base, "DELETE", f"/api/v1/files?path={folder}")
    check(st == 200, "delete the folder")

    if with_corpus or with_bench:
        names = sorted(n for n in os.listdir(CORPUS) if not n.startswith("."))
        for n in names:
            body = open(os.path.join(CORPUS, n), "rb").read()
            st, j = request(base, "POST", f"/api/v1/files?path=animations/{n}", body, content_type="application/octet-stream")
            check(st == 200, f"upload corpus {n}: {j.get('error', '')}")
        if with_bench:
            print("\nformat    size     frames  first ms  avg ms  max ms  sustainable fps  file")
            for n in names:
                st, j = request(base, "GET", f"/api/v1/diag/bench?path=animations/{n}&loops=2", timeout=180)
                if st != 200:
                    check(False, f"bench {n}: {j.get('error', '')}")
                    continue
                b = j["data"]
                print(f"{b['format']:<8} {b['width']:>3}x{b['height']:<3}  {b['frames']:>5}  {b['first_frame_ms']:>8.2f}  {b['avg_frame_ms']:>6.2f}  {b['max_frame_ms']:>6.2f}  {b['sustainable_fps']:>15.1f}  {n}")
    print(f"\n{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
