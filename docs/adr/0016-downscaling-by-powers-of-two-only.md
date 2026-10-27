---
status: accepted
date: 2026-10-27
---

# Artworks shrink only by powers of two

A canvas larger than the panel on either axis is box-averaged down by the fewest
halvings that fit both axes (1/2, 1/4, 1/8, ...), never by a fractional ratio; an odd
remainder is averaged into the edge pixel; the result is centred with bars. Enlarging
stays what it was (the largest integer factor, nearest neighbour). `gfx::Scaler` and spec
3.5 say so; the dotpict listing filter (private area) was written to the same rule first.

Why: the user's rule for p64 (2026-10-27), set while mapping dotpict, where canvases of
96, 100, 160 and 210 px are common: a fractional box (a 96 px canvas stretched over 64
panel pixels, each averaging 1.5 source pixels) blurs the pixel grid unevenly, some
panel pixels one source pixel, some two; a power of two keeps every panel pixel the same
block, so the art stays pixel art, smaller. The cost is accepted: a 96x96 shows at 48x48
with 8-pixel bars, a 100x100 at 50x50, where before they filled the panel.

Consequences: the acceptance test of spec 18 names the 96 px case; the artwork-size
settings (16 to 128 by default) bound the halvings to one; the streams (spec 7, raw p64
UDP up to 128x128) follow the same scaler and so halve 65 to 127 px frames too.
