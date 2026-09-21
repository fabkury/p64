# p64 product specification

Version 1.0, 2026-09-19. Source of truth for what the p64 device does. Words are used as
defined in `CONTEXT.md`; hard-to-reverse choices and their reasons are in `docs/adr/`.
This document says what the device does and what its limits are; how the firmware is
built to do it is the architecture's job and is not written here.

How to change this document: edit it in the same commit as the change it describes; add
an ADR when the change reverses one; keep the settings and limits tables complete.

Sources referred to as "p3a" are the user's ESP32-P4 player firmware (v1.2.3, cloned at
`firmware/reference/p3a`); "the server" is Makapix Club (cloned at
`firmware/reference/makapix`); "the hardware tests" are `hardware-tests/`.

## 1. Product

p64 is a self-contained desktop pixel-art player: a 64x64 RGB LED panel in a printed
shell, powered over USB-C, controlled from a browser on the same Wi-Fi network and from
Makapix Club. It plays animated and static pixel art from the owner's microSD card and
from Makapix Club channels, shows a clock, the weather and the room temperature as
widgets, and accepts pixel streams from the network.

Posture: a public, open-source, do-it-yourself product (Apache-2.0), the same as p3a:
`main` is production, devices update over the air from GitHub Releases, and every feature
must work for a stranger who assembled the device from the parts list. English only.

Principles, in priority order:

1. Playback is accurate, tearless and jitter-free. Nothing the device does in the
   background may disturb the panel.
2. Every change on the panel is seamless.
3. The device keeps working alone: offline, without a card, without Makapix.
4. The web UI feels like p3a's: same navigation, same look, same vocabulary.
5. Nothing is silently dropped or skipped: rejected files, failed downloads and late
   frames are counted and visible in the web UI.

## 2. Hardware

| Part | Facts | Used by v1 |
|---|---|---|
| Driver board | Waveshare ESP32-S3-RGB-Matrix: ESP32-S3-WROOM-2-N32R16V, 32 MB octal flash, 16 MB octal PSRAM, HUB75 header, two USB-C (POWER, USB), BOOT button, IMU (6-axis), RTC, temperature/humidity sensor, audio codec with two microphones, speaker output, TF slot. Native USB Serial/JTAG on the USB port. | Panel, BOOT, IMU, RTC, temperature/humidity sensor, TF slot, USB console |
| Panel | Waveshare RGB-Matrix-P2-64x64: 4096 RGB LEDs, 2 mm pitch, 1/32 scan, HUB75E, 5 V, 15 W max, 128x128 mm. | Yes |
| Power | Waveshare PSU-27W-USB-C-B on the POWER port; the USB port takes a computer for console and flashing. Full white at brightness 255 draws close to the panel's 15 W; a USB-only rig cannot supply that (see brightness ceiling). | Yes |
| Shell | `enclosure/` v4: panel turned 90 degrees clockwise, two rotary encoders on the back planned, panel-mount USB-C sockets. | Rotation default 90; encoders reserved |
| Not used in v1 | Audio codec, speaker, microphones, Bluetooth. | Extension points only |

Firmware platform: ESP-IDF v5.5, C++, the vendored and patched `esp-hub75` driver from
the hardware tests (pin map, GDMA priority, refresh and tonal-depth findings recorded in
`hardware-tests/README.md`). Single supported target; the panel's width and height are
one configuration property read everywhere, never literal numbers.

## 3. Panel and picture

### 3.1 Panel modes

| Mode | Bit planes | Refresh (measured at 20 MHz pixel clock, 2026-09-20) | Codes per channel | Light at full white | Use |
|---|---|---|---|---|---|
| Quality (default) | 10 | 271.3 Hz (transition bit 4, 36 transmissions per row) | 1024 | 100 % (1979 of 2304 pixel clocks per frame lit) | Normal viewing: dark tones keep their steps |
| Photo | 8 | 813.8 Hz (transition bit 4, 12 transmissions per row) | 256 | about 74 % of Quality (491 of 768 clocks lit, at three times the rate) | Being photographed or filmed: no rolling-shutter banding |

Photo mode is dimmer by design: the five planes sent once carry halved output-enable
windows whatever the plane count, and with only twelve transmissions per row they are a
larger share of the frame. Cameras compensate with exposure; the setting is not meant
for viewing. Quality mode stays at 271 Hz: the next finer setting (transition bit 3,
146 Hz) would need two 25.7 KB descriptor chains in internal RAM instead of two 13.8 KB
ones, which the device does not have (settled with the user on 2026-09-20).

Switching modes is a user action in the web UI and the API. The driver's refresh
profile changes in place (no re-initialisation, nothing allocated): the panel shows the
previous picture for a few refresh periods and never blanks; it must not require a
reboot. The mode persists across reboots.

### 3.2 Brightness

- User brightness: integer 1 to 255, default 255, persisted. Never shown or accepted as a
  percentage. 0 is not a brightness; darkness is "panel off" or "pause".
- Brightness ceiling: advanced setting, 1 to 255, default 255. The effective brightness is
  min(requested, ceiling) at all times, including schedules, Makapix commands and the API.
- Night schedule: one daily window (start HH:MM, end HH:MM, may cross midnight), disabled
  by default, with a target brightness 1 to 255 or "panel off". Inside the window the
  effective brightness is the target (capped by the ceiling); outside it is the user
  brightness. Changing the user brightness inside the window applies after the window.
