#!/usr/bin/env python3
r"""The device suite: runs the device tests of a lane one after the other against a p64,
with the health oracle between them, and writes one report.

    python tests\device\run_suite.py http://<ip> --lane smoke
    python tests\device\run_suite.py http://<ip> --lane full [--only pin_smoke,ws_smoke]
    python tests\device\run_suite.py http://<ip> --lane soak --minutes 60
    python tests\device\run_suite.py --list

Lanes: `smoke` (about ten minutes, nothing the release image lacks), `full` (every
feature test), `abuse` (docs/testing-strategy/web-ui-and-api.md), `hooks` (the tests that
need the test build's fault injection), `update` (installs this PC's build over the air
and rolls back: two reboots), `soak`.

Every test declares what it needs (the TESTS table below). A test whose requirement the
device does not meet is SKIPPED with the reason, never counted as passed. Around every
test the health oracle (harness.py) asks the device whether it stayed up, the show goes
on, memory came back, the panel kept its lock, the log is clean and the settings are what
they were; a test that passes its own checks and leaves the device wrong fails here.

Exit code: 0 all passed, 1 a failure, 2 no failure but something was skipped (pass
`--allow-skips` to make that 0). The report goes to tests/device/reports/<time>-<lane>/
(git-ignored): summary.md, junit.xml, one log per test, and serial.log when the board is
on USB (`--serial off` to leave the port alone, `--serial COMx` to name it).
"""

import datetime
import os
import subprocess
import sys
import time
import xml.sax.saxutils as sax

import harness

HERE = os.path.dirname(os.path.abspath(__file__))

# name: (script arguments, requirements, lanes, timeout in seconds, oracle allowances)
# Requirements: card ("required" or "any"), internet, ntp, paired, encoders, hooks,
# build (this PC's build/p64.bin exists). Allowances: reboot (the test reboots the
# device on purpose).
TESTS = {
    "api_smoke": ([], {"card": "required"}, ("smoke", "full"), 300, {}),
    "content_smoke": ([], {"card": "required"}, ("smoke", "full"), 300, {}),
    "ui_smoke": ([], {}, ("smoke", "full"), 120, {}),
    "ws_smoke": ([], {}, ("smoke", "full"), 240, {}),
    "ops_smoke": ([], {"ntp": True}, ("smoke", "full"), 240, {}),
    "intro_smoke": ([], {}, ("smoke", "full"), 240, {}),
    "widgets_smoke": ([], {"internet": True, "ntp": True}, ("full",), 600, {}),
    "faces_smoke": ([], {"ntp": True}, ("full",), 600, {}),
    "stream_smoke": ([], {}, ("full",), 300, {}),
    "panel_mode_smoke": ([], {}, ("full",), 300, {}),
    "brightness_smoke": ([], {}, ("full",), 300, {}),
    "timing_smoke": ([], {"card": "required"}, ("full",), 600, {}),
    "imu_smoke": ([], {}, ("full",), 180, {}),
    "encoders_smoke": ([], {"encoders": True}, ("full",), 180, {}),
    "pin_smoke": ([], {}, ("full",), 300, {}),
    "makapix_smoke": (["{paired}"], {"internet": True, "ntp": True}, ("full",), 900, {}),
    "cache_sweep_smoke": ([], {"card": "required", "ntp": True}, ("full",), 600, {}),
    "tls_slot_smoke": ([], {"paired": True, "internet": True}, ("full",), 600, {}),
    "ota_smoke:check": (["--no-install"], {"internet": True, "ntp": True}, ("full",), 300, {}),
    "ota_smoke": ([], {"internet": True, "ntp": True, "build": True}, ("update",), 900, {"reboot": True}),
    "abuse_smoke": ([], {}, ("abuse",), 600, {}),
    "hooks_smoke": ([], {"hooks": True}, ("hooks",), 900, {"reboot": True}),
    "soak": (["--minutes", "{minutes}"], {"internet": True, "ntp": True}, ("soak",), None, {}),
}


