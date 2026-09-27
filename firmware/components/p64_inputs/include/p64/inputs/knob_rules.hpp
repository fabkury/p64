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

// The brightness after `detents` (positive = up). Each detent moves about 10 % of the
// current value and at least 1, so the steps are even to the eye across 1..255 (the
// driver's curve is floored near 17/255, spec 3.2): 42 detents from 1 to 255, 34 from
// 255 down to 1.
uint8_t brightness_after(uint8_t current, int32_t detents);

}  // namespace p64::inputs
