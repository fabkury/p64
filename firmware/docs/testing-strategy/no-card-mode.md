# Cardless operation: design gaps and test plan

[Strategy](README.md) · [Findings](findings.md) · 2026-11-11

The owner's question was whether running without a microSD card is merely untested or
also undesigned. It is both. The specification gives it four sentences (5.2, 5.4, 14,
ADR 0006); the code implements a first attempt of them that no test has ever run on
purpose; and most of what a user would do without a card has no defined answer. This
chapter lists what happens today, what was decided on 2026-11-11, what still has to be
decided, and how the result will be tested.

## 1. What happens today

From the code audit (source checked by hand where marked in
[findings.md](findings.md)). Nothing here was run without a card yet: the test unit has
one, and the first cardless session is part of the work.

| Area | Today, without a card |
|---|---|
| Mount | One attempt at boot. No retry, no remount route, no detection of a card that stops answering later (F-12). The reasons are "no card answers", "no FAT file system on the card" or an error name; Home shows "No card" for all of them. |
| Makapix channels | Indexes live in RAM only, so every active channel is walked from the server again at every boot (pages of 50, up to the channel cache size, 2048 by default). Promoted works unpaired. |
| The memory cache | 48 files or 6 MB in PSRAM, first-in first-out, never refuses a file. The downloader does not stop when it is full: it downloads the whole listing and keeps the last 48; evicted entries stay marked as cached, so later picks miss and re-download (F-11). |
| Play-this by URL or post, Makapix "show artwork" | Works; the file goes into the same 48 slots under a `dl-` key and evicts channel files. |
| User playsets | Listing answers 200 with an empty list and `"error":"no card"`; reading 404; saving and renaming 503; activating 404. The Playsets page still says "Create your first mix!" and fails on save (U-04). |
| The saved playset at boot | The name is in NVS. A saved user playset falls back to Promoted (the name is kept); a saved Local shows "No artwork: no card" forever, even online (F-13). |
| Local, the file manager, uploads | Local is disabled with the reason "no card". The file routes answer with five different codes for the one cause (503, 404, 500, 409, 422). Upload, new folder and format stay enabled in the pages and fail after the click. |
| Format | Impossible for a card without a file system: format needs a mounted card (F-29). |
| History, previous | In memory, 32 items. An item whose file was evicted is dropped when revisited and the walk continues. A failed play-this keeps the current picture and says nothing. |
| No Wi-Fi | Never configured: the setup pages hold the panel. Configured but down: the setup pages replace "no artwork". |
| Wi-Fi up, time not yet known | Nothing can be listed or downloaded (the device counts itself offline until NTP answers); the panel and the pages say "offline" while the network page says connected (F-26). With a card the cache plays meanwhile; without one nothing ever does if NTP is blocked. |
| Widgets, interludes, streams, pairing, likes, MQTT commands, OTA, settings, the PIN | No dependency on the card found. The nightly cache sweep is skipped. |
| What the user is told | A banner on Home; a red line in Settings > Storage. The status document has no figures for the memory cache. |
| Tests | Three host assertions on wording. The show core's fake environment hard-codes a mounted card. No device test targets cardless operation; several pass or skip silently without a card, and `makapix_smoke.py` filters history on `cache/` paths, so its play-this step does nothing with `mem:` paths. |

## 2. Decided on 2026-11-11

- **D5. Cardless is a first-class mode.** A p64 without a card is a supported way to own
  one: the Makapix playsets, user playsets, play-this, uploads, widgets and streams all
  work; what cannot work says so before the click.
- **D6. The card is changed with the device unpowered.** There is no card-detect line,
  and the firmware will not probe for one. A card inserted while running is ignored
  until the next boot. A card removed or failing while running must be survivable: no
  crash, no endless download loop, a banner that says what happened, and a reboot
  restores normal operation. The specification's remount promise (section 14) is
  withdrawn; its "card failed" latch stays, since the banner and the end of the loop
  need it.
- **D7. The feed is a rolling window.** The memory cache is filled once; after that at
  most one new artwork per swap replaces the least recently played.

