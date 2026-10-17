# Build your own p64

[Home](../README.md) · [Owner's guide](using-p64.md) · [Project status](status.md)

This guide builds **p64a**, with no soldering. You need basic command-line skills, a
printer or print service, and hand tools. **p64b** uses the same firmware and adds
soldered encoder wiring;
its extra parts and steps are marked below.

[Parts](#1-parts-and-tools) → [Print](#2-print-the-shell) →
[Flash](#3-build-and-flash) → [Assemble](#4-assemble) → [First run](#5-first-run)

## 1. Parts and tools

This is the main shopping list. Prices are in US dollars, excluding shipping and tax,
as paid by the maintainer on the dates in the [build costs](costs.md) page, which has
the full per-build totals with parts, shipping and tax separated.

| Qty | Part | Notes |
|---|---|---|
| 1 | [ESP32-S3-RGB-Matrix](https://www.waveshare.com/esp32-s3-rgb-matrix.htm) | Controller, $24.99. Includes the panel power lead (VH4 plug to bare wires). |
| 1 | [RGB-Matrix-P2-64x64-B](https://www.waveshare.com/rgb-matrix-p2-64x64.htm?sku=33838) | GOB panel, $31.99. Standard non-GOB P2 64×64 also fits ($28.99 recorded). |
| 1 | [PSU-27W-USB-C-C](https://www.waveshare.com/psu-27w-usb-c-c.htm), or a 5 V USB-C supply rated at least 3 A | $6.99. Laptop power may be insufficient for bright content. |
| 2 | Right-angle USB-C male-to-female adapters | Compatible options below. |
| 1 | USB data cable for the computer; power cable if the supply needs one | A charge-only cable cannot flash firmware. |
| 6 | M3×10 screws, socket or pan head | **Buy separately.** Heads 6 mm or smaller; no countersunk heads. The maintainer's: a 50-pack of button-head hex socket screws, $5.99 on 2026-09-18. |
| 1 each | Printed shell and cradle insert | Files below. |
| Small amount | Cyanoacrylate or double-sided tape | Attaches the insert. |
| Optional | microSD card | Recommended for local files and persistent caches. |

Tools: a long PH1 or matching hex driver (the deepest screw heads sit 17 mm inside),
and a computer with Git and ESP-IDF.

What one unit cost the maintainer, from the invoices in [build costs](costs.md):

| Build | Parts | Shipping | Tax (New York) | Delivered |
|---|---:|---:|---:|---:|
| p64a | $86.25 | $31.84 | $0.97 | $119.06 |
| p64b | $115.06 | $37.66 | $4.04 | $156.76 |

Parts are the controller, panel and supply ($63.97), the JLC3DP shell and insert
($11.30 for v7a, $11.17 for v7b), the adapters, the screw pack and, for p64b, the
encoder parts and the resistor pack. Shipping is dominated by the $25.60 Waveshare freight to New York; a
single shell printed alone ships for more than the half-order share used here (the v1
shell cost $18.83 delivered). Glue, a microSD card and the USB cable are not counted.

### USB-C adapters

The maintainer bought [this 4-pack](https://www.amazon.com/dp/B0H3JNGX1D) ($6.99 on
2026-10-10) and confirms [this alternative](https://www.amazon.com/dp/B0B2NJ3P3L) also
works (2026-10-15; $4.99 on 2026-10-17, the better buy for a single p64).
Other adapters may fit. The design uses a 19.3×12.7×8.0 mm body and 7.7 mm plug
protrusion; both adapters must seat together with sockets facing the back window.
Compare the [measurements and photos](../enclosure/input/measurements.md#90-degree-usb-c-adapter-two-one-per-driver-board-port)
when substituting a different shape. Dry-fit before gluing.

### Additional parts for p64b

| Qty | Part | Purpose |
|---|---|---|
| 2 | Adafruit 5880 I2C QT encoder boards, $7.95 each | Bridge A0 on board B for address 0x37. |
| 2 | Adafruit 5528 knobs ($2.95 each; the blue 5529 is the same knob), or compatible 6 mm-bore knobs | The model uses 20 mm diameter knobs. |
| 1 each | Adafruit 4209 and 4397 cables, $0.95 each | Cross the GPIO pin order to STEMMA QT by function. |
| 1 | Adafruit 4401, 200 mm JST-SH cable, $1.25 | Connect the boards inside the shell. |
| 2 | 2.2 kΩ resistors (smallest pack found: 40 for $3.99) | Pull SDA and SCL up to 3.3 V. |
| As needed | Insulation (tape, or an optional heat-shrink pack, Adafruit 1649, $4.95), soldering equipment, breadboard and jumper pins | Test and permanent harness options are in the wiring guide. |
| 1 | 2 mm hex key | Knob set screws. |

Adafruit prices as paid on 2026-09-11 (the 4401 on 2026-09-27); the p64b row
of the cost table above includes them.

Follow the [two-encoder wiring guide](hardware/encoders-soldered.md). The GPIO and
STEMMA QT pin orders differ: do not connect them with a straight cable. The
[one-encoder experiment](hardware/encoders-solderless.md) is not the p64a build.

## 2. Print the shell

Choose [p64a/v7a](../enclosure/output/p64a/v7a/) or
[p64b/v7b](../enclosure/output/p64b/v7b/). Print `p64_enclosure_print.stl` (or `.3mf`)
and `p64_cradle_insert.stl`. The shell is already oriented; print the insert sole down.
Both shells are printed, assembled and in daily use (p64a since 2026-10-09, p64b since 2026-10-17).

Use PLA or PETG, 0.2 mm layers, 3–4 perimeters, 20% infill, no supports, and a bed at
least 140×140 mm. Allow about 82 g plus under 2 g for the insert. Enable 0.1–0.2 mm
elephant-foot compensation. For MJF/SLA use the larger-clearance
`p64_enclosure_service.stl`; for FDM use `_print.stl`.

**Check:** holes are clear, the panel seats without force, and the insert fits around
the adapters. See the [design reference](../enclosure/design-reference.md) for
tolerances, or the [legacy v1 notes](../enclosure/archive/pre-v7/build-v1.md) for an older shell.

## 3. Build and flash

The documented installation builds from source with **ESP-IDF v5.5.4**.
Windows/PowerShell is the documented hardware workflow; Linux builds run in CI.
macOS and Linux hardware flashing have not been verified by the maintainer.

1. Install Git, PowerShell 7 (Windows), and ESP-IDF v5.5.4 for ESP32-S3 using
   [Espressif's instructions](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html).
2. Clone and enter the firmware folder:

   ```powershell
   git clone https://github.com/fabkury/p64.git
   cd p64/firmware
   ```

3. Connect the computer to **USB**, the port nearer the board's middle. **POWER**
   cannot carry data. Flash before assembly for easier access to ports and buttons.
4. On Windows, adjust the three installation paths in [tools/env.ps1](../firmware/tools/env.ps1)
   (`IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH`) to your install, then run:

   ```powershell
   .\tools\build.ps1
   .\tools\flash.ps1
   .\tools\monitor.ps1
   ```

   The scripts load ESP-IDF and detect the port. Exit the monitor with Ctrl+].
   If detection fails, use `.\tools\flash.ps1 -Port COM7`, replacing `COM7` with your
   port. To enter download mode manually, hold BOOT, tap RESET, then release BOOT.

   On Linux/macOS, source ESP-IDF v5.5.4's `export.sh`, then from `firmware/`:

   ```sh
   idf.py set-target esp32s3
   idf.py build
   idf.py -p /dev/ttyACM0 flash monitor
   ```

   Replace `/dev/ttyACM0` with your port.

**Check:** flashing completes and the monitor shows p64 starting. Disconnect power
before assembly. For later flashes after OTA, see the [firmware guide](../firmware/README.md).

## 4. Assemble

Leave the panel's plastic frame unmodified. In the assembled v7a, both USB-C
adapters seat fully without cutting the frame. Check the fit of your adapters
before attaching the insert.

1. With power disconnected, plug the controller onto **HUB75 IN**. Connect the power
   lead: VH4 to the panel, red to **5V**, ground to **GND** at the screw terminals.
2. Push both adapters fully onto the ports, bodies toward the frame edge. Dry-fit the
   panel, adapters and insert. The insert sits between its rails, flat against the
   inside back wall, around the adapter bodies.

   <img src="../enclosure/output/p64a/v7a/render_insert.png" alt="CAD view of the separate U-shaped USB-C cradle insert and its flat sole" width="420">

   The separately printed cradle insert (CAD view).

3. Attach the insert with a thin bead of cyanoacrylate along its sole's outer edge,
   away from the opening, or double-sided tape. Seat the panel with the adapters
   positioning the insert, fit two screws and let the adhesive set.
4. **p64b:** complete the [bench test and permanent harness](hardware/encoders-soldered.md).
   Fit boards on their pegs, shafts through the back, then washers, nuts and knobs.
   Leave room for push-switch travel. Connect the harness before final panel seating.
5. Seat the panel with the controller at the **bottom**, ports toward the wedge.
   Fit all six M3×10 screws from the back.
6. Connect the supply to **POWER**, on the left viewed from behind. **USB**, on the
   right, is marked by a groove and remains accessible for programming.

![Rear port orientation: POWER left, USB right, with a groove beside USB](images/illustrations/rear-ports.svg)

<img src="images/photos/p64-back.jpg" alt="Assembled v7a viewed from behind, with a cable in POWER and the USB port accessible" width="600">

The assembled p64a: supply connected to POWER, USB free for programming.

**Check:** the panel seats without force, the adapters stay in place when a cable is
connected, and the device stands securely. Default image rotation is 90° for the shell.

## 5. First run

1. Optional: with power off, insert a FAT32 card with a test artwork in `p64/animations/`.
2. Power on. With no working network saved, **p64-setup** appears after about a minute.
   Join it; open [192.168.4.1](http://192.168.4.1) if the setup page does not appear.
3. Select a **2.4 GHz** network, enter its password, optionally name the device, and
   save. Rejoin your normal network after the device reboots.
4. Open [p64.local](http://p64.local), or `http://p64-<name>.local` for a named device.
   If `.local` fails, use the IP from your router's client list.
5. Set your time zone in **Settings → Network**. Choose **Local** for card files or
   **Promoted** for Makapix without an account. New downloads wait for Wi-Fi and NTP.

**Check:** artwork appears and Home's next/pause controls respond. Continue with the
[owner's guide](using-p64.md) and [troubleshooting](using-p64.md#troubleshooting).
