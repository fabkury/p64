# Project status

[Home](../README.md) · [Build guide](build-your-own.md) · [Owner's guide](using-p64.md)

Documentation reviewed 2026-10-15, updated 2026-10-19. Physical status was confirmed by
the maintainer on 2026-10-15; test results below are existing records, not a new
acceptance run. The development device runs firmware 0.2.0-dev (the private build,
`0.2.0-dev+private`, flashed 2026-10-19 before the version became 0.2.0). The release tooling exists since 2026-10-19
(tag-driven CI release, web flasher, command-line guide); the first release, v0.2.0, is
published once its tag is pushed, so check [GitHub Releases](https://github.com/fabkury/p64/releases).

| Area | Current state | Evidence or next step |
|---|---|---|
| p64a | v7a assembled and in daily use; no soldering | Maintainer confirmation, 2026-10-15 |
| p64b | v7b assembled 2026-10-17 and in daily use: pull-ups soldered on board 1, harness mated, both boards on the pegs, panel seated, knobs working through the closed shell; turn direction reversed to match the hand on 2026-10-19 (0.2.0-dev), confirmed by the maintainer | `encoders_smoke.py` 0 failures with the shell closed, both boards polled with 0 read errors; [bench record](hardware/encoder-history.md) |
| Firmware | 0.2.0: core product features implemented, including encoders, Air, 13 clock faces, PIN and OTA | [Specification](spec/p64-spec.md), [device and host test history](../firmware/docs/PROGRESS.md) |
| Installation | Pre-built releases flash from the [web flasher](https://fabkury.github.io/p64/web-flasher/) or with [one esptool command](flash-firmware.md); building from source stays documented for developers | The release process and the audit of what the image carries: [release build](../firmware/docs/release-build.md). Not yet verified on hardware: the esptool command and the browser flasher against a real board (the board was not on USB on 2026-10-19); the first field OTA from v0.2.0 to its successor |
| Automated checks | CI builds the public firmware, checks size budgets, and runs host tests with sanitizers | [Workflow](../.github/workflows/firmware.yml); device tests require hardware |
| Acceptance | Incomplete | See the outstanding checks below |

## Outstanding checks

The latest [firmware log](../firmware/docs/PROGRESS.md#log) still leaves these open:

- Instrument measurements: sampled high-speed video for seamless changes and Photo
  mode, power at full white, and stream latency against the specification.
- Full 12-hour seamless-playback and 24-hour reliability acceptance soaks.
- Hands-on checks of shell taps, rotation direction, BOOT hold and the Photo-mode picture.
- Final visual checks and longer runs for the newer clock faces.
- Remaining v7 dimensional checks in
  the [enclosure reference](../enclosure/design-reference.md#things-that-could-not-be-verified),
  and the encoder washer and nut diameters (they seat in the 14 mm spot-face, unmeasured).
- Makapix views refused with HTTP 429 are retried at the next swap, one TLS session each
  when MQTT is down; honour the server's Retry-After and pause the views instead (found
  by the 2026-10-17 soak, which failed its core 0 budget on that alone: Makapix task 31 %).
- `soak.py` should print the Makapix state (MQTT, last error, views sent) with its
  verdict, so a server-side refusal is readable from the report.
- While pairing, the device polls the server for its credentials often enough to draw
  "HTTP 429 Too many credential requests" (25 s of them on 2026-10-17); space the polls
  to what the server allows.

These are verification gaps, not a claim that the features fail. Shorter successful
tests do not close longer acceptance requirements.

## Known limits

- Demanding animations slow down rather than skip frames. The September benchmark
  measured 35–45 fps for its 128×128 animated samples.
- Provider downloads can reduce a 16 ms APNG to about 55–57 fps. The 2026-10-04
  timing test passed once in four runs; the log records both timing-sample uncertainty
  and the known late-frame limit. Timing acceptance is not fully closed.
- A cold boot needs NTP before clocks, night scheduling and online refreshes work.
  Cached artwork on a card can play before Wi-Fi or time synchronization.
- Cardless playback uses memory for downloaded artwork; it does not provide durable
  artwork storage. See [offline and cardless use](using-p64.md#offline-and-cardless-use).

## Keeping this page current

Record implementation, test evidence, physical assembly and release availability
separately. Link dated evidence when closing a check. The homepage carries only a short
summary; historical logs retain the results and limitations that applied at the time.
