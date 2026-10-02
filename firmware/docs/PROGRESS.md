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
| M7 | Widgets: fonts pipeline, clock overlay, clock, weather, temperature, interludes | done (analogue face added 2026-09-20; six themed faces 2026-09-26; the LED face in five styles 2026-09-28; interludes as a median gap, ADR 0014, 2026-09-28) | 2026-09-19: SHTC3 read, Open-Meteo fetched, clock/weather/temperature frames captured, overlay on artworks, interludes in history |
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
- 2026-09-23, clock overlay fonts (prompt p033): five bundled fonts, the regular cut of
  each (`tools/gen_fonts.py`): Capital Hill 6 px, Everyday Slight 5 px, Everyday Standard
  6 px, Everyday Typical 7 px (the former `everyday`, renamed `everyday-typical` with its
  folder, no mapping of the old name: it draws the default) and High Birth 9 px, which is
  offered for the Clock widget only (`Font::overlay`). Extra Thick is in `assets/fonts` but
  not bundled: HH:MM is 66 to 69 px wide at its 17 px. `GET /api/v1/fonts` lists them and
  both font menus of the web UI are built from it. Bug fixed: the overlay's redraw key
  left out the font, so on a still artwork a font chosen in the UI showed only at the
  next minute; the overlay's key and drawing moved into the pure `faces.cpp` with host
  tests (every setting that shapes it changes the key). New setting
  `show.clock_overlay.outline` (default on) and a colour picker for the overlay on the
  Settings page. The Clock widget draws the date in the default font when the chosen
  font's date does not fit (High Birth's is 79 px) and the analogue numerals in it when
  the font is taller than 7 px. 107 host cases. Verified on the device: the font list, the
  four overlay fonts over an artwork, the outline off, a coloured bottom-right overlay,
  High Birth on the digital and analogue faces (frames read back through
  `/api/v1/frame`), the menus filled on the Settings page; `widgets_smoke` (with the new
  font-change check: 30 vs 58 overlay pixels within 1.5 s) and `ui_smoke` pass.
- 2026-09-23, the clock overlay's per-frame cost: measured on the device with temporary
  logging in the player (US/Eastern, animated WebP): the overlay cost about 380 us per
  frame, 145 us in the key (the settings and `local_time_at`, 87 to 94 us once the decode
  has evicted the cache, 31 us warm) and 230 to 250 us in the drawing, which converted
  the time again and redrew the glyphs and the outline (outline 46 us warm); decoding the
  same frames took 2.7 to 24 ms. Now the key converts the time once and, when it changes,
  draws the overlay into a cached `faces::OverlaySprite` (a mask over the panel, 4 KB in
  PSRAM, built by the new `gfx::fonts::draw_mask`); `draw_overlay()` only stamps it.
  Re-measured the same way: drawing 44 to 58 us, key unchanged at 134 to 150 us, so about
  190 us per frame; the rebuild at each minute takes the key to 620 to 690 us once. Host
  tests: the stamped overlay equals the direct drawing for every font, corner, outline and
  12/24 h setting, and the mask equals the frame drawing at scales 1 to 3 (108 cases).
  `widgets_smoke` and `api_smoke` pass (internal heap 81 KB free). Left: converting the
  time once a minute instead of once a frame (most of the 140 us that remain).
- 2026-09-23, system fonts, Setup and Update screens, overlay border (prompt p034): Everyday
  Ample 9 px bundled (six fonts; offered for the overlay too). The firmware's own text
  moves to the system fonts, `gfx::fonts::system_font()` (Everyday Standard) and
  `system_large_font()` (Everyday Ample), in sentence case: `status_screens.cpp` is
  rewritten over a small stack layout (lines centred as a block inside the screen's
  border, text wrapped by pixel width, hostnames broken after '-' or '.', the hostname
  dropped from "stream waiting" when the IP needs two lines) and the built-in 5x7 font
  (`gfx::text`) is gone. Two screens the spec listed but the panel never drew: Setup
  (pages every 3 s: "Wi-Fi setup", "Join p64-setup", "Open 192.168.4.1"; it holds the panel
  when no network is saved, and with a saved network only replaces "no artwork", so
  artworks keep playing from the cache; `rules::setup_screen`, `wifi::Status::network_saved`
  from the RAM copy) and Update (version and progress bar while downloading and
  verifying, "Update ready / Restart to run it" until the reboot, "Update failed" and the
  reason for 10 s after a failed install, nothing after a failed check; the show follows
  `ShowEnv::update_state()` in `tick()`; nothing else takes the panel meanwhile, and
  streams wait). Clock overlay: `outline` becomes `border` with `border_colour` (default
  black) and `border_opacity` (1..255, default 255, blended by the sprite; the text stays
  opaque); the Settings page has the toggle, a colour picker and an opacity slider. Host
  tests: every screen inside its border with long names and addresses, the stack of
  setup and update scenarios through the fake env, the border blending and the sprite
  against the font drawing (114 cases). Verified on the device: `widgets_smoke` (border
  colour and clamp), `api_smoke`, `ui_smoke`, and `ota_smoke` with the Update screen
  captured from `/api/v1/frame` during a local install (15, 35, 89 %, verifying). Not
  seen on the device: the Setup screen (needs Wi-Fi erased), the pairing and no-artwork
  screens in their new fonts (host-rendered and checked by eye only).
- 2026-09-23, clock overlay positions: `corner` also takes `top_center` and
  `bottom_center` (text centred horizontally, same 2 px from the top or bottom edge);
  the Settings page's "Position" menu lists all six. Host tests: the sprite against the
  font drawing at all six, the settings round trip; `widgets_smoke` checks top center
  on the device (columns 20..42); bottom center seen on the panel over an artwork.
