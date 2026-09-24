# Evidence: testing inventory (code audit, 2026-09-22)

What protects the firmware today, file by file, from the code on 2026-09-22. Paths are
relative to `firmware/`. The analysis and proposals are in `testing.md`.

## 1. Host tests

Framework: hand-rolled. `tests/host/main.cpp:46-65` defines `CHECK(cond)` and
`CHECK_EQ(a,b)`, which bump two file-static counters (`g_checks`, `g_failures`, lines
43-44), print `FAIL file:line: expr` to stderr and continue. `run_unit()`
(main.cpp:1118-1145) calls 24 `test_*` functions in sequence in one process and returns
1 if any check failed. No isolation: no fixtures, no per-test setup or teardown, shared
static state (a `static ReadySlot slots` at main.cpp:222), no way to run one test by
name, and the pass output is one line (`unit tests: 1433 checks, 0 failures`) without
test names. `tests/host/run.py` (460 lines) is the driver: it compiles the sources with
the PC's gcc and g++ into `tests/host/build/` (mtime-based incremental, run.py:123-125),
runs `unit`, then runs `dump` (main.cpp:1152-1235) to write every frame of every corpus
file and checks the dumps in Python against Pillow (run.py:358-395), a spec-compliant
APNG compositor written because Pillow's is wrong (run.py:204-300), a Python twin of the
scaler (run.py:157-201) and the browser delay rule (run.py:350-355). Tolerance 0 for
GIF, PNG and BMP, 1 for WebP and APNG (run.py:371). Corpus: 22 Pillow-made files in
`tests/host/corpus/` plus 64 GIFs from the hardware tests (in `tests/host/corpus/gifs/`
since 2026-09-24): 86 files, 1531
frames.

Flags: C++ `g++ -std=c++20 -O2 -Wall -Wextra -D__LINUX__` (run.py:128); vendored C
`gcc -O2 -w -DHAVE_UNISTD_H` (warnings suppressed, run.py:130); link `-static`
(run.py:153). No `-Werror`, no sanitizers, no coverage, no `-g`. A cold build emits two
warnings, both in vendored AnimatedGIF, none in p64 code.

Time (measured on this laptop, 2026-09-22): cold build 100.8 s, almost all libwebp,
libpng and zlib (119 objects); warm unit tests 0.2 s; warm unit plus the full corpus
7.0 s. Needs gcc on the PATH, the system Python with Pillow, `IDF_PATH` for cJSON
(run.py:36) and a prior firmware build for `managed_components/espressif__zlib`
(run.py:137).

Check counts (static `CHECK` sites in main.cpp; loops make the runtime count 1433;
PROGRESS.md recorded 74 at M1 and 1115 at M5):

| Area | Test functions | Sites |
|---|---|---|
| p64_decode (delay rule, sniffing) | 2 | 16 |
| p64_gfx (scaler x2, rotation and gains, blend, 5x7 text, bundled fonts) | 6 | 84 |
| p64_playback (`FrameQueue`, header-only) | 1 | 8 |
| p64_content (playset model, JSON, scheduler x2, history, makapix index) | 6 | 142 |
| p64_widgets (clock_format, analogue, weather_model) | 3 | 60 |
| p64_stream (protocol: raw and DDP headers, assembler, conversion) | 1 | 60 |
| p64_system (night, rtc_codec) | 2 | 30 |
| p64_inputs (taps, orientation) | 2 | 29 |
| p64_ota (version) | 1 | 14 |
| Total | 24 | 445 sites, 1433 at runtime, plus 86 corpus files |

Compiled into the host binary (run.py:40-69): 28 p64 sources plus
`animatedgif/src/AnimatedGIF.cpp`, zlib (10 files), libpng, libwebp (dec, dsp, utils,
demux) and IDF's `cJSON.c`. 26 of the 74 p64 `.cpp` files are host-compiled. Six more
are already IDF-free but not listed in run.py: `p64_gfx/src/png_encode.cpp`,
`p64_content/src/local_index.cpp` (dirent and stat only),
`p64_playback/src/artwork.cpp`, `p64_net/src/tz.cpp`, `main/status_screens.cpp`,
`main/boot_animation.cpp`.

Documentation drift: `docs/architecture.md:44-46` and `:173` say `tools/hosttest/`
builds the host tests and `tools/bench/` the benchmark; neither directory exists (the
tests are `tests/host/`, the benchmark is `GET /api/v1/diag/bench`). The table at
architecture.md:14-33 lists `p64_ops` (there is `p64_ota` plus `main/ops.cpp`), marks
`p64_net` "no" although `tz.cpp` is pure, and omits `p64_ota`'s host-tested
`version.cpp`.

