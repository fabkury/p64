---
status: accepted
date: 2026-09-19
---

# Text is drawn from build-time bitmap fonts; no uGFX, no TrueType on the device

p3a renders its on-screen text with uGFX and DejaVu fonts. p64 does not: uGFX carries a
conditional licence (free for open source, commercial licence otherwise) that p3a's own
`LICENSING.md` lists as a liability, and a 64x64 panel wants pixel fonts drawn for it,
not anti-aliased vector text. p64 bundles pixel fonts (Capital Hill 6 px and Everyday
Typical 7 px by VEXED, CC-BY 4.0, two more planned), rasterised at build time from the
TTFs into bitmap glyph tables, and draws them with its own small renderer (integer scale,
optional 1 px outline). Attribution is shown in the web UI's About section as the licence
requires.