- The driver's curve is floored at about 17/255 of output-enable time on this panel and any
  value below 255 costs the low bit planes first (hardware tests); the spec accepts that
  and does not add software dimming in v1.

### 3.3 Colour

- Tone curve: gamma 2.2 (sRGB monitors are what the artwork is made on), fixed.
- White balance: advanced per-channel gains R, G, B, each 50 to 100 %, default 100. Applied
  after gamma, so 255 white at gains 100/100/100 is the panel's native white.
- Transparency and partial alpha are blended over the background colour in gamma space,
  the way a browser composites, so the panel matches the artist's preview on makapix.club.
- Background colour: RGB, default 0,0,0, used for transparent pixels and for letterbox and
  pillarbox bars. Changing it re-composites the current artwork.

### 3.4 Rotation

Setting: 0, 90, 180, 270 (clockwise) or auto; default 90 (the printed shell stands the
panel that way). Applied in the display layer: every state renders in logical orientation
and never knows the physical one. Auto reads gravity from the IMU and picks the multiple of
90 degrees that puts "up" up, with hysteresis so a tilted desk does not flip it; the last
resolved value is used until the IMU reports a new stable orientation.

### 3.5 Scaling

Canvases are fitted into the panel preserving aspect ratio:

- Smaller than the panel on both axes: nearest-neighbour upscaling by the largest integer
  factor that fits, then centred. (A 32x32 shows at 2x, a 16x16 at 4x; a 48x48 stays 1x,
  never blurred.)
- Larger than the panel on either axis: box-average downscaling to fit, non-integer ratios
  allowed (a 128x128 becomes 64x64 by averaging 2x2 blocks; a 100x100 by averaging
  fractional boxes).
- Exactly the panel size: unchanged.
- Non-square canvases keep their aspect ratio; the unused rows or columns are bars in the
  background colour, centred.

Streams follow the same rules (section 8).

### 3.6 Seamless changes

Definition, also the acceptance test: the previous frame stays on the panel until the next
content's first frame is fully rendered in memory; the change then lands on one panel
refresh boundary; no blank frame, no partial frame, no flicker, no brightness step. This
holds for artwork to artwork, artwork to widget, widget to artwork, stream entry and exit,
and overlay changes. There are no transition effects in v1 (cross-fade and wipes are
possible later because a transition is content that owns two frames).

Exceptions, allowed and documented: panel mode switch (a few refresh periods of the
previous picture), reboot, pause (deliberate darkness).

### 3.7 Live preview

The device can hand out its current logical frame (64x64 RGB888, or a PNG of it) on
request, and pushes it to the web UI at 2 to 4 frames per second while the Home page is
open. Every state is previewable, including widgets, streams and status screens.

## 4. Artworks and playback

### 4.1 Formats

| Format | Static | Animated | Transparency |
|---|---|---|---|
| GIF | yes | yes | 1-bit, per frame, all four disposal methods |
| PNG / APNG | yes | yes (APNG) | 8-bit alpha, APNG blend and dispose ops |
| WebP | yes | yes | 8-bit alpha, animation blend and dispose |
| BMP | yes | no | alpha on V4/V5 headers with an alpha mask; 1, 4, 8, 16, 24, 32 bit; RLE |

Formats are detected from content (magic bytes), never from the file extension alone.
JPEG is not supported (nothing on 64x64 wants a photo codec); a JPEG is rejected with a
clear reason.

### 4.2 Limits

| Limit | Value | Behaviour beyond it |
|---|---|---|
| Canvas | up to 256x256 (any aspect) | rejected |
| File size | up to 5 MB (5 242 880 bytes) | rejected |
| Frame count, duration, fps | unbounded | played |
| Guaranteed smooth envelope | 64x64 at 60 fps, any supported format | see 4.4 |
| Best-effort envelope | up to 128x128 at 60 fps | slows down when decode is late (4.4) |

These are the Makapix upload limits, so every Makapix artwork plays. A rejection is
recorded with the artwork's identity and reason, shown in the web UI (status banner and
diagnostics), and the item is skipped without stopping playback.

### 4.3 Frame timing

- Presentation is locked to the panel refresh; a frame changes only on a refresh boundary.
- Presentation rate is capped at 60 frames per second: a frame is never shown for less
  than one 60 Hz period even when its delay is shorter.
- The browser rule applies to stored delays: GIF delays of 10 ms or less become 100 ms;
  APNG and WebP delays of 0 become 100 ms; every other delay is used as written. This
  matches what artists see in a browser and the server's own duration metadata.
- Static images have no delay; they stay until the swap.
- Loop counts in files are ignored: every animation loops forever.
- Timeline: frame targets carry the stored durations exactly on the device clock, so
  there is no cumulative drift while decoding keeps up (a 40-frame loop measures
  40 x delay per loop). A frame lands on the first panel refresh at or after its
  target, so one stay can differ from its delay by up to one refresh period (3.7 ms
  in Quality mode); only a frame that misses by more than a period re-anchors the
  timeline (no catch-up burst).

### 4.4 Decode budget and the no-drop rule

Frames of an animation are never skipped. When decoding a frame takes longer than the
previous frame's delay, the previous frame simply stays up until the new one is ready, so
playback slows down rather than dropping frames; afterwards the timeline re-anchors from
the late frame (no catch-up burst). Late frames are counted per artwork and in totals,
visible in diagnostics and, when an artwork's late-frame share exceeds a threshold, as a
small note in the web UI's artwork info.

