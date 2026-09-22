# Memory discipline

Question asked: is the firmware mindful of the device's limited memory, is PSRAM used
appropriately, and would dropping any feature reduce memory consumption significantly?

Short answer: PSRAM is used well (frames, indexes, caches, artwork bytes, JSON, most task
stacks, TLS buffers), and internal RAM is nonetheless exhausted: 13 to 23 KB free at
steady state, an 11 to 16 KB largest block, and a low-water mark of 1 055 bytes read from
the device today after five hours of uptime. The target set for this review is 64 KB free
and a 32 KB largest block. Two configuration lines measured today bring the device to
54 to 58 KB free with a 32.7 KB largest block; the remaining distance is closed by
replacing the MQTT-over-mTLS session with an HTTPS command channel (the largest variable
consumer, and the one whose transient peaks cause the low-water marks) and by a handful
of code trims. No feature needs to be dropped: the features the user does not use (the
streams) cost almost no internal RAM, and the features that do cost it (Makapix, the web
server, the panel) are the product.

The inventory behind this page is `evidence-memory.md`. Everything below marked
"measured" was read from the device on 2026-09-22.

## 1. Where the internal RAM is

The ESP32-S3 has 512 KB of SRAM, of which the firmware can address about 341 KB as
data plus 16 KB as instruction RAM; code that must run with the flash cache off (the
Wi-Fi driver's hot paths, FreeRTOS, the HAL, the flash driver) is copied into the same
SRAM and takes it away from the heap.

Static, from `idf.py size` on the committed build (measured):

| Segment | Bytes | Note |
|---|---|---|
| DIRAM .text (code kept in internal RAM) | 83 163 | Wi-Fi (`libpp`, `libnet80211`, `libphy`: about 25 KB), FreeRTOS 16.5 KB, spi_flash 11 KB, HAL 7.5 KB, esp_hw_support 8 KB, heap 4.7 KB, the hub75 driver 2.2 KB |
| DIRAM .bss + .data | 66 964 | `g_display` 13.2 KB (the physical staging frame), Wi-Fi state, the coredump stack, mDNS packet buffers, lwIP tables |
| DIRAM total used | 150 127 | of 341 760: about 191 KB left for the heap before anything runs |
| Image | 2 076 119 | of an 8 MB slot; the web UI is 182 KB of it |

Dynamic, at steady state on the committed code, by allocating task (measured with heap
task tracking; internal bytes):

| Owner | Internal | Blocks | What it is |
|---|---|---|---|
| main (boot-time init) | 256 564 | 434 | the hub75 driver's DMA memory (about 110 KB), every internal task stack it created (render, httpd, plus TCBs), the card bounce buffer (16 KB), httpd tables, lwIP and netif init, mDNS, SNTP, the Wi-Fi static RX buffers (12.8 KB) |
| ipc0 | 46 128 | 50 | IDF work done through the inter-processor call on core 0 at Wi-Fi and PHY init |
| events (the dispatcher) | 20 124 | 33 | what handlers allocate on it: the esp-mqtt client, its 6 KB task stack and 6 KB buffers are created here on `WifiConnected` |
| makapix (fetcher) | 17 768 | 7 | the two kept-alive `esp_http_client`s (about 11 KB of buffers), the PEM copies |
| wifi | 15 896 | 36 | the driver's own runtime allocations |
| before the scheduler, ISRs | 14 616 | 156 | heap init, ROM, early drivers |
| sys_evt, mqtt_task, tiT, loader, stream, sensor, render, weather, timers, mdns, httpd | 13 640 | 100 | small runtime state; httpd's per-request work is PSRAM by the cJSON hooks |
| total allocated | 385 388 | | of which about 4 KB is the tracking overhead itself |
| free | 13 191 | | largest block 11 756, minimum since boot 7 775 (measured 60 s after boot on the instrumentation build; the committed build reads 20 to 23 KB free) |

The two figures that matter for the product are the free space and the largest block,
and both are dominated by things the firmware cannot avoid (the panel's DMA memory, the
Wi-Fi driver, the HTTP server) plus one thing it chose (the MQTT session).

## 2. The low-water mark

`GET /api/v1/diag/memory` reports the minimum free internal RAM since boot. On the
committed build it read 1 055 bytes at 10:32 today, 878 s after a boot that followed a
flash (reset reason `usb`); in those fifteen minutes the fetcher had walked the five
channels of the Followed playset and downloaded 425 artworks into an empty cache, with
MQTT connected and the weather fetched. When the committed build was flashed back at the
end of the session it read 939 bytes 50 s after boot, with the cache already full and
22 KB free at that moment; the instrumentation boots in between, on the same
configuration, bottomed at 7.3 to 7.8 KB. So the boot sequence itself (the Wi-Fi join,
the MQTT handshake, the Followed activation with five index loads, the first downloads,
the weather fetch and the OTA check, all inside the first two minutes) can empty the
heap, and how far it goes depends on which of those overlap. The M6 measurement ("25 to
30 KB free with downloads running") read the steady state; the low-water mark was not
read then. The device did not fail, because ESP-IDF's allocator
falls back to PSRAM for a plain `malloc` when internal RAM is out. What cannot fall back
are the allocations that must be internal: a task stack created without the PSRAM
capability (`flash_op`, `ota_flash`, `wifi_change`, `factory`, `portal_act`), a DMA
buffer, and mbedTLS's handshake state when configured for internal memory. At a 1 KB
floor, any of those fails: the settings write silently returns false (flash_guard.cpp:40
returns false when the helper task cannot be created), an OTA install refuses, the panel
mode switch was refused for this reason on 2026-09-20 (`PROGRESS.md`), and a Wi-Fi
reconnect can fail inside the driver.

Where the dips come from, in the order the evidence supports:

1. A transient TLS session next to the MQTT session: a like, a view over HTTPS, a channel
   page, the weather fetch, the OTA check. The handshake state is internal; today's
   sample showed the minimum fall from 13.0 KB to 7.3 KB during the OTA check at 93 s
   after boot (measured), so a handshake costs at least 5.7 KB of internal peak on top
   of what the dynamic buffers put in PSRAM.
2. The index merge after a channel walk: `merge_index` builds a `std::unordered_map`
   with one node of about 28 bytes per previous entry, each below the always-internal
   threshold; est. 57 KB of internal requests for the default 2048-entry cap, which
   exhausts internal RAM and spills the remainder into PSRAM (`evidence-memory.md`,
   smell 2). A refresh of the Followed playset's five channels does this five times.
3. Everything of 4 KB or less that is allocated with a plain `new` on the show path:
   artwork files up to 4 KB (most 64x64 pixel art), decoder canvases up to 36x37 px, the
   history's strings, the `fetch::Result` bodies while they are small.

## 3. Experiments (measured today)

Both experiments were flashed on the instrumentation branch and read at the same points
after boot (a 30 s sample starting at 80 s, then a reading at 170 s), on the same
playset, with MQTT connected. The baseline is the same branch without the change.

| Build | Free at 80 to 110 s | Largest block | Minimum since boot | Free at 170 s | Static DIRAM used |
|---|---|---|---|---|---|
| Baseline (committed configuration + tracking) | 13 191 | 11 756 | 7 775 | 12 903 | 150 127 |
| A: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024` | 21 123 | 12 780 | 10 239 | 25 715 | 154 147 |
| B: A + `CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n`, `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y`, `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=y` | 57 583 | 32 748 | 42 059 | 53 811 | 123 951 |

Experiment A moves every allocation between 1 KB and 4 KB to PSRAM first: the esp-mqtt
and `esp_http_client` buffers (4 KB each), the small artworks and canvases, the PEMs,
the `fetch` bodies. It gains 8 to 13 KB. The heap-by-task table showed the dispatcher's
share fall from 20 KB to 8 KB (the MQTT buffers moved) and the MQTT task keep its stack.

Experiment B additionally takes the Wi-Fi driver's hot code, the FreeRTOS kernel and the
ring buffer out of internal RAM (26 KB of static DIRAM) and hands that SRAM to the heap.
It reaches the review's target on free space and on the largest block, with a minimum
since boot of 42 KB, which is more than the committed build ever has free. During the
sample a 128x128 WebP played at 11.8 fps with the copy at 7.60 ms, 0 late flips, 0
timeouts, frame lock kept, and the MQTT session, the downloads and the weather fetch
ran as before (measured from the renderer's log lines and the status document).

What B costs and what remains to verify before it is adopted:

- Wi-Fi code in flash means cache misses on the driver's receive and transmit paths.
  Espressif documents the two `IRAM_OPT` options as throughput and latency
  optimisations; their effect on this device is unmeasured. To verify: the download rate
  from the vault (the fetcher logs bytes per file; a timed 5 MB fetch would do), the
  stream test at 128x128 30 fps (1.5 MB/s, `stream_smoke.py` reports the loss), and a
  soak with the browser open. The panel is unaffected by construction (its DMA and the
  render task do not touch the Wi-Fi code), and the measured copy time and frame lock
  agree.
- FreeRTOS in flash raises interrupt and context-switch latency by the cache miss cost
  on rarely used kernel paths. The hub75 driver has no interrupt handler that depends
  on the kernel (the DMA chain is continuous), and the render task's spin does not
  call the kernel; the measured 0.6 to 1.6 ms of spin slack absorbs microseconds. To
  verify: `panel_mode_smoke.py` (the switch stops and restarts the DMA under a mutex),
  the 45-minute soak's late-flip count, and an OTA install (flash writes with the cache
  off while the kernel's functions live in flash are an ordinary configuration for
  ESP-IDF, but this firmware's rule about PSRAM stacks and flash makes it worth an
  explicit run of `ota_smoke.py`).
- Both options are configuration, revertible by deleting two lines and `sdkconfig`.

## 4. Findings

M1. Internal RAM is being used mindfully, and it is still not enough. The choices are
right: the 4096-byte boundary, PSRAM for every frame and buffer, PSRAM stacks for every
task that can have one, the flash guard for the ones that cannot, dynamic TLS buffers,
the certificates freed after the handshake, the reduced Wi-Fi static buffers, the
descriptor chains allocated once. Each was measured and recorded when it was made. The
sum still leaves a 13 to 23 KB heap with an 11 to 16 KB largest block, and a low-water
mark of 1 KB, because two things were never measured against each other: the code the
SDK keeps in SRAM (83 KB, tunable by 26 KB in one experiment) and the transient peaks of
a second TLS session next to the persistent one.

M2. The MQTT-over-mTLS session is the largest variable consumer and the reason the peaks
exist. Its steady cost is about 17 to 20 KB of internal RAM (the 6 KB task stack, the
6 KB buffers, the client structure, the session state that mbedTLS keeps internal;
measured as the dispatcher's 20 KB share minus its baseline, plus the MQTT task's own
4 to 6 KB); its real cost is that every other HTTPS request the device makes (likes,
views, pages, the weather, the OTA check, pairing) is a second TLS handshake next to it,
and the handshake's internal peak is what drives the minimum to single digits. ADR 0009
accepted this budget for v1 with "verified before pairing ships"; it was verified at
25 to 30 KB free, which the review's target calls insufficient. The server's own
documentation (`reference/makapix/docs/http-api/player-rpc.md`, "Parity with MQTT")
names command polling or an SSE stream over HTTPS as the planned follow-up, so the
contract change has a home on both ends. Proposal P-M1.

M3. The always-internal threshold is set one step too high for this firmware.
At 4096, the 4 KB buffers of esp-mqtt and every `esp_http_client`, the small artworks,
the small canvases and the PEMs all land internal; at 1024 they go to PSRAM, where a
network buffer or a 3 KB GIF costs nothing measurable. Measured gain 8 to 13 KB
(experiment A). The hardware-tests lesson about newlib's stdio buffer moving to PSRAM
and breaking SDMMC writes does not recur: the card path uses POSIX reads and writes with
an explicit internal bounce buffer since M4. Proposal P-M2.

M4. Code that does not need to be in SRAM is in SRAM. Experiment B's 26 KB is the
measured part; a further 8 to 12 KB is available from the same family of options that
were not tried today (`CONFIG_HAL_ASSERTION` levels do not matter; candidates are the
`ESP_WIFI_SLP_IRAM_OPT` default (off already), `CONFIG_SPI_FLASH_ROM_IMPL` (uses the
ROM's flash driver instead of the 11 KB copy; needs the ROM to support the chip's flash,
which the S3's does for the WROOM-2's octal flash only with the IDF driver, so probably
not), the hub75 driver's `CONFIG_HUB75_IRAM_OPTIMIZATION` (2.2 KB; keep it, the copy is
the hot path), and the coredump stack (1.9 KB, keep it). Proposal P-M2 covers the
measured part; the rest is not worth its verification.

M5. The `merge_index` map is the wrong data structure for this heap. Both inputs are
vectors of 64-byte records in PSRAM; a sort by key and a two-pointer merge needs no
nodes at all, and the merge is host-tested already (`tests/host/main.cpp`), so the
change is safe. Proposal P-M3.

M6. A set of small internal residents that belong in PSRAM or nowhere: the second copy
of the three PEMs in `mqtt.cpp` (3 to 5 KB; pass pointers), the history's 32 x 7
`std::string` (5 to 8 KB; a fixed-width record in PSRAM), the two kept-alive HTTP
clients (11 KB; one client, since the fetcher serialises everything anyway), the
`physical_` staging frame inside `g_display` (12 KB of .bss; the driver reads it with
the CPU, not the DMA, and a PSRAM source costs a measurable but small amount on the
7.6 ms copy, to be measured), and `logring::tail()`'s 64 KB copy under a critical
section (correctness, not size). Proposal P-M3.

M7. Two rules are not enforced by anything. The flash guard wraps NVS in the settings,
state, credentials and PIN stores, but `wifi.cpp` reads and writes NVS unwrapped and is
safe only because its callers happen to run on internal stacks; and the "every network
task on core 0" rule is broken by the esp-mqtt task, which is unpinned because
`CONFIG_MQTT_TASK_CORE_SELECTION` is not set. Both are one-line fixes; the point is that
a host test or a boot-time assertion should hold them (proposal P-T6 in `testing.md`
adds the task table to the budget file).

M8. Streams cost 175 KB of PSRAM at boot even when both listeners are off, and about
2 KB of internal RAM. The PSRAM is irrelevant on 16 MB; the internal cost is not worth
a drop. If the feature is demoted to a compile-time option (proposal P-D1 in
`proposals.md`), the buffers should at least be allocated when a listener is enabled.

## 5. What each feature costs in internal RAM

Steady state, from the inventory and today's heap-by-task readings; "removing" means
removing the component and its tasks, not just disabling it.

| Feature | Internal RAM if removed | PSRAM | Verdict |
|---|---|---|---|
| Panel driver (10 planes, double buffered, Photo profile arrays) | about 110 KB DMA + 13 KB staging | 0 | the product; not a candidate. Eight planes in Quality would save 16 KB of row buffers and 5 KB of descriptors at a tonal cost the spec rejected |
| Wi-Fi, lwIP, mDNS, SNTP | 40 to 60 KB heap + 25 KB of driver code in SRAM (26 KB movable) | | required; the code placement is the lever (P-M2) |
| HTTP server, API, WebSocket push, live preview | 8 KB stack + about 10 KB of tables + 12 sockets | the push task's stack, all JSON, the preview | sacred; the WebSocket push could be replaced by polling to save one PSRAM task and nothing internal, so no |
| Makapix: fetcher, indexes, cache | about 17 KB (two HTTP clients, PEMs, jobs) | large | sacred; P-M3 halves the internal part |
| Makapix: MQTT session | 17 to 20 KB steady + the second-handshake peaks | some | the one large lever that is a design change (P-M1) |
| Widgets: clock, weather, temperature | about 1 KB + a transient TLS handshake per weather refresh | 16 KB of stacks | keep (used); after P-M1 the weather fetch is one of the two remaining transient TLS users |
| Streams | about 2 KB | 183 KB | nothing to gain by dropping; demote for maintenance reasons only (P-D1) |
| Inputs (IMU) | 1 to 2 KB | 4 KB stack | keep (used) |
| OTA | 0 at rest; 4 KB task + client buffers during a job | 12 KB stack during a job | sacred |
| PIN | under 1 KB | | sacred |
| Reliability, night schedule, RTC, factory reset | under 2 KB | | keep |
| Decoders (PNG, WebP as opposed to GIF only) | 0 at rest; canvases and library state are PSRAM by size (small ones internal until P-M2) | per artwork | dropping WebP and PNG would save 134 KB of flash and nothing internal; the site serves WebP, so no |

Answer to the question as asked: no single feature, if dropped, reduces internal RAM
consumption significantly except the MQTT session, and that one is replaced rather than
dropped. The significant reductions are configuration (26 KB + 8 to 13 KB, measured)
and code trims (est. 20 to 30 KB), with P-M1 removing the peaks.

## 6. The budget after the roadmap

Estimated steady state with MQTT connected (or its replacement idle), a browser on the
Home page, an artwork playing:

| Step | Free | Largest block | Basis |
|---|---|---|---|
| Today (committed build) | 20 to 23 KB | 16 KB | measured |
| P-M2 (configuration) | 54 to 58 KB | 32 KB | measured (experiment B, without the browser) |
| P-M3 (code trims) | 70 to 85 KB | 40 KB and more | est.: one HTTP client (5.6 KB), PEMs once (4 KB), history (6 KB), merge without nodes (no peak), staging frame (12 KB if the copy allows) |
| P-M1 (HTTPS commands instead of MQTT) | 85 to 100 KB | 48 KB and more | est.: the session's 17 to 20 KB plus no second handshake, so the minimum tracks the steady state |

The number to hold in `firmware/budgets.json` after P-M2 is 48 KB free and a 32 KB
largest block at steady state, and a minimum since boot of no less than 32 KB over a
24-hour soak; after P-M1 the minimum should sit within 10 KB of the steady state.
