# p64 firmware

The firmware of the p64 device: a desktop 64x64 RGB LED matrix built on Waveshare's
ESP32-S3-RGB-Matrix driver board and RGB-Matrix-P2-64x64 panel (the repository README
lists the hardware).

Started from zero on 2026-09-19 and built in ten milestones the same day (M0 to M10,
`docs/PROGRESS.md` has the table, the log and what was verified on the device). The
design is `docs/architecture.md`, the routes `docs/api.md`.

## Specification

What the device does is fixed by `docs/spec/p64-spec.md` at the repository root, with
the vocabulary in `CONTEXT.md` and the decisions in `docs/adr/`.

## Building, flashing, testing

ESP-IDF v5.5.4 (`tools/env.ps1` knows the install), PowerShell 7, from this folder:
`./tools/build.ps1`, `./tools/flash.ps1` (auto-detects the board's COM port),
`./tools/monitor.ps1`, `./tools/idf.ps1 <args>`. `sdkconfig.defaults` is the source
of truth; a changed default needs `sdkconfig` deleted. Wi-Fi credentials live only in
the git-ignored `sdkconfig.secrets`. Boot logs without a reset: `tools/serial_peek.py
COM13 <seconds>` with the IDF venv's python while `POST /api/v1/action/reboot`
restarts the firmware.

Host tests (`python tests/host/run.py`, gcc and the system Python with Pillow) build
the ESP-IDF-free parts: decoders against Pillow pixel for pixel, the content model,
fonts, widgets' pure parts, the stream protocol, the night rule, the time rules, taps and
orientation, the version rule. Device tests under `tests/device/` run against the
live device: `api_smoke`, `content_smoke`, `makapix_smoke [--paired]`,
`widgets_smoke`, `stream_smoke`, `ops_smoke`, `imu_smoke`, `pin_smoke`, `ota_smoke
[--no-install]`, `ui_smoke`, `cache_sweep_smoke [--delete]`, `panel_mode_smoke`,
`timing_smoke` (playback cadence), `ws_smoke` (the WebSocket push held open), and
`soak --minutes N` for an
unattended acceptance run. The resource floors (internal RAM free and largest block at
steady state and over a soak, core 0 busy share, image size, static internal RAM) live
in `budgets.json`: `api_smoke` and `soak` check the runtime ones against the device,
`tools/check_size.py` the static ones against a build; a change that breaks a floor
raises it there deliberately (rule of 2026-09-22, `docs/review-2026-09/`). Give the
timing-sensitive tests (`stream_smoke`, `soak`) the device's IP rather than `p64.local`:
on this laptop the first mDNS resolution of a process takes up to 3 s, which lands the
stream test's status read after the stream has gone silent (seen 2026-09-22; against the
IP the same run measures 29.7 fps with 0 incomplete frames).
Tools: `stream_send.py` (send pixels), `release_assets.py` (the GitHub release assets),
`cpu_sample.py` (per-task CPU shares and heap figures from `diag/memory`; heap owners
too when the build has `CONFIG_HEAP_TASK_TRACKING`), `check_size.py`, `gen_fonts.py`,
`gen_weather_icons.py`, `gen_ui_icons.py`.

## Fonts and icons

`tools/gen_fonts.py` rasterises the bundled TTFs (`assets/fonts`, VEXED's Capital Hill,
Everyday Slight, Standard, Typical and Ample, and High Birth, CC BY 4.0) into `components/p64_gfx/src/fonts_data.cpp`;
`tools/gen_weather_icons.py` draws the weather icons into `assets/weather/*.png` (edit
the PNGs by hand and rerun to compile them in). Both need Pillow (the system Python)
and both outputs are committed, so the firmware build needs neither.

## HTTP API

Route reference: `docs/api.md`. Device tests: `tests/device/api_smoke.py` and
`tests/device/content_smoke.py` (both need a card with files; `--corpus` uploads the
host corpus). `tests/device/panel_mode_smoke.py` switches Quality and Photo mode a dozen
times and checks the refresh rate, plane count, DMA streaming and the internal heap
after each (the panel modes are refresh profiles of the driver, `docs/architecture.md`
section 4; the 2026-09-20 rework is in `docs/PROGRESS.md`).

## Web UI

