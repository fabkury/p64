# p64 HTTP API v1 (route reference)

The device's own API (spec section 12, ADR 0002): `http://<hostname>.local/api/v1/...`,
JSON bodies, p3a's envelope `{"ok":true,"data":...}` / `{"ok":false,"error":"...","code":"..."}`.
Actions answer at once; the work happens on the main task. No authentication yet (PIN
comes with M9). `tests/device/api_smoke.py` and `tests/device/content_smoke.py` exercise
everything below against a live device.

## Web UI

`/` (Home), `/playsets`, `/settings` (tabs by `#display`, `#widgets`, `#stream`,
`#network`, `#storage`, `#makapix`, `#system`), `/update`, `/static/common.css`,
`/static/theme.js`, `/static/app.js`, `/manifest.json`, `/static/icon-192.png`,
`/static/icon-512.png`, `/favicon.png`: embedded in the image, open (no PIN), served
with an `ETag` of the firmware version (`If-None-Match` answers 304).

## Status and settings

| Route | Method | What |
|---|---|---|
| `/api/v1/status` | GET | firmware, uptime, heap, network, time, card, panel health, `playback` (below) |
| `/api/v1/settings` | GET | the settings document (spec section 16 groups) |
| `/api/v1/settings` | PUT | merge the keys present, clamped to their ranges; returns the document |
| `/api/v1/ws` | WebSocket | `{"type":"status","data":...}` every 2 s and at once on events (Wi-Fi, a swap, a settings write, a channel list or count change, a playset saved or deleted, pairing, the card, the files) |
| `/api/v1/frame` | GET | the panel's current logical frame as PNG (live preview) |
| `/api/v1/frame.raw` | GET | the same as 64x64x3 RGB888 bytes |
| `/api/v1/action/set_time` | POST | `{"utc": <seconds since 1970>}`: sets the clock by hand (and the RTC); source becomes `manual` |
| `/api/v1/action/factory_reset` | POST | `{"confirm": "ERASE"}` required; answers, then erases settings, state, Wi-Fi, Makapix credentials and the PIN and reboots into setup mode (the card is untouched) |
| `/api/v1/diag/coredump/erase` | POST | erases the stored core dump |
| `/api/v1/diag/imu` | GET | live accelerometer reading (g), gravity angle and in-plane magnitude, calibration and resolution state, the tap threshold, the peak impulse of the last 2 s, tap counters, samples and read errors |
| `/api/v1/action/calibrate_upright` | POST | `{"rotation": 0|90|180|270}` (default: the display's current rotation): "the panel is upright now" becomes auto-rotation's reference |

`playback`: `state`, `paused`, `stream_up` (a stream holds the panel), `playset {name, builtin, channels, scanning, version}`, `artwork
{name, path, format, width, height, bytes, animated, frames_decoded, since_s, channel,
channel_index, source}` (absent on a status screen or pause), `no_artwork` (reason or
""), `last_error`, `history {count, position, can_back, can_forward}`, `auto_swap
{interval_s, remaining_s}`, `prepared`, `swaps`, `load_failures`, `frames`, `late`,
`skipped`. Two change counters let a page refetch only when something moved: `playback.playset.version`
grows whenever the channel list or its counts change (a scan installed, a Makapix index
refreshed, an artwork cached), `playsets_version` whenever the playset list, the
built-ins' availability or the card folders may differ (a playset saved or deleted,
pairing, the card, the files). The Home page refetches `/api/v1/channels` on the first
and at most every 2 s, `/api/v1/playsets` on the second; the Playsets page reloads on the
second.

## Show (M5)

