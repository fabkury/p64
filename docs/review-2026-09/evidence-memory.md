# Evidence: memory inventory (code audit, 2026-09-22)

Inventory of every task, long-lived allocation and configuration item that costs internal
RAM, taken from the code on 2026-09-22 (commit 0d0507f plus the instrumentation branch).
Numbers without "est." are stated in code or docs; "est." are derived. The analysis and
the proposals are in `memory.md` and `proposals.md`; this file is the evidence they cite.

Rule applied throughout: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` means any `malloc`,
`new`, `std::vector` or `std::string` of 4096 bytes or less lands in internal RAM, larger
in PSRAM (both fall back to the other pool when the preferred one is exhausted).

Recorded baselines (`firmware/docs/PROGRESS.md`): 96 KB internal free with no network
(M4); 25 to 30 KB free, largest 24 KB, with MQTT up (M6); 23 to 24 KB, largest 17 KB, with
MQTT and the stream listener (M8); largest 21.5 KB after hours (2026-09-20).

## 1. Tasks

### 1a. Created by the firmware

| Task | Where | Core | Prio | Stack | Stack RAM | Lifetime | Feature |
|---|---|---|---|---|---|---|---|
| `render` | renderer.cpp:28 | 1 | 20 | 6144 | internal | forever | display/render |
| `player` | player.cpp:30 | 1 | 15 | 12288 | PSRAM | forever | playback |
| `main` (show loop) | sdkconfig `ESP_MAIN_TASK_STACK_SIZE=8192`, `AFFINITY_CPU0` | 0 | 1 (IDF default; architecture.md says 5) | 8192 | internal | forever | show |
| `events` | event_bus.cpp:88 | 0 | 5 | 6144 | PSRAM | forever | system |
| `loader` | main/loader.cpp:163 | 0 | 4 | 8192 | PSRAM | forever | storage/show |
| `makapix` (fetcher) | fetcher.cpp:767 | 0 | 4 | 12288 | PSRAM | forever | Makapix |
| `ws_push` | ws.cpp:119 | 0 | 4 | 6144 | PSRAM | forever | web |
| `weather` | widgets.cpp:339 | 0 | 3 | 12288 | PSRAM | forever | widgets |
| `sensor` | widgets.cpp:336 | 0 | 3 | 4096 | PSRAM | forever | widgets |
| `stream` | stream.cpp:349 | 0 | 9 | 8192 | PSRAM | forever | streams |
| `imu` | inputs.cpp:119 | 0 | 6 | 4096 | PSRAM | forever (if the IMU answers) | inputs |
| `dns` | dns_hijack.cpp:79 | 0 | 4 | 3072 | PSRAM | setup mode only | net |
| `ota` worker | ota.cpp:368 | 0 | 5 | 12288 | PSRAM | per check or install | OTA |
| `ota_flash` | ota.cpp:298 | 0 | 5 | 4096 | internal | per install | OTA |
| `flash_op` | flash_guard.cpp:40 | caller's | caller+1 | 4096 | internal | per NVS call from a PSRAM-stack task | system |
| `portal_act` | setup_portal.cpp:171 | 0 | 5 | 4096 | internal | per portal save or erase | net |
| `reboot`, `factory`, `wifi_change` | api.cpp:316,408 / 339 / 477,485 | any | 5 | 2048 / 4096 / 4096 | internal | per action | web |
| `mqtt_task` (esp-mqtt) | mqtt.cpp:181 | unpinned (`CONFIG_MQTT_TASK_CORE_SELECTION` not set) | 5 | 6144 | internal | while paired | Makapix |
| `httpd` | net/http_server.cpp:39-42 | 0 | 5 | 8192 | internal | forever | HTTP |

Documentation drift: fetcher.cpp:3 and :765 say "internal stack (TLS runs here)"; the code
at :766-767 creates it with a PSRAM stack of 12 KB; architecture.md section 12 says
"internal 10 KB" and section 20 lists `makapix` among the PSRAM stacks.

### 1b. IDF tasks (all internal stacks)

`tcpip` 4096 (tuned, default 3072, no affinity), `sys_evt` 3584 (tuned), `esp_timer` 3584
(default, core 0), `wifi` about 6.5 KB (default, core 0), `mdns` 4096 (default), `Tmr Svc`
2048, `ipc0`/`ipc1` about 1.3 KB each, `IDLE0`/`IDLE1` 1.5 KB each. Internal stacks in total
(firmware plus IDF): est. 58 KB, plus about 350 B of TCB each.

### 1c. Queues, semaphores, timers

| Object | Where | Size | RAM |
|---|---|---|---|
| events queue, 32 x `Message` (8 B) | event_bus.cpp:85 | ~256 B | internal |
| show command queue, 16 x `Command` (28 B) | show.cpp:79-87, :1177 | ~448 B | internal |
| loader 8 x ptr, fetcher jobs 8 x ptr, player 4 x ptr | loader.cpp:160, fetcher.cpp:764, player.cpp:26 | 32 / 32 / 16 B | internal |
| TLS slot recursive mutex, stream frame semaphore | fetch.cpp:27, stream.cpp:337 | 2 | internal |
| transient binary semaphores | makapix.cpp:608 (like), ota.cpp:296, flash_guard.cpp:36 | per call | internal |
| `std::mutex` globals and members | 22 files | ~22 x 90 B, est. 2 KB | internal |
| `esp_timer` | wifi.cpp:260,263; ops.cpp:72,79; makapix.cpp:406; mqtt.cpp:193; stream.cpp:340; ota.cpp:414 | 8 (7 at steady state) | internal, tiny |

## 2. Long-lived allocations

### 2a. Explicit PSRAM (`MALLOC_CAP_SPIRAM` or `PsramAllocator`)

| Item | Where | Size |
|---|---|---|
| Ready slots, 3 x `ReadySlot` | main.cpp:113 | ~36.9 KB |
| Boot-hold scratch `Frame` (never freed) | main.cpp:132 | 12 KB |
| Player `last_frame_`, Renderer `preview_`, show `g_scratch` | player.cpp:24, renderer.cpp:24, show.cpp:1175 | 3 x 12 KB |
| Stream: 2 frames, 2 assembler stores (49 920 B each), canvas 49 152 B, packet 1 564 B | stream.cpp:326-332, :239 | ~175 KB, allocated at boot whatever the settings say |
| Log ring | main.cpp:94, log_ring.cpp:50 | 32 KB |
| All cJSON through the allocation hooks | main.cpp:87-93 | every status, settings and listing tree |
| Makapix indexes, `MakapixEntries` (64 B per entry, up to 4096) | makapix_index.hpp:41; kept in `Channel::entries` and `walk_fresh` (internal.hpp:25,40), again in the show's `ChannelRuntime::mk_entries` (show.cpp:119), and a transient copy per `snapshot()` (makapix.cpp:500) | up to 256 KB per channel, 2 to 3 copies |
| Local entries, `LocalEntries` (140 B per entry) | local_index.hpp:23; caps 4096 per channel, 16384 per playset (loader.cpp:23-24) | up to 2.3 MB |
| Memory cache without a card | cache.cpp:18-19 | 6 MB, 48 files |
| Every `FrameSource` (static, boot, clock, weather, temperature, stream, countdown) | show.cpp:202, widgets.cpp:328, stream.cpp:338, ops.cpp:99 | 12 KB and more each, `allocate_shared` |
| Cache-sweep key list | makapix.cpp:653 | transient |
| OTA image | ota.cpp:38,270 | up to 6 MB, transient |

### 2b. Implicit by the 4096 rule (no caps; PSRAM only when larger than 4 KB)

| Item | Where | Note |
|---|---|---|
| `Artwork::bytes_`, the file contents | artwork.hpp:50, loader.cpp:54 | files of 4 KB or less land internal (common for 64x64 pixel art); two artworks live at once (`g_current`, `g_prepared`, show.cpp:166,184) |
| GIF canvas and disposal backup, `new uint8_t[w*h*3]` | gif_decoder.cpp:38,115 | internal when the canvas is 36x37 px or smaller |
| PNG and WebP canvases, `static_pixels_`, `subframe_`, `prev_snapshot_` | png_decoder.hpp:56-73, webp_decoder.hpp:43,48 | same rule |
| `AnimatedGIF` object (about 27 KB, gif_decoder.hpp:48) | gif_decoder.cpp:18 | PSRAM by size |
| libpng and libwebp internal state | their own `malloc` | mixed; not hooked to PSRAM |
| `Assembler::have_`, 780 B x 2 | protocol.hpp:83, stream.cpp:334-335 | internal, forever |
| `fetch::Result::body` | fetch.cpp:113,127 | grows 2 KB, 4 KB (internal), then 8 KB and more (PSRAM) |
| `merge_index` `std::unordered_map` | makapix_index.cpp:180-182 | one node of about 28 B per previous entry: est. 57 KB of small internal nodes at the default 2048 cap, 115 KB at 4096, transient per refresh; spills into PSRAM only once internal RAM is exhausted |
| History: 32 items x 7 `std::string` (paths longer than 15 characters allocate) | history.hpp:16-31, show.cpp:160 | est. 5 to 8 KB internal, rewritten per swap |
| Makapix credentials (three PEMs, each under 4 KB) held in `g_creds` (makapix.cpp:32) and copied to `g_ca`, `g_cert`, `g_priv` (mqtt.cpp:27,154-156) | est. 3 to 5 KB per copy, two copies | internal |
| `esp_http_client` buffers, 4096 rx + 1536 tx per open client | fetch.cpp:48-49 | internal (4096 is at the limit, not above it); the vault session is kept open 20 s (fetcher.cpp:45-47,747) next to a walk session (fetcher.cpp:272): up to about 11 KB plus the client structs |
| esp-mqtt buffers, 4096 rx + 2048 tx | mqtt.cpp:183-184 | internal |
| `png_encode.cpp:8` `crc_table[256]` | .bss | 1 KB internal |
| weather samples `std::deque` | widgets.cpp:50 | about 512 B, internal |

### 2c. Internal by design (static, .bss, DMA)

| Item | Where | Size |
|---|---|---|
| HUB75 row buffers, 2 x (64 px x 10 planes x 2 B x 32 rows) | gdma_dma.cpp:397-402,429 | 81 920 B, DMA |
| HUB75 descriptor arrays, allocated once, rebuilt in place | gdma_dma.cpp:1351,1369; P64-CHANGES.md:81 | 2 x 13.8 KB = 27.6 KB, DMA |
| `Display g_display` (.bss): `physical_` 12 288 B + LUT 768 B + fields | display.hpp:118,135; main.cpp:54 | about 13.3 KB (the largest static symbol in the map, 13 208 B) |
| Card I/O bounce buffer (must be internal and DMA-capable, card.cpp:275-279) | card.cpp:283 | 16 KB |
| `FrameQueue` (atomics must be internal) | main.cpp:117 | about 24 B |
| PIN sessions, 8 x 48 B | auth.cpp:46 | 384 B |
| httpd: 96 handler slots, about 70 `httpd_uri_t` copies with `strdup` of the URI, 70 `Route` wrappers (http_server.cpp:41,63), 12 socket slots, scratch buffers | est. 8 to 10 KB |
| Fonts, weather icons, time zone table, UI files | fonts_data.cpp, weather_icons.cpp, tz.cpp, p64_web/ui | flash only; the UI is 181 946 B |

Static internal RAM as linked (`idf.py size`, 2026-09-22): DIRAM 150 127 B used of
341 760 B (.text 83 163 B of code placed in internal RAM, .bss 41 728 B, .data 25 236 B);
IRAM 16 384 B. The largest static symbols in the map: `g_display` 13 208 B (main),
`g_cnxMgr` 3 880 B (Wi-Fi), the coredump stack 1 892 B, two mDNS packet buffers 1 460 B
each, `s_wifi_nvs` 1 308 B, the lwIP DNS table 1 184 B, the PNG encoder's CRC table 1 024 B.

## 3. Per-feature attribution (internal RAM at steady state; est. unless stated)

| Feature | Internal | PSRAM | Could move to PSRAM safely |
|---|---|---|---|
| Display and render | about 110 KB of driver DMA memory (required) + 13.3 KB `g_display` + 6 KB render stack: about 130 KB | 12 KB preview | `physical_` is a CPU staging copy fed to `draw_pixels()` (display.cpp:304-305), not a DMA target: 12 KB movable if the copy time stays acceptable |
| Playback and decoders | 0 to 20 KB of small files and canvases (two artworks), the queue, mutexes | 37 KB slots + 24 KB frames + up to 5 MB of bytes + about 27 KB of GIF state + up to 400 KB of canvases per artwork | `bytes_`, the canvases, `have_`: none touch DMA or flash |
| Storage and card | 16 KB bounce (required) + FATFS objects, est. 4 to 8 KB | 8 KB loader stack, entries | none |
| Settings and NVS | `g_settings` about 300 B; the NVS page cache est. 4 to 8 KB (IDF); 4 KB `flash_op` per guarded call | none | none |
| Wi-Fi, lwIP, mDNS, SNTP | budget 40 to 60 KB (architecture.md section 6): static RX 8 x 1600 = 12.8 KB DMA, the wifi, tcpip, sys_evt, mdns and esp_timer stacks about 21 KB, pools, 24 sockets with mailboxes | dynamic RX 64, lwIP pbufs | IDF-controlled |
| HTTP, API, WebSocket, preview | httpd 8 KB stack + about 9 KB of tables; 1 KB `query_param` per request (api.cpp:119) | ws_push 6 KB stack; all JSON; the snapshot frame and PNG (api.cpp:419-421) | none |
| Web UI | 0 | 0 | flash 182 KB |
| Makapix | mqtt task 6 KB + 6 KB buffers + the TLS context (about 40 KB per session budgeted, mostly internal during the handshake) + the PEMs twice (6 to 10 KB) + the vault and walk clients (5.6 to 11 KB) + `Job` objects + the `merge_index` transient: about 25 to 70 KB, the dominant variable | 12 KB stack, indexes two or three times, downloads, the memory cache | the second PEM copy; the merge map nodes |
| Widgets | about 1 KB (deque, strings); a transient TLS session for the weather (one at a time through the TLS slot) | 16 KB of stacks | none |
| Streams | 1.6 KB `have_` + two UDP netconns with mailboxes (48 x 4 B each) + semaphore and timer | 8 KB stack + 175 KB of buffers | `have_` |
| Inputs | I2C driver 1 to 2 KB | 4 KB stack | none |
| Ops (OTA, reliability, night, RTC) | timers; reliability strings under 1 KB; OTA borrows a 4 KB internal task and client buffers per job | 12 KB stack + up to 6 MB image, transient | none |
| PIN | 384 B + a mutex | none | none |

## 4. Smells

1. Small artworks go internal: `bytes_` of 4 KB or less and canvases of 4 KB or less use
   plain `std::vector` and `new` (artwork.hpp:50, gif_decoder.cpp:38,115, the PNG and
   WebP headers). Two artworks at a time, replaced every swap: medium-lived internal
   blocks interleaved with TLS churn, on the most fragmented heap.
2. `merge_index` node storm (makapix_index.cpp:180-182): a `std::unordered_map` with one
   heap node per previous entry, every node under 4 KB and therefore internal first. With
   25 KB free and a 2048-entry index it exhausts internal RAM and spills to PSRAM by
   fallback; the internal remainder is fragmented afterwards. The bucket array (the
   `reserve`) is PSRAM by size.
3. An allocation inside a critical section: `logring::tail()` resizes `out` (up to 64 KB)
   and copies 64 KB under `portENTER_CRITICAL` (log_ring.cpp:60-68).
4. The PEMs are held twice (makapix.cpp:32 and mqtt.cpp:27,154-156), both internal by
   size.
5. `Settings` copied by value on hot paths: `system::settings()` (settings.cpp:439-441,
   six `std::string` members) is called per frame in the clock, weather and temperature
   sources (widgets.cpp:178,227,291), in `overlay_key()` on the player task
   (widgets.cpp:370) and per delivered stream frame (stream.cpp:72,106). Heap-free only
   while every string fits the small-string optimisation (15 characters); a time zone
   such as `America/Sao_Paulo` costs one internal malloc and free per copy per frame.
6. The status push every 2 s (ws.cpp:95-101) builds its tree in PSRAM (good), then
   `broadcast(const std::string&)` copies the printed document once more (ws.cpp:99;
   PSRAM by size). `build_status()` also calls `Display::health()`, which busy-waits
   200 us under `driver_mutex_` (display.cpp:445,462).
7. Per-command heap objects: the show's `send()` boxes a `new std::string` or `new
   Playset` per command (show.cpp:1263-1288); the loader `new LoadResult` and
   `new ScanResult` (loader.cpp:50,91); the player boxes a `shared_ptr` per `play()`
   (player.cpp:40). All internal, transient, small: correct, but churn.
8. History strings (history.hpp:16-31): est. 5 to 8 KB internal, rewritten per swap.
9. Post-boot internal allocations that can fail at a 16 KB largest block: the `flash_op`
   helper task, 4 KB plus a TCB per guarded NVS call (flash_guard.cpp:40; returns false on
   failure, and `settings_update` then fails silently); `ota_flash` 4 KB (ota.cpp:298);
   `wifi_change`, `factory` and `portal_act` 4 KB each (api.cpp:339,477;
   setup_portal.cpp:171); an `esp_http_client` 4096 B rx buffer per request
   (fetch.cpp:48); the esp-mqtt 4096 B buffer on `mqtt::start()` (mqtt.cpp:183); every
   TLS handshake. Each needs a contiguous internal block of 4 KB or more.
10. Unguarded NVS in wifi.cpp (wifi.cpp:50-82): safe today only because every caller
    (`start`, `portal_act`, `wifi_change`, `factory`, main) happens to run on an internal
    stack; nothing enforces it.
11. Stream buffers at boot (stream.cpp:326-335): 175 KB of PSRAM even with both listeners
    disabled; harmless on 16 MB.
12. The boot-hold scratch frame leaks (main.cpp:132-135): 12 KB of PSRAM, harmless.
13. Checked and clean: `Player::run` copies an `Overlay` per frame (player.cpp:90-96), but
    both callables are captureless, so no heap; the cache sweep key list uses
    `PsramAllocator` (makapix.cpp:653); the indexes and entries use it throughout.

## 5. sdkconfig.defaults items affecting internal RAM

| Item | Value | Default | Assessment |
|---|---|---|---|
| `SPIRAM_MALLOC_ALWAYSINTERNAL` | 4096 | 16384 | tuned down; this boundary is what puts the 4096 B client and MQTT buffers internal |
| `SPIRAM_MALLOC_RESERVE_INTERNAL` | 65536 | 32768 | tuned up |
| `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | y | n | tuned |
| `SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` | y | n | tuned (enables the PSRAM stacks) |
| `ESP_WIFI_STATIC_RX_BUFFER_NUM` / `DYNAMIC_RX` / `RX_BA_WIN` | 8 / 64 / 4 | 10 / 32 / 6 | tuned (static: 3.2 KB less internal; dynamic in PSRAM) |
| Wi-Fi TX buffers, `ESP_WIFI_IRAM_OPT`, `ESP_WIFI_RX_IRAM_OPT` | defaults | dynamic 32; both IRAM options on (tens of KB of shared SRAM) | not tuned; see the experiments in `memory.md` |
| `LWIP_TCP_WND_DEFAULT` / `SND_BUF` | 65535 / 65535 | 5760 / 5760 | tuned for throughput; pbufs in PSRAM |
| `LWIP_TCP_RECVMBOX_SIZE` / `UDP_RECVMBOX_SIZE` | 64 / 48 | 6 / 6 | tuned; internal 256 B / 192 B per socket |
| `LWIP_MAX_SOCKETS` | 24 | 10 | tuned; static tables about 1.5 KB |
| `LWIP_TCPIP_TASK_STACK_SIZE`, `ESP_SYSTEM_EVENT_TASK_STACK_SIZE`, `ESP_MAIN_TASK_STACK_SIZE` | 4096 / 3584 / 8192 | 3072 / 2304 / 3584 | tuned up from measured headroom |
| `MBEDTLS_DYNAMIC_BUFFER`, `_FREE_CONFIG_DATA`, `_FREE_CA_CERT`, `DEFAULT_MEM_ALLOC`, hardware AES and SHA | y | n | tuned (the measured 15 to 25-30 KB gain of M6) |
| `MBEDTLS_SSL_IN/OUT_CONTENT_LEN`, the certificate bundle | defaults 16384 / 4096, full bundle | | not tuned; the in-buffer is PSRAM by size |
| `HTTPD_WS_SUPPORT` | y | n | needed; `MAX_REQ_HDR_LEN` and `MAX_URI_LEN` default 512; server sizes set in code (8 KB stack, 12 sockets, 96 handlers) |
| esp-mqtt | task and buffers set in code (6144, 4096 / 2048) | 6144, 1024 | buffers raised above default; core selection not enabled, so the task is unpinned despite the "every network task on core 0" rule |
| `ESP_TIMER_TASK_STACK_SIZE`, `MDNS_TASK_STACK_SIZE`, `FREERTOS_TIMER_TASK_STACK_DEPTH`, cache sizes (instruction 16 KB, data 32 KB) | defaults | | not tuned |
| `FREERTOS_USE_TRACE_FACILITY` | y | n | small per-TCB cost, needed by `/diag/memory` |

