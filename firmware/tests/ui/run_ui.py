#!/usr/bin/env python3
r"""The browser lane (docs/testing-strategy/web-ui-and-api.md, UI-B1 to UI-B4): the web
UI's own JavaScript, run in a real browser.

    python tests\ui\run_ui.py                         # the pages against the fake device
    python tests\ui\run_ui.py --device http://<ip>    # the real pages on a real device
    python tests\ui\run_ui.py --only storm,like       # some of the fake-device tests
    python tests\ui\run_ui.py --headed                # watch it

Needs Playwright (`python -m pip install playwright`, then `python -m playwright install
chromium`). Against the fake device (fake_device.py) the tests make the "device" slow,
busy or absent and count what the pages send: a control pressed many times must not pile
requests up, a slow device must not be polled twice at once, a lost WebSocket must be
retried with a back-off, a refusal must be shown, a queued like must end in the right
heart. Against a real device they press the real pages' buttons fast, from two tabs, and
then ask the health oracle (tests/device/harness.py) whether the device is fine.

Exit code 1 on any failure.
"""

import os
import sys
import tempfile
import time

from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "device"))

failures = 0


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1
    return bool(cond)


class Page:
    """A browser page with its script errors collected."""

    def __init__(self, context, url):
        self.errors = []
        self.page = context.new_page()
        self.page.on("pageerror", lambda e: self.errors.append("pageerror: %s" % e))
        self.page.on("console", lambda m: self.errors.append("console: %s" % m.text) if m.type == "error" and
                     "Failed to load resource" not in m.text and "WebSocket" not in m.text else None)
        self.page.goto(url, wait_until="domcontentloaded")

    def toast(self):
        return self.page.evaluate("() => { var t = document.getElementById('p64-toast'); return t && t.className.indexOf('show') >= 0 ? t.textContent : ''; }")

    def wait_toast(self, text, seconds=6):
        deadline = time.time() + seconds
        seen = ""
        while time.time() < deadline:
            seen = self.toast() or seen
            if text.lower() in (seen or "").lower():
                return seen
            time.sleep(0.05)
        return seen

    def click_fast(self, selector, times, every_ms):
        self.page.evaluate("""([sel, n, ms]) => new Promise(function (done) {
            var el = document.querySelector(sel), i = 0;
            var t = setInterval(function () { el.click(); if (++i >= n) { clearInterval(t); done(); } }, ms);
        })""", [selector, times, every_ms])


# ---- against the fake device ---------------------------------------------------------

def t_pages(fake, base, context):
    """Every page loads and runs its scripts with no error, wide and narrow."""
    for path in ("/", "/playsets", "/settings", "/update"):
        for width in (1280, 360):
            p = Page(context, base + path)
            p.page.set_viewport_size({"width": width, "height": 800})
            p.page.wait_for_timeout(1500)
            check(not p.errors, "%s at %d px: no script error%s" % (path, width, (" (" + p.errors[0][:120] + ")") if p.errors else ""))
            over = p.page.evaluate("() => document.documentElement.scrollWidth - window.innerWidth")
            check(over <= 1, "%s at %d px: nothing wider than the screen (%d px over)" % (path, width, over))
            p.page.close()


def t_storm(fake, base, context):
    """A button pressed 40 times in 2 s while the device takes 400 ms per answer."""
    p = Page(context, base + "/")
    p.page.wait_for_timeout(1000)
    for selector, route in (("#next-btn", "/api/v1/action/next"), ("#back-btn", "/api/v1/action/previous"), ("#pause-btn", None)):
        fake.clear()
        for r in ("next", "previous", "pause", "resume"):
            fake.delay["POST /api/v1/action/" + r] = 0.4
        p.click_fast(selector, 40, 50)
        p.page.wait_for_timeout(1200)
        routes = [route] if route else ["/api/v1/action/pause", "/api/v1/action/resume"]
        sent = sum(fake.count("POST", r) for r in routes)
        most = max(fake.most.get("POST " + r, 0) for r in routes)
        check(most <= 1, "storm %s: never two of the same request in flight (most %d)" % (selector, most))
        check(1 <= sent <= 8, "storm %s: 40 presses in 2 s sent %d requests, not 40" % (selector, sent))
    fake.delay.clear()
    check(not p.errors, "storm: no script error%s" % ((" (" + p.errors[0][:120] + ")") if p.errors else ""))
    p.page.close()