| Route | Method | Body | What |
|---|---|---|---|
| `/api/v1/action/next` | POST | | forward in history, or a fresh pick at its end |
| `/api/v1/action/previous` | POST | | back in history (no-op at the start) |
| `/api/v1/action/history_go` | POST | `{"position":n}` | show that history item |
| `/api/v1/action/pause` | POST | | panel dark, timer stopped |
| `/api/v1/action/resume` | POST | | the same artwork again, timer restarted |
| `/api/v1/action/reset_timer` | POST | | restart the auto-swap interval |
| `/api/v1/action/refresh` | POST | | rescan the local channels |
| `/api/v1/action/play` | POST | `{"path":"animations/x.gif"}` | play-this from the card (422 when missing) |
| `/api/v1/action/play_playset` | POST | `{"name":"..."}` | activate a playset (built-in or stored; 404 otherwise) |
| `/api/v1/history` | GET | | `{count, position, items:[{index, kind, source, name, path, channel, channel_index, playset, shown_s_ago, current}]}` |
| `/api/v1/channels` | GET | | the active playset's channels: `{playset, scanning, version, last_scan_ms, channels:[{index, kind, identifier, display_name, weight, offset, entries, available, status, share, credit, cursor}]}` (Makapix channels add `cached`, `last_refresh`, `oversized`, `refreshing`, `error`) |
| `/api/v1/folders` | GET | | folders that can be local channels: `[{folder, name, files}]` |

## Playsets (M5)

| Route | Method | What |
|---|---|---|
| `/api/v1/playsets` | GET | `{active, playsets:[{name, channels}], builtins:[{name, enabled, reason}], max_playsets}` |
| `/api/v1/playsets/<name>` | GET | the playset (`{name, builtin, channels:[...]}`); built-in names work too |
| `/api/v1/playsets/<name>` | PUT | create or replace: `{"channels":[{kind, identifier, display_name, weight, offset}]}` |
| `/api/v1/playsets/<name>` | DELETE | remove a user playset |

Names: 1 to 32 of `[A-Za-z0-9_]`; `Promoted`, `All`, `Followed`, `Local` are reserved.
Channel kinds: `local` (identifier = folder under `animations/`, "" = the root),
`promoted`, `all`, `own`, `artist` (sqid), `hashtag` (tag without `#`), `reactions`
(sqid); `url_list` and `pinned` are reserved. p3a's shape (`type`/`name`, `sdcard`,
`user`, `named`) is accepted on input.

## Makapix Club (M6)

| Route | Method | Body | What |
|---|---|---|---|
| `/api/v1/makapix` | GET | | `{state: unpaired|pairing|paired|invalid, host, player_key, code, code_seconds_left, online, mqtt_connected, activity, last_error, cert_expires_at, refreshes, downloads, download_failures, views_sent, commands, cache {files, bytes, last_sweep, last_deleted, last_freed_bytes}}` (also under `makapix` in the status document; `cache` counts the card cache as the last sweep or dry run saw it) |
| `/api/v1/diag/cache_sweep` | POST | `{"older_than_s": N, "dry_run": true|false}` (defaults: the cache retention setting in seconds, true) | the cache sweep now (spec 5.4): deletes, or with `dry_run` only counts, every file under `cache/`, `downloads/` and `channels/` not played (or refreshed) for `older_than_s`, and clears the cached flag of the entries whose file went; answers `{dry_run, older_than_s, examined, bytes, deleted, freed_bytes, indexes_deleted, downloads_deleted, took_ms}`, 409 without a card, without a synced clock or while a sweep runs |
| `/api/v1/makapix/pair` | POST | | asks the server for a pairing code; `code` appears in the status within seconds and on the panel |
| `/api/v1/makapix/pair/cancel` | POST | | drops the code |
| `/api/v1/makapix/unpair` | POST | | erases the credentials, closes the MQTT session |
| `/api/v1/makapix/like` | POST | `{"post_id":n,"like":true|false}` | reacts as the owner (needs pairing; blocks up to 20 s) |
| `/api/v1/action/play` | POST | `{"post":"<sqid or makapix.club/p/<sqid>>"}` | play-this of a Makapix post (looked up and downloaded, then played) |
| `/api/v1/action/play` | POST | `{"url":"http(s)://..."}` | play-this of an arbitrary artwork URL (downloaded into `downloads/`) |

