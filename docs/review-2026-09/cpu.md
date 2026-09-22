# CPU discipline

Question asked: is the firmware mindful of the ESP32-S3's CPU, and would dropping any
feature free a significant amount of it?

Short answer: CPU is not the scarce resource. Measured on the device on 2026-09-22, core 0
(everything except playback) is 3 to 5 % busy at steady state and core 1 (playback) is
9 % busy with a small GIF and 44 % with a 128x128 WebP. No feature is worth dropping for
CPU. What the audit did find are four discipline faults (a wrong task priority, a lock
rule broken on the per-frame path, an unnecessary 250 Hz poll, and a busy-wait inside the
status document) and one structural limit (the 7.6 ms bit-plane copy per presented frame,
which is the driver's cost and bounds the frame rate, not the features').

## 1. Measurements

Taken with the instrumentation branch (`review/cpu-instrumentation`:
`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y` and per-task run time in
`GET /api/v1/diag/memory`; the sampling script takes two readings and divides the
per-task delta by the wall time). Paired device, MQTT connected, Followed playset, no
browser open, 30 s windows.

| Task | Core | Prio | Idle-ish (32x32 GIF at 5 fps) | Playing a 128x128 WebP at 10 fps |
|---|---|---|---|---|
| render | 1 | 20 | 4.8 % | 10.4 % |
| player | 1 | 15 | 4.0 % | 33.4 % |
| imu | 0 | 6 | 2.5 % | 3.4 % |
| wifi | 0 | 23 | 0.22 % | 0.28 % |
| stream | 0 | 9 | 0.20 % | 0.22 % |
| tiT (lwIP) | any | 18 | 0.16 % | 0.20 % |
| httpd (serving the sampler) | 0 | 5 | 0.13 % | 0.16 % |
| mqtt_task | any | 5 | 0.10 % | 0.12 % |
| loader, esp_timer, makapix, mdns, main, ws_push | 0 | | 0.01 to 0.07 % each | same |
| sensor, weather, sys_evt, events, ipc, Tmr Svc | 0 | | 0.00 % | 0.00 % |
| busy total, core 0 | | | 3.3 % | 4.7 % |
| busy total, core 1 | | | 8.8 % | 43.8 % |

The player's 33 % on the 128x128 WebP matches the renderer's own log line at the time
(decode 24.7 ms per frame at 10 fps) and the M5 benchmark (128x128 lossy WebP 28.7 ms).
The render task's 10 % at 10 fps is the 7.6 ms copy plus about 1 ms of boundary spin and
rotation per frame, as the code audit predicted (`evidence-cpu.md` section 2).

Recorded elsewhere and consistent with this: the M5 best-effort figures (128x128 APNG
28 fps with decode 16.5 ms, lossy WebP 27 fps with 36.6 ms), the 10 s renderer log line
(copy 7.60 to 7.64 ms on every sample today), and the soak runs (0 late flips over 45
minutes).

## 2. What the CPU goes to

Core 1 is playback only, by design, and its cost per presented frame is fixed: the
bit-plane copy (7.6 ms at ten planes, 5.8 ms at eight), the boundary spin (0.6 to
1.6 ms), the rotation and gains pass (0.3 ms), the preview copy (0.1 ms). At 60 fps that
is 55 % of the core before any decoding. Decoding a 64x64 GIF costs 1 to 3 ms per frame;
128x128 WebP or APNG 17 to 37 ms, which is why 128x128 stays best effort (spec 4.4). None
of this is feature overhead; it is the product. The one avoidable item is the 1 kHz
`vTaskDelay(1)` poll of the empty queue (about 0.3 % of core 1).

Core 0 carries everything else, and at steady state almost all of it is idle. Ranked by
measured share: the IMU sampler (2.5 to 3.4 %), then the IDF network baseline (Wi-Fi,
lwIP, the MQTT keepalive: under 1 % together), then everything p64 wrote (under 0.5 %
together). With a browser open the status document (1.5 to 3 ms per build, every 2 s and
on every event) and the 1 Hz preview PNG (2 to 3 ms/s) add 5 to 10 ms/s per page, so a
page costs about as much as the IMU. During a cache fill, each download costs 50 to
200 ms of core 0 (TLS, card write, index CRC and save) and 0.5 to 1.5 ms on the show loop
for the full index snapshot it triggers; that is transient.

## 3. Findings

F1. The show loop runs at priority 1, not 5. `architecture.md` section 2 says the main
task runs at 5; nothing sets it, so it keeps ESP-IDF's default of 1, below every p64 task
and every IDF task on core 0. A TLS handshake on the fetcher (priority 4) or the weather
task (3) runs to completion before the show loop gets the core. It has not shown up as a
symptom because core 0 is idle, but it inverts the documented design: the task that
answers the user (next, pause, a Makapix command) is the lowest priority task on the
device. Fix: set it (one line in main), and add the assertion to a device test.

