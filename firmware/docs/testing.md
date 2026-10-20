# Testing p64

[Developer guide](../README.md) · [Current acceptance status](../../docs/status.md)

All commands run from `firmware/`. Tests returning zero passed their automated
checks; this does not establish visual quality or close instrument-based acceptance.

## Host tests

Install a native gcc/g++ toolchain and Python with Pillow. The runner also needs cJSON
from ESP-IDF and zlib from managed components. Configure the project once to fetch it.
On Windows, with PowerShell 7 and the paths in `tools/env.ps1` adjusted:

```powershell
. ./tools/env.ps1
python -m pip install pillow
idf.py reconfigure
python tests/host/run.py --werror
```

On Linux, source ESP-IDF v5.5.4's `export.sh` first and use the same Python/IDF commands.
`--sanitize` adds ASan/UBSan on Linux/WSL; `--tc "show*"` filters unit cases and
`--junit host-junit.xml` writes a report. The [runner](../tests/host/run.py) tests pure
product logic and compares decoded/rendered image corpora with reference pixels.

## Firmware and resource checks

In the ESP-IDF environment:

```powershell
idf.py build
python tools/check_size.py
```

The size check reads the build image/map and uses `esp_idf_size` from the IDF Python
environment. [budgets.json](../budgets.json) also defines runtime heap and timing floors
checked by device tests. A budget change needs an explicit reason and measurements.

[CI](../../.github/workflows/firmware.yml) builds the public firmware, checks sizes and
runs host tests with sanitizers and `-Werror`. It does not run hardware tests.

## Device tests

Use a development device on your LAN. Read each script's opening instructions before
running it: tests can change settings and playback, upload/delete files, contact online
services, or reboot. Many restore settings on normal completion, but an interrupted
test may not. Use the IP address for timing tests to avoid mDNS lookup delays.

```powershell
$p64Url = 'http://192.168.1.100' # replace with the device IP
python tests/device/api_smoke.py $p64Url --corpus
python tests/device/content_smoke.py $p64Url
python tests/device/ui_smoke.py $p64Url
```

| Test | Prerequisites and effects |
|---|---|
| `api_smoke.py` | Card; changes settings, creates/uploads/deletes test files and plays them. `--corpus` uploads the corpus; `--bench` measures decode performance. |
| `content_smoke.py` | Card artwork; creates/deletes a test playset and exercises playback. |
| `makapix_smoke.py` | Internet and NTP; downloads and plays artwork. `--paired` also exercises account likes and paired playsets. |
| `widgets_smoke.py`, `faces_smoke.py` | Select and inspect widgets/faces; network-backed widgets need their data prerequisites. |
| `intro_smoke.py [--all]` | The intro animations (p101): the list, the off-screen render bench of all 21 against the frame budget, the `boot_animation` setting round trip, one animation (every one with `--all`) played on the panel for a slot with no late flips. |
| `stream_smoke.py` | Sends UDP frames and takes over playback. |
| `panel_mode_smoke.py`, `brightness_smoke.py`, `timing_smoke.py` | Change display/playback settings; timing tests need suitable test artwork. |
| `ops_smoke.py` | Changes night settings and ceiling; does not factory-reset. |
| `imu_smoke.py`, `encoders_smoke.py` | Exercise input settings and diagnostics; encoder tests can watch physical turns/presses. |
| `pin_smoke.py` | Sets/tests a PIN and lockout; **leaves the device without a PIN**. |
| `ota_smoke.py` | Needs `build/p64.bin`; installs it over HTTP, reboots and rolls back. `--no-install` skips installation. |
| `cache_sweep_smoke.py` | Card and NTP; dry-run by default. `--delete` actually removes older cached/downloaded files and indexes. |
| `ws_smoke.py`, `ui_smoke.py` | WebSocket and browser-route checks; inspect scripts for their complete scope. |
| `soak.py` | Changes playset and swap interval for the run; restores them at normal completion. |

For a short soak:

```powershell
python tests/device/soak.py $p64Url --minutes 10
```

The specification calls for separate 12-hour and 24-hour runs, with additional visual,
stress and recovery checks. Use `--minutes 720` or `--minutes 1440` as appropriate;
the script alone does not cover every [acceptance criterion](../../docs/spec/p64-spec.md#18-acceptance-criteria).
Record the firmware/build, configuration, duration and failures alongside results.

## Diagnostics and benchmarks

Use Settings → System or the [API diagnostics](api.md#diagnostics) for heap, task,
display and log information. [cpu_sample.py](../tools/cpu_sample.py) samples task/heap
data. [serial_peek.py](../tools/serial_peek.py) reads a chosen COM port without resetting
the board; use the IDF Python environment with pyserial installed.

The [September decode benchmark](benchmarks.md) records its files and conditions.
Repeat it after decoder/scaler changes; it is not a current performance guarantee.
