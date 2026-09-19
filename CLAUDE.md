# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

p64 is a desktop 64x64 RGB LED matrix: Waveshare's ESP32-S3-RGB-Matrix driver board
plugged onto their RGB-Matrix-P2-64x64 panel, in a 3D-printed tabletop shell. Three
folders of work live here, each with its own README that is the detailed reference:

- `firmware/` — the p64 product firmware, started from zero on 2026-09-19. Its behaviour is
  fixed by the product specification `docs/spec/p64-spec.md` (settled with the user on
  2026-09-19, settings and limits tables included) and the decisions in `docs/adr/`; the
  vocabulary is `CONTEXT.md`. Its design is `firmware/docs/architecture.md`, its state of
  progress `firmware/docs/PROGRESS.md` (read that first when resuming), its API
  `firmware/docs/api.md`. The "Firmware" section below has the commands. Do not carry
  code or patterns over from `hardware-tests/` on your own initiative.
- `hardware-tests/` — the former `firmware/` (renamed on 2026-09-19; git history follows
  the rename, `git log --follow` works on its files): ESP-IDF v5.5 test firmware, C++20,
  directly on the `esphome/esp-hub75` DMA driver (vendored and patched under
  `hardware-tests/components/esp-hub75`). No Arduino, no LVGL, no Waveshare BSP (only
  its pin map was reused). It is the technical reference for the product firmware (what
  the hardware taught: pin map, driver patch, frame pacing, GDMA, network, microSD), not
  its architectural reference. The "Hardware tests" sections below describe it.
- `enclosure/` — the OpenSCAD shell, versioned as separate `.scad` files with outputs
  under `enclosure/output/vN/`.

Also: `firmware/reference/` is git-ignored upstream clones: Waveshare's example repo,
`p3a/` (the user's production ESP32-P4 pixel-art player, github.com/fabkury/p3a, the
reference for module boundaries, web UI and Makapix client) and `makapix/` (the Makapix
Club server, github.com/fabkury/makapix, the device contract in its `docs/player/` and
`docs/mqtt-api/`); re-clone commands are in `firmware/README.md`. `prompt/pNNN-*.txt` are the user's task
prompts, one per session, committed. `enclosure/archive/2026-09-dhruv-solidworks/` is a
friend's separate SolidWorks work: never edit it. `enclosure/inbox/` is an untracked
staging area for incoming material; do not commit it unless asked.

## Firmware

ESP-IDF v5.5.4, C++20, components under `firmware/components/` (`p64_gfx`, `p64_decode`,
`p64_display`, `p64_system`, `p64_playback`, `p64_storage`, `p64_content`, `p64_net`,
`p64_web`, vendored `esp-hub75` with the p64 patch, `animatedgif`, `libpng`, `libwebp`)
and the application in `firmware/main/` (`main.cpp` wiring, `show.cpp` the state
machine, `loader.cpp` the card I/O worker). Milestones M0 to M5 are done (2026-09-19):
display, decoders, storage, settings, Wi-Fi with setup mode, API v1 with WebSocket and
web UI, the content model (playsets, channels, scheduler, history). Makapix (M6),
widgets and fonts (M7), streams (M8), IMU/OTA/PIN (M9) and the full web UI (M10) are
pending; `firmware/docs/PROGRESS.md` has the table and the log with what was verified
on the device.

