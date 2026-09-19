---
status: accepted
date: 2026-09-19
---

# The microSD card is optional and FAT32 only; formatting is never automatic

p3a requires a card and halts on a fatal screen without one. p64 runs without a card in a
degraded mode (Makapix and URL artworks in a memory cache, no local channels, no
persistence beyond settings) with a persistent notice in the web UI, because the ESP32-S3
board's 16 MB PSRAM makes that mode useful and a product should not die for a missing
accessory. Cards must be FAT32: exFAT was evaluated and rejected on p3a
(`firmware/reference/p3a/docs/exfat-support-evaluation.md`) and nothing here changes
that. A card without a usable file system is reported, never formatted automatically
(the hardware tests auto-formatted; a product must not risk a user's data); formatting
is an explicit, confirmed action in the web UI.
