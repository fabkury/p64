# Test catalogue: everything else, by area

[Strategy](README.md) · [Findings](findings.md) · 2026-11-11

Cardless operation and the web under abuse have their own chapters
([no-card-mode.md](no-card-mode.md), [web-ui-and-api.md](web-ui-and-api.md)). This one
covers the rest of the product against the 1.0.0 bar. For each area: what is tested
today, what is not, and the tests to add. The "how" column names the lane:

- **host**: `tests/host/`, in CI under the sanitizers;
- **device**: a script against the test unit, no special firmware;
- **hook**: needs the test build's fault injection ([infrastructure.md](infrastructure.md#test-hooks));
- **fake**: needs a fake server on the test PC ([infrastructure.md](infrastructure.md#fake-servers));
- **usb**: uses the USB connection (esptool, the serial console);
- **hands**: part of the one hands-on session ([release-gates.md](release-gates.md#gate-6-the-hands-on-session)).

Every device test ends with the health oracle. Areas are ordered by what would hurt a
stranger most.

## Power, boot and recovery

Today: reboot through the API is used by several scripts; `ota_smoke.py` reboots twice.
Nothing interrupts the device at a moment it did not choose. Acceptance 18.12 (power
pulled mid-download) was never tried.

The stand-in for a power cut, with no hands: an abrupt chip reset at a random instant,
either from the PC over the USB Serial/JTAG interface or by a test hook that resets
without running any shutdown code. It reproduces what matters for NVS, the OTA data and
the FAT metadata (a write stopped between two sectors). It does not reproduce the card
losing power in the middle of its own internal write; only a switched supply does. A
USB hub with per-port power switching or a smart plug on the 27 W supply (15 to 30 USD)
would close that gap and is the one purchase worth considering.

| ID | Test | How | Passes when |
|---|---|---|---|
| PWR-1 | 200 abrupt resets at random instants during normal playback with downloads running | usb or hook | every boot reaches an artwork within the boot budget; settings, pairing and the active playset are intact; the card mounts; the reset counters are right |
| PWR-2 | Abrupt resets during bursts of settings writes | hook | the settings are the old or the new document, never defaults, never garbage (S-06) |
| PWR-3 | Abrupt resets during card writes: an upload, a download into the cache, an index save, a playset save, a rename | hook | the card mounts; no partial file is listed or played; temporary files are gone after the boot (the cleanup is to be defined: today nothing removes a stray `.tmp`) |
| PWR-4 | Abrupt resets during an update: mid-download, during verification, between the write and the reboot | usb | the running image is intact; the next check works; the slots are consistent |
| PWR-5 | An image that crashes at boot is rolled back automatically | usb (a deliberately crashing build of the same tree) | the previous image runs again within two boots and says why (never exercised so far) |
| PWR-6 | Escape from a crash loop: a poison setting and a poison file | hook | BOOT hold recovers the first; the second is rejected once, recorded, never retried in a loop (18.12). If any single stored item can make every boot crash, that is a defect |
| PWR-7 | The panic path itself | hook (a deliberate panic) | a core dump summary with valid text (F-03), the counters, a clean next boot |
| PWR-8 | A real power cut during a card write, 20 times | hands, or the switched supply | as PWR-3 |

## First boot and setup

The first minute of a stranger's device: a blank flash, no settings, no Wi-Fi, no card,
no pairing. Today: seen once by hand on 2026-09-19; the setup screen "never seen on a
device" since (PROGRESS.md); 18.8's setup-mode timings never measured.

Automating the portal needs a client that can join `p64-setup` without taking the test
PC off the LAN: a second Wi-Fi adapter on the PC (a USB dongle, about 10 USD). Without
one, the portal's pages are tested through the browser lane and the join itself by hand.

| ID | Test | How | Passes when |
|---|---|---|---|
| FIRST-1 | Erase the flash, write the release files exactly as the web flasher's manifest lists them | usb | the device boots into setup mode with default settings; the panel shows the setup pages; nothing of the development build's Wi-Fi seed is present |
| FIRST-2 | The portal end to end: probe URLs, scan list, save, the countdown page, the name | second adapter, or hands | joined within the stated time; the access point is gone within 5 s (18.8); `p64.local` answers |
| FIRST-3 | Wrong password in the portal; wrong password through Settings > Network (F-23) | second adapter or hook | setup mode is back within 60 s (18.8); a working saved network is never lost to a typo (to define) |
| FIRST-4 | The stranger's path: fresh device, Wi-Fi saved, no card, unpaired | usb + device | Promoted plays; the time from power-on to the first artwork is recorded as the headline number of the release |
| FIRST-5 | Factory reset through the API | usb (NVS snapshot and restore around it) | the state equals FIRST-1's; the card is untouched |
| FIRST-6 | Factory reset by BOOT hold; the web flasher's page in a browser; the captive sheet on a phone | hands | as written in the guides |

## Network

Today: the Wi-Fi manager and the fetch helper have no test at all; the rules of the
setup screen are host-tested. Server refusals reached the device three times in the
project's history (a dead keep-alive, a TLS slot held ten minutes, a 429 lockout that
took 50 % of core 0).

| ID | Test | How | Passes when |
|---|---|---|---|
| NET-1 | Wi-Fi lost for 10 s, 2 min and 10 min during playback from a card | hook | playback never stops; the access point appears after 60 s and disappears within 5 s of the reconnection; MQTT returns; the heaps return |
| NET-2 | Wi-Fi lost during a download, a channel walk, an update download, an NTP sync, a pairing | hook | each resumes or retries by its backoff; the TLS slot is free afterwards; nothing leaks |
| NET-3 | Hostile servers, for each client (Makapix API, the file host, Open-Meteo, GitHub): DNS failure, refused connection, a bad certificate, 1 byte per second, a close mid-body, a Content-Length that lies, a redirect loop, 429 with Retry-After, a run of 5xx | fake | bounded retries (counted at the fake), the backoff honoured, core 0 within its budget, the user told where the specification says so |
| NET-4 | The device name changed, three times quickly | device | reachable under the last name; mDNS answers once |
| NET-5 | Two p64 on one network with the same name | hands (the two units) | defined behaviour (to define: today unknown) |
| NET-6 | A router restart (new lease, possibly a new address) | hands, optional | reconnects; the pages work at the new address |

## Time

Today: the rules are host-tested (`time.cpp`, the night window); `ops_smoke.py` checks a
synced device. Never on a device: a refused answer, no DHCP server, DST, the nightly
sweep's trigger (seen once by hand).

| ID | Test | How | Passes when |
|---|---|---|---|
| TIME-1 | The time withheld from boot, with a card | hook | cached artwork plays; clocks show `--:--`; the pages say the device waits for NTP; everything starts when the time arrives |
| TIME-2 | DST in both directions, midnight, the year's end, for three zones | hook (injected time) | every clock face, the overlay, the night window and the sweep's trigger are right across the edge |
| TIME-3 | The night schedule's edges: a window across midnight, start equal to end, "panel off", the user's brightness changed inside the window, the ceiling below the target | hook | section 3.2 to the letter |
| TIME-4 | An NTP answer earlier than the build date; a server that stops answering | fake NTP on the PC | refused and counted; the time stays trusted, with its age shown |
| TIME-5 | The time zone changed while a clock face is up | device | the next frame is right |

## Makapix Club

Today: the contract and the policy are host-tested; `makapix_smoke.py` runs against the
real site; pairing was done by hand twice; certificate renewal and the invalid
registration state have no test. A fake server makes the rest reachable. Because the
device stores the CA it receives at pairing, a fake pairing can hand it a test CA, and a
local MQTT broker then stands in for the real one.

| ID | Test | How | Passes when |
|---|---|---|---|
| MKP-1 | Pairing: the code within 5 s, the credentials within 10 s of the entry (18.10), expiry, cancel, unpair, pair again, a 429 on the credential poll (status.md's note) | fake | the timings; one poll rate the server allows |
| MKP-2 | Certificate renewal inside the window; an expired certificate; repeated authentication failures | fake + hook (time) | renewed without the user; the "registration invalid" state and re-pairing work |
| MKP-3 | Listings: malformed JSON, empty pages, duplicates, entries that vanish or change, more entries than the cap, the size filters at their edges | fake | the index is right; nothing loops |
| MKP-4 | Files: 404, a wrong length, a non-image, 5 MB plus one byte, a corrupt image | fake | rejected once with a reason the pages show; requests counted at the fake stay bounded (18.12) |
| MKP-5 | Every MQTT command, malformed payloads, a broker that drops the connection every minute, the last will | fake (local broker) | section 13; the brightness ceiling holds under commands (18.7); "send to device" within 15 s (18.10) |
| MKP-6 | Views: at most one per 5 s; a 429 honoured | fake | status.md's engineering note closed |
| MKP-7 | The server gone for six hours | fake | playback from the cache; core 0 within budget; recovery without a reboot |
| MKP-8 | A small live check against makapix.club | device (today's `makapix_smoke.py`) | the contract has not drifted |

## The card (with a card present)

Today: the file manager's round trip, the sweep's budgets. No test for a full card, a
corrupt file, format, or large folders.

| ID | Test | How | Passes when |
|---|---|---|---|
| SD-1 | A full card (the free space reported under the floor; writes that fail) | hook | downloads pause; uploads and playset saves are refused with the reason; the show goes on |
| SD-2 | A cached file corrupted on the card (truncated, zeroed, random) | hook or a test route | rejected once, recorded, not retried in a loop (18.12) |
| SD-3 | Large content: 4096 files in a folder, 16384 in a playset, 64 channels, names of 128 bytes, non-ASCII names, names that differ in case | device | scan time and memory within budgets to set; the pages stay usable |
| SD-4 | Format while playing, downloading and scanning | device | the folders are recreated; the show recovers; no stall beyond the format itself |
| SD-5 | A slow card (added latency on every card operation) | hook | no late frame (playback never reads the card); the boot budget's dependence on the card is measured |
| SD-6 | The cards strangers buy: a new 64 GB card is exFAT | hands (NC-P2) | the device says so in words a stranger understands; the build guide says what to buy or how to format |

## Playback and decoders

Today: the strongest area. 86 corpus files are compared pixel for pixel on the host; the
timeline is host-tested; `timing_smoke.py` checks three synthetic files (and is flaky,
T-10). Missing: hostile files, the limits, the corpus on the device.

| ID | Test | How | Passes when |
|---|---|---|---|
| PLAY-1 | Decoder fuzzing: mutated corpus files through each decoder and the compositor, time-boxed in CI, crashers kept as a regression corpus | host (Linux, sanitizers) | no crash, no out-of-bounds, bounded memory |
| PLAY-2 | The limits on the device: 5 MB and one byte more, 256x256 and 257, 1x1, 256x1, 10 000 frames, zero delays, files truncated at each structural boundary, a JPEG | device | plays, or is rejected with a reason visible in the pages (principle 5); never a reboot |
| PLAY-3 | The whole corpus played on the device with the late counters read (18.2) | device | no late frame at 64x64 |
| PLAY-4 | 18.3 and 18.4 as written: ten-minute runs, fps taken from one consistent reading | device | within 1 %; no skipped frame; T-10 closed |
| PLAY-5 | Every transition (artwork, widget, interlude, stream entry and exit, status screens, pause, panel off) driven in sequence | device | the renderer's counters show no timeout and no late flip beyond budget; the camera check is in release-gates |
| PLAY-6 | Auto-swap at 5 s, 86400 s and 0; history at 32 and beyond; play-this with the interval at 0 | host (show core) and device | sections 4.5 to 4.7 |
| PLAY-7 | A 32-bit host build of the decoders | host, if a toolchain allows | the same results as the 64-bit build (the device is 32-bit; the host runs a different AnimatedGIF path) |

## Widgets and clock faces

Today: 67 pixel-exact references, the models, and two device scripts. To add:

| ID | Test | How | Passes when |
|---|---|---|---|
| WID-1 | Every face rendered for every minute of a day, and across the animated faces' frames | host, sanitizers | no crash, no write outside the frame |
| WID-2 | Weather and air against a fake Open-Meteo: fresh, stale with its age, "no data" after 6 h, a malformed reply, a change of location | fake + hook (time) | sections 7.2 and 7.4 |
| WID-3 | The cost of each face on the device | device | core 1 share under a budget to set (the station face once took 36.6 %) |

## Streams

Today: `stream_smoke.py` at 30 fps, 50 ms, 1.5 s of silence; the parsers on the host.

| ID | Test | How | Passes when |
|---|---|---|---|
| STR-1 | Acceptance 18.9 as written: 60 fps DDP, latency under 30 ms on a quiet network, silence after exactly 5 s | device | as written (S-10) |
| STR-2 | Floods and garbage: 10 000 datagrams a second, random bytes, chunks out of order, offsets beyond the frame, two senders at once | device | the panel keeps its lock; the counters count; the web server still answers |
| STR-3 | A stream during the setup, pairing and update screens; during a navigation storm | device + hook | section 8.3 |

## Inputs

| ID | Test | How | Passes when |
|---|---|---|---|
| INP-1 | Knob storms through the existing turn hook, and a press hook to add | device | brightness lands where the detents say; one NVS write after the burst |
| INP-2 | Real taps, auto-rotation through the four orientations, BOOT hold | hands | section 9; the direction of auto-rotation has never been verified |

## Updates

Today: `ota_smoke.py` installs over the LAN and rolls back; the release parser is
host-tested; the first field update was done by hand on 2026-10-20.

| ID | Test | How | Passes when |
|---|---|---|---|
| OTA-1 | Failure paths: a wrong checksum, a truncated image, a server that dies mid-download, a missing asset, the same version, an older version | fake | refused with the reason on the panel and in the pages; the running image untouched |
| OTA-2 | Install during playback, a download and an open page | device | no late flip beyond budget; the page follows the reboot |
| OTA-3 | **Migration**: flash the published v0.2.1, configure it as a user would (settings, playsets with plain and with accented names, pairing, a PIN), update to the candidate, then roll back | usb + device | everything survives the update; the old firmware still works after the rollback |
| OTA-4 | The web flasher's manifest against the release assets (offsets, sizes, checksums) | host | they agree; FIRST-1 flashes from the same manifest |

## The PIN

Covered by [web-ui-and-api.md](web-ui-and-api.md) (WEB-D10, Q5). To add here: the PIN
survives a reboot and an update; factory reset clears it; whether the lockout survives
a reboot is defined and tested.

## Long uptime

Today: the longest instrumented run is 55 minutes. The daily units show the device
survives weeks; they do not show whether a margin is shrinking. On 2026-11-11 the test
unit, after 49 hours, had its largest internal block under the budget floor and a
lowest-ever free heap of 2.3 KB (F-07).

| ID | Test | How | Passes when |
|---|---|---|---|
| LONG-1 | The 24 h soak of 18.11: Makapix refreshes, downloads, a page open in a browser, a monkey burst every hour | device | no reboot, no stall; and **trends**, not only floors: internal free, largest block, lowest-ever free, PSRAM, every task's stack high-water mark, open sockets, late flips and core 0 load have no slope after the first hour |
| LONG-2 | The 12 h seamless soak of 18.1 at a 5 s interval | device | the renderer's counters |
| LONG-3 | A week on the test unit between sessions, sampled every 10 minutes | device | the same trends; answers F-07 on the public image |
| LONG-4 | Time compression: every periodic job made frequent (channel refresh, NTP resync, update check, certificate check, the sweep, status publishes) so that a month of them happens in a day | hook | each ran its count; no growth |
| LONG-5 | Large clock values in the pure modules (the show core, the timeline, the policies) started near every 32-bit boundary | host | no misbehaviour; plus a read-through of every 32-bit time in the shells |
| LONG-6 | The units in daily use, observed and never disturbed: one status and memory reading a day, kept as a log | device (a scheduled script on the owner's PC) | a record that shows drifts and reboots between releases |

## Documents as tests

Claims a stranger relies on, checked mechanically where possible:

| ID | Test | How |
|---|---|---|
| DOC-1 | The settings table of the specification (section 16) against the settings model's defaults and ranges | host (generated and compared) |
| DOC-2 | The routes of `api.md` against the routes the firmware registers, both ways | host or device |
| DOC-3 | Each row of the owner's guide's troubleshooting and offline tables reproduced once | hook + device |
| DOC-4 | The flashing guides followed literally on a blank device | usb (FIRST-1) and hands (the browser page) |

## The private build

Not a gate for a public release. The suite must run unchanged on the private build so
its owner can; the public image is what every gate is measured on; the parity build
(`-DP64_NO_PRIVATE=ON`) stays the check that nothing public depends on the private area.