Decoding is live (frame by frame from the file) for every artwork; the device does not
cache decoded frames. The first milestone of the firmware is a decode benchmark of every
format at 64x64, 128x128 and 256x256 on the device, recorded in `firmware/README.md`; the
guaranteed envelope above must hold on that benchmark's worst case.

### 4.5 Auto-swap and swaps

- Auto-swap interval: default 30 s; 5 to 86400 s; 0 means never. Each artwork gets the
  full interval; the timer restarts on every swap of any kind.
- Auto-swap is a hard cut at the interval, regardless of where the animation is in its
  loop. Static images swap at exactly the interval.
- Manual next and previous are immediate. Play-this is immediate once the artwork is
  available (a URL download first shows nothing new: the current artwork stays up until
  the download is complete and decodable).
- The next artwork is prepared while the current one plays, so a swap costs nothing
  visible. When nothing is ready at the interval (empty cache, download still running),
  the current artwork stays up and the swap happens the moment something is ready.
- Every swap is seamless (3.6).

### 4.6 History and navigation

- History holds the last 32 items shown (artworks and interludes) with a position.
  Previous walks back; next walks forward while there is forward history and picks fresh
  at the end. Items whose file has disappeared are skipped.
- History is in memory only; a reboot starts with an empty history and a fresh pick from
  the active playset (which is persisted).
- Play-this items enter history. Widget interludes enter history and replay as a widget
  for one interval when revisited.

### 4.7 Pause and panel off

- Pause: the panel goes dark, playback stops on the current artwork, the auto-swap timer
  stops. Resume restores the same artwork and restarts the timer. Pause is a user action
  (web UI, API, Makapix command) and survives nothing: a reboot resumes playing.
- Panel off (night schedule) does not stop playback: the show continues in the dark and
  the panel lights up at the end of the window on whatever is current.

## 5. Content model

### 5.1 Channel kinds

| Kind | Identity | Needs | v1 |
|---|---|---|---|
| Local | a folder path under `animations/` on the card; the root folder counts | card | yes |
| Makapix Promoted | none | network | yes (no pairing needed) |
| Makapix All | none | pairing | yes |
| Makapix Own | the owner's own posts | pairing | yes |
| Makapix Artist | an artist's sqid | pairing | yes |
| Makapix Hashtag | a hashtag without `#` | pairing | yes |
| Makapix Reactions | a user's sqid (posts they liked) | pairing | yes |
| URL list | a list of artwork URLs (file on the card or pasted in the UI) | network | reserved (v1.x) |
| Pinned list | a named list of pinned artworks | card | reserved (v1.x) |
| Giphy, Klipy, museum IIIF | as on p3a | keys | possible, not planned |

A local folder is a channel; subfolders are separate channels (one level below
`animations/` is enough for v1; deeper folders are listed but not offered as channels).

### 5.2 Playsets

Semantics are p3a's, unchanged:

- Up to 32 user playsets; names up to 32 characters, letters, digits and underscore.
- Up to 64 channels per playset, each with a display name (up to 64 characters), a weight
  (0 mutes; all zero means equal) and an offset (ordered sources only: local, URL list,
  pinned).
- Channel selection per pick: SWRR or stochastic; default stochastic; global setting.
- Pick mode within a channel: random or recency; default random; global setting.
- Playback picks only among artworks whose file is on the card (or, without a card, in
  the device's memory cache, 5.4). A channel with nothing available is skipped; a playset
  with nothing available shows the "no artwork" status screen until something lands.
- Built-in playsets: Promoted, All, Followed, Local. They are shown as pills, cannot be
  edited or deleted, and All and Followed are disabled until the device is paired.
- Followed is the server-generated `followed_artists` playset: fetched from the server at
  activation and on refresh, one Artist channel per followed artist, equal weights. The
  server's "play this playset" command is honoured (p3a never implemented it).
- The active playset is persisted and restored at boot; if it cannot be restored, the
  device falls back to Promoted (or Local when there is no network).

### 5.3 Channel index and refresh

- Each channel keeps an index of up to the per-channel cap: hard cap 4096 entries,
  default 2048, user setting per device ("channel cache size").
- Makapix channels refresh every 4 h by default (60 s to 24 h), walking the server's
  listing newest-first up to the cap, dropping entries that vanished, marking entries whose
  file changed on the server for re-download.
- Local channels refresh on demand (file manager actions, card mount) and at most every
  60 s when polled.
- Refresh failures back off exponentially (30 s doubling to 15 min) and never block
  playback; the last successful index stays in use. At most two refreshes run at once.

### 5.4 Artwork cache and downloads

- With a card: channel artworks are downloaded into the cache on the card, round-robin
  across the playset's channels, oldest-missing first, atomically (temporary file, sync,
  rename). Missing remote files are remembered so they are not retried every cycle.
- Without a card: the device keeps a memory cache of Makapix and URL artworks (a bounded
  number of files in PSRAM, evicting least recently played) so the show still runs; the
  web UI shows a persistent "no card" notice; local channels, pinned lists and history
  persistence are unavailable.
- Card space: downloads pause when free space is below a floor; an age-based eviction
  keeps free space above a watermark, never touching `animations/`, and deletes cached
  files least recently played first.
- One TLS download at a time, in addition to the persistent Makapix connection; the
  memory budget for both is fixed at design time (ADR 0009).

### 5.5 Play-this

