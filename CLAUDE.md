# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

p64 is a desktop 64x64 RGB LED matrix: Waveshare's ESP32-S3-RGB-Matrix driver board
plugged onto their RGB-Matrix-P2-64x64 panel, in a 3D-printed tabletop shell. Two lines
of work live here, each with its own README that is the detailed reference:

- `firmware/` — ESP-IDF v5.5 firmware, C++20, directly on the `esphome/esp-hub75` DMA
  driver (vendored and patched under `firmware/components/esp-hub75`). No Arduino, no
  LVGL, no Waveshare BSP (only its pin map was reused).
- `enclosure/` — the OpenSCAD shell, versioned as separate `.scad` files with outputs
  under `enclosure/output/vN/`.

Also: `reference/` is git-ignored upstream clones (Waveshare's example repo; the
re-clone command is in the root README). `prompt/pNNN-*.txt` are the user's task prompts,
one per session, committed. `dhruv/` is a friend's separate SolidWorks work: never move
or edit it. `enclosure/inbox/` is an untracked staging area for incoming material; do not
commit it unless asked.

## Firmware: commands

All from `firmware/`, in PowerShell 7 (the scripts dot-source ESP-IDF themselves):

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
  false), pulse `rts` true for 0.1 s to reset, then read; `idf.py monitor` wants a TTY.
- `sdkconfig.defaults` is the source of truth (board, panel, pins, driver options);
  `sdkconfig` is generated and ignored. `dependencies.lock` is committed. Wi-Fi
  credentials live only in the git-ignored `firmware/sdkconfig.secrets` (template:
  `sdkconfig.secrets.example`), applied by the project CMakeLists on top of the
  defaults; never write them anywhere else, and never echo the SSID or password
  into a summary (the Wi-Fi driver's own log lines print the SSID).
- Full white at brightness 255 draws close to the panel's 15 W rating; the 27 W supply
  goes on the POWER port, the laptop on the USB port. `P64_MAX_BRIGHTNESS` (menu "p64")
  caps every scene for laptop-only sessions.

## Firmware: architecture

`main.cpp` runs an array of `Scene`s in a loop (BOOT button skips ahead). A `Scene`
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
(145.8 Hz: 10 bit planes at 20 MHz with the four lowest sent once per frame, see the
driver patch below; 32 MHz needs software TLS crypto, see the GDMA lesson below):
render into RAM right after `present()`,
then `wait_for_back_buffer()`, then `present()`. Keep that order, and keep the wait
blocking at least occasionally (it does), otherwise the idle task starves and the task
watchdog fires. Scenes return "dirty" only when something changed; unchanged frames are
not re-presented. Measured: 6.9 ms per `draw_pixels()` for 64x64 with 10 planes (5.8 ms
with 8), so the copy is the cost to watch before any faster refresh. The main task runs on core 1
(`ESP_MAIN_TASK_AFFINITY_CPU1`), Wi-Fi and lwIP on core 0.
Changing `sdkconfig.defaults` or `sdkconfig.secrets` does not touch an existing
generated `sdkconfig`: delete `firmware/sdkconfig` (or use menuconfig) for a changed
default to take effect.

Driver: `esp-hub75` 0.3.6 is vendored under `firmware/components/esp-hub75` with a patch
("p64 patch" markers, listed in its `P64-CHANGES.md`): planes at or below the driver's
transition bit get halving output-enable windows so `HUB75_MIN_REFRESH_RATE` no longer
collapses the levels (upstream gave them equal weight), the LUT is refitted to the real
on-times, the gamma 2.2 table is fixed (upstream mapped black to white), and getters
expose frame period, descriptor count and transition bit (Display relies on them).
Current setting 10 bits, minimum 140 Hz -> transition 3, 145.8 Hz, 1024 levels; the
reasoning and the numbers are in `firmware/README.md` "Tonal depth and refresh". The
released driver lacks things present on its git main (no `row_decoder`; `ICN2038S` is a
distinct enumerator). Brightness 0 blanks the panel; 1-255 go through a curve floored at
~17/255 on a 64-wide panel, and any value below 255 now costs the low planes first.

Makapix Club (`main/net/makapix.*`): a fetcher task on core 0 does two anonymous
HTTPS GETs per artwork (`/api/post?promoted=true&sort=random&limit=1&width_max=..&
height_max=..&file_format=gif`, then `/api/d/{sqid}.gif`), gated on Wi-Fi and NTP
(TLS needs the clock), handing bytes to the scene under a mutex. The server code is
github.com/fabkury/makapix (the user's own); `api/openapi.json` there is the contract.

Web control (`main/net/web.*`): `esp_http_server` on port 80 plus mDNS (`espressif/mdns`,
hostname `p64` -> p64.local). `/play?post=<Makapix URL or sqid>|url=<GIF URL>[&seconds=N]`
answers 202 at once and queues a `makapix::PlayRequest` (`/pattern` holds a tone test
pattern, `/stop` ends either); the fetcher task serves those
ahead of the rotation (one TLS session at a time, on purpose: two at once ran internal
RAM out before), validates the GIF header and size (`P64_WEB_MAX_BYTES`,
`P64_WEB_MAX_DIMENSION`), and the scene picks the result up with `take_play()`,
pre-empting the 30 s slot until the time is up, `/stop`, or the next request (0 s =
indefinite). The scene publishes `web::NowPlaying` for `/status`. No authentication.

Network: `sdkconfig.defaults` sets a 64 KB TCP window, lwIP buffers in PSRAM and
mbedTLS buffers internal + dynamic; measured numbers and the reasoning are in
`firmware/README.md` "Network throughput". `P64_SPEEDTEST` (menuconfig, off) runs a
download test 45 s after boot and logs it; it downloads ~12 MB, so never leave it on.
Makapix Club is ~220 ms away and its throughput is erratic (20-220 KB/s); that is the
path, not the device.

GDMA lesson (hard-won, 2026-09-12): the hardware AES/SHA engines stream through GDMA and
their bursts starve the panel's LCD_CAM FIFO when the panel's DMA stream is fast; at
32 MHz the panel's DMA freezes mid-frame (descriptor pointer stuck, OUT_DONE set, no
EOF) and only a reboot recovers it. The user chose hardware crypto with the panel at
20 MHz (verified stall-free); 32 MHz requires `CONFIG_MBEDTLS_HARDWARE_AES=n` and
`_SHA=n`. Never raise the HUB75 clock without revisiting that. Expect the same from any
other heavy GDMA user (audio, SD). Things that were tried and do not help: reserving the
panel pair's receive channel; stopping and restarting the Hub75Driver in place (the DMA
does not come back). `Display` logs "panel DMA stalled" once when it detects the
frozen pointer.

