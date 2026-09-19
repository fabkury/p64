#!/usr/bin/env python3
r"""Reads the board's console for a while without resetting it.

    python tools\serial_peek.py COM13 [seconds] [--grep REGEX]

Opens the port with DTR and RTS low (an ordinary terminal pulses them, which resets the
ESP32-S3), prints every line for `seconds` (default 8), then closes. Use the ESP-IDF
Python environment (it has pyserial): tools\env.ps1 puts it first on PATH.
"""
import re
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM13"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 8.0
pattern = None
if "--grep" in sys.argv:
    pattern = re.compile(sys.argv[sys.argv.index("--grep") + 1])

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.5
s.dtr = False
s.rts = False
s.open()
t0 = time.time()
n = 0
try:
    while time.time() - t0 < seconds:
        line = s.readline()
        if not line:
            continue
        text = line.decode("utf-8", "replace").rstrip()
        if pattern and not pattern.search(text):
            continue
        n += 1
        print(text, flush=True)
finally:
    s.close()
print(f"--- {n} lines in {seconds:g} s ---")
