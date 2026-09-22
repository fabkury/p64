# Evidence: CPU inventory (code audit, 2026-09-22)

Every periodic activity, per-frame path, event handler and blocking hazard in the
firmware, from the code on 2026-09-22. "est." marks costs derived from the code; the
measured per-task figures are in `cpu.md`. Tick rate 1 kHz (`CONFIG_FREERTOS_HZ=1000`),
240 MHz, task watchdog 10 s.

## 0. Task table as built (one difference from architecture.md section 2)

| Task | Core | Prio | Created at |
|---|---|---|---|
| render | 1 | 20 | renderer.cpp:28 (internal stack) |
| player | 1 | 15 | player.cpp:30 (PSRAM stack) |
| stream | 0 | 9 | stream.cpp:349 |
| imu | 0 | 6 | inputs.cpp:119 |
| events, ota, portal_act, httpd (IDF default), esp-mqtt | 0 | 5 | event_bus.cpp:88, ota.cpp:368, mqtt.cpp:182 |
| ws_push, makapix, loader, dns | 0 | 4 | ws.cpp:119, fetcher.cpp:767, loader.cpp:163 |
| sensor, weather | 0 | 3 | widgets.cpp:336,339 |
| main (the show loop) | 0 | 1 (IDF default; no `vTaskPrioritySet` anywhere) | main.cpp |
| IDF: wifi 23, esp_timer 22, sys_evt 20, tiT 18, mdns 1 | 0 | | defaults |

The architecture table says the show loop runs at 5; it runs at 1, below every p64 task
and every IDF task. Every core-0 task preempts it.

## 1. Periodic and polling activities