ADR 0006 ("a degraded mode") is superseded by a new ADR once the questions below are
settled.

## 3. Open design questions

**All settled on 2026-11-11**: the answers are D9 to D24 of [decisions.md](decisions.md)
and ADR 0018. Three went against the recommendation written here: cardless playsets
live in NVS (Q1), the two playset lists are separate with no migration (Q9), and a
playset that cannot play shows "No artwork" with its reason instead of Promoted (Q5).
The text below is kept as the record of what was weighed.

### Q1. Where cardless data lives

"First-class" needs somewhere to keep user playsets (and, depending on Q2, uploads)
across reboots. Facts: the NVS partition is 64 KB and already holds the settings
document, the Wi-Fi data and the Makapix credentials (a certificate, a key and a CA);
its free space has not been measured. A playset is about 1 KB with six channels and up
to roughly 10 KB with the maximum of 64. The partition table also has an unused 15.5 MB
data partition (`storage`, "reserved for later use", `firmware/partitions.csv`), present
on every device flashed so far.

| Option | What | For | Against |
|---|---|---|---|
| A. NVS | Cardless playsets as NVS blobs under a byte budget (for example 16 KB in total); uploads only in memory (Q2) | no new file system; the owner's wording of D5 | a tight, shared budget (a full NVS endangers the settings); a second playset store with its own limits and its own bugs; two stores to reconcile when a card appears (Q9) |
| B. The internal partition as a small card | Mount a file system on `storage` and use it as the storage root whenever no card is mounted: playsets, indexes, uploads, even the artwork cache go through the code that already exists | one code path; everything works, only smaller; offline playback after a reboot | flash writes stall code that runs from flash; a cache that turns over every swap writes megabytes per hour (wear is acceptable on paper, the stalls are not known); needs a measured proof that the panel is not disturbed (principle 1) |
| C. Hybrid (recommended) | The internal partition holds what is written rarely: playsets, channel indexes, and uploads the user chooses to keep, under a cap (for example 8 MB). Feed artwork stays in PSRAM (the rolling window) | first-class in the full sense: playsets, a small Local channel and offline playback of the owner's own files with no card; no wear and no stalls from the feed; indexes survive a reboot, so no full re-walk at boot | a file system on internal flash is new code to trust; the stall of a rare write still has to be measured |

A further simplification worth weighing under B or C: make the internal partition the
one home of playsets for every device, card or not. Then a playset survives a card swap
and there is nothing to reconcile (Q9). The cost is compatibility with a rollback to an
older firmware, which reads playsets from the card; a copy written to the card when one
is present covers that for a release or two.

First step either way, half a day: measure the NVS statistics on a paired device, and
measure late flips while writing files of 1 KB, 50 KB and 1 MB to a file system on the
`storage` partition during a 60 fps animation. (One data point exists: 100 settings
writes in 3 s during the probes of 2026-11-11 added no late frame.)

### Q2. Uploads without a card

- Under A: "Play from... upload" plays the file from memory as a play-this (one
  auto-swap interval, or held when the interval is 0; replayable from history while it
  is still cached). The file manager needs a card and says so.
- Under B or C: the upload is kept in the internal `animations/` folder up to the cap,
  and Local is a real, small channel. The page shows the space left.

