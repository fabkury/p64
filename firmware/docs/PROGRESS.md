# p64 firmware: progress log

Read this first when resuming. Newest entry at the bottom of "Log"; the milestone table
shows where things stand. Spec: `docs/spec/p64-spec.md`. Design: `architecture.md`.

## How to resume

1. Read `architecture.md` and the milestone table below.
2. Build and flash from `firmware/` in PowerShell 7: `.\tools\build.ps1`,
   `.\tools\flash.ps1`, `.\tools\monitor.ps1` (or `tools\serial_peek.py` for a
   non-interactive read of the console; the board is COM13 on this laptop).
3. Check the device: `http://p64.local/api/v1/status` once the web layer exists; the
   hardware-tests firmware answered `/status` and `/debug` before that.
4. Continue with the first unchecked item of the current milestone. Commit per step.

## Milestones

| # | Milestone | Status | Verified on device |
|---|---|---|---|
| M0 | Project skeleton: builds, boots, console, boot animation on the panel, tools | done | 2026-09-19: boot log clean, panel frame-locked at 271.3 Hz, boot animation 2010 ms, idle 66.7 fps, 0 late flips |
| M1 | Display layer ported (pacing, rotation, gains, brightness, panel modes) + GIF from the card in the new player | done (panel modes reworked 2026-09-20) | 2026-09-19: card GIFs play through player + renderer at their stored delays, late 0, decode 1.1-1.5 ms, copy 7.5 ms, swap drops the old queued frame |
| M2 | PNG/APNG, WebP, BMP decoders; format sniffing; decode benchmark; no-drop timeline | done except the on-device benchmark (needs files on the card: M4 upload) | 2026-09-19: host tests 86 files / 1531 frames exact; device builds and plays GIFs unchanged |
| M3 | Storage layout, settings store, Wi-Fi manager with setup mode, mDNS, time | done (RTC chip deferred to M9) | 2026-09-19: settings in NVS, dev seed, STA join, mDNS, SNTP + zone table, HTTP server, portal page, scan, captive probes, erase -> setup mode (AP+STA, captive DNS) all seen on the device |
| M4 | HTTP API v1, WebSocket push, live preview, minimal web UI | done | 2026-09-19: smoke test 0 failures (status, settings, frame PNG, uploads read back byte for byte, play, delete); panel modes switch in place, frame-locked; decode benchmark recorded |
| M5 | Content: local channels, playsets, scheduler, history, auto-swap, play-this | done (Makapix channels wait for M6) | 2026-09-19: content smoke test 38 checks / 0 failures; boot to first artwork 3.3 s; 40 ms APNG at 25.0 fps with 0 late; playsets CRUD, activation, history navigation, pause/resume on the device |
| M6 | Makapix: promoted anonymous, pairing, MQTT commands, downloads, views, likes | done (commands from the site await the user's test) | 2026-09-19: Promoted lists 290 posts anonymously and plays 1.4 s after the first download; paired with code TDPCHB, MQTT connected 2 s after the credentials; views published; likes over HTTPS next to MQTT; All (2048 entries) and hashtag/own channels walk page by page; internal RAM 25-30 KB free with MQTT up |
| M7 | Widgets: fonts pipeline, clock overlay, clock, weather, temperature, interludes | done (analogue face added 2026-09-20) | 2026-09-19: SHTC3 read, Open-Meteo fetched, clock/weather/temperature frames captured, overlay on artworks, interludes in history |
| M8 | Streams: DDP, raw UDP, takeover | done | 2026-09-19: both protocols pixel-exact on the device (RGB888, RGB565, indexed, 128x128 downscaled, reversed chunks), takeover and return after silence, Stream state; `tests/device/stream_smoke.py` |
| M9 | IMU, night schedule, PIN, OTA, coredump, diagnostics, factory reset | done | 2026-09-19: reliability (reset reason, counters, core dump summary, deferred image confirmation), RTC seed, night schedule, factory reset (API and BOOT hold), task watchdog on the loops: `tests/device/ops_smoke.py` 0 failures. IMU taps and auto-rotation (`tests/device/imu_smoke.py` 0 failures; taps and the rotation sign await a hand on the shell). PIN (`tests/device/pin_smoke.py` 0 failures). OTA: check against GitHub, install of a local build over HTTP with SHA256, reboot into the other slot, confirmation, rollback (`tests/device/ota_smoke.py`) |
| M10 | Full web UI port, acceptance tests, docs | done (instrument measurements open) | 2026-09-19: the four pages (Home, Playsets, Settings with seven tabs, Update) on p3a's stylesheet and five themes, the setup portal in the same style, PWA manifest and icons, `tests/device/ui_smoke.py`; `tests/device/soak.py` passed 10 min (120 swaps, 0 late, 0 timeouts, heap floor 11.8 KB); the crash loop from flash reads on a PSRAM stack found and fixed (flash_guard) |

## Log

### 2026-09-19

- Product spec, glossary and ADRs settled and committed (commit 47fdee5).
- Quick device check for the user: COM13 console readable without reset (pyserial,
  DTR/RTS low), hardware-tests web endpoints answered at 192.168.20.58, panel 271 Hz,
  card mounted. The user left; questions go to their phone.
- Decision while porting: the hub75 driver's bit depth is compile-time, so Photo mode
  is implemented as a driver re-creation with `min_refresh_rate` 600 (transition bit 6,
  698 Hz), see `architecture.md` section 4.
- M0 started: `architecture.md`, this file, vendored `hub75` and `animatedgif` copied
  from the hardware tests, tool scripts copied, dev Wi-Fi seed in `sdkconfig.secrets`
  (git-ignored).
- M0 verified on the device (first flash of the new firmware): `display` reports the
  driver at 271.3 Hz with frame boundaries read from GDMA channel 0; the boot animation
  ran 2010 ms; the idle pattern presents at 66.7 fps with render 6.7 ms (float maths, not
  representative), copy 7.4 ms, wait 0.8 ms, 0 late flips, 0 timeouts. Internal heap at
  boot: 297 KB + 21 KB + 32 KB DRAM. The core dump partition logs "incorrect size" once
  because it was never written; harmless.
- M1 done: `p64_decode` (Decoder interface, sniffing, browser delay rule, GIF decoder
  with background colour), `p64_storage` (card mount, p64 layout created, listing,
  reads), `p64_playback` (FrameQueue of 3 slots with generations, Artwork, Player task
  on core 1 at priority 15, Renderer task at 20). main plays random card GIFs every
  30 s. Two pacing bugs found and fixed on the device: (1) the minimum stay was
  measured from the end of the 7.5 ms copy, so every frame slipped by the copy time
  (10 fps instead of 20); the renderer now starts the copy a measured lead ahead of
  the target and keeps the schedule on target times; (2) lateness was measured against
  the player's timeline anchored at the first decode, unattainable by the copy time;
  it is now measured against the renderer's own schedule, which starts at the first
  frame's visibility. Result: late 0, fps equal to the stored delays (8 fps for
  125 ms frames, 1.9 fps for 500 ms). The first 10 s window shows a higher fps because
  the 60 fps boot animation frames are counted in it.
