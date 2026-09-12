# p64 firmware

ESP-IDF firmware for the Waveshare ESP32-S3-RGB-Matrix driver board and the
RGB-Matrix-P2-64x64 panel. Written in C++20 directly on top of the
[esphome/esp-hub75](https://github.com/esphome-libs/esp-hub75) DMA driver (pulled in
by the IDF component manager); no Arduino, no LVGL.

## What it does today: the GIF show with a clock

The firmware plays the GIFs embedded from `assets/gifs/` (64 Makapix Club artworks,
1.1 MB) at their intended speed, in an order shuffled at boot, 30 s per GIF (looping as
needed), wrapping around after the last one. Frame delays are honoured with the browser
rule: a delay under 20 ms is shown for 100 ms. Top-left, on a black box, a 24-hour
clock (HH:MM) set from NTP over Wi-Fi; it reads `--:--` until the first sync. Pressing
**BOOT** restarts the scene with a new shuffle.

Per GIF the log reports frames shown, fps, loops and decode+scale time per frame; every
10 s the main loop logs frames presented, render / wait / copy times, late flips and
sync timeouts. Frames are only presented when a GIF frame or the clock changed.

Artwork is scaled in the firmware to 64x64 with the aspect ratio kept: nearest
neighbour when enlarging (pixel art stays crisp), box average when shrinking, black
bars around the image. Transparent pixels show black.

Menu `p64` in menuconfig (`.\tools\idf.ps1 menuconfig`) holds the knobs: brightness cap,
Wi-Fi credentials, NTP server, timezone (POSIX TZ string, default New York), seconds
per GIF, and two switches that turn the show back into the frame-rate test: ignore
frame delays, and show the delivered fps top-right.

Earlier scenes (bouncing ball, white fades, full-power white, the rotating square hue
wheel) live in git history (`git log -- main/scenes`).

### Wi-Fi credentials