## Top 10 internal-RAM consumers, ranked

1. HUB75 driver DMA memory: 81.9 KB of row buffers + 27.6 KB of descriptors, about
   110 KB (gdma_dma.cpp:402,429,1351,1369). Required by the panel.
2. Task stacks kept internal, about 58 KB (render 6, httpd 8, main 8, mqtt 6, wifi about
   6.5, tcpip 4, mdns 4, sys_evt 3.5, esp_timer 3.5, Tmr Svc 2, ipc and idle about 5.6).
3. Wi-Fi driver and lwIP, 40 to 60 KB (the architecture's budget): 12.8 KB of static RX
   DMA, driver state, pools, the mailboxes of 24 sockets.
4. The Makapix MQTT-over-mTLS session: 6 KB stack + 6 KB buffers + the mbedTLS context
   and handshake state (dynamic; the measured drop from 96 to 25-30 KB free in M6 is
   dominated by this). The largest variable cost.
5. The `merge_index` transient node storm: est. 57 KB at the default 2048 cap
   (makapix_index.cpp:180). Transient, but it hits the heap at its worst moment.
6. The card I/O bounce buffer, 16 KB DMA (card.cpp:283). Required.
7. `Display g_display` .bss, 13.3 KB, of which `physical_` (12 KB) is CPU staging
   (display.hpp:135).
8. httpd tables and sockets, est. 8 to 10 KB (96 handler slots, about 70 route copies and
   wrappers, 12 socket slots, scratch).
9. `esp_http_client` buffers, 4096 + 1536 B per open client, up to two kept alive by the
   fetcher (fetch.cpp:48-49; fetcher.cpp:45,272): about 11 KB.
10. Small long-lived objects that could be PSRAM: the PEMs twice (6 to 10 KB), the history
    strings (5 to 8 KB), files and canvases of 4 KB or less for two artworks (0 to 20 KB),
    `Assembler::have_` (1.6 KB), the weather deque (0.5 KB).