| # | Activity (owner) | Where | Period | Work per tick | Cost per tick (est.) |
|---|---|---|---|---|---|
| 1 | IMU sampler (inputs) | inputs.cpp:65, `vTaskDelayUntil` 4 ms | 250 Hz | one `i2c_master_transmit_receive` of 6 B at 400 kHz (qmi8658.cpp:27,74), float `sqrt` (tap.cpp:25), `sqrt` and `atan2` (orientation.cpp:51-52), one mutex take | about 150 us of I2C wait (blocked, not CPU) + 30 to 60 us CPU with two context switches: 10 to 15 ms/s, the largest steady wake-up source (measured 2.5 to 3.4 % of core 0, `cpu.md`) |
| 2 | SHTC3 sampler (widgets) | widgets.cpp:73 | 60 s (600 s if absent) | 3 I2C operations + 1 ms delay (shtc3.cpp:48-52), a `settings()` copy, deque trim | negligible |
| 3 | Weather task (widgets) | widgets.cpp:86, notify wait 30 s | 30 s wake; a fetch every `refresh_minutes` | wake: `settings()` copy + `wifi::status()`; fetch: HTTPS GET with a TLS handshake + cJSON parse of the Open-Meteo body | wake negligible; fetch 0.3 to 1 s of core 0 per refresh interval |
| 4 | WebSocket push (web) | ws.cpp:84 | 100 ms unconditional wake; a build every 2 s or on `notify()` when a client is connected | `build_status()` (api.cpp:147-238), `cJSON_PrintUnformatted`, `httpd_ws_send_data` per client | idle: 10 wakes/s of microseconds each; with a client see 1a |
| 5 | Live preview (web UI) | index.html:197 `setInterval(tick,1000)`, `GET /api/v1/frame`, api.cpp:418 | 1 Hz per open Home page while visible | `snapshot()` under `preview_mutex_` (12 KB copy, renderer.cpp:53), `encode_png_rgb` (png_encode.cpp:47): three vectors of about 12.4 KB (PSRAM), CRC32 over 12.4 KB, adler32 with two `%` per byte, stored zlib; a 12.5 KB HTTP send | 0.6 to 1 ms encode + 1 to 2 ms lwIP send: 2 to 3 ms/s per page |
| 6 | Makapix fetcher idle tick | fetcher.cpp:695, queue wait 500 ms | 2 Hz | `nightly_sweep_tick` (every 10 s: `settings()` copy + `localtime`), `online()` = `wifi::status()` (5 string copies + `esp_wifi_sta_get_ap_info`, a round trip to the wifi task, wifi.cpp:352-370) + `clock::synced()`, `next_channel_needing_service()` (mutex, O(channels)), `download_step()` with `next_download()`, an O(entries) flag scan under the makapix mutex | 100 to 150 us: about 0.3 ms/s |
| 7 | Makapix pairing poll | fetcher.cpp:31,720 | 3 s while pairing | HTTPS GET | a TLS session per poll, only during pairing |
| 8 | Makapix renewal check | fetcher.cpp:36 | 24 h | certificate check | nil |
| 9 | Makapix view timer | makapix.cpp:133,541, one-shot 5 s after each `note_shown` | per artwork | queues a view job: HTTPS POST on the fetcher | one TLS request per artwork shown for 5 s or more (every 30 s at the default auto-swap); reuses the vault session while it is open (`kVaultIdleUs` 20 s) |
| 10 | MQTT heartbeat | mqtt.cpp:194, periodic 30 s, on the esp_timer task (prio 22) | 30 s | `publish_status`: a small cJSON tree + QoS 1 publish; `current_post_id` takes the show's `g_mutex` | 0.5 to 1 ms per 30 s |
| 11 | MQTT keepalive | mqtt.cpp:175 | 60 s | esp-mqtt ping | nil |
| 12 | Show loop tick | show.cpp:1238, `wait_ticks()` up to 1 s | at least 1 Hz plus per command | two `system::settings()` copies per iteration (show.cpp:1099,1130: a mutex and a copy of a struct with six `std::string`; SSO keeps allocations at zero at the defaults), `tick()` comparisons | 30 to 60 us |
| 13 | Night schedule | ops.cpp:73, periodic 15 s, esp_timer task | 15 s | `settings()` copy + `localtime_r`; the display is re-applied only when the brightness changed | about 20 us |
| 14 | Image confirmation | ops.cpp:80, once at 30 s | once | an NVS write through the flash guard | once |
| 15 | OTA check | ota.cpp:415,386 | 90 s until online, then 12 h | HTTPS GET of the GitHub API + JSON parse | about 1 s of CPU per 12 h |
| 16 | Wi-Fi timers | wifi.cpp:31-32,205-210,327 | the 60 s fallback once; reconnect backoff; a 30 s setup retry only in setup mode | a reconnect attempt | none at steady state |
| 17 | SNTP | IDF default `CONFIG_LWIP_SNTP_UPDATE_DELAY` = 1 h | 1 h | one UDP exchange; `on_sync` writes the RTC | nil |
| 18 | mDNS | wifi.cpp:99-107, IDF task prio 1 | on query | responder | nil when idle |
| 19 | Stream listener idle | stream.cpp:248, `select` 250 ms | 4 Hz (plus `esp_task_wdt_reset`) | fd_set setup | negligible; a 500 ms sleep when no socket (:253) |
| 20 | Loader idle | loader.cpp:135, queue 1 s | 1 Hz | watchdog reset | nil |
| 21 | Player idle with a static picture | player.cpp:15,110, `kIdleWait` 100 ms | 10 Hz on core 1 | `overlay_key()`: a `settings()` copy + `time()` + `localtime_r` (widgets.cpp:368-372) | 20 to 40 us per 100 ms |
| 22 | Renderer empty-queue poll | renderer.cpp:100, `vTaskDelay(1)` | 1 kHz on core 1 while no slot is ready | `consumer_peek()` | 2 to 3 us each with the switch: about 0.3 % of core 1 |
| 23 | Renderer far-target sleep | renderer.cpp:133 | steps of up to 10 ms while a frame is more than 1 ms away | a generation check | trivial |
| 24 | Renderer statistics log | renderer.cpp:15,66 | 10 s | `ESP_LOGI` with nine floats to the USB serial and the log ring; `take_stats` mutexes | 0.2 to 0.5 ms per 10 s on core 1 |
| 25 | Watchdog resets | show.cpp:1236, loader.cpp:134, stream.cpp:242 | per loop | `esp_task_wdt_reset` | nil |
| 26 | Nightly cache sweep | fetcher.cpp `nightly_sweep_tick`, cache.cpp:197-209 | once per night when armed | a walk of every shard with a 2 ms yield per shard, unlinks | seconds of card I/O, once a day |
| 27 | Retry loops | fetcher.cpp:418,432 (5 s after a failed download), fetcher.cpp:748 (500 ms when nothing downloaded), wifi.cpp:210 (exponential backoff) | on failure | sleep | none |

