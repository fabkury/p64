# p64 firmware architecture

How the firmware is built to do what `docs/spec/p64-spec.md` says. Vocabulary from
`CONTEXT.md`. This file is kept current as the code grows; the milestone status is in
`PROGRESS.md` next to it.

## 1. Shape

ESP-IDF v5.5 project, C++20, one application, components under `firmware/components/`.
Boundaries follow p3a's (ADR 0001) but are grouped into fewer, larger components sized
for the ESP32-S3; each component has one job, a public header set under
`include/p64/<name>/`, and no knowledge of the components above it.

| Component | Job | Depends on | Host-testable |
|---|---|---|---|
| `hub75` | vendored esphome/esp-hub75 0.3.6 with the p64 patch (from the hardware tests) | IDF | no |
| `animatedgif` | vendored bitbank2/AnimatedGIF | none | yes |
| `p64_gfx` | `Frame` (RGB888, panel-sized, logical orientation), `Rgb`, blending, rotation, `Scaler`, bitmap fonts and text | none | yes |
| `p64_decode` | `Decoder` interface, format sniffing, GIF, PNG/APNG, WebP, BMP decoders, the frame-delay rule | `animatedgif`, libpng, libwebp | yes |
| `p64_display` | `Display`: owns the driver, frame pacing locked to the DMA, rotation, gains, brightness pipeline, panel modes, health | `hub75`, `p64_gfx`, IDF | no |
| `p64_system` | event bus, task helpers, monotonic clock, log ring buffer, reboot counters, coredump summary, settings store (NVS JSON document) | IDF | partly |
| `p64_playback` | `FrameSource` (anything the panel can show), `Artwork` (bytes + decoder + scaler), `StaticSource`, `Player` (timeline, no-drop rule), `Renderer` (the only presenter), `FrameQueue` | `p64_decode`, `p64_gfx`, `p64_display` | partly (queue) |
| `p64_storage` | card mount, layout under the root, atomic writes, file manager operations, eviction | IDF | no |
| `p64_content` | channels, playsets and their JSON, scheduler (SWRR/stochastic, recency/random), history, local folder index, playset store; Makapix indexes, cache and downloads come with M6 | `p64_storage`, cJSON | yes (model, JSON, scheduler, history) |
| `p64_net` | Wi-Fi manager (STA, setup mode, captive portal), mDNS, SNTP, time zone table, HTTP fetch helper with the TLS gate | IDF | no |
| `p64_web` | HTTP server, `/api/v1`, WebSocket push, embedded web UI, PIN | `p64_net`, everything it exposes | no |
| `p64_makapix` | pairing, credentials, MQTT over mTLS, player RPC, commands, views, likes | `p64_net`, `p64_content` | no |
| `p64_widgets` | clock (digital, analogue), weather, temperature; font and icon assets | `p64_gfx`, `p64_system` | partly |
| `p64_stream` | DDP and raw UDP listeners, assembly by offset, conversion and scaling, the latest-frame source, silence timer | `p64_gfx`, `p64_playback`, `p64_system`, lwIP | yes (`protocol.cpp`: parsers, assembler, conversion) |
| `p64_inputs` | QMI8658 sampler (250 Hz polling, PSRAM stack), tap gestures, gravity auto-rotation with an upright calibration; encoders later. The BOOT button lives in `main/ops` | `p64_system`, IDF | yes (`tap.cpp`, `orientation.cpp`) |
| `p64_ops` | OTA, factory reset, diagnostics endpoints' data | IDF | no |
| `main` | boot sequence and wiring (`main.cpp`), the show state machine (`show.cpp`: active playset, channel runtimes, scheduler, history, auto-swap, pause, play-this, activation), the loader task (`loader.cpp`: file reads and folder scans on core 0), status screens | all | no |

Host-testable components keep every file free of ESP-IDF includes; `tools/hosttest/`
builds them with the PC's g++ and runs their tests (the hardware tests' `gifcheck`
approach, generalised). Everything else is verified on the device through the serial
console and the API.

## 2. Tasks and cores