- From the web UI: a local file (file manager), a Makapix post (pasted URL or sqid), or an
  arbitrary URL.
- From Makapix: "send to device" commands (show artwork, play channel, play playset).
- URL downloads are transient: stored in `downloads/` on the card (capped in size, oldest
  evicted; in memory without a card), so they replay from history but do not enter any
  local channel. Uploads from the file manager are local files in the folder the user
  chose (default `animations/`).
- A play-this artwork holds the panel for one auto-swap interval like any artwork, then
  the show continues from the active playset. A play-this with interval 0 stays until the
  user acts.

## 6. States

The device is always in exactly one main state: Animation show, Widget, or Stream. The
main state is chosen by the user (web UI, API) and persisted. Stream takeover (8.3) and
status screens (6.4) are overlays on the main state, not states. Any request for an
artwork (choosing a playset, next, previous, history, play-this, a Makapix command, a
tap on the shell) is also a choice: it switches the device to the Animation show and
persists that, so an artwork never plays inside the Widget or Stream state (settled
2026-09-21, after an artwork was found frozen on the panel with the state still Widget).

### 6.1 Animation show

Plays the active playset as in section 4, with:

- Clock overlay: optional (default on), HH:MM, 24-hour by default with a 12-hour option,
  in one of the bundled pixel fonts (default Capital Hill 6 px) at 1x, text colour
  configurable (default white) with a 1 px black outline so it reads over any artwork,
  in one of the four corners (default top-left) with a 1 px margin, no seconds. Not shown
  in the other states.
- Interludes: at every auto-swap, each widget that has an interlude probability above 0
  is rolled (independently, in a fixed order: Clock, Weather, Temperature); the first that
  wins takes the slot: the widget is shown for one auto-swap interval, seamlessly, and
  enters history. Manual next and previous never trigger an interlude; next during an
  interlude ends it. Default probability 0 for every widget.

### 6.2 Widget state

One chosen widget stays on the panel indefinitely and updates itself (clock every second,
weather on its refresh, temperature every minute). No overlay, no interludes.

### 6.3 Stream state

The device shows the "stream: waiting" status screen (hostname, IP, ports) until frames
arrive, then shows frames as they come (section 8). When the stream ends (silence
timeout) it returns to the waiting screen. Leaving the state is a user action.

### 6.4 Status screens

Text on the panel, in a bundled pixel font, only when the user must act or wait:

| Screen | When | Content |
|---|---|---|
| Boot | power-on until the first artwork | boot animation (default 2 s; 0 to 5 s; 0 = off) |
| Setup | setup mode | pages cycling every 3 s: "Wi-Fi setup", the AP name `p64-setup`, `192.168.4.1` |
| Connected | for 15 s after joining a network | hostname and IP address |
| Pairing | while a pairing code is valid | the 6-character code, then "paired" for 10 s |
| No artwork | active playset has nothing available | "no artwork" plus the reason (no card, offline, empty) |
| Stream waiting | Stream state, no frames | hostname, IP, ports |
| Update | during a firmware update | progress bar and version |

Routine work (downloads, refreshes, decoding) never shows text; the current artwork stays
up. Status screens are drawn seamlessly like any content.

## 7. Widgets

