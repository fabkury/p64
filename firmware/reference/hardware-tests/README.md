# p64 hardware tests

This folder was `firmware/` until 2026-09-19, then `hardware-tests/` at the repository
root until 2026-09-24, when it moved under `firmware/reference/` (tracked in git, unlike
the upstream clones beside it). It is the test firmware that brought up the driver board
and the panel, kept as technical reference (pin map, driver patch, frame pacing, tonal
depth, network settings, microSD, measured numbers) for the product firmware that is
being built from zero in `firmware/`. It still builds and flashes as described below;
every relative path in this document is from this folder.

ESP-IDF firmware for the Waveshare ESP32-S3-RGB-Matrix driver board and the
RGB-Matrix-P2-64x64 panel. Written in C++20 directly on top of the
[esphome/esp-hub75](https://github.com/esphome-libs/esp-hub75) DMA driver (pulled in
by the IDF component manager); no Arduino, no LVGL.

## What it does today: Makapix Club artwork with a clock

At boot the firmware plays a random GIF from the microSD card's root (black, with the
clock, when the card has none). In the background it asks Makapix Club for a random
promoted GIF that fits the panel and downloads it; the panel switches to that artwork
the moment it lands, and from then on to a fresh download every 30 s, or as soon as
the next one lands when it is late (Wi-Fi down, request failed, NTP not synced yet):
the current artwork stays up meanwhile and the fetcher keeps trying every few
seconds. Nothing but fresh downloads plays after the startup one; no GIF is embedded in
the firmware any more (the 64 in `assets/gifs/` are the test corpus of `gifcheck`, and
the copy loop under "microSD card" puts them on a card). GIFs
play at their intended speed, looping as needed; frame delays are honoured with the
browser rule (a delay under 20 ms is shown for 100 ms). Top-left, on a 70 % black box
(the artwork shows through at 30 % brightness), a 24-hour clock (HH:MM) set from NTP
over Wi-Fi; it reads `--:--` until the first sync. A GIF requested over the network
(see "Web control" below) pre-empts the rotation for its requested time, and GIF files
copied to the microSD card can be played one at a time or as a playlist (see "microSD
card").
Pressing **BOOT** restarts the scene.

### Makapix Club client (`main/net/makapix.*`)

Anonymous HTTPS, two requests per artwork, both against the public API of
<https://makapix.club> (its code is at <https://github.com/fabkury/makapix>):

```
GET /api/post?promoted=true&sort=random&limit=1&width_max=64&height_max=64&file_format=gif
GET /api/d/{public_sqid}.gif
```

The first returns one random promoted post with a GIF variant no larger than
`P64_MAKAPIX_MAX_DIMENSION` (64, the panel size, by default; the JSON carries sqid,
title, artist, size and the files list; 143 candidates at the time of writing); the
second is the GIF file itself. The API also takes `width_min`/`height_min`, which is
how the 128x128-only test below was run (of the first 200 promoted GIFs up to 128 px on
2026-09-15: 82 were 128x128, 82 64x64, 20 32x32, 16 16x16). A fetcher task on the Wi-Fi core
does the work: it waits for Wi-Fi and the NTP sync (TLS checks certificate dates),
skips artworks shown in the last 32 picks (re-drawing up to three times), caps the
download at `P64_MAKAPIX_MAX_BYTES` (1 MB), retries once, and hands the bytes to the
scene through a mutex. The scene logs title, artist and the page URL for each artwork.

Measured 2026-09-15 over 12 minutes of the show (serial log, 24 swaps, device already
running, home Wi-Fi at -49 dBm): the next artwork was ready 5.1 to 19.6 s after each
swap (median 5.9 s, mean 6.8 s, standard deviation 3.0 s; the fetch starts within a
millisecond of the swap, so this is the two HTTPS requests end to end), never late for
the 30 s slot; all 24 intervals were 30.0 s, no fetcher warning, no panel sync timeout.
The slowest three fetches (19.6, 10.8, 9.2 s) were the server's erratic throughput, not
retries. So the 30 s slot has 10 s of margin at the worst seen; a slot under ~20 s would
start to run late.

Same measurement the same day with the rotation restricted to 128x128 artworks only
(`P64_MAKAPIX_MAX_DIMENSION` 128 plus a temporary `width_min=128&height_min=128` in the
query; window from the first 30 s swap, 24 swaps, all 128x128, 3 KB to 443 KB, median
32 KB): ready 5.2 to 20.5 s after the swap (median 5.9 s, mean 7.3 s, standard deviation
3.4 s), never late, every interval 30.0 s, no warning, no sync timeout. The first
download took 20.5 s from boot (a 211 KB file; 10.7 s at 64 px). Decode plus box-average
downscale of a 128x128 frame to 64x64 cost 1.2 to 5.4 ms (median 2.3 ms) against 0.3 to
1.3 ms for 64x64 sources; the delivered GIF frame rates were unchanged (median 10 fps).
The stats line's "render ms per frame" reads 280 ms on a still image: that field sums
the render calls of a whole 10 s window over the frames presented (one), so it is an
accounting artifact, not a slow frame. Reverted to 64 afterwards.
User agent: `p64/<git version>`. Menu `p64 > Makapix Club` can disable it or change
the host.

### Web control (`main/net/web.*`): play a GIF on command

The device runs a small HTTP server on port 80 and announces itself over mDNS as
**p64.local** (hostname and an `_http._tcp` service; the DHCP hostname is `p64` too).
Endpoints:

| Request | Effect |
|---|---|
| `GET /play?post=<Makapix post URL or sqid>[&seconds=N]` | download that post's GIF and play it |
| `GET /play?url=<GIF URL>[&seconds=N]` | download any GIF (http or https) and play it |
| `POST /play` with the same fields form-encoded | same |
| `GET /stop` | end on-demand playback now; the rotation resumes |
| `GET /pattern` | hold a tone test pattern (grey and colour ramps of the darkest quarter, a full ramp, two shaded spheres) until `/stop` or the next `/play` |
| `GET /play?file=<name>[&seconds=N]` | play a GIF file from the microSD card |
| `GET /sd` | JSON: card info and its GIF files |
| `PUT /sd/<name>`, `GET /sd/<name>`, `DELETE /sd/<name>` | copy a GIF to the card, read it back, remove it |
| `GET /sd/play[?seconds=N]` | play every GIF on the card in turn (30 s each by default) until `/stop` or another request |
| `GET /sd/mount` | mount the card again after swapping it |
| `GET /debug` | panel DMA health (stall flag, cumulative timeouts and late flips), GDMA priority, timing, crypto settings |
| `GET /debug/dma?priority=N` | GDMA priority of the panel's channel, 0 to 5, applied at once |
| `GET /debug/stress[?loops=N]` | run the download speed test now (about 12 MB per pass, a heavy TLS load) |
| `GET /debug/reboot` | restart the board |
| `GET /status` | JSON: what is playing (name, source, size, seconds left), the last request (id, state, error), Wi-Fi IP and RSSI, heap |
| `GET /` | a form for phones |

`post` accepts `https://makapix.club/p/7fQw`, `makapix.club/p/7fQw` or just `7fQw`.
`seconds` defaults to 60 (`P64_WEB_DEFAULT_SECONDS`); `0` keeps the GIF up until the
next request or `/stop`. `/play` answers at once with `202 Accepted` and a JSON summary
(`id`, `sqid` or `url`, `seconds`); the fetcher task then downloads the file ahead of
the rotation's own requests and the scene switches as soon as it lands (3 to 10 s over
TLS to Makapix, less for plain HTTP). Failures (bad sqid, 404, not a GIF, too large,
offline) show up in `/status` under `request.error` and in the log. A newer request
replaces one still waiting; when the time is up, the rotation continues with a fresh
slot. Limits: files up to 4 MB and 512x512 px (`P64_WEB_MAX_BYTES`, `P64_WEB_MAX_DIMENSION`);
larger frames are scaled down to the panel like everything else. Anything on the LAN
can call these endpoints: there is no authentication.

