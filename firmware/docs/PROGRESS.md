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
| M1 | Display layer ported (pacing, rotation, gains, brightness, panel modes) + GIF from the card in the new player | pending | |
| M2 | PNG/APNG, WebP, BMP decoders; format sniffing; decode benchmark; no-drop timeline | pending | |
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
- Next: M1, the decoder interface and GIF decoder (`p64_decode`), the player and frame
  queue (`p64_playback`), card mount (`p64_storage`), and a first show of card GIFs.