Common to all widgets: fonts are bitmap glyphs rasterised at build time from the bundled
pixel fonts (Capital Hill 6 px and Everyday Typical 7 px now, two more to be added, all
CC-BY 4.0 with attribution shown in the web UI's About section); text and background
colours are per-widget settings; every widget renders its first frame within one panel
refresh of being asked so it can be shown seamlessly.

### 7.1 Clock

- Digital face: time in the chosen font at 2x (12 to 14 px tall), 12/24 h, optional
  seconds, optional blinking colon; below it the date and weekday at 1x, with a day-month
  or month-day order setting (default day-month-year).
- Analogue face (settled 2026-09-20): twelve tick marks at the rim, the cardinal four
  longer; the numerals 12, 3, 6 and 9 in the 6 px font inside them; crisp pixel hands
  with no anti-aliasing (hour hand 2 px wide and short, moving continuously; minute
  hand 1 px and long, stepping per minute); a second hand in the accent colour when the
  seconds setting is on; a hub; the date in dimmed small text under the centre.
- Face choice, font, scale and colours are settings.

### 7.2 Weather

- Source: Open-Meteo (free, no key): current conditions and daily forecast. Location as
  latitude and longitude, entered directly or found with a city search in the web UI
  (Open-Meteo geocoding). Units metric or imperial. Refresh every 30 min (10 to 180).
- Layout on 64x64: current condition icon (24x24) and current temperature; today's high
  and low; a strip of the next three days (weekday letter, 12x12 icon, high/low).
- Icons: one pixel-art raster per WMO weather-code group, in 24x24 and 12x12, day and
  night variants, drawn ahead of time as PNGs in `firmware/assets/weather/` and converted
  at build time. Groups: clear, partly cloudy, overcast, fog, drizzle, rain, freezing rain,
  snow, rain showers, snow showers, thunderstorm, thunderstorm with hail.
- Offline: the last fetched forecast is kept and shown with its age; after 6 h without a
  refresh the widget shows "no data" rather than stale numbers.

### 7.3 Temperature

- Source: the board's temperature/humidity sensor. Calibration offsets for both values
  (the sensor sits in a shell with a warm panel).
- Layout: temperature at 2x with unit, humidity at 1x below, optional trend arrow from the
  last hour. Units follow the weather widget's setting.
- Both values are also in the status API at all times, whether or not the widget is used.

## 8. Streams

### 8.1 Protocols

| Protocol | Port | Pixel formats | Frame size |
|---|---|---|---|
| DDP (Distributed Display Protocol) | UDP 4048 | RGB888 | 64x64 (12 288 bytes, split over datagrams by DDP offsets) or 128x128 (box-downscaled 2:1); other sizes rejected |
| Raw p64 UDP | UDP 4064 | RGB888, RGB565 (little-endian), 8-bit indexed with a 256-entry RGB888 palette in the frame | any width and height up to 128x128, scaled by the artwork rules (3.5) |

Raw p64 UDP frame: a fixed header (magic `P64F`, version, pixel format, width, height,
frame sequence number, flags with a "palette present" and a "last chunk" bit, byte offset
of this chunk, total frame length) followed by the palette when present and by pixel data;
frames may be split into chunks of at most 1400 bytes of payload and are assembled by
offset; a frame is complete when its last chunk (or all bytes) has arrived. The exact
field layout is fixed by the API reference.

Both protocols: latest complete frame wins (an incomplete frame is discarded when a newer
one completes), presented on the next panel refresh; sequence numbers detect loss and
reordering for diagnostics only. No WebSocket stream in v1 (ADR 0007).

### 8.2 Latency

Target: a complete frame is on the panel within one panel refresh plus one Wi-Fi receive
of arriving, with no buffering beyond one frame. Measured latency and loss counters are in
diagnostics.

### 8.3 Takeover and silence

- Stream takeover, default on: while in Animation show or Widget state, the first complete
  stream frame takes the panel seamlessly; the previous state keeps running invisibly
  (timers continue) and returns, seamlessly, after the silence timeout with no frames
  (default 5 s, 500 ms to 60 s). With takeover off, streams are ignored outside the
  Stream state.
- A stream that starts during a status screen that requires the user (setup, pairing,
  update) waits until that screen is gone.
- Streams are LAN-trust only: they are not covered by the PIN.

## 9. Inputs

- BOOT button: held for 10 s at power-on performs a factory reset (the panel counts down
  the last 3 s). No other function in v1.
- IMU: a single firm tap on the shell is next; a double tap is previous; a sensitivity
  setting (1 to 10) and a 1 s lockout; tap gestures can be disabled. Auto-rotation as in
  3.4.
- Rotary encoders: reserved. The firmware exposes an input abstraction so the two
  encoders planned for the shell plug in without touching the states; their roles are
  decided when the shell exists.
- Everything else is the web UI, the API and Makapix commands.

## 10. Network

### 10.1 Wi-Fi

- One saved network (SSID, password), DHCP only, 2.4 GHz.
- Boot: try the saved network. After 60 s without a connection, start setup mode while
  continuing to try the saved network every 30 s; the moment it connects, setup mode
  ends. Setup mode is never on while connected.
- Loss of connection while running: reconnect with backoff; the same 60 s rule then opens
  setup mode underneath. Playback continues throughout from the cache.
- Setup mode: open access point `p64-setup` on 192.168.4.1 with a captive portal (DNS
  answers everything with the device's address, the usual OS probe URLs are handled); the
  setup portal offers a scan list of visible networks, typed SSID and password, and the
  device name; saving reboots into the new network and the portal's success page counts
  down to `http://<hostname>.local/`.
- Hostname `p64` or `p64-<device name>`, announced over mDNS with an HTTP service; also set
  as the DHCP hostname.

### 10.2 Time

NTP (default `pool.ntp.org`, configurable) corrects the on-board RTC, which keeps time
across reboots and power cuts without network. Time zone: chosen from the IANA list in the
web UI and mapped by an embedded table to the POSIX rule, so daylight saving is automatic;
default UTC. A "set time from this browser" action exists for installs with no internet.
Until the time is known, clock features show `--:--` and Makapix TLS waits (certificates
need a clock).

### 10.3 PIN

Optional 4 to 8 digit PIN, off by default, set, changed or cleared in Settings, cleared
by factory reset. When on: every page and API route requires it, entered once per browser
(session cookie) or sent by scripts in a header; after 5 failures the device delays
answers for 30 s. The setup portal in setup mode and the UDP streams are not covered.

## 11. Web UI

Served by the device on port 80 (`http://<hostname>.local/`), embedded in the firmware
image, mobile-first, installable as a PWA, with p3a's stylesheet and its five themes
(Spectrum default, Gallery, Pixel Console, Dither Pop, Blossom) under a p64 wordmark.

### 11.1 Pages

Bottom navigation: Home, Playsets, Settings, Update (badge when an update is available).

- Home: live preview of the panel (3.7); now-playing card (playset name, per-channel
  counts and refresh state, shuffle toggle); transport row: previous, like/unlike (Makapix,
  paired), pause/resume, artwork info, next; playset pills (user playsets, then built-ins
  Promoted, All, Followed, Local); "Play from..." (upload a file to a chosen folder, or
  a URL or Makapix link); status banners (no card, offline, pairing needed, rejected
  artwork, update available).
- Playsets: list of user playsets (play, edit, delete, create up to 32) and the editor:
  name, channel balance bar, channels list with reorder, per-channel display name, weight,
  offset, and an "add channel" dialog offering Local (folder picker) and Makapix
  (Promoted, All, Own, Artist with a check, Hashtag with a check, Reactions with a check).
- Settings tabs:
  - Display: brightness (1 to 255), brightness ceiling, night schedule, panel mode,
    rotation (including auto), background colour, RGB gains, clock overlay (on/off, font,
    corner, 12/24 h, colour), boot animation length.
  - Widgets: main state selector (Animation show, Widget: which, Stream), per-widget
    settings (clock face and font, weather location and units, temperature offsets) and
    each widget's interlude probability.
  - Stream: takeover on/off, silence timeout, DDP and raw UDP on/off with their ports.
  - Network: connection status (SSID, IP, gateway, signal), device name, time zone, NTP
    server, set time from browser, PIN, erase Wi-Fi and restart in setup mode.
  - Storage: card status and space, root folder, the file manager (browse folders, upload,
    create folder, rename, delete, play now), explicit format with confirmation.
  - Makapix: pairing status and code, player key, certificate expiry, pair, unpair, refresh
    interval, channel cache size.
  - System: firmware version and build, uptime, reboot, factory reset, diagnostics (panel
    health, refresh rate, late frames, memory, tasks, log, reboot counters, last crash),
    About (licences and font attributions).
- Update: installed and available versions, release notes, check, install, rollback.
- Setup portal (setup mode only): the streamlined Wi-Fi page.

### 11.2 Behaviour

- Status changes (now playing, counters, banners) are pushed to open pages over a
  WebSocket; polling every 4 s is the fallback.
- Every destructive action (delete, format, unpair, factory reset, erase Wi-Fi) asks for
  confirmation in the UI.
- The UI never assumes a source URL: the preview comes from the device.

## 12. HTTP API

- Own API, p3a-inspired: versioned under `/api/v1/`, JSON bodies, p3a's envelope
  (`{"ok":true,"data":...}` and `{"ok":false,"error":"...","code":"..."}`), actions as
  POST to `/api/v1/action/<name>` answering 202 when queued, settings read and merged with
  GET and PUT on `/api/v1/settings`, resource routes for playsets, channels, files,
  history, Makapix, network, update, diagnostics; one WebSocket for status push and the
  live preview; an API version number reported in status. Route-level detail lives in the
  API reference written with the architecture.
- No route is disabled in release builds; the optional PIN covers all of them.
- The API is the only way the web UI talks to the device; anything the UI can do, a
  script can do.

## 13. Makapix Club

The device is a Makapix "player". The server's contract is in its repository
(`docs/player/`, `docs/mqtt-api/`, `docs/http-api/player-rpc.md`, `api/openapi.json`);
p3a's client is the reference implementation. p64 uses:

- Identity: every HTTP request carries `User-Agent: p64/<firmware version>`; the device
  registers with `device_model` `p64`.
- Pairing: the device asks the server for a player key and a 6-character code (valid
  15 min), shows the code on the panel and in the web UI, polls for its credentials until
  the owner has entered the code on the site, then stores the certificate, key, CA and
  API token. Token-based certificate renewal within the server's renewal window; the
  "registration invalid" state after repeated authentication failures, with re-pairing
  from the web UI. Unpairing deletes the credentials.
- Listing: the player RPC (`query_posts`) over HTTPS with the token (the same contract
  the MQTT request topics carry; HTTPS spares the device the 128 KB reassembly of
  fragmented MQTT replies and keeps one transient TLS session at a time); Promoted
  without pairing through the public promoted feed. Pages of 50 over a kept-alive
  connection, within the server's per-device rate limits; the first pages of a channel
  that has no index yet play before the walk completes.
- Files: downloaded from the server's file store in the artwork's native format, over
  plain HTTP as the server offers players (`docs/player/displaying-artwork.md`): the
  files are public, every download is checked against its Content-Length and decoded
  before use, and a TLS session per file would cost internal RAM the device does not
  have. `P64_MAKAPIX_VAULT_TLS` switches to HTTPS.
- Push: MQTT over mutual TLS: commands (show artwork, play channel, play playset, next,
  previous, pause, brightness, rotation, background colour), presence (status every 30 s
  and a last-will "offline"), advertised capabilities (pause, brightness 1 to 255,
  rotation values) and state. Reconnect with jittered backoff.
- Engagement: views reported for every artwork shown (artwork views and channel
  impressions as the server distinguishes them, at most one per 5 s); Like and unlike from
  the web UI; artwork info (title, author, date) fetched by the browser from the site as
  p3a does.
- Memory: the persistent MQTT TLS session plus one download TLS session must fit the
  ESP32-S3's internal RAM alongside the panel buffers; the budget is set in the
  architecture (ADR 0009) and verified before pairing ships.

## 14. Storage

- microSD card, optional (5.4), FAT32 only (exFAT is out; a card without a FAT file system
  is reported, never formatted automatically). Formatting is an explicit, confirmed action
  in the web UI.
- Layout under a configurable root, default `/sdcard/p64/`:
  `animations/` (local files and folders, the user's), `downloads/` (transient URL plays,
  capped), `cache/` (channel artworks, sharded), `channels/` (indexes and playsets),
  `state/` (reserved for larger on-card state). Existing files are never moved when the
  root changes. The active playset's name lives in NVS (namespace `p64state`), not on
  the card, so a device without a card still remembers which built-in it plays
  (ADR 0006).
- Every write is atomic (temporary file, sync, rename); nothing is deleted because a read
  failed (a dying card misreads healthy files); card failures latch a "card failed" state
  that stops writes, keeps playing from memory, and is shown in the web UI.
- Settings live in NVS as one JSON document, written double-buffered; Wi-Fi credentials
  and Makapix credentials in their own NVS namespaces; nothing else in flash except the
  firmware and its embedded web UI.
- No card-detect line: a swapped card is picked up on the next mount attempt (automatic
  after a failure, or on request from the web UI).

## 15. Operations

### 15.1 Boot

Power-on to the first artwork in under 3 s with a card and a cached artwork. Order of
appearance: boot animation (default 2 s) while the card mounts and the active playset is
restored; first artwork; Wi-Fi joins in the background; the "connected" status screen
does not interrupt an artwork that is already up (it is shown only when nothing is
playing yet, otherwise the IP is in the web UI).

### 15.2 Updates

- Firmware from GitHub Releases: automatic check every 12 h (and on request), never an
  automatic install; the user installs from the Update page; SHA256 of the image verified
  against the release asset before the swap; two application slots with rollback to the
  previous version from the Update page; a failed boot rolls back automatically.
- The web UI is part of the firmware image and updates with it (ADR 0005).
- The firmware version, build date and API version are shown in the UI and status.

### 15.3 Reliability

- Task watchdog on every task; crashes write a core dump to flash and reboot; the reboot
  reason, crash count and last core dump summary are shown in diagnostics.
- Brownout detection on; the panel is blanked before the supply collapses where the
  hardware allows it.
- The panel's DMA channel keeps GDMA priority over the crypto engines (hardware tests:
  without it, TLS traffic freezes the panel). Any new DMA user (SD, SPI, I2S) is checked
  with the panel stress test.
- Reboot counters by cause (watchdog, panic, brownout, Wi-Fi recovery) persist in NVS
  and reset on a successful update.

### 15.4 Factory reset

From the web UI (confirmed) or BOOT held 10 s at power-on: erases settings, Wi-Fi
credentials, PIN and Makapix credentials, then reboots into setup mode. The card is not
touched.

### 15.5 Diagnostics

Always on, behind the PIN: panel health (DMA stall flag, refresh rate, mode, timing),
GDMA priority (read and set live), a panel stress test (network load while the panel is
watched), late-frame counters, stream loss and latency, memory (internal, PSRAM, largest
free block), tasks, an in-memory log ring buffer readable over HTTP, reboot counters and
the last crash summary, and a reboot action.

## 16. Settings reference

All persisted unless noted. Ranges are inclusive.

| Group | Setting | Type and range | Default |
|---|---|---|---|
| Display | brightness | 1 to 255 | 255 |
| Display | brightness ceiling | 1 to 255 | 255 |
| Display | night schedule | enabled; start, end HH:MM; brightness 1 to 255 or off | disabled |
| Display | panel mode | quality, photo | quality |
| Display | rotation | 0, 90, 180, 270, auto | 90 |
| Display | background colour | RGB 0 to 255 each | 0, 0, 0 |
| Display | RGB gains | 50 to 100 % each | 100, 100, 100 |
| Display | boot animation length | 0 to 5000 ms | 2000 |
| Show | main state | animation show, widget, stream | animation show |
| Show | auto-swap interval | 0, or 5 to 86400 s | 30 |
| Show | pick mode | random, recency | random |
| Show | channel selection | stochastic, swrr | stochastic |
| Show | clock overlay | enabled; font; corner; 12/24 h; colour | on; Capital Hill; top-left; 24 h; white |
| Widgets | chosen widget (Widget state) | clock, weather, temperature | clock |
| Widgets | interlude probability, per widget | 0 to 100 % | 0 |
| Clock | face; font; scale; seconds; blinking colon; 12/24 h; date order; colours | as listed | digital; Capital Hill; 2x; off; off; 24 h; day-month; white on black |
| Weather | latitude, longitude; units; refresh | decimal degrees; metric, imperial; 10 to 180 min | unset; metric; 30 |
| Temperature | offsets; trend arrow | -10 to +10 units each; on/off | 0; on |
| Stream | takeover; silence timeout; DDP on, port; raw UDP on, port | on/off; 500 to 60000 ms; on/off, port | on; 5000; on, 4048; on, 4064 |
| Inputs | tap gestures; sensitivity | on/off; 1 to 10 | on; 5 |
| Network | Wi-Fi SSID, password | strings (NVS, separate) | unset |
| Network | device name | up to 16 chars `[a-z0-9-]`, no edge hyphen | empty |
| Network | time zone; NTP server | IANA name; host | UTC; pool.ntp.org |
| Network | PIN | 4 to 8 digits, stored hashed | off |
| Storage | card root | path under `/sdcard` | `/p64` |
| Storage | downloads cap | 16 to 1024 MB | 64 |
| Makapix | refresh interval; channel cache size | 60 to 86400 s; 32 to 4096 | 14400; 2048 |
| Makapix | credentials | player key, certificate, private key, CA, token (NVS, separate) | unset |
| Updates | automatic check | on/off | on |

Runtime, not persisted: pause, current stream, history, live preview subscribers.

## 17. Limits reference

| Item | Limit |
|---|---|
| Panel | 64x64, one property |
| Artwork canvas | 256x256 |
| Artwork file | 5 MB |
| Stream frame | 128x128; raw UDP chunk payload 1400 bytes |
| Playsets | 32 user, plus built-ins |
| Channels per playset | 64 |
| Channel index | 4096 (default 2048) |
| History | 32 |
| Playset name | 32 characters |
| Channel display name | 64 characters |
| Device name | 16 characters |
| PIN | 4 to 8 digits |
| Downloads folder | capped by setting, default 64 MB |
| Fonts bundled | 2 now, 4 planned |

## 18. Acceptance criteria

Each is a test the firmware must pass before a release; measurements are logged by the
device and read over diagnostics.

1. Seamless: over a 12 h soak of the Promoted playset at a 5 s interval, no blank or
   partial frame is presented at any swap (verified by the frame counter and DMA health,
   and by camera at 240 fps for a sample).
2. Jitter: for every artwork in the test corpus at 64x64, no presented frame is later than
   one panel refresh period past its due time; late-frame counters stay at 0.
3. Best effort: at 128x128 60 fps, no frame is skipped; late frames are counted; playback
   slows rather than drops.
4. Timing: a GIF with 10 ms delays plays at 10 fps; an APNG with 16 ms delays plays at
   60 fps; a 40-frame loop measures 40 x delay per loop within 1 % over 10 minutes.
5. Scaling: 32x32 shows at exactly 2x with hard edges; 128x128 averages 2x2; a 64x32
   shows pillar/letterboxed in the background colour, centred.
6. Modes: switching to Photo mode measures at least 600 Hz; back to Quality at 271 Hz;
   both with under 1 s of blank. Verified 2026-09-20 from the driver's timing (813.8
   and 271.3 Hz) over 12 switches plus a five-request burst, frame lock kept, no
   allocation (`tests/device/panel_mode_smoke.py`); a camera measurement is still open.
