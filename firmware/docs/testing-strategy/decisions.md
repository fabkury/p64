# Decisions of 2026-11-11

[Strategy](README.md) · [Findings](findings.md)

The record of what the owner decided on 2026-11-11, first with the strategy (D1 to D8)
and then in a question-by-question review of every open point (D9 onward). Where a
chapter of this folder still shows a question with its options, this page says how it
was answered; the options stay there as the record of what was weighed. The two
decisions that are hard to reverse have ADRs:
[0018](../../../docs/adr/0018-cardless-is-a-storage-mode-decided-at-boot.md) (cardless)
and [0019](../../../docs/adr/0019-one-http-task-with-a-stated-promise.md) (the web
server). The words are in the [glossary](../../../CONTEXT.md). Nothing here is
implemented yet; the specification changes with the code, as its own rule says.
(Since then: D25 to D30 and D32 are implemented; of cardless, by 2026-11-12, the memory
window (D7, D16, D23, D24 in part), the playset rule (D13 to D15), the one status screen
(D17, D18), "waiting for the time" (D22) and device playsets (D9, D11, D12). Uploads as play-this
(D21), the pages' cardless side and the one `409 NO_CARD` (D24) followed the same day.
The card-failed latch (D19), formatting from the pages (D20) and the four-page limit (D27)
followed on the same day: every cardless decision is implemented.)

## The frame

| # | Decision |
|---|---|
| D1 | 1.0.0 is stranger-proof; deliberate attack from the LAN is not the bar. |
| D2 | The p64b on USB-C and on the LAN is the test unit and may be reset, erased and perturbed. |
| D3 | Minimal hands: one hands-on session of at most 30 minutes for 1.0.0. |
| D4 | Fault-injection hooks, compiled out of releases. |
| D5 | Cardless is a first-class mode. |
| D6 | The card is changed with the device unpowered; removal or failure while running must be survivable. |
| D7 | The cardless feed renews one artwork per swap. |
| D8 | The first session delivered documents and probes, no fixes. |

## Cardless (ADR 0018)

