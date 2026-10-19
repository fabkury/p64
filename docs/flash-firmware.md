# Flash the firmware

[Home](../README.md) · [Build guide](build-your-own.md) · [Owner's guide](using-p64.md) · [Status](status.md)

Putting p64's firmware on the controller (Waveshare ESP32-S3-RGB-Matrix) over USB. This
is needed once, on a new board, and to recover a device; afterwards updates install over
Wi-Fi from the web UI's Update page. Nothing here needs ESP-IDF: the firmware comes
pre-built from [GitHub Releases](https://github.com/fabkury/p64/releases).

Two ways, same result:

- **In the browser:** [fabkury.github.io/p64/web-flasher](https://fabkury.github.io/p64/web-flasher/)
  in Chrome or Edge. Choose the release, click Connect, pick the board, click Flash.
  That page's source and its hosting are described in [docs/web-flasher](web-flasher/README.md).
- **From the command line:** below, with Espressif's `esptool`. Works in any browser's
  absence, on Windows, macOS and Linux.

Either way, plug a USB-C **data** cable into the controller's **USB** port (on an
assembled p64: the right-hand port from behind, the one with the groove). The POWER
port carries no data. The panel may keep its own supply connected. The board is seen
as a serial port (ESP32-S3 USB Serial/JTAG, vendor 303A, product 1001): `COMn` on
Windows, `/dev/ttyACM0` or similar on Linux, `/dev/cu.usbmodem…` on macOS.

## Get esptool

Either of:

- The standalone executable from
  [esptool's releases](https://github.com/espressif/esptool/releases) (`esptool-win64.zip`,
  `esptool-macos-arm64.zip`, `esptool-linux-amd64.zip`, and the others): unzip, and run
  `esptool` (Windows: `esptool.exe`) from that folder. No Python needed.
- With Python 3 installed: `pip install esptool`, then `esptool` (or `python -m esptool`).

The commands below are for esptool 5. Version 4 (an older `pip` install) spells them
`write_flash` and `erase_flash`; everything else is the same.

## Get the firmware

From the newest [release](https://github.com/fabkury/p64/releases/latest), download the
four flash files into one folder:

| File | Flash offset | What |
|---|---|---|
| `bootloader.bin` | `0x0` | second-stage bootloader |
| `partition-table.bin` | `0x8000` | partition table |
| `ota_data_initial.bin` | `0x19000` | OTA selector (makes the first app slot boot) |
| `p64-firmware.bin` | `0x60000` | the firmware (the same file the Update page installs) |

`flash_args` in the release lists exactly these offsets, and `SHA256SUMS` their
checksums (`sha256sum -c SHA256SUMS` on Linux and macOS).

## Flash

From that folder, with your port in place of `COM7`:

```sh
esptool --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x19000 ota_data_initial.bin 0x60000 p64-firmware.bin
```

It takes about a minute at 460800 baud. The flash mode, frequency and size are taken from
the bootloader image itself (octal flash, 80 MHz, 32 MB), so no `--flash-mode` options are
needed. When it ends the board resets and starts the firmware.

**Keeping settings.** The command above writes only the firmware; the settings partition
is untouched, so a device that was already set up comes back on its Wi-Fi network with its
playsets, pairing and PIN. To start from a blank chip instead (a device you cannot get
into, or one changing owner), erase first; this loses the Wi-Fi network, every setting,
the Makapix Club pairing and the PIN:

```sh
esptool --chip esp32s3 --port COM7 erase-flash
```

then flash as above. The setup network `p64-setup` appears on the next boot
([first run](build-your-own.md#e-first-run)).

**After a Wi-Fi update.** A device that installed an update over Wi-Fi runs from its
second app slot. The command above writes the first slot and the OTA selector that points
at it, so the USB flash is what boots next either way.

## If it does not start

- **No port, or "Failed to connect".** Most USB-C cables are charge-only; use one that
  carries data. Then hold **BOOT**, tap **RESET**, release BOOT, and run the command
  again: the board is now waiting for the download. Both buttons are on the controller,
  next to the USB ports; on a closed shell this needs the panel out, which is why p64b
  flashes before assembly.
- **A COM number keeps changing (Windows).** The board re-enumerates on reset; run
  `esptool --port COM7 chip-id`, or look in Device Manager, to find the current one.
- **Permission denied on `/dev/ttyACM0` (Linux).** Add yourself to the `dialout` group
  (`uucp` on some distributions) and log in again, or run with `sudo` once.
- **"A fatal error occurred: ... MD5 mismatch"** or a flash that never finishes: lower
  the baud rate to `115200`.

The first boot after a flash prints its log on the same port; `esptool`'s companion
`esp_idf_monitor` is not needed, any serial terminal at 115200 baud shows it.

## Building from source instead

Developers and anyone changing the firmware build it with ESP-IDF v5.5.4 and flash with
the repository's scripts: [firmware/README.md](../firmware/README.md). The published
binaries are the same public build, made by [CI](../.github/workflows/release.yml) from
the tagged commit; [firmware/docs/release-build.md](../firmware/docs/release-build.md)
describes what a release contains and how one is cut.
