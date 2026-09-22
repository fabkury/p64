# Tier 1 executed (2026-09-22, prompt p022)

What was done from `proposals.md` tier 1, what was measured, and what was decided on
the way. Decisions the user gave for it (asked with the tool): the full verification
list before keeping P-M2; the show loop set to 5 as documented; run-time statistics
always on and heap tracking left as an ESP-IDF option for diagnosis builds; the budgets
as absolute floors that fail the tests.

## Commits

| Commit | Proposal | Content |
|---|---|---|
| `abfe4e1` | P-T6 | the instrumentation branch cherry-picked: per-task `run_time` and `core`, and `heap_by_task` behind `CONFIG_HEAP_TASK_TRACKING`, in `GET /api/v1/diag/memory` |
| `f51eb8f` | P-T7 | the written rule in CLAUDE.md (tests travel with the code; the budgets file) |
| `3f05d31` | P-C1, P-M7 | the show loop at priority 5; `settings_view()` for the per-frame paths and the NVS write outside the settings mutex; `Display::health()` without the 200 us busy-wait; `wifi.cpp` NVS through the flash guard |
| `94faea2` | P-T6 | `budgets.json`, `tools/cpu_sample.py`, `tools/check_size.py`; `api_smoke.py` and `soak.py` check the floors |
| `55c5c30` | P-M2, P-M7 | `sdkconfig.defaults`: the five internal-RAM lines, the MQTT task on core 0, run-time statistics |
| `b3d9a62`, `dc70ea4` | | the architecture and README updates, this record; `check_size.py` on the venv's legacy size JSON |

## Measurements

All on the paired development device, Followed playset, MQTT connected, no browser.
"Committed" is the build of the morning (commit 0d0507f); "P-M2" adds the five
configuration lines; "tier 1" adds the code changes.

| Build | Internal free | Largest block | Minimum since boot | Static DIRAM | Core 0 busy |
|---|---|---|---|---|---|
| Committed | 13 to 23 KB | 11.7 to 20 KB | 939 B to 7.8 KB | 150 127 B | 3.3 % |
| P-M2 | 71 to 80 KB | 45 to 47 KB | 9.7 KB (first boot after the flash), 69 KB (after the OTA reboot) | 123 951 B | 4.7 % |
| Tier 1 (P-M2 + code) | 74.7 to 80 KB | 45 to 47 KB | 50.5 to 60 KB | 120 083 B | 3.9 % idle; 7.8 % over the soak with the browser poll |
| Variant C (tier 1 with the two Wi-Fi IRAM options back on) | 57.3 KB | 32.8 KB | 31.8 KB | 138 099 B | |

The P-M2 figures are higher than the review's experiment B (54 to 58 KB) because the
experiment carried heap task tracking (about 4 KB) and a fuller cache. The minimum since
boot still dips on some boots (9.7 KB on the first boot after the flash, when the cache
was re-walked and the OTA check, the weather fetch and the MQTT handshake overlapped);
on the next boot it stayed at 69 KB, and on the tier-1 boot at 50 KB. The dip is the
second-handshake peak of `memory.md` M2, which P-M1 removes.

Verification of P-M2 (the list from `proposals.md`):

| Check | Committed | P-M2 | Verdict |
|---|---|---|---|
| Wi-Fi throughput through the device's HTTP server, a 4.27 MB GIF to and from the card, best of three | RX 3.71 Mbit/s, TX 3.40 Mbit/s | RX 4.90 Mbit/s, TX 3.31 Mbit/s | not worse (card-bound both times) |
| `api_smoke.py` | 0 failures (old 20 KB floor) | 0 failures against the new floors (48 KB free, 32 KB largest) | ok |
| `panel_mode_smoke.py` | | 12 switches, 813.8 / 271.3 Hz, 0 timeouts, largest block 45 056 B unchanged | ok |
| `ota_smoke.py` | | install from the PC over HTTP in 21 s, boot from `ota_1`, confirmation, rollback to `ota_0` | ok (the kernel in flash coexists with the flash writes) |
| `stream_smoke.py` | | see below | ok |
| `soak.py --minutes 45` with the browser poll (Promoted at 5 s swaps, status and preview polled every second) | 45 min on 2026-09-19: 540 swaps, 0 late flips, floor 13.8 KB | 529 swaps, 32 619 frames, 0 late flips, 0 timeouts, no stall, no reboot, heap floor 59 243 B (typical 64 to 70 KB), core 0 busy 7.8 % with the poll on, 0 poll errors | ok |

The stream test needed attribution. Two consecutive runs on the P-M2 build lost most of
the 128x128 bursts (4 of 10 frames counted, 21 and 51 incomplete frames) with the RSSI
at -70 dBm, down from -53 dBm in the morning; the tier-1 build with the same Wi-Fi
configuration then completed every frame (0 incomplete, all pixel-exact), and variant C
with the two Wi-Fi IRAM options restored did the same twice, at -68 dBm. So the loss was
the radio environment of that half hour, not the code placement, and the two options
stay off (they cost 14 KB of static DIRAM: variant C's 138 099 B against 123 951 B, and
17 KB of free heap at steady state). One check kept failing on every build today, "30 fps
stream measured at 0.0 fps" with zero incomplete frames: the first HTTP request of each
test process spends up to 3 s resolving `p64.local` (measured: 2.91 s, then 0.1 s), so
the status read after the two-second run lands after the stream has gone silent and the
fps field has been zeroed. Against the IP the same test passes with 29.7 fps. Written into
`firmware/README.md`; the harness of P-T5 should resolve the name once.

## Findings on the way

- The `merge_index` and the second-handshake peaks are unchanged by tier 1 (they are
  P-M3 and P-M1); the boot-time minimum still varies between 10 and 70 KB from boot to
  boot.
- `CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64` keeps the run-time counters from wrapping
  at 71 minutes; `cpu_sample.py` takes deltas anyway.
- With heap tracking off, the tier-1 build's steady state is 75 KB free; the budget
  floors (48 KB free, 32 KB largest) leave room for a browser session and a transient
  TLS session.
- `main` now reports priority 5 in `diag/memory`; its CPU share is unchanged (0.02 %).