Channel objects of `/api/v1/channels` for Makapix kinds add `cached`, `last_refresh`
(epoch seconds), `oversized` (entries the last refresh listed but dropped for being over
the size limit; kept in RAM only, 0 after a reboot until the next refresh), `refreshing`
and `error`. A Makapix channel with nothing cached has the `status` "downloading" (its
index has entries), "offline", "no listing yet" (no refresh has landed), "no artworks"
(the refresh landed empty) or "nothing fits N px (M too large)" (everything listed was
over the size limit). History items and the status artwork carry
`post_id` and `sqid` for Makapix artworks.

`settings.makapix.max_size` (32, 64, 128 or 256; default 128; other numbers snap up to the
next step) is the maximum artwork size of the Makapix channels: the paired listings carry
it as `width`/`height` `lte` criteria, the promoted feed is filtered on the device, and
entries over it are never picked. Writing a different value refreshes every channel.
Play-this and the site's commands are not limited by it (only by the 256x256 canvas).

## Widgets (M7)

The status document carries `sensor {valid, temperature_c, humidity, trend_c_per_hour}`
and `weather {valid, error, age_s, temperature, units, humidity, code, condition, is_day,
today_max, today_min, days:[{weekday, condition, max, min}]}`; `playback.state` is
`animation_show`, `widget` or `stream` and `playback.widget` names the widget on the
panel. Every action that asks for an artwork (`play_playset`, `next`, `previous`,
`history/go`, `resume`, `play`, the Makapix commands, a tap) switches
`show.main_state` to `animation_show` and persists it (spec 6); a client that wants the
Widget or Stream state back sets it again afterwards. Settings groups `clock` (face, font, scale, seconds, blink_colon, h24,
date_order, colour, background), `weather` (latitude, longitude, units,
refresh_minutes) and `temperature` (offset_temperature, offset_humidity, trend) join
`show.clock_overlay` and `widgets` (widget, interlude_percent). History items of kind
`interlude` carry `widget`.

## Updates (M9, spec 15.2)

| Route | Method | What |
|---|---|---|
| `/api/v1/update` | GET | `{state, current_version, available_version, notes, available_size, download_url, sha256_published, bytes_read, image_size, progress_percent, error, last_check_age_s, can_rollback, rollback_version, rollback_partition, repository, asset}`; `state` is `idle`, `checking`, `up_to_date`, `available`, `downloading`, `verifying`, `ready_to_reboot` or `error` |
| `/api/v1/update/check` | POST | asks GitHub for the latest release now (409 `BUSY` while a job runs) |
| `/api/v1/update/install` | POST | empty body: installs the available release (its `.sha256` asset is verified); `{"url": "...", "sha256": "<64 hex>"}`: installs any image, plain HTTP allowed on the LAN, the checksum required |
| `/api/v1/update/rollback` | POST | makes the other slot bootable (409 `NO_ROLLBACK` when it holds no valid image) and reboots |

The status document carries `update_state`. The check runs 90 s after boot once online
and every 12 h while `updates.auto_check` is on. The image is written to the other OTA
slot while the panel keeps playing, its SHA256 read back from flash and compared, then
the slot is made bootable; the user reboots. The new image confirms itself 30 s after
boot; a crash before that rolls back automatically. Releases: tag `v<MAJOR.MINOR.PATCH>`
at the repository in `P64_OTA_GITHUB_REPO` with the assets `tools/release_assets.py`
produces (`p64-firmware.bin` and `p64-firmware.bin.sha256`).

## PIN (M9, spec 10.3)

