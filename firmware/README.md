# p64 firmware

The firmware of the p64 device: a desktop 64x64 RGB LED matrix built on Waveshare's
ESP32-S3-RGB-Matrix driver board and RGB-Matrix-P2-64x64 panel (the repository README
lists the hardware).

This folder was started from zero on 2026-09-19. Nothing is decided yet: the device's
features and the firmware's architecture are the next discussion, and this README is the
only file until then.

## Specification

What the device does is fixed by `docs/spec/p64-spec.md` at the repository root, with
the vocabulary in `CONTEXT.md` and the decisions in `docs/adr/`. This folder's README will
describe how the firmware is built and flashed once it exists.

## Fonts and icons

`tools/gen_fonts.py` rasterises the bundled TTFs (`assets/fonts`, VEXED's Capital Hill
and Everyday Typical, CC BY 4.0) into `components/p64_gfx/src/fonts_data.cpp`;
`tools/gen_weather_icons.py` draws the weather icons into `assets/weather/*.png` (edit
the PNGs by hand and rerun to compile them in). Both need Pillow (the system Python)
and both outputs are committed, so the firmware build needs neither.

## HTTP API

Route reference: `docs/api.md`. Device tests: `tests/device/api_smoke.py` and
`tests/device/content_smoke.py` (both need a card with files; `--corpus` uploads the
host corpus).

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
changes: `python testsdevicepi_smoke.py http://p64.local --corpus --bench`.

## Relation to `hardware-tests/`

`hardware-tests/` is the previous `firmware/` folder: the test firmware that brought up
the driver board and the panel (git history follows the rename; `git log --follow` works
on its files). It is the technical reference for this project, not the architectural one:

- Take from it what the hardware taught: the pin map, the vendored and patched esp-hub75
  driver, the frame pacing against the panel's DMA, the tonal-depth and refresh numbers,
  the GDMA priority lesson, the network settings and measured throughput, the microSD
  wiring, the ESP-IDF tool scripts and environment facts.
- Do not take its architecture or code patterns (the scene loop, the module layout, the
  fetcher/web split) as given; they were shaped by testing one thing at a time, not by
  the device we want to build.

Its README, `hardware-tests/README.md`, holds the details and the measured numbers.

## Reference clones

`reference/` (git-ignored) holds local clones of upstream repositories, the way
`hardware-tests/reference/` did until 2026-09-19. Waveshare's example repository for the
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
