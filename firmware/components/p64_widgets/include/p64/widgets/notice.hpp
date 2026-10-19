// p64 -- the brightness notice (p095, 2026-10-19): a sun stamped over whatever is on the
// panel for a moment when the p64b brightness knob is turned past its maximum (spec 9).
// Pure (host-tested): the sun is assets/clock/notice/sun.png (Retro Diffusion,
// assets/rd-source), baked as assets::kNoticeSun.
#pragma once

#include <cstdint>

#include "p64/gfx/frame.hpp"

namespace p64::widgets::notice {

// The sun centred on the frame at opacity `alpha` (0 = nothing drawn): a one-pixel ring
// of white at three quarters of `alpha` around its silhouette, so it reads over any
// artwork, then the sun's own pixels at `alpha`.
void draw_sun(gfx::Frame &frame, uint8_t alpha);

// The ring's opacity for a notice at `alpha` (three quarters).
constexpr uint8_t ring_alpha(uint8_t alpha) { return static_cast<uint8_t>(alpha * 3 / 4); }

}  // namespace p64::widgets::notice
