#!/usr/bin/env python3
r"""Who holds the internal RAM while pages are loaded (finding F-32): a diagnosis tool.

Needs a diagnosis image: the test build with heap tracing, which no other build has.

    tools\idf.ps1 -B build_diag -DP64_NO_PRIVATE=ON -DP64_TEST=ON -DSDKCONFIG=build_diag/sdkconfig reconfigure
    (in build_diag\sdkconfig: CONFIG_HEAP_TRACING_STANDALONE=y, CONFIG_HEAP_TRACING=y,
     CONFIG_HEAP_TRACING_STACK_DEPTH=6, and CONFIG_HEAP_TRACING_OFF not set)
    tools\idf.ps1 -B build_diag build ; tools\idf.ps1 -B build_diag -p COM13 flash

    python tools\heap_trace_report.py http://<ip> COM13 [--pages 4] [--seconds 40] [--tls] [--top 25] [--arm 26000] [--upload BYTES]

It starts the trace (`POST /api/v1/test {"heap_trace": "start"}`), opens N pages in
Chromium (Playwright) and loads one of them again every 2 s, with `--tls` asks for an
update check every 8 s, and has the device print what is still allocated in internal RAM
three times: in the middle of the loads, at their end, and a minute after the pages closed
(what is left then is what fragments the heap). `--arm N` adds the dump that matters
most: the device prints it by itself the moment its internal free memory falls below N. Each dump is read from the console,
grouped by the code that allocated it (the callers resolved with addr2line against
build_diag/p64.elf) and printed, largest first.

The trace sees only what was allocated after its start, and holds 6000 records.
"""

import collections
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.request

import serial
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "..", "build_diag", "p64.elf")
ADDR2LINE = "xtensa-esp32s3-elf-addr2line"
SKIP = ("heap_caps", "multi_heap", "/heap/", "heap_trace", "newlib/heap.c", "wrap_malloc", "libstdc++", "new_op", "esp_libc")


def arg(name, default):
    return type(default)(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def post(base, path, body=None):
    data = json.dumps(body).encode() if body is not None else b""
    req = urllib.request.Request(base + path, data=data, method="POST", headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


class Console:
    def __init__(self, port):
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, 115200, 0.2
        self.ser.dtr = self.ser.rts = False  # do not reset the board
        self.ser.open()
        self.text, self.stop = "", False
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while not self.stop:
            try:
                self.text += self.ser.read(65536).decode("utf-8", "replace")
            except Exception:  # noqa: BLE001
                time.sleep(0.2)

    def dump(self, base, label):
        """Asks for a dump and waits for its end."""
        mark = len(self.text)
        post(base, "/api/v1/test", {"heap_trace": "dump:" + label.replace(" ", "_")})
        deadline = time.time() + 60
        while "HEAP TRACE END" not in self.text[mark:] and time.time() < deadline:
            time.sleep(0.2)

    def dumps(self):
        """Every dump the console showed, the armed one included: (label, heap, records)."""
        out = []
        for m in re.finditer(r"HEAP TRACE BEGIN (\S+) free (\d+) largest (\d+)(.*?)HEAP TRACE END", self.text, re.S):
            recs = re.findall(r"(\d+) bytes \(@ 0x[0-9a-f]+, Internal\) allocated CPU \d+ ccount 0x[0-9a-f]+ caller ((?:0x[0-9a-f]+:?)+)", m.group(4))
            out.append((m.group(1).replace("_", " "), (int(m.group(2)), int(m.group(3))), [(int(b), c.strip(":").split(":")) for b, c in recs]))
        return out


_names = {}


def resolve(addrs):
    todo = [a for a in addrs if a not in _names]
    if todo:
        try:
            out = subprocess.run([ADDR2LINE, "-pfiaC", "-e", ELF] + todo, capture_output=True, text=True, timeout=120).stdout
            for line in out.splitlines():
                m = re.match(r"(0x[0-9a-f]+): (.*?) at (.*)", line)
                if m:
                    path = m.group(3).replace("\\", "/")
                    short = "/".join(path.split("/")[-2:])
                    _names["0x" + m.group(1)[2:].lstrip("0").rjust(8, "0")] = (m.group(2), short, path)
        except (OSError, subprocess.SubprocessError) as e:
            print("addr2line: %r (is the ESP-IDF toolchain on PATH?)" % e)
    return [_names.get(a, (a, "?", "?")) for a in addrs]


def report(label, heap, recs, top):
    print("\n== %s: internal free %d, largest block %d; %d blocks, %d bytes held since the trace began" % (
        label, heap[0], heap[1], len(recs), sum(b for b, _ in recs)))
    resolve(sorted({a for _, chain in recs for a in chain}))
    groups = collections.defaultdict(lambda: [0, 0, None])
    for size, chain in recs:
        frames = [f for f in resolve(chain) if not any(s in f[2] or s in f[0] for s in SKIP)]
        key = " < ".join("%s (%s)" % (f[0], f[1]) for f in frames[:3]) or "?"
        g = groups[key]
        g[0] += size
        g[1] += 1
    for key, (size, n, _) in sorted(groups.items(), key=lambda kv: -kv[1][0])[:top]:
        print("  %6d bytes in %4d blocks  %s" % (size, n, key))


def main():
    base = next(a for a in sys.argv[1:] if a.startswith("http")).rstrip("/")
    port = next(a for a in sys.argv[1:] if a.upper().startswith("COM") or a.startswith("/dev/"))
    pages, seconds, top, tls = arg("--pages", 4), arg("--seconds", 40), arg("--top", 25), "--tls" in sys.argv
    arm = arg("--arm", 0)
    upload = arg("--upload", 0)
    con = Console(port)
    post(base, "/api/v1/test", {"heap_trace": "start"})
    if arm:
        post(base, "/api/v1/test", {"heap_trace": "arm:%d" % arm})
    with sync_playwright() as pw:
        browser = pw.chromium.launch()
        ctx = browser.new_context()
        opened = []
        for i in range(pages):
            p = ctx.new_page()
            p.goto(base + ("/", "/settings", "/playsets", "/")[i % 4], wait_until="domcontentloaded")
            opened.append(p)
        t0, i, last_tls, mid = time.time(), 0, 0.0, False
        if upload:
            # Filler sent to the device as an upload: megabytes received (refused at the
            # end as no picture; nothing is written).
            def send():
                req = urllib.request.Request(base + "/api/v1/files?path=animations/heap-trace.gif", data=bytes(upload),
                                             method="POST", headers={"Content-Type": "application/octet-stream"})
                try:
                    urllib.request.urlopen(req, timeout=120).read()
                except Exception:  # noqa: BLE001
                    pass
            threading.Thread(target=send, daemon=True).start()
        while time.time() - t0 < seconds:
            i += 1
            if tls and time.time() - last_tls > 8:
                last_tls = time.time()
                threading.Thread(target=lambda: post(base, "/api/v1/update/check"), daemon=True).start()
            try:
                if opened:
                    opened[i % len(opened)].reload(wait_until="domcontentloaded", timeout=15000)
            except Exception:  # noqa: BLE001
                pass
            if not mid and time.time() - t0 > seconds / 2:
                mid = True
                con.dump(base, "in the middle of the loads")
            time.sleep(2)
        con.dump(base, "pages open, loads over")
        browser.close()
    time.sleep(60)
    con.dump(base, "60 s after the pages closed")
    post(base, "/api/v1/test", {"heap_trace": "stop"})
    con.stop = True
    time.sleep(1)
    for label, heap, recs in con.dumps():
        report(label, heap, recs, top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
