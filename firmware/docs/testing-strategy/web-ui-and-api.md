# The web UI and the API under abuse

[Strategy](README.md) · [Findings](findings.md) · 2026-11-11

"A user spamming buttons on the web UI" had never been tested. Twenty-five minutes of it
on 2026-11-11 froze the show for good once and rebooted the device five times
([findings.md](findings.md), F-01 to F-05). This chapter is the model of why, the
questions the fixes have to answer first, and the tests that keep it fixed.

## 1. How a request travels

From the audit; the limits in the first table were read in the source or in ESP-IDF's
defaults.

```
browser tabs --- up to 12 sockets ---> one HTTP task (core 0, 8 KB stack)
                                         |  serves one request at a time, to completion
                                         |
        answers built here <-------------+--> settings_update(): NVS write, event bus (32)
        (status, files, frame)           |
                                         +--> show::send(): command queue (16) ---> main task
                                         |                                        (one command per turn)
                                         +--> makapix submit(): job queue (8) --> worker --> TLS slot (1)
                                         |
   WebSocket push task --- sends through the HTTP task ---+
                                                          main task --> loader queue (8) --> loader task
                                                          loader results --> the same command queue (16)
```

| Limit | Value | When it is exceeded |
|---|---|---|
| Requests served at once | 1 | the others wait, up to the stalled one's 10 s timeouts (F-04) |
| Open sockets | 12, least recently used purged | a push-only WebSocket is the first victim |
| Routes | 88 of 96 registered in the public image | a failed registration only logs |
| JSON body | 32 KB; nesting up to cJSON's default 1000 | a nesting of 200 overflows the stack (F-02) |
| Request line, headers | 512 and 1024 bytes (ESP-IDF defaults) | connection reset or 431 |
| Command queue to the show | 16, 500 ms timeout | the command is dropped and logged; the client got 200 (F-17) |
| Loader queue | 8, 500 ms timeout | the request is dropped; the main task waited 500 ms |
| Loader results | share the command queue | a dropped result is never replaced (F-01) |
| Event bus | 32, no wait | the event is dropped and logged (F-05) |
| Makapix jobs | 8, 200 ms timeout | "busy" |
| TLS sessions | 1, first come first served, no timeout | everything behind it waits |
| PIN sessions | 8 | the ninth login evicts one |
| Upload | 5 MB, buffer sized from Content-Length | refused with 413, body still drained |

What already protects the device: `next` at the end of the history coalesces into one
load; pause, resume and refresh are idempotent; the panel mode is last-wins; OTA check
and install answer 409 while busy; the sweep is single-flight; factory reset and format
need a confirmation word; file names are validated segment by segment; uploads are
decoded before they are written and written atomically; sliders in the pages save on
release, not on every movement.

## 2. What the probes showed

All against the p64b test unit, firmware `0.2.1+private`, 2026-11-11.

| Probe | Result |
|---|---|
| 40 `next`, one connection | 40 answers, 40 swaps in 4.5 s, healthy |
| 120 `next`, 16 connections | 120 answers, 20 swaps (coalesced), healthy; internal heap 6 KB lower for a while |
| 120 `previous`/`next` alternating, 16 connections | 21 s of queue overflow, server unreachable, then **the show stuck without artwork** (F-01) |
| 120 `history_go`, 16 connections | the same overflow for 21 s |
| 100 settings writes, 8 connections, 3 s | all 200; "settings changed" events dropped (F-05); no late frame |
| Nested JSON, depth 200 to 15000 | **panic and reboot, every time** (F-02); then an invalid status document (F-03) |
| Invalid JSON, wrong types, 40 KB body, bad positions, traversal, unknown routes | answered correctly (400, 413, 404, 405); wrong types ignored with 200 |
| 20 idle connections | no effect |
| 10 half-sent requests | **server unavailable for about 90 s** (F-04) |
| 6 WebSocket clients reading; 6 not reading for 25 s | no effect seen |
| 8 MB upload (refused) | **every other request waited up to 27 s** (F-04) |
| Upload stalled mid-body | another request waited 19 s; no temporary file left |

## 3. Design questions

The fixes for F-01 to F-05 and F-17 are choices, and the tests assert the choices.

**All settled on 2026-11-11**: D25 to D31 of [decisions.md](decisions.md) and ADR 0019.
One answer differs from the recommendation written here: Q1 was answered with option A
alone, so the promise is "within 3 s, except during an upload" and no worker task is
added. The text below is kept as the record of what was weighed.

### Q1. One stalled client freezes every other