| Task | Core | Priority | Owns | Notes |
|---|---|---|---|---|
| `render` | 1 | 20 | `Display` | The only caller of `present()`. Loop: take the frame due next, `wait_for_back_buffer()`, apply rotation and gains, `present()`. Never blocks on anything but the DMA boundary. |
| `player` | 1 | 15 | decoders, scaler, the ready-frame ring | Decodes the next frame of the current artwork into a ready slot ahead of its due time; prepares the next artwork's first frame in a second slot; composes overlays. Slow decode delays frames (no-drop rule), never the render task. |
| `main` (app) | 0 | 5 | the state machine, timers, event dispatch | Auto-swap timer, interlude rolls, history, commands from the API and Makapix. |
| `net` tasks | 0 | 3 to 8 | Wi-Fi, lwIP, mDNS, SNTP, HTTP server, MQTT, downloads, channel refresh | IDF's own tasks plus the fetcher, refresh and download workers. Every network and storage task is pinned to core 0 (p3a jitter lesson 7). |
| `storage` | 0 | 4 | card I/O for the file manager, index and cache writes | Playback never reads the card: the player works from file bytes already in PSRAM. |
| `stream` | 0 | 9 | UDP sockets | Assembles frames into the stream sink; the player picks them up. |
| `inputs` | 0 | 6 | IMU polling, BOOT | 50 Hz poll. |

Rules: the two core-1 tasks never take a lock that a core-0 task can hold for long
(frame handoff is a lock-free ring of ready slots); all cross-task requests go through
the event bus or a command queue drained by the main task; no task blocks the render
task except the DMA boundary wait.

## 3. Frame flow

```
file bytes (PSRAM)  -> Decoder -> canvas RGB888 (canvas size, PSRAM)
                    -> Scaler  -> logical Frame 64x64 (internal RAM)
                    -> overlay (clock) -> ready slot (frame + due time)
render task         -> rotation + RGB gains into the physical buffer
                    -> hub75 draw_pixels (RGB888 -> bit planes, 6.9 ms at 10 planes)
                    -> flip on the next DMA frame boundary
```

Three ready slots and one source at a time (M5): the show prepares the next artwork
(file read into PSRAM, decoder opened) while the current one plays; a swap hands the
prepared source to the player, whose first frame goes into the queue with a new
generation; the render task cuts to it at the next boundary and drops older slots. The
previous picture stays up until that first frame exists, which is what makes every
swap seamless (spec 3.6). Status screens, the pause frame and the boot animation are
`FrameSource`s played the same way.

