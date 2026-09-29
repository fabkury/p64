// p64 -- what the p64b knobs do (pure, host-tested): which knob carries which role, and
// the brightness steps a turn takes. Roles decided 2026-09-20: knob A (0x36) turns the
// brightness and its press pauses or resumes; knob B (0x37) turns next / previous and
// its press likes the Makapix artwork on the panel. `swap` exchanges the two.
#pragma once

#include <cstdint>

namespace p64::inputs {

enum class KnobRole : uint8_t { Brightness, Navigate };

// Knob index 0 = A, 1 = B.
constexpr KnobRole role_of(int knob, bool swap) {
  const bool first = knob == 0;
  return (first != swap) ? KnobRole::Brightness : KnobRole::Navigate;
}

// The brightness after `detents` (positive = up). The brightness number is perceived
// lightness (spec 3.2, since 2026-09-27: even in the number, the light its cube, 1 the
// software floor), so each detent adds or removes a fixed kDetentStep, even to the eye:
// 37 detents from 1 to 255 and 37 back, about a turn and a half of a 24-detent knob,
// clamped to 1..255. (Until 2026-09-27 a detent was 10 % of the value, and the seven
// lowest values were one picture on the driver's floored curve.)
constexpr int kDetentStep = 7;
uint8_t brightness_after(uint8_t current, int32_t detents);

// Whether a probe of the GPIO socket can find anything. The controller pulls IO45 and
// IO46 down with 10 k (R59/R60, docs/hardware); only a board's pull-ups raise them, so
// with the socket empty (every p64a) both lines idle low, and a probe there does not end
// in a quiet NACK: it times out, and the I2C driver prints an error line for each one
// (seen 2026-09-29, two lines every 5 s). Probe only while both lines idle high.
constexpr bool socket_idle_high(int sda_level, int scl_level) { return sda_level == 1 && scl_level == 1; }

}  // namespace p64::inputs
