---
status: accepted
date: 2026-10-02
---

# Quality mode sends eleven bit planes at 264 Hz, with binary low windows

Quality mode is 11 bit planes at the 250 Hz minimum refresh: transition bit 5, 37
transmissions per row, 263.9 Hz at the 20 MHz pixel clock. The output-enable windows of
the six planes sent once are rounded to the nearest clock (1 2 4 8 16 31) instead of
truncated, so the planes weigh 1 2 4 8 16 31 62 124 248 496 992 clocks: binary from one
clock, 1984 clocks of light per frame (Quality's 1979 before). The gamma 2.2 fit gets
243 distinct codes per channel, 51 of them in the darkest quarter of the inputs (0..63).
Photo mode is unchanged (8 planes, 814 Hz, truncated windows).

## Context

Until this decision Quality was 10 planes at transition bit 4 (271.3 Hz): windows 1 3 7
15 31 62, 233 codes, 41 in the darkest quarter, inputs 0..5 black. The shortest
output-enable pulse is already one pixel clock (50 ns), so depth can only grow by adding
planes, and every plane costs 8 KB of internal DMA memory (two row buffers of 4 KB);
doubling the top plane's time instead also doubles the transmissions per row, so a
refresh below 250 Hz buys depth only together with more descriptor memory. The host
model of the profiles (prompts p065 and p068) gave:

| Profile | Refresh | Codes | Dark quarter | Internal RAM over 10 planes |
|---|---|---|---|---|
| 10 planes, bit 4 (until now) | 271 Hz | 233 | 41 | 0 |
| 11 planes, bit 5, rounded low windows (chosen) | 264 Hz | 243 | 51 | +9 KB |
| 11 planes, bit 4 | 144 Hz | 243 | 51 | +32 KB |
| 12 planes, bit 5 | 141 Hz | 249 | 57 | +41 KB |
| 10 planes, bit 3 | 146 Hz | 234 | 42 | +23 KB |

The eleventh plane had been dropped on the morning of 2026-10-02 because the largest
free internal block was already at its 32 KB floor. The user then asked to see the
difference on the panel (p069): a trial mode switched in place beside Quality showed
dark grey and colour ramps and a dark gradient side by side, and the user judged the
eleven-plane picture better.

## Decision

- Quality is 11 planes, rounded low windows, 250 Hz minimum; the sdkconfig depth is 11.
- The internal heap's largest-block floor in `firmware/budgets.json` goes from 32 KB to
  28 KB (one TLS session needs 12 to 13 KB, so 28 KB still holds one with room), and the
  drift of the largest block since the review of 2026-09-22 (45 KB then, about 33 KB
  before this change) is investigated separately.

## Consequences

- 8 KB more internal DMA memory for the row buffers (two descriptor chains grow by 0.8
  KB together); the floors are lowered deliberately, not silently.
- Refresh 263.9 Hz instead of 271.3 Hz: a frame lands on a 3.8 ms grid instead of 3.7 ms,
  and a photograph needs an exposure of 1/264 s or longer (Photo mode is unchanged).
- The copy of a frame into the bit planes writes one plane more: 8.17 ms per frame on the
  device (about 7.5 ms with ten). The renderer's copy lead adapts on its own
  (`timing.hpp`); 25 and 10 fps artworks stay exact, and 60 fps (16 ms frames, the
  tightest case on core 1) holds its rate with an occasional late frame (one in two of
  four `timing_smoke` runs on 2026-10-02, none on 2026-09-29 with ten planes). A faster
  bit-plane copy would buy that margin back.
- The driver floor's share of the light is unchanged (128 of 1984 clocks against 127 of
  1979), so the brightness scale of ADR 0013 keeps its numbers.
- A twelfth plane (249 codes) would need a refresh of 141 Hz and about 41 KB more internal
  RAM, which the device does not have; the remaining gain is six codes, all below input 64.
