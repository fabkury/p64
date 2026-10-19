# The release build

What a published firmware release is made of, how one is cut, and what the image
carries that only development uses (reviewed on 2026-10-19; the decision was to publish
the development image unchanged, for the reasons in section 3).

## 1. What a release contains

A release is a GitHub release of `fabkury/p64` tagged `v<MAJOR.MINOR.PATCH>` whose
assets `tools/release_assets.py` collects from a build:

| Asset | Flash offset | Role |
|---|---|---|
| `p64-firmware.bin` | `0x60000` | the application image; what the device's updater downloads (`CONFIG_P64_OTA_ASSET`) and what a USB flash writes into `ota_0` |
| `p64-firmware.bin.sha256` | | `"<hex>  p64-firmware.bin"`; the updater verifies it ([api.md](api.md#updates)) |
| `bootloader.bin` | `0x0` | second-stage bootloader |
| `partition-table.bin` | `0x8000` | `partitions.csv` compiled |
| `ota_data_initial.bin` | `0x19000` | OTA selector pointing at `ota_0` |
| `flash_args` | | the esptool options line and one `<offset> <asset>` line per file above, in offset order |
| `SHA256SUMS` | | every file's digest in `sha256sum -c` format |

The first two are the OTA contract: renaming or omitting them breaks updates on every
device. The others serve the USB paths, [docs/flash-firmware.md](../../docs/flash-firmware.md)
(esptool) and [docs/web-flasher](../../docs/web-flasher/README.md) (the browser).

The image is **the public build**: the firmware without the private area (ADR 0012) and
without `sdkconfig.secrets`, exactly what CI builds on every push. The private build
carries the version suffix `+private` and is never published; a private device installs
a public release only knowingly (`private_build` in the update document).

## 2. Cutting a release

1. Set `PROJECT_VER` in `firmware/CMakeLists.txt` to the version (no `-dev` suffix) and
   commit. The version stays at the released value afterwards until the next bump; a
   build between releases that must be told apart gets a suffix (`0.3.0-dev`), which
   can never be tagged.
2. Make sure CI is green on that commit (`firmware` workflow: host tests, build, size
   budgets).
3. Tag and push:

   ```sh
   git tag v0.2.0
   git push origin v0.2.0
   ```

   The [release workflow](../../.github/workflows/release.yml) refuses a tag that does
   not equal `v` + `PROJECT_VER`, builds the public firmware in the `espressif/idf:v5.5.4`
   container, checks `budgets.json`, collects the assets and publishes the release with
   generated notes under a fixed header (the flashing links). A tag with a suffix
   (`v0.3.0-rc1`) becomes a pre-release: the updater ignores it (it asks for the latest
   release) and the web flasher labels it and does not preselect it.
4. The release workflow then calls the [pages workflow](../../.github/workflows/pages.yml),
   which rebuilds the web flasher site with the new release's files.
5. Verify: a device on the previous release shows the update on its Update page and
   installs it (`tests/device/ota_smoke.py` ends with a rollback); the web flasher
   lists the release; `docs/status.md` records the release.

A release built locally for a check (`.\tools\idf.ps1 -B build_public -DP64_NO_PRIVATE=ON
build`, then `python tools\release_assets.py --build-dir build_public`) produces the
same asset set in `build_public/release/`; `gh release create v0.2.0 build_public/release/*`
would publish it by hand, but the tag path is the documented one so that every
published image comes from CI and the tagged commit.

## 3. Development resources in the image

Everything below is in every build, the published one included. The question asked on
2026-10-19 was whether any of it should leave the release for performance; the answer
was no, because none has a measurable runtime cost, and the image users get is then the
exact image the device tests and soaks run against. The one development aid with a
measurable cost, heap attribution by task (about 4 KB of internal RAM), has been off
since the review of 2026-09-22 and is switched on only in a diagnosis build
(`CONFIG_HEAP_TASK_TRACKING=y`).

| Resource | Where | Cost in the running device | Why it stays |
|---|---|---|---|
| Development Wi-Fi seed | `P64_DEV_WIFI_SSID/PASSWORD` (`main/Kconfig.projbuild`), read in `p64_net/src/wifi.cpp` | one string comparison at boot; the strings are empty in CI builds (they only come from the git-ignored `sdkconfig.secrets`) | nothing to remove: a release build never has values |
| Diagnostics routes: `diag/log`, `diag/memory`, `diag/imu`, `diag/encoders`, `diag/coredump*`, `diag/cache_sweep` | `p64_web/src/api.cpp` | flash only; they do work only when called | product features (spec 15.5: the web UI's diagnostics page, the core dump summary, the sweep) |
| Test-only routes: `diag/bench` (decode benchmark), `diag/dma` (panel GDMA priority), `diag/encoders/turn` (drive the knob) | `p64_web/src/api.cpp` | flash only (a few KB); `bench` costs CPU only while a request runs | used by the device tests; behind the PIN like every route when one is set; `diag/dma` can stall the panel at priority 0 (the GDMA lesson), which a hands-on tester accepts |
| Log ring in PSRAM | `p64_system/src/log_ring.cpp` | one copy of each log line into PSRAM | the diagnostics page's log without a cable (spec 15.5) |
| INFO-level logging to the USB console | `CONFIG_LOG_DEFAULT_LEVEL=3` | formatting per line; the USB Serial/JTAG driver drops output when no host reads it | WARN would empty the diagnostics log; the lines are the evidence of every bench session |
| Per-task run time and the task list | `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`, `CONFIG_FREERTOS_USE_TRACE_FACILITY` | measured as nothing (review of 2026-09-22) | `diag/memory`'s task table and `tools/cpu_sample.py`; the core-0 budget is checked with it |
| Assertions | `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE` | the checks themselves, no measurable share | an assert text in the core dump summary is worth more than its bytes |
| Core dump to flash | `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` | a 256 KB partition; nothing at run time until a crash | spec 15 (reliability): the crash summary on the diagnostics page |
| Encoder polling on a p64a | `p64_inputs/src/encoders.cpp` | an I2C probe every 5 s when no board answers (2.7 % of core 0 when two boards are present, `budgets.json`) | one firmware for both variants (decided 2026-09-22) |
| The embedded web UI, fonts, clock-face art | `p64_web/ui`, `assets/` | flash only (the image is 2.46 MB of an 8 MB slot) | the product |

Not in the image at all: `tests/`, `tools/`, the hardware test bed
(`reference/hardware-tests/`, with its own `/debug` endpoints), the mock-ups and the
asset generators.

Already trimmed for every build, by earlier measurements: the PSRAM memory test at boot
(0.8 s), the image hash at power-on (0.6 s), heap task tracking (4 KB), the Wi-Fi
driver's IRAM copies and the FreeRTOS kernel in flash (26 KB of internal RAM, review of
2026-09-22). The budgets in `budgets.json` are what a change is measured against; a
future trim is justified by a measurement that moves one of them, not by the label
"development".