- Panel mode switching (`Display::set_mode`) is implemented but not yet exercised; it
  gets its test with the API (M4). Rotation is fixed at 90 until the settings store.
- Host tests in `tests/host/` (run.py + main.cpp): 74 unit checks, GIF corpus exact.
- M2: PNG/APNG (components/libpng, p3a's APNG-patched libpng 1.6.52 fork, pruned;
  zlib from the registry), WebP (components/libwebp, decoder subset of v1.4.0 vendored),
  BMP (ported from p3a), all behind `Decoder`; alpha flattened over the background in
  gamma space (`alpha.hpp`). A Pillow-made corpus of 22 PNG/APNG/WebP/BMP/GIF files
  lives in `tests/host/corpus/`; the runner now compiles zlib, libpng and libwebp on
  the PC. Finding: Pillow's APNG reader pastes OVER-blended sub-frames with their alpha
  as a mask (halving colour and alpha over transparent areas) instead of a true OVER,
  so the tests use a spec compositor (`apng_reference_frames`) as the APNG oracle; our
  decoder agrees with the spec. All 86 files exact.
- Device: builds with all decoders, plays card GIFs unchanged. Internal heap at boot
  fell from 297 KB to 258 KB with the libraries linked (see size report below when
  taken). The on-device decode benchmark waits for a way to put PNG/WebP files on the
  card (M4 file manager).