The server handles one request at a time, so a slow upload, a phone that lost its
connection mid-request, a like that waits 20 s for the Makapix worker, a format or a
Wi-Fi scan each stop every page.

| Option | What | Cost |
|---|---|---|
| A. Tighten what exists (recommended first) | Receive and send timeouts of 2 to 3 s instead of 10; a refused upload closes the connection instead of draining the body; the pages check the file size before sending; nothing slow answers inline (like becomes "queued, the status will say") | small; the stall shrinks from tens of seconds to a few, it does not disappear |
| B. Slow work leaves the HTTP task | Uploads, file downloads, format and the Wi-Fi scan become asynchronous handlers on a small worker (ESP-IDF 5 has them) | a task stack to place and measure (internal RAM is the scarce resource; a PSRAM stack must never touch flash); more concurrency to reason about |
| C. A second server for heavy routes | A separate listener for files | more sockets and RAM; not recommended |

Proposed target, the thing the tests assert: **while one client stalls, uploads or sends
garbage, a request from another client is answered within 3 s.** Option A alone may
meet it for stalls; uploads need B.

### Q2. The answer when a command cannot be taken

Today: 200, and a line in the log. Proposed:

- A command that cannot be queued answers `503` with `Retry-After`, and the pages show
  it.
- Internal results (a loaded file, a finished scan) never share a queue with commands
  and are never dropped; a load whose result does not arrive in time is abandoned and
  retried by the show itself (the self-healing that F-01 lacks).
- Navigation is "latest wins": a burst of `next`, `previous` and `history_go` keeps at
  most one load in flight and one request waiting, the most recent.
- The main task never blocks on a full queue.

### Q3. How many browsers at once

Twelve sockets; a page uses a WebSocket plus one or two request connections. Proposed:
state a number in the specification (for example four pages open at once), test at that
number, and above it degrade politely (the oldest page's WebSocket closes and that page
falls back to polling; no page breaks).

### Q4. What a malformed request gets

Proposed: a JSON nesting limit of 16 at the parser; a 400 with the reason for a wrong
type (today ignored with a 200, F-08; the pages already send correct types); 414 and 431
answered with a body where ESP-IDF allows; a single error envelope for every route,
including the 404 and 405 that are plain text today.

### Q5. Who is trusted on the LAN

D1 does not ask for resistance to an attacker, but three things are reachable without
intent and cost little: a web page on the internet can make a visitor's browser POST to
`p64.local` (no Origin check, F-28); the setup portal's save and erase routes answer on
a running device (F-14); and the PIN's lockout is global. Proposed: reject state-changing
requests that carry a foreign `Origin`; close `/setup/save` and `/setup/erase` outside
setup mode; leave the global lockout as a documented limit. The statement "streams and
the setup portal are LAN-trust" stays in the specification.

### Q6. Settings writes

Proposed: a write that changes nothing writes nothing; rapid writes are coalesced into
one NVS write (the knob path already waits 800 ms); and the invariant the tests assert:
**after the last write, what is applied equals what is stored**, whatever was dropped on
the way (so the appliers read the document, never the event's payload, and a dropped
event is followed by another).

### Q7. The pages themselves

Proposed rules for the JavaScript: a control whose request is in flight is disabled or
its presses coalesce; one poll at a time; the reconnect backs off and never doubles; a
request the device is still working on is not reported as failed at 8 s (long actions
answer 202 at once and report through the status); what the page shows is reconciled
with the status after every action (no optimistic state that can stay wrong).

## 4. Test plan