def missing(requires, facts):
    """Why the device cannot run a test with these requirements ("" when it can)."""
    if requires.get("card") == "required" and not facts["card"]:
        return "needs a card"
    if requires.get("internet") and not facts["online"]:
        return "needs the network"
    if requires.get("ntp") and not facts["ntp"]:
        return "needs the time (NTP)"
    if requires.get("paired") and not facts["paired"]:
        return "needs a Makapix pairing"
    if requires.get("encoders") and not facts["encoders"]:
        return "needs the p64b encoders"
    if requires.get("hooks") and not facts["hooks"]:
        return "needs the test build (CONFIG_P64_TEST_HOOKS)"
    if requires.get("build") and not os.path.exists(os.path.join(HERE, "..", "..", "build", "p64.bin")):
        return "needs this PC's build (build/p64.bin)"
    return ""


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def junit(path, lane, results):
    cases = []
    for r in results:
        body = ""
        if r["outcome"] == "failed":
            body = "<failure message=%s>%s</failure>" % (sax.quoteattr("; ".join(r["problems"])[:300]),
                                                          sax.escape("\n".join(r["problems"]) + "\n" + r["tail"]))
        elif r["outcome"] == "skipped":
            body = "<skipped message=%s/>" % sax.quoteattr(r["reason"])
        cases.append('  <testcase classname="device.%s" name=%s time="%.1f">%s</testcase>' %
                     (lane, sax.quoteattr(r["name"]), r["seconds"], body))
    failures = sum(1 for r in results if r["outcome"] == "failed")
    skipped = sum(1 for r in results if r["outcome"] == "skipped")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<testsuite name="p64 device %s" tests="%d" failures="%d" skipped="%d">\n%s\n</testsuite>\n'
                % (lane, len(results), failures, skipped, "\n".join(cases)))