| Route | Method | What |
|---|---|---|
| `/api/v1/auth` | GET | `{pin_set, authenticated, locked_for_s}` (open) |
| `/api/v1/auth/login` | POST | `{"pin": "..."}`: sets the `p64_session` cookie (open; 401 `WRONG_PIN`, 429 `LOCKED` with `Retry-After`) |
| `/api/v1/auth/logout` | POST | forgets this browser's session (open) |
| `/api/v1/auth/pin` | PUT | `{"pin": "1234", "current": "..."}` sets or changes the PIN (4 to 8 digits); `"pin": ""` clears it; with a PIN set the request must be authenticated and carry the current PIN; every session is dropped |

With a PIN set, every other route (including the WebSocket handshake and `/api/v1/frame`)
needs one of: the session cookie from a login, `Authorization: Bearer <session>`, or the
PIN itself in an `X-P64-Pin` header (for scripts; counted like a login attempt). Otherwise
401 `UNAUTHORIZED`. Five wrong PINs lock logins and header checks for 30 s (429 `LOCKED`,
`Retry-After`); sessions already open keep working. Open regardless: the UI shell page
(`/`, which shows the PIN prompt), the auth routes, the setup portal in setup mode, and the
UDP streams. Sessions live in RAM (up to 8; a reboot signs every browser out); the PIN is
stored as a salted SHA-256 in NVS and erased by the factory reset.

## Operations (M9)