def t_poll(fake, base, context):
    """A device slower than the polling interval is asked once at a time."""
    fake.clear()
    fake.delay["GET /api/v1/status"] = 6.0
    p = Page(context, base + "/")
    p.page.wait_for_timeout(15000)
    most = fake.most.get("GET /api/v1/status", 0)
    sent = fake.count("GET", "/api/v1/status")
    check(most <= 1, "poll: with a status that takes 6 s, never two polls at once (most %d, %d sent in 15 s)" % (most, sent))
    fake.delay.clear()
    p.page.close()


def t_reconnect(fake, base, context):
    """A WebSocket that cannot open is retried with a back-off, one retry at a time."""
    fake.clear()
    p = Page(context, base + "/")
    p.page.wait_for_timeout(33000)
    tries = fake.count("GET", "/api/v1/ws")
    # Opened at 0 s, then after 2, 4, 8 and 15 s: five attempts in 33 s. The old code
    # scheduled a retry from both the error and the close event of each failure.
    check(3 <= tries <= 6, "reconnect: %d WebSocket attempts in 33 s (a back-off of 2, 4, 8, 15 s gives 5)" % tries)
    polls = fake.count("GET", "/api/v1/status")
    check(6 <= polls <= 12, "reconnect: meanwhile the page polls every 4 s (%d polls)" % polls)
    p.page.close()


def t_refusals(fake, base, context):
    """What the device refuses is shown; a device that went away is said, and found again."""
    p = Page(context, base + "/")
    p.page.wait_for_timeout(1000)
    fake.fail["POST /api/v1/action/next"] = (503, "the device is busy; try again", "BUSY")
    p.page.click("#next-btn")
    seen = p.wait_toast("busy")
    check("busy" in seen, "refusal: a 503 from the device is shown (%r)" % seen)
    fake.fail["POST /api/v1/action/next"] = (500, "something broke", "INTERNAL")
    p.page.wait_for_timeout(300)
    p.page.click("#next-btn")
    seen = p.wait_toast("something broke")
    check("something broke" in seen, "refusal: a 500's message is shown (%r)" % seen)
    fake.fail.clear()
    fake.away = True
    p.page.wait_for_timeout(300)
    p.page.click("#next-btn")
    seen = p.wait_toast("reached", 12)
    check("reached" in seen or "answer" in seen, "absent: a device that is gone is said (%r)" % seen)
    fake.away = False
    fake.status["playback"]["playset"]["name"] = "Back again"
    p.page.evaluate("() => p64.refresh()")
    p.page.wait_for_timeout(1500)
    name = p.page.inner_text("#now-playing-name")
    check(name == "Back again", "absent: when the device is back the page follows (%r)" % name)
    fake.status["playback"]["playset"]["name"] = "Desk mix"
    check(not p.errors, "refusal: no script error%s" % ((" (" + p.errors[0][:120] + ")") if p.errors else ""))
    p.page.close()


def t_like(fake, base, context):
    """A like the device queues ends in the right heart when the status says how it went."""
    fake.status["makapix"]["state"] = "paired"
    fake.status["makapix"].pop("like", None)
    post = fake.status["playback"]["artwork"]["post_id"]
    p = Page(context, base + "/")
    p.page.wait_for_timeout(1200)
    active = lambda: p.page.evaluate("() => document.getElementById('like-btn').classList.contains('active')")
    fake.like_queued = False
    p.page.click("#like-btn")
    seen = p.wait_toast("Liked")
    check("Liked" in seen and active(), "like: answered at once, the heart is on (%r)" % seen)
    fake.like_queued = True
    p.page.click("#like-btn")  # unlike, queued behind a busy worker
    seen = p.wait_toast("Unlike sent")
    check("Unlike sent" in seen, "like: a queued unlike says it was sent, not that it is done (%r)" % seen)
    fake.status["makapix"]["like"] = {"post_id": post, "liked": False, "pending": False, "error": ""}
    p.page.evaluate("() => p64.refresh()")
    seen = p.wait_toast("Unliked")
    check("Unliked" in seen and not active(), "like: when the status reports it done, the heart is off (%r)" % seen)
    p.page.click("#like-btn")  # like, queued, and it fails
    p.wait_toast("Like sent")
    fake.status["makapix"]["like"] = {"post_id": post, "liked": True, "pending": False, "error": "the server refused"}
    p.page.evaluate("() => p64.refresh()")
    seen = p.wait_toast("refused")
    check("refused" in seen and not active(), "like: a queued like that failed says why and leaves the heart off (%r)" % seen)
    fake.like_queued = False
    check(not p.errors, "like: no script error%s" % ((" (" + p.errors[0][:120] + ")") if p.errors else ""))
    p.page.close()