## 2. Seams

Method: an include scan plus call-site counts (ESP_LOG, esp_timer, FreeRTOS tasks,
queues and semaphores, NVS, sockets and httpd, heap_caps, cJSON) per file.

| File | What blocks it | Pure part inside | Grade |
|---|---|---|---|
| `main/show.cpp` (1470 lines) | the FreeRTOS command queue (:221, :1177, :1238), `esp_task_wdt` (:1234-1236), `esp_timer` (:199), `esp_random` (:558, :727), `heap_caps` (:1175), 24 log sites, and direct calls into makapix, wifi, card, stream, web, widgets, settings, state_store and the loader (:19-39). State is file-static globals in an anonymous namespace. | `pick_fresh` (:346), `mark_entry` (:383), `install` (:696), `same_channels` (:687), `enter_animation_show` (:292), `channel_status` and `no_artwork_reason` (:249, :265), the tick and timer gating | Expensive seam: a "state struct + injected clock and RNG + effects interface" split touches the whole file. Highest value: the 2026-09-21 frozen-artwork and Followed-restore bugs live here. |
| `p64_makapix/src/api.cpp` (433) | one `ESP_LOGD` (:429), `esp_app_get_description` (:147), `CONFIG_*` (:98, :378, :383); transport through `net::fetch` (:55) | `entry_from_post` (:100), `fill_page` (:82), the `query_page` body (:219), `download_url` (:377): 83 cJSON calls, the site contract | Cheap seam: a log shim header, a stub `net::fetch`, a version string. |
| `p64_makapix/src/makapix.cpp` (722) | `esp_timer`, mbedtls x509 (`cert_not_after` :146), tasks | `handle_command` (:227) parses the MQTT command JSON | Cheap for `handle_command` if it returns a command struct; expensive for the rest. |
| `p64_makapix/src/mqtt.cpp` (283) | the esp_mqtt client and its event structs (`on_event` :59, fragment reassembly :29-30) | the payload builders `publish_capabilities`, `status`, `state`, `view`, `ack` (:39-266) build cJSON then call `publish()` (:34) | Cheap for the payloads (return the string); medium for the event handler. |
| `p64_makapix/src/fetcher.cpp` (770) | the task loop, queue, `esp_timer` (19 sites), heap | `refresh_step` (:248), `finish_walk` (:190), `next_download` (:328), the "park an offline Followed job" rule (:566) | Expensive seam: policy interleaved with API, cache and task calls. |
| `p64_makapix/src/cache.cpp` (211) | `ESP_LOG`, `heap_caps`, one `vTaskDelay`; POSIX file I/O under `/sdcard` | `sweep_folder` (:169) and `sweep` (:198) are directory walks | Cheap: a root-path parameter plus shims; testable on a temporary directory. |
| `p64_makapix/src/credentials.cpp` | NVS only (38 sites) | none | Cheap but low value. |
| `p64_web/src/*.cpp` (7 files, about 1750 lines) | every handler takes `httpd_req_t*` (api.cpp:242-572, auth.cpp:158-296, content, files, makapix_routes, ws, ui); body parsing (`parse_body` api.cpp:126) and validation inline | `build_status` (api.cpp:147), `check_pin` and the lockout (auth.cpp:113), `hash_pin` (auth.cpp:50, mbedtls sha256), `valid_pin` (:98) | Expensive as written; a p3a-style "(method, path, query, body) to (status, json)" core would be moderate per file, large in total. The auth lockout and session logic is a cheap seam with clock and RNG injection. |
| `p64_system/src/settings.cpp` (466) | NVS confined to `nvs_write_impl` and `nvs_read_impl` (:384-411, wrapped by `on_internal_stack` :413-414), six log sites, `std::mutex` | `Settings::clamp` (:121), `to_json` (:161-258), `apply_json` (:260-380): 84 cJSON calls, every range, enum and clamp rule of spec section 16 | Cheap seam: compile with a stub for the two NVS functions and a log shim. The M4 "values wrapped before clamping" bug lives in this pure code and has no host test. |
| `p64_system/src/state_store.cpp` | NVS only | none | Cheap, low value. |
| `p64_playback/src/player.cpp` (196) | a FreeRTOS task and command queue (`take_command` :70), `esp_timer`, `heap_caps` | the timeline rule in `run()` (:80): due = previous due + delay, the 60 Hz floor, `decoded_late`, the generation announcement | Expensive (medium size): extract a `Timeline` struct with an injected `now()`. |
| `p64_playback/src/renderer.cpp` (170) | `esp_timer` (6 sites), `Display`, `heap_caps` | the schedule in `run()` (:80): target = max(due, previous + delay), re-anchor only beyond one period, the copy lead; the M1 and M5 pacing bugs live here | Expensive but small: a `Schedule` struct would be pure. |
| `p64_net/src/*.cpp` | wifi.cpp: esp_wifi, netif, mdns, NVS, timers; fetch.cpp: esp_http_client and a semaphore; http_server.cpp and setup_portal.cpp: httpd; dns_hijack.cpp: lwIP; clock.cpp: SNTP | `tz.cpp` is pure (not compiled); the Wi-Fi fallback and backoff state (`on_event` :173, timers :150-172) | Genuinely stack-bound except `tz.cpp` (free) and the backoff state (expensive). |
| `p64_ota/src/ota.cpp` (479) | `esp_ota_ops`, `esp_partition`, `esp_https_ota`, mbedtls sha256, a task and a semaphore | the `do_check` release-JSON parsing (:119-138: tag, body, assets, the `.sha256` asset), `hex_to_bin` (:66); the version rule is already in `version.cpp` | Cheap for a `parse_release(json)` split; hardware-bound for install and rollback. |
| `p64_inputs/src/inputs.cpp`, `qmi8658.cpp` | a task, `esp_timer`, the cJSON diagnostics document; the I2C driver | already extracted: `tap.cpp`, `orientation.cpp` | Pure core already; the remainder is hardware-bound. |
| `p64_stream/src/stream.cpp` | lwIP sockets, a task, `esp_timer`, the watchdog | `deliver` (:95), conversion and scaling; the parsers are already in `protocol.cpp` | Pure core already; the socket loop is genuinely bound. |
| `p64_widgets/src/widgets.cpp` (444) | tasks (`sensor_task` :53, `weather_task` :84), `esp_timer` (`local_time_at` :130) | the digital clock, weather and temperature `FrameSource` drawing, `temperature_text` (:146), `draw_trend` (:156) | Cheap: draw from a `tm` plus a model, as `analogue.cpp` already does. |
| `p64_display/src/display.cpp`, `p64_storage/src/card.cpp`, `main/loader.cpp`, `main/ops.cpp`, `main/main.cpp` | GDMA registers, hub75, SDMMC and VFS, task wiring | nothing worth extracting | Genuinely hardware-bound. |

