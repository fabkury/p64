# p64

A small pixel-art display for your desk: 64×64 RGB LEDs in a 3D-printed shell,
powered over USB-C and controlled from a browser. Play animated artwork from a
microSD card or [Makapix Club](https://makapix.club), show clocks and widgets, or
stream pixels from your computer. The firmware and enclosure are open source.

![p64 in its printed shell on a desk next to a laptop](docs/images/photos/p64-hero.jpg)

**Finished and in daily use:** both variants work as designed. The maintainer's two
devices run around the clock, p64a since 2026-10-09 and p64b (the two knobs) since
2026-10-17. Firmware 0.2.0 is released and flashes from the browser; development
continues. [Project status](docs/status.md)

**[Build one](docs/build-your-own.md)** · **[Use it](docs/using-p64.md)** ·
**[Develop it](firmware/README.md)** · **[Documentation](docs/README.md)**

| ![Landscape clock on the finished p64](docs/images/photos/p64-clock.jpg) | ![Time overlay on mummy artwork on the assembled v7a](docs/images/photos/p64-mummy-laptop.jpg) | ![The assembled v7a held in one hand, viewed from behind](docs/images/photos/p64-back-in-hand.jpg) |
|---|---|---|
| A landscape clock | Time over artwork | The v7a shell in hand |

## What it does

- **Plays pixel art:** GIF, PNG/APNG, WebP and BMP, with transparency and pixel-aware
  scaling. Demanding animations slow down rather than skip frames.
- **Plays community artwork:** Makapix's Promoted channel needs no account. Pair for
  more channels, likes and sending artwork from the site. A card keeps cached art
  available offline; playsets mix channels.
- **Shows useful information:** clock faces and a time overlay, weather, air quality,
  UV, and the board's temperature sensor. A night schedule dims or blanks the panel.
- **Accepts live pixels:** DDP or raw p64 UDP from a computer, including screen capture
  with the bundled sender.
- **Offers browser controls:** mobile-friendly UI, live preview, optional PIN and a
  documented HTTP API. Tap gestures, auto-rotation and optional knobs add local control.
- **Supports firmware updates over Wi-Fi:** user-initiated installs with checksum
  verification and rollback. Pre-built releases flash from the browser.

Quality mode uses 11 bit planes at about 264 Hz; Photo mode raises refresh to about
814 Hz for cameras. Clocks require NTP after each boot. See the
[owner's guide](docs/using-p64.md) for operating limits.

| ![Synthwave pixel art on p64](docs/images/photos/p64-synthwave.jpg) | ![Yoshi artwork with the time overlay on v7a](docs/images/photos/p64-yoshi-laptop.jpg) | ![Car artwork photographed in low light](docs/images/photos/p64-night-car.jpg) |
|---|---|---|
| Colourful pixel art | Artwork with the time overlay | In low light |

![An animation playing in the shell](docs/images/photos/p64-playing.gif)

[More photos, playback clips and the web UI](docs/gallery.md) · [Clock-face comparison](docs/using-p64.md#clocks-and-widgets) · [Concept video of p64b](docs/video/output/p64b-concept-v4.mp4) (45 s, rendered from the enclosure source; [how it is made](docs/video/README.md))

## Choose a variant

| | p64a | p64b |
|---|---|---|
| Controls | Browser and tap gestures | Same, plus two rotary knobs |
| Soldering | None | Required for the encoder wiring |
| Shell | v7a | v7b |
| Status | Finished; assembled 2026-10-09, in daily use | Finished; assembled 2026-10-17, in daily use |

Both variants use the same firmware and remain supported. The build guide defaults
to p64a and links the extra p64b steps.

| ![Side view showing the p64 shell's wedge profile](docs/images/photos/p64-side.jpg) | ![Back of the assembled p64a with vents and two USB-C adapters](docs/images/photos/p64-back.jpg) |
|---|---|
| A slim tabletop shell | The assembled p64a, from behind |

## Build one

No printer, no breadboard, no custom boards. A p64 is four steps:

1. **Buy the parts online**: three items from Waveshare (controller, panel, supply)
   and two from Amazon (USB-C adapters, screws). p64b adds a few Adafruit parts.
2. **Order the shell from JLC3DP**: upload two files, choose black PLA, pay.
3. **Assemble**: p64a with a hex key; p64b after three small solder joints.
4. **Put the firmware on it**: from the browser with the
   [web flasher](https://fabkury.github.io/p64/web-flasher/), or with one esptool
   command ([flashing guide](docs/flash-firmware.md)). No ESP-IDF needed.

For scale, one p64a cost the maintainer about **US$119 delivered** (parts $86, shipping
$32, tax $1) and one p64b about **US$157** (parts $115, shipping $38, tax $4), at
the September and October 2026 prices paid, with shipping to New York. The
[build guide](docs/build-your-own.md#a-buy-the-parts) has every link and the
[build costs](docs/costs.md) page every invoice line.

[Follow the build guide →](docs/build-your-own.md)

## Documentation

| I want to… | Read |
|---|---|
| Assemble and set up a device | [Build guide](docs/build-your-own.md) |
| Add artwork, choose widgets or recover a device | [Owner's guide](docs/using-p64.md) |
| Check readiness and known limits | [Project status](docs/status.md) |
| Build or change the firmware | [Firmware guide](firmware/README.md) |
| Print or modify the shell | [Enclosure guide](enclosure/README.md) |
| Write a client | [HTTP API](firmware/docs/api.md) |
| Find specifications, decisions and history | [Documentation index](docs/README.md) |

## About

p64 is a sibling of [p3a](https://github.com/fabkury/p3a), the same author's ESP32-P4
pixel-art player, and a player for [Makapix Club](https://makapix.club).
Development history includes the [session prompts](prompt/) and
[hardware test records](firmware/reference/hardware-tests/README.md).

Project code and documentation use [Apache 2.0](LICENSE). Third-party components and
assets retain their own licenses; see [credits](docs/credits.md).