```
curl "http://p64.local/play?post=https://makapix.club/p/Hqm"
curl "http://p64.local/play?post=Hqm&seconds=120"
curl "http://p64.local/play?url=https://vault.makapix.club/02/33/478727e5-b81c-4c4e-9742-0607fdb86d7f.gif&seconds=0"
curl http://p64.local/status
curl http://p64.local/stop
```

### microSD card (`main/sdcard.*`)

The board's card slot is wired to the ESP32-S3's SDMMC host in 1-bit mode (CLK GPIO 1,
CMD 44, D0 17, from Waveshare's BSP), which has its own DMA engine. The card is mounted
at boot as FAT at `/sdcard` with long file names; a card with no FAT file system (for
example a 64 GB card that comes as exFAT) is formatted to FAT32 on the first failed
mount (`P64_SD_FORMAT_IF_MOUNT_FAILED`, on by default: it erases the card). There is no
card-detect line, so a swapped card needs `GET /sd/mount`. GIF files live in the card's
root; other files are ignored.

Copying files works over HTTP because the card sits in the board: `curl -T file.gif
http://p64.local/sd/file.gif` for each file (any script, phone app or the front page's
drag-and-drop zone does the same with a `PUT`). Uploads stream to a `.part` file, are
checked for the GIF header, then renamed. Playback of a card file follows the same path
as a Makapix post: the fetcher task reads the file into PSRAM (1 to 9 ms for the files
tried) and the scene switches to it; `/sd/play` walks the whole card, one file per slot,
keeping the current file up until the next has loaded, skipping files that fail, and
looping until `/stop` or another request. `/status` shows the card (name, size, free
space, GIF count) and the playlist position.

Measured 2026-09-13 with a 32 GB card: the 64 Makapix GIFs (1.07 MB) copied in 38 s
with one curl call per file (the per-request HTTP overhead dominates; a 124 KB file took
1 s), a file read back byte-identical, the panel's frame pacing unaffected by card
traffic (SDMMC does not use the GDMA that the panel uses). Menu `p64 > microSD card`
holds the pins, the bus clock (20 MHz; lower it if the panel ever stalls during card
traffic) and the format policy.

```
for f in assets/gifs/*.gif; do curl -T "$f" "http://p64.local/sd/$(basename "$f")"; done
curl http://p64.local/sd
curl "http://p64.local/sd/play?seconds=10"
curl "http://p64.local/play?file=5PKj_32x32_piranha-plant.gif&seconds=20"
curl -X DELETE http://p64.local/sd/5PKj_32x32_piranha-plant.gif
```

Windows 10+, macOS, iOS and Linux with Avahi resolve `p64.local`; Android's browser
does not, so use the IP address from `/status` (or the router) there. Menu
`p64 > Web control` can disable the server or change hostname, port and limits.

Per GIF the log reports frames shown, fps, loops and decode+scale time per frame; every
10 s the main loop logs frames presented, render / wait / copy times, late flips and
sync timeouts. Frames are only presented when a GIF frame or the clock changed.

Artwork is scaled in the firmware to 64x64 with the aspect ratio kept: nearest
neighbour when enlarging (pixel art stays crisp), box average when shrinking, black
bars around the image. Transparent pixels show black.

Menu `p64` in menuconfig (`.\tools\idf.ps1 menuconfig`) holds the knobs: brightness cap,
Wi-Fi credentials, NTP server, timezone (POSIX TZ string, default New York), Makapix
on/off, host and download cap, the web control (on/off, hostname, port, default seconds,
size limits), seconds per artwork, and two switches that turn the show back into the
frame-rate test: ignore frame delays, and show the delivered fps top-right.

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

TLS uses the hardware AES and SHA engines (ESP-IDF's defaults). They stream through
GDMA, the shared DMA block that also feeds the panel, and on 2026-09-12 their bursts
stalled the panel whenever it ran at 32 MHz: the LCD FIFO underran, the DMA descriptor
pointer froze mid-frame and only a reboot recovered it (none at 20 MHz, none with
software crypto, and restarting the driver in place did not help). The root cause,
found on 2026-09-13, is GDMA arbitration: every channel sat at priority 0, so a long
crypto burst could hold the panel's channel off for longer than the panel's FIFO
lasts; the FIFO has no back-pressure, so it must never wait. The vendored driver now
sets the panel's channel to priority 5 (`HUB75_GDMA_PRIORITY`, see "GDMA priority
experiment" below), which made 32 MHz survive three passes of the download speed test
with hardware crypto. The display still logs "panel DMA stalled" once if a stall ever
happens, and `/debug` reports it.

#### GDMA priority experiment (2026-09-13)

32 MHz, 10 bit planes, transition bit 4 (434 Hz), hardware AES and SHA on. Health read
from `/debug` (cumulative timeouts and the stall flag), load from `/debug/stress` (one
pass = about 12 MB: two 1 MB and one 5 MB HTTPS download from Cloudflare, a 5 MB HTTP
download, seven Makapix downloads).

| Panel channel priority | Load | Result |
|---|---|---|
| 5 | 50 s of the show with its Makapix fetches | 0 timeouts |
| 5 | 2 stress passes back to back (about 140 s of TLS) | 0 timeouts, no stall |
| 5 | reboot, 1 more stress pass | 0 timeouts, no stall |
| 0 (switched live) | the show's next Makapix fetch | stalled after 2.6 s, every wait timed out from then on |

Throughput at priority 5 matched the earlier measurements: 531 KB/s for the 5 MB HTTPS
download, 476 KB/s HTTP, 360 KB/s per file from Makapix on a kept-alive connection. So
the panel's priority costs the network nothing measurable. The clock stays at 20 MHz
regardless, because 32 MHz shortens every LED pulse 1.6x (plane 0 would be 31 ns); the
option is open now, with or without hardware crypto. One side note: while the speed
test saturated the link, the web server stopped answering for about 30 s; the panel
was unaffected.

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

Adding artwork to the device: copy the `.gif` to the microSD card (see "microSD
card"); `assets/gifs/` is only the test corpus and is not embedded in the firmware.

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

The panel refreshes at 271.3 Hz (64x64, 10 bit planes with the five lowest sent once
per frame, 20 MHz HUB75 clock; see "Tonal depth and refresh" below). The driver
double-buffers, but its
`flip_buffer()` only relinks the DMA descriptor chain: the DMA keeps scanning the old
front buffer until that frame ends, and the driver gives no signal when it has
switched. Drawing into the back buffer too early tears.

`Display` therefore watches the LCD GDMA channel directly (`display.cpp`):

1. `present()` copies the RAM frame into the back buffer, notes which chain is front
   (the one containing the descriptor the channel is fetching, `dscr`; the last
   descriptors of both chains were learned at start-up from two frame boundaries),
   clears the channel's end-of-frame flag, then flips.
2. The scene renders the next frame into RAM straight away (rendering overlaps the
   panel's switch).
3. `wait_for_back_buffer()` sleeps until shortly before the predicted boundary
   (boundaries are exactly periodic), spins on the end-of-frame flag, then reads the
   channel's `dscr` register: if the descriptor being fetched still lies in the chain
   that was front at the flip, the flip missed the boundary (the DMA had already
   prefetched the last descriptor) and it waits for the next one. Testing against the
   chain noted at flip time, rather than the chain that just ended, is what keeps this
   correct when presents are sparse and many boundaries have passed in between.
   The front chain must not be identified from `eof_des_addr` either: it names the
   chain whose frame ended last, so for one frame after a switch it still points at
   the previous front. A present inside that window (every artwork transition presents
   two frames a few milliseconds apart) recorded the wrong chain, every later check
   then said "still old" until the timeout, and with 100 ms GIF delays the overdue next
   frame recreated the condition: 4 to 11 late flips per frame for minutes at a time
   (found and fixed on 2026-09-12).
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

The copy into the driver's bit-plane buffers is the limit: 5.8 ms with 8 planes, 6.9 ms
with the current 10 planes, almost two of the 3.69 ms refresh periods, so at most every
second refresh can carry a new frame (about 135 fps, far above any GIF). Going further
means shrinking the copy (dirty-rectangle updates, or a faster blit inside the driver).

## Tonal depth and refresh

An LCD receives gamma-encoded values, so dark tones get as many codes as bright ones;
the panel modulates light linearly in time, so with 8 bit planes the darkest quarter of
the input range (0-63 of 255) had only 11 distinct codes and dark artwork (Makapix Hqm,
a shaded brown ball) showed 2-3 flat shades. More planes double the frame time each,
and the driver's `HUB75_MIN_REFRESH_RATE` knob, which sends the low planes once instead
of repeating them, collapsed the levels in the released driver because it kept the same
output-enable window for every plane (8 bits forced to 150 Hz gave 66 levels, worse
than plain 7 bits).

The driver is therefore vendored under `components/esp-hub75` and patched (see its
`P64-CHANGES.md`): planes at or below the transition bit T are sent once with a halving
output-enable window, so every plane keeps its binary weight, and the LUT is refitted
to the planes' real on-times after each brightness change. Frame time is
32 rows x (T + 2^(bits-1-T)) transmissions x 64 pixels / clock, with T the smallest
value that reaches the minimum refresh rate. Current setting: 10 bits, minimum 250 Hz,
hence T = 4, 36 transmissions per row, 271.3 Hz, 1024 codes per channel; output-enable
windows 1/3/7/15/31 pixel clocks (50 ns to 1.55 us) for planes 0-4 and 62 for the rest.
The 50 ns pulse of plane 0 (1/1979 of full scale) may give little light, which would
merge neighbouring codes: the darkest quarter has 49 codes on paper and at least 25
(it had 11 at 8 bits). The step below, minimum 140 Hz -> T = 3, 145.8 Hz, keeps all
ten planes at 150 ns or longer (48 dark codes); the steps above are 465 Hz (T = 5,
plane 0 without a window, 13-25 dark codes) and 698 Hz (T = 6, 7-14, the old 8-bit
look). The clock stays at 20 MHz by choice: 32 MHz would shorten every pulse 1.6x. It is
no longer forced by the crypto engines (the panel's GDMA channel now has priority, see
"Wi-Fi credentials") nor by the SD card (its host has its own DMA). The tone curve is gamma 2.2 (the released driver's gamma
2.2 table was broken: black mapped to full white; fixed in the patch), matching the
sRGB monitors the artwork is made on. Photos need an exposure of one refresh or longer,
1/270 s. Use `/pattern` to judge the darks against a monitor. Brightness below 255
shortens every window and costs the low planes first, so dim in software if ever needed.

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

## Network throughput

Measured on 2026-09-12 with the show and the clock running, Wi-Fi at -50 dBm on a 40 MHz
802.11n channel, application-level (bytes handed to the application per second of
body transfer; "connect" is DNS + TCP + TLS + request). `P64_SPEEDTEST` in menuconfig
runs the test 45 s after boot and logs it (`main/net/speedtest.cpp`); it holds the
artwork fetcher meanwhile.

| Transfer | Default lwIP (5.7 KB window) | Tuned (64 KB window, TLS internal + dynamic) |
|---|---|---|
| CDN, HTTPS, 1 MB (Cloudflare, ~12 ms away) | 450-460 KB/s, connect 1.2 s | 130-460 KB/s (link-dependent), connect 1.1 s |
| CDN, HTTPS, 5 MB | 410-430 KB/s | 510 KB/s |
| Plain HTTP, 5 MB (~100 ms away) | 55 KB/s | 600 KB/s |
| Makapix Club, HTTPS, one 239 KB GIF, cold | 25 KB/s, connect 2.5 s | 30-220 KB/s, connect 2.5 s |
| Makapix Club, five GIFs (956 KB) on one kept-alive connection | 25 KB/s overall | 25-45 KB/s overall, single files 15-250 KB/s |

What the numbers say: with the default window a transfer moves 5.7 KB per round trip,
so speed is set by distance alone. With the 64 KB window a nearby server is limited by
the device itself at roughly 450-510 KB/s over HTTPS (about 4 Mbit/s; TLS record
processing on the Wi-Fi core), plain HTTP reaches 600 KB/s, and Makapix Club, about
220 ms away, swings between 20 and 220 KB/s from one transfer to the next regardless of
settings: that is the long path and its losses, not the device. A typical 100-250 KB
artwork therefore takes 5 to 15 s including the 2.5 s TLS handshake, which matches the
fetcher's own log. Keeping TLS buffers only in PSRAM cost throughput (240-390 KB/s from
the CDN) and was dropped; keeping them only in internal RAM failed mid-download once
the HTTP server and mDNS were added (about 35-40 KB of internal RAM is free at run time
with the 8-bit panel buffers). mbedTLS therefore uses the default allocator, internal
RAM first and PSRAM when that is short, and releases its buffers between records
(`MBEDTLS_DYNAMIC_BUFFER`), which also fixed the out-of-memory that two simultaneous
TLS sessions once caused.

## Layout

```
firmware/reference/hardware-tests/
  CMakeLists.txt          ESP-IDF project "p64"
  sdkconfig.defaults      every setting that differs from ESP-IDF defaults (board, panel, pins)
  partitions.csv          32 MB flash: nvs, otadata, phy, ota_0 (4 MB), ota_1 (4 MB), storage
  assets/gifs/            64 Makapix GIFs: the gifcheck test corpus, and what the card copy loop uploads
  components/animatedgif/ vendored bitbank2/AnimatedGIF decoder (see its README)
  components/esp-hub75/   vendored esphome/esp-hub75 0.3.6 with the p64 patch (see P64-CHANGES.md)
  main/
    CMakeLists.txt        sources and component requirements
    idf_component.yml     dependencies (espressif/mdns); dependencies.lock pins versions
    Kconfig.projbuild     menu "p64": brightness cap, Wi-Fi/NTP/TZ, Makapix, web control, microSD, GIF dwell, tests
    main.cpp              app_main: display, Wi-Fi + clock start, scene loop, BOOT restarts, stats log
    display.hpp/.cpp      Frame (RGB888 buffer) and Display (owns the Hub75Driver, frame-locked presents)
    button.hpp/.cpp       debounced BOOT button (GPIO0)
    scene.hpp             Scene interface: enter() + render(FrameInfo) per frame
    gif_player.hpp/.cpp   GifPlayer (decode + composite) and Scaler; no ESP-IDF dependencies
    sdcard.hpp/.cpp       microSD card: SDMMC 1-bit mount, listing, read, remove
    net/wifi.*            Wi-Fi station with reconnect
    net/clock.*           timezone + SNTP, local time of day
    net/makapix.*         background fetcher: random promoted GIFs for the show, on-demand downloads and card reads
    net/web.*             HTTP server + mDNS (p64.local): /play, /stop, /status, /pattern, /sd..., /debug..., / page
    net/speedtest.*       download throughput test (P64_SPEEDTEST at boot, or /debug/stress on demand)
    color.hpp             HSV to RGB
    font3x5.hpp           3x5 font (digits, colon, dash) for on-panel text
    scenes/gif_show.*     the show: card GIF at boot, then fresh Makapix artwork per slot, web requests, card playlist, clock
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
- microSD card: SDMMC 1-bit, CLK=1 CMD=44 D0=17 (D1-D3 not wired; CS=14 would be SPI
  mode), mounted at boot; verified 2026-09-13 with a 32 GB card at 20 MHz.
- Other board pins for later (schematic + Waveshare's `bsp/config.h`): I2C SDA=47 SCL=48;
  I2S MCLK=12 SCLK=43 LRCK=38 DOUT=21 DIN=39, PA enable=11; RTC INT=10; BOOT button=0.
  The 4-pin JST-SH "GPIO" socket (U8): pin 1 IO45, 2 IO46, 3 3V3, 4 GND, with 10 k
  pull-downs R59/R60 on IO45/IO46; the rotary-encoder wiring built on it is in
  `docs/hardware/encoders-*.md`.
- Panel: 64x64, 1/32 scan, standard wiring, shift driver set to **FM6126A** (what
  Waveshare's Arduino demos use). Verified working on 2026-09-08: correct image with
  this setting. The chip marking itself is still unread; GENERIC may work too.
- 10 bit planes (1024 codes per channel) with the five lowest sent once per frame,
  gamma 2.2, 20 MHz HUB75 clock: 271.3 Hz refresh (see "Tonal depth and refresh").
  History: 8 bits full BCM was 76 Hz; 7 bits (153 Hz) banded visibly and was reverted
  on 2026-09-12. 20 MHz is the fastest clock that coexists with hardware TLS crypto
  (32 MHz works with software crypto; no visible artefacts at 32 MHz on this panel
  although the FM6126A-class drivers are specified around 25-30 MHz). Double buffering.
- Photographing the panel: it is multiplexed (two rows lit at a time), so a short
  exposure captures a stripe of rows. Use a manual exposure of 1/30 s or longer, or
  lower the brightness so the phone picks a longer one; a faster refresh only shrinks
  the effect.
- Main task pinned to core 1; Wi-Fi, lwIP and the event loop on core 0.
- Brightness 0 blanks the panel; values 1-255 pass through a driver curve whose floor on
  a 64-wide panel is about 17/255 of output-enable time.

## Upstream references

- Board: <https://docs.waveshare.com/ESP32-S3-RGB-Matrix>, examples and schematic in
  <https://github.com/waveshareteam/ESP32-S3-RGB-Matrix> (cloned beside this folder under `../`, git-ignored; the
  clone command is in `firmware/README.md`).
- Panel: <https://docs.waveshare.com/RGB-Matrix-Px-64x64>.
- Driver: <https://github.com/esphome-libs/esp-hub75> (docs/ has menuconfig, troubleshooting
  and multi-panel guides).
