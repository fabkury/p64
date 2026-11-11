# Test infrastructure to build

[Strategy](README.md) · [Findings](findings.md) · 2026-11-11

The tests of the other chapters need six things that did not exist on 2026-11-11: a
suite, an oracle, fixtures, a test build with fault-injection hooks, fake servers, and a
browser lane. This chapter specifies each.

**Built so far** (2026-11-11, the first step of phase 1):

| Piece | State |
|---|---|
| Suite runner | `tests/device/run_suite.py`: lanes `smoke`, `full`, `abuse`, `hooks`, `update`, `soak`; declared requirements; skips never count as passes; `summary.md`, `junit.xml` and a log per test under `tests/device/reports/` |
| Health oracle | `tests/device/harness.py`, asked around every test: up, show alive, heap floors, panel lock, clean log, settings and playset as before |
| Fixtures | the settings document and the playset (the runner puts back what a test left changed; `harness.settings_restored`). Not yet: the test folder, the PIN, the NVS snapshot over USB |
| Serial capture | in the runner: the USB console recorded without resetting the board, a panic attached to the test it hit. Not yet: the backtrace decoded against the ELF |
| Test build | `-DP64_TEST=ON` (`sdkconfig.test`, `CONFIG_P64_TEST_HOOKS`), version `+test`, `test_hooks: true` in the status; CI builds it and checks the release image has none of it. It costs the release image nothing (16 bytes) and the test image 1.8 KB |
| Hooks | `GET`/`POST /api/v1/test`: `card_eject` (a reason; the next boots mount no card), `time_withheld`, `hold_worker_ms`, `panic`, `reset` (abrupt). Not yet: card I/O failing while running, a full card, latency, Wi-Fi drops, host overrides, queue pressure, low memory, time injection, interval scaling, the panel stress |
| Hook tests | `tests/device/hooks_smoke.py`: the like with the worker held (F-10), the time withheld at run time and across a boot, a deliberate panic (F-03), five abrupt resets during settings writes, two cardless boots observed |
| Fake servers, browser lane, fuzzers | not started |

## The suite runner

Today there are 20 device scripts, each run by hand, each with its own idea of
preconditions and of leaving the device as found (T-04, T-05); one cannot fail (T-01)
and two pass when their precondition is missing (T-02).

Proposed: `tests/device/run_suite.py <url> --lane <name>` and a shared module that every
script uses.

- **Declared requirements.** Each test states what it needs: a card (`required`, `any`,
  `absent`), pairing, the internet, the test hooks, a fake server, the p64b's encoders,
  the USB connection, and whether it is destructive. The runner reads the device's
  status and decides. A test whose requirement is missing is reported as **skipped,
  with the reason**; a skip is never counted as a pass, and a lane with a skip that the
  lane requires fails.
- **Lanes.** `smoke` (10 minutes, no hooks: what runs on the release image), `full`
  (every feature test), `abuse` ([web-ui-and-api.md](web-ui-and-api.md)), `cardless`
  ([no-card-mode.md](no-card-mode.md); it also reruns every `card: any` test with the
  card ejected virtually), `faults` (power, network, time, servers), `soak`.
- **One report.** JUnit for machines and a short Markdown summary for
  `firmware/docs/PROGRESS.md`: the firmware version and commit, which image (test,
  release, private), the unit, the lane, durations, every failure with the device's log
  around it, every skip with its reason, and the oracle's before-and-after numbers.
- **Migration.** The existing scripts keep working alone; they are moved onto the
  shared module one at a time, fixing T-01 to T-04 on the way.

## The health oracle

One function, called at the end of every device test and by the runner between tests.
It compares the device with the snapshot taken before the test:

| Question | Read from | Fails when |
|---|---|---|
| Did it stay up? | `uptime_s`, `reliability` | the uptime went backwards, or a reset counter moved (unless the test declared a reboot) |
| Is the status a valid document? | the raw bytes | not strict UTF-8 JSON (F-03) |
| Is the show alive? | `playback` | in the Animation show with a playable playset: no swap within one interval plus a margin, or `prepared` stays false, or `no_artwork` appeared (F-01) |
| Did memory come back? | `diag/memory` | internal free, largest internal block or PSRAM stay below the pre-test value by more than a margin after a settling time; any floor of `budgets.json` crossed; a task's stack high-water mark under a floor |
| Did the panel keep its lock? | `panel`, the renderer's counters | a DMA stall, a timeout, late flips beyond the budget |
| Is the log clean? | `diag/log` since the snapshot | an error line, or any "dropped" or "queue full" line, outside the test's declared allowances |
| Is the web server answering? | a timed status request | slower than the budget |
| Are the settings what they were? | `settings` | any difference the test did not declare (T-11) |
| Are the files what they were? | the test folder's listing | leftovers |

