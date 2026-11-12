#!/usr/bin/env python3
r"""Shared pieces of the device suite (docs/testing-strategy/infrastructure.md): the
strict status read, the settings fixture, the health oracle and the serial capture.

The health oracle is the point: a test that exercised a feature and left the device
subtly broken must fail, whatever the feature answered. Take a `Snapshot` before a test
and call `oracle()` after it; it returns the list of what is wrong (empty when the
device is fine). `run_suite.py` does that around every test; a script can do it itself.
"""

import contextlib
import json
import os
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

# Lines of the device log that are never acceptable after a test, whatever it did.
BAD_LOG = ("queue full", "dropped", "never came back", "Guru Meditation", "abort() was called",
           "stack overflow", "Task watchdog got triggered", "panel DMA stalled")
PANIC_MARKS = ("Guru Meditation", "abort() was called", "Backtrace:", "Rebooting...", "stack overflow",
               "Task watchdog got triggered")


def budgets():
    with open(os.path.join(HERE, "..", "..", "budgets.json")) as f:
        return json.load(f)


def http(base, method, path, body=None, timeout=20):
    """One request; returns (status, raw bytes). Transport failures raise."""
    data = body
    headers = {}
    if body is not None and not isinstance(body, (bytes, bytearray)):
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(base + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def strict_json(raw):
    """Parses bytes as strict UTF-8 JSON (finding F-03: a status document with raw bytes)."""
    return json.loads(raw.decode("utf-8", "strict"))


def status(base, timeout=20):
    st, raw = http(base, "GET", "/api/v1/status", timeout=timeout)
    if st != 200:
        raise RuntimeError("status answered %d" % st)
    return strict_json(raw)["data"]


def settings(base):
    st, raw = http(base, "GET", "/api/v1/settings")
    if st != 200:
        raise RuntimeError("settings answered %d" % st)
    return strict_json(raw)["data"]


def wait_for_device(base, seconds):
    """Waits until the device answers its status (after a reboot); None when it does not."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            return status(base, timeout=5)
        except Exception:  # noqa: BLE001
            time.sleep(1)
    return None


@contextlib.contextmanager
def settings_restored(base):
    """The whole settings document is put back when the block ends, whatever happened in
    it (finding T-11: a probe left brightness at 1)."""
    original = settings(base)
    try:
        yield original
    finally:
        try:
            if settings(base) != original:
                http(base, "PUT", "/api/v1/settings", original)
        except Exception:  # noqa: BLE001  the oracle reports a device that does not answer
            pass


def log_lines(base, since_ms):
    """The device log's lines stamped at or after `since_ms` (uptime), level W or E."""
    st, raw = http(base, "GET", "/api/v1/diag/log?bytes=65536", timeout=30)
    if st != 200:
        return []
    out = []
    for line in raw.decode("utf-8", "replace").splitlines():
        if len(line) < 4 or line[0] not in "WE" or line[1:3] != " (":
            continue
        try:
            stamp = int(line[3:line.index(")")])
        except ValueError:
            continue
        if stamp >= since_ms:
            out.append(line)
    return out


class Snapshot:
    """What the oracle compares the device with afterwards."""

    def __init__(self, base):
        self.base = base
        self.status = status(base)
        self.settings = settings(base)
        self.taken = time.time()

    @property
    def uptime_ms(self):
        return self.status["uptime_s"] * 1000


def facts(d):
    """What a test may require of the device, read from its status document."""
    inputs = d.get("inputs", {}) if isinstance(d.get("inputs"), dict) else {}
    return {
        "card": bool(d.get("card", {}).get("mounted")),
        "online": bool(d.get("network", {}).get("connected")),
        "ntp": bool(d.get("time", {}).get("synced")),
        "paired": d.get("makapix", {}).get("state") == "paired",
        "hooks": bool(d.get("test_hooks")),
        "encoders": bool(inputs.get("encoders_present")),
        "version": d.get("firmware", {}).get("version", "?"),
    }


def oracle(before, reboot_expected=False, settings_may_change=False, log_allow=(), settle_s=20, panics_expected=0):
    """Asks the device the questions that every test must leave answered well. Returns a
    list of failures (strings); the second value is a dict of numbers for the report."""
    base = before.base
    problems = []
    numbers = {}

    # 1. Does it answer, in time, with a valid document?
    t0 = time.time()
    try:
        now = status(base, timeout=15)
    except Exception as e:  # noqa: BLE001
        now = wait_for_device(base, 60)
        if now is None:
            return ["the device does not answer its status (%r)" % e], numbers
        problems.append("the status request failed once after the test (%r)" % e)
    numbers["status_ms"] = round((time.time() - t0) * 1000)
    if numbers["status_ms"] > 3000:
        problems.append("the status took %d ms" % numbers["status_ms"])

    # 2. Did it stay up?
    r0, r1 = before.status["reliability"]["counters"], now["reliability"]["counters"]
    rebooted = now["uptime_s"] < before.status["uptime_s"]
    if rebooted and not reboot_expected:
        problems.append("the device rebooted (uptime %d -> %d s, reset reason %s)" %
                        (before.status["uptime_s"], now["uptime_s"], now["reliability"].get("reset_reason")))
    for cause in ("panic", "watchdog", "brownout"):
        expected = panics_expected if cause == "panic" else 0
        if r1.get(cause, 0) != r0.get(cause, 0) + expected:
            problems.append("the %s counter moved (%d -> %d)" % (cause, r0.get(cause, 0), r1.get(cause, 0)))

    # 3. Did the panel keep its lock?
    p0, p1 = before.status["panel"], now["panel"]
    if p1.get("stalled"):
        problems.append("the panel's DMA is stalled")
    if not rebooted:
        if p1.get("timeouts", 0) != p0.get("timeouts", 0):
            problems.append("panel timeouts %d -> %d" % (p0.get("timeouts", 0), p1.get("timeouts", 0)))
        late = p1.get("late_flips", 0) - p0.get("late_flips", 0)
        numbers["late_flips"] = late
        if late > budgets()["late_flips_per_soak_max"]:
            problems.append("%d late flips during the test" % late)

    # 4. Is the show alive? In the Animation show, not paused, with something to play: an
    #    artwork or an interlude up and the next artwork prepared (finding F-01).
    def show_ok(d):
        pb = d["playback"]
        if pb.get("state") != "animation_show" or pb.get("paused") or pb.get("stream_up"):
            return True
        if pb.get("no_artwork") or pb.get("screen"):
            return True  # a status screen: not this check's business
        return bool(pb.get("artwork") or pb.get("widget")) and bool(pb.get("prepared"))

    deadline = time.time() + settle_s
    alive = show_ok(now)
    while not alive and time.time() < deadline:
        time.sleep(1)
        try:
            now = status(base)
            alive = show_ok(now)
        except Exception:  # noqa: BLE001
            pass
    if not alive:
        pb = now["playback"]
        problems.append("the show is not going on: artwork %s, prepared %s, swaps %s" %
                        ((pb.get("artwork") or {}).get("name"), pb.get("prepared"), pb.get("swaps")))
    if before.status["playback"].get("no_artwork") == "" and now["playback"].get("no_artwork"):
        problems.append("the panel now shows 'no artwork: %s'" % now["playback"]["no_artwork"])

    # 5. Did memory come back? (Polled: sockets of the test close within seconds.)
    b = budgets()

    def heap_ok(d):
        h = d["heap"]
        return h["internal_free"] >= b["internal_free_min"] and h["internal_largest"] >= b["internal_largest_block_min"]

    while not heap_ok(now) and time.time() < deadline:
        time.sleep(2)
        try:
            now = status(base)
        except Exception:  # noqa: BLE001
            pass
    numbers.update({"internal_free": now["heap"]["internal_free"], "internal_largest": now["heap"]["internal_largest"],
                    "psram_free": now["heap"]["psram_free"]})
    if not heap_ok(now):
        problems.append("internal heap below its floors: free %d (floor %d), largest block %d (floor %d)" %
                        (now["heap"]["internal_free"], b["internal_free_min"],
                         now["heap"]["internal_largest"], b["internal_largest_block_min"]))

    # 6. Is the log clean?
    if not rebooted:
        bad = [line for line in log_lines(base, before.uptime_ms)
               if any(mark in line for mark in BAD_LOG) and not any(ok in line for ok in log_allow)]
        if bad:
            problems.append("%d bad lines in the device log, first: %s" % (len(bad), bad[0][:120]))

    # 7. Are the settings and the playset what they were?
    try:
        after_settings = settings(base)
        if after_settings != before.settings and not settings_may_change:
            changed = sorted(_diff(before.settings, after_settings))
            problems.append("the test left settings changed: %s" % ", ".join(changed[:8]))
    except Exception as e:  # noqa: BLE001
        problems.append("the settings cannot be read (%r)" % e)
    name0 = before.status["playback"]["playset"]["name"]
    name1 = now["playback"]["playset"]["name"]
    if name0 != name1:
        problems.append("the active playset changed: %s -> %s" % (name0, name1))
    return problems, numbers


def _diff(a, b, prefix=""):
    keys = set()
    for k in set(a) | set(b):
        va, vb = a.get(k), b.get(k)
        if isinstance(va, dict) and isinstance(vb, dict):
            keys |= _diff(va, vb, prefix + k + ".")
        elif va != vb:
            keys.add(prefix + k)
    return keys


def restore(before):
    """Puts the settings and the playset back after a test that left them changed, so the
    next test starts from the same device. Best effort; the oracle already reported it."""
    base = before.base
    try:
        if settings(base) != before.settings:
            http(base, "PUT", "/api/v1/settings", before.settings)
        name = before.status["playback"]["playset"]["name"]
        if name and status(base)["playback"]["playset"]["name"] != name:
            http(base, "POST", "/api/v1/action/play_playset", {"name": name})
            time.sleep(3)
    except Exception:  # noqa: BLE001
        pass


class SerialCapture(threading.Thread):
    """Records the board's USB console for the length of a run, without resetting the
    board (DTR and RTS low, as tools/serial_peek.py). A panic is visible to an HTTP test
    only as a timeout; here its lines are kept and handed to the failing test's report."""

    def __init__(self, port, path):
        super().__init__(daemon=True)
        self.port, self.path = port, path
        self.stop = False
        self.lines = []
        self.error = None
        self._lock = threading.Lock()

    @staticmethod
    def find_port():
        try:
            from serial.tools import list_ports
        except ImportError:
            return None
        for p in list_ports.comports():
            if p.vid == 0x303A and p.pid == 0x1001:  # the ESP32-S3's native USB Serial/JTAG
                return p.device
        return None

    def run(self):
        try:
            import serial
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = self.port, 115200, 0.5
            s.dtr = False
            s.rts = False
            s.open()
        except Exception as e:  # noqa: BLE001
            self.error = repr(e)
            return
        with open(self.path, "w", encoding="utf-8", newline="\n") as f:
            while not self.stop:
                try:
                    raw = s.readline()
                except Exception as e:  # noqa: BLE001  the port vanishes while the board resets
                    self.error = repr(e)
                    time.sleep(1)
                    try:
                        s.close()
                        s.open()
                    except Exception:  # noqa: BLE001
                        pass
                    continue
                if not raw:
                    continue
                text = raw.decode("utf-8", "replace").rstrip()
                f.write(text + "\n")
                f.flush()
                with self._lock:
                    self.lines.append(text)
        s.close()

    def mark(self):
        with self._lock:
            return len(self.lines)

    def panic_since(self, mark):
        """The console lines around a panic seen since `mark`, or [] when there was none."""
        with self._lock:
            chunk = self.lines[mark:]
        hits = [i for i, line in enumerate(chunk) if any(m in line for m in PANIC_MARKS)]
        if not hits:
            return []
        return chunk[max(hits[0] - 5, 0):hits[0] + 30]
