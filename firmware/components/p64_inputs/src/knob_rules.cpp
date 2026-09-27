#include "p64/inputs/knob_rules.hpp"

namespace p64::inputs {

uint8_t brightness_after(uint8_t current, int32_t detents) {
  // The brightness number is perceived lightness (light_curve.hpp, spec 3.2), so a fixed
  // step is an even step to the eye: kDetentStep of 254 from 1 to 255, 37 detents each
  // way (the last one shorter), every one visible.
  long value = (current < 1 ? 1 : current) + static_cast<long>(detents) * kDetentStep;
  if (value > 255) value = 255;
  if (value < 1) value = 1;
  return static_cast<uint8_t>(value);
}

}  // namespace p64::inputs