- 2026-09-24, content providers and the private area (ADR 0012, p036): every channel
  that is not a local folder now belongs to a `content::Provider`
  (`p64/content/provider.hpp`, registry in `p64_content`); Makapix Club is the first
  (`p64_makapix/src/provider.cpp`, items are post ids resolved to the cached file at pick
  time) and the show's eight "Makapix or local" branches became one provider path
  (runtime, pick, load failure, shown and hidden reports, install, change handler,
  channel JSON, history's `provider` and `item_id`). New kind `external`
  (`<provider>:<channel>`), `providers` in the status document, the Playsets page offers
  a provider's channels from it, `ProviderChannelChanged` replaces
  `MakapixChannelChanged`, `ShowEnv` lost the Makapix content methods (the pairing
  screens and Followed stay). The private area: `firmware/private/` is the separate
  `p64-private` repository, git-ignored, discovered by CMake (components,
  `sdkconfig.private`, `+private` version, strict warnings), `main.cpp` calls
  `p64::priv::start()` under `CONFIG_P64_PRIVATE`, `run.py` reads its manifest, the OTA
  status carries `private_build` and the Update page warns. Host tests: the show core
  with a fake provider (items, a failed item reported and dropped, labels, the size
  limit, an unserved provider, missing credentials), the registry, the `external` kind
  and its JSON (118 cases).
- 2026-09-24, provider hooks: `Provider::settings_path()` (published in `providers`, the
  Settings page's System tab lists every provider with a Settings link when set) and
  `Provider::erase_credentials()` (called by the factory reset); the Playsets page's
  External channel select gains "Other" for identifiers a provider does not offer by
  name; `run.py` accepts globs and `system_includes` in the private manifest. Host tests
  for both hooks.
- 2026-09-24, playset documents: a channel's default display name written into the
  document no longer comes back as a chosen name on parse, so a provider's own label for
  an external channel applies after a round trip through the store (seen on the device:
  the channel view named an external channel by its identifier). Host test in
  `content.cpp`.
- 2026-09-26, clock overlay: in 12 h mode the overlay appends AM or PM (1x, 2 px after
  the time, as the digital clock face does). Reported as "switching the overlay to 12 h
  does nothing": the setting saved and the drawing followed it (verified on the device
  with the time zone set to UTC for a minute: "4:20" against "16:20"), but the switch
  was made at 12:18, and 10, 11 and 12 o'clock draw the same digits in both modes, so
  nothing visible changed and the 12-hour overlay never said which half of the day it
  was. Spec 6.1 amended; the web UI's option reads "12 h (AM/PM)". Host tests in
  `widgets.cpp` (12 h differs from 24 h at every hour, the marker fits every overlay
  font and corner, the cached sprite matches the font drawing) and `decode_gfx.cpp`.
- 2026-09-26, provider contract: a provider's post-load check of its cached files is
  informative, not a gate. Found with the second provider (private area): after every
  boot it offered only the entries its file check had passed, published nothing while
  the check crawled, and a channel with every artwork on the card read "downloading"
  with nothing available for half an hour (the show's copy was taken at the load and
  never refreshed; the UI's "cached" count rose because the status document snapshots
  the provider on every request). `ChannelSnapshot` carries `unchecked` (entries the
  check has not reached), `/api/v1/channels` and the show's channel runtime carry it,
  the UI shows "checking files (N left)" as a pulsing hint on a channel that keeps
  playing, and `channel_status` says "checking files" (blue on the panel) for a
  provider that does hold entries back, instead of "downloading". Host tests in
  `show.cpp` (the rule and a core scenario: a channel with a check running is available
  and plays; the count drops to 0 when it ends). Verified on the device: the channel is
  available the instant its index loads, the check ends 30 s later in the background.
- 2026-09-26, time-to-first-artwork (prompts p044, p045): measured before anything was
  touched, the first artwork went up 18.2 s after a reset, with the Local playset as with
  a Makapix one, against the spec's 3 s. Three causes, from the boot log: (1) the
  "connected" status screen: the IP landed during the boot animation, nothing was
  "playing yet", so the screen took the panel for its 15 s and the artwork that was
  prepared by then waited (since M6; acceptance 18.8 had been failing unnoticed);
  (2) a Makapix channel's index was read from the card only inside the fetcher loop,
  which waits for Wi-Fi and NTP, so a cached Promoted playset could not play before the
  time was trusted, and never played at all without a network (the playsets document
  claimed it "plays from its cache when offline"); (3) before the app even started, the
  PSRAM memory test cost 0.8 s and the bootloader's image hash 0.6 s (2.5 MB read in the
  slow single-line mode OPI flash leaves the bootloader). The "last played" touch was
  never the gate: the loader already plays any file and only skips the touch while the
  clock is untrusted. Done: the connected screen is decided at the boot animation's end
  by `rules::connected_screen` (skipped when anything is up or on its way, else shown,
  and an artwork that becomes ready replaces it after 2 s); the "no artwork" reason also
  waits for the animation's end (it used to cut the rings short); the Makapix fetcher and
  the private provider read an active channel's index at once, network or not, and settle
  its refresh age once online (`policy::next_unloaded_channel`, `kNeverUs`); the channel
  status says "offline" before "downloading"; `CONFIG_SPIRAM_MEMTEST=n` and
  `CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON=y` (software resets and the first boot
  after an update still verify, so the rollback stands); the boot animation is 0 (off) or
  1 to 7 s, default 3 s (the user's decision; the web UI field follows); the status
  document carries `playback.boot {animation_end_ms, first_artwork_ms}` and
  `api_smoke.py` checks the gap against `first_artwork_after_animation_ms` (2000) in
  `budgets.json`. Host tests: the rule, two show-core scenarios (the 18 s boot replayed,
  and the IP shown then yielding to a late artwork), the policy helpers, the settings
  clamp. Measured on the device after a software reset: app started at 0.68 s (was
  1.49), card mounted 1.32 s, playset restored 1.43 s, Promoted index from the card
  1.90 s, IP 2.37 s, first artwork 3.78 s with the 3 s animation (2.78 s with 2 s), NTP
  3.9 s; the status reports the artwork 2 ms after the animation's end. A power-on reset
  (the only one that skips the image hash) is still to be timed by hand. Spec 6.4, 10.2,
  15.1, 16 and 18.8 and ADR 0011 amended.
- 2026-09-26, themed clock faces (prompts p043 to p046): six new faces designed as
  64x64 mock-ups first (flip, nixie, horizon, then words, hourglass, orrery), reviewed and
  approved by the user, then implemented. The pipeline: `tools/mock_clock_faces.py` draws
  each face's pixel-art assets into `assets/clock/<face>/` (only when a PNG is missing, so
  hand edits survive), renders the review images under `docs/design/clock-candidates/`
  and writes 32 test references under `tests/host/corpus/clock/` using the firmware's own
  glyph tables and integer arithmetic; `tools/gen_clock_assets.py` bakes the PNGs into
  `clock_assets.cpp` (20 sprites, 68 KB of flash) with a Q14 sine table for the orrery;
  `tests/host/run.py` renders the same 32 moments with the firmware's code and compares
  them pixel for pixel (all 32 exact, the floating-point horizon included). Settings:
  `clock.face` is now an eight-value enum (the `analogue` flag is gone; the API key and
  its old values are unchanged); the seconds setting runs the per-second element (the
  flip's rail, the hourglass's stream, Mercury), the blinking colon applies to the nixie,
  horizon and orrery colons, 12 h mode blanks the leading zero and adds AM/PM; the horizon
  takes the weather location, the zone offset (local minus UTC) and the forecast's cover
  and precipitation from the clock source, 40 N and solar time without a location. The
  flip's minute change is ten 45 ms frames driven by a `ClockState` in the clock source.
  Host tests: the solar model against the almanac (New York 2026-09-26 sunrise and
  sunset, the full moon), the words grid, the flip's state machine, the hold times minus
  the milliseconds, the orrery's arithmetic, the hourglass's sand, 12 h mode, the settings
  round trip. Device (flashed 2026-09-26 16:38, `tests/device/faces_smoke.py`): every face
  draws and the settings echo it; the flip, nixie, words, hourglass and orrery frames read
  back from `/api/v1/frame.raw` are pixel for pixel the host render for the device's own
  minute, and so is the horizon for the weather location (Greenwich, overcast, night at
  16:41 EDT, which is what Open-Meteo's `is_day` said too); the flip's ten-frame change
  was caught live by polling the frame across a minute boundary (7 distinct frames in
  1.1 s at the API's rate); the seconds rail, the hourglass stream and Mercury move with
  the second. Internal heap after cycling the faces 70 KB free, largest 34.8 KB
  (`api_smoke.py` 0 failures against the budgets, `ui_smoke.py` 0 failures). The flash
  cost is 68 KB of sprites; no internal RAM is allocated by the faces (the flip draws its
  frames from a per-pixel function, the horizon from the sprites and floats on the
  stack). Note for the horizon: it shows the sky over the weather location in the
  device's local zone, so a location far from the device shows that place's day or night.
- 2026-09-26, settings page, the Clock section (prompt p047): the section shows only the
  controls the selected face reads (each control declares its faces in a `data-for`
  attribute; the grid reflows; the words face, which has none, says so), the Seconds
  label names what the setting adds on that face (second hand, seconds rail, sand
  stream, Mercury), a one-line description of the face replaces the paragraph, and a
  96 px live preview of the panel (refreshed once a second while the Widgets tab is in
  view, the Home page's mechanism) sits beside the selector with a "Show on the panel"
  button that appears whenever the panel is not on the clock widget. `ui_smoke.py`
  checks the pieces. Device (flashed 2026-09-26 17:00, checked in Chrome on the device's
  own page): each of the eight faces shows only its controls and its line (the words face
  its "no settings" note, the orrery "Mercury (seconds)"), the thumbnail refreshes once a
  second (it pauses while the tab is hidden, like the Home page), with an artwork playing
  the section says so and offers "Show on the panel", and the button puts the clock on
  the panel and disappears; at a 376 px viewport the section keeps the thumbnail beside
  the selector and the controls in one row without horizontal overflow; `ui_smoke.py`
  0 failures.
- 2026-09-26, p64b rotary encoders, stage B, the probe (prompt p048; the project is
  `docs/hardware/encoders-soldered.md`): the second I2C bus on the GPIO socket
  (`system::i2c_ext_bus()`, IO45 / IO46, `I2C_NUM_1`), the seesaw wire format
  (`seesaw_wire.hpp`, pure) and traffic (`seesaw.cpp`: address, STOP, 250 us, receive),
  the detent and switch tracker (`encoder_model.cpp`, pure: wrap-safe deltas, invert,
  two-sample debounce, 700 ms long press), the 50 Hz poller on core 0 with a PSRAM stack
  (`encoders.cpp`: identify at start, green on knob A 0x36 and blue on knob B 0x37 for
  two seconds; a board lost after ten failed reads, a missing board retried every 5 s),
  `GET /api/v1/diag/encoders`, `inputs.encoders_present` in the status document,
  `tests/device/encoders_smoke.py`, Kconfig `P64_ENCODERS` (on; p64a runs the same build
  with no board answering). Host tests: the wire format and the tracker (`inputs.cpp`).
  Bench (2026-09-26): the A0 bridge soldered on board B, the two boards chained on the
  breadboard through the 4209 and 4397 with the 2.2 k pull-ups, no multimeter, so the
  probe is the check; the keyed plug lands the 4209 as yellow = IO45, blue = IO46,
  red = 3V3, black = GND. First power-up (2026-09-26): `flash.ps1` enters download mode
  with the chain attached and IO46 pulled high, so the strapping question is settled and
  the chain stays connected for good; knob A answers at 0x36 (hardware id 0x55, not the
  0x87 of Adafruit's header), its NeoPixel lights green at boot, 0 read errors; knob B
  dark and absent from a bus scan (`?scan=1` added for this). Chased on the bench: B's
  ON LED lights, and alone on the 4397 (on either of its sockets) it answers at no
  address with both lines idle high (`sda_level`, `scl_level` added for this), while A
  put back on the same plug answers at once; so the harness and the cables are good and
  board B itself is silent after the A0 bridge; next: the A0 corner under magnification,
  wick the bridge off, see whether B answers at 0x36 again.
- 2026-09-26/27, p64b rotary encoders, stage B accepted on knob A and stage C, the roles
  (prompt p049; board B waits for the solder wick and the multimeter, the resume notes are
  in `docs/hardware/encoders-soldered.md` 5.1). Stage B on knob A: 10 detents clockwise
  counted +10 and 10 back landed on 0, two presses and a long press registered, 0 read
  errors, clockwise positive. Stage C: `knob_rules.hpp` (pure: `role_of`,
  `brightness_after`, about 10 % per detent, 42 detents from 1 to 255), the hooks
  `brightness_step`, `toggle_pause` and `like` on `inputs::Hooks`, implemented in
  `main.cpp` (the brightness to the display at once through the effective-brightness
  rule, the setting written by a one-shot timer 800 ms after the last detent; pause is
  `show::set_paused`; the like on a short-lived PSRAM-stack task), settings
  `inputs.encoders_enabled`, `encoders_swap`, `encoders_invert`, the settings page's new
  Inputs tab (taps and knobs, which boards answer), `ui_smoke.py` and `encoders_smoke.py`
  check them, host tests for the rules and the settings round trip. Device: turning knob A
  dims and brightens the panel at once and its press pauses and resumes (seen by the
  user); a long counter-clockwise turn left the setting at 25 (read back through the API:
  the delayed write works); `encoders_smoke.py` (settings applied and restored, roles
  swapped), `ui_smoke.py` and `api_smoke.py` 0 failures; internal heap 55 to 60 KB free,
  largest 32 KB, with the poll task and the timer. Knob B's roles (next, previous, like)
  are coded and untested until board B answers.
- 2026-09-27, the brightness scale (prompt p051, ADR 0013). The question was the lowest
  brightness: the driver's curve is floored at 17 on this panel (four clocks on the top
  plane, 6.4 % of the full light, about a third of full to the eye) and its windows are
  whole clocks, so the panel had 59 levels, the bottom ones 25 % apart, and 1 to 7 were
  one picture; the knob's 10 % rule walked seven detents through that. Decided with the
  user: the number is perceived lightness (even in the number, light its cube), 1 is a
  sixteenth of the driver floor (0.4 % of full, eight grey levels), the knob steps 7 per
  detent (37 from dark to full), no migration of stored values, spec 3.2 amended and the
  ADR written. Built: `p64bcm::plan_light` and a `scale_q16` on `fit_lut` (the driver
  picks the smallest output-enable level at or above its floor that reaches the share
  and scales the LUT's targets for the rest), `Hub75Driver::set_light` /
  `get_light_plan`, the pure `p64_display/light_curve.hpp`, `Display::set_brightness`
  through the plan, the plan in the status document's `panel` (`light`, `oe_level`,
  `lut_scale`), `knob_rules.hpp` `kDetentStep = 7`, the settings page's texts and a
  night preset "1 (dimmest)", host tests (`bcm.cpp`, `light.cpp`, `inputs.cpp`: 169
  cases) and `tests/device/brightness_smoke.py`. Device (flashed, boot log clean, 229
  codes at full as before): the ladder 1, 2, 8, 32, 64, 80, 128, 192, 255 lands on the
  curve to five decimals; 1 is level 17 with LUT scale 0.0625, 64 is level 17 at 0.77,
  80 is level 21 at 0.93, 128 is level 53 at 0.93, 192 is level 126 at 0.99, 255 is 255
  at 1; the level never drops below 17; `api_smoke`, `panel_mode_smoke` (the plan
  survives a profile switch, largest block unchanged), `ui_smoke` 0 failures;
  `encoders_smoke` reports only board B missing (known); internal heap 51 to 61 KB
  free, largest 32 to 35 KB, no new allocation (the plan lives on the stack). Not
  checked by hand: the knob's steps and the look of the bottom of the scale on the
  panel in a dark room (the user's eye). Later the same day the user read the panel's
  chips: FM6124HJ, a register-less shift register, so the current-gain lever does not
  exist and the FM6126A init sequence is merely ignored (hardware-tests README,
  `enclosure/input/measurements.md`).
- 2026-09-27, knob A's click "does nothing": diagnosed on the device with the user's
  hands, no firmware fault. `GET /api/v1/diag/encoders` showed 15 detents and 0 presses
  in the boot, no read errors, the switch pin steady at released; three clicks with the
  knob cap fitted counted 0, three clicks with the cap off counted 3 and the status
  document's `paused` toggled. The cap, fitted since yesterday's test, was seated on the
  nut and took up the shaft's sub-millimetre push travel. Recorded in
  `docs/hardware/encoders-soldered.md` (5.1, 7, 8) and the measurements file: seat the
  cap with about 1 mm of shaft above the nut and check `presses` after fitting. The user
  reseated the cap that way and the click pauses and resumes the show.
- 2026-09-27, board B with the multimeter and the wick (Klein MM325): the A0 bridge
  conducted with no short (the jumpers' top pads are a shared rail, so A0-to-top-A1 beeps
  by design), pull-ups 19.93 k, alone on the harness 3Vo 3.3 V and both lines 2.88 V,
  scan empty; bridge wicked off cleanly, scan empty again three times with the ON LED lit.
  Board B's chip is dead; a replacement 5880 is on the parts list. Knob A alone until then.
- 2026-09-28, the LED clock face (prompts p052 and p053): a seven-segment clock in VEXED's
  Digital Display font at its native 19 px (`assets/fonts/digital-display`, CC BY 4.0;
  the digits and colon rasterised once into `assets/clock/led/`), hours upper left and
  minutes lower right because HH:MM in one row is 67 px, 5x9 seconds digits, AM/PM/ALM
  indicators, a ghost 8 behind every digit; `clock.led_style` = red, green, amber, blue
  or vfd (the VFD was mocked as a second face and folded in as a style at the user's
  request; the highlight sweeping along the frame was dropped as artificial). Alive all
  the time (decided with the user): a frame every 200 ms for the breathing glow, five
  frames of 40 ms cross-fading every digit change. `face_led.cpp` builds its intensity
  and ghost maps inside the output frame and composes in place behind a three-row
  window, so it allocates nothing. Host: 17 `led-*` references pixel-exact (styles, 12 h,
  blink, fade frames), `themed.cpp` tests for the state machine, the hold times and the
  settings round trip; 171 cases pass. Device (192.168.4.44, flashed 11:14): `faces_smoke`
  0 failures (the LED frame is one of the host's ten frames for the two seconds around
  the capture, the glow breathes, the five styles draw and differ), `ui_smoke` 0
  failures; with the face up, internal heap 65.4 KB free, largest 34.8 KB, core 1 13.1 %
  busy and core 0 1.6 % (`cpu_sample.py`, 20 s), 0 late flips. Sizes: the public image
  2,320,192 bytes, 47 KB more for the baked assets (two 64x64 RGBA frames and the digit
  sheets), so `image_bytes_max` was raised from 2.3 MB to 2.4 MB in `budgets.json`
  (the app partition is 8 MB); the private build is 2,716,368 bytes and has been over
  the public budget since the Divoom provider, `check_size.py` is CI's check on the
  public build. The user's eye on the panel (p054, the same day): the unlit segments
  were too bright on the matrix, whose dark end is lifted, and competed with the lit
  ones; the ghost and the glow of every style are now a third of the first values (red
  ghost (46, 8, 6) -> (16, 3, 2), glow (80, 14, 6) -> (27, 5, 2); the VFD's ghost kept
  just above its glass at (5, 15, 13)), references regenerated and exact, reflashed,
  `faces_smoke` 0 failures again (the LED frame is the host's), heap 66 KB free. A third
  overshot (p055): the ghost was too faint to notice, so the final values are half the
  first ones (red ghost (23, 4, 3), glow (40, 7, 3); VFD ghost (5, 17, 15)), references
  regenerated and exact, reflashed and checked the same way. Lesson for dark-on-dark
  pixel art: the matrix lifts the dark end, so a dark value that looks right in a PNG is
  about twice too bright on the panel, and the eye's window is narrow; judge it on the
  panel. Still for the eye: the filter tints of the four colours and the meter.
- 2026-09-28, interludes as a median gap (prompt p056, ADR 0014). The per-swap
  percentages went: the user now enters, per widget, the median minutes between its
  interludes (`widgets.interlude_minutes`, 0 = never, else 5..1440; defaults clock 30,
  weather 180, temperature 0) and the firmware derives the per-swap chance from the
  auto-swap interval, p = 1 - 2^(-T / 60M), in `rules::interlude_plan`
  (`main/show_rules.cpp`): the largest gap is rolled first and wins a coincidence (ties
  clock, weather, temperature), the lower kinds are rolled above their target by the
  chance that a higher one took the slot, so every kind's realised rate is its target;
  an interval longer than the gap, or no auto-swap, is off with the reason in
  `playback.interludes` of the status document and under the fields of the Widgets tab
  (decided with the user: never rather than a cap). The maths was checked offline first
  (`tools/interlude_sim.py`, three million swaps: rates within 1.5 sigma for every corner,
  the naive roll 87 sigma slow for a pre-empted kind), then on the host with a seeded
  generator (174 cases pass). `POST /api/v1/action/interlude {"widget"}` plays a widget as
  an interlude now (409 outside the show), with a button per widget on the page, since no
  setting can force one any more; the device smoke test uses it. Device (192.168.4.44,
  flashed 2026-09-28): `widgets_smoke` 0 failures (the plan's numbers, the clamps, the
  off states, the action, history, the 409), `ui_smoke` 0 failures, internal heap 70 KB
  free (largest 34.8 KB) after the run. The old `interlude_percent` key is ignored.
- 2026-09-28, random clock face at clock interludes (prompt p057): the option
  `widgets.interlude_random_clock_face` (off by default) makes every clock interlude,
  rolled or asked for, draw one of the nine faces at random, never the previous
  interlude's face twice running (`rules::random_face`, host-tested: uniform over the
  others), with the configured LED style; the face travels with the widget source
  (`widgets::make(kind, face)`, a copy of the settings with that face) and with the
  history item, so a revisit shows the face it had; `playback.widget_face` names the face
  on the panel. Device: `widgets_smoke` 0 failures (four consecutive interludes with four
  different faces, history recording them, the configured face again with the option
  off), `ui_smoke` 0 failures.
- 2026-09-28, the minimum artwork size: `makapix.min_size` (16, 32, 64 or 128, default
  16) beside `max_size`, both sides of a listed artwork at least the one and at most the
  other; the paired Makapix listing asks the server with `gte` criteria next to the `lte`
  ones (the anonymous `/api/post` was tested live with `width_min`/`height_min` and the
  server source read for `query_posts`; the promoted feed has no bounds and keeps the
  local drop, now against both limits), a change of either refreshes every channel, and a
  minimum above the maximum is refused (400 `INVALID_SETTINGS`, nothing of the document
  stored: `apply_json` reads into a copy). The settings page greys out the crossing
  options. The empty-channel text became "nothing fits N to M px (K outside)". Host tests
  cover the snapping, the refusal, the drop and the show's pickable list;
  `makapix_smoke.py` the API. External providers apply the same pair (the Divoom listing
  asks its server with a size bitmask built from both, private area).
- 2026-09-28, the TLS slot's contention. Seen after the minimum-size flash: the Divoom
  channel sat on "page 1" for over ten minutes while the Makapix fetcher held the slot.
  Three holding habits, not capacity: a walk paused because its channel left the playset
  (what `makapix_smoke.py` does with All and Followed, then the restore) kept its
  keep-alive session, and only active channels are served, so the slot stayed held until
  the reboot; listing sessions were held across whole walks, sleeps and downloads
  included; the vault and file-host sessions stayed open 20 s idle; and the FreeRTOS
  mutex woke the highest-priority waiter, so the Divoom worker (priority 3) lost every
  contest to the fetcher (4). Measured first: one active transient session costs about
  12 to 13 KB internal (60 to 47 KB free at the lowest 200 ms sample, largest block 32 to
  24.5 KB), so a second slot would spend the review's margin; decided with the user to
  keep one slot and fix the holding. Done: `p64_net/src/tls_slot.cpp` (pure, host-tested:
  a recursive lock handed first come, first served; fetch.cpp blocks on it with a
  condition variable and exposes `tls_waiting()` and `tls_status()`), the fetcher yields
  between pages and closes an idle vault session when a waiter exists
  (`yield_slot_if_waiting`) and closes the sessions of paused walks
  (`policy::paused_walks`, host-tested), the Divoom worker follows the same rules
  (private), `network.tls_slot` in the status document, and
  `tests/device/tls_slot_smoke.py` reproduces the paused walk and checks the slot is free
  within seconds. The cost is one handshake per hand-off, only under contention.
  Verified on the device: `tls_slot_smoke` 0 failures (the paused walk's slot went to
  the Divoom worker within 2.4 s), `makapix_smoke --paired` 0 failures, and a Divoom
  walk started during a Makapix walk alternated with it page by page (22 hand-offs,
  the longest wait 1.3 s, never more than one waiter) and finished its 64 pages in
  about 60 s, with 54 KB of internal heap free afterwards.
- 2026-09-29, a bench session (prompt p058: two hours with the device on USB and the
  LAN, no knob attached). Boot time re-measured over the console after a software reset:
  bootloader done 0.71 s, card 0.94 s, playset 1.05 s, Promoted index from the card
  1.51 s, IP 2.53 s, NTP 3.82 s, first artwork 3.83 s, unchanged from 2026-09-26 (the
  status document's `playback.boot` counts from the app's start, so it reads 3.10 s, the
  artwork 1 ms after the animation's end). The full smoke battery on the 2026-09-28
  firmware: 14 of 18 green. `encoders_smoke` wanted a board that was not attached
  (`--expect 0` passes); `content_smoke` demanded the first artwork from the local
  channel, a stochastic 3:1 pick, relaxed to either channel of the playset;
  `cache_sweep_smoke` and `tls_slot_smoke` timed out because the sweep dry run of 8158
  cached files (247 MB) took 230 s on the httpd task and no request was answered
  meanwhile (open, below); `timing_smoke` got 55.2 fps and 14 late frames on the 16 ms
  APNG while the private Divoom worker transcoded galleries on core 0 (decode 15.6 ms per
  frame, max 27), and 60.05 fps with 0 late with the walk idle an hour later, so the
  cadence loss is contention from the transcode (open, below). Found and fixed: with the
  GPIO socket empty the encoder task's 5 s re-probe timed out instead of NACKing (the
  controller's 10 k pull-downs hold both lines low) and the I2C driver printed two error
  lines every 5 s, a p64a's console forever; the poll task now reads the two levels first
  and probes only while both idle high (pure `socket_idle_high`, host-tested; the diag
  scan skips a low bus too). Verified after the flash: 0 probe lines in 80 minutes of
  console, `encoders_smoke --expect 0` green. Soak of 55 minutes on the new firmware
  with the browser poll: 659 swaps, 39 609 frames, 0 late flips, 0 timeouts, no reboot,
  heap floor 52 035 B (budget 32 768), core 0 busy 8.5 % (budget 10 %); the console over
  the run carried one warning (no GitHub release yet) and no error. Open from this
  session: (1) the cache sweep's cost, `for_each_swept_file` does a `stat()` per file
  after `storage::list` and FatFs's stat is a linear directory search, quadratic in a big
  shard (about 1900 Divoom files in one), and the API route runs it on the httpd task;
  carry mtime and size out of the directory read (FatFs `f_readdir` has them, the VFS
  drops them) and answer the route at once with the sweep on a worker; (2) the 60 fps
  budget under a concurrent transcode, a private-provider matter first (its worker's
  priority or a pause while an animation above 30 fps plays). Both taken up the same
  day, next entry.
- 2026-09-29, the sweep and the cadence (prompt p059). The cache sweep: `storage::list`
  read the directory with readdir and then a `stat()` per entry, and the sweep another
  per file in each of its two passes; FatFs's stat is a search of the directory, so a
  shard of 1900 files was quadratic, 28 ms per file, 230 s for 8158 files on the HTTP
  task. Now `storage::list` reads the entries through FatFs's own `f_opendir`/`f_readdir`
  (name, size, attributes and time stamp with each entry; `FileInfo.mtime`, converted as
  the VFS stat does; the card is drive "0:"; FatFs is built re-entrant; the POSIX walk
  stays as the fallback) and the sweep walks twice as before, holding nothing between
  files. Measured: 8190 files in 22.5 s, 2.5 to 2.75 ms per file (28 before), the web
  server answering within 320 ms throughout. A single pass over a collected table was
  tried the same day (11.2 s, 1.37 ms per file) and dropped on the user's question about
  its memory: 9000 records with two heap strings each cost 1.3 MB, and under the
  1024-byte rule their small strings drained the internal heap from 61 to 36 KB, under
  the 48 KB floor, for the sweep's duration; the two-pass walk costs 6 KB, held by
  `sweep_internal_drop_max` (16 KB) in `budgets.json` and checked by the smoke. The
  check-everything-first rule (ADR 0011) stays: it is one comparison per file, and the
  sweep's whole cost is directory reads over the 1-bit bus. `POST /api/v1/diag/cache_sweep` queues a `Sweep` job
  for the fetcher task and answers 202 at once; `makapix.cache.sweep {queued, running,
  last}` in the status document carries the outcome; a second request while one is queued
  or running is 409. `cache_sweep_smoke.py` polls for the outcome, requires every status
  request during the sweep to answer within 2 s (115 to 160 ms measured), checks the 409,
  and holds the cost per file to `sweep_ms_per_file_max` (4 ms) in `budgets.json`.
  Verified on the device: cache_sweep_smoke, api_smoke and tls_slot_smoke 0 failures.
  The cadence, measured before deciding (the user's choice): a playset with an uncached
  Divoom channel forced a download-and-transcode walk while `timing_smoke` ran. Idle:
  60.05 fps, decode about 10 ms per frame. Worker as shipped (yields 5 ms in 20): 55.6
  fps, 51 transcodes during the test, decode 14 to 16 ms. Worker yielding 10 ms in 10:
  57.3 and 57.5 fps (52.6 right after a boot), 32 to 37 transcodes, decode 14 to 17 ms.
  No frame was skipped in any run; most transcodes take 100 to 500 ms, so the slowdown
  comes from the whole walk (TLS download, card writes) sharing PSRAM with the decoder,
  not from the transcode's duty cycle, and the throttle was reverted. Decision: accept
  the loss as a known limit (spec 18.4 amended); the lever that would reach 60 fps is a
  pause of the walks while an artwork's frame period is under 33 ms, not taken.
- 2026-10-01, the horizon checked against the sky (prompt p060), New York, from the
  laptop's clock. Reference: Skyfield with the JPL DE421 ephemeris, 2026-09-01 to
  2026-11-15 every 37 minutes, on the C++ `solar.cpp` built natively. The sun was right
  (0.44 degrees at worst in elevation, 0.45 in azimuth: the refraction the model leaves
  out). The moon was not: placed from the phase alone (trailing the sun by the phase, the
  sun's declination that many months later, the orbit's tilt and eccentricity ignored)
  it was off by up to 20 degrees in elevation and 52 in azimuth (9 px on the panel), drawn
  above the horizon while really below (or hidden while up) in 6 % of the night samples,
  and its phase lagged by a day (the 365.25-day year from 2000, local time taken for UT,
  the mean month). Replaced by the Astronomical Almanac's low-precision moon (six
  longitude terms, four latitude, the parallax) and the phase from the elongation, in
  `solar.cpp` and the mock: against Skyfield 0.24 degrees in elevation at worst, never
  more than 1 px, visibility never wrong, phase within 0.02 days; over 2025 to 2030 at
  New York, Sao Paulo, Tromso and Sydney within 0.37 degrees. Tonight's moonrise: 21:40
  by the model, 21:38 in the almanac (21:18 before). The signatures changed
  (`moon_phase` takes the zone, `moon` the year and no phase); the host tests check the
  full moon of 2026-09-26, the new moon of 2026-10-10 and tonight's moon against the
  almanac. On the device (flashed, `ota_0`): with the location at New York, 15:35 and
  15:47 EDT were pixel for pixel the mock (a high sun in the south-west, a clear blue
  sky), and with Tokyo (04:47 there, the moon at 76 degrees) the frame was the new mock
  exactly and 101 pixels off the old one; `faces_smoke` and `api_smoke` 0 failures (heap
  67.7 KB free). The weather location was Greenwich (so the horizon showed Greenwich's
  sky in the device's zone); set to New York at the user's choice. Second finding: the
  far hills stand up to 12 px above the horizon line on the right, so the setting sun hid
  behind them from about 18 degrees up, an hour before sunset (and the moon likewise).
  The user chose to draw the sun over the hills: the far hills are now drawn before the
  moon, the sun, the clouds and the rain, and the bodies set behind the near hills only,
  at the horizon line (New York's sun now visible until 18:36, sunset). Host test: the
  low sun shows from 17:00 to 18:40 on 2026-09-26 and is gone at 19:05 (it fails on the
  old order from 18:00). Flashed again: at 16:12 EDT with New York's clear forecast the
  frame is the mock within 1 on one channel in 717 hill pixels (float against double at
  full daylight, the horizon's documented tolerance); `faces_smoke` and `api_smoke` 0
  failures (heap 57.9 KB free with the New York forecast fetched).
- 2026-10-01, the horizon's time in Everyday Vast Black (prompt p061): the face drew the
  time in Capital Hill at twice its size; it now uses VEXED's Everyday Vast Black at its
  native 11 px (copied with its family into `assets/fonts/everyday-vast`, CC BY 4.0,
  credited in the About section), outlined as before, AM/PM and the date unchanged in
  Capital Hill. The font joins the bundled tables (`gen_fonts.py`, seventh font), so the
  Clock widget offers it too; it is kept out of the overlay. Mock and references
  regenerated; host tests 185 cases, 49 references exact. On the device: the time's rows
  exact against the mock at 16:23 EDT; `widgets_smoke`, `faces_smoke`, `ui_smoke` 0
  failures; `api_smoke`'s only failure is the first artwork after boot, the device having
  booted into the clock widget.
- 2026-10-01, the horizon's sun sets on the hill line (prompt p063; replaces drawing the
  bodies over the far hills, which looked odd). Decided with the user after a pros/cons
  review and two mock sheets: the bodies are drawn behind the far hills again and rise
  and set on their line (the far-hill sprite's top row, smoothed over five columns): the
  disc rests on it at +0.7 degrees and slides behind it by -0.83, the almanac's sunset,
  about 0.9 px a minute for the sun in New York; the moon follows the same rule. A first
  mapping lifted each column by its own hill height (fading by 90 degrees; the 20-degree
  fade first offered would have made the sun dip while rising, the hill and the disc
  being about 20 degrees of the panel's scale) and bulged the arc over the peak; the
  user asked for a round, symmetric path, so the arc is one curve of the elevation from
  a raised horizon (the higher of the line's two ends, row 41) to row 18, the local line
  bending only the first 5 degrees where it is lower; where the hills stand higher (the
  peak), they hide a low body (Tromso's February sun passes behind it). `themed::
  horizon_body_row` is the rule, host-tested: hidden below -0.83, never rising less
  as the elevation grows in any column, the same row in every column above 5 degrees,
  the rest and hidden rows; and New York's sun on 2026-09-26 whole until 18:40, partly
  hidden at 18:43, gone at 18:48 (the almanac: 18:46; the solar model runs 0.3 degrees
  high). The hill line is computed per frame on the stack (no static RAM; `player` keeps
  9 KB of stack free). Device: pixel for pixel the mock at 18:47 in New York and,
  mid-slide, at 40.7 N 77.3 W and 77.8 W; `faces_smoke` 0 failures on a rerun (its LED
  glow check failed twice, timing-sensitive); `widgets_smoke` stopped on
  `/api/v1/history`, whose JSON held a provider's name cut inside a UTF-8 character (a
  fixed-size name field in the private area): fixed at the user's request the same day,
  see the next entry; `api_smoke` failed the first artwork after boot (booted in the
  clock widget) and once the internal largest block (29.7 KB against the 32 KB floor
  with the private walk running; 34.8 KB on the next run; the change allocates nothing).
- 2026-10-01, names arrive in the show as valid UTF-8: `content::valid_utf8` (in
  `provider.cpp`, host-tested: whole characters kept, cut sequences, stray continuation
  bytes, overlong forms, surrogates and Latin-1 bytes dropped) is applied where a
  provider resolves an item's name and to play-this names, because the API writes names
  into its JSON unchecked and one broken byte made `/api/v1/history` unreadable. Show
  test: a fake provider's name cut after a lead byte reaches the history whole and the
  history document is valid UTF-8 (fails without the guard). The private source now cuts
  names at a character boundary too. `tests/host/run.py` no longer dies printing a
  failing case's raw bytes (it decodes and prints with replacement). Device: the history
  decodes, `widgets_smoke` and `api_smoke` 0 failures (largest block 34.8 KB).
- 2026-10-01, the horizon's time spaced (prompt p064): one more pixel between the
  characters of the time (Everyday Vast Black), drawn a character at a time in the face,
  the outlines first and then the ink so it matches a whole-string draw; the font itself
  and the Clock widget are unchanged; the widest time is 43 px. Mock and references
  regenerated; host tests 188 cases, 49 references exact; on the device the frame is the
  mock pixel for pixel. Found on the way: `widgets_smoke` set the weather location to
  Greenwich and never put it back (how the device came to show Greenwich's sky); it now
  restores the location and the units.
- 2026-10-01, p64b, knob B (prompt p062): the replacement 5880 was tested before
  soldering (green, 0x36), bridged at A0 (meter readings as expected) and answered at
  0x37 with its blue identify. With both boards chained on the breadboard harness the
  bench acceptance passed: `encoders_smoke.py --expect 2 --watch 45` 0 failures with
  both knobs turned at once and 0 read errors on either board; knob B's roles ran on
  hardware for the first time (next, previous, and a like that the log confirms: "liked
  post 1567"; a press on a card artwork is ignored); the board-to-board cable pulled
  while running dropped knob B after ten failed reads, left knob A untouched (0 errors,
  never lost) and B came back at 0x37 without a reboot. Internal heap 56 KB free,
  largest 32 KB, with both boards polled. No firmware change. Next for p64b: the
  permanent harness (tier 2), the v7b print, the in-shell assembly
  (`docs/hardware/encoders-soldered.md` 5.1, 6, 7).
- 2026-10-02, the panel's darks corrected (prompts p065, p066). Reading the driver
  patch showed that each plane's output-enable window sat in its own plane's buffer,
  while a buffer's window gates the data latched by the buffer before it (the lag
  upstream relies on when buffer 0 carries the previous row's address). The planes'
  data weighed 3 7 15 31 62 62 124 248 496 931 clocks in Quality instead of 1 3 7 15 31
  62 .. 992: the darks two to three times too bright and the light falling at every
  32nd code, by half from input 51 to 52. Confirmed on the panel before the fix: four
  grey bands 48, 51, 52, 56 streamed raw, the third clearly darker than the second.
  Fixed in the driver (`p64bcm::buffer_windows`, the LUT fitted to
  `p64bcm::data_weights`; host tests for the old weights and for every profile and
  level); Photo had the same fault (7 falls, the deepest at input 98) and is fixed by
  the same change. The fit's targets are now the gamma curve over 16 bits instead of
  the 10-bit table (229 -> 233 codes in Quality, 37 -> 41 in the darkest quarter, inputs
  6 and 7 no longer black). Device: boot log "1 3 7 15 31 62 62 62 62 62 ... 233
  distinct codes"; the same bands now rise steadily and two ramps (0..63, 0..252) look
  smooth to the user; `brightness_smoke`, `panel_mode_smoke`, `api_smoke` 0 failures.
  Considered and dropped: an eleventh plane (243 codes, worst dark error 15 % -> 2.5 %)
  needs 8 KB more internal DMA memory, and the largest free internal block measured
  28.7, 32.8 and 34.8 KB on three boots today, at or under the 32 KB floor. Everything
  dark that was tuned by eye before this day (the LED face's ghost segments, the themed
  faces' night colours, the brightness ladder's low end) was judged on the too-bright
  darks and wants a new look.
- Remaining: the acceptance measurements that need instruments (camera at 240 fps, a
  power meter), a 12 h and a 24 h soak (`soak.py --minutes 720` when the device can be
  left alone), and the hands-on checks (taps, rotation direction, BOOT hold, the Photo
  mode picture).
