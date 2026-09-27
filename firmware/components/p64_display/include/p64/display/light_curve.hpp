// p64 -- the brightness scale (spec 3.2, since 2026-09-27): the user's 1..255 is
// perceived lightness, and this file turns it into the share of the panel's full light
// that the driver's light plan emits (`Hub75Driver::set_light`). Pure, host-tested.
//
// The eye's lightness goes roughly with the cube root of the light (CIE L*), so an even
// scale of lightness is L(v) = L1 + (v - 1) / 254 * (1 - L1) and the light is L^3.
// L1 is the cube root of the floor: brightness 1 emits kFloorLight of full, a
// sixteenth of the driver's own floor (127 of 1979 clocks, 6.4 %, four clocks on the
// top plane; below it the LUT scale dims and each halving costs a bit of depth, so a
// sixteenth leaves eight grey levels per channel, a night-light glow). 255 is full;
// 128 is about a fifth of the light, which looks about half as bright. Before this
// the number went to the driver's own curve, floored at 17: 1 to 7 were the same
// picture and the panel had 59 levels in all.
#pragma once

#include <cstdint>

namespace p64::display::light {

// Brightness 1 as a share of the full light: 127 / 1979 / 16.
constexpr double kFloorLight = 127.0 / 1979.0 / 16.0;

// The share of full light for a brightness, 16.16 (0 for 0, 65536 for 255).
uint32_t light_q16(uint8_t brightness);

// The lightness (0..1) of a brightness (0 for 0): the scale the knob steps along.
double lightness(uint8_t brightness);

}  // namespace p64::display::light
