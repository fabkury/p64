# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

p64 is a desktop 64x64 RGB LED matrix: Waveshare's ESP32-S3-RGB-Matrix driver board
plugged onto their RGB-Matrix-P2-64x64 panel, in a 3D-printed tabletop shell. Two lines
of work live here, each with its own README that is the detailed reference:

- `firmware/` — ESP-IDF v5.5 firmware, C++20, directly on the `esphome/esp-hub75` DMA
  driver. No Arduino, no LVGL, no Waveshare BSP (only its pin map was reused).
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
  `sdkconfig` is generated and ignored. `dependencies.lock` is committed.
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
no signal. `Display` finds the LCD GDMA channel (`out.peri_sel == LCD`), clears its
end-of-frame flag before each flip, and `wait_for_back_buffer()` sleeps until just before
the predicted boundary, spins on the flag, then checks `eof_des_addr`/`dscr` to confirm
the DMA switched chains. The loop is therefore locked to the 76.3 Hz refresh: render into
RAM right after `present()`, then `wait_for_back_buffer()`, then `present()`. Keep that
order. Measured: 5.8 ms per `draw_pixels()` for 64x64, so the copy is the cost to watch;
a faster refresh (32 MHz clock or higher `HUB75_MIN_REFRESH_RATE`) is one sdkconfig line.

Driver API notes: the released `esp-hub75` (0.3.x from the component registry) lacks
things present on its git main (no `row_decoder`; `ICN2038S` is a distinct enumerator).
Brightness 0 blanks the panel; 1-255 go through a curve floored at ~17/255 on a 64-wide
panel.

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