def main():
    if "--list" in sys.argv:
        for name, (args, requires, lanes, _, allow) in TESTS.items():
            print("%-22s lanes %-14s needs %s%s" % (name, ",".join(lanes), requires or "nothing",
                                                    "  (reboots)" if allow.get("reboot") else ""))
        return 0
    base = next((a for a in sys.argv[1:] if a.startswith("http")), "http://p64.local").rstrip("/")
    lane = arg("--lane", "smoke")
    only = set(filter(None, (arg("--only", "") or "").split(",")))
    minutes = arg("--minutes", "10")
    names = [n for n, t in TESTS.items() if lane in t[2] and (not only or n in only or n.split(":")[0] in only)]
    if not names:
        print("no test in lane %s%s" % (lane, " matching --only" if only else ""))
        return 1

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = os.path.join(HERE, "reports", "%s-%s" % (stamp, lane))
    os.makedirs(out, exist_ok=True)

    first = harness.status(base)
    # A device that has just booted is still joining the network, syncing the time and
    # reading its indexes: timing checks fail on it for no fault of theirs (the intro
    # bench did, started 4 s after a reboot on 2026-11-11).
    while first["uptime_s"] < 30:
        time.sleep(2)
        first = harness.status(base)
    facts = harness.facts(first)
    print("device %s: firmware %s%s, card %s, time %s, paired %s, uptime %d s" % (
        base, facts["version"], " (test hooks)" if facts["hooks"] else "", facts["card"], facts["ntp"], facts["paired"],
        first["uptime_s"]))

    serial_arg = arg("--serial", "auto")
    capture = None
    if serial_arg != "off":
        port = harness.SerialCapture.find_port() if serial_arg == "auto" else serial_arg
        if port:
            capture = harness.SerialCapture(port, os.path.join(out, "serial.log"))
            capture.start()
            time.sleep(1)
            if capture.error:
                print("serial: %s not opened (%s); going on without the console" % (port, capture.error))
                capture = None
            else:
                print("serial: recording %s" % port)
        else:
            print("serial: no board on USB; going on without the console")

    results = []
    for name in names:
        args, requires, _, timeout, allow = TESTS[name]
        script = name.split(":")[0] + ".py"
        try:
            facts = harness.facts(harness.status(base))
        except Exception as e:  # noqa: BLE001
            results.append({"name": name, "outcome": "failed", "seconds": 0.0, "problems": ["the device does not answer before the test (%r)" % e], "tail": "", "numbers": {}})
            print("FAIL %-22s the device does not answer" % name)
            continue
        why = missing(requires, facts)
        if why:
            results.append({"name": name, "outcome": "skipped", "seconds": 0.0, "reason": why, "problems": [], "tail": "", "numbers": {}})
            print("SKIP %-22s %s" % (name, why))
            continue
        before = harness.Snapshot(base)
        mark = capture.mark() if capture else 0
        # {paired}: the script's own switch for a paired device (it asserts the unpaired
        # behaviour without it).
        fill = {"minutes": minutes, "paired": "--paired" if facts["paired"] else ""}
        cmd = [sys.executable, os.path.join(HERE, script), base] + [a for a in (x.format(**fill) for x in args) if a]
        t0 = time.time()
        limit = timeout if timeout else float(minutes) * 60 + 600
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=limit, cwd=os.path.join(HERE, "..", ".."))
            code, text = p.returncode, (p.stdout or "") + (p.stderr or "")
        except subprocess.TimeoutExpired as e:
            code, text = -1, ((e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")) + "\n(timed out after %d s)" % limit
        seconds = time.time() - t0
        with open(os.path.join(out, name.replace(":", "-") + ".log"), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        problems = []
        if code == harness_skipped():
            reason = next((line[5:] for line in text.splitlines() if line.startswith("SKIP ")), "the test skipped itself")
            results.append({"name": name, "outcome": "skipped", "seconds": seconds, "reason": reason, "problems": [], "tail": "", "numbers": {}})
            print("SKIP %-22s %s" % (name, reason))
            harness.restore(before)
            continue
        if code != 0:
            fails = [line for line in text.splitlines() if line.startswith("FAIL ")]
            problems.append("exit code %d%s" % (code, ": " + "; ".join(fails[:3]) if fails else ""))
        if allow.get("reboot"):
            harness.wait_for_device(base, 90)
        found, numbers = harness.oracle(before, reboot_expected=bool(allow.get("reboot")))
        problems += ["oracle: " + p for p in found]
        panic = capture.panic_since(mark) if capture and not allow.get("reboot") else []
        if panic:
            problems.append("console: a panic or reset during the test")
            text += "\n--- console ---\n" + "\n".join(panic)
        harness.restore(before)
        outcome = "failed" if problems else "passed"
        results.append({"name": name, "outcome": outcome, "seconds": seconds, "problems": problems,
                        "tail": "\n".join(text.splitlines()[-25:]), "numbers": numbers})
        print("%s %-22s %5.0f s  heap %s  %s" % ("ok  " if not problems else "FAIL", name, seconds,
                                                   numbers.get("internal_free", "?"), "; ".join(problems)[:200]))

    if capture:
        capture.stop = True
    last = harness.wait_for_device(base, 30)
    passed = sum(1 for r in results if r["outcome"] == "passed")
    failed = [r for r in results if r["outcome"] == "failed"]
    skipped = [r for r in results if r["outcome"] == "skipped"]
    lines = ["# Device suite: lane %s" % lane, "",
             "%s, firmware %s%s, %s." % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), facts["version"],
                                        " with test hooks" if facts["hooks"] else "", base),
             "", "**%d passed, %d failed, %d skipped** in %.0f minutes." % (passed, len(failed), len(skipped), sum(r["seconds"] for r in results) / 60), ""]
    if last:
        lines += ["Device at the end: uptime %d s, internal heap %d free (largest block %d), PSRAM %d free." % (
            last["uptime_s"], last["heap"]["internal_free"], last["heap"]["internal_largest"], last["heap"]["psram_free"]), ""]
    lines += ["| Test | Result | Seconds | Heap after | Late flips | Notes |", "|---|---|---|---|---|---|"]
    for r in results:
        note = r.get("reason") or "; ".join(r["problems"])
        lines.append("| %s | %s | %.0f | %s | %s | %s |" % (r["name"], r["outcome"], r["seconds"], r["numbers"].get("internal_free", ""),
                                                          r["numbers"].get("late_flips", ""), note.replace("|", "/")[:300]))
    summary = "\n".join(lines) + "\n"
    with open(os.path.join(out, "summary.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write(summary)
    junit(os.path.join(out, "junit.xml"), lane, results)
    print("\n%d passed, %d failed, %d skipped; report in %s" % (passed, len(failed), len(skipped), os.path.relpath(out)))
    if failed:
        return 1
    if skipped and "--allow-skips" not in sys.argv:
        return 2
    return 0


def harness_skipped():
    return 2  # api_smoke.SKIPPED


if __name__ == "__main__":
    sys.exit(main())
