# Build your own p64

This is the walk-through for building a p64 from the parts list in the
[README](../README.md). It assumes you can use a 3D printer or a print bureau, a
screwdriver and a command line. No soldering: the two boards plug together and the
power lead goes into screw terminals.

The state of things on 2026-09-21: the firmware is complete for version 1 and runs
daily on one device, but there is no ready-made firmware image yet, so step 3 compiles it
from source. The shell in the photos is version 1; the current design (v6) is not printed
yet. Both are covered below.

## 1. Order the parts

| Part | Notes |
|---|---|
| Waveshare ESP32-S3-RGB-Matrix | The driver board. Ships with the panel power lead (VH4 plug to bare wires), a 4-pin SH1.0 cable, screws and a small speaker that p64 does not use. |
| Waveshare RGB-Matrix-P2-64x64-B (or the standard RGB-Matrix-P2-64x64) | The 128 x 128 mm panel. The "-B" GOB version has a protective layer over the LEDs; both fit the shell. |
| A 5 V USB-C power supply, 3 A or more | Waveshare's PSU-27W-USB-C-B or any good phone charger. A laptop's USB port also powers it, but not at full brightness on bright content: full white at brightness 255 draws close to the panel's 15 W. |
| A USB-C cable | To power the device and, the first time, to flash it from a computer. For the v1 shell it must be a right-angle ("up/down angled") plug; for v6 any cable, plus two small 90-degree USB-C adapters (19.3 x 12.7 x 8 mm aluminium body, hand-measured in `enclosure/input/measurements.md`). |
| Six M3 x 10 screws | Into the panel's brass inserts. The heads sit 17 mm deep, so a long PH1 or hex driver is needed. |
| microSD card (optional) | FAT32. For your own files and for the Makapix cache; the device also runs without one. |

## 2. Print the shell

Everything is in `enclosure/`; its [README](../enclosure/README.md) explains the design.
Two versions matter:

- **v6, the current design:** `enclosure/output/v6/p64_enclosure_print.stl` (or `.3mf`).
  Cables plug into two 90-degree USB-C adapters that stay on the board's ports, reached
  through one window in the back face, and the back has mounts for two rotary knobs. It
  needs a small notch cut by hand in the panel's plastic frame (marked in red in
  `enclosure/output/v6/render_frame_notch.png`). v6 has not been printed yet.
- **v1, the proven one:** `enclosure/output/v1/p64_enclosure_print.stl`. Printed by
  JLC3DP in black PLA for $18.83 including shipping, and verified to fit. The cable is a right-angle USB-C plug pushed up into the port from
  underneath, laid in a groove to a notch at the back. No knob mounts.

Print settings: the file already lies on its back face, no supports; 0.2 mm layers,
3 to 4 perimeters, 20 % infill, PLA or PETG, about 80 g; bed at least 140 x 140 mm;
enable elephant-foot compensation (0.1 to 0.2 mm) so the counterbores stay clean. For a
bureau printing in MJF or SLA use the `_service.stl` file with its larger clearance; for
FDM stick to `_print.stl`.

## 3. Build and flash the firmware

The firmware is under `firmware/`, built with ESP-IDF v5.5 (Espressif's SDK) in C++.

1. Install ESP-IDF v5.5.x for the ESP32-S3 target with Espressif's installer (EIM) or
   the [official instructions](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html).
2. Clone this repository.
3. On Windows, in PowerShell 7, from `firmware/`: edit the paths at the top of
   `tools/env.ps1` if ESP-IDF is not under `C:\esp\v5.5.4`, then

   ```
   .\tools\build.ps1
   .\tools\flash.ps1
   ```

   The scripts load the ESP-IDF environment themselves and find the board's serial port
   by its USB ID. On Linux or macOS the same project builds with the standard commands
   after sourcing ESP-IDF's `export.sh`, untested by the author so far:

   ```
   idf.py set-target esp32s3
   idf.py build
   idf.py -p /dev/ttyACM0 flash
   ```

4. Connect the computer to the board's **USB** port (the one nearer the middle of the
   board; the other, **POWER**, is power only). No button dance is needed; if flashing
   refuses, hold BOOT, tap RESET, release BOOT.

