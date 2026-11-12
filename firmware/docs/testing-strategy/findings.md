# Findings register

[Strategy](README.md) · 2026-11-11

What the audits and the probes of 2026-11-11 found. Four kinds of evidence, never mixed:

- **Device**: observed on the p64b test unit (firmware `0.2.1+private`, a card present,
  paired, no PIN) through HTTP probes. The web and show code involved is public code; the
  findings still have to be repeated on the public image.
- **Source**: read and checked by hand in the files named.
- **Audit**: reported by a read-only audit with file and line, not re-checked by hand.
  Treat as a lead, not as a fact.
- **Unverified**: an inference nobody has tested.

Severity is against the 1.0.0 bar (D1, stranger-proof): **blocker** (a stranger can hit
it by accident and loses the device, the show or data), **major** (visible wrong
behaviour or a risk that grows with time), **minor** (cosmetic, or needs intent).
Nothing here has been fixed; the fixes belong to the testing work, each with its test.
The choices the findings left open (what to do about F-04, F-13, F-14, F-16, F-17, F-23,
F-28, F-29 and the divergences of section D) were decided the same day:
[decisions.md](decisions.md). F-01, F-02, F-03, F-10 and F-14 are fixed first, in 0.2.2.

## A. Confirmed on the device

### F-01 A burst of navigation leaves the show without artwork for good (blocker; fixed in 0.2.2)

- **Fixed** (2026-11-11, commit b32f29c): the loader's results have their own queue and are never
  dropped; the show never waits on the loader; one navigation load and one scan in flight
  at a time, the latest target wins; a load not back after 30 s is asked for again. Five
  host scenarios; on the device `abuse_smoke.py` serves the 240-request burst in 7 s and
  the show goes on.

- **Seen**: 120 `previous`/`next` requests alternating, from 16 parallel connections,
  then the same with `history_go`. For about 21 s each time the log alternated
  `loader: queue full; request dropped` and `show: command queue full; dropped command
  14` every 500 ms, the web server answered nothing (ten connection timeouts), and the
  internal heap fell to 33 KB. The history lost healthy items (32 to 23). Afterwards
  `playback.prepared` stayed false: no artwork played again. At every auto-swap the show
  put up an interlude instead (seven in a row over 3.5 minutes, see F-06); with every
  interlude gap at 0 it would have frozen on its last picture.
- **Recovery**: `next` and `previous` answered 200 and did nothing. Activating the
  playset again recovered it; a reboot would too. Nothing in the web UI tells the user.
- **Cause (source)**: the loader's results travel through the same 16-deep queue as the
  commands (`main/show.cpp:232-245`), sent with a 500 ms timeout and deleted when the
  queue is full. A lost `Loaded` leaves `prepared_load_id` set, and nothing ever times
  it out (`main/show_core.cpp:753-774`). The main task meanwhile blocks 500 ms per
  request on the loader's 8-deep queue (`main/loader.cpp:147-154`). Each `previous`,
  `history_go` or forward `next` submits its own load; nothing is cancelled or
  coalesced.
- **Not yet known**: the smallest burst that does it (40 sequential `next` were served
  correctly, one swap each, and 120 parallel `next` at the end of the history coalesced
  into 20 swaps without harm); whether one browser can reach it with key repeat, or two
  tabs; the same on the public image.
- **Tests**: a show-core scenario that drops a `Loaded` and a `Scanned` result and
  expects the show to recover by itself (host); the navigation storm in the abuse suite
  ending in the health oracle's "a swap happens within one interval" (device).

### F-02 A 400-byte request reboots the device (blocker; fixed in 0.2.2)

- **Fixed** (2026-11-11, commit 9294ea3): cJSON is built with a nesting limit of 16 for every
  parse in the firmware. Host test of the limit; on the device bodies of 17 to 15000
  levels answer 400 on six routes with no reboot (`abuse_smoke.py`).

- **Seen**: `PUT /api/v1/settings` with a body of 200 `[` followed by 200 `]`. The
  request hung, the device panicked and rebooted (uptime back to 0, reset reason
  `panic`). Depths 600, 1000, 5000 and 15000 did the same within 4 s. Depth 50 was
  answered 400 as it should. Five requests, five panics.
