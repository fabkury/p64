# Steps 3 and 4 executed (2026-09-22, prompt p024)

What the session after tier 1 did from `proposals.md`: step 3 (doctest, GitHub Actions)
in full, and the first part of step 4 (pure cores). Decisions the user gave for it
(asked with the tool): step 3 then as much of step 4 as fits; push main when CI is
ready; fix the eight warnings and make p64 warnings errors; vendor doctest and migrate
every test.

## Commits

| Commit | Proposal | Content |
|---|---|---|
| `715a120` | P-T3 | doctest 2.4.12 vendored; the 24 hand-rolled tests became named cases in `tests/host/unit/` (one file per component); `run.py --tc`, `--junit`, `--werror`, `--sanitize` |
| `667fe58` | P-T4 | `-Werror` for `extra`, `unused-*` and `deprecated-declarations` in every p64 component and `main` (`firmware/CMakeLists.txt`); `Job` got a constructor, ending the eight `-Wmissing-field-initializers` warnings |
| `8353c8b`, `75067c0` | P-T4 | `.github/workflows/firmware.yml`: host tests with ASan, UBSan and `-Werror` in `espressif/idf:v5.5.4`; the firmware build, `idf.py size` and `tools/check_size.py`; `p64.bin` with its SHA256 as artifacts |
| `fd3ac17` | P-T1 | `p64/playback/timing.hpp`: the player's `Timeline` and the renderer's `Schedule` as pure classes; eight host cases; `tests/device/timing_smoke.py` |
| `6aca7b9` | P-T2 | `p64_system/src/settings_model.cpp`: the settings document's clamps, enums and JSON; six host cases |
| `e746fe3` | P-T2 | the six files that were already pure but never compiled on the host (`png_encode`, `tz`, `local_index`, `artwork`, `status_screens`, `boot_animation`), with five cases |
| `57d38dc` | P-T2 | `p64_makapix/src/contract.cpp`: the server's post and page documents and the download URL; four cases |
| `b7c9ac2` | P-T1 | `main/show_rules.cpp`: the show's decisions; nine cases replaying the bug scenarios |

## Numbers

| | Before (morning) | After |
|---|---|---|
| Host test cases | 24 (one unnamed binary) | 56 named doctest cases |
| Host assertions | 1 433 | 22 180 |
| p64 source files compiled on the host | 26 | 36 |
| Fixed bugs of `evidence-testing.md` with a test | 9 of 24 | 15 of 24 |
| CI | none | two jobs on every push to main, both green |
| Warnings in p64 code | 8 (not fatal) | 0, and fatal |

The fixed bugs that gained a test: the renderer's two pacing bugs (M1, M5), the one-file
cache replay (M6), the out-of-range settings wrapping (M4, now every key), the frozen
artwork's timer gate (2026-09-21, now also on the host), and the stream parking.

## Found by the new tests

- The renderer's copy-lead running average truncated and settled a few microseconds
  below the copy time; when the refresh grid fell worst the flip landed just past one
  period after its target and the schedule slipped a period, about once in 1 760 frames
  at 25 fps. The average now rounds up (`timing.hpp`). Invisible on the panel, but it is
  exactly the kind of rule the M5 drift lived in.
- UBSan found misaligned 16-bit loads in AnimatedGIF, but only in its 64-bit host path;
  the ESP32-S3 build uses byte-wise macros. The alignment check is off for vendored code.
  The host tests therefore run a different AnimatedGIF path from the device for those
  header fields; a 32-bit host build (`-m32`) would close that gap.
- A passed-through POSIX time zone rule may not contain '/', so rules with transition
  times ("CET-1CEST,M3.5.0,M10.5.0/3") are refused. IANA names cover those zones;
  recorded in the test, not changed.
- `widgets_smoke.py` assumed an artwork was up; right after a flash the playset may still
  be activating. It now waits for one.

## Device verification

On the flashed builds, against the IP: `timing_smoke` (a 40 ms GIF at 24.93 fps, a 10 ms
GIF at 10.04 fps, a 16 ms APNG at 60.01 fps, no late frame), `makapix_smoke --paired`,
`widgets_smoke`, `stream_smoke`, `content_smoke`, `api_smoke` (floors held: 54 KB free,
45 KB largest block), `ops_smoke`. All pass.

## What remains of step 4

The show core is a first slice: the decisions are pure and tested, while the command
and event handling (`handle`, `install`, `on_loaded`, navigation, history resume) still
lives in `show.cpp` with its globals. Still to do, in the order of the untested bugs
they would cover:

1. The fetcher's policy (`refresh_step`, `finish_walk`, the 60 s walk restart, the
   offline Followed job, the download order) over a fake API and cache: bugs 5, 13, 14.
2. The rest of the show core (commands and events in, effects out): the stale resume
   after a status screen (bug 7), the widget redraw on a settings change (bug 11).
3. The web layer's "(method, path, body) to (status, JSON)" core, and a device test that
   holds the WebSocket open (bug 10, the M10 crash loop): proposal P-T5.
4. The cheap seams not yet taken: the OTA release parser, the cache sweep on a temporary
   folder, the digital clock, weather and temperature drawing, the PIN lockout, the MQTT
   payload builders, the driver's LUT fit (bug 12, the Photo-mode collapse).

Then step 5 (P-M1, the HTTPS command channel) as planned.