Timing: each ready slot carries its due time (previous due + frame delay after the
browser rule, at least one 60 Hz period). The render task keeps its own schedule
(target = max(due, previous target + previous delay)) and starts the 7.7 ms copy that
far ahead so the flip lands on the boundary at or after the target. Targets carry
durations exactly: a flip that lands up to one refresh period after its target does
not move the schedule (anchoring on the copy's end time drifted 1.2 % slow and ate the
player's lead, M5); only a miss beyond a period re-anchors. If a slot is not ready by
its target, the current frame stays; the late slot is presented when it lands and the
timeline re-anchors there (ADR 0003). The player flags frames it produced late
(`decoded_late`); the renderer counts only presentation lateness of the others. Both
playback tasks share core 1, so an artwork whose frames decode slower than their delays
keeps the player busy for as long as it plays: the core-1 idle watchdog is off for that
reason (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`).

## 4. Display

Ported from the hardware tests (`hardware-tests/main/display.*`) and extended:

- Frame boundaries are read from the LCD GDMA channel (the driver's flip only relinks
  descriptors); `wait_for_back_buffer()` sleeps until just before the predicted boundary,
  spins on the end-of-frame flag and confirms the descriptor pointer left the old chain.
  The rationale and the failure modes are in `hardware-tests/README.md` "Frame pacing".
- Rotation is applied in the display layer while copying the logical frame into the
  physical buffer; the driver stays at ROTATE_0. Auto rotation is a value the inputs
  component sets.
- Per-channel gains are applied in the same copy. Gamma stays in the driver's LUT (2.2,
  fixed by the p64 patch).
- Brightness pipeline: effective = min(user, ceiling, schedule) computed by the main
  task, applied through the driver's `set_brightness()`; 0 is only ever used for pause
  and panel off.
- Panel modes: the driver's bit depth is a compile-time constant (10), so Photo mode is
  a driver re-creation with a higher `min_refresh_rate` (600 selects transition bit 6:
  698 Hz on this panel at 20 MHz, above the spec's 600 Hz threshold). The re-creation
  blanks the panel for well under a second; the render task performs it between frames.
  A runtime bit-depth patch to the driver is the later refinement if 8-plane 810 Hz is
  wanted.
- Health: DMA stall detection, late flips, timeouts, GDMA priority (5 by default, the
  hardware tests' GDMA lesson) are exposed for diagnostics.

## 5. Decoders

One `Decoder` interface (open from bytes, info, decode next frame into an RGB888 canvas
with alpha pre-blended over the background colour in gamma space, frame delay, reset):

- GIF: vendored AnimatedGIF in RAW mode with the hardware tests' compositor (all four
  disposal methods, 1-bit transparency, 0-delay and ≤10 ms delays become 100 ms).
- PNG/APNG: libpng 1.6.x with the official APNG patch (p3a keeps this as a local
  component, `firmware/reference/p3a/components/espressif__libpng`); vendored the same
  way.
- WebP: libwebp 1.4.0 decoder and demux libraries, vendored as sources (p3a fetches
  them at configure time; p64 vendors to keep builds reproducible offline).
- BMP: p3a's decoder ported (1 to 32 bit, RLE, alpha masks).
- Format from magic bytes; the extension is never trusted.

The decode benchmark (`tools/bench/`) measures every decoder at 64, 128 and 256 px on
the device and on the host; results go to `firmware/README.md`.

## 6. Memory budget (ESP32-S3-WROOM-2: ~350 KB internal usable, 16 MB PSRAM)

| Item | Where | Size |
|---|---|---|
| Panel bit-plane buffers, 10 planes, double | internal (DMA) | about 82 KB |
| Ready frames (4 x 64x64x3) + physical buffer | internal | 60 KB |
| Wi-Fi + lwIP | internal/PSRAM (`SPIRAM_TRY_ALLOCATE_WIFI_LWIP`) | 40-60 KB internal |
| TLS: MQTT session + one download | PSRAM buffers, dynamic | 2 x ~40 KB |
| HTTP server, mDNS, MQTT client | internal | ~30 KB |
| Artwork bytes, current + next | PSRAM | 2 x ≤ 5 MB |
| Canvases (≤ 256x256x3, GIF backup x2) | PSRAM | ≤ 400 KB per artwork |
| Channel indexes (≤ 64 x 4096 x 64 B, lazily loaded) | PSRAM | ≤ 16 MB worst case; capped by a global index budget |
| Memory cache without a card | PSRAM | ≤ 8 MB |

The internal-RAM floor is checked at boot and logged; diagnostics expose the largest
free block. Anything larger than 4 KB that is not DMA-bound is allocated from PSRAM.

## 7. Settings

One JSON document in NVS (`p64_system::Settings`), typed accessors, defaults from the
spec's settings table, change notifications on the event bus, double-buffered writes.
Wi-Fi credentials and Makapix credentials live in their own NVS namespaces. Development
builds may seed Wi-Fi credentials from the git-ignored `sdkconfig.secrets`
(`P64_DEV_WIFI_*`) when NVS holds none; release builds leave those options empty.

## 8. Web and API

`esp_http_server` on port 80 with wildcard routing to `/api/v1/...` handlers, the
p3a envelope, one WebSocket endpoint for status push and the live preview (PNG of the
logical frame, 2 to 4 fps while a client is connected), the UI embedded through
`EMBED_FILES` (gzip where it pays). PIN check is one hook at the top of every handler.

## 9. Build and tools

`tools/*.ps1` from the hardware tests (build, flash, monitor, port, erase, idf) adapted to
this project; `tools/serial_peek.py` reads the console without resetting the board;
`tools/hosttest/` builds and runs the host tests; `tools/bench/` the decode benchmark.
`sdkconfig.defaults` is the source of truth; `sdkconfig` is generated and ignored.

## 10. Milestones

See `PROGRESS.md`.

## 11. Content and the show (M5)

`p64_content` is the model: `Playset`/`ChannelSpec` with validation and JSON
(p3a's channel shape accepted on input), `Scheduler` (p3a's semantics: weights
normalised to 65536 among the channels that have entries, all-zero means equal; SWRR
adds each channel's weight to its credit per pick, chooses the highest credit with random
ties and subtracts 65536; stochastic samples in proportion to weight x clamp(1 + 0.8 x
credit/65536, 0.1, 3) with the same credit moves; random picks avoid an immediate repeat;
recency walks a cursor from the channel offset and wraps), `History` (32 items, a
position; a push from the middle discards what was ahead), the local folder index
(`LocalEntry` 140 bytes in PSRAM, newest first, caps of 4096 per channel and 16384 per
playset) and the playset store (`channels/playsets/<name>.json`, atomic writes). The
built-in playsets are synthesised: Local gets one channel per folder found under
`animations/` (the root first).

`main/show.cpp` runs on the main task and owns the runtime: the active playset and one
`ChannelRuntime` per channel (spec, entries, available count, a status string such as
"no card", "needs pairing" or "Makapix not available yet"), the scheduler, the history,
the auto-swap deadline, the pause flag. Everything reaches it through a FreeRTOS queue of
commands; the API's readers take a mutex and build JSON from the state. Card I/O never
runs on the main task: `main/loader.cpp` (core 0, priority 4, PSRAM stack) reads files
and opens their decoders (`LoadResult`) and scans the playset's local folders
(`ScanResult`, which also rebuilds the Local built-in's channel list); results come back
as commands. Activation and refresh are scans carrying a generation number, so stale
results are ignored. A fresh pick is made and its file loaded right after each swap
(`prepared`), so the auto-swap and "next at the end of history" swaps are immediate;
navigation loads on demand. The boot animation is a `FrameSource` played first, and the
show holds its first swap until the animation has run its course.

Makapix channels exist in playsets from M5 on but supply nothing until M6; the
channel status says so and the scheduler gives them no share.

## 12. Makapix Club (M6)

`p64_makapix` holds the pairing state and credentials (NVS namespace `makapix`: player
key, the three PEMs, the API token, the broker), the channel indexes and the artwork
cache, and the MQTT session. One worker task on core 0 (`fetcher.cpp`, internal 10 KB
stack because TLS runs on it) performs every outbound HTTPS request in turn, so at most
one transient TLS session exists next to the persistent MQTT one (ADR 0009). Its loop:
a queued job (pairing, play-this, likes, views over HTTPS, the Followed playset,
certificate renewal), else the pairing poll, else one page of a channel refresh, then
one artwork download, then a short sleep.

- Listings: the anonymous promoted feed (`GET /api/feed/promoted`) before pairing, the
  player RPC `query_posts` over HTTPS with the bearer token after it (the same contract
  the MQTT request topics offer; HTTPS spares the 128 KB fragment reassembly the MQTT
  path needs). A refresh walks pages of 50 newest-first up to the channel cache size,
  one page per worker step over a kept-alive connection, and installs the first pages
  at once when the channel was empty so downloads and playback start within seconds;
  the full walk is then merged (`content::merge_index`: flags survive for unchanged
  entries, changed files re-download, vanished entries drop) and saved as
  `channels/<id>.p64x` (64-byte records, CRC32). Refresh failures back off 30 s to
  15 min.
- Downloads: round-robin over the active playset's Makapix channels, each channel's
  entries newest first, one file at a time over a kept-alive plain-HTTP connection to
  the vault (the server offers it for players; TLS would cost tens of kilobytes of
  internal RAM per session). Files are sniffed before they land in `cache/<xx>/<uuid>.<ext>`
  (atomic write); 404s and undecodable files are flagged in the index and not retried
  until the entry changes. Without a card a PSRAM memory cache (48 files, 6 MB) takes
  their place and the loader reads `mem:` paths from it.
- The show treats a Makapix channel like a local one whose pickable entries are the
  cached ones; `MakapixChannelChanged` events make it re-read the index snapshot and
  update the scheduler's counts, and a pick prepared from a tiny cache is replaced as
  the cache grows.
- MQTT: esp-mqtt over mutual TLS (`mqtts://makapix.club:8883`, client id and username =
  player key, last will `offline`, keep-alive 60 s, 6 KB task stack). On connect it
  publishes status, the retained capabilities (pause, brightness 1 to 255, rotation
  0/90/180/270) and the retained state; status every 30 s. Commands: `swap_next`,
  `swap_back`, `show_artwork` (downloaded into `downloads/` then played as play-this),
  `play_channel` and `play_playset` (transient playsets through the show), `set_paused`,
  `set_brightness`, `set_rotation` (acknowledged on `command/ack`), `set_mirror`
  (unsupported). Eight refusals in a row mark the pairing invalid.
- Views: an artwork counts as viewed after 5 s on the panel (one timer, restarted on
  every swap); published on the MQTT view topic when connected, else posted over HTTPS.
  Presence carries the current post id.
- Certificates: the notAfter of the stored certificate is checked daily; inside 45 days
  of expiry the worker calls `POST /player/renew-cert` with the token (rotating the token
  first on a 401), stores the new PEMs and restarts the MQTT session.

Memory, measured on 2026-09-19 with MQTT connected and downloads running: internal heap
25 to 30 KB free, largest block 24 KB, minimum seen 20 KB, after freeing the parsed
certificates of each TLS session once its handshake is done
(`CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA`, `_FREE_CA_CERT`), trimming the MQTT task
stack to 6 KB and the Wi-Fi static receive buffers to 8. Likes (a transient TLS session)
succeeded next to the MQTT session at that level. This is the tightest budget in the
firmware; anything new that wants internal RAM must be measured against it.

## 13. Widgets and text (M7)

Text: `gfx::text` is a built-in 5x7 font for status screens; `gfx::fonts` holds the
bundled pixel fonts as glyph tables generated by `tools/gen_fonts.py` from the TTFs in
`assets/fonts` (Capital Hill 6 px and Everyday Typical 7 px, rendered by Pillow at
their native sizes where they come out crisp; the generated `fonts_data.cpp` is
committed, ADR 0008). Layout is by ink box: `draw(frame, font, x, y, ...)` puts the
top of digits and capitals at y; descenders hang below `cap_height`, taller glyphs
(quotes) rise above. Integer scale and a one-pixel outline are the only effects.

`p64_widgets`: three `FrameSource`s and the overlay. The clock renders the time for
the frame's due instant (the player works ahead), so its frames are right when they
show; it asks for the next frame at the next minute (or second when seconds or the
blinking colon show). The weather keeps one `Forecast` (Open-Meteo current conditions
and four daily rows, parsed by the host-tested `weather_model`) fetched by a small
task with a PSRAM stack on the refresh interval, and draws "NO DATA" after six hours
without a refresh. The temperature widget reads the SHTC3 through a sampler task every
minute (calibration offsets from the settings, the trend from the last hour of samples)
and the reading is always in the status document. The overlay is a player hook: `key()`
changes with the minute and the overlay settings, `draw()` paints HH:MM with an outline
in the chosen corner; static images are re-emitted from the player's kept copy when the
key changes, so the overlay ticks without a decode.

The show owns the main state (spec 6): Animation show as before; Widget plays
`widgets::make(kind)` and pauses the swap timer; Stream shows the waiting screen
between streams (section 14). Interludes are rolled at auto-swap in the fixed order Clock,
Weather, Temperature with the settings' percentages; a winner enters history as an
`Interlude` item and revisiting it replays the widget. Manual next and previous never
roll one. Frames with minute-long delays taught two rules: the player announces a new
generation before its first frame so the render task frees the old slots at once, and
the render task sleeps towards a far target in 10 ms steps, checking for a newer
generation each time.

## 14. Streams (M8)

`p64_stream` (spec 8, ADR 0007) is one listener task on core 0 (priority 9, PSRAM
stack) selecting on two UDP sockets, DDP on 4048 and the raw p64 format on 4064 (ports
and enables from the settings; a change reopens the sockets). `protocol.cpp` is the
host-tested part: the two header parsers, the `Assembler` that collects chunks by byte
offset into caller-owned storage (a 64-byte block map counts distinct bytes, so a
resent chunk is not counted twice and chunks may arrive in any order; the raw "last"
flag is informational), the
RGB565 and indexed conversions and the DDP size rule (12 288 bytes = 64x64, 49 152 =
128x128, nothing else). Each protocol has its own assembler (49 920 bytes of PSRAM
each) so interleaved senders do not corrupt each other; within a protocol a new frame
(offset 0 for DDP, a new sequence or geometry for raw) abandons the one in progress
(`incomplete`). A complete frame is converted into an RGB888 canvas, scaled by the
artwork rules into a panel `Frame` kept as "the latest", a serial bumps, a semaphore
wakes the source, and the silence timer (esp_timer, `stream.silence_ms`) is re-armed.
`StreamStarted` goes out on the first frame after silence, `StreamEnded` when the timer
fires. lwIP's UDP mailbox is raised to 48 datagrams (`sdkconfig.defaults`) because a
128x128 frame is 35 datagrams in one burst.

The source (`stream::source()`, one instance) is a `FrameSource` like any other, which
is what makes the takeover seamless: the player swaps to it and back with the same
hard cut it uses for artworks. Its `next_frame()` waits for a frame newer than the one
it handed out last; because the player asks one 60 fps slot ahead, the source waits
until 2 ms before the slot's due time before taking the latest frame, so a sender
above 60 fps is sampled at the panel rate with the freshest frame and never queues
(spec 8.2: no buffering beyond one frame). After a second without frames it re-issues
the last frame slowly so the player keeps polling; `stream::wake()` breaks the wait so
the show can swap the stream out at once. Measured latency (last chunk in to the
player taking the frame) is in the status document; the panel copy and one refresh
add about 10 ms.

The show (`show.cpp`) routes every source it puts up through `present()`. When a
stream may take the panel (`stream.takeover` on, or the Stream state; after the boot
animation and not over the pairing screen, which waits) the source that was up is kept
as "behind", the stream source goes to the player and `stream_up` is set. While it is
up, `present()` parks whatever the state would show (a fresh pick at auto-swap, a
navigation, a pause, a widget, the waiting screen after a state change) as the new
"behind" instead of playing it, so timers and history run on unchanged. On
`StreamEnded`, or when takeover is switched off, the show wakes the source and plays
"behind"; a kept `Artwork` continues from where its decoder stopped. Views are noted as
hidden during a stream and reported again on return; the overlay hook returns no key
over a stream; streams never enter history.

## 15. Operations (M9)

`p64_system::reliability` classifies the reset at boot (`esp_reset_reason`), bumps a
per-cause counter in the state store, reads the core dump summary when one is stored
(task, pc, cause, backtrace) and knows whether the running image still awaits
confirmation. `main/ops` confirms a pending image 30 s after boot (a crash before that
lets the bootloader roll back to the previous slot; confirmation also resets the reboot
counters), runs the night schedule (a 15 s timer re-applies the display settings when
the effective brightness changes; the rule itself, `system::night`, is pure and
host-tested: night target inside the window, else the user's value, capped by the
ceiling, 0 = panel off), and performs the factory reset (Makapix unpair, Wi-Fi
credentials, state, settings, PIN; the card stays) either from the API or when BOOT is
held for 10 s at power-on, the panel counting down the last three seconds.

The on-board PCF85063A (`system::rtc`, on the shared I2C bus `system::i2c_bus`) seeds the
system clock at boot when its oscillator has run since the last set and the date is
plausible; every NTP sync and every manual set writes it back. `net::clock::source()`
says where the time came from.

The task watchdog watches the show loop, the loader and the stream listener (each waits
at most a second between resets); the player and the render task are excluded on
purpose (a slow artwork keeps them busy by design), as are the network workers whose
TLS calls can block longer than the 10 s timeout.

## 16. Inputs: the IMU (M9)

`p64_inputs` polls the QMI8658 accelerometer (I2C 0x6B, +-8 g at 500 Hz, no filter) every
4 ms from a small task on core 0 and feeds two host-tested detectors. `TapDetector`
tracks gravity with a 150 ms low-pass and looks at what is left: an excursion above the
threshold (sensitivity 1..10 maps to 2.5..0.25 g) that ends within 60 ms is an impulse;
a longer one is the shell being moved and cancels a pending tap; two impulses 80..350 ms
apart make a double tap, a lone one is reported when the window closes (so "next" lands
about 350 ms after the knock), and every report starts a 1 s lockout. The measured noise
floor on the resting board is about 0.004 g, three orders below the default threshold.
`OrientationTracker` low-passes the gravity vector (0.5 s) and reads its angle in the
board plane; a calibration ("the panel is upright now at rotation R") stores that angle
as the reference, and the resolved rotation is R plus the number of right angles gravity
has moved since (sign from Kconfig). A new value must hold for a second, within 30
degrees of a right angle, with at least 0.55 g in the plane (a panel lying flat keeps
the last value). The display applies the resolved value while `rotation_auto` is on;
the setting stays the fallback. On the development board gravity lies along +X with the
panel upright at rotation 90.
