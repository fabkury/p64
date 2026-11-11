# Testing strategy towards firmware 1.0.0

[Developer guide](../../README.md) · [Testing today](../testing.md) ·
[Specification](../../../docs/spec/p64-spec.md) · [Status](../../../docs/status.md)

Draft for discussion, written 2026-11-11 (prompt p123). Nothing here is implemented yet:
this folder is the plan and the evidence behind it. It will guide the testing work and is
meant to be argued with first.

## Why

p64 works: two devices have run around the clock for weeks. What that shows is that the
paths its owner walks every day are sound. It does not show what happens off those paths,
and a 1.0.0 that strangers build from the parts list will be used off them from the first
minute: no card, a phone on a weak connection, a finger that presses "next" eight times.
The existing tests (193 host cases, 20 device scripts, a soak harness) check that
features work when used as intended, one request at a time, on a healthy device with a
card. None of them sends two requests at once, a malformed body, or runs without a card
on purpose.

## What was done to write this

1. Three read-only audits of the code (2026-11-11): what the firmware does without a
   card, how the web server and the API behave under rapid, concurrent or malformed use,
   and what the tests cover today against the specification's sections and its twelve
   acceptance criteria.
2. The claims that matter most were re-read in the source by hand.
3. Short probes against the p64b test unit (firmware `0.2.1+private`, 49 h of uptime at
   the start), over HTTP only, no firmware change. They took about 25 minutes and
   confirmed five defects, one of which reboots the device with a 400-byte request and
   one of which leaves the show permanently without artwork.

The results are in [findings.md](findings.md). They are the best argument for the rest.

## Decisions taken (owner, 2026-11-11)

