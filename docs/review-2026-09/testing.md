# Testing discipline

Question asked: is the firmware protected by a comprehensive set of tests and by good
habits around testing, and what would greatly improve that?

Short answer: the pure layers are well protected and the device tests are broad, but the
protection stops exactly where the bugs have been. Of the 74 p64 source files, 26 are
host-tested (pixel-exact corpora, 1433 checks); the other 48, which hold the state
machine, the pacing, the fetcher and every API handler, are exercised only through 13
smoke scripts run by hand against the live device. Of the bugs the progress log records
as found on the device and fixed, fifteen have no test today, including the M10 crash
loop (the worst one). There is no CI, no `-Werror` on the host, no sanitizer, no size or
heap budget, no single command that runs everything, and the rule "a fix ships with its
test" is followed most of the time but written nowhere. The changes that pay most are
structural: give the show, the renderer, the player and the fetcher a pure core that the
host can drive, adopt a test framework, and run the host tests and the firmware build in
GitHub Actions on every push.

The inventory behind this page is `evidence-testing.md`.

## 1. What is protected today

Host (`tests/host/`): decoders (86 files, 1531 frames, exact against Pillow and a spec
APNG compositor), the scaler, rotation and gains, fonts and text, the playset model and
JSON, the scheduler (exact SWRR shares, stochastic within 5 %), history, the Makapix index
merge, the stream parsers and assembler, the night rule, the RTC codec, the tap and
orientation detectors, the version rule, the clock and analogue face, the weather model.
This is the strongest part of the project: everything numeric or bit-exact has an oracle.

Device (`tests/device/`): 13 scripts covering the API, content, Makapix, widgets,
streams, ops, IMU, PIN, OTA, UI, panel modes, the cache sweep and a soak. They found real
bugs (the handler table, the empty Retry-After, the frozen artwork) and they are the
acceptance record in `PROGRESS.md`.

## 2. Where it is thin

T1. The state and timing code has no test at all except by watching the device. The show
state machine (show.cpp, 1470 lines), the render schedule (renderer.cpp), the player
timeline (player.cpp) and the fetcher policy (fetcher.cpp) are where 11 of the 15
untested fixed bugs live: the two pacing bugs, the frozen artwork's cousins (stale resume,
one-file cache replay), the Followed restore, the walk restart. Each is a "what happens
when events arrive in this order" question, which a host test answers in a millisecond
and a device soak answers by luck.

T2. The web layer has no test below the socket. Every handler takes an `httpd_req_t`;
validation and JSON shaping are inline. The one bug that crash-looped the device (an NVS
read on a PSRAM stack from the WebSocket push task) is guarded by nothing: no test opens
the WebSocket, and the soak's "browser load" polls HTTP, which builds the document on a
different task with an internal stack.

T3. The settings clamp and enum rules (spec section 16, 84 cJSON calls in a file that
is pure apart from two NVS functions) have no host test; the M4 wrap-before-clamp bug is
device-checked for one key.

T4. The host framework is two macros and a counter. No test names in the output, no
isolation, no way to run one test, shared static state, `-Wall -Wextra` without
`-Werror`, no ASan or UBSan, no coverage. A failure prints `FAIL file:line: expr` and
the run continues, which is fine for a corpus check and poor for a state machine test.

T5. The device scripts duplicate their helpers (nine copies of `status()`, four of
`wait_for()` with three signatures), decide pass or fail with a global counter, abort on
transport errors without restoring the device (no `try/finally` anywhere), and leave
state behind on the happy path (auto-swap 30 s, the overlay corner, the weather location,
the stream enables, the IMU calibration, no PIN). Twenty endpoints in `docs/api.md` have
no test, among them the WebSocket, pairing, Wi-Fi, rename, format, the log ring.

T6. No CI. The repository is public, the host tests run on any Linux box in 100 s cold
and 7 s warm, and an ESP-IDF v5.5.4 docker image builds the firmware; none of it runs
automatically. Nothing checks the image size, the internal heap at boot, formatting or
warnings.

T7. The rule is unwritten. "Every fix ships with its test" is followed in 19 of 24
source commits, but CLAUDE.md's working conventions say nothing about tests, and the
five exceptions include the two pacing fixes and the Followed restore.

T8. Documentation drift that misleads a tester: `architecture.md` names `tools/hosttest/`
and `tools/bench/` (neither exists), lists a `p64_ops` component, marks `p64_net` as not
host-testable although `tz.cpp` is pure, and says the show loop runs at priority 5 (it
runs at 1). `api.md` says two scripts exercise everything below (they do not).

## 3. What would greatly improve it

Ordered by value over cost; the roadmap in `proposals.md` places them among the memory
items.

P-T1. A pure core for the four state-and-timing files (breaking, the one architectural
change this review recommends for testing). Split each into a core that owns its state
and takes time, randomness and effects through injected interfaces, and a thin shell that
binds it to FreeRTOS, esp_timer and the other components:

- `show.cpp`: a `ShowCore` struct (active playset, channel runtimes, scheduler, history,
  timers, state, stream parking) with `on_command(cmd, now)` and `on_event(ev, now)`
  returning a list of effects (`Play(source)`, `Load(path)`, `Scan(playset)`, `Persist`,
  `Notify`). The host test replays the scenarios from PROGRESS: the frozen artwork, the
  stale resume, the one-file cache, the Followed restore, the takeover and return.