Every device test below ends with the health oracle
([infrastructure.md](infrastructure.md#the-health-oracle)): the device answered, the
uptime did not reset, a swap happens within one interval, `prepared` returns to true,
the heaps return to within a margin of their start, the panel kept its lock, the log has
no dropped message and no error line, and the settings are what they were.

### On the host (CI, sanitizers)

| ID | Test |
|---|---|
| WEB-H1 | Every JSON entry point (settings, playsets, the action bodies, the Makapix contract, the release document, the weather and air replies) against a corpus of hostile documents: depth, size, wrong types, duplicate keys, huge numbers, invalid UTF-8, truncation. No crash, bounded stack (a build with a small stack limit), a defined error. |
| WEB-H2 | Property test of the settings model: whatever is applied, the result is within the clamps and serialises to valid JSON; applying a document twice equals applying it once. |
| WEB-H3 | Show-core scenarios under queue pressure: a lost `Loaded`, a lost `Scanned`, 100 navigation commands in one tick (at most one load in flight, the last wins), activation of three playsets in a row, commands arriving during a scan. After each, the show reaches a playing state by itself. |
| WEB-H4 | The timeout order of the like job (F-10) once its ownership is fixed. |
| WEB-H5 | The crash summary and every string that enters a status document: valid UTF-8 out, whatever comes in (F-03). |

### On the device: the abuse suite

| ID | Test | Detail |
|---|---|---|
| WEB-D1 | Bursts, per action | every action (`next`, `previous`, `history_go`, `pause`, `resume`, `reset_timer`, `refresh`, `play`, `play_playset`, `interlude`, `boot_animation`, like) x 50 sequential and 16 x 8 parallel; the answer is 200, 202 or the Q2 refusal, never a silent drop |
| WEB-D2 | The navigation storm | the reproduction of F-01, also with 5 MB files in the history and with a provider walk running; the smallest burst that failed before the fix is recorded and kept as the regression |
| WEB-D3 | The monkey | a seeded random walk over every non-destructive action and setting, from one to four connections, for 10 minutes; the seed is printed, so a failure is replayable |
| WEB-D4 | Settings bursts | identical writes, alternating writes, every live-applied setting (brightness, rotation, mirror, mode, background, overlay, time zone, device name): applied equals stored at the end; the count of NVS writes (a test-build counter) is what Q6 allows; no late flip |
| WEB-D5 | Malformed requests | the table of Q4: nesting depths, wrong types, sizes at and over each cap, invalid UTF-8, long request lines and headers, wrong methods and content types, chunked bodies, a Content-Length that lies; a defined answer each, uptime unchanged |
| WEB-D6 | Connections | idle sockets up to 30, half-sent headers, half-sent bodies, a reset mid-answer, pipelined requests; a second client is answered within the Q1 target throughout |
| WEB-D7 | Uploads | the size boundaries (exist today), oversize, aborted at three points, two at once, the same name twice, a non-image, a 5 MB file while a 5 MB file plays, a slow upload; nothing left behind, the Q1 target holds |
| WEB-D8 | WebSockets | the supported number of clients (Q3) and one more; a client that really stops reading (receive buffer set before connecting); 200 connects and disconnects; no leak (heap and a client-count diagnostic), no double sends |
| WEB-D9 | Overlaps of heavy actions | reboot twice; reboot during an upload; format during playback, a download and a scan; a playset storm across three playsets; ten play-this URLs; like while the worker is held for 30 s by a fake server (F-10); Wi-Fi saved twice; install an update during a navigation storm |
| WEB-D10 | With a PIN | a subset of all the above with the gate on; nine logins; the lockout; a WebSocket across a PIN change; the setup routes closed (F-14); a foreign Origin refused (Q5) |
| WEB-D11 | A busy device | the API's latency while a 5 MB WebP decodes, a channel walk runs and a sweep runs: a 95th-percentile budget added to `budgets.json` once measured |

### In a browser

Two lanes ([infrastructure.md](infrastructure.md#browser-automation)): the pages served
from the repository against a fake device on the PC (fast, can run in CI, can make the
"device" slow, failing or absent at will), and the real pages on the test unit.

| ID | Lane | Test |
|---|---|---|
| UI-B1 | fake | In-flight behaviour: 50 clicks on each transport button in 5 s produce the request pattern Q7 defines; no overlapping polls (requests counted by the fake); the reconnect attempts over 60 s of absence stay under a bound (U-02). |
| UI-B2 | fake | Failure behaviour: the device answers 503, 401, 500, nothing, or slowly; every page says something true and recovers when the device returns. |
| UI-B3 | fake | Every form: each control of the four pages round-trips its value through the fake's settings document; the cardless, unpaired, offline and "waiting for the time" renderings; the five themes at 360 px and 1280 px (no overflow, no dead control). |
| UI-B4 | real | Click storms through the real pages, in one tab and in two; a file upload through the file picker; play from a URL; the playset editor from creation to deletion; then the health oracle. |
| UI-B5 | real | Every setting changed in the page changes the device (read back from the status and, for what can be seen, from `/api/v1/frame`). |
| UI-B6 | both | The setup portal's pages (the real ones through the setup-mode hook). The captive-portal sheet of a phone stays in the hands-on session. |

## 5. Numbers to put under budget

New keys for `firmware/budgets.json`, set after the first measurements: the answer time
for a second client while a first one stalls (Q1); the 95th-percentile status latency on
a busy device; the time from the end of a storm to a healthy oracle; the internal heap's
dip under the monkey; the supported number of pages (Q3).
