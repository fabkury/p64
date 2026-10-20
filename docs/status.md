# Project status

[Home](../README.md) · [Build guide](build-your-own.md) · [Owner's guide](using-p64.md)

Documentation reviewed 2026-10-15, updated 2026-10-20. Both p64 variants are finished as
designed and work well: the maintainer's two devices, one p64a and one p64b, run
essentially around the clock (reported 2026-10-20). The firmware is at version 0.2.1 and in
good working order, and stays under development for an unspecified time: new faces,
widgets and refinements keep arriving, and each release is announced on the
[releases page](https://github.com/fabkury/p64/releases). The latest release,
[v0.2.1](https://github.com/fabkury/p64/releases/tag/v0.2.1) (the intro animations), was
published by CI on 2026-10-20 after [v0.2.0](https://github.com/fabkury/p64/releases/tag/v0.2.0)
on 2026-10-19, and the [web flasher](https://fabkury.github.io/p64/web-flasher/) lists both.
The development device runs the private build of the same version (`0.2.1+private`).

| Area | Current state | Evidence or next step |
|---|---|---|
| p64a | v7a, finished as designed; assembled 2026-10-09, in continuous use since; no soldering. The shell source changed in place on 2026-10-20: the rim is 0.5 mm taller (mask 0.5 mm proud instead of 1.0), not printed yet | Maintainer confirmation, 2026-10-15 and 2026-10-20 |
| p64b | v7b, finished as designed; assembled 2026-10-17, in continuous use since: pull-ups soldered on board 1, harness mated, both boards on the pegs, panel seated, knobs working through the closed shell; turn direction matched to the hand on 2026-10-19 (0.2.0-dev), confirmed by the maintainer. Same 0.5 mm rim change as p64a on 2026-10-20, not printed yet | `encoders_smoke.py` 0 failures with the shell closed, both boards polled with 0 read errors; [bench record](hardware/encoder-history.md) |
| Firmware | 0.2.1, working and under continued development: the product features of the specification are implemented, including encoders, Air, 13 clock faces, 21 intro animations (2026-10-20), PIN and OTA | [Specification](spec/p64-spec.md), [device and host test history](../firmware/docs/PROGRESS.md) |
| Installation | v0.2.1 published 2026-10-20 (v0.2.0 on 2026-10-19); pre-built releases flash from the [web flasher](https://fabkury.github.io/p64/web-flasher/) or with [one esptool command](flash-firmware.md); building from source stays documented for developers | Verified 2026-10-19 on the development board: the published v0.2.0 files flashed with the guide's esptool command over native USB (21 s, self-reset, booted 0.2.0, Wi-Fi settings kept), and the device's update check found the release and its checksum (`up_to_date`). The first field OTA was exercised on 2026-10-20: the development device (on the private 0.2.0 build) found v0.2.1 on its Update page, downloaded and verified the 2.5 MB image in 54 s, booted it from the other slot, confirmed it and played a boot animation; it was then rolled back and reflashed with the private build of 0.2.1. Not yet exercised: a flash through the browser page (it needs a hand on the port picker). Process and image audit: [release build](../firmware/docs/release-build.md) |
| Automated checks | CI builds the public firmware, checks size budgets, and runs host tests with sanitizers | [Workflow](../.github/workflows/firmware.yml); device tests require hardware |
| Reliability | Proven in use: two devices running continuously since their assembly (2026-10-09 and 2026-10-17) without issues, through Makapix refreshes, downloads, clocks and widgets | Maintainer's report, 2026-10-20; the unattended [soak harness](../firmware/docs/PROGRESS.md#log) passed every run up to 55 minutes |

## Remaining checks

Measurements the specification asks for that need instruments or a deliberate session, and
that field use does not stand in for:

- Instrument measurements: sampled high-speed video for seamless changes and Photo
  mode, power at full white, and stream latency against the specification.
- Hands-on checks not yet recorded: shell taps, BOOT hold and the Photo-mode picture.
  (The knob direction was checked on 2026-10-19.)
- Remaining v7 dimensional checks in
  the [enclosure reference](../enclosure/design-reference.md#things-that-could-not-be-verified),
  and the encoder washer and nut diameters (they seat in the 14 mm spot-face, unmeasured).

The formal 12-hour and 24-hour `soak.py` runs of the acceptance list (spec 18.1 and 18.11)
were never run as such; continuous operation of two devices for weeks covers what those
runs were meant to show, so they are not tracked as open work.

## Engineering notes

Observations from the bench that are worth a change some day. None of them affects a
device in normal use:

- Makapix views refused with HTTP 429 are retried at the next swap, one TLS session each
  when MQTT is down; honouring the server's Retry-After and pausing the views would be
  cheaper. Seen only once, in the 2026-10-17 soak, after the server had locked the device
  out and before it was re-paired (Makapix task 31 % of core 0 during that state).
- While pairing, the device polls the server for its credentials often enough to draw
  "HTTP 429 Too many credential requests" (25 s of them on 2026-10-17); the polls could be
  spaced to what the server allows. Pairing succeeded anyway.
- `soak.py` could print the Makapix state (MQTT, last error, views sent) with its
  verdict, so a server-side refusal is readable from the report.
- The newer clock faces (the Retro Diffusion faces and the painted dials, 2026-10-03)
  have had less time on the panel than the rest; the maintainer's eye is the remaining
  review.
- The 21 intro animations (2026-10-20, chosen from 26 on the panel the same day) have
  had little time on the panel yet; a random one plays at each boot.

## Known limits

- Demanding animations slow down rather than skip frames. The September benchmark
  measured 35–45 fps for its 128×128 animated samples.
- Provider downloads can reduce a 16 ms APNG to about 55–57 fps while a download is in
  flight; the 2026-10-04 timing test records the late-frame limit and the uncertainty of
  its own fps samples. This is a known limit of the hardware budget, not a defect.
- A cold boot needs NTP before clocks, night scheduling and online refreshes work.
  Cached artwork on a card can play before Wi-Fi or time synchronization.
- Cardless playback uses memory for downloaded artwork; it does not provide durable
  artwork storage. See [offline and cardless use](using-p64.md#offline-and-cardless-use).

## Keeping this page current

Record implementation, test evidence, physical assembly and release availability
separately. Link dated evidence when closing a check, and move an observation to the
engineering notes when it describes an improvement rather than a gap. The homepage
carries only a short summary; historical logs retain the results and limitations that
applied at the time.
