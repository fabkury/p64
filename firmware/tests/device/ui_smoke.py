#!/usr/bin/env python3
r"""Device smoke test of the embedded web UI (M10): every page and asset is served with
the right content type, the ETag revalidation answers 304, the pages reference only
assets the device serves, and the PWA manifest is valid JSON.

    python tests\device\ui_smoke.py [http://p64.local]

Exit code 1 on any failure.
"""

import json
import re
import sys
import urllib.error
import urllib.request

from api_smoke import check

PAGES = {
    "/": "text/html",
    "/playsets": "text/html",
    "/settings": "text/html",
    "/update": "text/html",
    "/static/common.css": "text/css",
    "/static/theme.js": "application/javascript",
    "/static/app.js": "application/javascript",
    "/manifest.json": "application/manifest+json",
    "/static/icon-192.png": "image/png",
    "/static/icon-512.png": "image/png",
    "/favicon.png": "image/png",
}


def fetch(base, path, headers=None):
    req = urllib.request.Request(base + path)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


def main():
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local")
    bodies = {}
    etag = None
    for path, ctype in PAGES.items():
        st, h, body = fetch(base, path)
        ok = st == 200 and (h.get("Content-Type") or "").startswith(ctype) and len(body) > 100
        check(ok, "%s -> %d %s, %d bytes" % (path, st, h.get("Content-Type"), len(body)))
        bodies[path] = body
        etag = etag or h.get("ETag")
    check(bool(etag), "assets carry an ETag (%s)" % etag)
    st, h, body = fetch(base, "/static/common.css", {"If-None-Match": etag or ""})
    check(st == 304 and len(body) == 0, "If-None-Match answers 304")
    for path in ("/", "/playsets", "/settings", "/update"):
        html = bodies[path].decode("utf-8", "replace")
        refs = set(re.findall(r'(?:href|src)="(/[^"?]+)', html))
        missing = [r for r in refs if r not in PAGES and not r.startswith("/api/")]
        check(not missing, "%s references only served assets%s" % (path, (": missing " + ", ".join(missing)) if missing else ""))
        check("p64.nav(" in html and "/static/app.js" in html, "%s uses the shared app script and navigation" % path)
        check(bodies[path].endswith(b"</html>\n") or bodies[path].endswith(b"</html>"), "%s ends cleanly (no embedded NUL)" % path)
    m = json.loads(bodies["/manifest.json"].decode())
    check(m.get("name") == "p64" and len(m.get("icons", [])) == 2, "manifest is valid")
    check(bodies["/static/icon-192.png"][:8] == b"\x89PNG\r\n\x1a\n", "icon is a PNG")
    from api_smoke import failures
    print("ui smoke: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