## 3. Device tests

Structure: `tests/device/api_smoke.py` doubles as the shared library: `check()` (:26-31,
a global `failures`) and `request()` (:34-52) are imported by 11 of the other 12 scripts;
`pin_smoke.py:22-40` and `ui_smoke.py:34-42` carry their own HTTP helpers because they
need response headers. Duplicated across files: `status()` is defined nine times
(content:19, makapix:20, widgets:24, stream:25, ops:17, imu:17, ota:28, cache_sweep:22,
soak:23), `settings()` four times (widgets:40, stream:37, ops:23, imu:29), `wait_for()`
four times with three different signatures (content:25, makapix:26, stream:119,
cache_sweep:38), `frame()` twice, and the base URL and flag parsing in every file
(`in sys.argv`, no argparse). `stream_smoke.py:20-22` imports the datagram builders from
`tools/stream_send.py`.

Pass and fail: `check()` prints `ok` or `FAIL` per assertion and counts; each `main()`
returns 1 if `failures` is non-zero (read back with `from api_smoke import failures`).
The helpers use bare `assert st == 200` (content:21, widgets:26, stream:39), so a
transport error aborts with a traceback, exit code 1 and no restoration: there is no
`try/finally` in any device script. `cache_sweep_smoke.py:62-64` and `:106-108` return 0
(a pass) when the card is absent or `--delete` was not given.

