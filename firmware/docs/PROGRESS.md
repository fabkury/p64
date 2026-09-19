# p64 firmware: progress log

Read this first when resuming. Newest entry at the bottom of "Log"; the milestone table
shows where things stand. Spec: `docs/spec/p64-spec.md`. Design: `architecture.md`.

## How to resume

1. Read `architecture.md` and the milestone table below.
2. Build and flash from `firmware/` in PowerShell 7: `.\tools\build.ps1`,
   `.\tools\flash.ps1`, `.\tools\monitor.ps1` (or `tools\serial_peek.py` for a
   non-interactive read of the console; the board is COM13 on this laptop).
3. Check the device: `http://p64.local/api/v1/status` once the web layer exists; the
   hardware-tests firmware answered `/status` and `/debug` before that.
4. Continue with the first unchecked item of the current milestone. Commit per step.

## Milestones

| # | Milestone | Status | Verified on device |
|---|---|---|---|
| M0 | Project skeleton: builds, boots, console, boot animation on the panel, tools | done | 2026-09-19: boot log clean, panel frame-locked at 271.3 Hz, boot animation 2010 ms, idle 66.7 fps, 0 late flips |
| M1 | Display layer ported (pacing, rotation, gains, brightness, panel modes) + GIF from the card in the new player | done (panel modes untested) | 2026-09-19: card GIFs play through player + renderer at their stored delays, late 0, decode 1.1-1.5 ms, copy 7.5 ms, swap drops the old queued frame |
| M2 | PNG/APNG, WebP, BMP decoders; format sniffing; decode benchmark; no-drop timeline | done except the on-device benchmark (needs files on the card: M4 upload) | 2026-09-19: host tests 86 files / 1531 frames exact; device builds and plays GIFs unchanged |
| M3 | Storage layout, settings store, Wi-Fi manager with setup mode, mDNS, time | pending | |
| M4 | HTTP API v1, WebSocket push, live preview, minimal web UI | pending | |
| M5 | Content: local channels, playsets, scheduler, history, auto-swap, play-this | pending | |
| M6 | Makapix: promoted anonymous, pairing, MQTT commands, downloads, views, likes | pending | |
| M7 | Widgets: fonts pipeline, clock overlay, clock, weather, temperature, interludes | pending | |
| M8 | Streams: DDP, raw UDP, takeover | pending | |
| M9 | IMU, night schedule, PIN, OTA, coredump, diagnostics, factory reset | pending | |
| M10 | Full web UI port, acceptance tests, docs | pending | |

## Log

### 2026-09-19

- Product spec, glossary and ADRs settled and committed (commit 47fdee5).
- Quick device check for the user: COM13 console readable without reset (pyserial,
  DTR/RTS low), hardware-tests web endpoints answered at 192.168.20.58, panel 271 Hz,
  card mounted. The user left; questions go to their phone.
- Decision while porting: the hub75 driver's bit depth is compile-time, so Photo mode
  is implemented as a driver re-creation with `min_refresh_rate` 600 (transition bit 6,
  698 Hz), see `architecture.md` section 4.
- M0 started: `architecture.md`, this file, vendored `hub75` and `animatedgif` copied
  from the hardware tests, tool scripts copied, dev Wi-Fi seed in `sdkconfig.secrets`
  (git-ignored).
- M0 verified on the device (first flash of the new firmware): `display` reports the
  driver at 271.3 Hz with frame boundaries read from GDMA channel 0; the boot animation
  ran 2010 ms; the idle pattern presents at 66.7 fps with render 6.7 ms (float maths, not
  representative), copy 7.4 ms, wait 0.8 ms, 0 late flips, 0 timeouts. Internal heap at
  boot: 297 KB + 21 KB + 32 KB DRAM. The core dump partition logs "incorrect size" once
  because it was never written; harmless.
- M1 done: `p64_decode` (Decoder interface, sniffing, browser delay rule, GIF decoder
  with background colour), `p64_storage` (card mount, p64 layout created, listing,
  reads), `p64_playback` (FrameQueue of 3 slots with generations, Artwork, Player task
  on core 1 at priority 15, Renderer task at 20). main plays random card GIFs every
  30 s. Two pacing bugs found and fixed on the device: (1) the minimum stay was
  measured from the end of the 7.5 ms copy, so every frame slipped by the copy time
  (10 fps instead of 20); the renderer now starts the copy a measured lead ahead of
  the target and keeps the schedule on target times; (2) lateness was measured against
  the player's timeline anchored at the first decode, unattainable by the copy time;
  it is now measured against the renderer's own schedule, which starts at the first
  frame's visibility. Result: late 0, fps equal to the stored delays (8 fps for
  125 ms frames, 1.9 fps for 500 ms). The first 10 s window shows a higher fps because
  the 60 fps boot animation frames are counted in it.
- Panel mode switching (`Display::set_mode`) is implemented but not yet exercised; it
  gets its test with the API (M4). Rotation is fixed at 90 until the settings store.
- Host tests in `tests/host/` (run.py + main.cpp): 74 unit checks, GIF corpus exact.
- M2: PNG/APNG (components/libpng, p3a's APNG-patched libpng 1.6.52 fork, pruned;
  zlib from the registry), WebP (components/libwebp, decoder subset of v1.4.0 vendored),
  BMP (ported from p3a), all behind `Decoder`; alpha flattened over the background in
  gamma space (`alpha.hpp`). A Pillow-made corpus of 22 PNG/APNG/WebP/BMP/GIF files
  lives in `tests/host/corpus/`; the runner now compiles zlib, libpng and libwebp on
  the PC. Finding: Pillow's APNG reader pastes OVER-blended sub-frames with their alpha
  as a mask (halving colour and alpha over transparent areas) instead of a true OVER,
  so the tests use a spec compositor (`apng_reference_frames`) as the APNG oracle; our
  decoder agrees with the spec. All 86 files exact.
- Device: builds with all decoders, plays card GIFs unchanged. Internal heap at boot
  fell from 297 KB to 258 KB with the libraries linked (see size report below when
  taken). The on-device decode benchmark waits for a way to put PNG/WebP files on the
  card (M4 file manager).
