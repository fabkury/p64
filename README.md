# p64

A desktop 64 x 64 RGB LED matrix: USB-C powered, Wi-Fi connected, built around
Waveshare's ESP32-S3 HUB75 driver board and their P2 64 x 64 panel, in a 3D-printed
tabletop shell.

## Hardware

| Part | What it is | Links |
|---|---|---|
| Driver board ("the RGB matrix chip") | Waveshare ESP32-S3-RGB-Matrix: ESP32-S3-WROOM-2-N32R16V (32 MB octal flash, 16 MB octal PSRAM), HUB75 header, 2 x USB-C (POWER, USB), IMU, RTC, temperature/humidity sensor, audio codec + 2 mics, TF slot. Ships with an 8 ohm 5 W speaker, SH1.0 4-pin cable and screws. | [product](https://www.waveshare.com/esp32-s3-rgb-matrix.htm), [wiki](https://docs.waveshare.com/ESP32-S3-RGB-Matrix), [examples](https://github.com/waveshareteam/ESP32-S3-RGB-Matrix) |
| LED panel ("the LED matrix panel") | Waveshare RGB-Matrix-P2-64x64: 4096 RGB LEDs, 2 mm pitch, 1/32 scan, HUB75E in/out, 5 V VH4 power socket, 15 W max. 128 x 128 mm. | [product](https://www.waveshare.com/rgb-matrix-p2-64x64.htm?sku=33838), [wiki](https://docs.waveshare.com/RGB-Matrix-Px-64x64) |
| Power supply | Waveshare PSU-27W-USB-C-B (5.1 V, USB-C). Without USB-PD negotiation the board gets the standard 3 A. | [product](https://www.waveshare.com/psu-27w-usb-c-b.htm?sku=27775) |

The driver board plugs straight onto the panel's HUB75 IN header; its 5 V screw
terminals feed the panel's VH4 socket.

## Repository map

| Path | What it is |
|---|---|
| `firmware/` | ESP-IDF (v5.5) firmware for the driver board. See `firmware/README.md` for setup, build and flash. |
| `enclosure/` | The 3D-printed back shell: OpenSCAD source, ready-to-print STL/3MF, renders and the Waveshare drawings it was checked against. See `enclosure/README.md`. |
| `dhruv/` | A friend's separate SolidWorks take on the enclosure. Kept as-is. |
| `prompt/` | The task prompts that drove each development session, numbered in order. |
| `reference/` | Local clones of upstream repositories used for reference (git-ignored). Clone them with the commands below. |

### Reference clones

```
git clone https://github.com/waveshareteam/ESP32-S3-RGB-Matrix reference/ESP32-S3-RGB-Matrix
```

## Status

- Enclosure: designed and verified against Waveshare's drawings; a print was ordered from
  a bureau on 2026-09-05 and is expected within a week or two.
- Firmware: bring-up test (bouncing ball, brightness fades, square hue wheel). See
  `firmware/README.md`.