def t_settings(fake, base, context):
    """Controls send what the API expects; the two removed settings are gone; a file that
    is too large is stopped in the page."""
    p = Page(context, base + "/settings")
    p.page.wait_for_timeout(1500)
    fake.clear()
    p.page.evaluate("""() => { var e = document.getElementById('brightness'); e.value = 123; e.dispatchEvent(new Event('change')); }""")
    p.page.wait_for_timeout(600)
    bodies = fake.bodies("PUT", "/api/v1/settings")
    check(len(bodies) == 1 and b'"brightness":123' in bodies[0].replace(b" ", b""), "settings: the brightness slider sends a number (%s)" % (bodies[:1]))
    fake.clear()
    p.page.evaluate("""() => { var e = document.getElementById('mirror'); e.checked = !e.checked; e.dispatchEvent(new Event('change')); }""")
    p.page.wait_for_timeout(600)
    bodies = fake.bodies("PUT", "/api/v1/settings")
    check(len(bodies) == 1 and (b"true" in bodies[0] or b"false" in bodies[0]) and b'"mirror"' in bodies[0], "settings: the mirror switch sends a boolean (%s)" % (bodies[:1]))
    check(p.page.query_selector("#dl-cap") is None and p.page.query_selector("#card-root") is None,
          "settings: the downloads cap and the root folder, which did nothing, are gone")
    # The file manager: 6 MB is refused in the page, 1 KB goes out.
    fake.clear()
    folder = tempfile.mkdtemp()
    big, small = os.path.join(folder, "big.gif"), os.path.join(folder, "small.gif")
    with open(big, "wb") as f:
        f.write(b"GIF89a" + b"\0" * (6 * 1024 * 1024))
    with open(small, "wb") as f:
        f.write(b"GIF89a" + b"\0" * 1024)
    p.page.set_input_files("#fm-file", big)
    seen = p.wait_toast("Too large")
    check("Too large" in seen and fake.count("POST", "/api/v1/files") == 0, "upload: a 6 MB file is stopped in the page (%r, %d sent)" % (seen, fake.count("POST", "/api/v1/files")))
    p.page.set_input_files("#fm-file", small)
    p.wait_toast("Uploaded")
    check(fake.count("POST", "/api/v1/files") == 1, "upload: a small file goes out once (%d)" % fake.count("POST", "/api/v1/files"))
    os.remove(big)
    os.remove(small)
    os.rmdir(folder)
    check(not p.errors, "settings: no script error%s" % ((" (" + p.errors[0][:120] + ")") if p.errors else ""))
    p.page.close()


def t_cardless(fake, base, context):
    """Without a card (ADR 0018) the pages say what that means and offer nothing that
    would fail after the click (docs/testing-strategy/no-card-mode.md, NC-D13)."""
    fake.set_cardless(True)
    try:
        p = Page(context, base + "/")
        p.page.wait_for_timeout(1500)
        shown = p.page.evaluate("() => getComputedStyle(document.getElementById('banner-card')).display !== 'none'")
        text = p.page.inner_text("#banner-card")
        check(shown and "kept in memory" in text and "unpowered" in text, "cardless home: the banner says what running without a card means")
        check(p.page.evaluate("() => document.getElementById('folder-select').parentNode.style.display") == "none",
              "cardless home: no folder to choose for an upload")
        check("not kept" in p.page.inner_text("#upload-btn"), "cardless home: the upload button says the file is not kept (%r)" % p.page.inner_text("#upload-btn"))
        folder = tempfile.mkdtemp()
        small = os.path.join(folder, "small.gif")
        with open(small, "wb") as f:
            f.write(b"GIF89a" + b"\0" * 1024)
        fake.clear()
        p.page.evaluate("() => document.getElementById('upload-section').classList.remove('collapsed')")
        p.page.set_input_files("#file-input", small)
        p.page.click("#upload-btn")
        seen = p.wait_toast("not kept")
        p.page.wait_for_timeout(500)
        check("not kept" in seen and fake.count("POST", "/api/v1/files") == 1 and fake.count("POST", "/api/v1/action/play") == 0,
              "cardless home: an upload is played once by the device; the page asks for nothing more (%r, %d play requests)" % (
                  seen, fake.count("POST", "/api/v1/action/play")))
        os.remove(small)
        os.rmdir(folder)
        check(not p.errors, "cardless home: no script error%s" % ((" (" + p.errors[0][:120] + ")") if p.errors else ""))
        p.page.close()

        s = Page(context, base + "/settings")
        s.page.wait_for_timeout(1500)
        vis = lambda sel: s.page.evaluate("(q) => getComputedStyle(document.querySelector(q)).display !== 'none'", sel)
        check(not vis("#files-section") and not vis("#card-only"), "cardless settings: the file manager and the format button are not offered")
        note = s.page.evaluate("() => document.getElementById('cardless-note').textContent")
        check(s.page.evaluate("() => document.getElementById('cardless-note').style.display") != "none" and "31 of 48" in note,
              "cardless settings: the note says where artwork is kept, with the window's figures")
        check("without a card" in s.page.evaluate("() => document.getElementById('card-status').textContent"), "cardless settings: the status says so")
        check(not s.errors, "cardless settings: no script error%s" % ((" (" + s.errors[0][:120] + ")") if s.errors else ""))
        s.page.close()

        q = Page(context, base + "/playsets")
        q.page.wait_for_timeout(1500)
        check(q.page.evaluate("() => document.getElementById('storeNote').style.display") != "none", "cardless playsets: the page says these playsets live on the device")
        check(q.page.inner_text("#psCount").endswith("/8"), "cardless playsets: the count is out of 8 (%r)" % q.page.inner_text("#psCount"))
        check(q.page.evaluate("() => document.querySelector('#channelSource option[value=\"local\"]').disabled"), "cardless playsets: a local channel cannot be chosen")
        check(q.page.evaluate("() => document.getElementById('channelMax').textContent") == "16", "cardless playsets: 16 channels at most")
        check(not q.errors, "cardless playsets: no script error%s" % ((" (" + q.errors[0][:120] + ")") if q.errors else ""))
        q.page.close()
    finally:
        fake.set_cardless(False)
    # And with a card none of it shows.
    p = Page(context, base + "/playsets")
    p.page.wait_for_timeout(1200)
    check(p.page.evaluate("() => document.getElementById('storeNote').style.display") == "none" and p.page.inner_text("#psCount").endswith("/32"),
          "with a card: the Playsets page is as before")
    p.page.close()


