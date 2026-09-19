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
| `p64_stream` | DDP and raw UDP receivers, frame assembly, the stream sink | `p64_gfx`, IDF | partly (parsers) |
| `p64_inputs` | BOOT button, IMU (tap, orientation), encoder abstraction | IDF | no |
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
