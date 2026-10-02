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

The windows are computed once per plane and must be superincreasing (each plane weighs
at least the sum of the planes below it) for the LUT fit below to hold. Upstream's
one-pixel floor for planes that truncate to zero can break that: ten planes at
transition bit 6 gave planes 0, 1 and 2 one clock each, the fit's monotonic walk got
stuck at code 3 and every input above 13 mapped to it (a posterized, nearly black
picture: p64's first Photo mode, 2026-09-20). A plane that would weigh less than the
planes below it is now blanked instead (window 0).

## Each window in the buffer after its plane (2026-10-02, p64_bcm.h buffer_windows)

The panel's shift registers latch a buffer's data at its last clock, so while a buffer is
clocked out the LEDs show the data of the buffer sent before it, gated by the
output-enable bits of the buffer being sent (upstream relies on this when buffer 0
carries the previous row's address). Upstream gives every plane the same window, so the
lag costs it nothing. The halving windows above were written into their own plane's
buffer and so each lit the plane below it: the data weighed 3 7 15 31 62 62 124 248 496
931 clocks in Quality instead of 1 3 7 15 31 62 124 248 496 992, the darks came out two
to three times too bright and the light fell at every 32nd code (by half from input 51
to 52, 31 falls along the ramp; Photo had 7, the deepest at input 98). Found by reading
the code on 2026-10-02 and confirmed on the panel with four grey bands (48, 51, 52, 56:
the third at half the second). `p64bcm::buffer_windows()` now puts the window of plane b
into buffer b + 1 and the top plane's into buffer 0, and the LUT is fitted to
`p64bcm::data_weights()`, the time each plane's data is on the LEDs given the windows in
the buffers. Total light, refresh and memory are unchanged.

## LUT fitted to the real on-times (gdma_dma.cpp, fit_lut_to_weights)

Integer windows and latch blanking make the plane weights only approximately binary, so
after every brightness change the 256-entry LUT is refitted: each input maps to the code
whose summed on-time is nearest the gamma table's target. This replaces upstream's
`adjust_lut_for_bcm()` monotonicity nudge, which is no longer called. Lowering the
brightness shortens every window, so the low planes lose resolution first; run the panel
near full brightness for the best gradation and dim in software if needed.

## The light plan: set_light() below the brightness curve's floor (2026-09-27)

`Hub75Driver::set_light(light_q16)` asks for a share of the profile's full light
(16.16, 65536 = full) instead of a brightness, and `get_light_plan()` reports what was
done. Upstream's `set_brightness()` runs the user value through a quadratic curve floored
at `min_brightness_` (17 on a 64-wide panel: four clocks on the top plane) and the
output-enable windows are whole pixel clocks, so the panel had 59 distinct levels, the
lowest at 6.4 % of full light and the bottom ones 25 % apart. `p64bcm::plan_light()`
(p64_bcm.h, host-tested) takes the smallest output-enable level at or above the floor
whose light reaches the request and scales the LUT's targets down for the rest
(`fit_lut()` gained a `scale_q16` argument): above the floor every request lands within a
clock with the scale between about 0.75 and 1; below the floor the level stays at the
floor and the scale alone dims, one bit of tonal depth per halving (the codes left are
the low planes' short pulses). `set_brightness()` returns to the curve. The plan is
recomputed inside `set_brightness_oe()`, so a refresh profile switch replans the same
share of the new profile's full light. p64's `Display::set_brightness()` maps its 1..255
(perceived lightness, spec 3.2) to a share of light and calls `set_light()`.

## HUB75_MIN_REFRESH_RATE range (Kconfig)

Raised from 30-240 to 30-2000 Hz: with binary-weighted low planes a high minimum is a
usable setting (p64 runs 250, which selects transition bit 4 and 271 Hz on a 64x64
panel at 20 MHz).

## GDMA channel priority (Kconfig HUB75_GDMA_PRIORITY, gdma_dma.cpp)

The panel's GDMA transmit channel gets the arbitration priority `HUB75_GDMA_PRIORITY`
(0-5, default 0 = upstream behaviour) right after `gdma_connect()`, and
`Hub75Driver::set_dma_priority()` / `get_dma_priority()` change and read it at
runtime. The panel's FIFO has no back-pressure: when other GDMA users (the hardware
AES/SHA engines during TLS) burst at the same priority, the panel channel can be
starved and the refresh stalls for good. See p64's README for the measurements.

## GDMA channel id getter (hub75.h, platform_dma.h, gdma_dma.h/.cpp)

`Hub75Driver::get_dma_channel_id()` returns the GDMA channel the panel streams on (-1 on
other platforms or before `begin()`), from `gdma_get_channel_id()`. p64's display layer
watches that channel's registers for frame boundaries; it used to scan the channels for
the LCD peripheral selector, which found a stale channel after a driver restart (the
panel mode switch re-creates the driver) because the old channel's selector still read
"LCD".

## Refresh profile changed in place (hub75.h, platform_dma.h, gdma_dma.h/.cpp)

`Hub75Driver::set_refresh_profile(planes, min_hz)` (and `set_min_refresh_rate(hz)`,
which keeps the plane count) changes the number of bit planes sent and the minimum
refresh rate without tearing the driver down: the DMA stops, the transition bit is
recomputed, the output-enable windows and the LUT are refitted, the descriptor chains
are rebuilt for the new transmission pattern, and the DMA restarts on the same channel
with the pixel data still in the row buffers (the caller redraws: those codes were made
with the previous LUT). The row buffers always hold the compile-time depth; a smaller
`planes` leaves the top planes out of the chain and the LUT produces codes of that many
bits (the compile-time gamma table is normalised by its own maximum).
`get_bit_planes()` reports the count in force. p64 switches between Quality (10 planes,
250 Hz minimum -> 271 Hz) and Photo (8 planes, 600 Hz minimum -> 814 Hz) this way.
Re-creating the driver (`end()` then a new `begin()`) leaves the DMA stalled on this
board: the descriptor pointer never moves again, in either mode.

The descriptor arrays are allocated once, at the size of the first chain built, and
every later profile is rebuilt inside them; a profile needing more descriptors is
refused before the DMA is touched, and a rebuild that fails anyway restores the
previous profile and restarts the DMA. The first version freed and re-allocated the
arrays (2 x 13.8 KB of internal DMA memory for ten planes) on every switch; after hours
of uptime the second block was not available, the rebuild failed with the DMA stopped
and the panel stayed dark until a reboot (2026-09-20).

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

## The plane arithmetic in p64_bcm.h; the LUT fit skips blanked planes (2026-09-22)

The output-enable windows of a refresh profile (the halving below the transition bit, the
one-clock fallback, the blanking of a plane that would break the superincreasing
weights) and the LUT fit moved unchanged into `src/platforms/gdma/p64_bcm.h`, a
header without ESP-IDF includes that `GdmaDma` calls and the firmware's host tests
compile (`firmware/tests/host/unit/bcm.cpp`: every profile from 6 to 10 planes, every
transition bit, every brightness).

Those tests showed the blanking of 2026-09-20 was not enough on its own: with a plane
blanked (weight 0) the fit still walked every code in order and stopped at the first
code made of the blanked plane alone, which weighs less than the code before it; ten
planes at transition bit 6 still fitted 3 codes. The fit now walks only the codes built
from lit planes. Quality (10 planes, bit 4) and Photo (8 planes, bit 4) never blank a
plane, so their tables are unchanged (229 and 179 distinct codes at full brightness, in
the driver's log on the device and in the test).