State left behind on the happy path: api_smoke restores the brightness (:75) and deletes
`animations/_smoke` (:112-114). content_smoke deletes its playset and re-activates Local
(:141-146) but sets `auto_swap_seconds` to 30 (:121) and never restores it.
makapix_smoke restores `max_size` (:70) and ends on Local (:138) whatever the starting
playset. widgets_smoke restores five keys (:137-139) but leaves the overlay corner
(`top_left`, :55) and the weather location (Greenwich, :112). stream_smoke restores
`main_state`, `takeover` and `silence_ms` (:257-258) but not `ddp_enabled` and
`raw_udp_enabled` (forced on, :140-141). imu_smoke restores the settings (:73-74) but
its `calibrate_upright` (:52) rewrites the calibration. pin_smoke ends with no PIN
(documented) and fails at :46 if the device has one. ota_smoke ends on the original slot
after two reboots. cache_sweep leaves Promoted playing (documented, :13) and with
`--delete` erases the cache (by design). soak restores the playset then the settings
(:107-108). ops, panel_mode and ui restore or are read-only.

| Script | Needs | Time | Deterministic? |
|---|---|---|---|
| api_smoke | card | about 10 s; `--bench` minutes | yes; the `internal_free > 20000` check (:64) sits inside the 13 to 30 KB range the device actually runs at |
| content_smoke | a card with files ("run api_smoke --corpus first", :6) | 30 to 60 s | depends on card contents and timers |
| makapix_smoke | internet; `--paired` needs the pairing | up to several minutes (waits of 90, 120, 60, 180 s) | no: the live site, random artworks, a `sqid or "BVLa"` fallback (:90), 200 or 422 both accepted (:110) |
| widgets_smoke | SHTC3, internet (Open-Meteo) | about 2 min | timing-based sleeps |
| stream_smoke | UDP reachability, `tools/stream_send.py` | about 30 s | tolerates up to 2 incomplete frames (:217) and Wi-Fi loss |
| ops_smoke | a synced clock | about 10 s | yes (does not run the reset) |
| imu_smoke | the IMU, the panel not flat (skips at :61-62) | about 5 s | position-dependent |
| pin_smoke | no PIN set | a 31 s sleep (:89) | yes |
| ota_smoke | `build/p64.bin`, a PC HTTP server (:97), two reboots | "about three minutes" (:9), fixed 12 s sleeps (:111, :127) | mostly |
| ui_smoke | nothing | seconds | yes |
| panel_mode_smoke | nothing | about 25 s | a heap tolerance of 4096 B (:87) |
| cache_sweep_smoke | card, synced clock, internet; `--delete` destructive | minutes | depends on cache contents |
| soak | internet (Promoted) | default 10 min | threshold-based (:99-106) |

All run from a second machine; none can run on the device alone.

Endpoints in `docs/api.md` with no device test: `/api/v1/ws` (soak.py's `WsLoad` class,
:35-49, polls HTTP status and frame and never opens the WebSocket),
`/api/v1/diag/coredump/erase`, `/api/v1/action/refresh`, `/api/v1/folders`,
`/api/v1/makapix/pair`, `pair/cancel`, `unpair`, `POST /api/v1/update/install` with the
empty body (the GitHub asset form; only the URL form is tested, ota_smoke:102),
`/api/v1/files/rename`, `/api/v1/files/format` (the refusal was checked by hand),
`/api/v1/wifi/scan`, `POST /api/v1/wifi`, `/api/v1/wifi/erase`, `/api/v1/timezones`,
`/api/v1/diag/log`, `/api/v1/diag/dma`. `/api/v1/diag/bench` only under `--bench`;
`/api/v1/action/factory_reset` only its refusal (ops:77); the setup portal has no test.
`api.md:5-7` ("api_smoke.py and content_smoke.py exercise everything below") is out of
date.

## 4. Regression protection

Bugs recorded as found and fixed in `docs/PROGRESS.md`, and whether a test guards them:

| PROGRESS line | Bug | Test |
|---|---|---|
| :56-62 | the renderer measured the minimum stay from the copy's end (10 fps instead of 20); lateness measured against the wrong timeline | none direct; soak.py:103 (at most 2 late flips) is indirect |
| :89-96 | LWIP_MAX_SOCKETS too low; httpd before netif init; a Wi-Fi mutex deadlock; the portal replied after acting | none (no Wi-Fi or portal test) |
| :108-110 | the settings handler hit the display during driver re-creation (crash) | indirect: panel_mode_smoke switches through a settings PUT |
| :110-113 | uploads returned zeros past the last 4 KB block | api_smoke:94 (sizes 4095, 4096, 4097 read back byte for byte) |
| :113-118 | driver re-creation left the DMA stalled | panel_mode_smoke:68 (`dma_moving`, `stalled`) |
| :118-119 | out-of-range settings wrapped before clamping | api_smoke:73-75 (999 to 255) on the device; no host test of the pure clamp code |
| :151-154 | the core-1 idle watchdog under slow decode | none (sdkconfig only) |
| :154-158 | the render schedule drifted 1.2 % slow and ate the player's lead | none direct |
| :159-162 | the boot animation late at 60 fps (now 30) | none |
| :201-205 | a refresh walked every page before installing; a one-file cache replayed the same artwork; a stale resume after a status screen; esp-mqtt started twice | none |
| :225-229 | minute-long widget frames blocked a swap for up to two minutes | indirect: widgets_smoke:68-74 expects each widget within 3 s |
| :229-231 | the font line top anchored on the tallest glyph | main.cpp:633-646 (no ink above the cap-height box) |
| :231 | the weather strip clipped at the bottom row | none |
| :261-263 | a "last" chunk arriving first ended the frame with holes | main.cpp:820-825, stream_smoke:162-165 |
| :263-265 | Wi-Fi RX buffers dropped 128x128 bursts | indirect: stream_smoke:217 (at most 2 incomplete) |
| :307-310 | the httpd handler table full (routes silently missing); `Retry-After` sent empty | indirect for the table; pin_smoke:82 for Retry-After |
| :339-352 | the WebSocket push task crash-looped on an NVS read from a PSRAM stack (the M10 "important one") | none: no test opens the WebSocket; soak polls over HTTP, which builds the document on the httpd task instead |
| :373-376 | a settings change while a widget was up did not redraw it | none |
| :377-405 | Photo mode posterized (the LUT fit collapse); a switch re-allocated the descriptor chains and failed after hours | panel_mode_smoke:64-70, :87 covers planes, rate and heap; the tonal collapse "reproduced on the host" has no committed test |
| :406-418 | the frozen artwork: artwork requests ignored the Widget state | widgets_smoke:77-109 |
| :419-439 | `max_size` snapping and fit | makapix_smoke:62-82; the 60 s walk restart (`kWalkIdleUs`) untested |
| :440-462 | channel counts stood still in the UI | api_smoke:65-66 checks that the two version fields exist, not that they move |
| :454-460 | a saved Followed playset never restored after a reboot (the fetcher answered "offline") | none; commit 7e0570c touched no test |
| :463-489 | the cache sweep | cache_sweep_smoke |

## 5. Practices

| Practice | Status |
|---|---|
| Warnings as errors (device) | not a project choice: no `compile_options` in any CMakeLists; ESP-IDF's `tools/cmake/build.cmake:132-140` supplies `-Wall -Werror -Wextra` with `-Wno-error=unused-*`, `-Wno-error=extra`, `-Wno-unused-parameter`, `-Wno-sign-compare`; the build log shows eight `-Wmissing-field-initializers` warnings in p64_makapix that would fail a stricter build |
| Warnings as errors (host) | no: `-Wall -Wextra` without `-Werror` (run.py:128); vendored C built with `-w` |
| clang-tidy, cppcheck, `-fanalyzer`, sanitizers | none anywhere |
| `.clang-format` | `firmware/.clang-format` exists (Google, 2 spaces, 120 columns); nothing runs or checks it |
| Pre-commit hook, CI | `.git/hooks` holds only samples; no `.pre-commit-config.yaml`, no `.github/` |
| Size budget | none; PROGRESS.md:78 "see size report below when taken" was never taken; `idf.py size` appears in no script or doc (the report of 2026-09-22 is in `memory.md`) |
| Heap floor | soak.py:105 (`heap_min > 8000`), api_smoke.py:64 (`> 20000`), panel_mode_smoke.py:87 (largest block within 4096 B); no boot-time or per-commit floor |
| One command for the suite | none; run.py is host-only; the 13 device scripts are listed one by one in CLAUDE.md and README.md; ordering only in docstrings (content_smoke:6) |
| "Run before committing" | written nowhere: CLAUDE.md "Working conventions" says nothing about tests; README.md:93 only says to rerun the benchmark after decoder or scaler changes |
| Followed in practice | since 2026-09-19: 34 commits touch `firmware/`, 24 touch p64 sources, 19 of those also touch `firmware/tests/` (mostly milestone commits bundling their smoke test); five do not: 8e2b602 (M0), 27c2976 (M1, the two pacing fixes), dac7c7d (M3, four device bugs), 7e0570c (the Followed restore fix), 5b02e9a (the rename); every 2026-09-20 and 2026-09-21 fix except 7e0570c shipped with a device check |

## Coverage by file