| # | Question | Decision |
|---|---|---|
| D9 | Where cardless data lives ([Q1](no-card-mode.md#q1-where-cardless-data-lives)) | **NVS only.** Measured the same day: about 14 KB of the partition's usable 60 KB is in use on a paired unit; a real playset is about 100 bytes a channel. The internal 15.5 MB partition stays unused. |
| D10 | What of cardless is in 1.0.0 | The safe core (the memory window, truthful pages, the playset rule, the card-failed latch) and device playsets gate 1.0.0. Kept uploads and a cardless Local channel are not in 1.0.0. |
| D11 | Two playset stores (Q9) | **Separate lists**: card playsets while a card is mounted, device playsets while cardless; nothing moves between them. |
| D12 | The limit of device playsets | **8 playsets of up to 16 channels**, none of them local. |
| D13 | The remembered playset cannot play (Q5) | **"No artwork" with the reason.** The device never plays a playset the user did not choose. (Today a card playset falls back to Promoted; that ends.) |
| D14 | The active playset | **One per storage mode.** A card change returns the choice last made in that mode. |
| D15 | A device on which nothing was ever chosen | **Promoted**, with or without a card. |
| D16 | The pace of the window (Q4) | One new artwork per swap, **at most 60 an hour**; the first cardless soak measures and decides whether the ceiling becomes a setting. |
| D17 | The panel while nothing can play yet (Q7) | The status screen **at once**, with the true reason. |
| D18 | Which status screen | **One screen**: "No artwork", its reason, and under it the hostname and address when the device is on a network. It replaces today's "No artwork" and "Connected" screens everywhere; "Connected" is retired. |
| D19 | The card stops answering (Q8) | The latch trips, a banner says so, and the device **goes on as cardless**: provider channels of the running playset continue from the memory window, card-bound channels are skipped; a later pick is remembered as the cardless choice. |
| D20 | A card without a FAT32 file system (Q8, F-29) | **Format from the pages** is implemented (the card is initialised without a mount; the usual confirmation). After a successful format **the device restarts**. Until then it is cardless and says "Card not FAT32". |
| D21 | Uploads without a card (Q2) | **One play-this from memory**, not kept; the page says so; the file manager is hidden. |
| D22 | No trusted time (Q6) | The reason **"waiting for the time"**, never "offline" while connected; the device keeps waiting. ADR 0011 stays whole. |
| D23 | Who owns the memory window (Q11) | **The content layer**, one window shared by every provider through the provider interface. |
| D24 | The proposed internals (Q3, Q10) | Accepted as proposed: 48 files and 6 MB until the PSRAM budget is measured; file bytes shared, never copied; the 8 most recent history items protected from eviction; play-this files share the window; listings walked only as far as needed; one answer for "needs a card" (`409 NO_CARD`); the status document gains the storage mode and the window's figures; pages disable card-bound controls before the click. |

Words: **cardless**, **storage mode**, **memory window** (not "memory cache" or "rolling
window"), **card failed**, **card playset**, **device playset**.

## The web server and the API (ADR 0019)

| # | Question | Decision |
|---|---|---|
| D25 | What a second client is owed ([Q1](web-ui-and-api.md#q1-one-stalled-client-freezes-every-other)) | **Answered within 5 s, except during an upload** (3 s at first; changed to 5 s by the owner when the work began, 2026-11-11, with the like waiting up to 5 s for the worker before it is queued with a 202: a like was measured at about 3 s, not the "under a second" first assumed). The single HTTP task stays; timeouts shorten, refused bodies are not drained, a like is queued. A legitimate upload holds the server (about 18 s at most) and the pages say so. |
| D26 | A command the device cannot take ([Q2](web-ui-and-api.md#q2-the-answer-when-a-command-cannot-be-taken)) | **Coalesce, else 503.** Navigation is "latest wins"; internal results have their own path and are never dropped; a load that does not come back is retried by the show; anything else answers 503 with Retry-After. |
| D27 | Open pages (Q3) | **Four** are supported; beyond that the page idle longest loses its WebSocket and polls. |
| D28 | Malformed requests (Q4) | A known settings key with a value of the wrong type answers **400**; unknown keys stay ignored; out-of-range numbers stay clamped. A JSON nesting limit of 16. Errors in the API's envelope where ESP-IDF allows. |
| D29 | LAN trust (Q5) | State-changing requests with a **foreign Origin are refused**; `/setup/save` and `/setup/erase` answer **only in setup mode**; the PIN's global lockout stays, documented, and does not survive a restart. |
| D30 | Settings writes and page rules (Q6, Q7) | Accepted as proposed: a write that changes nothing writes nothing; bursts coalesce into one NVS write; applied equals stored after the last write; controls are disabled or coalesce in flight; one poll at a time; the reconnect backs off; long actions answer 202. |
| D31 | New Wi-Fi credentials that are wrong (F-23) | (2026-11-12: no page offers a change of network; Settings only erases it, with its confirmation. The route `POST /api/v1/wifi` is the API's alone, and its reference carries the warning.) **As today, with a warning first**: the page says the device will leave the network and that a mistake means reconnecting through `p64-setup`. |

Implemented on 2026-11-11, the firmware side (D25 to D30 except the pages' own rules,
which with D31 belong to the pages): see [findings.md](findings.md) for what each closed.

## The specification and the code

| # | Divergence | Decision |
|---|---|---|
| D32 | The card root and downloads cap settings (F-16) | (done 2026-11-12) **Removed**: the root is `/p64`, the downloads folder is bounded by the cache sweep; the keys are ignored on input. |
| D33 | The live preview (S-08) | **Polling at 1 fps**; the specification and the architecture are corrected. |
| D34 | The panel stress test (S-07) | **A test hook only**; it leaves the product's diagnostics list and 18.11 names the test build. |
| D35 | Housekeeping | Stray temporary files are removed at mount; two units with one hostname follow the mDNS library's renaming, checked once and documented. |

## How the work is run

| # | Question | Decision |
|---|---|---|
| D36 | The path of versions | **0.2.2 at once**, then everything else through 0.9.x release candidates to 1.0.0. |
| D37 | What 0.2.2 carries | F-01, F-02, F-03, F-10 and **F-14**, each with its test, and the device tests that cannot fail repaired (T-01 to T-03). |
| D38 | What 0.2.2 must pass | The host tests; a regression script made from the probes (F-01, F-02, F-03, F-14, and F-10 if a seam allows); the existing smokes (api, content, ws, ui, pin, ops, makapix); a one-hour soak; all on the public image on the p64b; then the p64a updated over the air from the published release. |
| D39 | The order after 0.2.2 | The harness; the web hardening; cardless; faults and recovery; the long runs and the release candidates. |
| D40 | The test unit's image | The **public tree's test build, for the duration**; pairing and Wi-Fi kept through NVS snapshots; back to the private build after 1.0.0. |
| D41 | The unit in daily use | Read by a **script run by hand** that appends to a git-ignored log; no scheduled job. |
| D42 | The infrastructure | **As proposed** in [infrastructure.md](infrastructure.md), the browser lane and the fake Makapix with its MQTT broker included. |
| D43 | Purchases | **None for now.** The setup portal's join, the BOOT hold and a real card pull stay in the hands-on session; power cuts are stood in for by abrupt resets. |
| D44 | Instrument measurements | **None gates 1.0.0.** The camera and power claims stay "unmeasured" in `docs/status.md`. |
| D45 | ADRs | 0018 and 0019, written the same day. |

## What these decisions change in the chapters

- [no-card-mode.md](no-card-mode.md): section 3's recommendations are answered above,
  and three of them differently from what was recommended (D9 NVS instead of the hybrid,
  D11 separate lists instead of a move, D13 no fallback instead of Promoted meanwhile).
  The test plan's expected results follow this page.
- [web-ui-and-api.md](web-ui-and-api.md): Q1 was answered with option A alone (D25), so
  WEB-D7's uploads are exempt from the 3 s target and the pages must say "uploading".
- [release-gates.md](release-gates.md): the suggested path of versions is replaced by
  D36; no instrument gates (D44); the hands-on session gains "format an exFAT card from
  the pages" and keeps the portal and BOOT checks (D43).
- [test-catalogue.md](test-catalogue.md): FIRST-3's "a working network is never lost to
  a typo" is not required (D31); the warning is.