`components/p64_web/ui/`: `index.html` (Home), `playsets.html`, `settings.html`,
`update.html`, `static/common.css` and `static/theme.js` (p3a's stylesheet and themes,
copied verbatim; update them from `reference/p3a/webui/static/` when p3a's change),
`static/app.js` (shared helpers), `manifest.json` and the icons from
`tools/gen_ui_icons.py`. Everything is embedded in the image (ADR 0005); a change needs
a rebuild and flash. `tests/device/ui_smoke.py` checks the routes.

## Streams

Pixels over UDP: DDP on 4048 (LedFx, xLights, WLED tooling) and the raw p64 format on
4064 (RGB888, RGB565 or indexed, any size to 128x128, chunked by offset; the header is
in `docs/api.md`). `python tools\stream_send.py p64.local test` sends a moving
pattern, `... image.gif` any image or animation, `... screen --ddp --size 128` the PC
screen; `tests/device/stream_smoke.py` checks both protocols pixel for pixel and the
takeover.

## Decode benchmark (2026-09-19, first firmware milestone of spec 4.4)

Measured on the device with `GET /api/v1/diag/bench` (decode plus scaling to 64x64, per
frame, on the HTTP task on core 0 while the show ran on core 1; Pillow-made corpus from
`tests/host/corpus/`, two loops):

| File | Format | Canvas | Frames | Avg ms/frame | Max ms | Sustainable fps |
|---|---|---|---|---|---|---|
| gif_anim_32.gif | GIF | 32x32 | 8 | 0.98 | 1.07 | 1016 |
| apng_blend_48.png | APNG | 48x48 | 16 | 3.46 | 4.29 | 289 |
| apng_opaque_64.png | APNG | 64x64 | 12 | 4.73 | 5.37 | 212 |
| webp_anim_lossless_64.webp | WebP | 64x64 | 12 | 5.66 | 6.61 | 177 |
| apng_rgba_128.png | APNG | 128x128 | 8 | 22.30 | 25.04 | 45 |
| webp_anim_lossy_128.webp | WebP | 128x128 | 10 | 28.68 | 31.07 | 35 |
| png_rgba_128.png | PNG | 128x128 | 1 | 6.27 | 6.27 | (static) |
| png_rgb_256.png | PNG | 256x256 | 1 | 15.87 | 15.87 | (static) |
| bmp_24_64.bmp | BMP | 64x64 | 1 | 1.01 | 1.01 | (static) |

So 64x64 animations decode well inside the 16.7 ms budget of 60 fps in every format, and
128x128 animations sustain 35 to 45 fps: 128x128 at 60 fps plays in slow motion under
the no-drop rule (ADR 0003), as the spec allows. Run it again after decoder or scaler
changes: `python tests/device/api_smoke.py http://p64.local --corpus --bench`.

## Relation to `reference/hardware-tests/`

`reference/hardware-tests/` is the previous `firmware/` folder: the test firmware that
brought up the driver board and the panel (renamed `hardware-tests/` on 2026-09-19, moved
under `reference/` on 2026-09-24; git history follows both moves, `git log --follow` works
on its files). Unlike the clones beside it, it is tracked in git. It is the technical reference for this project, not the architectural one:

- Take from it what the hardware taught: the pin map, the vendored and patched esp-hub75
  driver, the frame pacing against the panel's DMA, the tonal-depth and refresh numbers,
  the GDMA priority lesson, the network settings and measured throughput, the microSD
  wiring, the ESP-IDF tool scripts and environment facts.
- Do not take its architecture or code patterns (the scene loop, the module layout, the
  fetcher/web split) as given; they were shaped by testing one thing at a time, not by
  the device we want to build.

Its README, `reference/hardware-tests/README.md`, holds the details and the measured numbers.

## Private area

`private/` is a separate repository, `github.com/fabkury/p64-private`, never published:
the user's own components (a private source of channels, ADR 0012). It is git-ignored
here and the pre-commit hook refuses its paths. The build finds it on its own
(`CMakeLists.txt`): its `components/` join the build, `sdkconfig.private` applies on top of
`sdkconfig.defaults`, the version ends in `+private`, `main.cpp` calls `p64::priv::start()`
under `CONFIG_P64_PRIVATE`, and `tests/host/run.py` compiles what
`private/tests/host/manifest.json` names. Without the folder the build is the public
firmware, which is what CI builds. To mount it on a fresh checkout, from `firmware/`:

```
git clone https://github.com/fabkury/p64-private private
```

A private build must not install a public release without knowing it drops the private
parts; the Update page says so (`private_build` in `/api/v1/update`).

## Reference clones

`reference/` holds local clones of upstream repositories, git-ignored (the tracked
`hardware-tests/` beside them is the exception). Waveshare's example repository for the
driver board (schematic, examples, pin map) is the one the hardware tests were written
against; to re-create it, from `firmware/`:

```
git clone https://github.com/waveshareteam/ESP32-S3-RGB-Matrix reference/ESP32-S3-RGB-Matrix
```

Two more clones live there since 2026-09-19 and are used as references by the
specification: `p3a/`, the user's production ESP32-P4 pixel-art player whose module
boundaries, web UI and Makapix client p64 follows, and `makapix/`, the Makapix Club
server, whose `docs/player/`, `docs/mqtt-api/` and `api/openapi.json` are the device
contract:

```
git clone https://github.com/fabkury/p3a reference/p3a
git clone https://github.com/fabkury/makapix reference/makapix
```