- `renderer.cpp`: a `Schedule` (target = max(due, previous + delay), re-anchor beyond one
  period, the copy lead) fed with slot due times and a fake clock; the test asserts the
  M1 and M5 rules (no drift over 10 000 frames, a late slot re-anchors).
- `player.cpp`: a `Timeline` (due = previous due + delay after the browser rule, the 60 Hz
  floor, `decoded_late`, the generation announcement before the first frame).
- `fetcher.cpp`: the policy (which channel needs service, the walk, `finish_walk`, the
  60 s restart, the offline Followed job, the download order) over a fake API and a fake
  cache.

Cost: a few days of refactoring, mostly moving code. Every later feature lands as a
host test first. This is the change that turns "found on the device and fixed" into
"reproduced on the host and fixed".

P-T2. Cheap seams, in the same pass: compile the six already-pure files
(`png_encode`, `local_index`, `artwork`, `tz`, `status_screens`, `boot_animation`);
give `settings.cpp` a stub for its two NVS functions and test every clamp and enum of
spec section 16; test the Makapix site contract parsers (`api.cpp`: `entry_from_post`,
`fill_page`, `download_url`) and the MQTT payload builders against recorded server
documents; test the OTA release parser; test the cache sweep on a temporary directory;
draw the digital clock, weather and temperature from a `tm` and a model as the analogue
face already does; test the PIN lockout and session logic with an injected clock.

P-T3. A test framework. Adopt doctest (one header, no build system, sub-second
compile) over the hand-rolled macros: named test cases and subcases, `--test-case` to
run one, first-failure abort where wanted, JUnit output for CI. Build the host binary
with `-Werror -fsanitize=address,undefined -g` in CI and `-O2` locally; keep run.py as
the corpus driver. Cost: a day.

P-T4. GitHub Actions, two jobs on every push and pull request:

- host: ubuntu, gcc, Pillow; `python tests/host/run.py`, sanitizers on; upload the
  JUnit file.
- firmware: the `espressif/idf:v5.5.4` container; `idf.py build`, then `idf.py size`
  compared against `firmware/budgets.json` (image size, static DIRAM, IRAM), warnings
  as errors for p64 components (add `-Werror` and drop the `-Wno-error=` relaxations in
  the p64 CMakeLists), `clang-format --dry-run` on p64 sources; upload `p64.bin` and its
  SHA256 as artifacts, which is also the release asset pair `tools/release_assets.py`
  wants.

Secrets: none needed (`sdkconfig.secrets` is git-ignored and optional; the CMake seed
tolerates its absence, to be verified in the first run). The device lane stays manual
by the user's decision.

P-T5. A device test harness. One `tests/device/p64test.py` module: argparse (base URL,
`--paired`, `--card`, `--destructive`), a `Device` class with `status()`, `settings()`,
`put_settings()`, `action()`, `wait_for(pred, timeout)`, `frame()`, a WebSocket client;
a `snapshot()` and `restore()` of settings, playset and state wrapped in `try/finally`
by a `run(main)` helper; per-check names and a JUnit summary; tiers so
`tests/device/run_all.py http://p64.local` runs the read-only and restoring tests in
one go and refuses the destructive ones without the flag. Then the missing tests: the
WebSocket push (open it, hold it 30 s, check the status keeps coming and the device does
not reboot), pairing cancel, Wi-Fi scan, rename, the log ring, the refresh action, the
counters moving.

P-T6. Budgets as tests. `firmware/budgets.json`: image size, static DIRAM, internal heap
free and largest block at boot and at steady state with MQTT up, the CPU busy share of
core 0. CI checks the static ones; `api_smoke.py` checks the runtime ones against the
running device and fails when the floor moves by more than a set amount; `soak.py`
already checks the heap floor. The numbers of 2026-09-22 (`memory.md`, `cpu.md`) are the
first entries.

P-T7. The written rule, in CLAUDE.md's working conventions: before a commit that touches
`firmware/`, run `python tests/host/run.py`; a fix for a bug found on the device ships
with the test that would have caught it, host if the code is pure and device otherwise,
in the same commit; a new component or file is host-testable unless it talks to
hardware, and the architecture table's "host-testable" column is kept true.

P-T8. Fix the documentation drift (T8) in the same pass as P-T1, since the table changes
anyway.

## 4. Would dropping a feature improve testing discipline?

Only one candidate is worth naming: the streams (DDP and raw UDP). The user does not use
them, they are the one device test that needs a second machine and a quiet network, and
they are the one feature whose parsers are host-tested while its socket loop, takeover
parking and silence timer are device-only. Dropping them removes 675 lines, a task, one
device test and the takeover branches of `show.cpp`; it saves almost no memory (see
`memory.md`) and no CPU. The recommendation in `proposals.md` is to demote rather than
drop: a compile-time option, off by default in the release build, built in CI as a
variant so the code does not rot, and the takeover logic moved into the show core where
a host test covers it. Everything else on the drop list is used, or costs nothing to
test.
