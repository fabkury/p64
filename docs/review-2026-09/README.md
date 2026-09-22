# Architecture review, September 2026: memory, CPU and testing discipline

Written 2026-09-22 (session prompt `prompt/p021-architecture-discipline-review.txt`),
three days after the product firmware reached M10. The question was whether there are
major breaking changes, including dropped features, that would improve the firmware's
memory, CPU or testing discipline if made now.

The answers, in one paragraph: internal RAM is the only resource in trouble, and it is in
real trouble (13 to 23 KB free at steady state against a 64 KB target, with a low-water
mark of about 1 KB seen on the device today); CPU is not a problem anywhere (core 0 is
3 to 5 % busy, core 1 is 9 to 44 % busy depending on the artwork) and no feature is
worth dropping for it; testing is strong for the pure layers and absent for the state and
timing code where the bugs have been. Two breaking changes are recommended: replace the
MQTT-over-mTLS session with an HTTPS command channel on the fetcher's single TLS slot
(the server's own roadmap already names it), and split the show, renderer, player and
fetcher into pure cores with thin shells so the host can test them. Around those, a set
of measured, non-breaking memory trims and a testing practice (framework, CI, budgets,
the written rule) do the rest. No feature needs to be dropped; streams are the one
candidate for demotion to a compile-time option.

## Files

| File | What it holds |
|---|---|
| `memory.md` | measurements taken today (heap by task, two configuration experiments, the low-water mark), the findings, and what each feature costs in internal RAM |
| `cpu.md` | measured per-task CPU shares at idle and under a 128x128 WebP, the findings, and the drop-a-feature table |
| `testing.md` | what is protected and what is not, the fifteen untested fixed bugs, and the practice changes |
| `proposals.md` | the ranked roadmap: every proposal with its measured or estimated gain, its cost, its risk, and whether it breaks anything |
| `tier1-results.md` | what tier 1 of the roadmap did on 2026-09-22 (prompt p022): the commits, the measurements before and after, the verification of the configuration change |
| `steps3-4-results.md` | what the next session did (prompt p024): doctest and GitHub Actions, and the first pure cores (timing, settings, the Makapix contract, the show's rules), with what their tests found and what remains |
| `evidence-memory.md` | the code inventory behind `memory.md`: every task, allocation and config item, with file and line |
| `evidence-cpu.md` | the code inventory behind `cpu.md`: every periodic activity, per-frame path, event handler and blocking hazard |
| `evidence-testing.md` | the code inventory behind `testing.md`: coverage by file, seams graded, the device scripts, the regression table |

## Decisions the review was made under

Asked with the tool at the start of the session and answered by the user:

- Makapix Club and the operations layer (web UI, PIN, OTA) are not candidates for
  removal. Everything else may be proposed.
- The device may be measured and flashed freely; the committed build is restored at the
  end. (Done: the device runs the committed build again; the instrumentation lives on
  the branch `review/cpu-instrumentation`.)
- Testing proposals may go as far as a framework, CI and a hardware lane; the user then
  chose GitHub Actions for the host tests and the firmware build, with the device lane
  staying manual.
- The server side of Makapix is open to change (the user owns makapix.club).
- The internal-RAM target is 64 KB free with a 32 KB largest block at steady state.
- Of the non-sacred features the user uses the weather and temperature widgets, the IMU
  and the clock; not the streams.
- The review session proposed only; tier 1 of the roadmap was executed the same
  afternoon under prompt p022 (`tier1-results.md`).

## Method

1. Three code audits (memory, CPU, testing) over `components/p64_*` and `main/`, each
   producing an inventory with file and line references (the `evidence-*.md` files).
2. Measurements on the paired development device: the linker map and `idf.py size`,
   the diagnostics API before and after a reboot, a boot log over the serial console, and
   an instrumentation build (per-task run time and heap-by-task attribution added to
   `GET /api/v1/diag/memory`) sampled at idle and under load.
3. Two configuration experiments flashed and measured (the always-internal malloc
   threshold; Wi-Fi and FreeRTOS code moved out of internal RAM), because they are the
   cheapest candidate levers and estimates for them are unreliable.
4. Synthesis into findings and a ranked roadmap.

## Instrumentation

The measurements used a branch, `review/cpu-instrumentation`, whose one commit was
cherry-picked into main the same day (tier 1, `tier1-results.md`): per-task `run_time`
and `core` in `diag/memory` are always on (`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`,
no measurable cost), and the `heap_by_task` array appears when a build sets
`CONFIG_HEAP_TASK_TRACKING` (about 4 KB of internal RAM, so off by default). The
sampling script is `firmware/tools/cpu_sample.py`.