### 1a. The WebSocket status document (api.cpp:147-238), per build

Touches, in order: `heap_caps_get_free_size` twice and `heap_caps_get_largest_free_block`
(three heap walks), `wifi::status()` (mutex + five string copies + a round trip to the
wifi task for the RSSI), `clock::local_time` + `strftime`, `storage::info()` (card mutex +
`esp_vfs_fat_info`, which is `f_getfree` under the volume lock: cached normally, a full
FAT read from the card if the cache is invalid, card.cpp:151), `Display::health()`
(`driver_mutex_` + a 200 us `esp_rom_delay_us` busy-wait, display.cpp:462),
`show::status_json()` (the show's `g_mutex`, show.cpp:1303, blocked for the whole
duration of any show handler; plus the `Renderer::totals()` mutex), `makapix_status`
(makapix mutex + five string copies), `widgets::sensor()` + `weather_json()` (mutex, a
`Forecast` copy), `stream::status_json()` (mutex), `reliability::json()` (mutex, RAM
only), `ota::status()`, three `inputs` mutex takes. About 120 cJSON nodes from PSRAM,
2.5 to 3.5 KB of text. Est. 1.5 to 3 ms per build, five or more mutexes across eight
subsystems, one explicit busy-wait, then a synchronous WebSocket send per client. Built
every 2 s with a client and on every `notify()` (api.cpp:655-668, show.cpp:730,884).

## 2. Hot paths per frame (core 1)

| Step | Where | Per | Cost (est.) | Floats | Allocations | Shared mutex |
|---|---|---|---|---|---|---|
| Decode | artwork.cpp:41-52, `GifDecoder::next` (gif_decoder.cpp:72), PNG, WebP, BMP | decoded frame | GIF 64x64 1 to 3 ms; larger canvases scale with area | no | GIF: canvas once (:38), backup once (:115); APNG: `open_stream()` re-creates the libpng read structs on every loop (png_decoder.cpp:211); static PNG `new png_bytep[]` at open only (:199) | none |
| Scale | scaler.cpp:49-90 | decoded frame | integer box average, 4096 outputs; 0.1 to 0.4 ms | `double` only in `configure()` (once per artwork) | none | none |
| Overlay key | player.cpp:145, widgets.cpp:368 | every decoded frame, and every 100 ms when static | a `system::settings()` copy under `system::g_mutex` + `time()` + `localtime_r` (20 to 40 us) | no | SSO strings, no allocation at the defaults | yes: `system::g_mutex` is held by `settings_update()` across `to_json()` and the NVS flash write (settings.cpp:446-456); the player blocks on it for the duration of a flash write on every settings change |
| Overlay draw | player.cpp:146,96-103 | frames with a non-zero key | copies two `std::function` under `Player::mutex_`; a five-glyph draw with outline | no | one SSO string | `Player::mutex_` (short holders only) |
| Stats | player.cpp:180-185 | decoded frame | mutex, adds | no | none | `Player::mutex_` with `take_stats()` (render task) |
| Static source | player.cpp:144 | first frame | a 12 KB PSRAM copy | | | |
| Command poll | player.cpp:72,110 | decoded frame | `xQueueReceive` with no wait | | | |
| Wait for the boundary | display.cpp:337-388 | presented frame | sleeps while at least 1 ms of slack remains (:357), then spins from `kSpinLeadUs` 600 us before the predicted boundary plus the sub-tick remainder: 0.6 to 1.6 ms busy per presented frame; a forced one-tick yield each second (:360) | `double` period arithmetic per wait | none | none |
| Rotation and gains | frame.cpp:98-129 | presented frame | 4096 pixels x a switch + three LUT lookups; the source is in PSRAM | no | none | none |
| `draw_pixels` | display.cpp:304 | presented frame | 7.5 ms (10 planes) or 5.8 ms (8 planes), measured | driver | none | none |
| Preview copy | renderer.cpp:150-154 | presented frame | a 12 KB PSRAM-to-PSRAM copy under `preview_mutex_` | | | yes: `snapshot()` from httpd (core 0) holds it for a 12 KB copy (50 to 100 us); the priority-20 render task blocks that long |
| Renderer stats | renderer.cpp:159-162 | presented frame | mutex | | | `Renderer::mutex_` with `totals()` from the status builder |