The oracle is what turns "the request answered 200" into "the device is fine". Its first
version would have caught F-01, F-03 and F-05 on the day they were written.

## Fixtures

- **Settings**: the whole document is snapshotted before a test and written back in a
  `finally`, whatever happened in between.
- **The show**: the active playset and the main state, likewise.
- **Files**: tests write only under `animations/_test/` and remove it.
- **The PIN**: set and cleared by the fixture, never left.
- **The whole persistent state**: for tests that erase it (factory reset, first boot,
  migration), the NVS partition is read over USB before and written back after. The
  image contains the Wi-Fi password and the Makapix credentials: it is kept outside the
  repository, never printed, and deleted after the restore.
- **Which firmware**: tests that flash (migration, first boot, the crashing image) end by
  flashing the image the campaign started with, and by checking which slot runs (an
  installed update boots from the other slot; see CLAUDE.md).

## Serial capture

A panic on the device is visible to an HTTP test only as a timeout. During every suite
run the USB console (COM13 on the development PC) is recorded by an extended
`tools/serial_peek.py`; a panic's backtrace is decoded against the build's ELF and
attached to the failing test's report. The console is also the only witness when the
network is the thing under test.

## Which image runs on the test unit

Three images matter:

| Image | Built by | Used for |
|---|---|---|
| **Test build** | the public tree with `CONFIG_P64_TEST_HOOKS=y` | every campaign: `full`, `abuse`, `cardless`, `faults`, `soak` |
| **Release candidate** | CI, exactly as a release | the `smoke` lane and every test that needs no hook, last, before the tag; the migration and first-boot tests |
| **Private build** | the owner's checkout | the owner's own use; the suite must run on it, no gate depends on it |

The test unit ran the private build on 2026-11-11 (`0.2.1+private`). Decided (D40, D41):
it stays on the test build of the public tree while the hardening lasts, its pairing and
Wi-Fi kept through NVS snapshots, and returns to the private build after 1.0.0; the
p64a keeps running the owner's daily image and is only observed, by a script run by hand
(LONG-6).

A test build must be impossible to mistake for a release: its version carries a `+test`
suffix, the status document says `test_hooks: true`, and the System page shows it.

## Test hooks

Decision D4: compiled out of releases. Rules:

1. One Kconfig option, off by default; CI builds the firmware both ways so the hook code
   cannot rot, and the release build's size check proves the option costs nothing when
   off.
