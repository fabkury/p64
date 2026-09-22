# Proposals: the ranked roadmap

Every proposal names its gain (measured where it was measured today, est. otherwise),
its cost in sessions of work, its risk, and whether it breaks a contract (the API, the
Makapix contract, a setting, a file format). Ordered by gain per cost within the three
tiers; the tiers are ordered by urgency. The decisions the review was made under are in
`README.md`; the evidence is in `memory.md`, `cpu.md` and `testing.md`.

Breaking changes recommended: two. P-M1 (the Makapix command channel) changes the device
contract on both ends; P-T1 (pure cores) changes the internal architecture of four files.
Feature drops recommended: none. One demotion (P-D1, streams to a compile-time option).

## Tier 1: do now, before any new feature

| # | Proposal | Gain | Cost | Risk | Breaks |
|---|---|---|---|---|---|
| P-M2 | Configuration: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024`, `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n`, `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y`, `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=y` in `sdkconfig.defaults` | measured: 13 KB to 54-58 KB free, largest block 12 KB to 32.7 KB, minimum since boot 42 KB (`memory.md` section 3) | half a session: the lines, delete `sdkconfig`, then the verification list | Wi-Fi throughput and latency unmeasured with the driver's code in flash; kernel paths in flash | nothing; revertible |
| | Verification before keeping it: a timed 5 MB vault download against the M6 figure (30 to 200 KB/s), `stream_smoke.py` at 128x128 30 fps (loss stays about 1 %), `panel_mode_smoke.py`, `ota_smoke.py`, a 45-minute `soak.py` with the browser poll, all with 0 late flips and the heap floor recorded. | | | | |
| P-T7 | The written rule in CLAUDE.md: run `tests/host/run.py` before a firmware commit; a device-found fix ships with its test in the same commit; new files are host-testable unless they talk to hardware | stops the gap growing (5 of 24 source commits had no test) | minutes | none | nothing |
| P-T6 | `firmware/budgets.json` (image size, static DIRAM, internal free and largest block at boot and at steady state, the minimum over a soak, core 0 busy share) checked by `api_smoke.py` and `soak.py` today and by CI later; the 2026-09-22 numbers are the first entries; keep the instrumentation branch's `diag/memory` fields and add `tools/cpu_sample.py` | turns every future regression of the kind found today into a red test | one session | none | nothing |
| P-C1 | The four CPU discipline fixes: set the show loop's priority to the documented 5 (or document 1 and reorder); the overlay hook and the widgets read an atomic settings snapshot instead of copying under the mutex that `settings_update()` holds across the NVS write; `settings_update()` releases the mutex before the write; `Display::health()` publishes a flag the render task sets instead of busy-waiting 200 us | correctness of two stated design rules (`cpu.md` F1, F2, F4) | one session | low | nothing |
| P-M7 | Enforce the two unenforced rules: wrap `wifi.cpp`'s NVS calls in the flash guard; pin the esp-mqtt task to core 0 (`CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED` and core 0, until P-M1 removes it) | closes a latent crash path and a documented rule | an hour | none | nothing |

## Tier 2: the two breaking changes

| # | Proposal | Gain | Cost | Risk | Breaks |
|---|---|---|---|---|---|
| P-T1 | Pure cores with thin shells for `show.cpp` (`ShowCore`: commands and events in, effects out, injected clock and RNG), `renderer.cpp` (`Schedule`), `player.cpp` (`Timeline`), `fetcher.cpp` (the refresh, walk and download policy over a fake API and cache); replay the scenarios of the fifteen untested fixed bugs as host tests; fix the architecture table and the priority drift in the same pass (`testing.md` P-T1, P-T2, P-T8) | the state and timing code, where 11 of the 15 untested fixed bugs live, becomes testable in milliseconds; every later feature lands as a host test first | three to four sessions, mostly moving code; the cheap seams (settings clamp, the site contract parsers, the MQTT payloads, the OTA release parser, the cache sweep, the widget drawing) come with it | regressions while moving; mitigated by doing it one file at a time behind the existing device tests | the internal architecture only; the API and the settings are untouched |
| P-M1 | Replace the MQTT-over-mTLS session with an HTTPS command channel on the fetcher's single TLS slot. Server: a long-poll endpoint (`GET /player/commands?wait=25`, returns the queued commands or times out; the same command JSON as `docs/mqtt-api/commands.md`), a presence rule (online while polls arrive, offline after two missed intervals), an ack endpoint, and the view and status endpoints that already exist over HTTPS. Device: the fetcher's loop holds one kept-alive HTTPS session and alternates a long-poll with its other jobs; the MQTT client, its task, its buffers and the certificate machinery (renewal, mTLS) go; the API token becomes the only credential (`docs/http-api/player-rpc.md` already defines it) | est. 17 to 20 KB of steady internal RAM and, more importantly, no second TLS handshake next to a persistent session, so the minimum since boot tracks the steady state (`memory.md` M2); esp-mqtt leaves the image; one fewer unpinned task | two sessions on the server (the long-poll router, presence, the p3a compatibility: keep MQTT for p3a, add the HTTPS path beside it), two on the device (the fetcher loop, the pairing without certificates, the status document, the tests); the server's own roadmap names this ("command polling or an SSE stream, not v1") | command latency becomes the long-poll's return time (sub-second while a poll is open, up to one fetcher step while it is busy with a download: keep downloads chunked so a poll starts within 2 s); presence is coarser than a last-will; a rate limit per device on the server | the Makapix device contract for p64 (p3a keeps MQTT); ADR 0009 is superseded by a new ADR; the pairing flow loses the certificate step (simpler) |

Order between the two: P-T1 first if the next weeks are feature work (it makes P-M1's
fetcher change testable); P-M1 first if the device is going to other people soon (it
removes the failure mode). They touch different files except `fetcher.cpp`, which P-T1
should split before P-M1 rewrites its loop.

## Tier 3: trims and practice, in order of gain per cost

| # | Proposal | Gain | Cost | Risk | Breaks |
|---|---|---|---|---|---|
| P-M3 | Code trims: `merge_index` as a sort-and-merge over the PSRAM vectors (no map nodes); one kept-alive `esp_http_client` in the fetcher; the PEMs held once (pointers into `g_creds`, moot after P-M1); the history as fixed-width records in PSRAM; `Artwork::bytes_` and the decoder canvases with the PSRAM allocator regardless of size (moot after P-M2 for sizes above 1 KB); `logring::tail()` copying outside the critical section; the stream buffers allocated when a listener is enabled | est. 20 to 30 KB steady internal after P-M2, and the merge's 57 KB transient peak gone | one session; the merge is host-tested already | low | nothing |
| P-T3 | doctest in place of the two macros; named cases, `--test-case`, JUnit output; `-Werror -fsanitize=address,undefined` in CI | failures name themselves; memory errors in the pure code surface on the PC | one day | none | nothing |
| P-T4 | GitHub Actions: host tests (ubuntu, gcc, Pillow, sanitizers) and the firmware build (`espressif/idf:v5.5.4`, `idf.py build`, `idf.py size` against `budgets.json`, `-Werror` for the p64 components, `clang-format --dry-run`), artifacts `p64.bin` + SHA256 | every push checked; the release assets built by the same job | one session, plus the first run's surprises (the secrets file is optional, to confirm) | none | nothing |
| P-T5 | `tests/device/p64test.py` (argparse, a `Device` class, `snapshot` and `restore` in `try/finally`, a WebSocket client, JUnit output) and `run_all.py` with tiers; then the missing tests: the WebSocket push held for 30 s, pairing cancel, Wi-Fi scan, rename, the log ring, the refresh action, the version counters moving | the M10 crash loop gets its test; scripts stop leaving state behind; one command runs the safe set | two sessions | none | nothing (the scripts' command lines change) |
| P-C2 | IMU at 100 Hz, or the QMI8658's tap interrupt with a 10 Hz gravity read | 1.5 to 3 % of core 0 (measured 2.5 to 3.4 % today) | half a session; the tap detector's thresholds re-verified by hand (still pending anyway) | the tap shape rule at 10 ms samples | nothing |
| P-C3 | The download path: `MakapixChannelChanged` carries the storage key so the show marks one entry instead of snapshotting every channel; a table-driven CRC32 for the index files; the renderer waits on the queue's semaphore instead of a 1 kHz poll | transient CPU during cache fills (0.5 to 1.5 ms per download on the show loop today), 0.3 % of core 1 | half a session | low | nothing |
| P-M6 | `physical_` (the 12 KB staging frame in `g_display`) in PSRAM, kept only if the 7.6 ms copy grows by less than 0.5 ms | 12 KB internal | an hour plus the measurement | the copy time | nothing |
| P-D1 | Streams demoted to a compile-time option (`P64_STREAMS`, off in the release build, on in a CI build variant); the takeover parking moves into the show core (P-T1) where a host test covers it; buffers allocated on enable | maintenance: 675 lines and one second-machine device test leave the default build; about 2 KB internal, 183 KB PSRAM | half a session after P-T1 | none | the setting `stream.*` group disappears from the release build's settings document; ADR 0007 gets a note |

## Not recommended

- Dropping the streams outright: the user may want them later, they cost nothing while
  compiled out, and the takeover logic is the best test case for the show core.
- Dropping the WebP or PNG decoders: 134 KB of flash and no internal RAM; the site serves
  WebP.
- Eight planes in Quality mode: 21 KB of internal DMA memory at the tonal cost the spec
  rejected on 2026-09-20.
- Replacing the WebSocket push with polling: one PSRAM stack; nothing internal.
- Dropping the IMU, the weather or the temperature widget: used, and cheap.
- A larger `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`: it does not create RAM, it only
  changes who fails first.
- Raising `CONFIG_ESP_TASK_WDT_TIMEOUT_S` to watch the TLS tasks: better to give the
  fetcher a "in TLS" grace than to loosen the watchdog for everyone.

## Suggested sequence

1. P-M2 with its verification list, P-T7, P-M7 (one session; the device leaves it with
   about 55 KB free).
2. P-T6 with the instrumentation merged, P-C1 (one session).
3. P-T3 and P-T4 (one to two sessions; CI green on the host tests and the build).
4. P-T1 file by file, P-T2 alongside (three to four sessions).
5. P-M1 server side, then device side, with a new ADR superseding 0009 (four sessions).
6. P-M3, P-T5, P-C2, P-C3, P-M6, P-D1 as time allows.

After step 1 the memory target is met on free space and largest block; after step 5 the
low-water mark stops being a separate number. After step 4 a bug found on the device is
reproduced on the host the same day.