Per presented frame on core 1: 8.5 to 9.5 ms (7.5 copy + 0.3 rotate + 0.6 to 1.6 spin +
0.1 preview). At 20 fps about 18 %, at 60 fps about 55 % of core 1 for the render task
alone. Decode at 20 fps adds 2 to 6 % (a small GIF) up to 40 % and more (128 to 256 px
canvases). No float in any per-pixel loop; `fill_disc` (frame.cpp:78) uses float only
for the analogue face once a second.

## 3. Event bus and command queue

Dispatcher: one task (priority 5, core 0) that copies the matching `std::function`s per
event (event_bus.cpp:33-40) and runs the handlers inline.

| Event | Frequency | Subscribers and cost |
|---|---|---|
| `PlaybackSwapped` | per swap (30 s default), pause, resume, widget | api.cpp:657 `ws::notify`: a status build (cheap unless a client is open) |
| `MakapixChannelChanged` | after every download (fetcher.cpp:444), every flag (:395), every cold page (`refresh_step`), card mount (files.cpp:171), a size-limit change | show.cpp:1199 `Cmd::MakapixChanged` then `on_makapix_changed()` (:879): for every Makapix channel, `makapix::snapshot()` copies the whole entry vector (64 B per entry, up to 2048: 128 KB) under the makapix mutex, moves it, rebuilds `mk_cached` in O(N), one `settings()` copy, then `web::notify()`. During a download burst (one artwork per 1 to 2 s) this is 0.5 to 1.5 ms per download on the show loop plus a status build per download if a browser is open. The heaviest handler on a frequent event. |
| `SettingsChanged` | per settings write (API PUT, a Makapix brightness or rotation command, state persistence on state changes only, show.cpp:293) | seven subscribers: inputs (a `settings()` copy), makapix (`on_settings_changed` + `mqtt::publish_state`, an MQTT publish with a cJSON build, makapix.cpp:411-414), stream (a copy), widgets (notifies the weather task, which wakes with `wifi::status()`), main twice (`apply_display_settings`: `set_brightness` under `driver_mutex_`, `set_rotation`, `set_gains`, `request_mode`; and `set_timezone` (`apply_zone` unconditionally), `set_ntp_server` (guarded), `wifi::set_hostname`, `esp_netif_set_hostname` unconditionally), show (`Cmd::Settings`). Fan-out 1 to 2 ms plus the NVS write itself. |
| `MakapixStateChanged` | rare | api (`++version`, notify), show |
| `WifiConnected`, `WifiDisconnected`, `TimeSynced` | rare | api notify; show `MakapixChanged` (the full snapshot again); makapix `on_network_change`; ops `night_tick`; widgets notify |
| `LocalFilesChanged`, `PlaysetsChanged`, `CardMounted`, `CardFailed` | per file-manager operation | show rescan (`kRescanDebounceUs`); api notify |
| `StreamStarted`, `StreamEnded` | per stream session | show swap |

The show command queue (show.cpp:1177, 16 deep) takes commands from the API, the IMU,
Makapix and the loader; every handler runs under `g_mutex` (:1239), which the status
builder needs.

## 4. Blocking and priority hazards