Commands, all from `firmware/` in PowerShell 7 (same scripts as the hardware tests):
`.\tools\build.ps1`, `.\tools\flash.ps1` (auto-detects COM13), `.\tools\monitor.ps1`,
`.\tools\idf.ps1 <args>`. Non-interactive console reads: `tools\serial_peek.py COM13
<seconds>` with the IDF venv's python (`C:\Espressif\tools\python\v5.5.4\venv`); it does
not reset the board, so to capture a boot log keep it open while
`POST /api/v1/action/reboot` restarts the firmware. Tests: `python tests\host\run.py`
(host build of the ESP-IDF-free components: unit tests plus the image corpora checked
pixel-exact against Pillow; needs gcc/g++ and the system Python with Pillow),
`python tests\device\api_smoke.py http://<ip> [--corpus]` and
`python tests\device\content_smoke.py http://<ip>` against the live device (the
development device answers at http://p64.local; its IP is in the boot log).

Facts that bite: `sdkconfig.defaults` is the source of truth and a changed default needs
`firmware/sdkconfig` deleted; Wi-Fi credentials live only in the git-ignored
`firmware/sdkconfig.secrets` (`CONFIG_P64_DEV_WIFI_SSID/PASSWORD`, seeded into NVS when
NVS has none) and are never echoed into summaries; `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`
is 4096, so anything large or long-lived (frames, indexes, task stacks of I/O tasks)
is placed in PSRAM explicitly; the core-1 idle watchdog is off on purpose (a slow
artwork keeps the player busy by design); both playback tasks live on core 1, all
network and storage tasks on core 0; the main task is the show loop and never does card
I/O.

## Hardware tests: commands

All from `hardware-tests/`, in PowerShell 7 (the scripts dot-source ESP-IDF themselves):

```
.\tools\build.ps1              # idf.py build
.\tools\flash.ps1              # build if needed + flash, auto-detects the board's COM port
.\tools\flash.ps1 -Monitor     # then open the serial console (Ctrl+] quits)
.\tools\monitor.ps1            # console only
.\tools\erase.ps1              # erase the whole flash
.\tools\port.ps1               # print the board's COM port
.\tools\idf.ps1 <args>         # any idf.py command, e.g. menuconfig, size-components
```

There are no unit tests; verification is flashing and reading the serial log. The board
logs a per-phase line with average fps, copy/wait/render times and sync counters.

Environment facts that bite:

- ESP-IDF v5.5.4 is the EIM install at `C:\esp\v5.5.4\esp-idf`, tools in
  `C:\Espressif\tools` (an older v5.5.2 classic install also exists; do not use it).
  `tools/env.ps1` holds the paths.
- `idf_tools.py` and friends refuse to run under Git Bash ("MSys/Mingw is not
  supported"): use the PowerShell tool for anything ESP-IDF.
- The board is native USB Serial/JTAG (VID 303A, PID 1001), COM13 on this laptop; the
  machine also has several Bluetooth COM ports, so always pass or detect an explicit
  port. To capture a boot log non-interactively, open the port with pyserial (dtr/rts
  false) and read; `idf.py monitor` wants a TTY. Pulsing `rts` true for 0.1 s did not
  reset the board on 2026-09-15; what worked was keeping the port open and reading
  while `GET /debug/reboot` restarts the firmware (no lines lost).
- `sdkconfig.defaults` is the source of truth (board, panel, pins, driver options);
  `sdkconfig` is generated and ignored. `dependencies.lock` is committed. Wi-Fi
  credentials live only in the git-ignored `hardware-tests/sdkconfig.secrets` (template:
  `sdkconfig.secrets.example`), applied by the project CMakeLists on top of the
  defaults; never write them anywhere else, and never echo the SSID or password
  into a summary (the Wi-Fi driver's own log lines print the SSID).
- Full white at brightness 255 draws close to the panel's 15 W rating; the 27 W supply
  goes on the POWER port, the laptop on the USB port. `P64_MAX_BRIGHTNESS` (menu "p64")
  caps every scene for laptop-only sessions.

## Hardware tests: architecture

`main.cpp` runs an array of `Scene`s in a loop (BOOT button skips ahead). The one scene,
`GifShowScene`, plays a random card GIF at boot (read directly in `enter()`, before the
loop runs), swaps to the first Makapix download the instant it lands, then to a fresh
download every 30 s or as soon as the next lands when late; nothing is embedded in the
binary any more and nothing but fresh downloads plays after the startup GIF (offline: the
current artwork stays up, the scene re-requests every 5 s). A `Scene`
(`scene.hpp`) implements `enter()` (must present its first frame) and `render()`, which
draws into a RAM `Frame` (RGB888, `display.hpp`) and returns whether the frame changed.
`Display` owns the `Hub75Driver`, builds its config entirely from `CONFIG_HUB75_*`, and
`present()` copies the frame in with one `draw_pixels()` call and flips.

The non-obvious part is frame pacing (`display.cpp`). The driver's `flip_buffer()` only
relinks the DMA descriptor chain; the DMA finishes the old front buffer first and gives
no signal. `Display` finds the LCD GDMA channel (`out.peri_sel == LCD`), learns the last
descriptor of both chains at start-up, clears the channel's end-of-frame flag before each
flip, and `wait_for_back_buffer()` sleeps until just before the predicted boundary, spins
on the flag, then checks that `dscr` has left the chain that was front (identified at flip
time from `dscr` itself: `eof_des_addr` lags one frame behind a switch and named the wrong
chain whenever two presents came within a frame, as at every artwork transition, after
which every wait ran into its timeout; testing against "the chain that just ended" breaks
as soon as presents are sparse). The loop is therefore locked to the panel refresh
(271.3 Hz: 10 bit planes at 20 MHz with the five lowest sent once per frame, see the
driver patch below; 32 MHz needs software TLS crypto, see the GDMA lesson below):
render into RAM right after `present()`,
then `wait_for_back_buffer()`, then `present()`. Keep that order, and keep the wait
blocking at least occasionally (it does), otherwise the idle task starves and the task
watchdog fires. Scenes return "dirty" only when something changed; unchanged frames are
not re-presented. Measured: 6.9 ms per `draw_pixels()` for 64x64 with 10 planes (5.8 ms
with 8), so the copy is the cost to watch before any faster refresh. The main task runs on core 1
(`ESP_MAIN_TASK_AFFINITY_CPU1`), Wi-Fi and lwIP on core 0.
Changing `sdkconfig.defaults` or `sdkconfig.secrets` does not touch an existing
generated `sdkconfig`: delete `hardware-tests/sdkconfig` (or use menuconfig) for a changed
default to take effect.

Driver: `esp-hub75` 0.3.6 is vendored under `hardware-tests/components/esp-hub75` with a patch
("p64 patch" markers, listed in its `P64-CHANGES.md`): planes at or below the driver's
transition bit get halving output-enable windows so `HUB75_MIN_REFRESH_RATE` no longer
collapses the levels (upstream gave them equal weight), the LUT is refitted to the real
on-times, the gamma 2.2 table is fixed (upstream mapped black to white), and getters
expose frame period, descriptor count and transition bit (Display relies on them).
Current setting 10 bits, minimum 250 Hz -> transition 4, 271.3 Hz, 1024 codes (plane 0
on a 50 ns pulse); the reasoning, the numbers and the neighbouring settings are in
`hardware-tests/README.md` "Tonal depth and refresh". The
released driver lacks things present on its git main (no `row_decoder`; `ICN2038S` is a
distinct enumerator). Brightness 0 blanks the panel; 1-255 go through a curve floored at
~17/255 on a 64-wide panel, and any value below 255 now costs the low planes first.

Makapix Club (`main/net/makapix.*`): a fetcher task on core 0 does two anonymous
HTTPS GETs per artwork (`/api/post?promoted=true&sort=random&limit=1&width_max=..&
height_max=..&file_format=gif` with `P64_MAKAPIX_MAX_DIMENSION`, default 64; the API
also takes `width_min`/`height_min`; then `/api/d/{sqid}.gif`), gated on Wi-Fi and NTP
(TLS needs the clock), handing bytes to the scene under a mutex. The server code is
github.com/fabkury/makapix (the user's own); `api/openapi.json` there is the contract.

Web control (`main/net/web.*`): `esp_http_server` on port 80 plus mDNS (`espressif/mdns`,
hostname `p64` -> p64.local). `/play?post=<Makapix URL or sqid>|url=<GIF URL>[&seconds=N]`
answers 202 at once and queues a `makapix::PlayRequest` (`/pattern` holds a tone test
pattern, `/stop` ends either; `file=<name>` plays a card file); the fetcher task serves those
ahead of the rotation (one TLS session at a time, on purpose: two at once ran internal
RAM out before), validates the GIF header and size (`P64_WEB_MAX_BYTES`,
`P64_WEB_MAX_DIMENSION`), and the scene picks the result up with `take_play()`,
pre-empting the 30 s slot until the time is up, `/stop`, or the next request (0 s =
indefinite). The scene publishes `web::NowPlaying` for `/status`. No authentication.

microSD card (`main/sdcard.*`): SDMMC 1-bit on CLK 1 / CMD 44 / D0 17 (the SDMMC host has
its own DMA, so card traffic did not disturb the panel), FAT at `/sdcard`, long file
names, formatted to FAT32 automatically when a card has no FAT (`P64_SD_FORMAT_IF_MOUNT_FAILED`).
GIFs live in the card's root. The web module owns the file endpoints (`/sd`, `PUT|GET|DELETE
/sd/<name>`, `/sd/play` playlist, `/sd/mount`); card reads for playback go through the
fetcher task (`PlayRequest::file`), never the rendering task. No card-detect line: a swapped
card needs `/sd/mount`.

Debug endpoints (`P64_DEBUG_ENDPOINTS`, on): `/debug` (panel health via `Display::health()`,
GDMA priority, timing), `/debug/dma?priority=N` (live), `/debug/stress[?loops=N]` (runs
`speedtest::run_now()`), `/debug/reboot`. `Display::instance()` gives the web module the
one Display.

Network: `sdkconfig.defaults` sets a 64 KB TCP window, lwIP buffers in PSRAM and
mbedTLS buffers internal + dynamic; measured numbers and the reasoning are in
`hardware-tests/README.md` "Network throughput". `P64_SPEEDTEST` (menuconfig, off) runs a
download test 45 s after boot and logs it; it downloads ~12 MB, so never leave it on.
Makapix Club is ~220 ms away and its throughput is erratic (20-220 KB/s); that is the
path, not the device.

GDMA lesson (2026-09-12, resolved 2026-09-13): the panel's LCD_CAM FIFO has no
back-pressure, so its GDMA channel must never lose arbitration for longer than the FIFO
lasts. The hardware AES/SHA engines also stream through GDMA, and with every channel at
priority 0 their bursts starved the panel at 32 MHz (descriptor pointer frozen, OUT_DONE
set, no EOF; only a reboot recovers). Fix: the vendored driver sets the panel channel to
GDMA priority 5 (`HUB75_GDMA_PRIORITY`); measured at 32 MHz under three passes of the
speed test with hardware crypto: zero timeouts, unchanged throughput, while priority 0
stalled within 2.6 s. The clock is 20 MHz by choice (LED pulse widths), not necessity.
The SD host has its own DMA and never disturbed the panel. Any new GDMA user (SPI, I2S)
should be checked with `/debug/stress` and `/debug`. Things that do not help: reserving
the panel pair's receive channel; restarting the Hub75Driver in place. `Display` logs
"panel DMA stalled" once when it detects the frozen pointer; `/debug` shows the flag.

GIF playback (`main/gif_player.*`): bitbank2/AnimatedGIF, vendored as
`hardware-tests/components/animatedgif` (Apache-2.0, one documented local patch), used in
`GIF_DRAW_RAW` mode with an RGB888 palette; `GifPlayer` composites the lines it gets
into an RGB888 canvas (transparency, all four disposal modes, black background) and
`Scaler` fits the canvas into 64x64 (nearest up, box-average down, black bars).
`gif_player.*` must stay free of ESP-IDF includes: `tools/gifcheck/gifcheck.py` builds
it natively with the harness in that folder and compares every frame of every GIF in
`assets/gifs/` against Pillow, pixel-exact; run it after touching the decoder, the
compositor, the scaler, or the assets. `assets/gifs/` is only that test corpus (and
the set the README's copy loop uploads to the card); it is not embedded in the firmware.

Orientation: the firmware drives the panel in native orientation (`ROTATE_0`): row 63 is
the native bottom, and the controller's USB-C ports sit behind the native right edge. The
enclosure turns the panel 90 degrees clockwise (front view); the matching setting is
`CONFIG_HUB75_ROTATE_90`, to be switched on when the shell is in use.

Style: `hardware-tests/.clang-format` (Google, 2 spaces, 120 columns), same as the driver.

## Enclosure

OpenSCAD, one file per version (`src/p64_enclosure.scad` = v1 as printed and ordered;
`_v2` .. `_v6` add features). **v6 is the version to print**: v4's panel-mount USB-C
sockets did not fit inside the v1 print, so v6 leaves two small 90-degree adapters on the
controller's ports behind one window in the back face and needs a hand-cut notch in the
panel frame's back plate (README, v6). v3, v4 and v5 are kept alternatives. Each version
writes the same file names into its own `output/vN/`
(`p64_enclosure_print.stl`/`.3mf`, `p64_enclosure_service.stl` with 0.45 mm clearance for
bureaus, `render_*.png`). The exact `openscad` commands per version and per render are in
`enclosure/README.md`, together with the design rationale, verified dimensions and the
list of things that could not be verified. Rules that have held across versions: never
modify v1; every wall or rib 2 mm or thicker; check any new geometry against the Waveshare
drawings in `enclosure/input/` and record hand measurements in `input/measurements.md`.

## Working conventions

- One commit per logical step, with a descriptive message; never push.
- Each area's README is the living record: when the hardware teaches something (pin map,
  orientation, driver quirk, measured numbers), write it there, not only in code comments.
- Numbers that come from Waveshare drawings or from measurement are stated with their
  source; guesses are labelled as such.
