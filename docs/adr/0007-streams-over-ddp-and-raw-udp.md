---
status: accepted
date: 2026-09-19
---

# Pixel streams arrive over DDP and a raw p64 UDP format; no WebSocket stream

p64 accepts pixel streams over UDP only: DDP (port 4048, the format LedFx, xLights and
WLED tooling already speak) and a raw p64 format (port 4064: RGB888, RGB565 or 8-bit
indexed with a palette, any size up to 128x128, chunked by byte offset). p3a's only
stream is a WebSocket carrying PICO-8 frames; p64 does not implement it, so p3a's
PICO-8 browser page does not drive a p64.

Why: streams optimise for latency and compatibility; UDP has no connection state, no
head-of-line blocking and the widest tool support, and a raw format lets a ten-line
script push frames. A browser cannot send UDP, so browser-originated streams (PICO-8,
canvases) are left for a later WebSocket bridge if wanted; the trade-off is accepted.
Art-Net/E1.31 was left out as redundant with DDP.