| # | Hazard | Where | Note |
|---|---|---|---|
| H1 | The show loop at priority 1 | main.cpp (no priority set) | Below httpd, MQTT and events (5), the fetcher, loader and ws_push (4), sensor and weather (3). A TLS handshake on the fetcher (4) or the weather task (3) runs to completion before the show loop resumes; only idle is lower. Documented as 5. |
| H2 | The player blocks on `system::g_mutex` during NVS writes | settings.cpp:446-456 holds the mutex across `to_json()` and the flash write; widgets.cpp:369 takes it per decoded frame on core 1 | Violates the section 2 rule that core-1 tasks never take a lock a core-0 task can hold for long. A brightness command from Makapix stalls the player for the write (the flash guard may also spawn `flash_op`, flash_guard.cpp:40). |
| H3 | The render task blocks on `preview_mutex_` | renderer.cpp:151 against :53 | Short (a 12 KB copy), on the priority-20 task; one httpd request per second while the Home page is open. |
| H4 | Show `g_mutex` contention | show.cpp:1239 (the whole iteration) against `status_json` (:1303), `is_paused` (:1292), `current_post_id` (:1297) | `install()` (:696) and `on_makapix_changed()` run under it; the WebSocket push (4) and the MQTT heartbeat (the esp_timer task, priority 22, through `current_post_id`) wait on it. An esp_timer task blocked on a priority-1 task's mutex delays every other esp_timer callback (night, silence, view, Wi-Fi timers) for that long; priority inheritance applies, but the handler still runs to completion. |
| H5 | A busy-wait inside a status build | display.cpp:462, `esp_rom_delay_us(200)` under `driver_mutex_` | 200 us of spin per status document (every 2 s with a client). |
| H6 | Long core-0 sections at priority 4 or more | fetcher: the TLS handshake (0.3 to 1 s of CPU with hardware SHA and AES), the cJSON parse of a 50-entry page in PSRAM, `merge_index` under the makapix mutex (`finish_walk`), a bit-serial CRC32 over the index (makapix_index.cpp:41-45, eight iterations per byte, 5 to 10 ms for 128 KB) at every save (`kSaveEveryDownloads` 8) and load; loader: `scan_folder` directory walks and the decoder open (first frame) per swap; weather: TLS at priority 3 | All below lwIP (18) and Wi-Fi (23), so they cannot starve the stack; they starve the show loop (H1) and each other. |
| H7 | Spin loops | display.cpp:364 (bounded by three periods, about 11 ms worst), display.cpp:421 (timed fallback spin of up to 1 ms), ops.cpp:97-111 (the BOOT hold, boot only) | intentional; core 1 only |
| H8 | `taskYIELD` | none used | |
| H9 | Task watchdog coverage | show loop, loader, stream (show.cpp:1234, loader.cpp:131, stream.cpp:240) + the core-0 idle; the core-1 idle is off (sdkconfig.defaults:43) | Not covered: player, render (by design), fetcher, weather, ota, ws_push, imu, sensor, events. A hung event dispatcher or ws_push would go unnoticed. |
| H10 | `wifi::status()` called at 2 Hz | fetcher.cpp:696,708 through `online()` | each call round-trips to the wifi task for the RSSI (wifi.cpp:370); cheap, but the most frequent cross-task call on core 0 after the IMU |
| H11 | A `system::settings()` copy per stream frame | stream.cpp:106 | up to 60 Hz of mutex plus struct copy on the priority-9 task during a stream |

## 5. Per-feature CPU attribution (estimates from the code)

Steady state: one GIF at 20 fps, MQTT connected, no browser.

| Feature | Core 0 ms/s | Core 1 ms/s | Basis |
|---|---|---|---|
| Render task (copy + spin + rotate + preview) | 0 | 170 to 190 | 8.5 to 9.5 ms x 20 |
| Decode + scale + overlay hook | 0 | 30 to 80 (small GIF); 200 to 500 for 128 to 256 px canvases | section 2 |
| Renderer empty-queue 1 kHz poll | 0 | about 3 | 1 #22 |
| IMU sampler | 10 to 15 | 0 | 250 Hz x 40 to 60 us |
| Wi-Fi, lwIP, MQTT idle (IDF: beacons, DTIM, keepalive, ARP) | 5 to 15 | 0 | IDF baseline, not p64 code |
| Fetcher idle tick (2 Hz, `wifi::status`) | about 0.3 | 0 | 1 #6 |
| Show loop (1 Hz + one swap per 30 s including the loader read and open) | 0.1 + 2 to 10 per swap (amortised 0.3) | 0 | 1 #12 |
| Makapix view POST + status publish per artwork (TLS reused) | 5 to 30 per artwork, amortised 0.5 to 1 | 0 | 1 #9 |
| WebSocket push idle wake (10 Hz), stream select (4 Hz), loader (1 Hz), MQTT heartbeat, night timer, sensor | under 0.5 together | 0 | section 1 |
| A download burst while the cache fills: TLS GET, card write, index CRC and save, `on_makapix_changed` snapshot | 50 to 200 per download (TLS and card I/O), plus 0.5 to 1.5 on the show loop | 0 | section 3, H6 |

