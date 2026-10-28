# p64 web flasher

Browser-based flashing of the p64 firmware, served from GitHub Pages at
<https://fabkury.github.io/p64/web-flasher/>. The user-facing instructions are in
[docs/flash-firmware.md](../flash-firmware.md); this page is about how the flasher is
built and hosted.

## How it works

1. The page reads `firmware/index.json`, the list of releases (tag, title, pre-release
   flag, date, notes, files with offsets and sizes), and preselects the newest release
   that is not a pre-release.
2. **Connect** opens the browser's serial port picker (Web Serial: Chrome and Edge) and
   talks to the ESP32-S3's ROM through esptool-js, checking that the chip is an ESP32-S3.
3. **Flash** downloads the release's files from the same origin (`firmware/<tag>/`),
   checks their sizes against the index and the image magic of the bootloader and the
   firmware, writes them at the offsets of the release's `flash_args` with every flash
   parameter on "keep" (the bootloader header carries the octal 80 MHz 32 MB settings
   the firmware was built with), and resets the board. The "erase everything first"
   box adds a full chip erase before the write.

GitHub's release assets cannot be fetched cross-origin from a browser, so the site
carries its own copy of every release's flash files. [build_site.py](build_site.py)
assembles the site at deployment: this folder's `index.html` and `esptool-bundle.js`,
the logo (`docs/images/logo/v2/p64-logo-v2.png` as the favicon and its 2x copy in the
header, both written by `firmware/tools/gen_logo_assets.py`), and for every release listed by `gh release list`
the files its `flash_args` names, downloaded from the release's assets, plus the
`index.json` the page reads. Nothing is committed: the
[pages workflow](../../.github/workflows/pages.yml) rebuilds and deploys the whole
site (actions/deploy-pages) when the
[release workflow](../../.github/workflows/release.yml) calls it after publishing a
release, when a release is published, edited or deleted by hand, when this folder
changes on `main`, and on request. The repository's Pages source is "GitHub Actions"
(set on 2026-10-19).

## Flash configuration

| Setting | Value |
|---|---|
| Chip | ESP32-S3 (ESP32-S3-WROOM-2-N32R16V on the Waveshare ESP32-S3-RGB-Matrix) |
| Flash mode, frequency, size | kept from the bootloader image (octal, 80 MHz, 32 MB: `firmware/sdkconfig.defaults`) |
| Baud rate | 460800 |
| Files and offsets | from the release's `flash_args` (`tools/release_assets.py`): bootloader `0x0`, partition table `0x8000`, OTA data `0x19000`, firmware `0x60000` |

## Files

| File | Purpose |
|---|---|
| `index.html` | the page: UI and flashing logic, no build step |
| `esptool-bundle.js` | esptool-js, the build p3a uses (from the `enhance/write-flash-array-buffer` branch, [esptool-js PR 226](https://github.com/espressif/esptool-js/pull/226)); it carries the ESP32-S3 ROM stub and the USB-JTAG reset sequence |
| `build_site.py` | assembles the Pages site from the releases (CI) or from a local release folder (preview) |

## Preview locally

With a build in `firmware/build_public/` (or `build/`):

```powershell
cd firmware
python tools\release_assets.py --build-dir build_public
cd ..
python docs\web-flasher\build_site.py --out ..\site --no-github --local v0.2.0=firmware\build_public\release
python -m http.server 8765 --directory ..\site
```

then open <http://127.0.0.1:8765/web-flasher/> in Chrome. Web Serial works on
`localhost` without HTTPS. Without `--no-github` the script also downloads the
published releases (it needs the `gh` CLI logged in).

## Troubleshooting

- **No device in the picker:** charge-only cable, or the wrong port (POWER carries no
  data). The board is "USB JTAG/serial debug unit" (vendor 303A, product 1001).
- **"Failed to connect":** hold BOOT, tap RESET, release BOOT, then Connect again.
- **Browser not supported:** Chrome or Edge 89+; Firefox and Safari have no Web Serial.
- **The port vanished after flashing:** the board re-enumerates on reset; that is
  normal, the firmware is running.

For anything else, the [command-line path](../flash-firmware.md) gives esptool's own
messages.