Copy `sdkconfig.secrets.example` to `sdkconfig.secrets` (git-ignored) and fill in the
SSID and password; the project's `CMakeLists.txt` applies it on top of
`sdkconfig.defaults`. Like any defaults file it only reaches an existing generated
`sdkconfig` after `menuconfig` or after deleting `sdkconfig`. Without it the firmware
runs offline and the clock stays at `--:--`. The network stack, lwIP and SNTP run on
core 0; the main task (rendering) is pinned to core 1 so traffic never delays a frame.
First sync after boot takes 5 to 30 s (Wi-Fi join, DNS, SNTP's own start-up delay).

### GIF pipeline

`components/animatedgif` is a vendored copy of bitbank2/AnimatedGIF (Apache-2.0, see
its README for the one local patch). It is used in RAW mode: the library decodes LZW
and hands `GifPlayer` (`main/gif_player.*`) one line at a time with the palette, the
frame rectangle, the transparency index and the disposal method; `GifPlayer`
composites into an RGB888 canvas of the GIF's logical size, honouring all four disposal
modes (the library itself does not implement "restore previous"), and `Scaler` fits the
canvas into the 64x64 frame. `gif_player.*` has no ESP-IDF dependencies on purpose.

`tools/gifcheck/gifcheck.py` builds that exact code natively (needs g++ and Pillow),
decodes every GIF in `assets/gifs/`, and compares every frame's canvas against Pillow
and every scaled frame against a Python twin of the scaler, pixel-exact. Run it after
touching the decoder, the compositor, the scaler or the assets:

```
python tools\gifcheck\gifcheck.py          # all GIFs; exit code 0 when everything matches
python tools\gifcheck\gifcheck.py a.gif    # specific files
```

Adding artwork: drop the `.gif` into `assets/gifs/` and rebuild; `main/CMakeLists.txt`
globs the folder, embeds the files in flash and generates the `kGifAssets` table.

The panel is driven in its **native orientation** (`CONFIG_HUB75_ROTATE_0`).

Verified on the hardware on 2026-09-08: with the panel in native orientation, seen
from the front, the native bottom edge (the floor line) is at the bottom and the
controller board with its two USB-C ports sits behind the **right** edge. The enclosure
mounts the panel turned 90 degrees clockwise (front view) so the USB-C ports point down;
that puts the native right edge at the bottom. The driver setting for that is
`CONFIG_HUB75_ROTATE_90=y` (its transform maps image (x, y) to native (y, 63 - x), so
image "down" becomes native +x). Switch it in `sdkconfig.defaults` when the enclosure
arrives and the ball will again fall toward the physical bottom.

## Frame pacing: locked to the panel refresh

The panel refreshes at 244.1 Hz (64x64, 7 bit planes, 32 MHz HUB75 clock; 8 bits give
122 Hz and 20 MHz with 8 bits 76 Hz, both photographing with more row banding). The
driver double-buffers, but its
`flip_buffer()` only relinks the DMA descriptor chain: the DMA keeps scanning the old
front buffer until that frame ends, and the driver gives no signal when it has
switched. Drawing into the back buffer too early tears.

`Display` therefore watches the LCD GDMA channel directly (`display.cpp`):

1. `present()` copies the RAM frame into the back buffer, notes which chain is front
   (the channel's `eof_des_addr`, the last descriptor of the chain the DMA is looping
   in), clears the channel's end-of-frame flag, then flips.
2. The scene renders the next frame into RAM straight away (rendering overlaps the
   panel's switch).
3. `wait_for_back_buffer()` sleeps until shortly before the predicted boundary
   (boundaries are exactly periodic), spins on the end-of-frame flag, then reads the
   channel's `dscr` register: if the descriptor being fetched still lies in the chain
   that was front at the flip, the flip missed the boundary (the DMA had already
   prefetched the last descriptor) and it waits for the next one. Testing against the
   chain noted at flip time, rather than the chain that just ended, is what keeps this
   correct when presents are sparse and many boundaries have passed in between.
4. Only then does the next `present()` copy into the freed buffer.

Results on the hardware (2026-09-12), one new frame per refresh in every case:

| Scene | HUB75 clock | Refresh | Delivered | Per frame: copy / render / wait | Late flips per 10 s |
|---|---|---|---|---|---|
| Ball | 20 MHz | 76.3 Hz | 76.0 fps | 5.8 / 0.04 / 7.3 ms | 3 |
| Ball | 32 MHz | 122.1 Hz | 122.0 fps | 5.8 / 0.04 / 2.4 ms | 0 |
| GIFs (16 to 64 px) | 32 MHz | 122.1 Hz | 122.1 fps | 5.8 / 1.0 to 1.4 / 1.0 to 1.4 ms | 0 |

For these small GIFs decode plus scale costs 1.0 to 1.4 ms per frame (a single-frame
GIF re-parses its header every frame), so the copy still dominates and the frame rate
stays at the refresh cap. Larger artwork would eat into the remaining ~1 ms of headroom
and start dropping to every other refresh.

"Late flips" are flips that landed in the DMA's prefetch window and cost one extra
refresh. If the GDMA channel cannot be found the wait falls back to a full refresh
period after each flip ("timed fallback" in the log, about 50 fps at 76 Hz).

The main task must block at least once in a while or the idle task on core 0 starves
and the task watchdog fires every 5 s; the wait sleeps whenever a whole tick of slack
exists and forces a one-tick yield once a second otherwise.

The copy into the driver's bit-plane buffers is now the limit: 5.8 ms of the 8.2 ms
period. A faster refresh (7-bit depth, or a higher `HUB75_MIN_REFRESH_RATE`) would
leave too little room for it; going further means shrinking the copy (dirty-rectangle
updates, or a faster blit inside the driver).

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
  assets/gifs/            the GIFs embedded in the firmware (+ Makapix manifest.json, not embedded)
  components/animatedgif/ vendored bitbank2/AnimatedGIF decoder (see its README)
  main/
    CMakeLists.txt        sources, embeds assets/gifs/*.gif, generates the gif_assets table
    idf_component.yml     dependencies (esphome/esp-hub75); dependencies.lock pins versions
    Kconfig.projbuild     menu "p64": brightness cap, Wi-Fi/NTP/TZ, GIF dwell, test switches
    main.cpp              app_main: display, Wi-Fi + clock start, scene loop, BOOT restarts, stats log
    display.hpp/.cpp      Frame (RGB888 buffer) and Display (owns the Hub75Driver, frame-locked presents)
    button.hpp/.cpp       debounced BOOT button (GPIO0)
    scene.hpp             Scene interface: enter() + render(FrameInfo) per frame
    gif_player.hpp/.cpp   GifPlayer (decode + composite) and Scaler; no ESP-IDF dependencies
    gif_assets.hpp        the embedded-GIF table (generated .cpp lives in build/)
    net/wifi.*            Wi-Fi station with reconnect
    net/clock.*           timezone + SNTP, local time of day
    color.hpp             HSV to RGB
    font3x5.hpp           3x5 font (digits, colon, dash) for on-panel text
    scenes/gif_show.*     the GIF show scene (clock overlay, optional fps overlay / max speed)
  sdkconfig.secrets.example  template for the git-ignored Wi-Fi credentials file
  tools/*.ps1             env activation and idf.py wrappers
  tools/gifcheck/         PC check of the GIF pipeline against Pillow
```

Rendering model: scenes draw into a 64x64 RGB888 `Frame` in ordinary RAM; `Display::present()`
hands it to the driver with one `draw_pixels()` call and flips the driver's double buffer.
The main loop presents one frame per panel refresh.

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
- 7-bit colour depth (128 levels per channel), CIE 1931 gamma, 32 MHz HUB75 clock:
  244 Hz refresh, chosen so phone photos show as little row banding as the driver
  allows. 8 bits at 32 MHz (122 Hz) and 8 bits at 20 MHz (76 Hz) were verified too; no
  visible artefacts at 32 MHz on this panel although the FM6126A-class drivers are
  specified around 25-30 MHz. Double buffering.
- Photographing the panel: it is multiplexed (two rows lit at a time), so a short
  exposure captures a stripe of rows. Use a manual exposure of 1/30 s or longer, or
  lower the brightness so the phone picks a longer one; a faster refresh only shrinks
  the effect.
- Main task pinned to core 1; Wi-Fi, lwIP and the event loop on core 0.
- Brightness 0 blanks the panel; values 1-255 pass through a driver curve whose floor on
  a 64-wide panel is about 17/255 of output-enable time.

## Upstream references

- Board: <https://docs.waveshare.com/ESP32-S3-RGB-Matrix>, examples and schematic in
  <https://github.com/waveshareteam/ESP32-S3-RGB-Matrix> (cloned under `../reference/`).
- Panel: <https://docs.waveshare.com/RGB-Matrix-Px-64x64>.
- Driver: <https://github.com/esphome-libs/esp-hub75> (docs/ has menuconfig, troubleshooting
  and multi-panel guides).