Core 0 steady total est. 20 to 35 ms/s (2 to 3.5 %), of which the IMU is roughly half of
the p64-attributable part. Core 1 est. 20 to 27 % for a small GIF at 20 fps, 55 to 60 %
at 60 fps. The measured samples in `cpu.md` agree.

With a browser open on the Home page (a WebSocket client and the 1 Hz preview):

| Added activity | Core 0 ms/s |
|---|---|
| The status build every 2 s (1a) + send | 1 to 2 |
| The preview PNG at 1 Hz + a 12.5 KB HTTP send | 2 to 3 |
| Status builds on events (per swap, per download) | 1.5 to 3 each |
| httpd and lwIP per-request overhead (two requests per second) | 1 to 2 |

About 5 to 10 ms/s per open page; two pages double it. The preview's `snapshot()` also
adds one 12 KB copy per second to the render task's mutex path (H3).

## Ranked: top CPU consumers on core 0 (steady state)

1. The IMU sampler, 250 Hz I2C and float maths: 10 to 15 ms/s (inputs.cpp:65). Measured
   2.5 to 3.4 % of core 0 (`cpu.md`). Dropping to 100 Hz, or using the QMI8658's own tap
   and motion interrupts, would free the most of any p64 code.
2. The IDF network baseline (Wi-Fi power save off, the MQTT TLS session, lwIP): 5 to
   15 ms/s; not p64 code.
3. Makapix per-artwork traffic: the view POST 5 s after each swap and the throttled
   status publish, 0.5 to 1 ms/s amortised, bursty TLS (makapix.cpp:541, mqtt.cpp:220).
4. Download bursts: TLS GET + card write + the bit-serial CRC32 index save +
   `on_makapix_changed` full snapshot per download (fetcher.cpp:444, show.cpp:879,
   makapix_index.cpp:41). Transient but the heaviest per-event handler.
5. The fetcher idle tick with `wifi::status()` at 2 Hz: about 0.3 ms/s
   (fetcher.cpp:695-708).
6. The show loop (two `settings()` copies per iteration), the WebSocket push idle wake,
   the stream select, the loader, the timers: under 1 ms/s together.

With a browser open, the status document build (1.5 to 3 ms, five or more mutexes, one
200 us busy-wait) and the 1 Hz PNG preview (2 to 3 ms/s) move to positions 2 and 3.

## Ranked: per-frame cost on core 1

1. The `draw_pixels` bit-plane copy: 7.5 ms (Quality) or 5.8 ms (Photo), display.cpp:304.
2. The boundary spin in `wait_for_dma_switch`: 0.6 to 1.6 ms busy per presented frame,
   display.cpp:355-364.
3. Decode: 1 to 3 ms for a 64x64 GIF, tens of ms for 128 to 256 px canvases
   (artwork.cpp:44); APNG re-creates the libpng structs every loop (png_decoder.cpp:211).
4. `rotate_copy` with the gains LUT: 0.25 to 0.4 ms, frame.cpp:98.
5. The scaler: 0.1 to 0.4 ms, scaler.cpp:49.
6. The preview copy under `preview_mutex_`: 0.05 to 0.1 ms, renderer.cpp:151.
7. The overlay hook's `settings()` copy + `localtime_r` per decoded frame: 0.02 to
   0.04 ms, but it takes the settings mutex that `settings_update` holds across a flash
   write (widgets.cpp:369, settings.cpp:454).
8. The renderer's 1 kHz `vTaskDelay(1)` poll while the queue is empty: about 3 ms/s,
   renderer.cpp:100.
