# Using p64

[Home](../README.md) · [Build and first setup](build-your-own.md) · [Project status](status.md)

Open [p64.local](http://p64.local) from a browser on the same network. A named device
uses `http://p64-<name>.local`. If that fails, use its IP address from your router.

Jump to [artwork](#add-artwork), [playsets](#make-a-playset),
[widgets](#clocks-and-widgets), [controls](#controls), [streams](#send-a-live-stream),
[offline use](#offline-and-cardless-use), [updates](#updates-and-pin), or
[troubleshooting](#troubleshooting).

## Add artwork

**Your files:** insert a FAT32 microSD card and upload through Home's **Play from…**
control or **Settings → Storage**. You can also copy files to `p64/animations/` on the
card; subfolders become local channels. Select **Local** on Home to play them.

Supported formats are GIF, PNG/APNG, WebP and BMP, up to 256×256 pixels and 5 MB per
file. Small images scale up by whole numbers; large images scale down with averaging.
Animations that are too demanding play more slowly rather than drop frames. The web
UI reports rejected files and failed downloads.

**Makapix Club:** select **Promoted** on Home to play community artwork without an
account. For other channels, likes and sending artwork from the site, open
**Settings → Makapix → Pair** and enter the displayed six-character code on
[Makapix Club](https://makapix.club) within 15 minutes.

## Make a playset

A channel supplies artworks; a playset mixes channels. For example, mix a local folder
with Makapix's Promoted channel:

1. Open **Playsets**, create a playset, and give it a name.
2. Add the local folder and the Promoted channel.
3. Set their weights: equal weights give equal shares of channel picks; 2 and 1 give
   the first channel about twice as many picks.
4. Save, then play it. It also appears on Home.

A zero weight mutes a channel unless all weights are zero, which means equal shares.
There can be 32 user playsets with up to 64 channels each. Built-ins such as Local and
Promoted cannot be edited. Widgets appear as interludes, configured separately below.

## Clocks and widgets

In **Settings → Network**, set your time zone. After each boot, clocks wait for NTP;
the onboard RTC is not used and there is no manual time setting.

In **Settings → Widgets**, choose a full-time widget or stay in **Animation show** and
set interlude gaps. An interlude replaces one artwork slot. Its gap is a typical
(median) interval, not an exact schedule; 0 disables it, and auto-swap must be enabled.

| Widget | Setup |
|---|---|
| Clock | Choose a face and its available options. Random clock faces can be enabled for interludes. |
| Weather | Set latitude, longitude and units; data comes from Open-Meteo. |
| Air | Uses the weather location for air quality and UV; choose US or European AQI. Data is fetched while the widget is in use. |
| Temperature | Reads the board's temperature/humidity sensor; offsets allow calibration. |

The 13 clock faces are Digital, Analogue, Flip, Nixie, Horizon, Words, Hourglass,
Orrery, LED, Horizon-RD, Aquarium, Bracket and Station. LED has red, green, amber,
blue and VFD styles. The separate **clock overlay** is under **Settings → Display**;
it shows time over artwork.

## Controls

Home provides previous, next, pause/resume, artwork information and Makapix reactions.
Pause blanks the panel; resume restores playback. Previous walks through history;
next walks forward, then picks a fresh artwork at history's end.

| Input | Action |
|---|---|
| Firm single tap on the shell | Next artwork |
| Double tap | Previous artwork |
| p64b knob A: turn / press | Brightness / pause or resume |
| p64b knob B: turn / press | Next or previous / like the current Makapix artwork when paired |

Use **Settings → Inputs** to adjust taps, disable knob actions, swap knob roles or
reverse their direction. Long knob presses have no assigned action.

In **Settings → Display**, choose brightness, rotation, mirror and night scheduling.
The night schedule lowers brightness or blanks the panel while the device stays on.
Quality mode is for normal viewing; Photo mode raises refresh for cameras and is dimmer.
The brightness ceiling limits both manual controls and schedules.

## Send a live stream

From the repository root, with Python installed:

```powershell
python -m pip install pillow mss
python firmware/tools/stream_send.py p64.local test --ddp --seconds 10
python firmware/tools/stream_send.py p64.local image.gif
python firmware/tools/stream_send.py p64.local screen
```

Replace `image.gif` with your file. Pillow supports image playback; `mss` is needed
for screen capture. Stop a continuous stream with Ctrl+C. With the default takeover
setting, normal playback resumes five seconds after the stream stops. Settings →
Stream controls takeover, timeout, DDP (UDP 4048) and raw p64 (UDP 4064).

## Offline and cardless use

| Situation | What to expect |
|---|---|
| Card present, internet unavailable | Local files and previously cached channel artwork can play. New downloads and online services wait. |
| Cold boot without NTP | Artwork can play from the card, but clocks show `--:--`; night scheduling and online refreshes wait. |
| Connection lost after time synchronized | The clock keeps running until reboot. Online data stops refreshing. |
| No card | Widgets, streams and memory-cached online artwork work; local file storage and persistent artwork caches are unavailable. |

Settings, Wi-Fi credentials, pairing and the selected playset name survive reboot
without a card. Downloaded artwork in memory does not. A FAT32 card is recommended
for offline artwork playback. exFAT is unsupported; formatting is never automatic.

## Updates and PIN

The **Update** page checks GitHub Releases and shows available firmware. Installation
requires your action; the device verifies the image checksum. Follow the page's reboot
prompt. A failed boot can roll back automatically; the page also offers manual rollback
when another valid image is available. See [status](status.md) for release readiness.

Set an optional PIN in **Settings → Network**. It protects device controls and the
HTTP API, but not UDP streams or the setup portal. The device uses HTTP on your local
network. Rebooting ends browser sessions; five wrong PIN attempts cause a 30-second
lockout.

## Troubleshooting

| Symptom | What to check |
|---|---|
| Cannot open `p64.local` | Use the router's client list to find the IP; check that phone and device share a network. |
| Wrong Wi-Fi saved | Wait about a minute for `p64-setup`, then open [192.168.4.1](http://192.168.4.1) and save the correct network. |
| Clock stays at `--:--`; downloads wait | Check NTP status in Settings → Network. The network must allow an NTP server to answer (UDP 123). |
| No artwork | Check the selected playset, channel status, card, and size filters. Promoted requires internet and trusted time for new downloads. |
| Dark panel | Check pause, night schedule, brightness, and the selected main state. |
| Flicker or resets on laptop power | Use the specified supply on POWER; laptop power may be insufficient for bright content. |
| Card not recognized | Check FAT32 format and seating. Formatting through Storage erases the card; copy needed files elsewhere first. |
| Old firmware after an update | Check the Update page for rollback and error information. |

If locked out, hold **BOOT for 10 seconds at power-on** to factory-reset. This erases
settings, Wi-Fi, PIN and Makapix pairing; it leaves card files alone. To change only
Wi-Fi when the UI is reachable, use **Settings → Network** instead.

For other issues, collect the firmware version, symptom and relevant diagnostics from
**Settings → System**, then [open an issue](https://github.com/fabkury/p64/issues).
Remove credentials and personal network details from logs before sharing them.