- M3: `p64_system` (typed Settings document in NVS as JSON with the spec's groups and
  ranges, change events; EventBus with a core-0 dispatcher task) and `p64_net` (Wi-Fi
  manager: one saved network in NVS namespace `wifi`, dev seed from sdkconfig.secrets,
  60 s fallback into setup mode with the STA retried every 30 s underneath, exponential
  reconnect backoff; setup mode = open AP `p64-setup` in AP+STA mode + captive DNS +
  portal on the single HTTP server; SNTP; IANA zone table of 519 entries generated by
  `tools/gen_tz_table.py` from Python's zoneinfo). main applies rotation, gains and
  brightness from the settings and re-applies them on change.
- Bugs found on the device: (1) the HTTP server asked for more sockets than lwIP's
  default allows (now `CONFIG_LWIP_MAX_SOCKETS=16`); (2) `httpd_start` before
  `esp_netif_init` asserts in lwIP (server now starts after Wi-Fi start); (3) holding
  the Wi-Fi state mutex across driver calls deadlocked with the event loop (mutex now
  guards state only); (4) the portal's erase/save answered after acting, so the reply
  never left the network the request came over (actions are now deferred 500 ms on a
  helper task). Verified: erase over HTTP returns 200, the log shows disconnect,
  AP+STA mode, captive DNS; a hard reset re-seeds and reconnects. Windows' netsh scan
  did not list `p64-setup` (its scan cache), so the AP's visibility and the portal
  flow from a phone remain for the user to confirm.
- M4: `p64_web` (/api/v1: status, settings GET/PUT with clamping, actions next/play/
  reboot, frame as PNG or raw, Wi-Fi scan/set/erase, time zones, diagnostics: log ring,
  memory with task stacks, DMA priority, decode benchmark; file manager: list, get,
  upload with sniff-and-decode validation, delete, mkdir, rename; WebSocket status push
  every 2 s and on events; embedded single-page UI), `p64_system::logring`, a minimal
  PNG encoder in `p64_gfx`, the renderer's preview snapshot and in-loop panel mode
  switch, a show controller in main (command queue: next, play this file; the API drives
  it). `tests/device/api_smoke.py` exercises the API on a live device (uploads of sizes
  around the 4 KB and 512 B boundaries read back byte for byte).
- Bugs found on the device and fixed: (1) the settings-change handler called the display
  while the render task re-created the driver (crash; the display now has a driver
  mutex); (2) uploaded files came back as zeros past their last full 4 KB block: newlib's
  stdio buffer had moved to PSRAM with `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` and the
  partial-block flush wrote zeros through the SDMMC path; card I/O now uses POSIX
  read/write with an internal DMA-capable bounce buffer; (3) a driver re-creation for the
  panel mode switch left the DMA stalled for good (descriptor pointer frozen, in either
  mode; a status probe `dma_moving` now shows it); the vendored driver gained
  `set_min_refresh_rate()` which rebuilds the descriptor chains in place, and
  `get_dma_channel_id()` so the display watches the right channel; switches now keep
  the DMA streaming and the frame lock in both directions; (4) out-of-range settings
  values wrapped before clamping.
- Internal RAM: 32 KB free became 96 KB free (largest block 59 KB) after moving the
  ready frames, the boot scratch and the preview to PSRAM and giving the player,
  events, WebSocket push and DNS tasks PSRAM stacks (the render task stays internal).
- Decode benchmark on the device (decode + scale per frame, HTTP task on core 0):
  64x64 GIF 1.0 ms, APNG 4.7 ms, WebP lossless 5.7 ms; 128x128 APNG 22.3 ms, WebP
  lossy 28.7 ms (35-45 fps sustainable, so 128x128 stays best effort as the spec
  allows); 256x256 PNG static 15.9 ms. Details in `README.md`.
- Setup-mode visibility from a phone and the portal flow remain for the user to confirm.
- M5: `p64_content` (channels and playsets with validation and p3a-compatible JSON;
  the scheduler ported from p3a's play scheduler: SWRR and stochastic selection over
  weights normalised to 65536 with credit correction, random and recency picks with
  per-channel offsets, PCG32; a history ring of 32 with a position; the local folder
  index, newest first, entries in PSRAM; the playset store as JSON files under
  `channels/playsets/`), host-tested (1115 checks: exact 3:1 SWRR shares, stochastic
  within 5 %, cursor offset and wrap, history navigation, JSON round trips).
  `main/show.cpp` is the state machine on the main task: a command queue (next,
  previous, go-to, pause, resume, reset timer, refresh, play file, activate playset)
  and a loader task on core 0 that reads files and scans folders. The next artwork is
  prepared (read and decoder opened) while the current one plays, so auto-swap and
  next cost nothing visible; history navigation drops items whose file vanished and
  walks on; play-this enters history; pause is a black frame; activation persists the
  name in NVS (`p64state`), and the restore at boot falls back to Local with a card
  (Promoted once Makapix exists); a "no artwork" status screen (a symbol until the
  fonts land in M7) shows the reason. The player now plays any `FrameSource`
  (Artwork, StaticSource, BootSource): the boot animation runs through it at 30 fps
  while the card mounts and the playset is scanned; first artwork 3.3 s after
  power-on (card at 1.65 s, Local scan of 22 files 12 to 225 ms). API: playsets
  (GET/PUT/DELETE `/api/v1/playsets[/name]`), `action/play_playset`, `channels`,
  `history`, `folders`, actions previous/pause/resume/reset_timer/refresh/history_go
  (`docs/api.md`); the dev UI got playset pills, transport buttons, the history and a
  JSON playset editor. `tests/device/content_smoke.py` (38 checks) passes.
- Found on the device and fixed: (1) the task watchdog fired on core 1's idle task
  while a 128x128 lossy WebP (30 to 37 ms per 16 ms frame) kept the player busy; that
  is the no-drop rule at work, so `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`
  (documented in `sdkconfig.defaults`). (2) The render schedule was anchored on each
  copy's end time and drifted 1.2 % slow (a 40 ms APNG measured 24.7 fps), which ate
  the player's three-frame lead until every frame was "decode late" by 0.1 to 2 ms;
  targets now carry durations exactly and only a miss beyond one refresh period
  re-anchors (spec 4.3 reworded): 25.0 fps, late 0, decode late 0. The renderer no
  longer counts player-late frames as its own. (3) The boot animation at 60 fps through
  the player was late on every frame (7 ms render plus the 7.7 ms copy on one core);
  30 fps now, late 0. Best-effort figures with the render task sharing core 1: 128x128
  APNG 28 fps (decode 16.5 ms, max 33), 128x128 lossy WebP 27 fps (36.6 ms, max 40).
- M6: `p64_makapix` (docs/architecture.md section 12): credentials in NVS, pairing
  (provision, code on the panel and in the UI, credential polling every 3 s for 15 min),
  the anonymous promoted feed and the player RPC over HTTPS with the bearer token,
  channel indexes (`content::MakapixEntry`, 64 bytes, binary files under `channels/`,
  merge rules host-tested), the artwork cache on the card (`cache/<xx>/<uuid>.<ext>`,
  plain HTTP from the vault over a kept-alive connection, sniffed before storing) or in
  PSRAM without a card, one worker task for every outbound request, esp-mqtt over mutual
  TLS (commands, acks, capabilities and state retained, presence every 30 s, last will),
  views after 5 s on the panel, likes, certificate renewal with token rotation. The
  built-in 5x7 font (`gfx::text`) draws the status screens: no-artwork reasons, the
  pairing code, "paired", the hostname and IP after joining. `net::fetch` is the one
  HTTP(S) client (User-Agent `p64/<version>`, manual redirects, size caps, `Session`
  for keep-alive). The show maps Makapix channels onto cached index entries, hears
  `MakapixChannelChanged`, reports what it shows, and takes the site's commands as
  transient playsets and play-this downloads. API: `/api/v1/makapix` (status, pair,
  cancel, unpair, like) and `action/play` with `post` or `url`; the dev UI has a Makapix
  card, a like button and a play-from box. `tests/device/makapix_smoke.py`.
- Verified on the device: Promoted listed 290 posts in 6 pages anonymously, downloads
  at about one per second (30 to 200 KB/s from the vault), first artwork 1.4 s after the
  first download; the index reloads from the card at boot (2048-entry All index in
  5.6 s after power-on). Pairing with code TDPCHB: credentials stored 38 s after the
  code (the owner's typing), MQTT connected 1.5 s later, views published (6 in the first
  minutes), like and unlike over HTTPS next to the MQTT session, play-this of a post by
  sqid and by link, the All channel walked to its 2048 cap, a hashtag channel and the
  owner's channel installed their first page within 2 s of activation while downloads
  interleaved. Commands from the site (next, previous, show artwork, brightness, pause)
  are wired and acknowledged but still wait for the owner to send them.
- Memory (the spec's risk 2): with the MQTT session up and downloads running the
  internal heap was 15 KB free before trimming and 25 to 30 KB after (largest block
  24 KB, minimum seen 20 KB): `MBEDTLS_DYNAMIC_FREE_CONFIG_DATA` and `_FREE_CA_CERT`
  free the parsed certificates once a handshake is done, the MQTT task stack is 6 KB
  (2.7 KB used at the handshake peak), Wi-Fi static receive buffers 8. A transient TLS
  session (likes, RPC pages) succeeded at that level. Anything new that needs internal
  RAM must be measured against this.
- Design choices recorded in the spec (section 13): listings over HTTPS RPC rather than
  MQTT request topics; vault downloads over plain HTTP with Content-Length and decode
  checks (`P64_MAKAPIX_VAULT_TLS` switches them to HTTPS); a channel with no index plays
  its first page before the walk completes.
- Fixed on the way: a channel refresh that walked all 41 pages before installing
  anything (now one page per worker step, connection kept open, progressive install); a
  prepared pick chosen from a one-file cache that replayed the same artwork; a stale
  resume after a status screen that showed one artwork while history named another;
  esp-mqtt logging an error when started twice.
- M7: `p64_widgets` (docs/architecture.md section 13): the bundled pixel fonts are
  rasterised by `tools/gen_fonts.py` from the TTFs at their native sizes (Capital Hill
  6 px, Everyday Typical 7 px; Pillow renders them crisp only there) into glyph tables in
  `p64_gfx` (`gfx::fonts`: integer scale, one-pixel outline, ink-box layout anchored on
  digits and capitals); the weather icons are drawn by `tools/gen_weather_icons.py` into
  `assets/weather/*.png` (12 WMO groups, day and night, 24 and 12 px; hand-editable) and
  compiled in. Widgets are `FrameSource`s: the digital clock (time at the chosen scale,
  12/24 h, seconds, blinking colon, date and weekday, colours), the weather (Open-Meteo
  current and four days, icon, temperature, today's high and low, a three-day strip;
  fetched every refresh interval on a PSRAM-stack task through the one-TLS-slot rule),
  the temperature (SHTC3 on the internal I2C bus, offsets, humidity, hourly trend
  arrow). The clock overlay draws through a player hook on every produced frame, and a
  static image is re-emitted when the minute changes. The show gained the main states
  (Widget: the chosen widget stays; Stream: a waiting screen until M8), interludes
  (rolled per widget at every auto-swap, entering history and replaying from it), and
  the sensor and weather in the status document. `net::fetch` now serialises HTTPS
  requests behind a recursive mutex so one transient TLS session exists at a time (a
  kept-alive session holds the slot while open). Settings gained the clock, weather and
  temperature groups. `tests/device/widgets_smoke.py`.
- Found on the device and fixed: a widget with minute-long frame delays kept all three
  ready slots and the render task slept towards a frame due a minute away, so a swap to
  the next widget waited up to two minutes; the player now announces a new generation
  before its first frame and the render task sleeps in 10 ms steps and drops the old
  generation's slots as soon as one is announced. The font line top was the tallest
  glyph (quotes), which pushed digits two rows down; it is anchored on digits and
  capitals now. The weather strip's low temperatures clipped at the panel's bottom row.
- Verified on the device: the SHTC3 answers (31.8 C and 33 % RH inside the warm shell),
  the weather fetch (New York and Greenwich) parses and draws, all three widgets and the
  overlay render as designed (frames captured through `/api/v1/frame`), interludes enter
  history at auto-swap, the main state persists.
- Deferred: the analogue clock face (spec 7.1 says its details are drawn with the user
  first); the city search for the weather location (browser-side, M10); the weather
  and clock widgets take the clock widget's colours (per-widget colours later).
- M8: `p64_stream` (docs/architecture.md section 14): one listener task selecting on
  the DDP (4048) and raw p64 (4064) sockets, host-tested parsers and assembly by byte
  offset (`protocol.cpp`), RGB565 and indexed conversion, scaling by the artwork rules,
  the latest-frame `FrameSource` that samples the freshest frame 2 ms before each 60 fps
  slot, the silence timer and the StreamStarted/StreamEnded events. The show routes every
  source through `present()`: a stream takes the panel (takeover on, or the Stream
  state; after the boot animation, never over the pairing screen) and the state keeps
  running invisibly, its sources parked as "behind" until the stream ends. The raw
  format's field layout is in docs/api.md. `tools/stream_send.py` sends a test pattern,
  any image or animation Pillow opens, or the PC screen (mss) over either protocol.
  `CONFIG_LWIP_UDP_RECVMBOX_SIZE=48` (a 128x128 DDP frame is 35 datagrams in a burst).
- Verified on the device (`tests/device/stream_smoke.py`, 0 failures): raw RGB888 64x64,
  RGB565 32x32 (upscaled 2x, bit-replicated values), indexed 64x64 with a palette,
  RGB888 128x128 (box-downscaled), chunks sent in reverse order, DDP 64x64 and 128x128,
  all pixel-exact against the panel frame; 30 fps measured at 30.0 fps; the takeover and
  the return after the silence timeout; takeover off (frames counted, panel kept); the
  Stream state's waiting screen before and after a stream. Latency from the last chunk to
  the player taking the frame: 0.4 ms at 30 fps (7.6 ms at 60 fps, where the source waits
  for the next slot on purpose). Stress: 64x64 at 60 fps 5 s, no loss; 128x128 at 30 fps
  (1.5 MB/s, 36 datagrams per frame) about 1 % of datagrams lost on this Wi-Fi so 145 of
  150 frames complete; 64x64 at 120 fps sampled to the panel's 60 fps with 0.8 % loss.
  Internal RAM with the listener up and MQTT connected: 23-24 KB free, largest 17 KB.
- Found on the device and fixed: a "last" chunk arriving first (chunks reordered) ended
  the frame with holes; completeness is now "every byte in" and the flag informational.
  With the M6 Wi-Fi trims (24 dynamic RX buffers) a 36-datagram burst lost 4-5 % of its
  datagrams and every third 128x128 frame; 64 dynamic RX buffers (PSRAM, no internal
  cost) lose none at 10 fps bursts.
- M9, first part (docs/architecture.md section 15): `system::reliability` (reset
  reason, per-cause reboot counters in NVS, the core dump summary, image confirmation
  deferred to 30 s after boot so a crashing update rolls back), `system::rtc` on the
  new shared `system::i2c_bus` (the PCF85063A seeds the clock at boot, NTP and manual
  sets write it back), `system::night` (pure: the effective brightness rule) applied by
  `main/ops` on a 15 s timer, the factory reset (API with the confirmation word, and
  BOOT held 10 s at power-on with a 3-2-1 countdown screen), `set_time` for browsers
  without internet, the task watchdog on the show loop, the loader and the stream
  listener. Status gains `time.source`, `panel.night_active` and `reliability`.
- Verified on the device (`tests/device/ops_smoke.py`, 0 failures): the RTC gives the
  time 1.5 s after boot (NTP re-syncs 2 s later), reset reasons and counters persist
  across reboots (a flash counts as `usb`), a core dump left by an earlier crash was
  summarised and erased through the API, the night window sets brightness 40 and
  "panel off" blanks, the ceiling caps outside the window, the factory reset refuses
  without the word. Not exercised: the BOOT hold (nobody at the button) and the reset
  itself (it would unpair the development device).
- M9, IMU (docs/architecture.md section 16): `p64_inputs` with the QMI8658 sampler,
  the host-tested `TapDetector` (impulse shape, double window, lockout) and
  `OrientationTracker` (calibrated reference, hysteresis, hold when flat), the
  `calibrate_upright` action, `diag/imu`, `inputs` in the status, settings
  `inputs.tap_enabled`, `inputs.tap_sensitivity` and `display.rotation_auto` applied
  live. `tests/device/imu_smoke.py`.
- Verified on the device: the IMU answers at 0x6B, 250 Hz sampling with no read errors,
  gravity 1.01 g along +X with the panel upright at rotation 90, a noise peak of 0.004 g
  at rest (the default tap threshold is 1.5 g), calibration and auto mode resolve to the
  current rotation and keep it. Not verified (needs a hand on the shell): that a real
  knock registers as a tap at sensitivity 5 (watch `peak_g` in `diag/imu` while knocking
  and adjust the sensitivity), and the direction of auto-rotation (turn the panel 90
  degrees clockwise after calibrating; if the picture turns the wrong way, set
  `P64_IMU_ROTATION_SIGN` to -1).
- M9, PIN (docs/architecture.md section 17): the HTTP server's route gate (every route
  behind a trampoline unless registered open), `web/auth.cpp` with the salted hash in
  NVS, session cookies and bearer tokens, the `X-P64-Pin` header for scripts, five
  failures locking for 30 s; the UI's PIN prompt on a 401, the PIN card, plus the
  security-and-inputs card (tap gestures, sensitivity, auto-rotation, calibration,
  set time from the browser, factory reset with the typed word) and the reliability
  line in the diagnostics. `tests/device/pin_smoke.py`.
- Verified on the device (`tests/device/pin_smoke.py`, 0 failures): PIN validation, 401 on
  every route without a session while `/`, `/api/v1/auth` and the portal stay open, the
  `X-P64-Pin` header, the cookie and the bearer token, five wrong PINs locking for 30 s
  (429 with Retry-After) while an open session keeps working, changing the PIN needs the
  current one, clearing reopens the routes. Found on the way: the HTTP server's handler
  table (64) was full, so the last routes registered never existed (now 96); a
  `Retry-After` header set from a temporary string went out empty (httpd keeps the
  pointer until the response is sent).
- M9, updates (docs/architecture.md section 18): `p64_ota` with the GitHub release
  check (every 12 h and on request), the install through esp_https_ota into the other
  slot with the SHA256 read back from flash, installs from any URL with a supplied
  checksum (plain HTTP allowed on the LAN), rollback, the Update card in the UI,
  `tools/release_assets.py` for the two release assets, host-tested version rule.
- Verified on the device (`tests/device/ota_smoke.py`, 0 failures): the GitHub check
  reports the missing release cleanly (404 on the empty repository), an install of the
  PC's build from a plain-HTTP URL with its SHA256 took 20 s for 1.9 MB, the device
  rebooted from `ota_1`, confirmed the image 30 s later, offered the rollback and came
  back on `ota_0`. Internal RAM is only borrowed during the job (the worker task is
  created per job).
- M9 is complete. Not exercised on the device: a real GitHub release install (none is
  published yet; the check against the empty repository reports the 404 cleanly), the
  BOOT-hold reset, real taps and the auto-rotation direction (a hand on the shell).
- M10, web UI (docs/architecture.md section 19): the dev page is replaced by p3a's
  layout: Home (live preview, now-playing card with channel rows and the shuffle toggle,
  transport with like and info, playset pills, "Play from..." with upload to a folder
  and URL or Makapix link, banners), Playsets (list, built-ins, the editor with the
  balance bar, channel cards with reorder, the add-channel dialog with Local folders
  and the Makapix kinds with browser-side checks), Settings (Display, Widgets, Stream,
  Network with the PIN and the time, Storage with the file manager, Makapix, System with
  diagnostics and about) and Update. Shared `static/app.js`; p3a's `common.css` and
  `theme.js` (five themes) verbatim; ETag revalidation; PWA manifest and icons.
- Verified on the device (`tests/device/ui_smoke.py` 0 failures; Chrome on the LAN):
  all four pages render in the Spectrum theme with the live preview, the now-playing
  card (channel rows, shuffle), the pills, the built-ins list, the settings tabs and the
  Update page (the rollback slot shown); the PIN prompt path is the same API the
  tests exercise. Page load: 29 KB HTML plus 38 KB CSS, revalidated with an ETag.
- Found on the device and fixed, the important one: opening the web UI crash-looped
  the device (22 panics). The core dump named the WebSocket push task: its stack lives
  in PSRAM, and the status document it builds every 2 s had gained (M9) the reboot
  counters read from NVS and the other slot's description from flash; a flash read
  turns the cache off and asserts on a PSRAM stack. Fix in two layers: those values
  are cached in RAM at boot, and `system::on_internal_stack()` (flash_guard) now wraps
  every NVS access (settings, state, Makapix credentials, the PIN), running it on a
  short-lived internal-stack helper when the caller's stack is in PSRAM. With that in
  place the Makapix worker (10 KB) and the sensor task moved to PSRAM stacks; the
  system event task (132 bytes of headroom) and the TCP/IP task got room; cJSON
  allocates in PSRAM; the ETag carries the build time (a dev rebuild with the same
  version served the browser's cached old page); lwIP sockets 16 -> 24 (12 for the
  server, a browser holds six plus the WebSocket). Internal RAM: 21 KB free at boot,
  23.5 KB with the Home page open (was 9 KB before the changes).
- M10, second part: the setup portal restyled on the shared stylesheet (it serves
  `/static/common.css` in setup mode too), the weather city search (Open-Meteo
  geocoding from the browser, a pick saves the coordinates), the card format action
  (`POST /api/v1/files/format` with the word, the Storage tab button), and
  `tests/device/soak.py`: the unattended acceptance run (spec 18.1 and 18.11 in
  miniature: a playset at a short interval under a status and preview poll, watching
  reboots, panics, watchdogs, panel timeouts and stalls, late flips and the heap floor).
- Verified on the device: `soak.py --minutes 10 --interval 5` on Promoted with a status
  and preview poll every second: 120 swaps, 7 798 frames presented, 0 late flips, 0
  panel timeouts, no stall, no reboot, internal heap floor 11.8 KB (20.5 KB typical),
  0 poll errors. The portal page, the city search and the format refusal checked over
  HTTP; the format itself was not run on the development card. A second soak of 45
  minutes on the committed build: 540 swaps, 31 879 frames, 0 late flips, 0 timeouts,
  no reboot, heap floor 13.8 KB (23 KB typical), 0 poll errors.
- 2026-09-20, the analogue clock face (`p64_widgets/src/analogue.cpp`, host-tested
  geometry and pixels), settled with the user: ticks with the cardinal four longer,
  the numerals 12, 3, 6 and 9, crisp Bresenham hands (hour 2 px, minute 1 px, second
  hand in the accent colour with the seconds setting), a hub, the date under the
  centre. The Settings page gained the face selector. Captured through `/api/v1/frame`
  on the device at 13:14 and 13:17: hands, ticks, numerals and the date where the
  design puts them; the second hand in the accent colour moves once a second. Found
  on the way: a settings change while a widget was up did not redraw it until its next
  frame (a minute for the clock); the show now restarts the widget's source on any
  settings change in the Widget state.
- 2026-09-20, panel modes reworked after the user's bug report (Photo mode posterized
  and dim; a switch back to Quality left the panel dark for good, and no button rescued
  it). Two causes, both in the driver patch. (1) Photo was ten planes at transition
  bit 6: planes 0, 1 and 2 all got one-clock output-enable windows, the plane weights
  stopped being superincreasing and the LUT fit's monotonic walk got stuck at code 3,
  so every input above 13 mapped to code 3 (3 distinct output levels instead of 229;
  reproduced from the driver's logic on the host). (2) Every switch freed and
  re-allocated both descriptor chains from internal DMA memory (2 x 13.8 KB for ten
  planes); after hours of uptime with the Makapix session up the largest free block
  was 21 KB, the second allocation failed with the DMA already stopped, the driver kept
  the new rate as "in force" so a retry was a no-op, and nothing restarted the DMA
  (the device was found in exactly that state: `stalled`, `dma_moving` false,
  largest block 21.5 KB; seven switches on a fresh boot worked, which fits an
  allocation failure rather than a timing bug). Fix, settled with the user: Photo is
  now the spec's 8 planes at 813.8 Hz (transition bit 4, windows 1 3 7 15 31 62 62 62,
  179 distinct codes, about 74 % of Quality's light), through a runtime plane count in
  the driver (`set_refresh_profile`); the descriptor arrays are allocated once at boot
  and rebuilt in place, a profile that would not fit is refused before the DMA stops,
  a failed rebuild restores the previous profile and restarts; a plane that would
  weigh less than the planes below it is blanked so the LUT fit always holds; the
  display repaints the last picture into both buffers after a switch (the buffers
  held codes from the previous LUT). Quality stays at 271 Hz: transition bit 3 would
  need two 25.7 KB chains in internal RAM. The user's observation that Quality mode
  photographs without banding is expected (a 1/100 s exposure spans nearly three
  refreshes), not a sign of low tonal quality. Verified on the device:
  `tests/device/panel_mode_smoke.py`, 12 switches at 1.5 s plus a five-request burst,
  every switch 34 to 55 ms, 813.8 / 271.3 Hz, frame lock kept, 0 timeouts, no stall,
  largest internal block unchanged (22.5 -> 21.5 KB, system noise). Open: the picture
  in Photo mode by eye and a camera measurement of the refresh.
- 2026-09-21, the frozen artwork: the user's device had shown the same artwork for
  minutes with `state: widget` and `auto_swap.remaining_s` -1. Diagnosed from the log
  ring without a reset: the state had been set to Widget on the Settings page, then the
  Promoted pill on Home (and later two taps) played artworks, since none of the artwork
  paths looked at the main state while `tick()` returned before the timer whenever the
  state was not Animation show; the clock overlay drew over the artwork too. Fix, settled
  with the user (every artwork request switches the state, from Stream as well, persisted,
  plus the timer and overlay guards and a device test): `enter_animation_show()` in
  `show.cpp`, `swap_timer_runs()`, the overlay hook checks `show_active()`, and the
  internal artwork paths (scan, prepared pick, Makapix change) are gated on the show, which
  also closes a latent path where a card rescan or the boot restore could play an artwork
  in Widget state. `tests/device/widgets_smoke.py` now covers the pill and Next from the
  Widget state; `soak.py` restores the playset before the settings.
- 2026-09-21, the maximum artwork size: a Makapix setting (`makapix.max_size`, 32, 64,
  128 or 256 pixels per side, default 128, a select on the Makapix tab). Settled with the
  user: channels only (play-this, the site's commands, URLs and the card stay free up to
  the 256x256 canvas), both sides within the limit, server criteria plus a play-time skip
  as the safety net, and a change refreshes every channel. The paired `query_posts` takes
  `width`/`height` `lte` criteria (`reference/makapix/docs/player/querying-artwork.md`);
  the anonymous promoted feed has no size filter (`/api/feed/promoted` only takes
  `fields`, `limit` and `cursor`), so `refresh_step()` drops the oversized entries of every
  page (counted in the refresh log line) and `snapshot_makapix()` in `show.cpp` leaves them
  out of the pickable list. `tests/device/makapix_smoke.py` checks the snapping and that
  the artworks Promoted plays fit the limit. Verified on the paired device: the Promoted
  index of 292 entries re-walks to 229 at 128 (5 pages) and to 136 at 64 (3 pages), the
  server honouring the criteria (0 dropped on the device), each re-walk within 10 s of the
  setting write; `makapix_smoke.py --paired` passes; internal heap unchanged (21.8 KB
  free). Found on the way: a walk interrupted by a playset switch (the channel leaves the
  playset mid-walk, so the worker stops serving it) resumed nine minutes later on its
  kept-alive connection and failed with `ESP_ERR_HTTP_WRITE_DATA` (the server had closed
  it), costing a 30 s retry; `refresh_step()` now starts a walk over when its last page is
  more than 60 s old (`kWalkIdleUs`); verified by switching to Local 1.5 s into a walk and
  back 75 s later: "refresh paused too long; starting over", then 292 entries in 6 pages
  at 256, no failure.
- 2026-09-21, live channel counts in the web UI: the user tapped Followed on the Home
  pill and the per-channel counts stood still until a reload. Two causes: the WebSocket
  push did not listen to `MakapixChannelChanged` (an index refreshed, an artwork cached),
  and the Home page refetched `/api/v1/channels` only on a playset name change or a card
  scan. Settled with the user: change counters in the status document rather than
  per-channel counts in every push. `playback.playset.version` (show.cpp, bumped in
  `install()` and `on_makapix_changed()`, after the change is applied, then
  `web::notify()`) and `playsets_version` (api.cpp, bumped and pushed on
  `PlaysetsChanged`, `MakapixStateChanged`, `CardMounted`, `CardFailed`,
  `LocalFilesChanged`). The Home page refetches the channels when the first moves, at most
  every 2 s, and the playsets when the second moves; the Playsets page reloads its list
  and folders on the second. `api_smoke.py` checks both fields. Verified in Chrome on the
  device: with Followed open, lowering `makapix.max_size` to 64 took the pill from 304/304
  to 135/135 to 102/102 with "just now" stamps, restoring 128 climbed 103/264, 107/271,
  130/304, no reload; internal heap 30.9 KB free at boot, unchanged. Found on the way and
  fixed: a saved Followed playset never came back after a reboot, because `restore()`
  queues the Followed job at 1.7 s and the fetcher answered "offline" (Wi-Fi connects at
  4 to 6 s), leaving an empty playset and "no artwork: empty"; the fetcher now parks an
  offline Followed job and runs it once online (log: "Followed requested offline; parked
  until the network is up", then "activating playset Followed (5 channels, from the
  site)" at 7.7 s). Observed, not changed: after a 64 -> 128 round trip the artists'
  cached counts restart low (23/184) and climb, so the entries dropped at 64 lose their
  cached flag and download again even when the file is still in the cache.
- 2026-09-21, the cache sweep (spec 5.4 rewritten, ADR 0010): nothing ever deleted cached
  artworks, and the spec's watermark eviction was never built. Settled with the user
  through a grill: a plain age limit (`makapix.cache_retention_days`, 1 to 365, default
  30, a field on the Makapix tab), applied to every file under `cache/`, `downloads/` and
  `channels/`, even one a channel of the active playset still lists (the sweep clears the
  cached flag in every loaded index and the download loop refetches it: churn accepted),
  fired only when the local clock crosses into the night window while the schedule is
  enabled and the clock synced (no catch-up, nothing persisted; a wrong-clock mtime before
  2026 counts as old), stale flags of a returning channel left to the fail-at-playback
  path. The loader touches a cache file's mtime after every successful read ("last
  played"; ESP-IDF's FAT VFS implements `utime`). `POST /api/v1/diag/cache_sweep`
  (`older_than_s`, `dry_run` default true) runs it on demand; the Makapix status gains
  `cache {files, bytes, last_sweep, last_deleted, last_freed_bytes}`, shown on the
  Storage tab; `tests/device/cache_sweep_smoke.py [--delete]` covers the setting, the
  counters, a dry run, the touch and (with `--delete`) a real sweep of files not played
  in the last hour followed by the re-download. Verified on the device on 2026-09-22
  (`cache_sweep_smoke.py --delete`, 20 checks, 0 failures): a dry run walked 553 files
  (67 MB) in 5.2 s; the real sweep of files not played in the last hour deleted 550 of
  them (66 MB, 9 indexes) in 17.8 s and unflagged 529 entries, Promoted went 229 to 4
  cached and was back at 229/229 about eight minutes later on its own; the nightly
  trigger fired 11 s after the local clock crossed into a night window set two minutes
  ahead (30-day retention, 197 files examined, 0 deleted, 2.9 s); internal heap 22.6 KB
  free with the re-download running. The `downloads_cap_mb` setting (Storage tab) is
  not enforced anywhere; the sweep now ages `downloads/` out. Fact that bites: the
  `built` date in the status document comes from ESP-IDF's app descriptor, whose object
  file only recompiles on a full rebuild, so it still reads Sep 19 after incremental
  builds; trust the firmware's behaviour or the flash log, not that date.
- 2026-09-22, the architecture review (`docs/review-2026-09/`, prompt p021): memory, CPU
  and testing discipline audited from the code and measured on the device. Internal RAM
  is the one resource in trouble (13 to 23 KB free, low-water marks of 939 and 1 055 bytes read
  within the first fifteen minutes of two boots); CPU is not (core 0 3 to 5 % busy, core 1 9 to 44 %); the pure
  layers are well tested and the state and timing code is not. Two configuration
  experiments were flashed and measured: the always-internal threshold at 1024 plus the
  Wi-Fi and FreeRTOS code moved out of internal RAM took the device from 13 KB free and an
  11.7 KB largest block to 54-58 KB and 32.7 KB, with pacing unchanged; `proposals.md`
  ranks that first, then the pure-core split of show, renderer, player and fetcher, then
  an HTTPS command channel in place of MQTT over mTLS. The per-task run time and heap
  attribution added to `diag/memory` for the measurements live on the unmerged branch
  `review/cpu-instrumentation`. The device was flashed back to the committed build.
- 2026-09-22, tier 1 of the review roadmap (prompt p022, `docs/review-2026-09/tier1-results.md`):
  `sdkconfig.defaults` sends allocations above 1 KB to PSRAM and moves the Wi-Fi driver's
  hot code, the FreeRTOS kernel and the ring buffer to flash, pins the MQTT task to core 0
  and turns the run-time statistics on; the show loop sets its priority to the documented
  5 (it ran at 1); `system::settings_view()` gives the overlay hook, the widget sources
  and the stream sink the settings without a copy, and `settings_update()` writes NVS
  outside the settings mutex; `Display::health()` compares the descriptor pointer across
  calls instead of busy-waiting 200 us under the driver mutex; `wifi.cpp`'s NVS goes
  through the flash guard. `budgets.json` holds the floors, checked by `api_smoke.py`
  (steady state), `soak.py` (the floor and core 0's share over a run) and
  `tools/check_size.py` (the image and the static internal RAM); `tools/cpu_sample.py`
  prints per-task CPU shares from `diag/memory`; CLAUDE.md has the rule that tests
  travel with the code. Measured on the device: internal RAM 75 to 80 KB free with a
  45 to 47 KB largest block (13 to 23 KB and 12 to 20 KB in the morning), static DIRAM
  120 083 B (150 127 B), image 2 078 240 B; throughput through the HTTP server not worse
  (RX 4.9 Mbit/s, TX 3.3 Mbit/s against 3.7 and 3.4); `panel_mode_smoke`, `ota_smoke`,
  `api_smoke`, `ops_smoke`, `widgets_smoke`, `stream_smoke` (0 incomplete frames) and a
  45-minute soak with the browser poll (529 swaps, 32 619 frames, 0 late flips, 0
  timeouts, heap floor 59 243 B, core 0 busy 7.8 %) all pass; host tests 86 files exact.
  Found on the way: two stream runs at RSSI -70 dBm lost most 128x128 bursts and an A/B
  with the Wi-Fi IRAM options restored cleared them of blame (the loss was the radio);
  `stream_smoke`'s "30 fps measured at 0.0 fps" is the first mDNS resolution of the test
  process taking up to 3 s, so timing-sensitive tests take the IP (README). The
  boot-time heap minimum still varies between 10 and 70 KB from boot to boot (the second
  TLS handshake next to the MQTT session), which proposal P-M1 addresses.
- 2026-09-22, steps 3 and 4 of the review roadmap (prompt p024,
  `docs/review-2026-09/steps3-4-results.md`): the host tests run on doctest (56 named
  cases in `tests/host/unit/`, 22 180 assertions, `run.py --tc/--junit/--werror/--sanitize`);
  GitHub Actions runs them with ASan, UBSan and `-Werror` and builds the firmware with the
  size budgets on every push (`.github/workflows/firmware.yml`, green); warnings in p64
  code are errors in the firmware build. Pure files with host tests: `timing.hpp` (the
  player's timeline, the renderer's schedule), `settings_model.cpp`, the Makapix
  `contract.cpp`, `main/show_rules.cpp`, plus six files that were pure but never compiled
  on the host. Found: the renderer's copy-lead average truncated and let the schedule
  slip a refresh period about once in 1 760 frames (now rounded up); a passed-through
  POSIX time zone rule with '/' is refused (recorded, not changed). New device test
  `timing_smoke.py` (spec 18.4): 24.93, 10.04 and 60.01 fps where 25, 10 and 60 are
  due, no late frame. Device: timing, makapix --paired, widgets, stream, content, api
  and ops smoke tests pass.
- 2026-09-22, step 4 of the review roadmap completed (prompt p025,
  `docs/review-2026-09/step4-results.md`): the show's whole logic is `main/show_core.cpp`
  behind the `ShowEnv` interface with its state in one `State` (`show.cpp` is the shell),
  driven on the host through nine scenarios by a fake env; the Makapix worker's decisions
  are `policy.cpp`; the site's commands and the MQTT payloads, the OTA release document,
  the PIN's rules, the widget faces and the driver's plane arithmetic (`p64_bcm.h`) are
  pure files with host tests; `tests/device/ws_smoke.py` holds the WebSocket push open.
  98 host cases, 128 079 assertions. Found and fixed: the LUT fit still collapsed when a
  plane was blanked (the fix of 2026-09-20 kept the weights in order but not the fit's
  walk; latent, the profiles in use never blank a plane and keep their 229 and 179
  codes); the cache sweep deleted files whose mtime was up to a day in the future (an
  unsigned wrap). Device tests fixed for state assumptions (content, makapix,
  panel_mode). Every device test passes on the final build.
- 2026-09-22, an empty channel told apart from a missing listing (prompt p028): the
  Followed artist @Sendew (`Rxn`) read "0/0" with the status "no listing yet" although
  its refresh had landed without error; the site reports 0 posts for that artist. The
  status is now "no artworks" when a listing landed empty and "nothing fits N px (M too
  large)" when everything listed was over the size limit (`oversized`, a new RAM-only
  field of `/api/v1/channels`); the Home page shows the status on the row, dimmed for an
  empty channel, orange only for real obstacles. Host case extended (`show: channel
  status texts`); verified on the device (the row reads "0/0 no artworks 1 h ago");
  ui, content and api smoke tests pass. The "nothing fits" text is host-tested only.
  Note: `content_smoke.py` leaves the Local playset active.
- 2026-09-23, the time is trusted only from NTP (prompt p032, ADR 0011): the RTC driver
  and codec are gone (no battery on the board), so is `POST /api/v1/action/set_time` and
  its button. SNTP runs four slots through the thread-safe `esp_sntp_*` API: the router's
  server from DHCP option 42, the setting, `time.google.com`, `time.cloudflare.com`; a
  re-sync every 6 h; the slots are repaired on connect, disconnect and a 30 s tick (lwIP
  clears slots 1..3 on every DHCP ACK). ESP-IDF's weak `sntp_sync_time()` is replaced so
  an answer before the build date never reaches the clock (the link map shows p64's
  definition, lwIP's weak one discarded). The sweep reads every date first and deletes
  nothing when a file is more than a day in the future; the implausible-date floor is the
  build date minus 367 days. New pure `time_rules.cpp` with host tests (104 cases).
  Verified on the device: the router (192.168.4.1) offers NTP and took slot 0, the time was
  trusted 3.4 s after the IP (6.6 s after boot) and Makapix started only then; status
  shows the servers and their reachability; `ops_smoke.py` (rewritten for NTP-only time,
  including a setting round trip) and `api_smoke`, `widgets_smoke`, `cache_sweep_smoke`
  pass; a dry-run sweep walked 440 files in 9.1 s (two passes). Stack headroom after:
  `esp_timer` 1456 bytes (from 1696), `events` 3152 (from 4592). Not exercised on the
  device: a refused answer, a network without DHCP NTP, a DHCP lease renewal (all pure,
  host-tested).
- Remaining: the acceptance measurements that need instruments (camera at 240 fps, a
  power meter), a 12 h and a 24 h soak (`soak.py --minutes 720` when the device can be
  left alone), and the hands-on checks (taps, rotation direction, BOOT hold, the Photo
  mode picture).
