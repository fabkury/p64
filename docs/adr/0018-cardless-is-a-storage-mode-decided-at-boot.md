---
status: accepted (decided 2026-11-11; not implemented yet)
date: 2026-11-11
---

# Cardless is a storage mode, decided at boot

Supersedes ADR 0006's "degraded mode" (its FAT32-only rule and "never format
automatically" stand). A p64 without a mounted card is a supported way to own one, not a
fallback: firmware 1.0.0 is meant for strangers who build the device from the parts list,
and many will start without a card. The storage mode, card or cardless, is decided once
at boot and afterwards changes only when the card fails.

What the mode means:

- **No probing while running.** There is no card-detect line. A card inserted while the
  device runs is ignored until the next boot; the card is changed with the device
  unpowered, and the guides say so. A card that stops answering is a card failed: the
  device goes on cardless, says so, and uses the card again only after a restart. The
  remount the specification promised (section 14) is withdrawn. A card without a FAT32
  file system leaves the device cardless with the true reason shown; the pages can
  format it after the usual confirmation, and the device restarts to mount it.
- **The memory window.** Channel artwork is kept in memory, bounded, shared by every
  content provider: filled once, then renewed by at most one new artwork per swap in
  place of the least recently played, up to 60 downloads an hour. A listing is walked
  only as far as the window needs.
- **Device playsets.** User playsets for cardless use are kept in NVS: up to 8, of up to
  16 channels each, none of them local. The card's and the device's lists are separate
  and nothing moves between them. One active playset is remembered per storage mode.
- **No automatic fallback.** When the remembered playset cannot play, the panel says
  why ("No artwork" with the reason, and the hostname and address when on a network);
  the device never plays a playset the user did not choose. The one default: a device
  on which nothing was ever chosen plays Promoted.
- **Uploads** without a card play once from memory (a play-this) and are not kept.

Considered and rejected:

- *The unused 15.5 MB internal data partition* as a small card, or as the home of
  playsets, indexes and kept uploads. It would have given offline playback after a
  restart and one code path, at the price of a file system on internal flash whose
  writes stall code running from flash, unmeasured against the panel. NVS had about
  45 KB free on a paired device (measured 2026-11-11) and a real playset is about 100
  bytes a channel, so NVS holds 8 x 16 with room to spare and needs nothing new.
- *Moving or mirroring playsets between the card and the device.* Separate lists have no
  sync rule to get wrong; the per-mode active playset removes the cost of a card change.
- *Remounting on request or on a timer.* It needs probing an empty slot and a run-time
  switch into card mode with its own transitions; a restart is one sentence in the guide.
- *Falling back to Promoted* when the chosen playset cannot play. Friendlier to a
  stranger, but the device would show artwork its owner did not ask for.
- *One new artwork per swap with no ceiling* (2880 downloads a day at the default
  interval, from the owner's own server and the user's connection).

Consequences: a channel's index is not kept across a cardless restart, so the listing
is fetched again at each boot; kept uploads and a cardless Local channel do not exist
(a candidate for a later version, with the internal partition); the worst-case PSRAM of
cardless operation must be bounded by design and tested, since a failed allocation
aborts; `firmware/docs/testing-strategy/no-card-mode.md` holds the questions as they
were weighed and the test plan.
