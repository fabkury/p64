---
status: accepted
date: 2026-09-21
---

# The artwork cache is bounded by age, not by space: a nightly sweep deletes what was not played for the retention period

Cached Makapix artworks, URL downloads and channel indexes are deleted once a night,
at the night schedule's start, when their file's modification time is older than the
cache retention setting (default 30 days). The loader touches a file's mtime whenever it
reads it for the show, so mtime means "last played" (a fresh download counts as played).
A file goes even when a channel of the active playset still lists it: the sweep clears
the entry's cached flag and the download loop fetches the artwork again when the channel
needs it. The sweep does not run while the night schedule is off or the time is not
trusted, and a missed night is simply skipped; nothing is persisted about it. (Amended
2026-09-23 by ADR 0011: the time is trusted only from NTP, a file date below the file
floor counts as written under an untrusted clock, and a file dated more than a day in the
future stops the whole sweep instead of being deleted.)

Why: the spec's original policy (least-recently-played eviction only below a free-space
watermark) needs an ordering across every cached file and behaves differently on every
card size, while the cards used are far larger than any playset (tens of megabytes of
artworks on a 32 GB card). A plain age is deterministic, needs no bookkeeping beyond the
file system's own timestamps, and turns the card into a mirror of what the device
actually played recently.

Considered and rejected: exempting files the active playset still lists (the age would
then only be a grace period for orphans, and never-played entries of an active channel
would live forever); a reference sweep against every index on the card (keeps whatever
any departed channel ever listed); a catch-up sweep after boot (needs a persisted last
run and surprises a user who powers the device on).

Consequences: entries a channel lists but never plays (recency pick mode, very large
playsets) are downloaded again every retention period; the download loop refetches them
right after the sweep, at night. Indexes of channels that come back after the retention
period may still flag files as cached; those entries fail once at playback, which clears
the flag, and download again.