- **Cause (source and audit)**: cJSON parses recursively with its default nesting limit
  of 1000 on the HTTP task's 8 KB stack (4 KB free at rest, `diag/memory`); the body cap
  is 32 KB (`components/p64_web/src/api.cpp:143-155`).
- **Reach**: every route that parses a JSON body. With a PIN set, `/api/v1/auth/login`
  parses its body before any authentication (audit, `auth.cpp:187`), so the PIN does
  not protect against it.
- **Tests**: a table of nesting depths against every body-taking route, expecting 400
  and an unchanged uptime (device); the JSON entry points fuzzed on the host with a
  stack limit.

### F-03 After a panic the status document is not valid UTF-8 (major; fixed in 0.2.2)

- **Fixed** (2026-11-11, commit 9294ea3): the crash summary's task name passes through
  `system::printable_ascii`. Host test with the bytes seen on the device. The wider rule
  (every outside string sanitised where it enters a JSON document, checked by the
  oracle) stays open.

- **Seen**: after F-02's panic, `reliability.crash.task` carried raw bytes
  (`\xaa4\xce?\xa4\xb7\xc9?`), read from a corrupted task block. Python's strict JSON
  parser refused the whole status document, so every device test would have failed at
  its first line until the core dump was erased (`POST /api/v1/diag/coredump/erase`).
  Browsers replace the bytes, so the pages kept working.
- **Class**: the same as the `/history` failure of 2026-10-01 (invalid UTF-8 from a file
  name). Text that did not originate in the firmware must be sanitised wherever it enters
  a JSON document.
- **Tests**: a host test of the crash summary's serialisation with hostile bytes; the
  oracle validates every status document as strict UTF-8 JSON.

### F-04 One stalled client freezes the web server for every other (major; bounded 2026-11-11)

- **Bounded** (ADR 0019, decision D25): timeouts of 5 s each way, and a refused body closes the
  connection instead of being drained. Measured on the p64b with `abuse_smoke.py`: behind
  one half-sent request another client is answered in 4.8 s; behind six in 29.7 s (they
  are timed out in turn; 90 s before); a refused 8 MB upload is cut off after 16 KB and
  others are answered in 0.2 s (27 s before). A legitimate upload still holds the server
  for its duration, by decision.

- **Seen**, three ways:
  - ten connections that each sent half a request header: no other request was answered
    for about 90 s (two 30 s timeouts, then a 28 s wait);
  - an 8 MB upload, refused at once with 413: the server kept reading the body for 27 s
    and every other request waited that long (two timed out);
  - an upload that stopped mid-body: a status request waited 19 s.
- **Cause (source and audit)**: one HTTP task serves every request to completion
  (`components/p64_net/src/http_server.cpp:37-46`), with 10 s receive and send
  timeouts; a refused body is still drained.
- **Why it matters to a stranger**: a phone that walks out of Wi-Fi range mid-request, or
  a file dragged into the file manager that is too large, freezes every page (the pages
  abort at 8 s and report failures for work that is still going on).
