# p64 changes to esp-hub75

Base: esphome-libs/esp-hub75 0.3.6 (component registry, 2026-09). Search the sources for
`p64 patch` to find every modified spot. Only the ESP32-S3 (GDMA) backend is changed;
the I2S and PARLIO backends keep upstream behaviour (they inherit the new getters'
defaults).

## Binary-weighted low bit planes (gdma_dma.cpp, set_brightness_oe_internal)

Upstream meets `HUB75_MIN_REFRESH_RATE` by sending the bit planes at or below
`lsbMsbTransitionBit` once each while keeping every plane's output-enable window the
same. Those planes then all weigh one transmission, so the number of distinct brightness
levels collapses to transmissions-per-row + 1 (8 bits forced to 150 Hz gave 66 levels).
The patch gives each such plane half the output-enable window of the plane above it,
which restores its binary weight: the refresh gain of the shortened chain is kept and
the panel keeps 2^bits levels. The shortest window is the one of plane 0:
`(63 * brightness/255) >> (transition + 1)` pixel clocks; at 20 MHz, 10 bits and
transition 3 that is 3 clocks = 150 ns.

## LUT fitted to the real on-times (gdma_dma.cpp, fit_lut_to_weights)

Integer windows and latch blanking make the plane weights only approximately binary, so
after every brightness change the 256-entry LUT is refitted: each input maps to the code
whose summed on-time is nearest the gamma table's target. This replaces upstream's
`adjust_lut_for_bcm()` monotonicity nudge, which is no longer called. Lowering the
brightness shortens every window, so the low planes lose resolution first; run the panel
near full brightness for the best gradation and dim in software if needed.

## HUB75_MIN_REFRESH_RATE range (Kconfig)

Raised from 30-240 to 30-2000 Hz: with binary-weighted low planes a high minimum is a
usable setting (p64 runs 250, which selects transition bit 4 and 271 Hz on a 64x64
panel at 20 MHz).

## Timing getters (hub75.h, platform_dma.h, gdma_dma.h/.cpp)

`Hub75Driver::get_frame_period_us()`, `get_descriptor_count()` and
`get_lsb_msb_transition_bit()` expose the refresh period, descriptors per frame and the
transition bit, which p64's frame-locked rendering needs (it used to assume full binary
code modulation).

## Gamma 2.2 table generator fixed (color_lut.h, constexpr_pow22)

Upstream computes x^2.2 with a four-term log series that only holds near x = 1 and a
short exp series: for x = 0 it returned 1 (black mapped to full white) and the table was
not monotonic, so the library's own static checks rejected `HUB75_GAMMA_2_2` at any bit
depth. The table now uses x^2 times the fifth root of x by Newton's method, exact at the
endpoints and monotonic.
