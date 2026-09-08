# p64 firmware

ESP-IDF firmware for the Waveshare ESP32-S3-RGB-Matrix driver board and the
RGB-Matrix-P2-64x64 panel. Written in C++20 directly on top of the
[esphome/esp-hub75](https://github.com/esphome-libs/esp-hub75) DMA driver (pulled in
by the IDF component manager); no Arduino, no LVGL.

## What it does today: the bring-up test

The firmware loops through four phases. Press the board's **BOOT** button to skip to
the next one at any time. Each phase change is logged on the serial console.

| # | Phase | Length | What to look for |
|---|---|---|---|
| 1 | Bouncing ball | 20 s | An orange ball drops and bounces on a blue floor line. **The floor is the panel's native bottom edge** (driver row 63). The L-shaped marker in the opposite corner is the native origin: white pixel = (0,0), red arm = +x, green arm = +y. |
| 2 | White fade, linear | 40 s | All pixels white. Brightness steps evenly through the driver's 0-255 scale from full to fully off (20 s) and back (20 s). Shows how the hardware behaves at each step. Note the driver floors non-zero values: on a 64-wide panel brightness 1 already means about 17/255 of OE time, so the last visible step is roughly 7 % and then off. |
| 3 | White fade, perceptual | 40 s | Same, but stepping CIE 1931 lightness evenly, so the change looks even to the eye. |
| 4 | Square hue wheel | 20 s | Hue by angle around the centre, saturation by square distance from the centre (pure hues on the edges, white in the middle). Makes one full turn in 20 s. |

Then it starts again at phase 1.

The panel is driven in its **native orientation** (`CONFIG_HUB75_ROTATE_0`).

Verified on the hardware on 2026-09-08: with the panel in native orientation, seen
from the front, the native bottom edge (the floor line) is at the bottom and the
controller board with its two USB-C ports sits behind the **right** edge. The enclosure
mounts the panel turned 90 degrees clockwise (front view) so the USB-C ports point down;
that puts the native right edge at the bottom. The driver setting for that is
`CONFIG_HUB75_ROTATE_90=y` (its transform maps image (x, y) to native (y, 63 - x), so
image "down" becomes native +x). Switch it in `sdkconfig.defaults` when the enclosure
arrives and the ball will again fall toward the physical bottom.

## Setup (Windows)

ESP-IDF **v5.5.x** installed with Espressif's Installation Manager (EIM). This machine
has v5.5.4 at `C:\esp\v5.5.4\esp-idf` with tools under `C:\Espressif\tools`; the
ESP32-S3 toolchain was added with

```
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
C:\Espressif\tools\python\v5.5.4\venv\Scripts\python.exe C:\esp\v5.5.4\esp-idf\tools\idf_tools.py install --targets esp32s3,esp32p4,esp32c6
```

(`idf_tools.py` refuses to run from Git Bash; use PowerShell.) `tools/env.ps1` holds
those paths; edit it if ESP-IDF lives elsewhere.

## Build, flash, monitor

Everything goes through the PowerShell scripts in `tools/`. They activate the ESP-IDF
environment themselves, so any PowerShell 7 window works:

```
.\tools\build.ps1              # idf.py build
.\tools\flash.ps1              # build if needed + flash; auto-detects the board's COM port
.\tools\flash.ps1 -Monitor     # same, then open the serial console
.\tools\monitor.ps1            # serial console only (Ctrl+] quits)
.\tools\erase.ps1              # erase the whole flash
.\tools\port.ps1               # print the board's COM port
.\tools\idf.ps1 menuconfig     # any other idf.py command
```

For a shell with the environment loaded, dot-source `tools\env.ps1` and use `idf.py`
directly.

Connect the laptop to the board's **USB** port (the one nearer the middle of the board;
the other one is **POWER**, power only). It enumerates as an Espressif USB Serial/JTAG
device (VID 303A, PID 1001); the scripts find it by that ID, or set `$env:P64_PORT`
or pass `-Port COMx`. Flashing needs no button dance; if it ever refuses, hold BOOT,
tap RESET, release BOOT.

Power the panel from the 27 W supply on the **POWER** port whenever brightness goes
above a few percent. A full-white panel at 255 draws close to the panel's 15 W rating.
Without USB-PD the supply gives the standard 3 A. If only a laptop cable is available,
lower `P64_MAX_BRIGHTNESS` in menuconfig (menu "p64") before flashing.

## Layout

```
firmware/
  CMakeLists.txt          ESP-IDF project "p64"
  sdkconfig.defaults      every setting that differs from ESP-IDF defaults (board, panel, pins)
  partitions.csv          32 MB flash: nvs, otadata, phy, ota_0 (4 MB), ota_1 (4 MB), storage
  main/
    idf_component.yml     dependencies (esphome/esp-hub75); dependencies.lock pins versions
    Kconfig.projbuild     menu "p64": P64_MAX_BRIGHTNESS
    main.cpp              app_main: runs the scenes in a loop, BOOT skips
    display.hpp/.cpp      Frame (RGB888 buffer) and Display (owns the Hub75Driver, built from sdkconfig)
    button.hpp/.cpp       debounced BOOT button (GPIO0)
    scene.hpp             Scene interface: enter() + render() per frame
    scenes/ball.*         phase 1
    scenes/fade.*         phases 2 and 3
    scenes/hue_wheel.*    phase 4
  tools/*.ps1             env activation and idf.py wrappers
```

Rendering model: scenes draw into a 64x64 RGB888 `Frame` in ordinary RAM; `Display::present()`
hands it to the driver with one `draw_pixels()` call and flips the driver's double buffer.
The main loop runs at 50 frames per second.

## Hardware facts baked into `sdkconfig.defaults`

- Module ESP32-S3-WROOM-2-N32R16V: 32 MB **octal** flash (OPI, 80 MHz), 16 MB **octal**
  PSRAM (80 MHz). PSRAM is enabled but the firmware boots even if it is not found.
- Console on the native USB Serial/JTAG (the board has no UART bridge chip).
- HUB75 pins (from the schematic, identical to Waveshare's example):
  R1=4 G1=5 B1=6 R2=7 G2=15 B2=16 A=18 B=8 C=3 D=42 E=9 LAT=40 OE=2 CLK=41.
- Other board pins for later (schematic + Waveshare's `bsp/config.h`): I2C SDA=47 SCL=48;
  I2S MCLK=12 SCLK=43 LRCK=38 DOUT=21 DIN=39, PA enable=11; SD card CLK=1 CMD=44 D0=17
  (CS=14 for SPI mode); RTC INT=10; BOOT button=0.
- Panel: 64x64, 1/32 scan, standard wiring, shift driver set to **FM6126A** (what
  Waveshare's Arduino demos use). Verified working on 2026-09-08: correct image with
  this setting. The chip marking itself is still unread; GENERIC may work too.
- 8-bit colour depth, CIE 1931 gamma, 20 MHz HUB75 clock, double buffering.

## Upstream references

- Board: <https://docs.waveshare.com/ESP32-S3-RGB-Matrix>, examples and schematic in
  <https://github.com/waveshareteam/ESP32-S3-RGB-Matrix> (cloned under `../reference/`).
- Panel: <https://docs.waveshare.com/RGB-Matrix-Px-64x64>.
- Driver: <https://github.com/esphome-libs/esp-hub75> (docs/ has menuconfig, troubleshooting
  and multi-panel guides).