7. Brightness: 255 with the 27 W supply draws under 15 W at full white; the ceiling holds
   under Makapix commands and schedules.
8. Boot: power-on to first artwork under 3 s with a cached artwork; setup mode reachable
   within 60 s on a device with wrong credentials; the AP disappears within 5 s of the
   saved network connecting.
9. Streams: a 60 fps DDP stream at 64x64 shows every frame with latency under 30 ms on a
   quiet network; silence returns the previous state exactly 5 s after the last frame.
10. Pairing: the code appears within 5 s of the request; credentials are stored within
    10 s of the owner entering it; "send to device" shows the artwork within 15 s.
11. Robustness: a 24 h soak with Makapix refreshes, downloads, the stress test and the
    web UI open shows no panel stall, no watchdog and no memory growth.
12. Recovery: pulling power mid-download leaves a mountable card and a playable playset;
    a corrupt cached file is rejected once, recorded, and never retried in a loop.

## 19. Non-goals and deferred items

Not in v1: transition effects; state scheduling; USB mass storage; WebSocket streams;
Art-Net/E1.31; Giphy, Klipy and museum sources; multi-device synchronisation;
per-artwork dwell from Makapix metadata; audio, microphones, Bluetooth; multiple saved
networks; static IP; HTTPS on the device; multi-panel geometry; exFAT.