- This is a design question before it is a test:
  [web-ui-and-api.md, Q1](web-ui-and-api.md#q1-one-stalled-client-freezes-every-other).

### F-05 Events are dropped under a burst of settings writes (major; fixed 2026-11-11)

- **Fixed**: events that only say "read again" are coalesced in the bus (one queued at a time),
  a settings change that changes nothing notifies nobody, and the flash write is deferred
  and merged. On the device: 120 changes from 8 connections in 3.2 s cost 2 flash writes,
  dropped no event, and what the panel applied equalled what was stored.

- **Seen**: 100 `PUT /api/v1/settings` from 8 connections in 3.1 s, all answered 200;
  the log shows `events: queue full; dropped settings changed` many times.
- **Consequence (unverified)**: a setting saved to NVS whose "changed" event was dropped
  is stored but not applied until something else changes. The probe did not demonstrate
  a stale value; the test must look for one (write A, B, A, B at speed, then compare the
  panel's `brightness` in force with the stored value).
- Also seen: every PUT, including an empty `{}`, is a full NVS write with no debounce
  (audit, `components/p64_system/src/settings.cpp:96-118`).

### F-06 The interlude roll repeats while no artwork is ready (major; fixed 2026-11-11)

- **Fixed**: one roll per due swap. Host scenario: 20 late swaps of 400 ticks each give at most
  two interludes at a 1.1 % chance (close to 20 before).

- **Seen** in F-01's stuck state: an interlude at every interval.
- **Cause (source)**: `main/show_core.cpp:1056-1057` runs `if (!roll_interlude())
  swap_fresh();` whenever the auto-swap is due. When `swap_fresh()` has nothing prepared
  the timer stays due, and the roll is made again on the next loop tick, until a widget
  wins.
- **Consequence**: whenever the next artwork is late at the deadline (a slow download,
  the cardless feed, a slow card), an interlude appears almost surely instead of
  rarely, which defeats the median gaps of ADR 0014. Unmeasured in normal use.
- **Tests**: a show-core scenario with the prepared artwork late by many ticks and a
  fixed random sequence: exactly one roll per due swap.

### F-07 Internal heap after two days of uptime (major, to be measured)

- **Seen** at the start of the probes (49 h uptime): internal free 52 KB, largest free
  block 24.6 KB (the floor in `budgets.json` is 28.7 KB), lowest free since boot 2.3 KB.
  Five minutes after the reboot: 61 KB free, largest block 29.7 to 34.8 KB, lowest
  11.5 KB.
- **Caveats**: the private build (which costs about 8 KB more static RAM); one sample.
  The floors are checked by `api_smoke.py` and `soak.py` only when someone runs them,
  and the longest instrumented run so far is 55 minutes.
- **Tests**: the trend soak ([test-catalogue.md, long uptime](test-catalogue.md#long-uptime)):
  largest block and lowest-ever free sampled over days, on the public image, with a slope
  limit instead of a floor.

### F-08 Small protocol faults (minor; partly fixed 2026-11-11)

- **Fixed**: a wrong type or an unknown choice in a settings write is a 400 naming the key
  (decision D28); unknown routes and wrong methods answer in the JSON envelope. Still
  open: the over-long request line.

- A request line over 512 bytes gets a connection reset, not a 414 with a body.
- Wrong types and unknown keys in a settings write are ignored and answered 200 (by
  design today; the pages rely on clamping). To decide: keep, or answer 400 for a wrong
  type so a script learns of its mistake.

### What the probes did not break

For the record, served correctly: 40 sequential and 120 parallel `next`; 20 idle
connections (least-recently-used purge); six WebSocket clients reading; six WebSocket
clients not reading for 25 s (the predicted stall of F-22 did not appear, probably
because the PC's receive window never closed); invalid JSON, non-objects, a 40 KB body
(413), bad history positions, path traversal in `play` and `files`, a NUL in a path,
unknown routes and methods; an aborted upload left no temporary file.

## B. Found by reading (source checked by hand)

### F-10 The like path hands the worker a stack object (blocker; fixed in 0.2.2)

**Fixed** (2026-11-11, commit 4fcc308): the job is on the heap and `handoff.hpp` decides which
side frees it; host test with 2000 races. The 20 s wait on the HTTP task stays until the
like is queued and reported through the status (D25). On the device since the test build
exists (`hooks_smoke.py`, 2026-11-11): with the worker held 30 s the like gives up at
20.1 s, the worker then runs and frees the abandoned job, twice, and the device stays up.

`makapix::like()` builds a `Job` on its own stack, queues its address and waits 20 s
(`components/p64_makapix/src/makapix.cpp:522-550`). On a timeout it returns; the comment
there says the job must not be freed while it may run, but the job is not on the heap:
the stack frame is gone. The worker later writes `job->ok`, assigns `job->error` (a
`std::string`) and gives `job->done` (`fetcher.cpp:61-70`) into whatever now occupies
that stack. It is reachable whenever the worker is busy for more than 20 s (a slow
play-this download, a channel walk, a wait for the TLS slot) and the user presses like,
from the page or from knob B. The web handler also blocks the only HTTP task for those
20 s. **Tests**: a fake-server delay in the device suite (like while the worker is held)
with the oracle; in the fix, the job lives on the heap with shared ownership, and a host
test covers the timeout order.

### F-11 The cardless feed downloads everything and keeps almost nothing (blocker for D5; fixed 2026-11-12)

**Fixed** by the memory window (`p64/content/memory_window.hpp`, ADR 0018): filled once, then one
new file per swap and per minute in place of the least recently played; an evicted file's entry
loses its flag in the same step; the listing is walked 100 entries deep and deeper only when a lap
runs out. On the p64b, ejected: 48 files in 46 s, then 2 downloads in 150 s of 30 swaps (85 in 73 s
before), no miss, no load failure (`cardless_smoke.py`).

`components/p64_makapix/src/cache.cpp:19-46`: the memory cache holds 48 files or 6 MB,
evicts first-in first-out (the specification says least recently played), and never
refuses a file (one bigger than the room left evicts everything). `fetcher.cpp:400-469`
has no "cache full" stop: the free-space floor applies only with a card, so the whole
listing (2048 entries per channel by default) is downloaded at every boot and only the
last 48 files survive. Eviction does not clear an entry's "cached" flag; the show picks
among all flagged entries, misses, and each miss clears one flag and triggers one
download (audit: `provider.cpp:55-65`, `loader.cpp:57-62`, `makapix.cpp:433-445`).
Decided: the rolling window (D7). Details in [no-card-mode.md](no-card-mode.md).

**Confirmed on the device** (2026-11-11, `hooks_smoke.py` with the card ejected virtually,
Promoted, firmware `0.2.2+test`): 85 downloads in the first 73 s of a cardless boot and 40
in 38 s of the next, more than one a second for as long as it was watched; PSRAM fell to
13.3 MB free. The first artwork was up 7 to 8 s after the reset, which also answers Q7's
estimate (10 to 30 s was a guess; the budget can be set near 15 s).

### F-12 The card is mounted once and its loss is never noticed (major; D6 defines the fix)

`main/main.cpp:282-286` mounts at boot and publishes `CardMounted` or `CardFailed`;
nothing else ever calls `mount()`, and `storage::remount()`
(`components/p64_storage/src/card.cpp:131`) has no caller. There is no detection of
I/O errors at run time: a card pulled while playing leaves `card.mounted` true and no
banner. Audit: provider downloads then fail to store and retry another entry every 5 s
for as long as the device runs (`fetcher.cpp:451-456`).

### F-13 A saved Local playset without a card shows "no card" forever (major) (fixed 2026-11-12: the playset rule of ADR 0018 (`rules::restore_choice`): the choice or Promoted, never a stand-in)

`main/show_core.cpp:1185-1198`: a saved user playset that cannot be read falls back, but
a saved "Local" resolves as a built-in and is activated with nothing to play, even with
Wi-Fi up and Promoted available. The fallback itself is chosen by the card's presence
(Local with a card, Promoted without), where the specification (5.2) says by the
network's.

### F-14 The setup portal's save and erase routes are always open (major; fixed in 0.2.2)

**Fixed** (2026-11-11, commit 4035fbf): both answer 403 outside setup mode; checked on the
device by `abuse_smoke.py`. The positive case (they work in setup mode) waits for the
setup-mode hook or the hands-on session.

`components/p64_net/src/setup_portal.cpp:177-233`: `/setup/save` and `/setup/erase` are
registered outside the PIN gate ("the setup portal is never behind the PIN") and their
handlers do not check that the device is in setup mode. On a running, PIN-protected
device, any client on the network can replace or erase the Wi-Fi credentials with one
POST. The specification (10.3) exempts the portal "in setup mode" only.
**Tests**: the two routes outside setup mode answer 403 or 404 (device; the virtual
setup-mode hook covers the positive case).

### F-15 A channel index that fails to parse is deleted (minor)

`cache.cpp:80-83`. The specification (14) says nothing is deleted because a read failed,
since a dying card misreads healthy files. The index is rebuilt from the server, so the
cost is a re-walk; with a marginal card it is a loop.

### F-16 Two settings are stored and never used (minor; removed 2026-11-12)

**Removed** from the settings model, the pages and the specification (decision D32); a document
that still carries the keys is read and they are not written back.

`storage.card_root` and `storage.downloads_cap_mb` exist in the settings model and the
API; no code reads them (the root is the constant `p64`, `card.cpp:27`). The
specification (14, 16) describes both as working. Either implement or remove before 1.0.0:
a setting that does nothing is a broken promise.

### F-17 A command that cannot be queued is answered 200 (major; fixed 2026-11-11)

**Fixed**: the show's `send()` reports a full queue and the routes answer 503 `BUSY` with
`Retry-After`. Not provoked on the device: since F-01's fix the queue no longer fills
under the bursts of `abuse_smoke.py`; it needs the queue-pressure hook.

`main/show.cpp:232-245`: `send()` returns nothing; the handler has already decided to
answer. Confirmed in F-01's log. To define: the answer when the device cannot take a
command ([web-ui-and-api.md, Q2](web-ui-and-api.md#q2-the-answer-when-a-command-cannot-be-taken)).

## C. Reported by the audits (leads, not re-checked)

| # | Lead | Where | Severity if true |
|---|---|---|---|
| F-20 | A lost `Scanned` result leaves `scan_running` true: no later rescan | `show_core.cpp:812-813` | major |
| F-21 | Cardless PSRAM has no worst-case bound: the 6 MB cache, a 5 MB download, the copy made on store, the current and the prepared artwork (each a copy of up to 5 MB) and the indexes can exceed 16 MB; with C++ exceptions off a failed allocation aborts | `cache.cpp`, `sdkconfig.defaults:111` | blocker for D5 |
| F-22 | (2026-11-11: one entry per descriptor now; five WebSocket clients served and a request answered in 0.2 s beside them; the stalled-client case is still not reproduced) The WebSocket client list keeps a dead descriptor that a new client can reuse (double sends); a client whose buffer is full costs the HTTP task up to 10 s per broadcast | `ws.cpp:24-68` | major |
| F-23 | `POST /api/v1/wifi` overwrites the saved credentials before trying the new ones, and starts a task per call without checking the result | `wifi.cpp:318-333`, `api.cpp:558,566` | major (a typo in the password strands the device until setup mode) |
| F-24 | Rename has no protected-folder check (delete has); an upload overwrites a file of the same name silently | `files.cpp:115-155`, `card.cpp:376` | minor |
| F-25 | Reboot does not unmount the card; format holds the mount mutex while the loader, the fetcher and the status builder wait | `api.cpp:378-389`, `card.cpp:94-115` | major |
| F-26 | (fixed 2026-11-12: the reason is "waiting for the time") With Wi-Fi up and NTP unanswered the device calls itself "offline"; `last_error` is never cleared, so the Home banner sticks | `makapix.cpp:117,485`, `show_core.cpp:759,782` | major (misleads) |
| F-27 | Play-this by URL or post: one job per press, no dedupe and no cancel; Followed makes a server call per press | `makapix.cpp:474-520,706-718` | minor |
| F-28 | (2026-11-11: a foreign Origin is refused on every changing request and on the WebSocket, decision D29; the global lockout and the session eviction stay as documented limits; a WebSocket open across a PIN change is still not closed) PIN: the lockout is global (five wrong tries by anyone lock everyone out of logging in for 30 s); a ninth login evicts a live session; WebSockets opened before a PIN change keep receiving status; no Origin check on any POST | `auth.cpp:96-155`, `http_server.cpp:27` | minor to major, a policy question |
| F-29 | A card without a FAT file system cannot be formatted from the web UI (format needs a mounted card), against ADR 0006; Home says only "No card" | `card.cpp:64,96` | major |
| F-30 | The boot mount runs on the main task; its duration with an empty slot is unknown | `main.cpp:282` | to measure |
| F-33 | (fixed 2026-11-12) The IMU was probed once at boot. On the boot after a flash that day the probe missed: `imu_present` false for the whole run, no taps, and the unit, set to auto-rotation, showed its picture at the fallback rotation (90 where it stands at 270) until a restart, which found the sensor at once. Now three probes at boot, then one every 5 s by the IMU task, with the calibration loaded when it answers; a probe that fails after the chip was found removes its device so the next one starts clean. `hooks_smoke.py` `imu` withholds the sensor for 25 s of a boot and sees it found without a restart. Seen once; how often a probe misses is not known | `p64_inputs/src/inputs.cpp`, `qmi8658.cpp` | major |
| F-32 | Open pages cost internal RAM that the idle floors do not show. Measured 2026-11-12 with a browser on the p64b: idle 58 KB free and a 32.8 KB largest block; with one to five pages open the lows over 12 s were 40 to 54 KB free and a largest block of 14.3 to 25.6 KB (14.3 KB with four pages), back to the idle values the moment the pages close. A TLS session needs 12 to 13 KB. Not new (the soak's floor of 32.8 KB with one poller already allowed for it), but never measured with real pages, and four pages are now promised (D27) | the HTTP server's per-connection memory | major: to test whether a TLS operation fails with four pages open (LONG-1) |
| F-31 | The brightness notice (the sun at 255) goes up with the artwork's next frame: over an animation with long frame delays it appears late and shortened, and can be missed. Found by the suite on 2026-11-11 when `encoders_smoke.py` failed one run in three | `show.cpp` (the player's overlay) | minor |

Web UI leads (audit of `components/p64_web/ui/`):

| # | Lead |
|---|---|
| U-01 | (fixed 2026-11-12: the shared script merges identical requests in flight, whichever control sent them; `run_ui.py` storm: 40 presses in 2 s send 5 requests, 40 before) Next, previous, like, pause, playset pills, interlude, intro, mode and rotation buttons are not disabled while their request is in flight (upload and "Show URL" are) |
| U-02 | (fixed 2026-11-12: one poll at a time; the socket is retried after 2, 4, 8 and every 15 s, one retry pending at a time. The suspected doubling was real: 15 attempts in 33 s before, 5 now) The polling fallback has no in-flight guard; the WebSocket reconnect is scheduled from both `onclose` and `onerror`, which can double the attempts at every cycle while the device is away |
| U-03 | (2026-11-12: a request that got no answer says so, "the device did not answer" or "cannot be reached", instead of a raw browser error; the 8 s limit stays, and the like, the longest inline wait, now takes 5 s at most) Every request aborts at 8 s; slow handlers (format, like, a large upload) are reported as failures while the device carries on |
| U-04 | Without a card the Playsets page says "No playsets yet. Create your first mix!" and fails only on save; upload, new folder and format stay enabled; the banner is on Home only |
| U-05 | (partly fixed 2026-11-12: the heart follows the like's outcome, also a queued one; the active playset is still set on the POST's success) Optimistic state: `liked` always starts false; the active playset is set on the POST's success though activation is asynchronous |

## D. The specification and the code disagree

| # | The specification says | The code does | To do |
|---|---|---|---|
| S-01 | 5.4: the memory cache evicts least recently played | first-in first-out | D7 rewrites this |
| S-02 | 14: card failures latch a "card failed" state; a swapped card is picked up on the next mount attempt | neither exists | D6: keep the latch, withdraw the remount |
| S-03 | (settled 2026-11-12: the specification and the code follow ADR 0018, no fallback) 5.2: an unrestorable playset falls back to Promoted, or Local without a network | Local with a card, Promoted without | decide the rule (no-card-mode.md, Q5) |
| S-04 | 5.4: history persistence is unavailable without a card (so it exists with one) | history is in memory only, always (4.6 agrees); `state/` is created and unused | fix the sentence |
| S-05 | 14, 16: configurable card root, downloads cap | F-16 | implement or remove |
| S-06 | 14: settings written double-buffered | one NVS blob (`settings.cpp:28-40`); NVS itself writes the new entry before retiring the old | reword, and test it (PWR-3) |
| S-07 | 15.5, 18.11: a panel stress test | does not exist in the product firmware | build it as a test hook, or drop the sentence |
| S-08 | 12 and architecture 8: the WebSocket carries the live preview; 3.7: pushed at 2 to 4 fps | the pages poll `GET /api/v1/frame` once a second | reword or implement |
| S-09 | architecture 6: memory cache of at most 8 MB | 6 MB | align with the cardless design |
| S-10 | 18.9: 60 fps, under 30 ms, silence after 5 s | `stream_smoke.py` checks 30 fps, 50 ms, 1.5 s | test what the criterion says |
| S-11 | 14: nothing is deleted because a read failed | F-15 | fix |

## E. Defects in the tests

| # | Defect | Evidence |
|---|---|---|
| T-01 | (fixed 2026-11-11, commit 9a9dbb0) `intro_smoke.py` cannot fail: it reads `api_smoke.FAILURES`, the counter is `failures` | `tests/device/intro_smoke.py:100-101`, `api_smoke.py:24` (source) |
| T-02 | (fixed 2026-11-11, commit 9a9dbb0: a missing precondition prints SKIP and exits 2) False passes: `cache_sweep_smoke.py` passes with no card, `tls_slot_smoke.py` passes unpaired | audit (`:112-114`, `:31-33`) |
| T-03 | (fixed 2026-11-11, commit 9a9dbb0: the limit is the budget's and the poller is named for what it is; a soak with a WebSocket and commands is LONG-1) `soak.py` hard-codes its late-flip limit instead of reading `budgets.json`; its "WsLoad" opens no WebSocket and sends no command | audit (`soak.py:105`) |
| T-04 | (2026-11-11: the suite's oracle now reports any setting or playset a test leaves changed and puts it back; `stream_smoke.py` fixed. The others still restore only on the happy path) Scripts leave the device changed: `content_smoke.py` (auto-swap left at 30), `stream_smoke.py` (both listeners left on), `imu_smoke.py` (calibration rewritten), `pin_smoke.py` (a PIN left set when aborted), `ota_smoke.py` (the other slot when aborted); most restore only on the happy path | audit |
| T-05 | (fixed 2026-11-11: `run_suite.py`, `harness.py`) No suite: 20 scripts, no runner, no shared fixture, no report; nothing device-side gates a release | audit; review item P-T5 was never done |
| T-06 | The scaler's Python twin in `tests/host/run.py` still encodes the fractional downscale that ADR 0016 retired; latent because no corpus canvas is a non-power-of-two above 64 px | audit (`run.py:301-304`) |
| T-07 | (2026-11-11: `release.yml` now runs the host tests and needs them to publish; the runtime budgets are checked by the oracle around every device test) `release.yml` runs no host tests; CI checks 2 of the 10 keys of `budgets.json` | audit |
| T-08 | `firmware/docs/testing.md` omitted `tls_slot_smoke.py` (closed 2026-11-11: the row was added with this register) | audit |
| T-09 | (closed by F-03's fix; `hooks_smoke.py` panics the device on purpose and reads the status strictly) Every Python device test parses the status strictly and dies after a panic (F-03) | device |
| T-10 | (2026-11-12: the remaining flake was the check itself: it demanded zero late frames at 60 fps, where a rare one is the known limit of Quality mode; one run in five showed one late frame in 1200 with the Makapix worker idle. One is allowed at 60 fps now and the test waits for an idle worker; the fps reading from two requests is unchanged) `timing_smoke.py` is flaky (one pass in four on 2026-10-04): two non-simultaneous HTTP reads | PROGRESS.md |
| T-11 | The probes of this session changed a setting by accident: a clamp test left brightness at 1 until restored by hand. Any test that writes a setting must snapshot and restore the whole document | this session |

## F. Never run, though the acceptance list asks

From the coverage audit; details in [test-catalogue.md](test-catalogue.md) and
[release-gates.md](release-gates.md).

- 18.1 and 18.11: the 12 h and 24 h soaks (longest instrumented run: 55 minutes).
- 18.2: the corpus played on the device with the late counters read.
- 18.8: setup mode within 60 s with wrong credentials; the access point gone within 5 s.
- 18.10: pairing timings.
- 18.12: power pulled mid-download; a corrupt cached file on the device.
- Factory reset (by API and by BOOT), the automatic rollback of an image that fails to
  boot, the captive portal on a phone, real taps, certificate renewal, a full card,
  format, DST transitions.