GIF playback (`main/gif_player.*`): bitbank2/AnimatedGIF, vendored as
`firmware/components/animatedgif` (Apache-2.0, one documented local patch), used in
`GIF_DRAW_RAW` mode with an RGB888 palette; `GifPlayer` composites the lines it gets
into an RGB888 canvas (transparency, all four disposal modes, black background) and
`Scaler` fits the canvas into 64x64 (nearest up, box-average down, black bars).
`gif_player.*` must stay free of ESP-IDF includes: `tools/gifcheck/gifcheck.py` builds
it natively with the harness in that folder and compares every frame of every GIF in
`assets/gifs/` against Pillow, pixel-exact; run it after touching the decoder, the
compositor, the scaler, or the assets. The GIFs are embedded by `main/CMakeLists.txt`
(glob over `assets/gifs/*.gif`, generated `gif_assets.cpp` table); dropping a file in
the folder and rebuilding is all it takes.

Orientation: the firmware drives the panel in native orientation (`ROTATE_0`): row 63 is
the native bottom, and the controller's USB-C ports sit behind the native right edge. The
enclosure turns the panel 90 degrees clockwise (front view); the matching setting is
`CONFIG_HUB75_ROTATE_90`, to be switched on when the shell is in use.

Style: `firmware/.clang-format` (Google, 2 spaces, 120 columns), same as the driver.

## Enclosure

OpenSCAD, one file per version (`src/p64_enclosure.scad` = v1 as printed and ordered;
`_v2` .. `_v5` add features). **v4 is the version to print**; v3 and v5 are kept
alternatives. Each version writes the same file names into its own `output/vN/`
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
