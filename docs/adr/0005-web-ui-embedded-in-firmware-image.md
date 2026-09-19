---
status: accepted
date: 2026-09-19
---

# The web UI ships inside the firmware image, not in its own partition

p3a keeps its web UI in a 4 MB LittleFS partition with its own over-the-air update,
version number, repair page and surrogate UI for when the partition is corrupt. p64
embeds the web UI files in the application image: one version number, one update, no
partition health machinery, no way for UI and firmware to disagree. The two 8 MB
application slots on the 32 MB flash have room (p3a's UI is about 1.5 MB with PICO-8,
under 1 MB without it).

Why: a separate UI partition earns its complexity when the UI changes far more often than
the firmware or is large; p64's UI is small and every UI change ships with a firmware
release anyway. The trade-off accepted: a UI-only fix needs a full firmware release.