You can flash before or after assembly. Flashing later is fine too: the USB port stays
reachable in the shell.

## 4. Assemble

1. Plug the driver board onto the panel's **HUB75 IN** header (the panel has two headers,
   IN and OUT; Waveshare's product photos show which).
2. Connect the power lead: the VH4 plug into the panel's power socket, the bare wires into
   the board's 5 V and GND screw terminals, red to 5V.
3. v6 only: push a 90-degree adapter onto each of the board's USB-C ports, body towards
   the frame's edge, after cutting the frame notch.
4. Slide the panel into the shell from the front with the board at the **bottom**, so the
   USB-C ports point at the wedge. It seats on the ledge and the six bosses.
5. Fit the six M3 x 10 screws from the back.
6. v1: push the right-angle USB-C plug up into the port from underneath and lay the cable in
   the groove towards the back notch. v6: plug the cable into the adapter through the back
   window.

The firmware rotates the picture 90 degrees by default to match the shell. If you stand
the panel some other way, change the rotation in Settings, or set it to "auto" and the IMU
keeps the picture upright.

## 5. First run

1. Power on. A boot animation plays. Once the device is online it plays Makapix Club's
   Promoted channel, which needs no account; with a card that has files, those play first.
2. With no Wi-Fi network saved, after about a minute the device opens an open Wi-Fi network
   called **p64-setup**. Join it from a phone or laptop; a setup page opens by itself (or
   go to `http://192.168.4.1`). Pick your network from the list, enter the password,
   optionally give the device a name, and save. The device reboots and joins your network.
3. Open `http://p64.local` (or `http://p64-<name>.local`) in a browser. If your network
   does not resolve `.local` names, the device's IP address is in your router's client
   list.

From here everything is in the web UI:

- **Your own files:** upload from the Home page, or copy them onto the microSD card under
  `p64/animations/` and put the card in the slot (the device creates the folders on first
  use). GIF, PNG, APNG, WebP and BMP up to 256 x 256 pixels and 5 MB.
- **Makapix Club:** in Settings, the Makapix tab, press Pair. A six-character code shows
  on the panel; enter it on [makapix.club](https://makapix.club) within 15 minutes. The
  device then plays the channels you choose, and the site can send artworks to it.
- **Playsets:** group channels (your folders, Makapix channels, the clock and the other
  widgets) and choose what plays when.
- **Widgets:** the clock overlay, the clock faces, the weather (set your location) and
  the temperature, on the Widgets tab.
- **Streams:** from a computer with Python and Pillow,
  `python firmware/tools/stream_send.py p64.local image.gif` sends an image or animation,
  `... screen` mirrors your screen, `... test --ddp` sends a DDP test pattern. The panel
  returns to normal playback a few seconds after the stream stops.
- **Panel:** brightness, the night schedule, rotation, the Quality and Photo modes and the
  colour gains on the Display tab.

## 6. If something goes wrong

- **Dim or flickering at high brightness on laptop power:** the USB port cannot supply the
  panel at full white. Use the 27 W supply on the POWER port, or lower the brightness.
- **Cannot reach `p64.local`:** use the IP address from your router, or check the serial
  console (`.\tools\monitor.ps1`, or `idf.py monitor`) where the device prints it at boot.
- **Wrong network saved:** hold BOOT for 10 seconds at power-on. The panel counts down
  the last 3 seconds and the device resets to factory state, including Wi-Fi.
- **After a firmware update the device boots the old version:** an update that fails to
  boot rolls back on its own; the Update page shows both versions and lets you roll back
  by hand.
- Anything else: `firmware/docs/api.md` documents the HTTP API, and
  `http://p64.local/api/v1/status` reports what the device thinks is going on.