FAKE_TESTS = [("pages", t_pages), ("storm", t_storm), ("refusals", t_refusals), ("like", t_like), ("settings", t_settings), ("cardless", t_cardless),
              ("poll", t_poll), ("reconnect", t_reconnect)]


# ---- against a real device -----------------------------------------------------------

def real_device(base, browser):
    import harness
    before = harness.Snapshot(base)
    context = browser.new_context()
    print("device %s: firmware %s, uptime %d s" % (base, before.status["firmware"]["version"], before.status["uptime_s"]))
    a = Page(context, base + "/")
    b = Page(context, base + "/")
    a.page.wait_for_timeout(2500)
    swaps0 = harness.status(base)["playback"]["swaps"]
    for page in (a, b):
        page.click_fast("#next-btn", 50, 40)
    a.page.wait_for_timeout(500)
    a.click_fast("#back-btn", 30, 40)
    b.click_fast("#next-btn", 30, 40)
    a.page.wait_for_timeout(3000)
    swaps1 = harness.status(base)["playback"]["swaps"]
    check(swaps1 > swaps0, "real: 160 presses from two tabs moved the show (swaps %d -> %d)" % (swaps0, swaps1))
    s = Page(context, base + "/settings")
    s.page.wait_for_timeout(2500)
    check(not s.errors, "real: the settings page runs with no script error%s" % ((" (" + s.errors[0][:120] + ")") if s.errors else ""))
    for page, name in ((a, "first tab"), (b, "second tab")):
        check(not page.errors, "real: the %s has no script error%s" % (name, (" (" + page.errors[0][:120] + ")") if page.errors else ""))
    for page in (a, b, s):
        page.page.close()
    # The browser's connections go with its context. While pages are open the device's
    # internal heap sits well under its idle floors (measured 2026-11-12: free down to
    # 40 KB and the largest block to 14 KB with four pages, back at once when they
    # close; finding F-32), and the oracle's floors are the idle ones.
    low = harness.status(base)["heap"]
    context.close()
    time.sleep(3)
    print("     heap with the pages still open: free %d, largest block %d" % (low["internal_free"], low["internal_largest"]))
    problems, numbers = harness.oracle(before)
    check(not problems, "real: the health oracle after the storm (%s)" % ("; ".join(problems) if problems else "heap %s" % numbers.get("internal_free")))


def main():
    device = sys.argv[sys.argv.index("--device") + 1].rstrip("/") if "--device" in sys.argv else None
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless="--headed" not in sys.argv)
        if device:
            real_device(device, browser)
        else:
            context = browser.new_context()
            from fake_device import Fake
            fake = Fake()
            base = fake.start()
            print("fake device at %s" % base)
            for name, fn in FAKE_TESTS:
                if only and name not in only:
                    continue
                print("--- %s" % name)
                fake.delay.clear()
                fake.fail.clear()
                fn(fake, base, context)
        browser.close()
    print("ui: %d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
