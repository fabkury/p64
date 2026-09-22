# p64

A small pixel-art display for your desk: 64 x 64 RGB LEDs behind a 3D-printed shell,
powered over USB-C, controlled from a browser. It plays animated pixel art from a microSD
card and from [Makapix Club](https://makapix.club), shows a clock, the weather and the room
temperature, and takes live pixel streams from your computer. Open source, built from two
off-the-shelf Waveshare boards, no soldering.

![p64 on a desk, playing an animation with the clock overlay](docs/images/photos/p64-hero.jpg)

## What it does

- **Plays pixel art the way it was drawn.** GIF, PNG/APNG, WebP and BMP, static or animated,
  with transparency. Small canvases are scaled up by whole numbers so pixels stay square;
  large ones are scaled down cleanly. Every frame is shown, none skipped.
- **Looks right.** 10-bit colour per channel at a 271 Hz refresh, gamma 2.2, seamless
  changes from one artwork to the next: no blank frame, no flicker, no brightness step.
  A "Photo" mode raises the refresh to 814 Hz for cameras.
- **Runs on its own.** Put files on a microSD card, or pair it with Makapix Club and it
  plays channels of community pixel art, refreshing its cache over Wi-Fi. Playsets group
  channels; a scheduler picks what plays when. Offline, it keeps playing what it has.
- **Is a clock too.** A time overlay on artworks, a digital and an analogue clock face,
  weather from Open-Meteo, the room temperature from the board's own sensor, and a night
  schedule that dims or blanks the panel.
- **Takes a live feed.** Send pixels over UDP with DDP (LedFx, xLights, WLED tooling) or
  the raw p64 format; a bundled script mirrors any image, animation or a region of your
  screen to the panel.
- **Is controlled from a browser.** A mobile-friendly web UI on `http://p64.local`
  (five colour themes, installable as a web app), a documented HTTP API with WebSocket
  push, an optional PIN. A tap on the shell skips to the next artwork; the IMU turns the
  picture upright whichever way you stand it.
- **Updates itself.** Firmware updates come from GitHub Releases with a verified install
  and rollback. Nothing is downloaded or installed without you pressing the button.

| ![Analogue clock face](docs/images/photos/p64-clock-face.jpg) | ![An artwork with the time overlay](docs/images/photos/p64-artwork.jpg) | ![Next to a laptop, for scale](docs/images/photos/p64-on-the-desk.jpg) |
|---|---|---|
| The analogue clock face | Time overlay on an artwork | 128 mm square, for scale |

![A short clip of an animation playing with the clock overlay](docs/images/photos/p64-playing.gif)

The web UI, on a phone:

| ![Home page](docs/images/web-ui-home.png) | ![Playsets page](docs/images/web-ui-playsets.png) | ![Settings page](docs/images/web-ui-settings.png) |
|---|---|---|
| Home: live preview, controls, channels | Playsets: your mixes and the built-ins | Settings: display, widgets, network, Makapix |

More photos:

| ![The bare panel on the bench, showing its first animation](docs/images/photos/p64-bare-panel-first-light.jpg) | ![The empty v1 shell seen from above](docs/images/photos/p64-shell-v1-empty-top.jpg) | ![The driver board and its leads behind the panel](docs/images/photos/p64-panel-back-wiring.jpg) |
|---|---|---|
| First light, before the shell | The v1 shell, empty | The driver board behind the panel |
| ![The first artwork in the shell](docs/images/photos/p64-first-artwork-in-shell.jpg) | ![p64 held in one hand](docs/images/photos/p64-in-hand.jpg) | ![An artwork in a dark room](docs/images/photos/p64-artwork-in-a-dark-room.jpg) |
| First artwork in the shell | In the hand | Lights off |
| ![The weather widget](docs/images/photos/p64-brick-wall-weather.jpg) | ![A mosaic of small artworks](docs/images/photos/p64-brick-wall-artwork-mosaic.jpg) | ![The whole desk, p64 at the left](docs/images/photos/p64-desk-wide.jpg) |
| The weather widget | A mosaic artwork | On the desk |

The rest are in [`docs/images/photos/`](docs/images/photos/).

## What you need

Three parts from Waveshare plug together, plus a print and a handful of screws. List
prices on waveshare.com on 2026-09-21, before shipping:

| Part | What it is | Price |
|---|---|---|
| [ESP32-S3-RGB-Matrix](https://www.waveshare.com/esp32-s3-rgb-matrix.htm) | The driver board: ESP32-S3 with 32 MB flash and 16 MB PSRAM, Wi-Fi, HUB75 header, two USB-C ports, IMU, real-time clock, temperature sensor, microSD slot. Ships with the power lead and screws. [Wiki](https://docs.waveshare.com/ESP32-S3-RGB-Matrix). | $24.99 |
| [RGB-Matrix-P2-64x64-B](https://www.waveshare.com/rgb-matrix-p2-64x64.htm?sku=33838) | The panel: 4096 RGB LEDs at a 2 mm pitch, 128 x 128 mm, GOB version (a protective layer over the LEDs). The [standard version](https://www.waveshare.com/rgb-matrix-p2-64x64.htm) without the layer is $28.99 and fits the same shell. [Wiki](https://docs.waveshare.com/RGB-Matrix-Px-64x64). | $31.99 |
| [PSU-27W-USB-C-B](https://www.waveshare.com/psu-27w-usb-c-b.htm?sku=27775) | A 5.1 V USB-C supply; any good 5 V 3 A USB-C supply works. US, EU and UK plugs. | $6.99 |
| The shell | One support-free 3D print, about 80 g of PLA or PETG on a 140 x 140 mm bed, or a print bureau. Files in `enclosure/output/`. The v1 shell in the photos was printed by JLC3DP in black PLA for $18.83, shipping included. | $18.83 at a bureau, a few dollars of filament at home |
| Small parts | Six M3 x 10 screws, a USB-C cable; for the current shell version two small 90-degree USB-C adapters. A microSD card is optional. | a few dollars |

About $64 in electronics plus the print, so a little over $80 before the electronics'
shipping. You also need a computer with a USB-C cable to
flash the firmware the first time; after that the device updates itself over Wi-Fi.

## Build one

[docs/build-your-own.md](docs/build-your-own.md) is the step-by-step guide. In short:

1. Order the parts above and print `enclosure/output/v7/p64_enclosure_print.stl` plus
   the small `p64_cradle_insert.stl` next to it (or `v1/`, the version that has been
   printed and verified; see the status below).
2. Install ESP-IDF v5.5, build the firmware from `firmware/` and flash it over the
   board's USB port.
3. Plug the board onto the panel, connect the power lead, slide the panel into the shell
   and fit the six screws.
4. Power it on. It opens a `p64-setup` Wi-Fi network; join it, pick your network in the
   portal, and open `http://p64.local` in a browser.

## Status

p64 is a working prototype. One device runs the firmware every day, and every feature in
[the specification](docs/spec/p64-spec.md) is implemented and checked on it. What is not
there yet, as of 2026-09-21:

- **No firmware release yet.** Building one today means compiling from source with
  ESP-IDF. A first GitHub release with a ready-to-flash image is planned; the updater is
  already in the firmware and waits for it.
- **The shell in the photos is v1.** The current design, v7, adds a window for the USB-C
  cables and mounts for two rotary knobs, and is not printed yet (v7 is v6 with room for
  the measurement errors around the USB-C window, settled on 2026-09-22). v1 is proven
  and needs a right-angle USB-C cable instead.
- **The two knobs are designed, not wired.** Their wiring projects are in
  `docs/hardware/`; the firmware has the input hooks ready.
- **Some acceptance measurements** in the specification still need instruments.

## Repository map

| Path | What it is |
|---|---|
| `firmware/` | The product firmware: ESP-IDF v5.5, C++20, on a patched `esp-hub75` driver. Its README has the build, flash and test commands; `firmware/docs/` has the architecture, the API and the progress log. |
| `enclosure/` | The OpenSCAD shell, one file per version, with ready-to-print STL/3MF and renders under `enclosure/output/vN/`. Its README has the design rationale and the print settings. |
| `docs/spec/p64-spec.md`, `CONTEXT.md`, `docs/adr/` | What the device does (the specification), the vocabulary, and the decisions that are hard to reverse. |
| `docs/build-your-own.md`, `docs/images/` | The build guide, the photos (`photos/`) and the web UI screenshots. |
| `docs/hardware/` | Wiring projects for the two rotary encoders, with schematics. |
| `hardware-tests/` | The test firmware that brought up the board and panel; the technical reference for what the hardware taught (pin map, driver patch, frame pacing, DMA). |
| `prompt/` | The task prompt of each development session, in order. |

## About

p64 is a sibling of [p3a](https://github.com/fabkury/p3a), the same author's ESP32-P4
pixel-art player, and a "player" for [Makapix Club](https://makapix.club), a community
site for pixel art whose [server](https://github.com/fabkury/makapix) is open source too.
The firmware, the shell and the documentation were developed with Claude Code, session by
session; the prompts are committed under `prompt/`, and every hardware finding was
verified on the device.

Licensed under the [Apache License 2.0](LICENSE). Bundled fonts are VEXED's Capital Hill
and Everyday Typical (CC BY 4.0); the LED driver is
[esp-hub75](https://github.com/esphome/esp-hub75) with a local patch, the GIF decoder is
bitbank2's AnimatedGIF, and libpng and libwebp decode the rest.