Planned for v1.x: URL list channels; pinned lists; the two extra fonts; encoder roles;
a temperature line on the clock face; screen-saver style blanking when idle beyond the
night schedule.

## 20. Risks and open technical questions

1. Decode throughput on the ESP32-S3 for 128x128 WebP and APNG at 60 fps is unmeasured;
   the first milestone measures it. The no-drop rule bounds the damage to slow motion.
2. Internal RAM: the 10-plane panel buffers, the persistent mutual-TLS MQTT session and a
   concurrent download session must coexist; the hardware tests ran out with two TLS
   sessions and 8-bit buffers. Mitigations: TLS buffers in PSRAM (throughput cost
   accepted), one download at a time, dynamic TLS buffers.
3. IMU tap detection through a printed shell on a desk may be unreliable; sensitivity
   setting and lockout are the knobs; the feature can be disabled.
4. The photo-mode refresh figure (813.8 Hz) comes from the driver's timing, not from a
   camera; the acceptance threshold is 600 Hz. Photo mode gives about 74 % of Quality's
   light (3.1).
5. The server plans daily caps on its anonymous endpoints; the Promoted channel without
   pairing depends on them staying generous.
6. Box-average downscaling of pixel art (128x128 to 64x64) softens it by design; the spec
   accepts this per the user's requirement.

## 21. References

- p3a firmware: `firmware/reference/p3a/AGENTS.md`, `docs/infrastructure/*.md`,
  `docs/HOW-TO-USE.md`, `components/play_scheduler/include/play_scheduler_types.h`,
  `components/config_store/`, `components/makapix/`, `components/wifi_manager/`,
  `components/http_api/`, `webui/`.
- Makapix Club server: `firmware/reference/makapix/CONTEXT.md`, `docs/player/`,
  `docs/mqtt-api/README.md`, `docs/http-api/player-rpc.md`, `api/openapi.json`,
  `docs/protect-artworks/`, `docs/p3a/messages/0001` (User-Agent contract).
- Hardware tests: `hardware-tests/README.md` sections "Frame pacing", "Tonal depth and
  refresh", "Network throughput", "Hardware facts baked into sdkconfig.defaults";
  `hardware-tests/components/esp-hub75/P64-CHANGES.md`.
- Fonts: `firmware/assets/fonts/*/​*-Info.md` (VEXED, CC-BY 4.0).
- Protocols: DDP (3waylabs, "Distributed Display Protocol"), Open-Meteo API, WMO weather
  interpretation codes.