F2. The player takes the settings mutex on every decoded frame, and `settings_update()`
holds that mutex across the NVS flash write. `overlay_key()` (widgets.cpp:369) copies the
settings under `system::g_mutex` for every frame the player produces (and every 100 ms
for a static picture); `settings_update()` (settings.cpp:446-456) holds the same mutex
while it serialises and writes NVS. A brightness command from the site or a settings PUT
therefore stalls the player for a flash write, on the one path the architecture promises
never blocks on core-0 work (section 2: "the two core-1 tasks never take a lock that a
core-0 task can hold for long"). Fix: the overlay hook reads an atomic snapshot (a
small POD struct updated on `SettingsChanged`), and `settings_update()` releases the mutex
before the write.

F3. The IMU is polled at 250 Hz from a task, which is the single largest CPU consumer on
core 0. The tap detector needs the rate for impulse shapes, but the QMI8658 has its own
tap and motion detection engines with an interrupt line, and the orientation tracker
needs only a few Hz. Options in order of cost: 100 Hz (halves the cost, shapes still
resolve at 10 ms), or the chip's tap interrupt plus a 10 Hz gravity read (removes the
poll altogether). Not urgent; core 0 has the room.

F4. The status document does a 200 us busy-wait under the driver mutex on every build.
`Display::health()` spins to see whether the DMA descriptor pointer moves
(display.cpp:462); the status builder calls it every 2 s while a client is connected and
on every event. It is 0.01 % of a core, but a busy-wait inside a document built for
telemetry is the wrong shape: the render task already knows whether the pointer moved
(it watches it every frame) and could publish a flag.

F5. Frequent, avoidable work on the download path. Every cached download fires
`MakapixChannelChanged`, and the show answers it by copying every entry of every Makapix
channel (up to 128 KB per channel) under the makapix mutex and rebuilding its pickable
list. During a cache fill that is one full snapshot per second. The index CRC32 is
bit-serial (eight iterations per byte, 5 to 10 ms for 128 KB) and runs at every save
(every eight downloads) and load. Both are transient and invisible today; they matter
only if the merge storm of `memory.md` is fixed by keeping more in internal RAM, since
they run on the same task.

F6. `system::settings()` is copied by value on hot paths: per frame in the widgets'
sources and the overlay hook, per delivered stream frame, twice per show-loop iteration,
and at 2 Hz in the fetcher's `online()` through `wifi::status()` (which also round-trips
to the Wi-Fi task for the RSSI). Heap-free only while every string member fits the
small-string optimisation; a time zone longer than 15 characters costs an internal
malloc and free per copy. A read-only snapshot pointer, swapped on change, removes both
the copies and the mutex.

F7. Task watchdog coverage is partial by design (player and render are excluded because a
slow artwork keeps them busy), but the event dispatcher, the fetcher, ws_push and the
weather task are also unwatched, and a hang in any of them is silent. The fetcher and the
weather task block in TLS for longer than the 10 s timeout, which is the stated reason;
a longer timeout for those two (or a per-task "I am in TLS" grace) would let them be
watched.

## 4. Would dropping a feature free significant CPU?

| Feature | Core 0 | Core 1 | Verdict |
|---|---|---|---|
| IMU (taps, auto-rotation) | 2.5 to 3.4 % | 0 | the only p64 feature with a visible share; the user values it; fix the poll rate instead (F3) |
| Streams | 0.2 % idle (a 4 Hz select); up to 60 Hz of frame conversion when a stream runs | frames at the panel rate | nothing at idle; irrelevant for CPU |
| Weather, temperature | 0 % between fetches; a TLS handshake per refresh interval | 0 | nothing |
| Clock, overlay | 0 | 0.02 to 0.04 ms per frame | nothing |
| Makapix MQTT | 0.1 % | 0 | nothing for CPU |
| WebSocket push, live preview | 5 to 10 ms/s per open page | a 12 KB copy per presented frame | only while a browser is open; nothing to drop |
| Photo mode | 0 | saves 1.8 ms per frame when selected | already a user choice |

No. The firmware is CPU-disciplined where it matters (core 1 does nothing but playback,
core 0 is idle, no floats in per-pixel loops, no per-frame allocation, frame pacing
locked to the DMA), and the faults are correctness-of-design items, not load.

## 5. What to keep from this review

- Keep the run-time statistics on: the per-task run time in `diag/memory` costs nothing
  measurable and turns "is the device busy" into a number. `CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64`
  avoids the 32-bit wrap at about 71 minutes; the sampler takes deltas, so the wrap only
  matters for absolute values.
- Record the two samples above as the CPU baseline in `firmware/README.md` and let
  `soak.py` assert that core 0's busy share stays under 10 % with the browser poll on.
