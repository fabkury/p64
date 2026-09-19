# p64 HTTP API v1 (route reference)

The device's own API (spec section 12, ADR 0002): `http://<hostname>.local/api/v1/...`,
JSON bodies, p3a's envelope `{"ok":true,"data":...}` / `{"ok":false,"error":"...","code":"..."}`.
Actions answer at once; the work happens on the main task. No authentication yet (PIN
comes with M9). `tests/device/api_smoke.py` and `tests/device/content_smoke.py` exercise
everything below against a live device.

## Status and settings

| Route | Method | What |
|---|---|---|
| `/api/v1/status` | GET | firmware, uptime, heap, network, time, card, panel health, `playback` (below) |
| `/api/v1/settings` | GET | the settings document (spec section 16 groups) |
| `/api/v1/settings` | PUT | merge the keys present, clamped to their ranges; returns the document |
| `/api/v1/ws` | WebSocket | `{"type":"status","data":...}` every 2 s and on events |
| `/api/v1/frame` | GET | the panel's current logical frame as PNG (live preview) |
| `/api/v1/frame.raw` | GET | the same as 64x64x3 RGB888 bytes |

`playback`: `state`, `paused`, `playset {name, builtin, channels, scanning}`, `artwork
{name, path, format, width, height, bytes, animated, frames_decoded, since_s, channel,
channel_index, source}` (absent on a status screen or pause), `no_artwork` (reason or
""), `last_error`, `history {count, position, can_back, can_forward}`, `auto_swap
{interval_s, remaining_s}`, `prepared`, `swaps`, `load_failures`, `frames`, `late`,
`skipped`.

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
| `/api/v1/channels` | GET | | the active playset's channels: `{index, kind, identifier, display_name, weight, offset, entries, available, status, share, credit, cursor}` |
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
| `/api/v1/makapix` | GET | | `{state: unpaired|pairing|paired|invalid, host, player_key, code, code_seconds_left, online, mqtt_connected, activity, last_error, cert_expires_at, refreshes, downloads, download_failures, views_sent, commands}` (also under `makapix` in the status document) |
| `/api/v1/makapix/pair` | POST | | asks the server for a pairing code; `code` appears in the status within seconds and on the panel |
| `/api/v1/makapix/pair/cancel` | POST | | drops the code |
| `/api/v1/makapix/unpair` | POST | | erases the credentials, closes the MQTT session |
| `/api/v1/makapix/like` | POST | `{"post_id":n,"like":true|false}` | reacts as the owner (needs pairing; blocks up to 20 s) |
| `/api/v1/action/play` | POST | `{"post":"<sqid or makapix.club/p/<sqid>>"}` | play-this of a Makapix post (looked up and downloaded, then played) |
| `/api/v1/action/play` | POST | `{"url":"http(s)://..."}` | play-this of an arbitrary artwork URL (downloaded into `downloads/`) |

Channel objects of `/api/v1/channels` for Makapix kinds add `cached`, `last_refresh`
(epoch seconds), `refreshing` and `error`. History items and the status artwork carry
`post_id` and `sqid` for Makapix artworks.

## Widgets (M7)

The status document carries `sensor {valid, temperature_c, humidity, trend_c_per_hour}`
and `weather {valid, error, age_s, temperature, units, humidity, code, condition, is_day,
today_max, today_min, days:[{weekday, condition, max, min}]}`; `playback.state` is
`animation_show`, `widget` or `stream` and `playback.widget` names the widget on the
panel. Settings groups `clock` (face, font, scale, seconds, blink_colon, h24,
date_order, colour, background), `weather` (latitude, longitude, units,
refresh_minutes) and `temperature` (offset_temperature, offset_humidity, trend) join
`show.clock_overlay` and `widgets` (widget, interlude_percent). History items of kind
`interlude` carry `widget`.

## Files (M4)

| Route | Method | What |
|---|---|---|
| `/api/v1/files?path=<rel>` | GET | list a folder (relative to the card root) |
| `/api/v1/files?path=<rel>` | POST | upload the raw body as that file (sniffed and trial-decoded first; 5 MB cap) |
| `/api/v1/files/get?path=<rel>` | GET | download a file |
| `/api/v1/files?path=<rel>` | DELETE | delete a file or empty folder (the p64 folders are protected) |
| `/api/v1/files/mkdir?path=<rel>` | POST | create a folder |
| `/api/v1/files/rename` | POST | `{"from":..., "to":...}` |

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