| # | Question | Decision |
|---|---|---|
| D1 | What 1.0.0 promises | **Stranger-proof**: a first-time builder with no card, flaky Wi-Fi, a phone browser and impatient fingers never gets a crash, a lock-up, a bricked device or lost data; every state has a defined, visible behaviour. Unusual hardware faults may be documented limits. Deliberate attack by a hostile LAN client is not the bar, but nothing reachable by accident may take the device down. |
| D2 | Test hardware | The p64b unit on USB-C and on the LAN is the test unit, available whenever testing is under way, and may be reset, erased and perturbed. |
| D3 | Hands-on effort | Minimal: automate everything that can be; one hands-on session of at most 30 minutes for 1.0.0. |
| D4 | Fault-injection hooks in the firmware | Yes, compiled out of releases. The release image is tested last, without them. |
| D5 | The role of cardless operation | **A first-class mode**: user playsets and uploads work without a card. (This is a change from ADR 0006's "degraded mode" and needs design, see [no-card-mode.md](no-card-mode.md).) |
| D6 | Card inserted, removed or failing while powered | **Power-off only**: the card is changed with the device unpowered, and the documentation says so. Removal or failure while running must be survivable (no crash, no download loop, a banner, a reboot recovers). The specification's "remount" promise is withdrawn. |
| D7 | Cardless Makapix feed | **Rolling window**: fill the memory cache once, then at most one new artwork per swap, replacing the least recently played. |
| D8 | This session | The documents plus quick probes; no fixes yet. |

## The strategy on one page

Principles:

1. **Invariants before examples.** Every device test ends by asking the same questions of
   the device (the *health oracle*, [infrastructure.md](infrastructure.md#the-health-oracle)):
   did it stay up, is the show alive, did memory come back, did the panel keep its lock,
   did the log stay clean, are the settings what they were. A test that exercises a
   feature and leaves the device subtly broken fails, whatever the feature answered.
   The stuck show of finding F-01 answered 200 to everything.
2. **Every rare state is reproducible without hands.** A test build carries hooks that
   eject the card virtually, withhold the time, drop Wi-Fi, fail a write, fill a queue;
   fake servers on the test PC stand in for Makapix, Open-Meteo and GitHub. A state that
   needs a hand to reach gets reached once a year; a state behind a hook gets reached in
   every run.
3. **Define, then test.** Where behaviour is undefined (most of cardless operation, the
   answer to a command that cannot be queued, the server's duty to a second client while
   a first one stalls), the definition comes first and goes into the specification. A
   test against undefined behaviour only freezes an accident.
4. **What ships is what is tested last.** Campaigns run on the test build; the release
   candidate, built by CI without hooks, repeats every test that needs none.
5. **Bugs become tests** (the project's rule since 2026-09-22, unchanged): each finding
   is fixed in the same commit as the test that would have caught it, a host test when
   the logic is pure.
6. **Risk first.** The order of work follows what would hurt a stranger most, not the
   order of the specification.

Layers, cheapest first:

| Layer | What | Runs | New work |
|---|---|---|---|
| L0 | Build, `-Werror`, image and static RAM budgets | CI, every push | add the host tests to `release.yml` |
| L1 | Host tests: unit, scenario (show core behind a fake env), pixel-exact corpora | CI, every push, ASan and UBSan | scenarios for lost results, cardless, queue pressure; fuzzing of every parser |
| L2 | The web UI's JavaScript in a real browser against a fake device on the PC | CI or the developer's PC | new (Playwright and a small fake API) |
| L3 | Device suite on the test unit: smoke, abuse, cardless, fault injection, recovery | one command, on demand and before every release | a suite runner, the oracle, the test hooks, the fake servers; about ten new scripts |
| L4 | Soaks with trend analysis: 12 h, 24 h, and a week; with a card and without | unattended on the test unit | growth and fragmentation checks, cardless and chaos variants |
| L5 | One hands-on session; passive observation of the units in daily use | once per major release | a written checklist of at most 30 minutes |

## The documents

| Document | Content |
|---|---|
| [findings.md](findings.md) | The register: defects confirmed on the device, defects found by reading, divergences between the specification and the code, defects in the tests themselves. Each with evidence and the test that would catch it. |
| [no-card-mode.md](no-card-mode.md) | Cardless operation: what the code does today, the decisions taken, the design questions still open (with a recommendation each), and the test plan. |
| [web-ui-and-api.md](web-ui-and-api.md) | The web server, the API and the pages under rapid, concurrent, malformed and stalled use: the model, the probe results, the design questions and the test plan. |
| [test-catalogue.md](test-catalogue.md) | Everything else, by area: power and recovery, network, time, Makapix, the card, playback, streams, inputs, updates, the PIN, long uptime, first boot. |
| [infrastructure.md](infrastructure.md) | What has to be built to run all this: the suite runner, the health oracle, the test build and its hooks, the fake servers, serial capture, browser automation, CI. |
| [release-gates.md](release-gates.md) | What must be true to tag 1.0.0: blockers, required runs, the hands-on checklist, accepted limits, documents to update. |

## Proposed order of work

Each phase ends with the device suite green on the test unit and its findings closed or
explicitly deferred.

| Phase | Content | Why now |
|---|---|---|
| 0. Stop the bleeding | Fix the two defects that take the device down or freeze the show from a browser (F-01, F-02), the unsafe like path (F-10) and the crash summary's invalid text (F-03), each with its test. Fix the tests that cannot fail (T-01 to T-03). | They are confirmed, cheap, and every later test trips over them. |
| 1. The harness | The suite runner, the health oracle, settings and NVS snapshot/restore, serial capture, the test build with the first hooks (virtual eject, withheld time, queue pressure). | Everything after this is written once and run forever. |
| 2. The web under abuse | Decide the open questions of [web-ui-and-api.md](web-ui-and-api.md#3-design-questions), then the abuse suite, parser fuzzing on the host, and the browser lane. | The owner's first concern; mostly independent of the cardless design. |
| 3. Cardless, designed | Settle [the open questions](no-card-mode.md#3-open-design-questions), write the specification text and the ADR, implement, and test with the virtual eject and one physical session. | The largest piece of design; needs the harness. |
| 4. Faults and recovery | Abrupt resets at random instants, Wi-Fi and server faults through hooks and fake servers, OTA failure paths, factory reset, first boot of a blank device. | Needs the hooks and fake servers of phase 1. |
| 5. Long runs and the release candidate | The 12 h and 24 h soaks of the acceptance list, a week of chaos soak, cardless soak, then the release candidate on the plain public image, the hands-on session, and the tag. | Last, on the image that ships. |

## Questions to settle in the discussion

The two design chapters end with their own lists; these are the ones that shape the
whole plan:

1. Where cardless playsets and uploads live: NVS, the unused 15.5 MB internal data
   partition, or a hybrid ([no-card-mode.md, Q1](no-card-mode.md#q1-where-cardless-data-lives)).
   This decides how much of "first-class" is reachable for 1.0.0.
2. What the web server owes a second client while a first one stalls or uploads
   ([web-ui-and-api.md, Q1](web-ui-and-api.md#q1-one-stalled-client-freezes-every-other)).
   The honest fix is architectural (more than one request at a time).
3. Whether the public image or the private build is the unit's everyday test image
   ([infrastructure.md](infrastructure.md#which-image-runs-on-the-test-unit)).
4. Whether to buy a switched USB hub or a smart plug (about 15 to 30 USD) for real power
   cuts, or to accept abrupt chip resets as the stand-in
   ([test-catalogue.md, power](test-catalogue.md#power-boot-and-recovery)).
5. Which of the acceptance criteria that need instruments (240 fps camera, power meter)
   are 1.0.0 gates and which become documented, unmeasured claims
   ([release-gates.md](release-gates.md#instrument-measurements)).