| Component | Host-compiled (run.py:40-69) | Not compiled (blocker) |
|---|---|---|
| p64_gfx | frame, scaler, text, fonts, fonts_data | png_encode (IDF-free, just not listed) |
| p64_decode | format, gif_decoder, png_decoder, webp_decoder, bmp_decoder | none |
| p64_content | playset, playset_json, scheduler, history, makapix_index | local_index (dirent and stat only, IDF-free); playset_store (esp_log and card files) |
| p64_playback | `frame_queue.hpp` (header-only) | artwork (IDF-free); player (FreeRTOS task and queue, esp_timer, heap_caps); renderer (esp_timer, Display, heap_caps) |
| p64_system | night, rtc_codec | settings (NVS in :384-414, cJSON, log); state_store (NVS); reliability (esp_ota, coredump, esp_system); event_bus (FreeRTOS queue and task); flash_guard (FreeRTOS, esp_memory_utils); log_ring (heap_caps, a critical section); rtc, i2c_bus (the I2C driver) |
| p64_widgets | clock_format, analogue, weather_model, weather_icons, weather_icons_util | widgets (tasks, esp_timer); shtc3 (the I2C driver) |
| p64_stream | protocol | stream (lwIP sockets, a task, esp_timer, the watchdog) |
| p64_inputs | tap, orientation | inputs (a task, esp_timer, cJSON); qmi8658 (I2C, vTaskDelay) |
| p64_ota | version | ota (esp_ota_ops, esp_partition, esp_https_ota, mbedtls, a task) |
| p64_net | none | tz (IDF-free); clock (SNTP); dns_hijack (lwIP); fetch (esp_http_client); http_server, setup_portal (httpd); wifi (esp_wifi, NVS, mdns) |
| p64_web | none | api, auth, content, files, makapix_routes, ui, ws (all bound to `httpd_req_t`; auth also NVS, mbedtls, esp_random) |
| p64_makapix | none | api (one log, the app description, CONFIG); cache (log, heap, file I/O); credentials (NVS); fetcher (task, queue, timer); makapix (timer, x509, tasks); mqtt (esp_mqtt) |
| p64_storage | none | card (SDMMC, VFS) |
| p64_display | none | display (GDMA registers, hub75) |
| main | none | status_screens, boot_animation (IDF-free); show (queue, watchdog, timer, random, heap and every component); loader, ops, main (tasks, GPIO, NVS) |

26 of 74 p64 sources are host-compiled; six more compile as they are without any seam.

## Untested fixed bugs

1. Renderer pacing: the minimum stay measured from the copy's end, and lateness against
   the player's timeline (PROGRESS.md:56-62, renderer.cpp).
2. The render schedule anchored on the copy end drifting 1.2 % slow (PROGRESS.md:154-158,
   renderer.cpp:80).
3. The boot animation late at 60 fps (PROGRESS.md:159-162).
4. The four M3 network bugs: the socket count, httpd before netif, the Wi-Fi mutex
   deadlock, the portal replying after acting (PROGRESS.md:89-96).
5. A channel refresh installing nothing until every page was walked (PROGRESS.md:201-202,
   fetcher.cpp:248).
6. A prepared pick from a one-file cache replaying the same artwork (PROGRESS.md:202-203,
   show.cpp:346).
7. A stale resume after a status screen showing one artwork while history named another
   (PROGRESS.md:203-204).
8. esp-mqtt logging an error when started twice (PROGRESS.md:205, mqtt.cpp:140).
9. The weather strip's low temperatures clipped at the bottom row (PROGRESS.md:231).
10. The WebSocket push task crash loop on an NVS read from a PSRAM stack
    (PROGRESS.md:339-352): no test opens `/api/v1/ws`.
11. A settings change in the Widget state not redrawing the widget until its next frame
    (PROGRESS.md:373-376).
12. The Photo-mode LUT fit collapse to three levels (PROGRESS.md:377-384): reproduced on
    the host, no test committed.
13. An interrupted Makapix walk resuming on a dead kept-alive connection (`kWalkIdleUs`,
    PROGRESS.md:433-439).
14. A saved Followed playset not restored after a reboot because the fetcher answered
    "offline" (PROGRESS.md:454-460, commit 7e0570c).
15. Out-of-range settings wrapping before clamping (PROGRESS.md:118-119): device-checked
    for one key only (`brightness`, api_smoke:73-75); the pure clamp and enum code in
    settings.cpp:121-380 has no host test.