2. A hook changes what the code under test *sees* at a seam (a card call fails, the
   clock does not answer, a server's name resolves to the test PC). It never replaces
   the logic being tested.
3. Hooks live under `/api/v1/test/`, behind the PIN gate like everything else, and
   state that outlives a reboot is kept in its own NVS key that the release image never
   reads.
4. Each hook's cost in internal RAM is measured; a hook must obey the flash and
   PSRAM-stack rule like any other code.

Proposed set, by seam:

| Seam | Hooks | Serves |
|---|---|---|
| Card (`p64_storage`) | boot as if the mount failed, with a chosen reason; fail reads, writes or both from now on; report a chosen free space; add latency to every operation; corrupt a named cache file | all of cardless; SD-1, SD-2, SD-5; NC-D12 |
| Time (`net::clock`) | withhold the time; inject a time and a rate; release | TIME-1 to TIME-3; cardless without time; certificate renewal; the sweep |
| Wi-Fi (`p64_net`) | drop the connection for N seconds; enter setup mode keeping the credentials; refuse to reconnect | NET-1, NET-2; FIRST-3; the setup screens |
| Fetch (`p64_net/fetch`) | per-host overrides to the test PC (Makapix API, file host, MQTT, Open-Meteo, GitHub); fail the next N requests with a chosen error | every fake-server test |
| Tasks and queues | hold the Makapix worker for N seconds; stall the loader; fill the command queue; drop the next loader result | F-01, F-10, WEB-H3's device twin |
| System | panic now; reset abruptly; trip the watchdog; take and hold N bytes of internal RAM or PSRAM; counters for NVS writes and WebSocket clients | PWR-1 to PWR-7; low-memory behaviour; WEB-D4, WEB-D8 |
| Periodic jobs | scale every long interval | LONG-4 |
| Panel stress | network and crypto load while the panel's counters are watched (the "panel stress test" the specification promises, S-07) | 18.11 |

The first three hooks to write, because they unlock the most: the virtual eject, the
withheld time, and the abrupt reset.

## Fake servers

Device tests reach the real makapix.club, Open-Meteo, GitHub and NTP today, so no test
can make a server misbehave, and a test's result depends on the weather on the
internet. Proposed, under `tests/fakes/`, plain Python on the test PC:

| Fake | Speaks | Scripted faults |
|---|---|---|
| Makapix | pairing and credentials, the player RPC (`query_posts`), the promoted feed, the file host; MQTT through a local broker with a test CA handed out at the fake pairing | delays, status codes, 429 with Retry-After, malformed and truncated bodies, wrong lengths, files of chosen size and content, a broker that drops |
| Open-Meteo | forecast, air quality, geocoding | stale, malformed, slow, absent |
| GitHub releases | the release document, the image, the checksum | wrong checksum, truncation, a missing asset, older and equal versions (`ota_smoke.py` already has the seed of this) |
| NTP | answers with a chosen time | an old time, silence |

Each fake keeps a request log that tests assert on ("at most three requests in a
minute"), which is the only way to test backoff and retry bounds. The Makapix host is a
build-time option and the Open-Meteo host a constant today; the fetch seam above makes
both overridable at run time in the test build only. A small live lane against the real
services stays (MKP-8) to notice when a contract drifts.

## Browser automation

The pages' JavaScript has never been executed by a test (`ui_smoke.py` checks strings).
Proposed: Playwright with Python, two lanes.

- **Fake device**: a small server on the PC serves the files of
  `components/p64_web/ui/` as the firmware embeds them and implements enough of
  `/api/v1` and the WebSocket from a scripted state. It can be slow, wrong or absent on
  command and counts every request. It runs anywhere, including CI, and is where the
  pages' own logic is tested (in-flight buttons, polling, reconnects, error messages,
  every form, cardless and offline renderings).
- **Real device**: the same Playwright drives the test unit's real pages for click
  storms, uploads and end-to-end flows, ending in the oracle.

Claude in Chrome remains useful for exploratory sessions on the real pages; it is not a
regression lane.

## Host test additions

- A mutation fuzzer for every parser and decoder: deterministic seeds, time-boxed (a few
  minutes in CI, longer on demand), crashers saved into a regression corpus.
- The show core's fake environment learns to lose results, to have no card, to fill its
  queues, and to run with a scripted random sequence.
- The memory cache and the window policy move into pure files (they include ESP-IDF
  headers today) so they are host-testable, as the project's rule asks.
- Generated checks of documents against code (DOC-1, DOC-2, OTA-4).
- A 32-bit build of the decoder tests if the toolchain allows (PLAY-7).

## CI

- `firmware.yml`: add the build with the test hooks on, the fake-device browser lane
  and a short fuzz run.
- `release.yml`: run the host tests before publishing (T-07).
- The device lanes cannot run in CI. The release gate is therefore a record: the suite's
  report for the exact commit of the tag, linked from the release notes.

## Optional hardware

| Item | About | Buys |
|---|---|---|
| A second Wi-Fi adapter for the PC | 10 USD | the setup portal tested end to end with no hands (FIRST-2, FIRST-3) |
| A USB hub with per-port power switching, or a smart plug | 15 to 30 USD | real power cuts in a loop (PWR-8) |
| An inline USB-C power meter | 15 USD | acceptance 18.7 |

None is required by the plan; each removes an item from the hands-on session or turns a
stand-in into the real thing. Decided (D43): none is bought for now.

## Rough sizes

To help order the work, not to promise dates: the runner, the oracle and the fixtures
are a few days and pay back at once; the first hooks (eject, time, reset) a few days;
each fake server a day or two, Makapix with MQTT more; the browser lane with its fake
device about a week; the fuzzers a few days. The cardless design and its implementation
are the largest single piece and depend on the answers to
[no-card-mode.md](no-card-mode.md#3-open-design-questions).