The status document carries `time.source` (`none`, `rtc`, `ntp`, `manual`),
`panel.night_active` and `panel.brightness` (the effective brightness: the user's value,
or the night schedule's target inside its window, capped by the ceiling; 0 = panel off),
and `reliability {reset_reason, counters {power, software, panic, watchdog, brownout,
usb, deep_sleep, other}, crash {present, task, pc, cause, backtrace}, image {partition,
pending_verify, other_partition, other_version, other_date}}`. The counters persist in
NVS and reset when a new image is confirmed (30 s after boot). Settings group
`display.night` (enabled, start_minutes, end_minutes, brightness 0..255 with 0 = off).

Inputs (spec 9): the status document carries `inputs {imu_present, auto_rotation_resolved,
auto_rotation, calibrated}`. Settings `inputs.tap_enabled` and `inputs.tap_sensitivity`
(1 firm knock .. 10 light touch: threshold 2.5 g .. 0.25 g above gravity), and
`display.rotation_auto` (the IMU's resolved rotation drives the display once it has one;
`display.rotation` is the fallback and the value shown before the first resolution). A
single tap is "next", a double tap "previous", with a 1 s lockout. Auto-rotation needs one
calibration (above); the direction the picture turns with the gravity angle is the Kconfig
`P64_IMU_ROTATION_SIGN` (see PROGRESS: unverified until the panel is turned by hand).

## Streams (M8)

No HTTP routes: pixels arrive over UDP (spec 8, ADR 0007). The status document carries
`stream {active, protocol, width, height, frames, incomplete, rejected, lost, datagrams,
fps, last_latency_ms, sender, ddp_listening, raw_listening}` and `playback.stream_up`.
Settings group `stream` (takeover, silence_ms 500..60000, ddp_enabled, ddp_port,
raw_udp_enabled, raw_udp_port); changing a port or an enable reopens the sockets.
`tools/stream_send.py` sends a test pattern, an image or the PC screen over either
protocol; `tests/device/stream_smoke.py` checks both pixel for pixel.

**DDP** (UDP 4048, the protocol LedFx, xLights and WLED speak): 10-byte header, flags
`0x40` (version 1) with `0x01` on the last chunk of a frame ("push") and `0x10` when a
4-byte timecode follows the header; byte 1 low nibble sequence; byte 2 data type (RGB
8-bit assumed); byte 3 destination id; bytes 4-7 the byte offset (big-endian); bytes
8-9 the payload length (big-endian). A chunk at offset 0 starts a frame; the byte count
at the push decides the size: 12 288 = 64x64, 49 152 = 128x128 (box-downscaled 2:1);
any other count is dropped as incomplete. Queries are ignored, and no reply is sent.

**Raw p64** (UDP 4064), little-endian, 22-byte header then up to 1400 bytes of payload:

| Offset | Field | Meaning |
|---|---|---|
| 0 | `char[4]` | magic `P64F` |
| 4 | u8 | version, 1 |
| 5 | u8 | pixel format: 0 RGB888, 1 RGB565 (little-endian), 2 indexed 8-bit with a palette |
| 6 | u16 | width, 1..128 |
| 8 | u16 | height, 1..128 |
| 10 | u16 | frame sequence number; a change starts a new frame; gaps count as `lost` |
| 12 | u8 | flags: bit 0 a 768-byte RGB888 palette opens the frame's stream (required for indexed), bit 1 last chunk |
| 13 | u8 | reserved, 0 |
| 14 | u32 | byte offset of this chunk in the frame's stream (palette, then pixels) |
| 18 | u32 | total bytes of the frame's stream; must equal width x height x bytes per pixel (+ 768 with a palette) |
| 22 | bytes | the chunk |

Chunks may arrive in any order; a frame is complete when every byte is in (the "last"
flag is the sender's end mark, not a cut-off); a chunk with a new sequence number or
geometry abandons an unfinished frame, counted as `incomplete`. Any size up to 128x128 is
scaled by the artwork rules (integer nearest up, box average down, background bars).
Both protocols: the latest complete frame wins; a frame is on the panel about 10 ms
after its last chunk (assembly and scaling on core 0, the copy to the panel on core 1,
one refresh); senders above 60 fps are sampled at the panel's 60 fps cap, always the
freshest frame.

Takeover (spec 8.3): with `stream.takeover` on, the first complete frame takes the panel
in the Animation show and Widget states (after the boot animation; a pairing screen
finishes first) and the state keeps running invisibly (swap timer, navigation, history)
until `silence_ms` pass without a frame; then the panel returns to whatever the state
has up. With takeover off, frames are counted but ignored outside the Stream state. In
the Stream state a waiting screen (hostname, IP, ports) shows between streams. Streams
never enter history and the clock overlay is not drawn over them.

## Files (M4)

| Route | Method | What |
|---|---|---|
| `/api/v1/files?path=<rel>` | GET | list a folder (relative to the card root) |
| `/api/v1/files?path=<rel>` | POST | upload the raw body as that file (sniffed and trial-decoded first; 5 MB cap) |
| `/api/v1/files/get?path=<rel>` | GET | download a file |
| `/api/v1/files?path=<rel>` | DELETE | delete a file or empty folder (the p64 folders are protected) |
| `/api/v1/files/mkdir?path=<rel>` | POST | create a folder |
| `/api/v1/files/rename` | POST | `{"from":..., "to":...}` |
| `/api/v1/files/format` | POST | `{"confirm":"FORMAT"}` required: formats the card (FAT32) and recreates the p64 folders; the show rescans |

Changes under `animations/` make the show rescan its local channels (2 s debounce).

## Network and time (M3/M4)

| Route | Method | What |
|---|---|---|
| `/api/v1/wifi/scan` | GET | networks in range |
| `/api/v1/wifi` | POST | `{"ssid":..., "password":...}` save and connect |
| `/api/v1/wifi/erase` | POST | forget the network, restart in setup mode |
| `/api/v1/timezones` | GET | the IANA zone names the device knows |

## Diagnostics (M4)

| Route | Method | What |
|---|---|---|
| `/api/v1/diag/log?bytes=N` | GET | the tail of the log ring buffer (text) |
| `/api/v1/diag/memory` | GET | heaps and task stacks |
| `/api/v1/diag/dma` | POST | `{"priority":0..5}` the panel's GDMA priority, live |
| `/api/v1/diag/bench?path=<rel>&loops=N` | GET | decode benchmark of a card file |
| `/api/v1/action/reboot` | POST | |
