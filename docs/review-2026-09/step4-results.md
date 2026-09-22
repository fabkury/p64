# Step 4 completed (2026-09-22, prompt p025)

The rest of step 4 of `proposals.md`: the pure cores and the cheap seams, after the first
slice recorded in `steps3-4-results.md`. Decisions the user gave for it (asked with the
tool): the order fetcher, show core, WebSocket test, seams; the show core in full, with
events in and effects out; push main freely. The work waited for another session to
finish in the same checkout, at the user's choice.

## What moved, and what now tests it

| Where | What is pure now | Host cases |
|---|---|---|
| `p64_makapix/src/policy.cpp` | the channel to serve, the refresh after a load, the walk restart after 60 s, when a page ends a walk and when the first pages install at once, the retry backoff, the round-robin download order, the offline job (run, park, fail), the sweep rule | 9 |
| `main/show_core.cpp` + `show_core.hpp` | the whole show: playsets and channels, picking and preparing, history, auto-swap, pause, play-this, status screens, the Widget and Stream states, the takeover, the status JSON; the world through one interface, `ShowEnv`; the state in one `State` | 9 scenarios through a fake `ShowEnv` |
| `p64_ota/src/release.cpp` | GitHub's release document, the checksum file | 4 |
| `p64_web/src/auth_rules.cpp` | a valid PIN, the lockout, the session table | 4 |
| `p64_makapix/src/contract.cpp` (extended) | the site's commands (every type, the acknowledgements), the status, state, capabilities, view and ack payloads | 4 |
| `p64_widgets/src/faces.cpp` | the digital clock, the weather, the temperature, drawn from the settings, the time and the data | 6 |
| `esp-hub75/src/platforms/gdma/p64_bcm.h` | the plane windows of a refresh profile and the LUT fit | 6, one of them a sweep of every profile from 6 to 10 planes, every transition bit and every brightness |

Plus `tests/device/ws_smoke.py`, which holds the WebSocket status push open with two
clients (the path of the M10 crash loop, which no test reached).

## Numbers

| | After steps 3 and part of 4 | Now |
|---|---|---|
| Host test cases | 56 | 98 |
| Host assertions | 22 180 | 128 079 |
| p64 source files compiled on the host | 36 | 40 plus two headers in the driver and the widgets |
| Fixed bugs of `evidence-testing.md` with a test | 15 of 24 | 21 of 24 |

The bugs that gained a test here: the walk that installed nothing until its last page,
the walk resumed on a dead connection, the lost Followed playset, the stale resume after
a status screen, the widget not redrawn on a settings change, the WebSocket push crash
loop, the weather strip clipped at the bottom row, the Photo-mode LUT collapse. Still
without one: the four M3 network bugs, the late boot animation at 60 fps, esp-mqtt
started twice (integration and hardware behaviour that only the device shows).

Two scenario tests were checked the hard way: undoing the stale-resume guard or the
widget redraw in the core makes its scenario fail.

## Found by the new tests

- **The LUT fit's fix of 2026-09-20 was incomplete.** Blanking the plane that would
  break the superincreasing weights kept the weights in order, but the fit still walked
  every code in order and stopped at the first code made of the blanked plane alone
  (weight 0), so ten planes at transition bit 6 still fitted 3 codes. The fit now walks
  only the codes built from lit planes. Quality and Photo never blank a plane at any
  brightness, so their tables are unchanged (229 and 179 codes at full brightness, in
  the test and in the driver's own log on the device); the bug was latent.
- **The cache sweep deleted files a little in the future.** A file whose mtime was up to
  a day ahead of the clock (a clock stepped back by NTP, FAT's two-second rounding right
  after the loader's touch) was not "implausible", and the unsigned age wrapped to about
  136 years, so it was deleted as ancient. It now counts as recent.
- **A misleading comment**: the command handler said a p3a `sdcard` channel was dropped;
  the content model maps it to this device's card (Local) on purpose. The test pins the
  behaviour and the comment now says so.
- **The time zone passthrough refuses rules with '/'** (recorded in the previous record,
  still not changed).

The device tests needed fixes of their own, all assumptions about the device's state
rather than firmware faults: `content_smoke` assumed Promoted had nothing cached, a
history with room past position 31, history names equal to artwork names, and no
"connected" screen right after a boot; `makapix_smoke` read the previous playset's
artwork during the seamless swap; `panel_mode_smoke` took its first heap reading while
the MQTT handshake was still running after a flash.

## Device verification

On the flashed builds, against the IP: `content`, `widgets`, `stream`, `timing`, `api`,
`ops`, `ui`, `makapix --paired`, `pin`, `panel_mode`, `cache_sweep` (dry run) and the new
`ws_smoke` pass. The boot log showed the parked Followed job run once online (parked at
1.7 s, activated at 7.8 s). The 45-minute soak on the final build, Promoted at 5 s swaps with the status and preview polled every second: 522 swaps, 33 153 frames, 0 late flips, 0 timeouts, no stall, no reboot, internal heap floor 54 163 B (typical 70 to 74 KB), core 0 busy 8.4 %, 0 poll errors, against 59 243 B and 7.8 % after tier 1: the restructuring cost nothing measurable.

## What remains

Step 4 is complete. The next step of the roadmap is P-M1 (the HTTPS command channel in
place of MQTT over mTLS, on the server and the device), then P-M3 (the memory trims),
P-T5 (the device test harness with snapshot and restore; this session showed why: four
tests assumed a device state), P-C2 (the IMU rate), P-D1 (streams as an option).
