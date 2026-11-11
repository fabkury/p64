# Gates for firmware 1.0.0

[Strategy](README.md) · [Findings](findings.md) · 2026-11-11

A proposal for what must be true before the tag `v1.0.0` is pushed. The bar is D1:
stranger-proof. Every gate is a record somebody can check, not a feeling.

## Gate 1. No open blocker

Every finding marked **blocker** in [findings.md](findings.md) is fixed with its test,
and every **major** is fixed or moved, with a reason, to the known limits of
`docs/status.md`. Today's blockers:

| # | Finding |
|---|---|
| F-01 | A burst of navigation leaves the show without artwork for good |
| F-02 | A 400-byte request reboots the device |
| F-10 | The like path hands the worker a stack object |
| F-11 | The cardless feed downloads everything and keeps almost nothing |
| F-21 | Cardless PSRAM has no worst-case bound (a lead to confirm) |

The leads of section C are each confirmed or dismissed before this gate is judged.

## Gate 2. Behaviour is defined

The open questions of [no-card-mode.md](no-card-mode.md#3-open-design-questions) and
[web-ui-and-api.md](web-ui-and-api.md#3-design-questions) are answered, the answers are
in the specification (with a new ADR for cardless operation, superseding 0006), and the
divergences of [findings.md, section D](findings.md#d-the-specification-and-the-code-disagree)
are closed one way or the other: the code changed, or the sentence changed. A 1.0.0
specification describes the 1.0.0 firmware.

## Gate 3. The suites are green on the test build

On the test unit, on the commit of the release candidate, with the report kept:

- `full`: every feature test, with no skip the lane does not allow;
- `abuse`: the web and API suite, including the monkey for 10 minutes on three seeds;
- `cardless`: the cardless tests and every `card: any` test rerun with the card ejected
  virtually;
- `faults`: power, network, time and server faults;
- the host tests and the fake-device browser lane in CI.

## Gate 4. The long runs

| Run | Length | Acceptance |
|---|---|---|
| Seamless soak, Promoted at a 5 s interval | 12 h | 18.1 |
| Robustness soak with a page open and a monkey burst every hour | 24 h | 18.11, with trends flat after the first hour |
| Cardless soak | 12 h | [NC-D14](no-card-mode.md#device-tests-test-build-virtual-eject) |
| Idle week, sampled every 10 minutes | 7 days | no slope in the largest internal block or the lowest-ever free heap (F-07) |
| Time-compressed month of periodic jobs | 1 day | LONG-4 |

## Gate 5. The release candidate

On the image CI built, without hooks:

- the `smoke` lane and every test that needs no hook;
- the migration from the published v0.2.1 with a user's data, and the rollback (OTA-3);
- a blank device flashed from the web flasher's manifest (FIRST-1) and the stranger's
  path timed (FIRST-4);
- the update from the previous candidate over the air on the test unit;
- one week on a unit in daily use with the daily reading kept (LONG-6).

## Gate 6. The hands-on session

Once, on the release candidate, 30 minutes at most (D3). The order keeps the device in
one piece; the NVS snapshot is taken before and restored after over USB.

| # | Check | From | Minutes |
|---|---|---|---|
| 1 | Boot with the slot empty: the panel's sequence, the reason shown, the mount's duration in the log | NC-P1 | 3 |
| 2 | Boot with a new exFAT card: the reason shown, nothing written; format it from the pages, the restart, the card mounted | NC-P2, SD-6, D20 | 4 |
| 3 | Pull the card while a local file plays and a download runs; insert it again while running (ignored); restart (mounted) | NC-P3, NC-P4 | 4 |
| 4 | With a phone: join `p64-setup`, the captive sheet, save the network, the countdown, the device back on the LAN | FIRST-6 | 5 |
| 5 | BOOT held 10 s at power-on: the countdown, the reset | FIRST-6 | 3 |
| 6 | Taps (one, two) on the closed shell; the unit turned through its four orientations with rotation on auto | INP-2 | 3 |
| 7 | The web flasher's page: flash through the browser's port picker | DOC-4 | 5 |
| 8 | Eyes on the panel: Photo mode, the clock faces at a glance | 18.6 | 3 |

Each check is a line in the record with what was seen. No hardware is bought for now
(D43), so the portal, the BOOT hold and the card pull stay in this list.

## Gate 7. The acceptance criteria, one by one

| Criterion | Evidence required for 1.0.0 |
|---|---|
| 18.1 Seamless | the 12 h soak's counters; the camera sample stays unmeasured (D44) |
| 18.2 Jitter | PLAY-3 |
| 18.3 Best effort | PLAY-4 |
| 18.4 Timing | PLAY-4, ten-minute runs; the known limit under a provider walk stays documented |
| 18.5 Scaling | the host tests, plus one device check of 96 to 48 |
| 18.6 Modes | `panel_mode_smoke.py`; the refresh figure stays "from the driver's timing" (D44) |
| 18.7 Brightness and power | the ceiling under schedules and Makapix commands (MKP-5); the 15 W figure worded as a design value (D44) |
| 18.8 Boot | the boot budget (exists); setup mode within 60 s and the access point gone within 5 s (NET-1, FIRST-3) |
| 18.9 Streams | STR-1 |
| 18.10 Pairing | MKP-1, MKP-5 |
| 18.11 Robustness | the 24 h soak |
| 18.12 Recovery | PWR-1, PWR-3, SD-2, MKP-4 |

## Instrument measurements

Three claims need an instrument: no blank frame at a swap (a camera at 240 fps), the
Photo mode refresh (the same camera), and the power at full white (a meter).
Decided (D44): none of them gates 1.0.0; they stay listed as unmeasured in
`docs/status.md`, as today, and 1.0.0 ships on the counters.

## Known limits that 1.0.0 accepts

Carried from `docs/status.md` unless the work changes them: demanding animations slow
down rather than skip frames; a 16 ms APNG plays at 55 to 57 fps while a provider
download runs; a cold boot needs NTP before clocks and online features work; the card is
changed with the device unpowered (D6); deliberate attack from the LAN is outside the
bar (D1), with the PIN's global lockout among the documented consequences.

## Documents at the tag

The specification and its ADRs; `docs/status.md` (the remaining checks and known limits
rewritten from the gates' records); the owner's guide (cardless use, changing the card,
troubleshooting); the build guide (the card as an option, what to buy); `firmware/docs/
testing.md` (the suite, the lanes, the test build); `firmware/docs/api.md` (the answers
of Q2 and Q4, the new status fields); the release notes, linking the suite's report.

## The path of versions

Decided (D36 to D38): 0.2.2 goes out at once with the fixes for F-01, F-02, F-03, F-10
and F-14, checked by the host tests, a regression script made from the probes, the
existing smokes and a one-hour soak on the public image, then by the p64a's update over
the air. Everything else lands in 0.9.x release candidates, each run for a week on a
unit in daily use, then 1.0.0. Each release goes through the existing pipeline, so the
update path itself is exercised several times before it carries the 1.0.0 image.