Recommended: follow Q1. Under A, word the page honestly ("played now, not kept: no
card").

### Q3. The rolling window, precisely

Proposed rules, to be turned into specification text and a pure, host-tested policy:

1. **One copy.** A cached file's bytes are shared with the player and the loader, never
   copied (today every read copies the file out of the cache, and a store copies it in).
2. **Capacity** in files and bytes, sized from the PSRAM budget in section 4.
3. **First fill**: round-robin across the active playset's channels by weight until the
   cache is full or the channels are exhausted. The first artwork plays as soon as the
   first file lands.
4. **Steady state**: after each swap, at most one download, of the next entry not yet
   seen in a channel chosen by the scheduler's weights; it replaces the least recently
   played file that is not current, prepared or protected (rule 7). One download in
   flight at a time; none while paused.
5. **Truthful flags**: an entry counts as available only while its file is in the cache.
   Eviction clears the flag in the same step (the phantom entries of F-11 cannot exist).
6. **Walk on demand**: the listing is walked only as far as the window needs (the first
   page at boot, further pages as unseen entries run out, wrapping at the end; a refresh
   restarts from the newest). No 2048-entry walk at every boot.
7. **History**: the most recent items (proposed: 8) are protected from eviction so that
   "previous" is immediate; older history items whose file is gone are dropped when
   revisited, as section 4.6 already says.
8. **Play-this** files enter the same cache and count like any other.
9. **Offline**: no download, no eviction; the window loops on what it has.

### Q4. How fast the window turns

One new artwork per swap at the default 30 s interval is 120 downloads an hour, 2880 a
day, for as long as the device runs: by a rough guess of 30 to 100 KB per file, 100 to
300 MB a day per cardless device, where a device with a card downloads each artwork
once. That is the owner's server and the stranger's data plan. Options: (a) one per
swap, as decided; (b) one per swap up to a ceiling per hour (for example 60); (c) a
"fresh share": one new artwork every Nth swap, the rest replayed from the window.
Recommended: (b), with the ceiling a constant at first and the measured numbers of the
first cardless soak deciding whether it becomes a setting.

### Q5. When the saved playset cannot play

Today: a user playset falls back to Promoted, Local does not fall back (F-13), and the
choice between Promoted and Local follows the card where the specification says the
network (S-03). Proposed single rule: *the saved playset is never forgotten; when it has
nothing it can ever play in the present conditions (needs a card that is absent, needs a
pairing that is gone), the device plays Promoted meanwhile and says so in the now-playing
card; when Promoted cannot play either, "No artwork" with the reason.* Under Q1's
options B and C Local exists without a card and the first case mostly disappears.

### Q6. No trusted time

Without a card and without an NTP answer nothing can be listed or downloaded, so nothing
plays, indefinitely on a network that blocks NTP. Proposed: a distinct reason on the
panel and in the pages, "waiting for the time", never "offline" (the specification, 10.2,
already promises the pages say so); a line in the troubleshooting table. Whether the
anonymous listing and the plain-HTTP file downloads could run before the time is known
is a possible later relief, not proposed for 1.0.0 (certificate validation needs the
clock).

### Q7. From power-on to the first cardless artwork

With a card the first artwork is up within 2 s of the boot animation's end. Without one
it needs Wi-Fi, NTP, a listing and a download: an estimated 10 to 30 s, unmeasured. What
the panel shows meanwhile is not defined; today it is whatever the "connected" and "no
artwork" rules happen to produce. Options: hold the finished logo of the boot animation;
show the status screen "No artwork: downloading"; show the clock once the time is
known. Recommended: hold the logo until the first artwork or 30 s, then the status
screen with its reason; and a budget for "first artwork after NTP" in `budgets.json`
once measured.

### Q8. A card that stops answering (D6 in detail)

To define: what trips the latch (proposed: a write error, or three consecutive read
errors on different files); what the device does next (proposed: it behaves as cardless
from memory until the reboot, with the banner "The card stopped answering. Restart the
device to use it again."); what the pages disable; that local files already loaded keep
playing. And the unformatted card at boot: either the pages can format it (ADR 0006
promises it; it needs the card initialised without a mount, F-29) or the guide says to
format it on a computer. Recommended for 1.0.0: say it in the guide, show the true
reason on Home ("Card not FAT32"), and remove the format button when nothing is
mounted.

### Q9. A card that appears or disappears between boots

Under Q1's option A there are two playset stores. To define: which list the pages show
when a card with playsets is inserted into a device that has cardless playsets (both,
merged by name? the card's only?), and what happens to a cardless playset that names
Local folders. Under "one home for playsets" (Q1) the question disappears, which is the
main argument for it. In every option: a channel that needs the card, in a playset used
without one, is skipped with the status "needs a card" and the rest of the playset
plays.

### Q10. What the user is told

- One wording and one API answer for "this needs a card": proposed `409 NO_CARD`
  everywhere, in place of today's five codes.
- The pages disable what cannot work before the click, with the reason beside it.
- The banner says what cardless means: "No card: artwork is kept in memory and
  downloaded again after a restart."
- The status document gains `storage.mode` (`card`, `internal`, `memory`, `failed`) and
  the memory cache's figures (files, bytes, capacity, evictions, hits, misses), which the
  tests need as much as the user.

### Q11. Other providers

Every channel source reaches the show through `content::Provider`. The cardless
contract (one shared cache, truthful availability, at most one download per swap) must
be the interface's, not Makapix's own, and is tested once with the fake provider of the
host tests.

## 4. A budget for PSRAM

Cardless operation moves megabytes into PSRAM, and with C++ exceptions off a failed
allocation aborts (F-21). The design must bound the worst case and a test must drive it
there. Proposed bound for 16 MB (about 16.0 MB is free at rest today with a card):

| Item | Worst case | Rule that bounds it |
|---|---|---|
| The memory cache | 6 MB (to be sized) | capacity, rule 2 |
| One download in flight | 5 MB | one at a time (already so) |
| The current and the prepared artwork | 0 beyond the cache | shared bytes (rule 1) |
| An upload being received | 5 MB | never at the same time as a download, or a smaller cardless cap |
| Canvases, two artworks | about 1 MB | 256x256 limit |
| Channel indexes | 1 MB | a global index budget (64 bytes an entry; walk on demand) |
| Everything else (cJSON, the status, TLS buffers, stacks) | 1 MB | measured |
| **Sum** | **about 14 MB with the exclusion, 19 MB without** | |

The allocation points for multi-megabyte buffers also have to fail softly (refuse the
file, keep playing) instead of aborting.

## 5. Test plan

Three ways to be without a card, in order of use:

1. **The virtual eject** (test build, [infrastructure.md](infrastructure.md#test-hooks)):
   a hook that makes the next boot behave as if the mount had failed, with a chosen
   reason, and another that makes card I/O fail from now on. The card stays in the slot;
   no hand is needed; every cardless test runs in the normal suite.
2. **The host**: the memory cache, the window policy and the show core are pure or made
   pure, and run under the sanitizers in CI.
3. **The real thing**, once, in the hands-on session: an empty slot, an unformatted
   card, a card pulled while playing.

### Host tests

| ID | Test |
|---|---|
| NC-H1 | The memory cache as a pure model: least-recently-played eviction, the protected items, capacity in files and bytes, a file larger than the cache, shared ownership (a file being played is not freed by its eviction). |
| NC-H2 | The window policy: first fill by weight, one download per swap, none when paused or offline, wrap at the end of the listing, refresh restarts at the newest, the hourly ceiling (Q4). |
| NC-H3 | Show-core scenarios with the fake environment's card absent: the restore matrix (saved Promoted, Followed, Local, a user playset, nothing) against network and pairing; no phantom entries after evictions; "previous" across an eviction; the first artwork path; exactly one interlude roll per due swap when the next artwork is late (F-06). |
| NC-H4 | The playset store against the cardless backend: the caps, the name rules of ADR 0017, rename, a full store, a corrupt entry. |
| NC-H5 | The provider contract with the fake provider: availability is truthful under eviction. |

### Device tests (test build, virtual eject)

| ID | Test | Passes when |
|---|---|---|
| NC-D1 | Boot cardless on Promoted, unpaired and paired | first artwork within the Q7 budget; the panel sequence is the defined one; no walk beyond what the window needs |
| NC-D2 | 30 minutes of steady state at a 5 s interval | downloads per swap at most 1; no load failure; cache figures within capacity; distinct artworks per hour above a floor; heap and PSRAM floors hold |
| NC-D3 | Worst-case memory: a fake Makapix server serves 5 MB files back to back, with 5 MB play-this requests and a 5 MB upload on top | no reboot; PSRAM never below its floor; refused files are refused with a reason |
| NC-D4 | Every API route, cardless | the table of expected answers (one code for "needs a card"); no 500 |
| NC-D5 | Playsets cardless: create, edit, rename, delete, activate, reboot, the caps | they persist and play; the cap is refused with its own message |
| NC-D6 | Uploads cardless (per Q2) | the defined behaviour; nothing kept that was said to be transient, nothing lost that was said to be kept |
| NC-D7 | Play-this by URL and post; history replay; "previous" after an eviction | as defined in Q3 rule 7 |
| NC-D8 | Cardless with the time withheld (hook) | the panel and the pages say "waiting for the time"; playback starts when the time arrives |
| NC-D9 | Cardless offline: Wi-Fi dropped mid-run (hook), and a boot with Wi-Fi down | the window keeps looping; at boot the defined screen; recovery when Wi-Fi returns |
| NC-D10 | Pairing, likes, the site's commands (show artwork, play channel, play playset) cardless | as with a card |
| NC-D11 | The existing smokes that need no card (widgets, faces, streams, brightness, panel mode, ops, PIN, OTA, intro, WebSocket, pages), run again cardless | unchanged results; each script declares `card: required` or `any`, and the suite runs the `any` ones in both modes |
| NC-D12 | The card stops answering while playing a local file, during an upload, during a download, during an index save, during a sweep (hook: card I/O fails from now on) | the latch trips; the banner appears; no more than one failed download per swap; the show goes on as defined in Q8; a reboot restores the card |
| NC-D13 | The pages in a browser, cardless (the browser lane) | no enabled control leads to an error; the banner and the Storage tab say what is defined in Q10 |
| NC-D14 | Cardless soak, 12 h, Promoted and a mixed playset | no reboot, no stall; heap and PSRAM trends flat; downloads, bytes and distinct artworks recorded (they answer Q4) |

### The state matrix

The combinations that define "what does the panel show": card (present, absent,
unformatted, failed while running) x Wi-Fi (never set, set and down, up) x time (not
yet, known) x pairing (no, yes) x saved playset (Promoted, Followed, Local, a user
playset). That is 192 cells; the hooks make each reachable. The suite covers every pair
of values at least once (about 20 boots) and every cell of the rows below, whose
expected results are part of the definition work:

| Card | Wi-Fi | Time | Saved playset | Expected on the panel (proposed) |
|---|---|---|---|---|
| absent | never set | - | any | setup pages |
| absent | set, down | - | any | "No artwork: offline" at once; the setup pages after 60 s |
| absent | up | not yet | any | "No artwork: waiting for the time", with the hostname and address |
| absent | up | known | Promoted | "No artwork: downloading" with the address, then artwork within the budget to set |
| absent | up | known | Local | "No artwork: needs a card" (D13) |
| absent | up | known | a device playset | plays |
| absent | up | known | none ever chosen while cardless | Promoted (D15); a choice made with a card is remembered for that mode only (D14) |
| absent | up | known | Followed, unpaired | "No artwork: needs pairing" |
| unformatted | any | any | any | as absent, with the reason "Card not FAT32"; format from the pages, then a restart (D20) |
| failed while running | up | known | any | goes on as cardless: provider channels from the memory window, the banner (D19) |
| present | up | not yet | any | cached artwork plays (already so) |

### Hands-on, once (part of the 30-minute session)

| ID | Check |
|---|---|
| NC-P1 | Boot with the slot empty: the time the mount attempt takes (F-30), the reason shown, the boot sequence on the panel. |
| NC-P2 | Boot with an exFAT or unformatted card: the reason shown, no write to the card; then format it from the pages: the confirmation, the restart, the card mounted (D20). |
| NC-P3 | Pull the card while a local file plays and a download runs: what the real driver reports, against what the hook simulates. |
| NC-P4 | Insert a card while running: ignored; reboot: mounted, the saved playset back. |

## 6. Documents to change when this is settled

The specification (5.2, 5.4, 5.5, 6.4, 14, 15.1, the settings and limits tables), a new
ADR superseding 0006, `CONTEXT.md` (the words "cardless", "memory cache", "rolling
window"), the architecture (the memory table, the content chapter), the owner's guide
(the offline and cardless table, troubleshooting), the build guide (the card as an
option, with what it adds), and `docs/status.md` (the known limit on cardless playback).
